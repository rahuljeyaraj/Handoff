#include <math.h>

#include "chan.h"
#include "goertzel.h"
#include "hf_test.h"
#include "tests.h"

#define PI 3.14159265358979323846

/* Feed one window of a pure tone at `hz` and return the score. */
static uint32_t score_tone(double hz, double amp, double dc, double phase)
{
    gz_t g;
    uint32_t s = 0;
    int i;

    gz_init(&g, HANDOFF_GZ_N, HANDOFF_GZ_BIN);
    for (i = 0; i < HANDOFF_GZ_N; i++) {
        const double t = (double)i / (double)HANDOFF_ADC_FS_HZ;
        const double v = dc + amp * sin(2.0 * PI * hz * t + phase);
        gz_push(&g, (int16_t)(v + (v < 0 ? -0.5 : 0.5)), &s);
    }
    return s;
}

void test_goertzel(void)
{
    hf_begin("goertzel: isqrt is exact on perfect squares");
    {
        uint64_t v;
        for (v = 0; v < 1000; v++) HF_EQ_INT(gz_isqrt64(v * v), v);
        HF_EQ_INT(gz_isqrt64(0xFFFFFFFFull * 0xFFFFFFFFull), 0xFFFFFFFFull);
        HF_EQ_INT(gz_isqrt64(15), 3);
        HF_EQ_INT(gz_isqrt64(16), 4);
        HF_EQ_INT(gz_isqrt64(24), 4);
    }

    /*
     * The score is normalised so it reads as amplitude. A 200 LSB carrier —
     * the design §5 link budget figure at the ADC — should score near 200.
     */
    hf_begin("goertzel: score tracks amplitude");
    {
        const uint32_t s200 = score_tone(HANDOFF_CARRIER_HZ, 200.0, 0.0, 0.0);
        const uint32_t s400 = score_tone(HANDOFF_CARRIER_HZ, 400.0, 0.0, 0.0);
        HF_NEAR(s200, 200.0, 12.0);
        HF_NEAR((double)s400 / (double)s200, 2.0, 0.1);
    }

    /*
     * Design §10.3: the carrier sits exactly on a bin centre, so a window
     * holds a whole number of carrier cycles. The consequence is that the
     * score does not depend on where in the cycle the window started — which
     * matters, because nothing aligns the ADC to the far end's carrier.
     */
    hf_begin("goertzel: score is independent of carrier phase");
    {
        const uint32_t ref = score_tone(HANDOFF_CARRIER_HZ, 200.0, 0.0, 0.0);
        int k;
        for (k = 1; k < 8; k++) {
            const uint32_t s = score_tone(HANDOFF_CARRIER_HZ, 200.0, 0.0,
                                          2.0 * PI * (double)k / 8.0);
            HF_NEAR((double)s, (double)ref, 8.0);
        }
    }

    /*
     * DC is bin 0, and an integer number of cycles per window means zero
     * leakage from it. This is why the ADC's 1.65 V bias needs no removal
     * before the filter — worth pinning, because "subtract the DC first" is
     * the obvious-looking change that would cost cycles for nothing.
     */
    hf_begin("goertzel: a full-scale DC bias contributes nothing");
    {
        const uint32_t clean = score_tone(HANDOFF_CARRIER_HZ, 200.0, 0.0, 0.0);
        const uint32_t biased = score_tone(HANDOFF_CARRIER_HZ, 200.0, 2000.0, 0.0);
        HF_NEAR((double)biased, (double)clean, 8.0);
        HF_CHECK(score_tone(HANDOFF_CARRIER_HZ, 0.0, 2000.0, 0.0) < 8);
    }

    hf_begin("goertzel: an off-bin tone is rejected");
    {
        const uint32_t on  = score_tone(HANDOFF_CARRIER_HZ, 200.0, 0.0, 0.0);
        const uint32_t adj = score_tone(HANDOFF_CARRIER_HZ + HANDOFF_WINDOW_RATE_HZ,
                                        200.0, 0.0, 0.0);
        const uint32_t hum = score_tone(50.0, 200.0, 0.0, 0.0);
        HF_CHECK_MSG(adj * 8u < on, "adjacent bin %u vs on-bin %u",
                     (unsigned)adj, (unsigned)on);
        HF_CHECK_MSG(hum * 8u < on, "50 Hz hum %u vs on-bin %u",
                     (unsigned)hum, (unsigned)on);
    }

    /*
     * A gated carrier is what OOK actually produces. An "off" chip must score
     * far below an "on" chip or the Manchester comparison has nothing to work
     * with.
     */
    hf_begin("goertzel: gated off scores near zero");
    {
        HF_CHECK(score_tone(HANDOFF_CARRIER_HZ, 0.0, 0.0, 0.0) < 4);
    }

    /*
     * Processing gain. Design §5 claims +17 dB at N = 50; the same argument
     * gives +14 dB at N = 25. Rather than assert the decibels, assert the
     * mechanism, which is what the claim rests on: the score is normalised so
     * a tone reads as its amplitude regardless of N, while noise averages down
     * as 1/sqrt(N). So doubling N must drop the noise score by about sqrt(2)
     * and leave the tone where it was — three decibels, bought and paid for.
     */
    hf_begin("goertzel: noise averages down as sqrt(N), signal does not");
    {
        rng_t rng;
        double noise_score[2], tone_score[2];
        int pass;

        for (pass = 0; pass < 2; pass++) {
            const uint16_t n = (uint16_t)(pass ? 50 : 25);
            const uint16_t bin = (uint16_t)(HANDOFF_CARRIER_HZ / (HANDOFF_ADC_FS_HZ / n));
            double noise = 0.0, tone = 0.0;
            int w;
            const int windows = 2000;

            rng_seed(&rng, 42);
            for (w = 0; w < windows; w++) {
                gz_t g;
                uint32_t s = 0;
                int i;

                gz_init(&g, n, bin);
                for (i = 0; i < n; i++)
                    gz_push(&g, (int16_t)(300.0 * rng_normal(&rng)), &s);
                noise += s;

                gz_init(&g, n, bin);
                for (i = 0; i < n; i++) {
                    const double t = (double)i / (double)HANDOFF_ADC_FS_HZ;
                    gz_push(&g, (int16_t)(200.0 * sin(2.0 * PI * HANDOFF_CARRIER_HZ * t)), &s);
                }
                tone += s;
            }
            noise_score[pass] = noise / windows;
            tone_score[pass] = tone / windows;
        }

        HF_NEAR(tone_score[0], 200.0, 12.0);
        HF_NEAR(tone_score[1], 200.0, 12.0);
        HF_NEAR(noise_score[0] / noise_score[1], 1.414, 0.08);
    }
}
