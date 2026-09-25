#include "presence.h"

#include <string.h>

void presence_init(presence_t *p)
{
    memset(p, 0, sizeof *p);
}

bool presence_push(presence_t *p, uint64_t signal_mag2, uint64_t guard_mag2,
                   bool guard_fresh)
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
     * which is gzb_ratio_gt()'s cross-multiplication with no division and no
     * square root. It also halves both sides if a product could ever wrap,
     * which for a 12-bit converter at HANDOFF_GZ_N it cannot — mag^2 stays
     * under 2^31, so the left side peaks near 2^43 and the right near 2^47.
     */
    p->busy = presence_ready(p) &&
              gzb_ratio_gt(p->signal, p->sum, HANDOFF_CFAR_K_NUM,
                           (uint32_t)HANDOFF_CFAR_K_DEN * HANDOFF_CFAR_CELLS);

    if (p->busy) p->busy_windows++;
    return p->busy;
}

bool presence_push_bank(presence_t *p, const gz_bank_t *b)
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
