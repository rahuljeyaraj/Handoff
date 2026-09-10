/*
 * Handoff — Goertzel filter. samples -> scores. Design §10.3.
 *
 * One magnitude per window of HANDOFF_GZ_N samples: the "score". The carrier
 * sits exactly on a bin centre, so a window holds a whole number of carrier
 * cycles and there is no spectral leakage — and, for the same reason, DC is
 * rejected exactly at any bin k != 0. Input need not be DC-centred.
 *
 * Fixed point throughout. Core 1 runs this at HANDOFF_WINDOW_RATE_HZ.
 */
#ifndef HANDOFF_GOERTZEL_H
#define HANDOFF_GOERTZEL_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#define GZ_COEFF_FRAC_BITS 14

typedef struct {
    int32_t  coeff;   /* 2*cos(2*pi*k/n), Q14 */
    uint16_t n;
    uint16_t k;
    uint16_t idx;     /* samples into the current window */
    int32_t  s1, s2;
} gz_t;

/* k is the bin index, n the window length. */
void     gz_init(gz_t *g, uint16_t n, uint16_t k);
void     gz_reset(gz_t *g);

/* Feed one sample. Returns true exactly once per window, with the magnitude
 * of bin k written to *score. Scores are magnitudes, not powers, so they are
 * linear in signal amplitude and a 6 dB change is a factor of two. */
bool     gz_push(gz_t *g, int16_t sample, uint32_t *score);

/* Magnitude of the window in progress, without disturbing it. Diagnostics. */
uint32_t gz_peek(const gz_t *g);

/* Exposed for tests and for anyone wanting mag^2 without the square root. */
uint64_t gz_mag2(const gz_t *g);
uint32_t gz_isqrt64(uint64_t v);

#endif /* HANDOFF_GOERTZEL_H */
