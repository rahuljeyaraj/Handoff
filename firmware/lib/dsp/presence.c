#include "presence.h"

#include <string.h>

void presence_init(presence_t *p)
{
    memset(p, 0, sizeof *p);
}

HANDOFF_HOT_FUNC bool presence_push(presence_t *p, uint64_t signal_mag2,
                                    uint64_t guard_mag2, bool guard_fresh)
{
    p->signal = signal_mag2;
    p->windows++;

    if (guard_fresh) {
        p->noise = guard_mag2;
        /* Carry the sum rather than re-adding forty cells every guard window.
         * Exact either way: the cell leaving is the one that went in. */
        p->sum -= p->ref[p->idx];
        p->ref[p->idx] = guard_mag2;
        p->sum += guard_mag2;
        p->idx = (uint16_t)((p->idx + 1u) % HANDOFF_CFAR_CELLS);
        if (p->filled < HANDOFF_CFAR_CELLS) p->filled++;
        p->cells++;
    }

    /*
     * signal > k * mean(reference), with k = K_NUM/K_DEN and the mean being
     * sum/N. Multiplied out:
     *
     *      signal * (K_DEN * N) > sum * K_NUM
     *
     * No division and no square root, which is the §6 requirement, and the
     * multiplication is written out here rather than handed to
     * gzb_ratio_gt(). THAT IS NOT A STYLE CHOICE. gzb_ratio_gt() guards
     * against a wrapping product by dividing — twice, in 64 bits, on a part
     * with no 64-bit divider — and this line runs twenty thousand times a
     * second. Measured on the bench: the first version of this file, with
     * that guard and with two square roots taken for telemetry, cost core 1
     * EIGHTEEN POINTS of load.
     *
     * The guard is not needed, and config.h static-asserts the reason. mag^2
     * out of a 12-bit converter at HANDOFF_GZ_N stays under 2^31, so the left
     * side peaks near 2^43; the reference is HANDOFF_CFAR_CELLS of those, so
     * the right side peaks near 2^47. Both sit inside a 64-bit product with
     * sixteen bits to spare.
     */
    p->busy = presence_ready(p) &&
              p->signal * ((uint64_t)HANDOFF_CFAR_K_DEN * HANDOFF_CFAR_CELLS)
                  > p->sum * (uint64_t)HANDOFF_CFAR_K_NUM;

    if (p->busy) p->busy_windows++;

    /*
     * The peak, for a reader that samples far slower than this runs. One
     * 64-bit compare and, rarely, three stores; no division and no root, so
     * the §6 rule this whole file is built around still holds. The reference
     * is captured HERE rather than divided here, for the same reason.
     */
    if (signal_mag2 > p->peak_signal) {
        p->peak_signal = signal_mag2;
        p->peak_sum    = p->sum;
        p->peak_filled = p->filled;
    }

    return p->busy;
}

HANDOFF_HOT_FUNC bool presence_push_bank(presence_t *p, const gz_bank_t *b)
{
    return presence_push(p, gzb_signal(b), gzb_noise(b), b->guards_fresh);
}

uint32_t presence_signal_score(const presence_t *p)
{
    return gzb_score(p->signal);
}

uint32_t presence_noise_score(const presence_t *p)
{
    if (p->filled == 0u) return 0u;
    return gzb_score(p->sum / p->filled);
}

void presence_take_peak(presence_t *p, uint32_t *signal, uint32_t *noise)
{
    if (signal) *signal = gzb_score(p->peak_signal);
    if (noise)  *noise  = p->peak_filled ? gzb_score(p->peak_sum / p->peak_filled)
                                         : 0u;
    p->peak_signal = 0u;
    p->peak_sum    = 0u;
    p->peak_filled = 0u;
}
