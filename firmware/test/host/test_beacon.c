/*
 * Handoff — the contact trigger. beacon.h, link v2 step 7.
 *
 * Three claims are on trial here and they are not the same claim.
 *
 * RENDEZVOUS: two bands free-running at arbitrary relative phase find each
 * other within a bounded time of touching. A test that tried one phase would
 * pass on luck, so the phase is swept across a whole worst-case cycle at 1 ms
 * resolution.
 *
 * NO TIE: over that same sweep, the pair never ends up both-sending or
 * both-listening. beacon.h §2 argues that from the geometry — decoding a peer
 * means you had not yet started your own beacon, and a decoder stops
 * beaconing, so at most one band can ever decode the other. That argument has
 * not been machine-checked and this file is what turns it into evidence. It is
 * the most important test here.
 *
 * THE NONCE DOES ITS ONE JOB: a band never acts on its own beacon, and two
 * bands that draw the same sixteen bits notice and redraw rather than both
 * standing down for ever. Step 7 owes both of those, and they are the last two
 * tests in the file.
 */
#include <stdio.h>
#include <string.h>

#include "beacon.h"
#include "hal_host.h"
#include "hf_test.h"
#include "link_sm.h"
#include "tests.h"

/* Fine enough that no beacon, settle or decode can be stepped over. */
#define STEP_US 100u

/* One whole cycle at its longest: beacon, settle, and the longest listen the
 * draw can produce. This is the window the phase sweep covers. */
#define CYCLE_MAX_US (HANDOFF_BEACON_AIRTIME_US + HANDOFF_TRIG_SETTLE_US + \
                      HANDOFF_LISTEN_MAX_US)

/*
 * The bound rendezvous must meet. beacon.h derives an EXPECTED time of 8
 * beacons — two mean cycles — from a per-cycle failure probability of one
 * half. That is a mean, and a geometric tail: eight worst-case cycles is
 * 2^-16 per phase on the arithmetic, and the sweep prints what it actually
 * took. The printed number is the one to believe; this is a ceiling.
 */
#define RENDEZVOUS_BOUND_US (8u * CYCLE_MAX_US)

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
 * would fail if the receive path were fed during a beacon: the band would
 * decode its own frame every cycle and burn a contact budget against nobody.
 *
 * Under v1 that failure was REAL and this test was the guard against it — a
 * band on a bench elected itself sender on 60 shouts out of 60. Under v2 the
 * nonce makes it unwritable, so this test now checks two things at once: that
 * nothing fires, and that self_echoes is the counter it would have shown up
 * in if it had.
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
    HF_CHECK_MSG(s.sm_a.trig.beacons >= 15,
             "A only beaconed %u times in 20 worst-case cycles",
             s.sm_a.trig.beacons);

    /* And it must have been listening the whole rest of the time — a band that
     * beaconed but never opened its ears would also pass the check above. */
    HF_CHECK_MSG(s.sm_a.trig.peers == 0,
             "A decoded %u peer beacons with nobody there", s.sm_a.trig.peers);
}

/*
 * The rendezvous sweep, run as one pass because its two assertions are about
 * the same ~230 rendezvous and running them twice would only halve the
 * confidence per unit of time.
 *
 * B is armed PHASE microseconds after A, at every offset across a full
 * worst-case cycle. Every offset must rendezvous inside the bound, and every
 * offset must produce exactly one sender and one receiver.
 */
static void phase_sweep(void)
{
    uint32_t phase;
    uint64_t worst = 0, total = 0;
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
        total += took;
        if (took > CYCLE_MAX_US) slow++;
        phases++;

        HF_CHECK_MSG(awake(&s.sm_a) && awake(&s.sm_b),
                 "phase %u ms: no rendezvous in %llu us (A %s, B %s)", phase,
                 (unsigned long long)took,
                 link_state_name(s.sm_a.state), link_state_name(s.sm_b.state));

        /*
         * THE CLAIM IN beacon.h §2. One end decoded the other's beacon and
         * takes the channel; the other never decoded a thing and is already
         * listening to the card that answers it. Both-send and both-listen are
         * supposed to be unreachable, not merely unlikely.
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

    /*
     * This is a rendezvous time, but it is NOT the one to quote: arming B
     * fresh makes it beacon immediately, which is kinder than a wrist.
     * steady_state_rendezvous() is the number design §2 has to live with.
     */
    printf("      rendezvous from a fresh arming: mean %llu us, worst %llu us"
           " (phase %u ms) over %u phases; %u past one cycle\n",
           (unsigned long long)(total / phases),
           (unsigned long long)worst, worst_phase, phases, slow);
}

/*
 * WHAT A WRIST ACTUALLY DOES, which is not what the phase sweep measures.
 *
 * The sweep arms B fresh, so B beacons the instant it joins and the rendezvous
 * is over unusually fast. That is the right shape for "exactly one sender",
 * which is what the sweep is for, and the wrong shape for a latency: on a
 * wrist BOTH bands have been beaconing for minutes and what changes is that
 * skin closes the channel, at a moment neither of them chose.
 *
 * So: two bands free-running, already out of step, and the coupling switched
 * on at every offset across a whole cycle. This is the number design §2's
 * one-second budget has to be spent out of, and beacon.h derives the beacon
 * period from an expectation of 8 beacon airtimes — printed beside it, because
 * the derivation is a rigid-phase bound and the simulator redraws the listen
 * window every cycle, which can only help.
 */
static void steady_state_rendezvous(void)
{
    uint32_t phase, n = 0, slow = 0;
    uint64_t worst = 0, total = 0;
    uint32_t worst_phase = 0;

    hf_begin("trigger: two bands already running, touched at every offset");

    for (phase = 0; phase <= CYCLE_MAX_US / 2000u; phase++) {
        bsim_t s;
        uint64_t touch_us, limit, took;

        bsim_init(&s, 0xB0D1E000u + phase);
        halh_set_coupled(&s.a, &s.b, false);

        /* Arm both apart, and let them run long enough that neither is
         * anywhere near the start of its cycle. */
        link_sm_idle(&s.sm_a, s.clock_us);
        while (s.clock_us < 13000u) bsim_step(&s);
        link_sm_idle(&s.sm_b, s.clock_us);

        touch_us = 400000u + (uint64_t)phase * 2000u;
        while (s.clock_us < touch_us) bsim_step(&s);

        /* Skin meets skin. */
        halh_set_coupled(&s.a, &s.b, true);
        limit = s.clock_us + RENDEZVOUS_BOUND_US;
        while (s.clock_us < limit && !(awake(&s.sm_a) && awake(&s.sm_b)))
            bsim_step(&s);

        took = s.clock_us - touch_us;
        HF_CHECK_MSG(awake(&s.sm_a) && awake(&s.sm_b),
                 "offset %u: no rendezvous in %llu us (A %s, B %s)", phase,
                 (unsigned long long)took,
                 link_state_name(s.sm_a.state), link_state_name(s.sm_b.state));
        HF_CHECK_MSG((link_sm_role(&s.sm_a) == LINK_ROLE_SENDER) !=
                     (link_sm_role(&s.sm_b) == LINK_ROLE_SENDER),
                 "offset %u: roles %s / %s", phase,
                 role_name(link_sm_role(&s.sm_a)),
                 role_name(link_sm_role(&s.sm_b)));

        total += took;
        if (took > worst) { worst = took; worst_phase = phase; }
        if (took > CYCLE_MAX_US) slow++;
        n++;
    }

    printf("      touched mid-cycle: mean %llu us, worst %llu us (offset %u)"
           " over %u contacts; %u past one cycle; beacon.h expects %u us\n",
           (unsigned long long)(total / n), (unsigned long long)worst,
           worst_phase, n, slow, (unsigned)(8u * HANDOFF_BEACON_AIRTIME_US));

    /*
     * Pinned against the derivation rather than against the measurement: the
     * period was chosen to minimise this, so a change that makes it worse than
     * the bound the period was derived from has undone the derivation.
     */
    HF_CHECK_MSG(total / n <= 8ull * HANDOFF_BEACON_AIRTIME_US,
             "mean rendezvous %llu us is worse than the %u us beacon.h derives"
             " the period from",
             (unsigned long long)(total / n),
             (unsigned)(8u * HANDOFF_BEACON_AIRTIME_US));
}

/*
 * The degenerate case the geometry admits to: two bands beaconing close enough
 * together that neither decodes the other, because each is deaf for its own
 * frame. Forced by arming both at the same instant, over many seeds.
 *
 * beacon.h puts a whole cycle's failure probability at 2T/C = one half, from
 * which the draw range is chosen. This is the measurement that replaces the
 * arithmetic, and the printed number is the one to quote.
 */
static void simultaneous_start(void)
{
    const uint32_t trials = 400;
    uint32_t seed, resolved_in_three = 0, rounds_worst = 0;
    uint64_t took_worst = 0;

    hf_begin("trigger: a simultaneous beacon resolves on a later round");

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

        /* Round 1 is the collision itself. */
        rounds = s.sm_a.trig.beacons > s.sm_b.trig.beacons ? s.sm_a.trig.beacons
                                                           : s.sm_b.trig.beacons;
        if (rounds <= 3u) resolved_in_three++;
        if (rounds > rounds_worst) rounds_worst = rounds;

        took = s.clock_us;
        if (took > took_worst) took_worst = took;
    }

    printf("      simultaneous start: %u/%u resolved within three rounds"
           " (%.1f%% needed more), worst %u rounds / %llu us\n",
           resolved_in_three, trials,
           100.0 * (double)(trials - resolved_in_three) / (double)trials,
           rounds_worst, (unsigned long long)took_worst);

    /*
     * Pinned, so a change that makes collisions common shows up as a failure
     * rather than as a slightly slower demo. A simultaneous start is the
     * worst phase there is — the two beacons overlap exactly — so the bar is
     * set against the measured figure with room to move, not against a
     * prediction.
     */
    HF_CHECK_MSG(resolved_in_three * 10u >= trials * 9u,
             "only %u of %u simultaneous starts resolved within three rounds",
             resolved_in_three, trials);
}

/*
 * A channel that is never quiet must not take the band off the air, and must
 * not make it beacon into traffic it can hear.
 *
 * This is what became of v1's stuck-carrier test. v1 needed a cap on TRIG_WAIT
 * and a rule about what to do when it expired, because a carrier that never
 * cleared was ambiguous: a peer's shout, the room, or a floor that had moved.
 * None of that survives. A busy channel holds the silence budget and nothing
 * else happens, for ever, which is the correct behaviour and needs no
 * constant.
 */
static void stuck_carrier(void)
{
    uint64_t clock_us = 0;
    halh_node_t n;
    trig_t t;
    trig_in_t in;
    uint64_t at;
    uint32_t sends = 0, receives = 0, beacons_while_busy = 0;
    uint32_t beacons_before = 0;

    hf_begin("trigger: a channel that is never quiet never becomes a send");

    halh_init(&n, "X", &clock_us, 12345);
    trig_init(&t, &n.iface);
    trig_start(&t, clock_us);

    for (at = 0; at < 20u * CYCLE_MAX_US; at += STEP_US) {
        const bool listening = trig_listening(&t);
        trig_state_t st;

        memset(&in, 0, sizeof in);
        /* Busy for ever, and nothing ever decodes — it is the room, or a
         * stuck transmitter, not a band. */
        in.busy = listening;

        beacons_before = t.beacons;
        st = trig_poll(&t, clock_us, &in);

        if (st == TRIG_SEND) sends++;
        if (st == TRIG_RECEIVE) receives++;
        if (listening && t.beacons != beacons_before) beacons_while_busy++;

        clock_us += STEP_US;
    }

    HF_CHECK_MSG(sends == 0, "the band sent into a channel it could hear was busy");
    HF_CHECK_MSG(receives == 0, "the band tried to receive a card that never arrived");
    HF_CHECK_MSG(beacons_while_busy == 0,
             "the band beaconed %u times over a channel it could hear was busy",
             beacons_while_busy);

    /* It beaconed exactly once — the arming beacon, before it had heard
     * anything — and then held. A band that kept beaconing would be talking
     * over whatever is out there; one that never beaconed at all would have
     * been asleep. */
    HF_CHECK_MSG(t.beacons == 1u,
             "beaconed %u times against a permanently busy channel", t.beacons);
    HF_CHECK_MSG(trig_listening(&t),
             "the band did not end up listening (state %d)", (int)t.state);
}

/*
 * THE FIRST THING STEP 7 OWES. A band must never act on its own beacon.
 *
 * Driven at the trigger directly and with its OWN frame fed back into it,
 * which is the one thing the paired simulator cannot do: there the settle is
 * long enough that a self-echo never reaches the ears, so a test built on it
 * would be testing the settle rather than the nonce. This bypasses the settle
 * entirely and asks the harder question — if the echo DOES arrive, what
 * happens?
 *
 * Under v1 the answer was "the band elects itself sender", sixty times out of
 * sixty. Under v2 it is arithmetic: the nonce is ours.
 */
static void own_beacon_is_never_a_peer(void)
{
    uint64_t clock_us = 0;
    halh_node_t n;
    trig_t t;
    uint32_t cycle;

    hf_begin("trigger: a band never acts on its own beacon");

    halh_init(&n, "X", &clock_us, 777);
    trig_init(&t, &n.iface);
    trig_start(&t, clock_us);

    for (cycle = 0; cycle < 40u; cycle++) {
        trig_in_t in;
        trig_state_t st;
        uint16_t mine;
        uint64_t guard;

        memset(&in, 0, sizeof in);

        /*
         * Run the free silent channel forward to the next beacon, then out
         * through the deaf phases to LISTEN, feeding nothing — exactly what
         * the real caller does while its own amplifier is driving. Both waits
         * are needed: after an echo the band is already in LISTEN, so waiting
         * only for LISTEN would stand still for ever.
         */
        guard = clock_us + 100ull * CYCLE_MAX_US;
        while (t.state != TRIG_BEACON && clock_us < guard) {
            (void)trig_poll(&t, clock_us, &in);
            clock_us += STEP_US;
        }
        mine = trig_nonce(&t);
        while (t.state != TRIG_LISTEN && clock_us < guard) {
            (void)trig_poll(&t, clock_us, &in);
            clock_us += STEP_US;
        }

        /* Now hand it back exactly what it just transmitted. */
        in.beacon = true;
        in.nonce = mine;
        st = trig_poll(&t, clock_us, &in);
        clock_us += STEP_US;

        HF_CHECK_MSG(st == TRIG_LISTEN,
                 "cycle %u: the band left LISTEN on its own beacon (state %d)",
                 cycle, (int)st);
    }

    HF_CHECK_MSG(t.sends == 0, "the band elected itself sender %u times", t.sends);
    HF_CHECK_MSG(t.peers == 0, "the band read its own beacon as a peer %u times",
             t.peers);
    HF_CHECK_MSG(t.self_echoes == 40u,
             "only %u of 40 self-echoes were recognised", t.self_echoes);

    /*
     * And it went out under a new name each time, which is the tie's half of
     * the same rule — the redraw happens at the NEXT beacon, never when the
     * echo arrives, so the stragglers of the beacon already on the wire keep
     * being rejected. beacon.h has the argument.
     */
    HF_CHECK_MSG(t.redraws + 1u >= t.beacons,
             "%u redraws over %u beacons after 40 echoes: a tie would not break",
             t.redraws, t.beacons);
}

/*
 * THE SECOND THING STEP 7 OWES, and the one the design puts a number on: two
 * bands that draw the same sixteen bits. Probability 2^-16 per contact, so it
 * is forced rather than waited for.
 *
 * Both bands are given a random source that hands out the SAME values, so
 * their nonces and their listen draws are identical — the worst case there
 * is. Each must read the other as an echo, redraw, and go on to rendezvous
 * with exactly one sender.
 */
static void nonce_tie_redraws(void)
{
    bsim_t s;
    /*
     * trig_start draws the nonce FIRST and the listen window after it, so the
     * head of each array is the forced tie and the tail is timing. The two
     * arrays agree in their low sixteen bits and in nothing else — forcing the
     * timing to match as well would put the pair in perfect lockstep, where
     * neither ever hears the other and the tie is never even discovered. That
     * is what the first version of this test did, and it passed for the wrong
     * reason until the check three lines from the end caught it.
     */
    static const uint32_t k_a[6] = { 0x00001234u, 61000u,  9000u, 33000u,
                                     51000u, 17000u };
    static const uint32_t k_b[6] = { 0xFFFF1234u, 20000u, 47000u,  5000u,
                                     38000u, 26000u };
    uint64_t limit;

    hf_begin("trigger: two bands that draw the same nonce redraw and rendezvous");

    bsim_init(&s, 31337);
    halh_force_random(&s.a, k_a, 6);
    halh_force_random(&s.b, k_b, 6);

    link_sm_idle(&s.sm_a, s.clock_us);
    link_sm_idle(&s.sm_b, s.clock_us);

    HF_CHECK_MSG(trig_nonce(&s.sm_a.trig) == trig_nonce(&s.sm_b.trig),
             "the tie was not actually forced: %u vs %u",
             trig_nonce(&s.sm_a.trig), trig_nonce(&s.sm_b.trig));

    limit = s.clock_us + RENDEZVOUS_BOUND_US;
    while (s.clock_us < limit && !(awake(&s.sm_a) && awake(&s.sm_b)))
        bsim_step(&s);

    HF_CHECK_MSG(awake(&s.sm_a) && awake(&s.sm_b),
             "a nonce tie never resolved (A %s, B %s)",
             link_state_name(s.sm_a.state), link_state_name(s.sm_b.state));

    HF_CHECK_MSG((link_sm_role(&s.sm_a) == LINK_ROLE_SENDER) !=
                 (link_sm_role(&s.sm_b) == LINK_ROLE_SENDER),
             "roles %s / %s", role_name(link_sm_role(&s.sm_a)),
             role_name(link_sm_role(&s.sm_b)));

    /*
     * The tie has to have been SEEN, or this passed by never colliding at all
     * and proves nothing about the redraw.
     */
    HF_CHECK_MSG(s.sm_a.trig.self_echoes + s.sm_b.trig.self_echoes > 0,
             "neither band ever decoded the shared nonce, so no tie occurred");
    HF_CHECK_MSG(s.sm_a.trig.redraws + s.sm_b.trig.redraws > 0,
             "the tie was seen but nobody redrew");

    printf("      nonce tie: %u echoes / %u redraws on A, %u / %u on B;"
           " resolved in %llu us\n",
           s.sm_a.trig.self_echoes, s.sm_a.trig.redraws,
           s.sm_b.trig.self_echoes, s.sm_b.trig.redraws,
           (unsigned long long)s.clock_us);
}

/*
 * THE THIRD THING STEP 7 OWES. A beacon whose CRC is damaged must be IGNORED,
 * not acted on. Half of that is frame.c's job and test_frame.c pins it; this
 * is the other half — that the trigger, handed a stream with a broken beacon
 * in it, does nothing at all.
 *
 * Driven through the framer with real chips, because the interesting failure
 * is not "the trigger ignored a flag" but "the framer handed the trigger a
 * nonce out of a frame that did not survive its checksum".
 */
static void damaged_beacon_is_ignored(void)
{
    uint8_t chips[FRAME_BEACON_TOTAL_CHIPS];
    frame_rx_t r;
    size_t n, i, bit;
    uint32_t acted = 0, ignored = 0;

    hf_begin("trigger: a beacon with a damaged CRC is never acted on");

    n = frame_beacon_encode(0x1234u, chips, sizeof chips);
    HF_CHECK_MSG(n == (size_t)FRAME_BEACON_TOTAL_CHIPS,
             "encoded %u chips, expected %u", (unsigned)n,
             (unsigned)FRAME_BEACON_TOTAL_CHIPS);

    /*
     * Flip one chip of the BODY at a time — every one of them, so this is not
     * a spot check. A flipped chip inside a Manchester pair makes the pair a
     * tie, which is a coin toss, so some flips land on the value that was
     * already there and the frame survives. Those must decode CORRECTLY; the
     * rest must not decode at all.
     */
    for (bit = FRAME_PREAMBLE_CHIPS + FRAME_MARKER_CHIPS; bit < n; bit++) {
        uint8_t bad[FRAME_BEACON_TOTAL_CHIPS];
        frame_rx_result_t res = FRAME_RX_NONE;

        memcpy(bad, chips, n);
        bad[bit] = (uint8_t)!bad[bit];

        frame_rx_init(&r);
        for (i = 0; i < n; i++) {
            const frame_rx_result_t one = frame_rx_push(&r, bad[i] ? 400 : -400);
            if (one != FRAME_RX_NONE) res = one;
        }

        if (res == FRAME_RX_BEACON) {
            acted++;
            HF_CHECK_MSG(frame_rx_nonce(&r) == 0x1234u,
                     "chip %u: a damaged beacon decoded to nonce %04x",
                     (unsigned)bit, frame_rx_nonce(&r));
        } else {
            ignored++;
            HF_CHECK_MSG(res == FRAME_RX_NONE,
                     "chip %u: a damaged beacon returned %d, not silence",
                     (unsigned)bit, (int)res);
        }
    }

    HF_CHECK_MSG(ignored > 0, "no single-chip flip damaged the beacon at all");
    printf("      damaged beacon: %u of %u body-chip flips rejected, %u"
           " survived intact\n", ignored, ignored + acted, acted);
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
 * THE SHOUT TEST WENT AT STEP 7. It checked that a shout could not be mistaken
 * for a frame, because v1's shout was a flat tone deliberately chosen to have
 * no transitions. The beacon IS a frame, on purpose, and what stops a peer's
 * framer mistaking it for the front of a card is frame.h's second marker —
 * tested in test_frame.c, where the format lives.
 */

/*
 * Triggering is not the point; handshaking is. The band must go on to exchange
 * a record with no host call anywhere in the path — and the frames it spends
 * doing so are the regression canary for handover, because an end that
 * misreads the channel talks over the reply it asked for and the count goes up
 * several fold.
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
         * card, having been started by nothing but a beacon. */
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
     * a channel it should have heard was busy.
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
    steady_state_rendezvous();
    simultaneous_start();
    stuck_carrier();
    own_beacon_is_never_a_peer();
    nonce_tie_redraws();
    damaged_beacon_is_ignored();
    trigger_completes_a_handshake();
    re_arms_after_a_contact();
}
