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

void sim_init(sim_t *s, const char *card_a, const char *card_b,
              const link_cfg_t *cfg, uint64_t seed)
{
    memset(s, 0, sizeof *s);

    if (cfg) s->cfg = *cfg; else link_cfg_default(&s->cfg);

    build_record(card_a, &s->rec_a, 11);
    build_record(card_b, &s->rec_b, 22);

    s->a_sends = ((seed & 1u) == 0u);

    halh_pair(&s->node_a, &s->node_b, &s->clock_us, seed);
    link_sm_init(&s->sm_a, &s->node_a.iface, &s->cfg, &s->rec_a);
    link_sm_init(&s->sm_b, &s->node_b.iface, &s->cfg, &s->rec_b);
}

/*
 * Quiet handed to the receiving end before the sender starts.
 *
 * Not a fudge, and not tuning. On a wrist the receiving end has been listening
 * for a whole window before a card arrives, so carrier.c's floor is primed on
 * ambient silence and the frame reads as three times that floor. Start both
 * ends in the same microsecond and the detector primes instead on the frame's
 * first preamble chip, which is a HIGH one: the floor sits at the carrier's own
 * level and, measured, never recovers inside the frame. Handover then decides
 * the channel is idle and talks over it. Costed at 650 frames for the same 50
 * handshakes against 450 with the lead-in — and the first frame of a contact,
 * which is the one a brief contact has.
 *
 * One millisecond is four chips; the floor primes on the first. It stays well
 * inside rx_idle_us, so the receiver cannot mistake the lead-in for the far end
 * having gone quiet.
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
