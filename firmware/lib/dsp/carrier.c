#include "carrier.h"

void carrier_init(carrier_t *c)
{
    c->fast_shift = 2;    /* ~4 chips, which is what sets HANDOFF_DETECT_US */
    c->slow_shift = 11;   /* ~2048 chips: far slower than a 10 ms shout (~100) */
    c->ratio_num  = 24;   /* present at 3x the ambient floor (24/8)        */
    c->min_delta  = 24;   /* ...and at least this far above it, in LSB     */
    c->hold_chips = 8;
    carrier_reset(c);
}

void carrier_reset(carrier_t *c)
{
    c->level = 0;
    c->floor = 0;
    c->present = false;
    c->hold = 0;
    c->primed = false;
}

void carrier_push(carrier_t *c, uint16_t chip_energy)
{
    const uint32_t e = chip_energy;

    if (!c->primed) { c->level = e; c->floor = e ? e : 1; c->primed = true; }

    c->level += ((int32_t)e - (int32_t)c->level) >> c->fast_shift;

    /*
     * The floor is a slow SYMMETRIC average of the ambient, not a chaser after
     * the minimum. A min-chaser collapses toward zero on the quiet half of
     * every Manchester bit, and then the ratio test fires on the noisy half —
     * the detector declares a carrier against its own noise. Symmetric and
     * slow, it barely moves across a 32-chip preamble, which is exactly the
     * window listen-before-talk has to decide in.
     */
    c->floor += ((int32_t)e - (int32_t)c->floor) >> c->slow_shift;
    if (c->floor == 0) c->floor = 1;

    /*
     * Both tests, and the second one is what the ratio alone cannot do: near a
     * floor of one or two LSB, three times nothing is still nothing.
     */
    if ((uint64_t)c->level * 8u > (uint64_t)c->floor * c->ratio_num &&
        c->level > c->floor + c->min_delta) {
        c->present = true;
        c->hold = c->hold_chips;
    } else if (c->hold) {
        c->hold--;
    } else {
        c->present = false;
    }
}

bool     carrier_present(const carrier_t *c) { return c->present; }
uint32_t carrier_level(const carrier_t *c)   { return c->level; }
uint32_t carrier_floor(const carrier_t *c)   { return c->floor; }
