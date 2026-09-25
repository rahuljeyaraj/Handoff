#include "chan.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846

/* ---- deterministic randomness ----------------------------------------- */

void rng_seed(rng_t *r, uint64_t seed)
{
    r->s = seed ? seed : 0x9E3779B97F4A7C15ull;
    r->spare = 0.0;
    r->has_spare = 0;
}

uint32_t rng_u32(rng_t *r)
{
    /* xorshift64*, plenty for a channel model and reproducible everywhere. */
    uint64_t x = r->s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    r->s = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
}

double rng_uniform(rng_t *r)
{
    return (double)rng_u32(r) / 4294967296.0;
}

double rng_normal(rng_t *r)
{
    double u, v, s;

    if (r->has_spare) { r->has_spare = 0; return r->spare; }

    do {
        u = rng_uniform(r) * 2.0 - 1.0;
        v = rng_uniform(r) * 2.0 - 1.0;
        s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);

    s = sqrt(-2.0 * log(s) / s);
    r->spare = v * s;
    r->has_spare = 1;
    return u * s;
}

/* ---- the channel ------------------------------------------------------ */

void chan_default(chan_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->amplitude      = 200.0;   /* design §5: ~160 mV at the ADC, ~200 LSB */
    c->noise_rms      = 0.0;
    c->dc             = 2048.0;  /* VREF, design §6.2                        */
    c->dc_drift_lsb_s = 0.0;
    c->carrier_ppm    = 0.0;
    c->clock_ppm      = 0.0;
    c->ramp_db        = 0.0;
    c->hum_hz         = 50.0;
    c->hum_lsb        = 0.0;
    c->dropout_prob   = 0.0;
    c->dropout_chips  = 0;
    c->tail_chips     = CHAN_TAIL_CHIPS;
    c->lead_samples   = 0;
    c->seed           = 1;
}

void chan_set_snr_db(chan_cfg_t *c, double snr_db)
{
    const double carrier_rms = c->amplitude / sqrt(2.0);
    c->noise_rms = carrier_rms / pow(10.0, snr_db / 20.0);
}

double chan_snr_db(const chan_cfg_t *c)
{
    const double carrier_rms = c->amplitude / sqrt(2.0);
    if (c->noise_rms <= 0.0) return 999.0;
    return 20.0 * log10(carrier_rms / c->noise_rms);
}

static size_t lead_for(const chan_cfg_t *c)
{
    rng_t rng;

    if (c->lead_samples >= 0) return (size_t)c->lead_samples;

    /* Drawn from the seed, but from a different stream than the noise so a
     * point with and without the lead sees the same noise samples. */
    rng_seed(&rng, c->seed ^ 0x5EED1EADull);
    return (size_t)(rng_u32(&rng) % (uint32_t)CHAN_SAMPLES_PER_CHIP);
}

size_t chan_samples_for(const chan_cfg_t *c, size_t nchips)
{
    const double spc = (double)CHAN_SAMPLES_PER_CHIP * (1.0 + c->clock_ppm * 1e-6);
    return (size_t)((double)(nchips + (size_t)c->tail_chips) * spc + 0.5) + lead_for(c);
}

size_t chan_render(const chan_cfg_t *c, const uint8_t *chips, size_t nchips,
                   int16_t *out, size_t max)
{
    const size_t n = chan_samples_for(c, nchips);
    const size_t lead = lead_for(c);
    const double fs = (double)HANDOFF_ADC_FS_HZ;
    /*
     * Link v2: two tones, one per chip value, and the pad is driven for EVERY
     * chip inside the burst. There is no space to render — that is what
     * "constant envelope" means, and it is why the v1 amplitude-threshold
     * machinery has nothing left to do (design §4).
     */
    const double fa = (double)HANDOFF_TONE_A_HZ * (1.0 + c->carrier_ppm * 1e-6);
    const double fb = (double)HANDOFF_TONE_B_HZ * (1.0 + c->carrier_ppm * 1e-6);
    const double spc = (double)CHAN_SAMPLES_PER_CHIP * (1.0 + c->clock_ppm * 1e-6);
    rng_t rng;
    size_t i;
    int dropout_left = 0;
    size_t last_chip = (size_t)-1;
    double phase = 0.0;

    if (n > max) return 0;
    rng_seed(&rng, c->seed);

    for (i = 0; i < n; i++) {
        const double t = (double)i / fs;
        /* Samples inside the lead belong to no chip: silence. */
        const size_t ci = i < lead ? nchips : (size_t)((double)(i - lead) / spc);
        double a, v;

        /*
         * Dropouts are drawn per chip, not per sample, so "the contact broke
         * for 3 chips" is expressible and repeatable.
         */
        if (ci != last_chip) {
            last_chip = ci;
            if (dropout_left > 0) dropout_left--;
            else if (c->dropout_prob > 0.0 && rng_uniform(&rng) < c->dropout_prob)
                dropout_left = c->dropout_chips;
        }

        /* Amplitude ramp across the burst: grip tightening mid-handshake. */
        a = c->amplitude;
        if (c->ramp_db != 0.0 && nchips > 1) {
            const double frac = (double)ci / (double)(nchips - 1);
            a *= pow(10.0, (c->ramp_db * frac) / 20.0);
        }
        if (dropout_left > 0) a = 0.0;

        v = c->dc + c->dc_drift_lsb_s * t;
        if (c->hum_lsb > 0.0) v += c->hum_lsb * sin(2.0 * PI * c->hum_hz * t);

        /*
         * Phase is integrated rather than recomputed from t, so a carrier
         * offset is a real frequency error rather than a per-sample
         * discontinuity. It is used before it is advanced, so sample 0 has
         * phase 0 — the same convention tools/gen_vectors.py uses, and the
         * two are compared against each other in test_vectors.
         *
         * It also CARRIES ACROSS A TONE CHANGE rather than restarting, which
         * is what the generator does. A chip is a whole number of periods of
         * either tone (config.h asserts it), so at an exact chip boundary an
         * integrated phase is back at zero — which is where gen_vectors.py's
         * sin(2*pi*f*t) is too, and why the two still agree to the LSB with
         * the tone switching every chip.
         */
        if (ci < nchips) {
            v += a * sin(phase);
            phase += 2.0 * PI * (chips[ci] ? fb : fa) / fs;
        } else {
            phase += 2.0 * PI * fa / fs;
        }
        if (phase > 2.0 * PI) phase -= 2.0 * PI;
        if (c->noise_rms > 0.0) v += c->noise_rms * rng_normal(&rng);

        /* The ADC is 12-bit and it does clip. Modelling that matters: design
         * §15.2's LC bandpass exists precisely because it might. */
        if (v < 0.0) v = 0.0;
        if (v > 4095.0) v = 4095.0;

        /* Centre it the way hal_pico will, so the DSP sees signed samples. */
        out[i] = (int16_t)((int)(v + 0.5) - 2048);
    }
    return n;
}

/* ---- the receiver's front half ---------------------------------------- */

/*
 * Link v2: two bins, scored in the SAME window, and what leaves is their
 * difference. The receiver on the board does this out of the five-bin bank
 * (dsp/gz_bank.c); here it is two plain Goertzels, because the three guard
 * bins are presence's business and presence is not in this path.
 *
 * gz_push returns an amplitude-like score and the bank works in mag^2. The
 * difference is a scale on d, and every decision downstream is a comparison
 * of two d's, so the scale cancels — what must match is the SIGN convention
 * and the fact that both bins come from one window, and both do.
 */
void demod_init(demod_t *d)
{
    gz_init(&d->a, HANDOFF_GZ_N, HANDOFF_TONE_A_BIN);
    gz_init(&d->b, HANDOFF_GZ_N, HANDOFF_TONE_B_BIN);
    sync_init(&d->sy, HANDOFF_WINDOWS_PER_CHIP, HANDOFF_CHIP_GUARD);
}

bool demod_push(demod_t *d, int16_t sample, frame_chip_t *chip)
{
    uint32_t ea = 0, eb = 0;
    const bool wa = gz_push(&d->a, sample, &ea);
    const bool wb = gz_push(&d->b, sample, &eb);

    if (!wa || !wb) return false;   /* same N, so they close together */
    return sync_push_d(&d->sy, (int32_t)eb - (int32_t)ea, chip);
}

size_t demod_run(demod_t *d, const int16_t *samples, size_t n,
                 frame_chip_t *chips, size_t max)
{
    size_t i, out = 0;

    for (i = 0; i < n; i++) {
        frame_chip_t chip;
        if (!demod_push(d, samples[i], &chip)) continue;
        if (out < max) chips[out++] = chip;
    }
    return out;
}
