#include <string.h>

#include "frag.h"
#include "hf_test.h"
#include "tests.h"
#include "vcard.h"

static const char k_card[] =
    "BEGIN:VCARD\r\nVERSION:3.0\r\n"
    "N:Lovelace;Ada;;;\r\n"
    "FN:Ada Lovelace\r\n"
    "ORG:Analytical Engines Ltd\r\n"
    "TITLE:Programmer\r\n"
    "TEL;TYPE=CELL:+44 7700 900123\r\n"
    "EMAIL;TYPE=INTERNET:ada@gmail.com\r\n"
    "URL:https://example.com/ada\r\n"
    "END:VCARD\r\n";

static size_t build_blob(uint8_t *blob, size_t max)
{
    compact_rec_t r;
    size_t len = 0;
    vcard_parse(k_card, strlen(k_card), &r);
    compact_sort_priority(&r);
    compact_encode_chunked(&r, HANDOFF_FRAG_PAYLOAD - 2u, blob, max, &len);
    return len;
}

/* A note long enough to be a head and two continuations: 30 + 30 + 10. */
static const char k_long[] =
    "BEGIN:VCARD\r\nVERSION:3.0\r\n"
    "FN:Ada Lovelace\r\n"
    "TEL;TYPE=CELL:+44 7700 900123\r\n"
    "EMAIL;TYPE=INTERNET:ada@gmail.com\r\n"
    "NOTE:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\r\n"
    "END:VCARD\r\n";

static void split_card(const char *card, frag_tx_t *t)
{
    compact_rec_t r;
    uint8_t blob[COMPACT_MAX_BLOB];
    size_t len = 0;
    vcard_parse(card, strlen(card), &r);
    compact_sort_priority(&r);
    compact_encode_chunked(&r, HANDOFF_FRAG_PAYLOAD - 2u, blob, sizeof blob, &len);
    frag_split(blob, len, 1, t);
}

/* Short, never wrong: every field in `part` is in `whole`, byte for byte. */
static void check_never_wrong(const compact_rec_t *part, const compact_rec_t *whole)
{
    uint8_t i;
    for (i = 0; i < part->n; i++) {
        const compact_field_t *w = compact_find(whole, part->f[i].tag);
        HF_CHECK_MSG(w != NULL, "tag 0x%02X is not in the card", part->f[i].tag);
        if (!w) continue;
        HF_CHECK_MSG(w->len == part->f[i].len &&
                     memcmp(w->val, part->f[i].val, w->len) == 0,
                     "tag 0x%02X arrived wrong", part->f[i].tag);
    }
}

/* Deliver a set of fragments, by index, and return what decodes. */
static void deliver(frag_rx_t *rx, const frag_tx_t *tx, const uint8_t *idx, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        const uint8_t *p = NULL;
        const size_t len = frag_get(tx, idx[i], &p);
        frame_hdr_t h;
        h.frag_index = idx[i];
        h.frag_count = tx->count;
        h.record_id = tx->record_id;
        h.flags = 0;
        HF_EQ_INT(frag_rx_add(rx, &h, p, len), FRAG_OK);
    }
}

void test_frag(void)
{
    uint8_t blob[COMPACT_MAX_BLOB];
    size_t blen = build_blob(blob, sizeof blob);

    hf_begin("frag: a realistic card needs a handful of fragments");
    {
        frag_tx_t t;
        HF_CHECK(blen > 0);
        HF_EQ_INT(frag_split(blob, blen, 5, &t), FRAG_OK);
        HF_CHECK_MSG(t.count >= 2 && t.count <= 6,
                     "%u fragments for %u bytes", t.count, (unsigned)blen);
        HF_EQ_INT(t.record_id, 5);
    }

    /*
     * The property architecture §8.4 depends on: every fragment holds whole
     * TLVs, so any one of them decodes on its own. Without this, "250 ms gives
     * you a name and a mobile" is not true.
     */
    hf_begin("frag: every fragment decodes standalone");
    {
        frag_tx_t t;
        uint8_t i;

        frag_split(blob, blen, 1, &t);
        for (i = 0; i < t.count; i++) {
            const uint8_t *p = NULL;
            const size_t len = frag_get(&t, i, &p);
            compact_rec_t r;
            size_t consumed = 0;

            HF_CHECK_MSG(len > 0, "fragment %u is empty", i);
            HF_EQ_INT(compact_decode_ex(p, len, &r, &consumed), COMPACT_OK);

            /*
             * The property that matters is that the fragment parses cleanly to
             * its end — no TLV cut in half. It may still yield no FIELDS: a
             * fragment holding only TAG_CONT chunks of an over-long value is
             * legitimately useless without its head, and compact_decode drops
             * those rather than inventing a field.
             */
            HF_CHECK_MSG(consumed == len, "fragment %u left %u bytes unparsed",
                         i, (unsigned)(len - consumed));
            if (p[0] != TAG_CONT)
                HF_CHECK_MSG(r.n > 0, "fragment %u decoded to no fields", i);
        }
    }

    hf_begin("frag: fragment 0 alone is a usable contact");
    {
        frag_tx_t t;
        frag_rx_t rx;
        const uint8_t only0[] = { 0 };
        compact_rec_t r;
        char card[512];
        size_t n = 0;

        frag_split(blob, blen, 1, &t);
        frag_rx_init(&rx);
        deliver(&rx, &t, only0, 1);

        HF_CHECK(!frag_rx_complete(&rx));
        HF_EQ_INT(compact_decode(t.data, t.len[0], &r), COMPACT_OK);
        HF_CHECK_MSG(compact_find(&r, TAG_FN) != NULL, "no name in fragment 0");
        HF_CHECK_MSG(compact_find(&r, TAG_TEL) != NULL, "no mobile in fragment 0");

        HF_EQ_INT(vcard_render(&r, card, sizeof card, &n), COMPACT_OK);
        HF_CHECK(strstr(card, "FN:Ada Lovelace") != NULL);
        HF_CHECK(strstr(card, "TEL;TYPE=CELL:+447700900123") != NULL);
    }

    hf_begin("frag: complete reassembly reproduces the card");
    {
        frag_tx_t t;
        frag_rx_t rx;
        uint8_t order[FRAME_MAX_FRAGS];
        const uint8_t *got = NULL;
        compact_rec_t r;
        char card[1024];
        size_t n = 0, glen;
        uint8_t i;

        frag_split(blob, blen, 9, &t);
        for (i = 0; i < t.count; i++) order[i] = i;

        frag_rx_init(&rx);
        deliver(&rx, &t, order, t.count);
        HF_CHECK(frag_rx_complete(&rx));
        HF_EQ_INT(frag_rx_missing(&rx), 0);

        glen = frag_rx_blob(&rx, &got);
        HF_EQ_INT(compact_decode(got, glen, &r), COMPACT_OK);
        HF_EQ_INT(vcard_render(&r, card, sizeof card, &n), COMPACT_OK);

        HF_CHECK(strstr(card, "FN:Ada Lovelace\r\n") != NULL);
        HF_CHECK(strstr(card, "ORG:Analytical Engines Ltd\r\n") != NULL);
        HF_CHECK(strstr(card, "TITLE:Programmer\r\n") != NULL);
        HF_CHECK(strstr(card, "URL:https://example.com/ada\r\n") != NULL);
        HF_CHECK(strstr(card, "EMAIL;TYPE=INTERNET:ada@gmail.com\r\n") != NULL);
    }

    /* The carousel delivers 0, 2, 1, ... — order must not matter. */
    hf_begin("frag: out-of-order arrival reassembles identically");
    {
        frag_tx_t t;
        frag_rx_t a, b;
        uint8_t fwd[FRAME_MAX_FRAGS], rev[FRAME_MAX_FRAGS];
        const uint8_t *ga = NULL, *gb = NULL;
        size_t la, lb;
        uint8_t i;

        frag_split(blob, blen, 3, &t);
        for (i = 0; i < t.count; i++) { fwd[i] = i; rev[i] = (uint8_t)(t.count - 1u - i); }

        frag_rx_init(&a); deliver(&a, &t, fwd, t.count);
        frag_rx_init(&b); deliver(&b, &t, rev, t.count);

        la = frag_rx_blob(&a, &ga);
        lb = frag_rx_blob(&b, &gb);
        HF_EQ_INT(lb, la);
        HF_EQ_MEM(gb, ga, la);
    }

    /*
     * A missing middle fragment must cost exactly one fragment's worth of
     * fields, not resynchronise the whole blob. This is the difference between
     * a sparse contact and a failed transfer.
     */
    hf_begin("frag: a missing middle fragment costs only its own fields");
    {
        frag_tx_t t;
        frag_rx_t rx;
        uint8_t some[FRAME_MAX_FRAGS];
        const uint8_t *got = NULL;
        compact_rec_t r;
        size_t glen;
        uint8_t i, n = 0;

        frag_split(blob, blen, 2, &t);
        HF_CHECK(t.count >= 3);
        for (i = 0; i < t.count; i++) if (i != 1) some[n++] = i;

        frag_rx_init(&rx);
        deliver(&rx, &t, some, n);
        HF_CHECK(!frag_rx_complete(&rx));
        HF_EQ_INT(frag_rx_missing(&rx), 1);

        glen = frag_rx_blob(&rx, &got);
        HF_EQ_INT(compact_decode(got, glen, &r), COMPACT_OK);
        HF_CHECK_MSG(compact_find(&r, TAG_FN) != NULL,
                     "losing fragment 1 destroyed fragment 0's name");
        HF_CHECK(r.n > 0);
    }

    /*
     * The partial card a cut-short contact hands to the phone. Every subset of
     * a card with a three-way split field: never a field that differs from the
     * card, the name whenever fragment 0 came, and nothing when it did not.
     */
    hf_begin("frag: a partial card is short, never wrong");
    {
        frag_tx_t t;
        frag_rx_t rx;
        uint8_t all[FRAME_MAX_FRAGS], out[FRAME_MAX_FRAGS * HANDOFF_FRAG_PAYLOAD];
        const uint8_t *got = NULL;
        compact_rec_t whole, part;
        unsigned mask, note = 0;
        uint8_t i;

        split_card(k_long, &t);
        for (i = 0; i < t.count; i++) {
            const uint8_t tag = t.data[(size_t)i * HANDOFF_FRAG_PAYLOAD];
            if (tag == TAG_NOTE || tag == TAG_CONT) note |= 1u << i;
            all[i] = i;
        }
        HF_CHECK_MSG(note == 0x0Eu, "note in fragments 0x%02X, the test wants 1-3", note);
        frag_rx_init(&rx);
        deliver(&rx, &t, all, t.count);
        {
            const size_t glen = frag_rx_blob(&rx, &got);
            HF_EQ_INT(compact_decode(got, glen, &whole), COMPACT_OK);
        }
        HF_CHECK(compact_find(&whole, TAG_NOTE) != NULL);

        for (mask = 1; mask < (1u << t.count); mask++) {
            uint8_t some[FRAME_MAX_FRAGS], n = 0;
            size_t plen;

            for (i = 0; i < t.count; i++) if (mask & (1u << i)) some[n++] = i;
            frag_rx_init(&rx);
            deliver(&rx, &t, some, n);

            plen = frag_rx_partial(&rx, out, sizeof out);
            if (!(mask & 1u)) {
                HF_CHECK_MSG(plen == 0, "mask 0x%02X: a card with no name", mask);
                continue;
            }
            HF_EQ_INT(compact_decode(out, plen, &part), COMPACT_OK);
            HF_CHECK_MSG(compact_find(&part, TAG_FN) != NULL, "mask 0x%02X: no name", mask);
            check_never_wrong(&part, &whole);
            /* The note arrives whole or not at all: all three pieces. */
            HF_CHECK_MSG((compact_find(&part, TAG_NOTE) != NULL) == ((mask & note) == note),
                         "mask 0x%02X: note kept wrongly", mask);
        }
    }

    hf_begin("frag: a partial card with every fragment is the whole card");
    {
        frag_tx_t t;
        frag_rx_t rx;
        uint8_t all[FRAME_MAX_FRAGS], out[FRAME_MAX_FRAGS * HANDOFF_FRAG_PAYLOAD];
        const uint8_t *got = NULL;
        size_t glen;
        uint8_t i;

        split_card(k_long, &t);
        for (i = 0; i < t.count; i++) all[i] = i;
        frag_rx_init(&rx);
        deliver(&rx, &t, all, t.count);
        glen = frag_rx_blob(&rx, &got);
        HF_EQ_INT(frag_rx_partial(&rx, out, sizeof out), glen);
        HF_EQ_MEM(out, got, glen);
    }

    hf_begin("frag: a re-provisioned record id restarts reassembly");
    {
        frag_tx_t t1, t2;
        frag_rx_t rx;
        const uint8_t one[] = { 0 };

        frag_split(blob, blen, 4, &t1);
        frag_split(blob, blen, 5, &t2);

        frag_rx_init(&rx);
        deliver(&rx, &t1, one, 1);
        HF_EQ_INT(rx.record_id, 4);
        deliver(&rx, &t2, one, 1);
        HF_EQ_INT(rx.record_id, 5);
        /* Only the new record's fragment 0 is held, not a merge of both. */
        HF_EQ_INT(rx.have, 1);
    }

    hf_begin("frag: a bad index is refused");
    {
        frag_tx_t t;
        frag_rx_t rx;
        frame_hdr_t h = { 5, 2, 0, 0 };
        uint8_t p[HANDOFF_FRAG_PAYLOAD];

        frag_split(blob, blen, 0, &t);
        frag_rx_init(&rx);
        memset(p, 0, sizeof p);
        HF_EQ_INT(frag_rx_add(&rx, &h, p, sizeof p), FRAG_ERR_BAD_INDEX);
    }

    hf_begin("frag: a blob too large for the frame count is refused");
    {
        uint8_t big[FRAME_MAX_FRAGS * HANDOFF_FRAG_PAYLOAD * 2];
        frag_tx_t t;
        size_t i;

        /* Fill with the largest TLVs a fragment can hold, so the count is
         * exactly the number of TLVs. */
        for (i = 0; i + HANDOFF_FRAG_PAYLOAD <= sizeof big; i += HANDOFF_FRAG_PAYLOAD) {
            big[i] = TAG_NOTE;
            big[i + 1] = (uint8_t)(HANDOFF_FRAG_PAYLOAD - 2u);
            memset(big + i + 2, 'x', HANDOFF_FRAG_PAYLOAD - 2u);
        }
        HF_EQ_INT(frag_split(big, sizeof big, 0, &t), FRAG_ERR_TOO_BIG);
    }
}
