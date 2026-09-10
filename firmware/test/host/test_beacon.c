/*
 * Handoff — contact detection. beacon.h.
 *
 * The claim under test is the rendezvous guarantee: two bands free-running at
 * arbitrary relative phase find each other within a bounded time of touching.
 * A test that only tried one phase would pass on luck, so the phase is swept
 * across a whole beacon period at a resolution finer than a sniff window.
 */
#include <string.h>

#include "beacon.h"
#include "hal_host.h"
#include "hf_test.h"
#include "link_sm.h"
#include "tests.h"

/* Fine enough that no sniff window or beacon burst can be stepped over. */
#define STEP_US 100u

/*
 * A whole period plus the jitter range, plus one beacon and the detector's
 * latency. That is the worst case the header claims when the two bands do not
 * collide; the collision case needs another round, hence the doubling in
 * the phase sweep below.
 */
#define WORST_CASE_US (HANDOFF_BEACON_PERIOD_US + HANDOFF_BEACON_JITTER_US + \
                       HANDOFF_BEACON_ON_US + 4u * ELECT_DETECT_US)

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

/* ---------------------------------------------------------------------- */

/*
 * A band alone in a room must stay in IDLE for ever. This is the test that
 * would fail if the carrier detector were fed during a beacon: the band would
 * wake on its own amplifier, every period, and burn a contact budget against
 * nobody.
 */
static void alone_never_wakes(void)
{
    bsim_t s;
    uint64_t t;

    hf_begin("beacon: a band alone never wakes");
    bsim_init(&s, 99);
    halh_set_coupled(&s.a, &s.b, false);

    link_sm_idle(&s.sm_a, s.clock_us);
    link_sm_idle(&s.sm_b, s.clock_us);

    for (t = 0; t < 20u * HANDOFF_BEACON_PERIOD_US; t += STEP_US)
        bsim_step(&s);

    HF_CHECK_MSG(!awake(&s.sm_a), "A woke with nothing to hear (state %s)",
             link_state_name(s.sm_a.state));
    HF_CHECK_MSG(!awake(&s.sm_b), "B woke with nothing to hear (state %s)",
             link_state_name(s.sm_b.state));

    /* It must actually have been trying, or the test above proves nothing. */
    HF_CHECK_MSG(s.sm_a.beacon.beacons >= 15,
             "A only beaconed %u times in 20 periods", s.sm_a.beacon.beacons);
    HF_CHECK_MSG(s.sm_a.beacon.sniffs > 100,
             "A only sniffed %u times", s.sm_a.beacon.sniffs);
}

/*
 * The guarantee. B is started PHASE microseconds after A, across a full period
 * and beyond, and every phase must rendezvous.
 */
static void every_phase_rendezvous(void)
{
    uint32_t phase;
    uint64_t worst = 0;
    uint32_t phases = 0, collided = 0;

    hf_begin("beacon: rendezvous at every relative phase");

    for (phase = 0; phase < HANDOFF_BEACON_PERIOD_US; phase += 1000u) {
        bsim_t s;
        uint64_t contact_us, limit, took = 0;
        bool started_b = false;

        bsim_init(&s, 0x5EED0000u + phase);
        halh_set_coupled(&s.a, &s.b, false);
        link_sm_idle(&s.sm_a, s.clock_us);

        /* Both bands are on wrists and beaconing before anyone shakes hands. */
        for (; s.clock_us < phase; ) {
            if (!started_b && s.clock_us >= phase) break;
            bsim_step(&s);
        }
        link_sm_idle(&s.sm_b, s.clock_us);
        started_b = true;
        (void)started_b;

        /* Let them free-run out of phase for a while, still not touching. */
        for (; s.clock_us < phase + 2u * HANDOFF_BEACON_PERIOD_US; )
            bsim_step(&s);

        HF_CHECK_MSG(!awake(&s.sm_a) && !awake(&s.sm_b),
                 "phase %u: woke before contact", phase);

        /* Hands meet. */
        contact_us = s.clock_us;
        halh_set_coupled(&s.a, &s.b, true);

        /* Two worst cases: one for the clean path, one more for a beacon
         * collision that has to decorrelate and retry. */
        limit = contact_us + 2u * WORST_CASE_US;
        while (s.clock_us < limit && !(awake(&s.sm_a) && awake(&s.sm_b)))
            bsim_step(&s);

        took = s.clock_us - contact_us;
        if (took > worst) worst = took;
        if (took > WORST_CASE_US) collided++;
        phases++;

        HF_CHECK_MSG(awake(&s.sm_a) && awake(&s.sm_b),
                 "phase %u: no rendezvous in %llu us (A %s, B %s)", phase,
                 (unsigned long long)(s.clock_us - contact_us),
                 link_state_name(s.sm_a.state), link_state_name(s.sm_b.state));
    }

    HF_CHECK_MSG(phases > 50, "phase sweep only ran %u points", phases);

    /*
     * Collisions are allowed — they are the hole the header admits to — but if
     * most phases needed a retry then the jitter is not doing its job and the
     * latency claim is wrong.
     */
    HF_CHECK_MSG(collided * 4u <= phases,
             "%u of %u phases needed a second beacon round", collided, phases);

    printf("      rendezvous: worst %llu us over %u phases, %u needed a retry\n",
           (unsigned long long)worst, phases, collided);
}

/*
 * Waking is not the point; handshaking is. The band must go on to elect a role
 * and exchange a record with no host call anywhere in the path.
 */
static void rendezvous_completes_a_handshake(void)
{
    bsim_t s;
    uint64_t limit;
    uint32_t seed;

    hf_begin("beacon: rendezvous leads to a completed exchange");

    for (seed = 0; seed < 8; seed++) {
        elect_role_t first_a = ELECT_ROLE_NONE, first_b = ELECT_ROLE_NONE;
        const uint8_t *got = NULL;
        size_t n;

        bsim_init(&s, 0xC0FFEEu + seed);
        halh_set_coupled(&s.a, &s.b, false);
        link_sm_idle(&s.sm_a, s.clock_us);

        for (; s.clock_us < 37000u + seed * 4200u; ) bsim_step(&s);
        link_sm_idle(&s.sm_b, s.clock_us);
        for (; s.clock_us < 200000u; ) bsim_step(&s);

        halh_set_coupled(&s.a, &s.b, true);

        limit = s.clock_us + 2u * WORST_CASE_US + s.cfg.contact_budget_us;
        while (s.clock_us < limit &&
               !(s.sm_a.state == LINK_COMPLETE && s.sm_b.state == LINK_COMPLETE)) {
            bsim_step(&s);
            /*
             * Roles are sampled the FIRST time each end leaves the election.
             * The end-of-run role proves nothing: suspect_collision() redraws
             * mid-exchange, so two ends that ran a perfectly good handshake
             * can both be sitting on the same role by the time it finishes.
             */
            if (first_a == ELECT_ROLE_NONE && s.sm_a.state >= LINK_TX_FRAME)
                first_a = link_sm_role(&s.sm_a);
            if (first_b == ELECT_ROLE_NONE && s.sm_b.state >= LINK_TX_FRAME)
                first_b = link_sm_role(&s.sm_b);
        }

        HF_CHECK_MSG(s.sm_a.state == LINK_COMPLETE && s.sm_b.state == LINK_COMPLETE,
                 "seed %u: A %s, B %s", seed,
                 link_state_name(s.sm_a.state), link_state_name(s.sm_b.state));

        HF_CHECK_MSG((first_a == ELECT_ROLE_INITIATOR) !=
                     (first_b == ELECT_ROLE_INITIATOR),
                 "seed %u: election gave roles %d / %d", seed,
                 (int)first_a, (int)first_b);

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
    }
}

/*
 * The beacon must not look like a frame. A constant-on burst has no
 * transitions, so frame.c's preamble hunt can never lock to it — if this ever
 * fails, someone made the beacon alternate and turned it into a preamble.
 */
static void beacon_is_not_a_preamble(void)
{
    uint8_t chips[BEACON_BURST_CHIPS];
    frame_rx_t r;
    size_t n, i;

    hf_begin("beacon: a burst cannot be mistaken for a frame");

    n = beacon_fill(chips, sizeof chips);
    HF_CHECK_MSG(n == (size_t)BEACON_BURST_CHIPS, "filled %u chips", (unsigned)n);

    for (i = 0; i < n; i++)
        HF_CHECK_MSG(chips[i] == 1u, "chip %u is not on: the beacon has a transition",
                 (unsigned)i);

    /* Feed many bursts back to back through a framer and demand it never
     * syncs, let alone reports a frame. */
    frame_rx_init(&r);
    for (i = 0; i < 40u * n; i++)
        (void)frame_rx_push(&r, 400);

    HF_CHECK_MSG(r.syncs == 0, "framer synced %u times on a beacon", r.syncs);
    HF_CHECK_MSG(r.frames_good == 0 && r.frames_bad_crc == 0,
             "framer produced %u good and %u bad frames from a beacon",
             r.frames_good, r.frames_bad_crc);
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

    hf_begin("beacon: a band re-arms after a handshake");
    bsim_init(&s, 4242);
    halh_set_coupled(&s.a, &s.b, false);
    link_sm_idle(&s.sm_a, s.clock_us);
    link_sm_idle(&s.sm_b, s.clock_us);
    for (; s.clock_us < 150000u; ) bsim_step(&s);

    halh_set_coupled(&s.a, &s.b, true);
    limit = s.clock_us + 2u * WORST_CASE_US + s.cfg.contact_budget_us;
    while (s.clock_us < limit && s.sm_a.state != LINK_COMPLETE) bsim_step(&s);
    HF_CHECK_MSG(s.sm_a.state == LINK_COMPLETE, "first handshake: A %s",
             link_state_name(s.sm_a.state));

    /* Hands part, both bands go back on the shelf. */
    halh_set_coupled(&s.a, &s.b, false);
    link_sm_idle(&s.sm_a, s.clock_us);
    link_sm_idle(&s.sm_b, s.clock_us);
    for (; s.clock_us < limit + 300000u; ) bsim_step(&s);
    HF_CHECK_MSG(!awake(&s.sm_a), "A woke again with nobody there");

    /* And a second, different person. */
    halh_set_coupled(&s.a, &s.b, true);
    limit = s.clock_us + 2u * WORST_CASE_US;
    while (s.clock_us < limit && !(awake(&s.sm_a) && awake(&s.sm_b)))
        bsim_step(&s);
    HF_CHECK_MSG(awake(&s.sm_a) && awake(&s.sm_b),
             "second contact never rendezvoused (A %s, B %s)",
             link_state_name(s.sm_a.state), link_state_name(s.sm_b.state));
}

void test_beacon(void)
{
    alone_never_wakes();
    every_phase_rendezvous();
    rendezvous_completes_a_handshake();
    beacon_is_not_a_preamble();
    re_arms_after_a_contact();
}
