/*
 * Handoff — the contact trigger. beacon.h, docs/simple-trigger-spec.md §6.
 *
 * Two claims are on trial here and they are not the same claim.
 *
 * RENDEZVOUS: two bands free-running at arbitrary relative phase find each
 * other within a bounded time of touching. A test that tried one phase would
 * pass on luck, so the phase is swept across a whole worst-case cycle at 1 ms
 * resolution.
 *
 * NO TIE: over that same sweep, the pair never ends up both-sending or
 * both-listening. Spec §2 argues that from the timing geometry — your ears
 * open 11 ms after your own shout starts, so the earlier shouter is always too
 * late and the later one is always in time. That argument has not been
 * machine-checked and this file is what turns it into evidence. It is the most
 * important test here.
 */
#include <stdio.h>
#include <string.h>

#include "beacon.h"
#include "hal_host.h"
#include "hf_test.h"
#include "link_sm.h"
#include "tests.h"

/* Fine enough that no shout, settle or detector latency can be stepped over. */
#define STEP_US 100u

/* One whole cycle at its longest: shout, settle, and the longest listen the
 * draw can produce. This is the window the phase sweep covers. */
#define CYCLE_MAX_US (HANDOFF_SHOUT_US + HANDOFF_TRIG_SETTLE_US + \
                      HANDOFF_LISTEN_MAX_US)

/*
 * The bound rendezvous must meet. Four worst-case cycles: one to notice the
 * other band, and three of slack for the simultaneous-shout case of §4.1 to
 * decorrelate. The sweep prints what it actually took, and the printed number
 * is the one to believe — this is a ceiling, not a prediction.
 */
#define RENDEZVOUS_BOUND_US (4u * CYCLE_MAX_US)

typedef struct {
    uint64_t    clock_us;
    halh_node_t a, b;
    link_sm_t   sm_a, sm_b;
    frag_tx_t   rec_a, rec_b;
    link_cfg_t  cfg;
} bsim_t;

/* Different on each side, so "they got each other's record" is a real check
 * rather than a comparison of a thing with itself. */
static const uint8_t k_blob_a[] = { 0x01, 0x05, 'A', 'l', 'i', 'c', 'e' };
static const uint8_t k_blob_b[] = { 0x01, 0x03, 'B', 'o', 'b' };

static void bsim_init(bsim_t *s, uint64_t seed)
{
    memset(s, 0, sizeof *s);
    halh_pair(&s->a, &s->b, &s->clock_us, seed);
    link_cfg_default(&s->cfg);

    frag_split(k_blob_a, sizeof k_blob_a, 1, &s->rec_a);
    frag_split(k_blob_b, sizeof k_blob_b, 2, &s->rec_b);

    link_sm_init(&s->sm_a, &s->a.iface, &s->cfg, &s->rec_a);
    link_sm_init(&s->sm_b, &s->b.iface, &s->cfg, &s->rec_b);
}

static void bsim_step(bsim_t *s)
{
    link_sm_poll(&s->sm_a, s->clock_us);
    link_sm_poll(&s->sm_b, s->clock_us);
    halh_advance(&s->a, &s->b, STEP_US);
}

static bool awake(const link_sm_t *sm)
{
    return sm->state != LINK_IDLE;
}

static const char *role_name(link_role_t r)
{
    return r == LINK_ROLE_SENDER   ? "SENDER"
         : r == LINK_ROLE_RECEIVER ? "RECEIVER" : "NONE";
}

/* ---------------------------------------------------------------------- */

/*
 * A band alone in a room must stay in IDLE for ever. This is the test that
 * would fail if the carrier detector were fed during a shout: the band would
 * trigger on its own amplifier, every cycle, and burn a contact budget against
 * nobody.
 */
static void alone_never_triggers(void)
{
    bsim_t s;
    uint64_t t;

    hf_begin("trigger: a band alone never fires");
    bsim_init(&s, 99);
    halh_set_coupled(&s.a, &s.b, false);

    link_sm_idle(&s.sm_a, s.clock_us);
    link_sm_idle(&s.sm_b, s.clock_us);

    for (t = 0; t < 20u * CYCLE_MAX_US; t += STEP_US)
        bsim_step(&s);

    HF_CHECK_MSG(!awake(&s.sm_a), "A fired with nothing to hear (state %s)",
             link_state_name(s.sm_a.state));
    HF_CHECK_MSG(!awake(&s.sm_b), "B fired with nothing to hear (state %s)",
             link_state_name(s.sm_b.state));

    /* It must actually have been trying, or the test above proves nothing. */
    HF_CHECK_MSG(s.sm_a.trig.shouts >= 15,
             "A only shouted %u times in 20 worst-case cycles", s.sm_a.trig.shouts);

    /* And it must have been listening the whole rest of the time — a band that
     * shouted but never opened its ears would also pass the check above. */
    HF_CHECK_MSG(s.sm_a.trig.waits == 0,
             "A heard %u carriers with nobody there", s.sm_a.trig.waits);
}

/*
 * §6.1 and §6.2, run as one sweep because they are two assertions about the
 * same 112 rendezvous and running them twice would only halve the confidence
 * per unit of time.
 *
 * B is armed PHASE microseconds after A, at every offset across a full
 * worst-case cycle. Every offset must rendezvous inside the bound, and every
 * offset must produce exactly one sender and one receiver.
 */
static void phase_sweep(void)
{
    uint32_t phase;
    uint64_t worst = 0;
    uint32_t worst_phase = 0;
    uint32_t phases = 0, slow = 0;
    uint32_t both_tx = 0, bad_roles = 0;

    hf_begin("trigger: every relative phase rendezvous, with exactly one sender");

    for (phase = 0; phase <= CYCLE_MAX_US / 1000u; phase++) {
        bsim_t s;
        const uint64_t start_us = (uint64_t)phase * 1000u;
        uint64_t limit, took;
        link_role_t role_a = LINK_ROLE_NONE, role_b = LINK_ROLE_NONE;

        bsim_init(&s, 0x5EED0000u + phase);

        /*
         * Both wearers are already touching; what differs is where each band
         * is in its own free-running cycle when the other one joins. Arming B
         * `phase` later is what sets that offset, and sweeping it to a whole
         * cycle covers every offset there is.
         */
        link_sm_idle(&s.sm_a, s.clock_us);
        while (s.clock_us < start_us) bsim_step(&s);
        link_sm_idle(&s.sm_b, s.clock_us);

        limit = s.clock_us + RENDEZVOUS_BOUND_US;
        while (s.clock_us < limit && !(awake(&s.sm_a) && awake(&s.sm_b))) {
            bsim_step(&s);

            /* Half duplex: two bands clocking out frames at once is the
             * both-send failure, in the only form that actually costs a card. */
            if (s.sm_a.state == LINK_TX_FRAME && s.sm_b.state == LINK_TX_FRAME)
                both_tx++;

            if (role_a == LINK_ROLE_NONE) role_a = link_sm_role(&s.sm_a);
            if (role_b == LINK_ROLE_NONE) role_b = link_sm_role(&s.sm_b);
        }

        took = s.clock_us - start_us;
        if (took > worst) { worst = took; worst_phase = phase; }
        if (took > CYCLE_MAX_US) slow++;
        phases++;

        HF_CHECK_MSG(awake(&s.sm_a) && awake(&s.sm_b),
                 "phase %u ms: no rendezvous in %llu us (A %s, B %s)", phase,
                 (unsigned long long)took,
                 link_state_name(s.sm_a.state), link_state_name(s.sm_b.state));

        /*
         * THE CLAIM IN §2. One end heard the other's shout and takes the
         * channel; the other never heard a thing and is already listening to
         * the card that answers it. Both-send and both-listen are supposed to
         * be unreachable, not merely unlikely.
         */
        if (!((role_a == LINK_ROLE_SENDER) != (role_b == LINK_ROLE_SENDER)) ||
            role_a == LINK_ROLE_NONE || role_b == LINK_ROLE_NONE) {
            bad_roles++;
            HF_CHECK_MSG(0, "phase %u ms: roles %s / %s", phase,
                         role_name(role_a), role_name(role_b));
        }
    }

    HF_CHECK_MSG(phases > 100, "phase sweep only ran %u points", phases);
    HF_CHECK_MSG(bad_roles == 0,
             "%u of %u phases did not produce exactly one sender",
             bad_roles, phases);
    HF_CHECK_MSG(both_tx == 0,
             "the pair was clocking out frames simultaneously on %u polls",
             both_tx);

    printf("      rendezvous: worst %llu us (phase %u ms) over %u phases;"
           " %u past one cycle\n",
           (unsigned long long)worst, worst_phase, phases, slow);
}

/*
 * §6.3. The degenerate case §4.1 admits to: two bands shouting close enough
 * together that neither hears the other, because each is deaf for its own
 * burst. Forced by arming both at the same instant, over many seeds.
 *
 * §4.1 estimates a repeat at about 4 % per round from the ratio of the
 * detector latency to the draw range. That is arithmetic. This is the
 * measurement that replaces it, and the printed number is the one to quote.
 */
static void simultaneous_start(void)
{
    const uint32_t trials = 400;
    uint32_t seed, resolved_in_two = 0, rounds_worst = 0;
    uint64_t took_worst = 0;

    hf_begin("trigger: a simultaneous shout resolves on a later round");

    for (seed = 0; seed < trials; seed++) {
        bsim_t s;
        uint64_t limit, took;
        uint32_t rounds;

        bsim_init(&s, 0xA11CE000u + seed * 7919u);

        /* Same instant, to the microsecond. The two bands still draw different
         * listen windows, which is the only thing that can separate them. */
        link_sm_idle(&s.sm_a, s.clock_us);
        link_sm_idle(&s.sm_b, s.clock_us);

        limit = s.clock_us + RENDEZVOUS_BOUND_US;
        while (s.clock_us < limit && !(awake(&s.sm_a) && awake(&s.sm_b)))
            bsim_step(&s);

        HF_CHECK_MSG(awake(&s.sm_a) && awake(&s.sm_b),
                 "seed %u: simultaneous start never resolved (A %s, B %s)",
                 seed, link_state_name(s.sm_a.state),
                 link_state_name(s.sm_b.state));

        HF_CHECK_MSG((link_sm_role(&s.sm_a) == LINK_ROLE_SENDER) !=
                     (link_sm_role(&s.sm_b) == LINK_ROLE_SENDER),
                 "seed %u: roles %s / %s", seed,
                 role_name(link_sm_role(&s.sm_a)),
                 role_name(link_sm_role(&s.sm_b)));

        /* Round 1 is the collision itself, so "resolved within two rounds"
         * means neither band shouted more than twice. */
        rounds = s.sm_a.trig.shouts > s.sm_b.trig.shouts ? s.sm_a.trig.shouts
                                                         : s.sm_b.trig.shouts;
        if (rounds <= 2u) resolved_in_two++;
        if (rounds > rounds_worst) rounds_worst = rounds;

        took = s.clock_us;
        if (took > took_worst) took_worst = took;
    }

    printf("      simultaneous start: %u/%u resolved within two rounds"
           " (%.1f%% needed more), worst %u rounds / %llu us\n",
           resolved_in_two, trials,
           100.0 * (double)(trials - resolved_in_two) / (double)trials,
           rounds_worst, (unsigned long long)took_worst);

    /*
     * Pinned, so a change that makes collisions common shows up as a failure
     * rather than as a slightly slower demo. The threshold is set against the
     * measured figure with room to move, not against the estimate in §4.1.
     */
    HF_CHECK_MSG(resolved_in_two * 20u >= trials * 19u,
             "only %u of %u simultaneous starts resolved within two rounds",
             resolved_in_two, trials);
}

/*
 * §6.4, and §4.3's answer to it. A carrier that never clears — noise, a stuck
 * transmitter, a floor that has moved — must not take the band off the air and
 * must not make it transmit into a channel it can see is busy.
 *
 * Driven at the trigger directly rather than through a simulated channel: the
 * point is what the state machine does when told "still busy" for ever, and a
 * simulated stuck carrier would also be testing carrier.c's floor tracking.
 */
static void stuck_carrier(void)
{
    uint64_t clock_us = 0;
    halh_node_t n;
    trig_t t;
    uint64_t at;
    uint32_t sends = 0, receives = 0, listens = 0;
    uint64_t first_listen_after_wait = 0;
    bool waiting = false;

    hf_begin("trigger: a carrier that never stops never becomes a send");

    halh_init(&n, "X", &clock_us, 12345);
    trig_init(&t, &n.iface);
    trig_start(&t, clock_us);

    for (at = 0; at < 20u * CYCLE_MAX_US; at += STEP_US) {
        const bool listening = trig_listening(&t);
        /* Carrier for ever, and the framer never locks — it is a flat tone,
         * or noise, not a card. */
        const trig_state_t st = trig_poll(&t, clock_us, listening, false);

        if (st == TRIG_SEND) sends++;
        if (st == TRIG_RECEIVE) receives++;

        if (st == TRIG_WAIT && !waiting) {
            waiting = true;
        } else if (waiting && st == TRIG_LISTEN) {
            waiting = false;
            listens++;
            if (!first_listen_after_wait) first_listen_after_wait = clock_us;
        }
        clock_us += STEP_US;
    }

    HF_CHECK_MSG(sends == 0, "the band sent into a channel it could hear was busy");
    HF_CHECK_MSG(receives == 0, "the band tried to receive a card that never locked");
    HF_CHECK_MSG(listens > 0, "the band never came back out of TRIG_WAIT");
    HF_CHECK_MSG(t.quiet_timeouts > 0, "the quiet-wait cap never fired");

    /*
     * And it came back within the cap, not eventually. The first WAIT starts on
     * the first listening poll after the shout and settle, so the cap must have
     * expired by one cycle plus the cap itself.
     */
    HF_CHECK_MSG(first_listen_after_wait <=
                 HANDOFF_SHOUT_US + HANDOFF_TRIG_SETTLE_US +
                 HANDOFF_QUIET_WAIT_MAX_US + 4u * STEP_US,
             "took %llu us to give up on a stuck carrier, cap is %u us",
             (unsigned long long)first_listen_after_wait,
             (unsigned)HANDOFF_QUIET_WAIT_MAX_US);
}

/*
 * §6.5. The shout must not look like a frame. A constant-on burst has no
 * transitions, so frame.c's preamble hunt can never lock to it — if this ever
 * fails, someone made the shout alternate and turned it into a preamble, which
 * is the decision §7 of the spec re-examined and kept.
 */
static void shout_is_not_a_preamble(void)
{
    uint8_t chips[TRIG_SHOUT_CHIPS];
    frame_rx_t r;
    size_t n, i, cycle;

    hf_begin("trigger: a shout cannot be mistaken for a frame");

    n = trig_fill(chips, sizeof chips);
    HF_CHECK_MSG(n == (size_t)TRIG_SHOUT_CHIPS, "filled %u chips", (unsigned)n);

    for (i = 0; i < n; i++)
        HF_CHECK_MSG(chips[i] == 1u,
                 "chip %u is not on: the shout has a transition", (unsigned)i);

    /*
     * Forty shout cycles through one framer — burst, then the silence a real
     * listen window would be, so the burst edges are in the stream too. A
     * preamble shout would sync on almost every one of these.
     *
     * LINK V2 STEP 6: the shout is one unbroken TONE, so it arrives as a run
     * of the same sign; the silence between is both bins reading the room, so
     * it arrives as a small difference of RANDOM sign. That second half is
     * the one worth modelling properly — a quiet channel that alternated
     * would be a preamble, and the framer must not be handed a kind one.
     */
    frame_rx_init(&r);
    {
        uint32_t lcg = 12345u;
        for (cycle = 0; cycle < 40u; cycle++) {
            for (i = 0; i < n; i++) (void)frame_rx_push(&r, 400);
            for (i = 0; i < 4u * n; i++) {
                lcg = lcg * 1103515245u + 12345u;
                (void)frame_rx_push(&r, ((lcg >> 16) & 1u) ? 6 : -6);
            }
        }
    }

    HF_CHECK_MSG(r.syncs == 0, "framer synced %u times on a shout", r.syncs);
    HF_CHECK_MSG(r.frames_good == 0 && r.frames_bad_crc == 0,
             "framer produced %u good and %u bad frames from a shout",
             r.frames_good, r.frames_bad_crc);
}

/*
 * THE FLOOR TESTS ARE GONE, WITH THE FLOOR. Link v2 step 5 deleted
 * dsp/carrier.c, and six checks went with it: that the floor lands on the mean
 * of ambient rather than its minimum; that it does not climb to meet a carrier
 * lasting a whole frame; that a reset mid-frame does not blind it on either
 * Manchester phase; that a reprime recovers inside a frame; that it barely
 * moves across a preamble; and that brief spikes do not train it upward.
 *
 * Every one of those was a way a REMEMBERED number could be poisoned by the
 * signal it was meant to measure. v2 remembers nothing about the signal: the
 * reference is three bins the transmitter cannot enter, measured in the same
 * windows as the signal. test_presence.c tests what replaced them.
 *
 * The TRIGGER tests in this file stay. They were never about the floor — they
 * are about what a band does with a bool.
 */

/*
 * §6.6. Triggering is not the point; handshaking is. The band must go on to
 * exchange a record with no host call anywhere in the path — and the frames it
 * spends doing so are the regression canary for §5.1's carrier-detector
 * question, because a detector left mis-primed makes handover talk over the
 * reply it asked for and the count goes up several fold.
 */
static void trigger_completes_a_handshake(void)
{
    uint32_t seed;
    uint32_t frames_worst = 0;

    hf_begin("trigger: a rendezvous leads to a completed exchange");

    for (seed = 0; seed < 8; seed++) {
        bsim_t s;
        uint64_t limit;
        const uint8_t *got = NULL;
        size_t n;
        uint32_t frames;

        bsim_init(&s, 0xC0FFEEu + seed);
        halh_set_coupled(&s.a, &s.b, false);
        link_sm_idle(&s.sm_a, s.clock_us);

        for (; s.clock_us < 37000u + seed * 4200u; ) bsim_step(&s);
        link_sm_idle(&s.sm_b, s.clock_us);
        for (; s.clock_us < 200000u; ) bsim_step(&s);

        halh_set_coupled(&s.a, &s.b, true);

        limit = s.clock_us + RENDEZVOUS_BOUND_US + s.cfg.contact_budget_us;
        while (s.clock_us < limit &&
               !(s.sm_a.state == LINK_COMPLETE && s.sm_b.state == LINK_COMPLETE))
            bsim_step(&s);

        HF_CHECK_MSG(s.sm_a.state == LINK_COMPLETE && s.sm_b.state == LINK_COMPLETE,
                 "seed %u: A %s, B %s", seed,
                 link_state_name(s.sm_a.state), link_state_name(s.sm_b.state));

        HF_CHECK_MSG((link_sm_role(&s.sm_a) == LINK_ROLE_SENDER) !=
                     (link_sm_role(&s.sm_b) == LINK_ROLE_SENDER),
                 "seed %u: roles %s / %s", seed,
                 role_name(link_sm_role(&s.sm_a)),
                 role_name(link_sm_role(&s.sm_b)));

        /* The point of the whole exercise: each end is holding the other's
         * card, having been started by nothing but a shout. */
        n = link_sm_received(&s.sm_a, &got);
        HF_CHECK_MSG(n >= sizeof k_blob_b &&
                     memcmp(got, k_blob_b, sizeof k_blob_b) == 0,
                 "seed %u: A did not receive B's record (%u bytes)",
                 seed, (unsigned)n);

        n = link_sm_received(&s.sm_b, &got);
        HF_CHECK_MSG(n >= sizeof k_blob_a &&
                     memcmp(got, k_blob_a, sizeof k_blob_a) == 0,
                 "seed %u: B did not receive A's record (%u bytes)",
                 seed, (unsigned)n);

        frames = s.sm_a.frames_sent + s.sm_b.frames_sent;
        if (frames > frames_worst) frames_worst = frames;
    }

    printf("      handshake after rendezvous: worst %u frames sent for"
           " two 1-fragment cards\n", frames_worst);

    /*
     * Two single-fragment cards need one frame each way, plus an acknowledging
     * frame each way. Anything far above that means an end is transmitting into
     * a channel it should have heard was busy — see §5.1.
     */
    HF_CHECK_MSG(frames_worst <= 12,
             "%u frames to exchange two one-fragment cards: handover is"
             " misreading the channel", frames_worst);
}

/*
 * Contact that breaks and comes back. The band has to return to IDLE and be
 * able to rendezvous again, or a band is good for exactly one handshake per
 * power cycle.
 */
static void re_arms_after_a_contact(void)
{
    bsim_t s;
    uint64_t limit;

    hf_begin("trigger: a band re-arms after a handshake");
    bsim_init(&s, 4242);
    halh_set_coupled(&s.a, &s.b, false);
    link_sm_idle(&s.sm_a, s.clock_us);
    link_sm_idle(&s.sm_b, s.clock_us);
    for (; s.clock_us < 150000u; ) bsim_step(&s);

    halh_set_coupled(&s.a, &s.b, true);
    limit = s.clock_us + RENDEZVOUS_BOUND_US + s.cfg.contact_budget_us;
    while (s.clock_us < limit && s.sm_a.state != LINK_COMPLETE) bsim_step(&s);
    HF_CHECK_MSG(s.sm_a.state == LINK_COMPLETE, "first handshake: A %s",
             link_state_name(s.sm_a.state));

    /* Hands part, both bands go back on the shelf. */
    halh_set_coupled(&s.a, &s.b, false);
    link_sm_idle(&s.sm_a, s.clock_us);
    link_sm_idle(&s.sm_b, s.clock_us);
    for (; s.clock_us < limit + 300000u; ) bsim_step(&s);
    HF_CHECK_MSG(!awake(&s.sm_a), "A fired again with nobody there");

    /* And a second, different person. */
    halh_set_coupled(&s.a, &s.b, true);
    limit = s.clock_us + RENDEZVOUS_BOUND_US;
    while (s.clock_us < limit && !(awake(&s.sm_a) && awake(&s.sm_b)))
        bsim_step(&s);
    HF_CHECK_MSG(awake(&s.sm_a) && awake(&s.sm_b),
             "second contact never rendezvoused (A %s, B %s)",
             link_state_name(s.sm_a.state), link_state_name(s.sm_b.state));
}

void test_beacon(void)
{
    alone_never_triggers();
    phase_sweep();
    simultaneous_start();
    stuck_carrier();
    shout_is_not_a_preamble();
    trigger_completes_a_handshake();
    re_arms_after_a_contact();
}
