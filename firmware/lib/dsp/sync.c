#include "sync.h"

#define EDGE_DECAY_SHIFT 3   /* leak 1/8 per chip */

void sync_init(sync_t *s, uint8_t windows_per_chip, uint8_t guard)
{
    if (windows_per_chip < 2) windows_per_chip = 2;
    if (windows_per_chip > SYNC_MAX_WPC) windows_per_chip = SYNC_MAX_WPC;
    while (windows_per_chip - 2 * guard < 1 && guard > 0) guard--;

    s->wpc = windows_per_chip;
    s->guard = guard;
    sync_reset(s);
}

void sync_reset(sync_t *s)
{
    int i;
    for (i = 0; i < SYNC_MAX_WPC; i++) { s->hist[i] = 0; s->edge[i] = 0; }
    s->pos = 0;
    s->len = s->wpc;
    s->prev = 0;
    s->have_prev = false;
    s->last_correction = 0;
    s->chips_out = 0;
}

/*
 * Signed distance from the sampling phase to the strongest transition, in
 * windows, folded into [-wpc/2, +wpc/2). Position 0 is the chip boundary, so
 * a locked tracker reports 0.
 */
int sync_phase_error(const sync_t *s)
{
    int best = 0, i, off;
    for (i = 1; i < s->wpc; i++)
        if (s->edge[i] > s->edge[best]) best = i;

    off = best;
    if (off > s->wpc / 2) off -= s->wpc;
    return off;
}

HANDOFF_HOT_FUNC bool sync_push_d(sync_t *s, int32_t d, int32_t *chip)
{
    uint64_t delta;
    int i, off, lo, hi;
    int64_t sum;

    /* Transition energy attributed to the phase slot it was observed in.
     * Under FSK the level does not move at a chip boundary and the SIGN
     * does, so this is where the edge now lives. */
    delta = s->have_prev
              ? (uint64_t)(d > s->prev ? (int64_t)d - s->prev : (int64_t)s->prev - d)
              : 0u;
    s->prev = d;
    s->have_prev = true;

    if (s->pos < SYNC_MAX_WPC) s->hist[s->pos] = d;
    if (s->pos < SYNC_MAX_WPC) {
        s->edge[s->pos] -= s->edge[s->pos] >> EDGE_DECAY_SHIFT;
        s->edge[s->pos] += delta;
    }

    if (++s->pos < s->len) return false;

    /*
     * Integrate the chip, dropping `guard` windows at each end. A boundary
     * window is a mixture of two chips whenever timing is imperfect, so it
     * carries noise and no information.
     */
    lo = s->guard;
    hi = (int)s->len - s->guard;
    if (hi <= lo) { lo = 0; hi = s->len; }

    sum = 0;
    for (i = lo; i < hi && i < SYNC_MAX_WPC; i++) sum += s->hist[i];
    sum /= (int64_t)(hi - lo);

    if (chip) *chip = (int32_t)sum;

    /*
     * Early/late correction. Nudge by at most one window per chip: the tracker
     * has to follow tens of ppm of crystal offset, not chase noise.
     */
    off = sync_phase_error(s);
    s->last_correction = (off > 0) ? 1 : (off < 0 ? -1 : 0);
    s->len = (uint8_t)((int)s->wpc + s->last_correction);

    if (s->last_correction != 0) {
        /* Rotate the edge histogram with the phase, so the correction is not
         * immediately re-applied against a stale index. */
        uint64_t rot[SYNC_MAX_WPC];
        for (i = 0; i < s->wpc; i++)
            rot[i] = s->edge[(i + s->last_correction + s->wpc) % s->wpc];
        for (i = 0; i < s->wpc; i++) s->edge[i] = rot[i];
    }

    s->pos = 0;
    s->chips_out++;
    return true;
}

/*
 * The single-bin form. E_A is held at zero, so d is the score itself and the
 * edge detector sees exactly what it saw before FSK: |score - prev|. Nothing
 * about the v1 chain's timing changes by going through sync_push_d().
 */
HANDOFF_HOT_FUNC bool sync_push(sync_t *s, uint32_t score, uint16_t *chip)
{
    int32_t v = 0;
    const int32_t in = (score > 0x7FFFFFFFu) ? 0x7FFFFFFF : (int32_t)score;

    if (!sync_push_d(s, in, &v)) return false;
    if (chip) {
        if (v < 0) v = 0;
        *chip = (v > 0xFFFF) ? 0xFFFFu : (uint16_t)v;
    }
    return true;
}
