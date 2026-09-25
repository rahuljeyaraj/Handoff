#include "carrier.h"

void carrier_init(carrier_t *c)
{
    c->fast_shift = 2;    /* ~4 chips, which is what sets HANDOFF_DETECT_US */
    c->slow_shift = 11;   /* ~2048 chips: far slower than a 10 ms shout (~100) */
    c->ratio_num  = 24;   /* present at 3x the ambient floor (24/8)        */
    c->min_delta  = 24;   /* ...and at least this far above it, in LSB     */
    c->hold_chips = 8;
    /* reset() no longer touches these, and the struct is often a bare local. */
    c->level = 0;
    c->floor = 0;
    carrier_reset(c);
}

void carrier_reset(carrier_t *c)
{
    /*
     * level and floor are deliberately left alone. push() re-primes both from
     * the next chip before anything in here reads them, so zeroing them changed
     * no decision — it only made the status line print a detector that is
     * between chips as one reading nothing at all. During TX_FRAME no chips are
     * drained, so that window is a whole frame long and looks exactly like a
     * dead receiver. Callers that need the difference ask carrier_primed().
     */
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
     *
     * IT IS NOT ACTUALLY SYMMETRIC, and that was measured on 25 Sep 2026 rather
     * than argued. `>>` rounds toward minus infinity, so a dip of one LSB below
     * the floor subtracts a whole one while a rise adds one only at 2048 above.
     * The floor therefore walks downward only, and it walks all the way to the
     * clamp below — on ambient noise, and on a loud carrier too. Simulated
     * against this board's own measured chip energies it reaches 1 within a
     * couple of thousand chips from any starting point. The ratio test is then
     * comparing against 1 and min_delta is the only gate still standing, so the
     * detector is in practice a fixed threshold at level > 25.
     *
     * THE RATCHET IS LOAD-BEARING. Do not "fix" it without redesigning the
     * recovery path with it. Replacing it with a true symmetric average (a
     * floor_acc scaled by 2^slow_shift, with or without freezing the average
     * while present) turns 1 failure into 25: rendezvous dies at 10 of 117
     * phases and the link tests go with it. The reason is the re-prime. A
     * detector reset mid-frame primes its floor on a Manchester chip, high half
     * the time, and an honest average then needs ~2048 chips — 512 ms — to come
     * back down, against a rendezvous budget of 464 ms. The downward ratchet
     * drags a poisoned floor back in about 200 chips, and that accident is what
     * makes rendezvous work at all. test_beacon.c's "re-priming mid-frame is
     * not safe" is the test that watches this, and architecture §5.1 is the
     * open question it points at. Both ends need solving together.
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
bool     carrier_primed(const carrier_t *c)  { return c->primed; }
