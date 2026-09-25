/*
 * Handoff — channel simulator. Development plan M1.
 *
 * Chips in, int16 ADC samples out, with every impairment the bench is going to
 * hand us: attenuation, additive noise, carrier frequency offset, sample-clock
 * offset, DC drift, an amplitude ramp across the packet (the "grip changes
 * mid-handshake" case), mains hum, and burst dropouts.
 *
 * This is the sample-level path. DSP tests and the BER harness inject here.
 * Protocol tests inject one layer higher, at the signed tone differences of
 * link v2 step 6, through hal_host — see architecture §5.
 *
 * Everything is deterministic given a seed. A failing BER point must be
 * reproducible or it is not evidence.
 */
#ifndef HANDOFF_CHAN_H
#define HANDOFF_CHAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "frame.h"
#include "goertzel.h"
#include "sync.h"

#define CHAN_SAMPLES_PER_CHIP (HANDOFF_GZ_N * HANDOFF_WINDOWS_PER_CHIP)

/*
 * Buffer size for n chips. A transmitter whose chip clock is slow needs MORE
 * receiver samples to carry the same chips, so a buffer sized at the nominal
 * rate truncates the last chip — and the last chip of a frame is part of the
 * CRC. One extra sample per chip covers offsets up to 8000 ppm, far past
 * anything a crystal can do.
 */
#define CHAN_TAIL_CHIPS 4
#define CHAN_MAX_SAMPLES(nchips)     (((nchips) + CHAN_TAIL_CHIPS + 1) * CHAN_SAMPLES_PER_CHIP + (nchips) + 64)

/* ---- deterministic randomness ----------------------------------------- */

typedef struct { uint64_t s; double spare; int has_spare; } rng_t;

void     rng_seed(rng_t *r, uint64_t seed);
uint32_t rng_u32(rng_t *r);
double   rng_uniform(rng_t *r);   /* [0, 1) */
double   rng_normal(rng_t *r);    /* mean 0, sigma 1 */

/* ---- the channel ------------------------------------------------------ */

typedef struct {
    double amplitude;       /* peak LSB at the ADC while a chip is on        */
    double noise_rms;       /* additive white gaussian, LSB RMS              */
    double dc;              /* static offset, LSB (VREF sits at ~2048)       */
    double dc_drift_lsb_s;  /* bias wander                                   */
    double carrier_ppm;     /* TX crystal against RX crystal                 */
    double clock_ppm;       /* TX chip clock against RX sample clock         */
    double ramp_db;         /* amplitude change from first chip to last      */
    double hum_hz;          /* 50 or 60                                      */
    double hum_lsb;         /* mains hum amplitude, LSB                      */
    double dropout_prob;    /* probability a dropout starts, per chip        */
    int    dropout_chips;   /* how long one lasts                            */

    /*
     * Silent chips appended after the burst. Not cosmetic: the timing tracker
     * lengthens and shortens individual chips to follow the far end's clock,
     * so the last chip of a frame can still be in the integrator when the
     * chips run out — and the last chip of a frame is part of the CRC. On real
     * hardware the ADC free-runs and the turnaround silence flushes it. A
     * simulator that stops dead at the final chip does not, and the frame is
     * lost to an artefact of the model rather than of the link.
     */
    int    tail_chips;

    /*
     * Silent samples BEFORE the burst. Zero renders chip 0 at sample 0, which
     * puts every chip boundary exactly on a Goertzel window boundary -- the
     * kindest case, and the one the bench never sees: there the ADC free-runs
     * and a frame starts wherever it starts. Negative draws the lead from the
     * seed, anywhere inside one chip, so a sweep sees every window phase.
     */
    int    lead_samples;
    uint64_t seed;
} chan_cfg_t;

#define CHAN_LEAD_RANDOM (-1)

/* Link-budget nominal: design §5 puts ~160 mV at the ADC, which is ~200 LSB. */
void   chan_default(chan_cfg_t *c);

/* Set noise_rms so the on-chip carrier sits at the given SNR, defined as
 * 20*log10(carrier RMS / noise RMS). */
void   chan_set_snr_db(chan_cfg_t *c, double snr_db);
double chan_snr_db(const chan_cfg_t *c);

/* Samples this configuration will produce for n chips. */
size_t chan_samples_for(const chan_cfg_t *c, size_t nchips);

/* Render chips to samples. Returns samples written, 0 if they would not fit. */
size_t chan_render(const chan_cfg_t *c, const uint8_t *chips, size_t nchips,
                   int16_t *out, size_t max);

/* ---- the receiver's front half ---------------------------------------- */

/*
 * Two bins scored in the same window, and a signed difference out — the same
 * thing core 1 hands core 0 on hardware. Link v2 step 6.
 */
typedef struct {
    gz_t   a, b;
    sync_t sy;
} demod_t;

void   demod_init(demod_t *d);

/* One sample. True once per chip, with the tone difference in *chip. */
bool   demod_push(demod_t *d, int16_t sample, frame_chip_t *chip);

size_t demod_run(demod_t *d, const int16_t *samples, size_t n,
                 frame_chip_t *chips, size_t max);

#endif /* HANDOFF_CHAN_H */
