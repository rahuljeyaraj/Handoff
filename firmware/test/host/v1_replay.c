#include "v1_replay.h"

#include <string.h>

#include "config.h"

void v1_replay_init(v1_replay_t *v, uint16_t bin)
{
    memset(v, 0, sizeof *v);
    gz_init(&v->gz, HANDOFF_GZ_N, bin);
    sync_init(&v->sy, HANDOFF_WINDOWS_PER_CHIP, HANDOFF_CHIP_GUARD);
}

/*
 * v1's adaptive slicer, verbatim. hi tracks fast up and decays slow; lo
 * mirrors it; the midpoint slices.
 *
 * EVERY STEP IS AT LEAST ONE LSB, and that line is the whole reason one of
 * the captures exists. Written as a plain shift, (hi - lo) >> 6 is zero once
 * the gap is under 64, so after one loud transient hi froze at lo + 63 and a
 * 3 LSB frame could never slice high again. The receiver was deaf until
 * re-initialised, and a wristband receiver is never re-initialised between a
 * firm grip and a light one.
 */
static int32_t step_toward(int32_t gap, int shift)
{
    const int32_t s = gap >> shift;
    return s > 0 ? s : (gap > 0 ? 1 : 0);
}

bool v1_replay_push(v1_replay_t *v, int16_t sample, frame_chip_t *chip)
{
    uint32_t score;
    uint16_t e;
    int32_t  x;

    if (!gz_push(&v->gz, sample, &score)) return false;
    if (!sync_push(&v->sy, score, &e)) return false;

    x = (int32_t)e;
    if (!v->primed) { v->hi = x; v->lo = x; v->primed = true; }

    if (x > v->hi) v->hi += step_toward(x - v->hi, 1);
    else           v->hi -= step_toward(v->hi - v->lo, 6);
    if (x < v->lo) v->lo -= step_toward(v->lo - x, 1);
    else           v->lo += step_toward(v->hi - v->lo, 6);
    if (v->hi < v->lo) { const int32_t t = v->hi; v->hi = v->lo; v->lo = t; }

    if (chip) *chip = x * 2 - (v->hi + v->lo);
    return true;
}
