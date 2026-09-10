/*
 * Handoff — tests for the simulator itself.
 *
 * The channel model is what every BER number and every sweep decision rests
 * on. If it is wrong, M1's exit criteria are met against a fiction and the
 * error is not discovered until M5 compares bench against simulator. So the
 * model gets tested like anything else.
 */
#include <math.h>
#include <string.h>

#include "chan.h"
#include "frame.h"
#include "hf_test.h"
#include "tests.h"

#define NCHIPS 64

static int16_t g_samples[CHAN_MAX_SAMPLES(NCHIPS)];

void test_channel(void)
{
    hf_begin("chan: the RNG is deterministic and reproducible");
    {
        rng_t a, b;
        int i;
        rng_seed(&a, 12345);
        rng_seed(&b, 12345);
        for (i = 0; i < 100; i++) HF_EQ_INT(rng_u32(&a), rng_u32(&b));
    }

    hf_begin("chan: the gaussian has roughly the right moments");
    {
        rng_t r;
        double sum = 0.0, sumsq = 0.0;
        int i;
        const int n = 200000;

        rng_seed(&r, 5);
        for (i = 0; i < n; i++) {
            const double v = rng_normal(&r);
            sum += v;
            sumsq += v * v;
        }
        HF_NEAR(sum / n, 0.0, 0.02);
        HF_NEAR(sqrt(sumsq / n), 1.0, 0.02);
    }

    hf_begin("chan: SNR is set and read back consistently");
    {
        chan_cfg_t c;
        double db;
        chan_default(&c);
        for (db = -6.0; db <= 30.0; db += 3.0) {
            chan_set_snr_db(&c, db);
            HF_NEAR(chan_snr_db(&c), db, 0.001);
        }
    }

    hf_begin("chan: a rendered mark scores near the configured amplitude");
    {
        chan_cfg_t c;
        uint8_t chips[NCHIPS];
        demod_t d;
        uint16_t out[NCHIPS + 8];
        size_t ns, nc, i;
        uint32_t hi = 0, lo = 0xFFFFu;

        chan_default(&c);
        memset(chips, 1, sizeof chips);
        ns = chan_render(&c, chips, NCHIPS, g_samples, sizeof g_samples / sizeof g_samples[0]);
        HF_EQ_INT(ns, chan_samples_for(&c, NCHIPS));

        demod_init(&d);
        nc = demod_run(&d, g_samples, ns, out, sizeof out / sizeof out[0]);
        HF_CHECK(nc > 10);
        /* The last chips are the deliberate burst tail, which is silence. */
        for (i = 5; i + CHAN_TAIL_CHIPS + 1 < nc; i++) {
            if (out[i] > hi) hi = out[i];
            if (out[i] < lo) lo = out[i];
        }
        /* Half amplitude, because chan renders a sine and the Goertzel score
         * is normalised to that sine's amplitude. */
        HF_NEAR((double)hi, 200.0, 20.0);
        HF_NEAR((double)lo, 200.0, 20.0);
    }

    hf_begin("chan: a gated-off chip scores near nothing");
    {
        chan_cfg_t c;
        uint8_t chips[NCHIPS];
        demod_t d;
        uint16_t out[NCHIPS + 8];
        size_t ns, nc, i;
        uint32_t worst = 0;

        chan_default(&c);
        memset(chips, 0, sizeof chips);
        ns = chan_render(&c, chips, NCHIPS, g_samples, sizeof g_samples / sizeof g_samples[0]);
        demod_init(&d);
        nc = demod_run(&d, g_samples, ns, out, sizeof out / sizeof out[0]);
        for (i = 5; i < nc; i++) if (out[i] > worst) worst = out[i];
        HF_CHECK_MSG(worst < 4, "an off chip scored %u", (unsigned)worst);
    }

    hf_begin("chan: clipping is modelled, not ignored");
    {
        chan_cfg_t c;
        uint8_t chips[8];
        size_t ns, i;
        int clipped_hi = 0, clipped_lo = 0;

        chan_default(&c);
        c.amplitude = 4000.0;   /* well past the 12-bit range around VREF */
        memset(chips, 1, sizeof chips);
        ns = chan_render(&c, chips, sizeof chips, g_samples,
                         sizeof g_samples / sizeof g_samples[0]);
        for (i = 0; i < ns; i++) {
            if (g_samples[i] >= 2047) clipped_hi++;
            if (g_samples[i] <= -2048) clipped_lo++;
        }
        HF_CHECK(clipped_hi > 0);
        HF_CHECK(clipped_lo > 0);
    }

    hf_begin("chan: mains hum lands off the carrier bin and is rejected");
    {
        chan_cfg_t c;
        uint8_t chips[NCHIPS];
        demod_t d;
        uint16_t out[NCHIPS + 8];
        size_t ns, nc, i;
        uint32_t worst = 0;

        chan_default(&c);
        c.hum_lsb = 500.0;      /* enormous — far worse than design §10.4 fears */
        memset(chips, 0, sizeof chips);
        ns = chan_render(&c, chips, NCHIPS, g_samples, sizeof g_samples / sizeof g_samples[0]);
        demod_init(&d);
        nc = demod_run(&d, g_samples, ns, out, sizeof out / sizeof out[0]);
        for (i = 5; i < nc; i++) if (out[i] > worst) worst = out[i];
        HF_CHECK_MSG(worst < 20, "500 LSB of hum leaked %u into the carrier bin",
                     (unsigned)worst);
    }

    hf_begin("chan: a dropout silences exactly the chips it should");
    {
        chan_cfg_t c;
        uint8_t chips[NCHIPS];
        size_t ns, i;
        long energy = 0;

        chan_default(&c);
        c.dropout_prob = 1.0;      /* the first chip starts one */
        c.dropout_chips = NCHIPS;
        memset(chips, 1, sizeof chips);
        ns = chan_render(&c, chips, NCHIPS, g_samples, sizeof g_samples / sizeof g_samples[0]);
        for (i = 0; i < ns; i++) energy += g_samples[i] < 0 ? -g_samples[i] : g_samples[i];
        HF_CHECK_MSG(energy == 0, "a full dropout still carried %ld LSB", energy);
    }

    hf_begin("chan: the same seed renders the same samples");
    {
        chan_cfg_t c;
        uint8_t chips[NCHIPS];
        static int16_t again[CHAN_MAX_SAMPLES(NCHIPS)];
        size_t ns;

        chan_default(&c);
        chan_set_snr_db(&c, 6.0);
        memset(chips, 1, sizeof chips);

        ns = chan_render(&c, chips, NCHIPS, g_samples, sizeof g_samples / sizeof g_samples[0]);
        chan_render(&c, chips, NCHIPS, again, sizeof again / sizeof again[0]);
        HF_EQ_MEM(again, g_samples, ns * sizeof g_samples[0]);
    }

    hf_begin("chan: render refuses a buffer that is too small");
    {
        chan_cfg_t c;
        uint8_t chips[NCHIPS];
        chan_default(&c);
        HF_EQ_INT(chan_render(&c, chips, NCHIPS, g_samples, 10), 0);
    }
}
