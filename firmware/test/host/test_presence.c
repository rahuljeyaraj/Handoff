/*
 * Link v2 step 5. dsp/presence.c — CFAR presence, and the k it is built from.
 *
 * What replaced test_beacon.c's six floor checks. Those asked whether a
 * remembered number could be poisoned by the signal; none of them has a
 * meaning here, because nothing about the signal is remembered. What is
 * checkable instead is the arithmetic, the derivation behind the one constant,
 * and the two failure shapes that WOULD matter if either went wrong: going
 * deaf, and calling an empty room busy.
 */
#include <math.h>

#include "chan.h"
#include "frame.h"
#include "gz_bank.h"
#include "hf_test.h"
#include "presence.h"
#include "tests.h"

#define PI 3.14159265358979323846

/* A score, as mag^2 — the inverse of gz_score_of(). */
static uint64_t mag2_of(uint32_t score)
{
    const uint64_t a = (uint64_t)score * (uint64_t)HANDOFF_GZ_N / 2u;
    return a * a;
}

/* Windows of a quiet room: the signal bins hold nothing but the same noise the
 * guards do. Returns how many of them read busy. */
static uint32_t quiet_windows(presence_t *p, rng_t *rng, uint32_t noise,
                              uint32_t windows)
{
    uint32_t busy = 0, w;

    for (w = 0; w < windows; w++) {
        /* Rayleigh-ish: a magnitude whose square is exponential, which is what
         * one Goertzel bin of Gaussian noise gives. */
        const double u = 1e-9 + rng_uniform(rng);
        const double e = -log(u);              /* exponential, mean 1 */
        const uint32_t s = (uint32_t)(noise * sqrt(e));

        const double ug = 1e-9 + rng_uniform(rng);
        const uint32_t g = (uint32_t)(noise * sqrt(-log(ug)));

        if (presence_push(p, mag2_of(s), mag2_of(g),
                          (w % HANDOFF_GUARD_DECIM) == 0u))
            busy++;
    }
    return busy;
}


void test_presence(void)
{
    /*
     * ONE. k IS THE FORMULA, NOT A NUMBER SOMEONE LIKED.
     *
     * config.h cannot take a fortieth root, so the constant is written there
     * and checked here. Changing HANDOFF_CFAR_FALSE_BUSY_S, the window rate,
     * the decimation, the windows per chip or the preamble and not the
     * constant fails at this line rather than on a bench.
     *
     *      P_fa = (1 + k/N)^-N      =>      k = N * (P_fa^(-1/N) - 1)
     */
    {
        const double n    = (double)HANDOFF_CFAR_CELLS;
        const double pfa  = 1.0 / (double)HANDOFF_CFAR_PFA_INV;
        const double want = n * (pow(pfa, -1.0 / n) - 1.0);
        const double got  = (double)HANDOFF_CFAR_K_NUM
                          / (double)HANDOFF_CFAR_K_DEN;

        hf_begin("presence: k is computed from the stated false-busy rate");
        HF_CHECK_MSG(fabs(got - want) < 0.01,
                 "HANDOFF_CFAR_K is %.4f; the formula gives %.4f for "
                 "%d cells at one false busy per %u s",
                 got, want, (int)HANDOFF_CFAR_CELLS,
                 (unsigned)HANDOFF_CFAR_FALSE_BUSY_S);
    }

    /*
     * TWO. THE REFERENCE IS ONE PREAMBLE LONG, and config.h says so without
     * being able to see frame.h. This is the join.
     */
    {
        hf_begin("presence: the CFAR reference spans one preamble");
        HF_EQ_INT(HANDOFF_CFAR_REF_CHIPS, FRAME_PREAMBLE_CHIPS);
        HF_EQ_INT(HANDOFF_CFAR_CELLS,
                  FRAME_PREAMBLE_CHIPS * HANDOFF_WINDOWS_PER_CHIP
                      / HANDOFF_GUARD_DECIM);
        /* Every chip phase the same number of times — the preprocessor checks
         * the multiple, this checks the count it implies. */
        HF_EQ_INT(HANDOFF_CFAR_CELLS % HANDOFF_WINDOWS_PER_CHIP, 0);
    }

    /*
     * THREE. NO ANSWER UNTIL THE REFERENCE IS FULL. A threshold taken against
     * fewer cells than k was derived for is not the threshold that was
     * derived, so it is not taken at all.
     */
    {
        presence_t p;
        uint32_t w;

        hf_begin("presence: silent until the reference is full");
        presence_init(&p);

        for (w = 0; w < HANDOFF_CFAR_CELLS - 1u; w++) {
            const bool busy = presence_push(&p, mag2_of(10000), mag2_of(1),
                                            true);
            HF_CHECK_MSG(!busy, "busy at cell %u of %d with a deafening signal",
                     (unsigned)w, (int)HANDOFF_CFAR_CELLS);
            if (hf_failures) break;
        }
        HF_CHECK(!presence_ready(&p));
        HF_CHECK(presence_push(&p, mag2_of(10000), mag2_of(1), true));
        HF_CHECK(presence_ready(&p));
    }

    /*
     * FOUR. THE THRESHOLD IS WHERE k PUTS IT, to the LSB either side.
     *
     * The reference is filled with one exact value so mean(reference) is that
     * value and nothing is left to argue about. A signal at k times it in
     * POWER must be busy; a signal a hair under must not.
     */
    {
        presence_t p;
        const uint64_t ref = mag2_of(100);
        const uint64_t thr = ref * HANDOFF_CFAR_K_NUM / HANDOFF_CFAR_K_DEN;
        uint32_t w;

        hf_begin("presence: busy exactly where k says");
        presence_init(&p);
        for (w = 0; w < HANDOFF_CFAR_CELLS; w++)
            (void)presence_push(&p, 0, ref, true);

        HF_CHECK_MSG(!presence_push(&p, thr - 2u, 0, false),
                 "busy just under k * noise");
        HF_CHECK_MSG(presence_push(&p, thr + 2u, 0, false),
                 "not busy just over k * noise");

        /* And the scores a console prints are the two sides of that same
         * comparison, not two different measurements. */
        HF_EQ_INT(presence_noise_score(&p), 100);
    }

    /*
     * FIVE. A WRECKED GUARD CANNOT DEAFEN IT. This is the median earning its
     * place, and it is not hypothetical: step 3 read the three guards at
     * 2, 23 and 391 through a railed self loop, where the mean would have been
     * 138 and 138 is a deaf receiver.
     *
     * The bank takes the median, so what is checked here is that presence uses
     * gzb_noise() and not an average — fed a bank whose guards are that exact
     * triple, a real signal must still read busy.
     */
    {
        gz_bank_t  b;
        presence_t p;
        uint32_t   w;

        hf_begin("presence: one wrecked guard cannot deafen it");
        gzb_init(&b);
        presence_init(&p);

        b.mag2[GZB_G_LO]  = mag2_of(391);
        b.mag2[GZB_G_MID] = mag2_of(23);
        b.mag2[GZB_G_HI]  = mag2_of(2);
        b.guards_fresh    = true;
        b.mag2[GZB_A]     = 0;
        b.mag2[GZB_B]     = 0;

        for (w = 0; w < HANDOFF_CFAR_CELLS; w++) (void)presence_push_bank(&p, &b);
        /* Against the round trip, not against 23: mag2_of() truncates, and
         * what is being checked is which of the three got picked. */
        HF_EQ_INT(presence_noise_score(&p), gzb_score(mag2_of(23)));

        /* 391 * k would be 6553. The median is 23, so 500 is loud. */
        b.mag2[GZB_B] = mag2_of(500);
        HF_CHECK_MSG(presence_push_bank(&p, &b),
                 "a 500 signal read quiet against guards 391/23/2");
    }

    /*
     * SIX. A DECIMATED GUARD IS COUNTED ONCE, NOT FOUR TIMES. Between guard
     * windows the bank holds its last value; taking it again would weight one
     * reading HANDOFF_GUARD_DECIM times and make the reference span a quarter
     * of the room it claims to.
     */
    {
        presence_t p;
        uint32_t w;

        hf_begin("presence: a stale guard does not enter the reference");
        presence_init(&p);
        for (w = 0; w < HANDOFF_CFAR_CELLS * HANDOFF_GUARD_DECIM; w++)
            (void)presence_push(&p, 0, mag2_of(50),
                                (w % HANDOFF_GUARD_DECIM) == 0u);
        HF_EQ_INT(p.cells, HANDOFF_CFAR_CELLS);
        HF_EQ_INT(p.windows, HANDOFF_CFAR_CELLS * HANDOFF_GUARD_DECIM);
    }

    /*
     * SEVEN. AN EMPTY ROOM IS NOT BUSY — AND IT STAYS NOT BUSY WHILE THE ROOM
     * GETS LOUDER, WHICH IS THE WHOLE POINT.
     *
     * v1's floor climbed until the detector went deaf, and it climbed because
     * the room and the signal shared a channel. Here they do not, so the same
     * sweep that killed v1 must change nothing: the noise is raised 64-fold
     * and the false-busy count must not move, because the ratio has not.
     *
     * The rate check is loose on purpose. The stated requirement is one false
     * busy per HANDOFF_CFAR_FALSE_BUSY_S of listening, which at the window
     * rate is about one in 1.2 million windows; a test that ran long enough to
     * measure that would not be a unit test. What 200 000 windows CAN reject
     * is a k off by an order of magnitude, which is the mistake worth
     * catching.
     */
    {
        static const uint32_t k_noise[] = { 8, 32, 128, 512 };
        rng_t rng;
        size_t i;

        hf_begin("presence: a quiet room stays quiet however loud it gets");
        for (i = 0; i < sizeof k_noise / sizeof k_noise[0]; i++) {
            presence_t p;
            uint32_t busy;

            rng_seed(&rng, 20260925u + (uint32_t)i);
            presence_init(&p);
            busy = quiet_windows(&p, &rng, k_noise[i], 200000u);

            HF_CHECK_MSG(busy <= 20u,
                     "noise %u: %u false busies in 200000 windows",
                     (unsigned)k_noise[i], (unsigned)busy);
        }
    }

    /*
     * EIGHT. AND A REAL SIGNAL IS HEARD AT EVERY ONE OF THOSE LEVELS. The
     * other half of seven: a detector that never fires is trivially free of
     * false alarms.
     *
     * Step 4 measured the CFAR margin on a plate path at 94 in power for tone
     * B and 52 for tone A, against a k of about 17. 50x is inside both, so
     * this asks for less than the bench delivered — and it has to be well
     * clear of k rather than just over it, because the reference is 40 random
     * cells and its mean carries about 16 % of scatter.
     */
    {
        static const uint32_t k_noise[] = { 8, 32, 128, 512 };
        rng_t rng;
        size_t i;

        hf_begin("presence: a signal 50x the room is heard at every level");
        for (i = 0; i < sizeof k_noise / sizeof k_noise[0]; i++) {
            presence_t p;
            uint32_t w, heard = 0;

            rng_seed(&rng, 7u + (uint32_t)i);
            presence_init(&p);
            (void)quiet_windows(&p, &rng, k_noise[i], HANDOFF_CFAR_CELLS * 8u);

            /* 50x in power is sqrt(50) in score. */
            for (w = 0; w < 100u; w++) {
                const uint32_t s = (uint32_t)(k_noise[i] * 7.071);
                if (presence_push(&p, mag2_of(s), 0, false)) heard++;
            }
            HF_CHECK_MSG(heard == 100u,
                     "noise %u: heard %u of 100 windows at 50x",
                     (unsigned)k_noise[i], (unsigned)heard);
        }
    }

    /*
     * NINE. NO HYSTERESIS. busy is this window's verdict and nothing else —
     * there is no hold, so it must drop the window the signal does.
     *
     * This is a REQUIREMENT, not an observation. v1's eight-chip hold existed
     * to paper over a floor that could lapse mid-frame; keeping one here would
     * put state back into the one decision that was rebuilt to have none, and
     * link_sm.c's turn keepalive is written assuming there is no hold (it
     * leans on frame_rx_busy() instead).
     */
    {
        presence_t p;
        uint32_t w;

        hf_begin("presence: no hold, no hysteresis");
        presence_init(&p);
        for (w = 0; w < HANDOFF_CFAR_CELLS; w++)
            (void)presence_push(&p, 0, mag2_of(10), true);

        HF_CHECK(presence_push(&p, mag2_of(1000), 0, false));
        HF_CHECK_MSG(!presence_push(&p, 0, 0, false),
                 "still busy the window after the signal stopped");
    }

    /*
     * TEN. THE PEAK, which is the only thing a slow reader can read.
     *
     * The failure this guards is not arithmetic, it is sampling: the phone's
     * Body link block goes out twice a second and a beacon is on air for
     * eleven milliseconds, so an instantaneous read misses it roughly two
     * hundred windows out of two hundred and one. The peak must survive the
     * whole interval, must come back with the reference from ITS OWN window
     * and not the latest one, and must reset on the take so the next interval
     * is the next interval.
     */
    {
        presence_t p;
        uint32_t w, sig = 0, noi = 0;

        hf_begin("presence: the peak survives a slow reader");
        presence_init(&p);
        for (w = 0; w < HANDOFF_CFAR_CELLS; w++)
            (void)presence_push(&p, 0, mag2_of(10), true);

        /* One loud window, then two hundred silent ones: the sampling ratio
         * the phone actually reads at. */
        (void)presence_push(&p, mag2_of(400), 0, false);
        for (w = 0; w < 200u; w++) (void)presence_push(&p, mag2_of(20), 0, false);

        presence_take_peak(&p, &sig, &noi);
        HF_CHECK_MSG(sig == 400u, "peak signal %u, wanted 400", (unsigned)sig);
        HF_CHECK_MSG(noi == 10u, "peak reference %u, wanted 10", (unsigned)noi);

        /* A take is a take. The next interval starts from the room, and the
         * room is what it reports — never a peak of zero, which a reader
         * would have to tell apart from silence. */
        for (w = 0; w < 10u; w++) (void)presence_push(&p, mag2_of(20), 0, false);
        presence_take_peak(&p, &sig, &noi);
        HF_CHECK_MSG(sig == 20u, "peak did not reset: %u", (unsigned)sig);

        /* And the reference is the one from the peak's window, not the last:
         * push a loud window against a quiet room, then flood the boxcar. */
        presence_init(&p);
        for (w = 0; w < HANDOFF_CFAR_CELLS; w++)
            (void)presence_push(&p, 0, mag2_of(10), true);
        (void)presence_push(&p, mag2_of(400), 0, false);
        for (w = 0; w < HANDOFF_CFAR_CELLS; w++)
            (void)presence_push(&p, 0, mag2_of(200), true);

        presence_take_peak(&p, &sig, &noi);
        HF_CHECK_MSG(sig == 400u, "peak signal %u, wanted 400", (unsigned)sig);
        HF_CHECK_MSG(noi == 10u,
                 "peak reference %u is the room NOW, not the room at the peak",
                 (unsigned)noi);
    }
}
