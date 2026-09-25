#include "sim_twonode.h"

#include <string.h>

#include "store.h"
#include "vcard.h"

static void build_record(const char *card, frag_tx_t *out, uint8_t record_id)
{
    store_t st;
    const uint8_t *blob = NULL;
    size_t len = 0;

    store_init(&st);
    st.record_id = (uint8_t)(record_id - 1u);   /* store_put bumps it */
    store_put_vcard(&st, card, strlen(card));
    store_get(&st, &blob, &len);
    frag_split(blob, len, store_record_id(&st), out);
}

/*
 * THE ROOM, BEFORE ANYBODY ARRIVES. Link v2 step 5.
 *
 * dsp/presence.c answers nothing until its CFAR reference holds
 * HANDOFF_CFAR_CELLS guard windows — one preamble of airtime, by derivation.
 * A band on a wrist filled that in the first eight milliseconds after boot and
 * has held it ever since; a simulator that builds two fresh nodes per contact
 * has not, so without this every contact begins against a detector that is
 * still deaf and the receiving end walks away before the preamble decodes.
 *
 * This cannot be done by lengthening the lead-in above, because the lead-in is
 * capped by rx_idle_us at six milliseconds and the reference wants eight. So
 * the channel is run BEFORE either end is armed, which is what actually
 * happened on the wrist.
 *
 * Twice the reference, so the boxcar is full and settled rather than exactly
 * full on the last chip.
 */
#define SIM_ROOM_CHIPS (2u * (unsigned)HANDOFF_CFAR_REF_CHIPS)

void sim_init(sim_t *s, const char *card_a, const char *card_b,
              const link_cfg_t *cfg, uint64_t seed)
{
    unsigned i;

    memset(s, 0, sizeof *s);

    if (cfg) s->cfg = *cfg; else link_cfg_default(&s->cfg);

    build_record(card_a, &s->rec_a, 11);
    build_record(card_b, &s->rec_b, 22);

    s->a_sends = ((seed & 1u) == 0u);

    halh_pair(&s->node_a, &s->node_b, &s->clock_us, seed);

    /* Let the room exist before anybody arrives — see SIM_ROOM_CHIPS. Here
     * rather than in sim_run(), because several tests drive the two state
     * machines themselves and every one of them needs it. */
    for (i = 0; i < SIM_ROOM_CHIPS; i++)
        halh_advance(&s->node_a, &s->node_b, HANDOFF_CHIP_US);

    link_sm_init(&s->sm_a, &s->node_a.iface, &s->cfg, &s->rec_a);
    link_sm_init(&s->sm_b, &s->node_b.iface, &s->cfg, &s->rec_b);
}

/*
 * Quiet handed to the receiving end before the sender starts.
 *
 * Not a fudge, and not tuning. On a wrist the receiving end has been listening
 * for a whole window before a card arrives. Start both ends in the same
 * microsecond and the receiver meets the frame with a detector that has never
 * seen the channel.
 *
 * It stays well inside rx_idle_us, so the receiver cannot mistake the lead-in
 * for the far end having gone quiet — which is what bounds it, and which is
 * why the room below is settled separately rather than by making this longer.
 */
#define SIM_RX_PRIME_US 1000u


sim_result_t sim_run(sim_t *s, uint64_t contact_us)
{
    sim_result_t r;
    const uint64_t step = HANDOFF_CHIP_US;
    link_sm_t *const tx = s->a_sends ? &s->sm_a : &s->sm_b;
    link_sm_t *const rx = s->a_sends ? &s->sm_b : &s->sm_a;
    bool sending = false;
    uint64_t t;

    memset(&r, 0, sizeof r);

    link_sm_begin(rx, s->clock_us, LINK_ROLE_RECEIVER);

    for (t = 0; t < contact_us; t += step) {
        link_state_t sa, sb;

        if (!sending && t >= SIM_RX_PRIME_US) {
            link_sm_begin(tx, s->clock_us, LINK_ROLE_SENDER);
            sending = true;
        }

        sa = link_sm_poll(&s->sm_a, s->clock_us);
        sb = link_sm_poll(&s->sm_b, s->clock_us);

        if (sending &&
            (sa == LINK_COMPLETE || sa == LINK_ABORT) &&
            (sb == LINK_COMPLETE || sb == LINK_ABORT))
            break;

        halh_advance(&s->node_a, &s->node_b, step);
    }

    r.duration_us = t;
    r.a_complete = (s->sm_a.state == LINK_COMPLETE);
    r.b_complete = (s->sm_b.state == LINK_COMPLETE);
    r.a_has_b = frag_rx_complete(&s->sm_a.rx);
    r.b_has_a = frag_rx_complete(&s->sm_b.rx);
    r.frames_a = s->sm_a.frames_rx_good;
    r.frames_b = s->sm_b.frames_rx_good;
    r.bad_crc_a = s->sm_a.frames_rx_bad;
    r.bad_crc_b = s->sm_b.frames_rx_bad;
    r.role_a = link_sm_role(&s->sm_a);
    r.role_b = link_sm_role(&s->sm_b);
    return r;
}

static size_t render_received(const link_sm_t *sm, char *out, size_t max)
{
    const uint8_t *blob = NULL;
    const size_t len = link_sm_received(sm, &blob);
    compact_rec_t rec;
    size_t n = 0;

    if (!len) { if (max) out[0] = '\0'; return 0; }
    if (compact_decode(blob, len, &rec) != COMPACT_OK) { if (max) out[0] = '\0'; return 0; }
    if (vcard_render(&rec, out, max, &n) != COMPACT_OK) { if (max) out[0] = '\0'; return 0; }
    return n;
}

size_t sim_card_a_sees(const sim_t *s, char *out, size_t max)
{
    return render_received(&s->sm_a, out, max);
}

size_t sim_card_b_sees(const sim_t *s, char *out, size_t max)
{
    return render_received(&s->sm_b, out, max);
}
