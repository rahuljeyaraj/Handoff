#include "gz_bank.h"

static const uint16_t k_bank_bins[GZB_BINS] = {
    HANDOFF_TONE_A_BIN,
    HANDOFF_TONE_B_BIN,
    HANDOFF_GUARD_LO_BIN,
    HANDOFF_GUARD_MID_BIN,
    HANDOFF_GUARD_HI_BIN,
};

void gzb_init(gz_bank_t *b)
{
    int i;
    for (i = 0; i < GZB_BINS; i++) gz_init(&b->bin[i], HANDOFF_GZ_N, k_bank_bins[i]);
    gzb_reset(b);
}

void gzb_reset(gz_bank_t *b)
{
    int i;
    for (i = 0; i < GZB_BINS; i++) {
        gz_reset(&b->bin[i]);
        b->mag2[i] = 0;
    }
    b->windows = 0;
    b->guard_windows = 0;
    b->guards_fresh = false;
    b->guards_on = true;     /* window 0 is a guard window */
}

/*
 * One bin, over a run of samples that does not cross a window boundary.
 *
 * This is the hot loop of the whole receiver, and its shape is the reason the
 * bank fits. Per sample, gz_push_mag2() must reach into the filter for coeff,
 * s1, s2, idx and n and put three of them back — five times a sample, through
 * five function calls, with the code itself coming out of flash. Hoisting the
 * state into locals and running the recurrence over a run leaves the filter in
 * registers and touches memory once at each end.
 *
 * The arithmetic is gz_push_mag2()'s, to the bit. test_gz_bank.c checks the
 * bank against the same Goertzels run one sample at a time and requires them
 * equal, not close.
 */
static void bin_run(gz_t *g, const int16_t *s, size_t n)
{
    const int32_t coeff = g->coeff;
    int32_t s1 = g->s1, s2 = g->s2;
    size_t i;

    for (i = 0; i < n; i++) {
        const int32_t s0 = (int32_t)s[i]
                         + (int32_t)(((int64_t)coeff * s1) >> GZ_COEFF_FRAC_BITS)
                         - s2;
        s2 = s1;
        s1 = s0;
    }

    g->s1 = s1;
    g->s2 = s2;
    g->idx = (uint16_t)(g->idx + n);
}

static void bank_window_end(gz_bank_t *b)
{
    int i;

    if (b->guards_on)
        for (i = GZB_G_LO; i < GZB_BINS; i++) {
            b->mag2[i] = gz_mag2(&b->bin[i]);
            gz_reset(&b->bin[i]);
        }

    for (i = GZB_A; i < GZB_G_LO; i++) {
        b->mag2[i] = gz_mag2(&b->bin[i]);
        gz_reset(&b->bin[i]);
    }

    b->guards_fresh = b->guards_on;
    if (b->guards_on) b->guard_windows++;
    b->windows++;
    /* One flag, decided at the window boundary, rather than a modulo every
     * sample: this runs 500 000 times a second. */
    b->guards_on = (b->windows % HANDOFF_GUARD_DECIM) == 0u;
}

/*
 * The hot path. Two tone filters over every sample; the three guards only in
 * a window the decimation has chosen.
 *
 * A guard is fed for a whole window or not at all — never part of one —
 * because a Goertzel's output means nothing over a fragment. A skipped window
 * leaves that filter reset from the last boundary, so the next window it runs
 * in starts clean.
 */
size_t gzb_push_run(gz_bank_t *b, const int16_t *s, size_t n, bool *complete)
{
    const size_t left = (size_t)(HANDOFF_GZ_N - b->bin[GZB_A].idx);
    const size_t take = n < left ? n : left;
    int i;

    if (complete) *complete = false;
    if (take == 0) return 0;

    if (b->guards_on)
        for (i = GZB_G_LO; i < GZB_BINS; i++) bin_run(&b->bin[i], s, take);
    bin_run(&b->bin[GZB_A], s, take);
    bin_run(&b->bin[GZB_B], s, take);

    if (take == left) {
        bank_window_end(b);
        if (complete) *complete = true;
    }
    return take;
}

bool gzb_push(gz_bank_t *b, int16_t sample)
{
    bool done = false;
    (void)gzb_push_run(b, &sample, 1u, &done);
    return done;
}

uint64_t gzb_signal(const gz_bank_t *b)
{
    return b->mag2[GZB_A] > b->mag2[GZB_B] ? b->mag2[GZB_A] : b->mag2[GZB_B];
}

uint64_t gzb_noise(const gz_bank_t *b)
{
    const uint64_t x = b->mag2[GZB_G_LO];
    const uint64_t y = b->mag2[GZB_G_MID];
    const uint64_t z = b->mag2[GZB_G_HI];

    /* Median of three, branch-only: the one that is neither the largest nor
     * the smallest. Three comparisons, no sort, no array. */
    if (x > y) {
        if (y > z) return y;
        return x > z ? z : x;
    }
    if (x > z) return x;
    return y > z ? z : y;
}

bool gzb_tone_is_b(const gz_bank_t *b)
{
    return b->mag2[GZB_B] > b->mag2[GZB_A];
}

bool gzb_ratio_gt(uint64_t a, uint64_t b, uint32_t num, uint32_t den)
{
    const uint64_t kMax = (uint64_t)1 << 62;

    if (den == 0u) return false;

    /* Halve both sides until the products cannot wrap. Losing the bottom bit
     * of a magnitude that large changes nothing a decision can see. */
    while (a > kMax / den || (num != 0u && b > kMax / num)) {
        a >>= 1;
        b >>= 1;
        if (a == 0u && b == 0u) return false;
    }
    return a * (uint64_t)den > b * (uint64_t)num;
}

uint32_t gzb_score(uint64_t mag2)
{
    return gz_score_of(mag2, (uint16_t)HANDOFF_GZ_N);
}
