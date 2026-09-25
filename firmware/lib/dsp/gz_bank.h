/*
 * Handoff — the link v2 Goertzel bank. Five bins, one pass over the samples.
 * link-v2-design.md §4 and §6; brief §4 (step 3).
 *
 * v1 scores one bin and asks "is this louder than I remember?". v2 scores
 * five and asks "is this louder than the room is, right now" — the two tones
 * against the median of three guard bins measured in the SAME window, through
 * the same amplifier, the same body and the same gain. That is the whole
 * redesign: no floor, no time constant, nothing remembered.
 *
 *      bin 7  140 kHz   guard      bin 9   180 kHz  tone A
 *      bin 8  160 kHz   guard      bin 10  200 kHz  tone B
 *      bin 11 220 kHz   guard
 *
 * Two things make it affordable on core 1:
 *
 *   - **No square roots.** Every v2 decision is a ratio, and a ratio can be
 *     taken on mag^2 by cross-multiplication. gz_isqrt64 is telemetry only.
 *   - **The guards are decimated** by HANDOFF_GUARD_DECIM. They are noise
 *     estimates and nothing we transmit can reach them, so they cost a
 *     quarter and lose nothing. config.h carries the derivation.
 *
 * Nothing in here is a threshold. The bank reports magnitudes and a median;
 * what counts as "busy" is step 5's job, and the number it uses comes from a
 * stated false-alarm rate rather than a bench.
 */
#ifndef HANDOFF_GZ_BANK_H
#define HANDOFF_GZ_BANK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "goertzel.h"

/* Bank order. Tones first so a chip decision touches two adjacent entries. */
#define GZB_A       0
#define GZB_B       1
#define GZB_G_LO    2
#define GZB_G_MID   3
#define GZB_G_HI    4
#define GZB_BINS    5
#define GZB_GUARDS  3

typedef struct {
    gz_t     bin[GZB_BINS];
    uint64_t mag2[GZB_BINS];    /* last window scored, per bin */
    uint32_t windows;           /* windows completed since reset */
    uint32_t guard_windows;     /* of those, windows the guards ran in */
    bool     guards_fresh;      /* the guards were scored in THIS window */
    bool     guards_on;         /* the window in progress is a guard window */
} gz_bank_t;

/* Bins come from config.h — the tone pair and the guard set are structural. */
void     gzb_init(gz_bank_t *b);
void     gzb_reset(gz_bank_t *b);

/*
 * Feed a run of samples, stopping at the end of a window. Returns how many
 * were taken, and sets *complete when a window closed — which is when mag2[]
 * has been updated: both tones every time, the guards every
 * HANDOFF_GUARD_DECIM-th window. Between guard windows the guard entries hold
 * their last value, which is what a time-averaged noise estimate should do.
 *
 * A run, not a sample, because that is what makes the bank affordable: the
 * five filters stay in registers across it instead of being loaded and stored
 * five times for every sample. The caller loops until the block is spent:
 *
 *     while (off < n) {
 *         bool done;
 *         off += gzb_push_run(b, blk + off, n - off, &done);
 *         if (done) ... mag2[] is fresh ...
 *     }
 */
size_t   gzb_push_run(gz_bank_t *b, const int16_t *s, size_t n, bool *complete);

/* One sample, for the host tests and anything not holding a block. True once
 * per window, exactly as gzb_push_run()'s *complete. */
bool     gzb_push(gz_bank_t *b, int16_t sample);

/* max of the two tones — the signal, whichever tone is being sent. */
uint64_t gzb_signal(const gz_bank_t *b);

/*
 * Median of the three guards.
 *
 * Median, not mean, and that is the point: one interferer landing in one
 * guard bin cannot move the median of three. Standard CFAR. A mean would
 * hand a single spur the power to deafen the receiver, which is close to
 * what v1's floor did.
 */
uint64_t gzb_noise(const gz_bank_t *b);

/* Which tone this window holds, with no threshold anywhere: a chip is
 * E_A > E_B or it is not. Ties go to A, arbitrarily and harmlessly — a tie
 * carries no information either way. */
bool     gzb_tone_is_b(const gz_bank_t *b);

/*
 * a > b * num / den, without a division and without a square root, for the
 * magnitudes this bank produces. Overflow-safe by construction for mag^2 of
 * a 12-bit converter: mag^2 is under 2^31 for HANDOFF_GZ_N = 25, so the
 * products below stay inside 64 bits for any sane ratio, and the guard here
 * is belt and braces rather than a live case.
 */
bool     gzb_ratio_gt(uint64_t a, uint64_t b, uint32_t num, uint32_t den);

/* mag^2 -> the amplitude-like score the consoles and the host tests print.
 * Takes a square root: telemetry, never the hot path. */
uint32_t gzb_score(uint64_t mag2);

#endif /* HANDOFF_GZ_BANK_H */
