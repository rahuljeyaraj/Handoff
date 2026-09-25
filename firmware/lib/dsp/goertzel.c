#include "goertzel.h"

#include <math.h>

#define GZ_ONE (1 << GZ_COEFF_FRAC_BITS)

void gz_init(gz_t *g, uint16_t n, uint16_t k)
{
    const double w = 2.0 * 3.14159265358979323846 * (double)k / (double)n;
    double c = 2.0 * cos(w) * (double)GZ_ONE;

    g->n = n;
    g->k = k;
    g->coeff = (int32_t)(c < 0 ? c - 0.5 : c + 0.5);
    gz_reset(g);
}

void gz_reset(gz_t *g)
{
    g->s1 = 0;
    g->s2 = 0;
    g->idx = 0;
}

/*
 * mag^2 = s1^2 + s2^2 - coeff*s1*s2
 *
 * s1 and s2 reach roughly n/2 * amplitude for an on-bin tone: 25 * 2048 at
 * full-scale 12-bit input, so ~5e4. The cross term is then coeff*s1*s2 ~
 * 1.6e4 * 2.5e9, which overflows 32 bits by four orders of magnitude. int64
 * is not optional here.
 */
uint64_t gz_mag2(const gz_t *g)
{
    const int64_t s1 = g->s1, s2 = g->s2;
    int64_t m = s1 * s1 + s2 * s2 - ((g->coeff * s1 * s2) >> GZ_COEFF_FRAC_BITS);
    return m < 0 ? 0 : (uint64_t)m;
}

/* Integer square root, Newton with a bit-shift seed. No FPU on the hot path. */
uint32_t gz_isqrt64(uint64_t v)
{
    uint64_t x, prev;
    int shift = 0;

    if (v == 0) return 0;
    /* Seed at 2^ceil(bits/2), which is within a factor of sqrt(2). */
    { uint64_t t = v; while (t) { t >>= 2; shift++; } }
    x = (uint64_t)1 << shift;

    do {
        prev = x;
        x = (x + v / x) >> 1;
    } while (x < prev);

    return (uint32_t)prev;
}

bool gz_push(gz_t *g, int16_t sample, uint32_t *score)
{
    const int32_t s0 = (int32_t)sample
                     + (int32_t)(((int64_t)g->coeff * g->s1) >> GZ_COEFF_FRAC_BITS)
                     - g->s2;
    g->s2 = g->s1;
    g->s1 = s0;

    if (++g->idx < g->n) return false;

    if (score) {
        /* Normalise by n so the score is comparable across window lengths;
         * without it a sweep of HANDOFF_GZ_N compares apples to oranges. */
        *score = (uint32_t)((uint64_t)gz_isqrt64(gz_mag2(g)) * 2u / g->n);
    }
    gz_reset(g);
    return true;
}

/*
 * Same recurrence, and deliberately the same three lines: the only difference
 * from gz_push() is what comes out at the window boundary. One square root is
 * cheap; five of them, 20000 times a second, is the work link v2 §5 takes off
 * core 1.
 */
bool gz_push_mag2(gz_t *g, int16_t sample, uint64_t *mag2)
{
    const int32_t s0 = (int32_t)sample
                     + (int32_t)(((int64_t)g->coeff * g->s1) >> GZ_COEFF_FRAC_BITS)
                     - g->s2;
    g->s2 = g->s1;
    g->s1 = s0;

    if (++g->idx < g->n) return false;

    if (mag2) *mag2 = gz_mag2(g);
    gz_reset(g);
    return true;
}

uint32_t gz_score_of(uint64_t mag2, uint16_t n)
{
    if (n == 0) return 0;
    return (uint32_t)((uint64_t)gz_isqrt64(mag2) * 2u / n);
}

uint32_t gz_peek(const gz_t *g)
{
    return (uint32_t)((uint64_t)gz_isqrt64(gz_mag2(g)) * 2u / g->n);
}
