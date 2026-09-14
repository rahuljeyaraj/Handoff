/*
 * The wristband's own record, and its persistence. architecture §9.
 *
 * Development plan M2: "provisioning round-trips: write your own card from the
 * app, power-cycle the Pico, read it back unchanged." The power cycle is a
 * board test, but everything either side of it is logic, and it is tested here
 * against a fake backend so that a failure on the bench has one candidate
 * cause — the flash driver — instead of two.
 */
#include <string.h>

#include "hf_test.h"
#include "store.h"
#include "tests.h"
#include "vcard.h"

/* ---- a backend that is RAM, so a "power cycle" is a function call ------ */

static uint8_t g_blob[COMPACT_MAX_BLOB];
static size_t  g_len;
static uint8_t g_id;
static bool    g_haptic;
static bool    g_present;
static bool    g_fail_writes;
static int     g_saves;

static bool fake_load(uint8_t *blob, size_t max, size_t *len, uint8_t *record_id,
                      bool *haptic_on)
{
    if (!g_present || g_len > max) return false;
    memcpy(blob, g_blob, g_len);
    *len = g_len;
    *record_id = g_id;
    *haptic_on = g_haptic;
    return true;
}

static bool fake_save(const uint8_t *blob, size_t len, uint8_t record_id,
                      bool haptic_on)
{
    g_saves++;
    if (g_fail_writes || len > sizeof g_blob) return false;
    memcpy(g_blob, blob, len);
    g_len = len;
    g_id = record_id;
    g_haptic = haptic_on;
    g_present = true;
    return true;
}

static bool fake_erase(void)
{
    if (g_fail_writes) return false;
    g_present = false;
    g_len = 0;
    return true;
}

static const store_backend_t k_fake = { fake_load, fake_save, fake_erase };

static void fake_reset(void)
{
    memset(g_blob, 0, sizeof g_blob);
    g_len = 0;
    g_id = 0;
    g_haptic = false;
    g_present = false;
    g_fail_writes = false;
    g_saves = 0;
}

/* ---------------------------------------------------------------------- */

static const char k_card[] =
    "BEGIN:VCARD\r\n"
    "VERSION:3.0\r\n"
    "N:Lovelace;Ada;;;\r\n"
    "FN:Ada Lovelace\r\n"
    "ORG:Analytical Engines Ltd\r\n"
    "TEL;TYPE=CELL:+44 7700 900123\r\n"
    "EMAIL;TYPE=INTERNET:ada@gmail.com\r\n"
    "END:VCARD\r\n";

static const char k_other[] =
    "BEGIN:VCARD\r\n"
    "VERSION:3.0\r\n"
    "FN:Bo Tester\r\n"
    "TEL;TYPE=CELL:+1 555 0199\r\n"
    "END:VCARD\r\n";

static void no_backend_fails_loudly(void)
{
    store_t s;

    store_set_backend(NULL);
    store_init(&s);
    HF_EQ_INT(store_put_vcard(&s, k_card, strlen(k_card)), STORE_OK);

    /*
     * The M1 state, and it must never look like success. A store that
     * silently pretends to persist is how a wristband ends up transmitting
     * someone else's card after a reset.
     */
    HF_EQ_INT(store_save(&s), STORE_ERR_BACKEND);
    HF_EQ_INT(store_load(&s), STORE_ERR_BACKEND);
    HF_EQ_INT(store_forget(&s), STORE_ERR_BACKEND);
}

static void provisioning_round_trips_across_a_power_cycle(void)
{
    store_t s, after;
    const uint8_t *blob = NULL, *blob2 = NULL;
    size_t len = 0, len2 = 0;

    fake_reset();
    store_set_backend(&k_fake);

    store_init(&s);
    HF_EQ_INT(store_put_vcard(&s, k_card, strlen(k_card)), STORE_OK);
    HF_EQ_INT(store_save(&s), STORE_OK);
    HF_EQ_INT(store_get(&s, &blob, &len), STORE_OK);
    HF_CHECK(len > 0);

    /* The power cycle: everything in RAM is gone. */
    store_init(&after);
    HF_EQ_INT(store_get(&after, &blob2, &len2), STORE_ERR_EMPTY);

    HF_EQ_INT(store_load(&after), STORE_OK);
    HF_EQ_INT(store_get(&after, &blob2, &len2), STORE_OK);
    HF_EQ_INT(len2, len);
    if (len2 == len) HF_EQ_MEM(blob2, blob, len);
}

static void the_record_id_survives_the_power_cycle(void)
{
    store_t s, after;
    uint8_t id;

    fake_reset();
    store_set_backend(&k_fake);

    store_init(&s);
    HF_EQ_INT(store_put_vcard(&s, k_card, strlen(k_card)), STORE_OK);
    HF_EQ_INT(store_put_vcard(&s, k_other, strlen(k_other)), STORE_OK);
    id = store_record_id(&s);
    HF_EQ_INT(id, 2);                       /* one bump per put */
    HF_EQ_INT(store_save(&s), STORE_OK);

    /*
     * The reason this matters: frag_rx_add keys reassembly on record_id, so a
     * wristband that rebooted between two frames of one exchange must not
     * claim to still be sending the record it was sending before the reboot.
     * Restarting the counter at zero would do exactly that.
     */
    store_init(&after);
    HF_EQ_INT(store_load(&after), STORE_OK);
    HF_EQ_INT(store_record_id(&after), id);

    /* And re-provisioning continues from there rather than from zero. */
    HF_EQ_INT(store_put_vcard(&after, k_card, strlen(k_card)), STORE_OK);
    HF_EQ_INT(store_record_id(&after), (id + 1) & FRAME_MAX_RECORD_ID);
}

static void the_record_id_wraps_within_the_header_field(void)
{
    store_t s;
    int i;

    fake_reset();
    store_set_backend(&k_fake);
    store_init(&s);

    /* frame.h gives record_id six bits. 64 puts is one full lap. */
    for (i = 0; i < FRAME_MAX_RECORD_ID + 1; i++)
        HF_EQ_INT(store_put(&s, (const uint8_t *)"x", 1), STORE_OK);

    HF_EQ_INT(store_record_id(&s), 0);
    HF_CHECK(store_record_id(&s) <= FRAME_MAX_RECORD_ID);
}

static void an_empty_backend_reports_empty_not_broken(void)
{
    store_t s;

    fake_reset();
    store_set_backend(&k_fake);

    /* A wristband out of the box has never been provisioned. That is a normal
     * state the app has to be told about, and it is not the same as a backend
     * that is not there — which is why they are different errors. */
    store_init(&s);
    HF_EQ_INT(store_load(&s), STORE_ERR_EMPTY);
    HF_EQ_INT(store_get(&s, NULL, NULL), STORE_ERR_EMPTY);
}

static void a_failed_write_is_reported(void)
{
    store_t s;

    fake_reset();
    store_set_backend(&k_fake);

    store_init(&s);
    HF_EQ_INT(store_put_vcard(&s, k_card, strlen(k_card)), STORE_OK);

    g_fail_writes = true;
    HF_EQ_INT(store_save(&s), STORE_ERR_BACKEND);
    HF_EQ_INT(g_saves, 1);

    /* Nothing to save is not the same as failing to save it. */
    g_fail_writes = false;
    store_init(&s);
    HF_EQ_INT(store_save(&s), STORE_ERR_EMPTY);
    HF_EQ_INT(g_saves, 1);
}

static void forgetting_clears_both_halves(void)
{
    store_t s;

    fake_reset();
    store_set_backend(&k_fake);

    store_init(&s);
    HF_EQ_INT(store_put_vcard(&s, k_card, strlen(k_card)), STORE_OK);
    HF_EQ_INT(store_save(&s), STORE_OK);

    HF_EQ_INT(store_forget(&s), STORE_OK);
    HF_EQ_INT(store_get(&s, NULL, NULL), STORE_ERR_EMPTY);
    HF_EQ_INT(store_load(&s), STORE_ERR_EMPTY);
}

static void haptic_defaults_on_and_survives_a_power_cycle(void)
{
    store_t s, after;

    fake_reset();
    store_set_backend(&k_fake);

    /* review O5: the band defaults to on, with nothing loaded yet. */
    store_init(&s);
    HF_CHECK(store_haptic_on(&s));

    /* Turning it off has nowhere to go until a card exists (review O5's
     * documented limitation) — the RAM value still changes, but nothing
     * persists. */
    store_set_haptic(&s, false);
    HF_CHECK(!store_haptic_on(&s));
    HF_EQ_INT(store_save(&s), STORE_ERR_EMPTY);

    /* Once a card exists, the preference rides along with it. */
    HF_EQ_INT(store_put_vcard(&s, k_card, strlen(k_card)), STORE_OK);
    HF_EQ_INT(store_save(&s), STORE_OK);

    store_init(&after);
    HF_CHECK(store_haptic_on(&after));   /* the fresh default, pre-load */
    HF_EQ_INT(store_load(&after), STORE_OK);
    HF_CHECK(!store_haptic_on(&after));  /* the persisted off survives reload */
}

static void what_is_stored_is_the_compact_form_not_the_text(void)
{
    store_t s;
    compact_rec_t rec;
    const uint8_t *blob = NULL;
    size_t len = 0;
    char back[512];
    size_t back_len = 0;

    fake_reset();
    store_set_backend(&k_fake);

    store_init(&s);
    HF_EQ_INT(store_put_vcard(&s, k_card, strlen(k_card)), STORE_OK);
    HF_EQ_INT(store_get(&s, &blob, &len), STORE_OK);

    /* architecture §9: the encoding cost is paid once at provisioning, not at
     * every handshake. If this ever stops holding, the blob will be about as
     * long as the card. */
    HF_CHECK_MSG(len < strlen(k_card), "compact blob %u bytes vs %u of text",
                 (unsigned)len, (unsigned)strlen(k_card));

    /* And it is still a card at the far end — priority-ordered, so fragment 0
     * alone is a usable contact. */
    HF_EQ_INT(compact_decode(blob, len, &rec), COMPACT_OK);
    HF_CHECK(compact_find(&rec, TAG_FN) != NULL);
    HF_CHECK(compact_find(&rec, TAG_TEL) != NULL);
    HF_EQ_INT(rec.f[0].tag, TAG_FN);
    HF_EQ_INT(vcard_render(&rec, back, sizeof back, &back_len), COMPACT_OK);
    HF_CHECK(strstr(back, "FN:Ada Lovelace") != NULL);
    HF_CHECK(strstr(back, "gmail.com") != NULL);
}

void test_store(void)
{
    hf_begin("store: no backend fails loudly");
    no_backend_fails_loudly();

    hf_begin("store: provisioning round-trips across a power cycle");
    provisioning_round_trips_across_a_power_cycle();

    hf_begin("store: the record id survives the power cycle");
    the_record_id_survives_the_power_cycle();

    hf_begin("store: the record id wraps inside its header field");
    the_record_id_wraps_within_the_header_field();

    hf_begin("store: an unprovisioned wristband reports empty, not broken");
    an_empty_backend_reports_empty_not_broken();

    hf_begin("store: a failed write is reported");
    a_failed_write_is_reported();

    hf_begin("store: forgetting clears RAM and flash together");
    forgetting_clears_both_halves();

    hf_begin("store: haptic defaults on and survives a power cycle");
    haptic_defaults_on_and_survives_a_power_cycle();

    hf_begin("store: what is stored is the compact form, not the text");
    what_is_stored_is_the_compact_form_not_the_text();

    store_set_backend(NULL);
}
