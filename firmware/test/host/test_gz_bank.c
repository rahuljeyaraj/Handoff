#include <math.h>

#include "chan.h"
#include "gz_bank.h"
#include "hf_test.h"
#include "tests.h"

#define PI 3.14159265358979323846

/*
 * A two-tone source. `hz` is what the transmitter is sending; `spur_hz` is
 * anything else in the room. Both are exact bin centres in every use below,
 * so a window holds a whole number of cycles of each and there is no leakage
 * to argue about — which is the property the whole bank rests on.
 */
typedef struct {
    double   hz, amp;
    double   spur_hz, spur_amp;
    double   noise;
    uint32_t t;          /* sample index, so phase is continuous across windows */
    rng_t    rng;
} src_t;

static void src_init(src_t *s, double hz, double amp)
{
    s->hz = hz; s->amp = amp;
    s->spur_hz = 0.0; s->spur_amp = 0.0;
    s->noise = 0.0;
    s->t = 0;
    rng_seed(&s->rng, 7);
}

static int16_t src_next(src_t *s)
{
    const double t = (double)s->t++ / (double)HANDOFF_ADC_FS_HZ;
    double v = 0.0;

    if (s->amp > 0.0)      v += s->amp * sin(2.0 * PI * s->hz * t);
    if (s->spur_amp > 0.0) v += s->spur_amp * sin(2.0 * PI * s->spur_hz * t);
    if (s->noise > 0.0)    v += s->noise * rng_normal(&s->rng);

    return (int16_t)(v + (v < 0 ? -0.5 : 0.5));
}

/* Run whole windows through the bank. Returns the number completed. */
static uint32_t run_windows(gz_bank_t *b, src_t *s, int windows)
{
    uint32_t done = 0;
    int i, w;

    for (w = 0; w < windows; w++)
        for (i = 0; i < HANDOFF_GZ_N; i++)
            if (gzb_push(b, src_next(s))) done++;

    return done;
}

static int gcd_i(int a, int c) { while (c) { int t = a % c; a = c; c = t; } return a; }

void test_gz_bank(void)
{
    /*
     * config.h derives the decimation from a requirement it cannot fully
     * check at build time: the guard windows must walk every phase of a chip
     * rather than landing on one forever. The preprocessor cannot compute a
     * gcd; this can.
     */
    hf_begin("bank: guard decimation is coprime with the windows per chip");
    {
        HF_EQ_INT(gcd_i(HANDOFF_GUARD_DECIM, HANDOFF_WINDOWS_PER_CHIP), 1);
        HF_CHECK(HANDOFF_GUARD_DECIM >= 1);
    }

    hf_begin("bank: the bins are the design's five, in order");
    {
        gz_bank_t b;
        gzb_init(&b);
        HF_EQ_INT(b.bin[GZB_A].k,     HANDOFF_TONE_A_BIN);
        HF_EQ_INT(b.bin[GZB_B].k,     HANDOFF_TONE_B_BIN);
        HF_EQ_INT(b.bin[GZB_G_LO].k,  HANDOFF_GUARD_LO_BIN);
        HF_EQ_INT(b.bin[GZB_G_MID].k, HANDOFF_GUARD_MID_BIN);
        HF_EQ_INT(b.bin[GZB_G_HI].k,  HANDOFF_GUARD_HI_BIN);
        HF_EQ_INT(b.bin[GZB_A].n,     HANDOFF_GZ_N);
        HF_EQ_INT(HANDOFF_TONE_A_HZ,  180000);
        HF_EQ_INT(HANDOFF_TONE_B_HZ,  200000);
    }

    /*
     * The one measurement the whole design rests on: with tone A transmitted,
     * tone B's bin and all three guards must be empty in the SAME window. On
     * bin centres that is exact arithmetic, not a margin — so the check is
     * hard, not "mostly".
     */
    hf_begin("bank: one tone fills one bin and leaves the other four empty");
    {
        gz_bank_t b;
        src_t s;
        int tone;

        for (tone = 0; tone < 2; tone++) {
            const int hot  = tone ? GZB_B : GZB_A;
            const int cold = tone ? GZB_A : GZB_B;
            int i;

            gzb_init(&b);
            src_init(&s, tone ? HANDOFF_TONE_B_HZ : HANDOFF_TONE_A_HZ, 200.0);
            run_windows(&b, &s, 8);

            HF_NEAR(gzb_score(b.mag2[hot]), 200.0, 12.0);
            HF_CHECK_MSG(gzb_score(b.mag2[cold]) * 8u < gzb_score(b.mag2[hot]),
                         "tone %d: other tone bin %u against %u", tone,
                         (unsigned)gzb_score(b.mag2[cold]),
                         (unsigned)gzb_score(b.mag2[hot]));
            for (i = GZB_G_LO; i < GZB_BINS; i++)
                HF_CHECK_MSG(gzb_score(b.mag2[i]) * 8u < gzb_score(b.mag2[hot]),
                             "tone %d: guard %d at %u against %u", tone, i,
                             (unsigned)gzb_score(b.mag2[i]),
                             (unsigned)gzb_score(b.mag2[hot]));

            HF_EQ_INT(gzb_signal(&b), b.mag2[hot]);
            HF_EQ_INT(gzb_tone_is_b(&b), tone != 0);
        }
    }

    /*
     * A chip decision is a comparison of two numbers taken in the same
     * window, so it must survive the gain changing under it. Halve the
     * amplitude — a 6 dB swing, which a wrist moving does far worse than —
     * and the decision must not move. v1 needed a slicer to do this and the
     * slicer needed a threshold; there is no threshold here to drift.
     */
    hf_begin("bank: the chip decision does not move with gain");
    {
        double amp;
        for (amp = 800.0; amp > 3.0; amp /= 2.0) {
            gz_bank_t b;
            src_t s;

            gzb_init(&b);
            src_init(&s, HANDOFF_TONE_B_HZ, amp);
            run_windows(&b, &s, 8);
            HF_CHECK_MSG(gzb_tone_is_b(&b), "tone B at amplitude %.0f", amp);

            gzb_init(&b);
            src_init(&s, HANDOFF_TONE_A_HZ, amp);
            run_windows(&b, &s, 8);
            HF_CHECK_MSG(!gzb_tone_is_b(&b), "tone A at amplitude %.0f", amp);
        }
    }

    /*
     * The interferer case the brief names. One guard bin is sat on by
     * something loud — a switching supply, a phone charger, the band's own
     * motor — and the median of three must not notice. A mean would.
     */
    hf_begin("bank: one interfered guard cannot move the median");
    {
        gz_bank_t clean, spoilt;
        src_t s;
        uint64_t mean_clean = 0, mean_spoilt = 0;
        int i;

        gzb_init(&clean);
        src_init(&s, 0.0, 0.0);
        s.noise = 40.0;
        run_windows(&clean, &s, 40);

        gzb_init(&spoilt);
        src_init(&s, 0.0, 0.0);
        s.noise = 40.0;
        s.spur_hz  = HANDOFF_GUARD_MID_BIN * (double)HANDOFF_WINDOW_RATE_HZ;
        s.spur_amp = 900.0;
        run_windows(&spoilt, &s, 40);

        /* The interferer really is in the bank, and really is huge. */
        HF_CHECK_MSG(gzb_score(spoilt.mag2[GZB_G_MID]) > 500,
                     "interferer only scored %u",
                     (unsigned)gzb_score(spoilt.mag2[GZB_G_MID]));

        /* The median ignores it; the mean does not. This is the reason for
         * the median, stated as a number rather than as a claim. */
        HF_CHECK_MSG(gzb_noise(&spoilt) < 4u * gzb_noise(&clean) + 16u,
                     "median moved: %llu clean, %llu spoilt",
                     (unsigned long long)gzb_noise(&clean),
                     (unsigned long long)gzb_noise(&spoilt));

        for (i = GZB_G_LO; i < GZB_BINS; i++) {
            mean_clean  += clean.mag2[i]  / GZB_GUARDS;
            mean_spoilt += spoilt.mag2[i] / GZB_GUARDS;
        }
        HF_CHECK_MSG(mean_spoilt > 50u * (mean_clean + 1u),
                     "the mean should have been wrecked: %llu against %llu",
                     (unsigned long long)mean_spoilt,
                     (unsigned long long)mean_clean);
    }

    /*
     * Decimation. The guards run every HANDOFF_GUARD_DECIM-th window and hold
     * their value between — which is what makes them a quarter of the cost,
     * and it must be exactly that, not approximately.
     */
    hf_begin("bank: the guards run every Nth window and hold in between");
    {
        gz_bank_t b;
        src_t s;
        uint32_t w, fresh = 0;
        uint64_t held = 0;
        int i;

        gzb_init(&b);
        src_init(&s, HANDOFF_TONE_A_HZ, 300.0);
        s.noise = 30.0;

        for (w = 0; w < 40u; w++) {
            for (i = 0; i < HANDOFF_GZ_N; i++) (void)gzb_push(&b, src_next(&s));

            if (b.guards_fresh) {
                fresh++;
                held = b.mag2[GZB_G_LO];
                HF_EQ_INT(w % HANDOFF_GUARD_DECIM, 0);
            } else {
                HF_EQ_INT(b.mag2[GZB_G_LO], held);   /* held, not recomputed */
                HF_CHECK(w % HANDOFF_GUARD_DECIM != 0);
            }
        }

        HF_EQ_INT(b.windows, 40u);
        HF_EQ_INT(b.guard_windows, 40u / HANDOFF_GUARD_DECIM);
        HF_EQ_INT(fresh, 40u / HANDOFF_GUARD_DECIM);

        /* The tones are never decimated: every window has a fresh pair. */
        HF_NEAR(gzb_score(b.mag2[GZB_A]), 300.0, 20.0);
    }

    /*
     * The bank must be the five separate filters, not an approximation of
     * them: same samples, same window boundaries, same numbers to the bit.
     * Decimation is the only difference, so this compares a guard only on a
     * window it ran in.
     */
    hf_begin("bank: each bin equals the same Goertzel run alone");
    {
        gz_bank_t b;
        gz_t solo[GZB_BINS];
        uint64_t solo_mag2[GZB_BINS];
        src_t s;
        int i, w;
        const uint16_t bins[GZB_BINS] = {
            HANDOFF_TONE_A_BIN, HANDOFF_TONE_B_BIN,
            HANDOFF_GUARD_LO_BIN, HANDOFF_GUARD_MID_BIN, HANDOFF_GUARD_HI_BIN
        };

        gzb_init(&b);
        for (i = 0; i < GZB_BINS; i++) {
            gz_init(&solo[i], HANDOFF_GZ_N, bins[i]);
            solo_mag2[i] = 0;
        }

        src_init(&s, HANDOFF_TONE_B_HZ, 250.0);
        s.noise = 25.0;

        for (w = 0; w < 12; w++) {
            for (i = 0; i < HANDOFF_GZ_N; i++) {
                const int16_t v = src_next(&s);
                int k;
                (void)gzb_push(&b, v);
                for (k = 0; k < GZB_BINS; k++)
                    (void)gz_push_mag2(&solo[k], v, &solo_mag2[k]);
            }
            for (i = 0; i < GZB_BINS; i++) {
                if (i >= GZB_G_LO && !b.guards_fresh) continue;
                HF_EQ_INT(b.mag2[i], solo_mag2[i]);
            }
        }
    }

    /*
     * The ratio, taken on mag^2 with no square root and no division. This is
     * what lets gz_isqrt64 leave the hot path — every decision downstream is
     * one of these.
     */
    hf_begin("bank: ratios on mag^2 agree with the arithmetic");
    {
        HF_CHECK(gzb_ratio_gt(101, 10, 10, 1));     /* 101 > 100 */
        HF_CHECK(!gzb_ratio_gt(100, 10, 10, 1));    /* not strictly */
        HF_CHECK(!gzb_ratio_gt(99, 10, 10, 1));
        HF_CHECK(gzb_ratio_gt(7, 2, 3, 1));
        HF_CHECK(!gzb_ratio_gt(5, 2, 3, 1));
        HF_CHECK(gzb_ratio_gt(3, 2, 5, 4));         /* 3 > 2.5 */
        HF_CHECK(!gzb_ratio_gt(2, 2, 5, 4));
        HF_CHECK(!gzb_ratio_gt(0, 0, 62, 1));       /* silence is not busy */
        HF_CHECK(gzb_ratio_gt(1, 0, 62, 1));        /* anything beats nothing */
        HF_CHECK(!gzb_ratio_gt(5, 5, 1, 1));
        HF_CHECK(!gzb_ratio_gt(5, 5, 1, 0));        /* a zero denominator */

        /* The magnitudes this actually sees, scaled by the k step 5 will
         * derive: nothing here may wrap. */
        {
            const uint64_t big = (uint64_t)1 << 40;
            HF_CHECK(gzb_ratio_gt(big * 100, big, 62, 1));
            HF_CHECK(!gzb_ratio_gt(big * 61, big, 62, 1));
        }
    }

    /*
     * Presence, spelt out as §6 spells it: signal against the median of the
     * guards, in the same window. No floor, nothing remembered. The ratio
     * used here is an illustration only — step 5 derives the real one from a
     * stated false-busy rate.
     */
    hf_begin("bank: signal over guard median separates a tone from silence");
    {
        gz_bank_t quiet, loud;
        src_t s;

        gzb_init(&quiet);
        src_init(&s, 0.0, 0.0);
        s.noise = 40.0;
        run_windows(&quiet, &s, 40);

        gzb_init(&loud);
        src_init(&s, HANDOFF_TONE_A_HZ, 300.0);
        s.noise = 40.0;
        run_windows(&loud, &s, 40);

        HF_CHECK_MSG(gzb_ratio_gt(gzb_signal(&loud), gzb_noise(&loud), 62, 1),
                     "a 300 LSB tone did not read busy: %llu against %llu",
                     (unsigned long long)gzb_signal(&loud),
                     (unsigned long long)gzb_noise(&loud));
        HF_CHECK_MSG(!gzb_ratio_gt(gzb_signal(&quiet), gzb_noise(&quiet), 62, 1),
                     "noise alone read busy: %llu against %llu",
                     (unsigned long long)gzb_signal(&quiet),
                     (unsigned long long)gzb_noise(&quiet));

        /* And the same noise at ten times the gain still does not read busy,
         * because the guards rose with it. This is the line v1 could not
         * hold: its floor was a remembered number and the gain was not. */
        {
            gz_bank_t hot;
            gzb_init(&hot);
            src_init(&s, 0.0, 0.0);
            s.noise = 400.0;
            run_windows(&hot, &s, 40);
            HF_CHECK_MSG(!gzb_ratio_gt(gzb_signal(&hot), gzb_noise(&hot), 62, 1),
                         "loud noise read busy: %llu against %llu",
                         (unsigned long long)gzb_signal(&hot),
                         (unsigned long long)gzb_noise(&hot));
        }
    }
}
