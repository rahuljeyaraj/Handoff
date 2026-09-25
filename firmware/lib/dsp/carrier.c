#include "carrier.h"

/*
 * Fractional bits carried under the floor, and the whole reason the floor
 * works at all now.
 *
 * The old floor was a whole number updated by
 *
 *     floor += (e - floor) >> slow_shift
 *
 * and its own comment called that a slow symmetric average. It was not one.
 * Ambient chip energies sit a few LSB either side of the floor, so the shifted
 * delta was -1 or 0 and never +1: a dip of one LSB subtracted a whole 1, while
 * a rise added nothing until the chip was 2^slow_shift above. The floor could
 * only ever walk downward, and it walked down until it sat on the MINIMUM of
 * whatever it was watching — on ambient noise, and on a loud carrier too.
 * Measured on 379E the floor column only ever fell: 15, 11, 9, 8, 7, 6. Where
 * ambient reaches zero it walked to the clamp at 1, the ratio test was then
 * comparing against 1, and min_delta was the only gate still standing — which
 * made the detector a fixed threshold at level > 25 wearing an ambient
 * tracker's clothes.
 *
 * Keeping the remainder fixes it at the root, because the average can now move
 * by less than one LSB per chip instead of rounding the move away. The
 * rounding direction stops mattering once the fraction is there: `>>` still
 * rounds toward minus infinity, but what it rounds away is half of one part in
 * 2^FLOOR_FRAC, which leaves the settled floor about 0.03 LSB off the mean.
 * Rounding the step toward zero instead was tried and no test can tell the two
 * apart, so the simpler line stands.
 *
 * slow_shift must stay at or below FLOOR_FRAC, or a one-LSB difference shifts
 * away to nothing again and the old behaviour comes straight back. 65535 <<
 * FLOOR_FRAC also has to leave the delta inside an int32, which is the other
 * reason this is 12 and not larger.
 */
#define FLOOR_FRAC 12
#define FLOOR_ONE  (1u << FLOOR_FRAC)

static void floor_set(carrier_t *c, uint32_t whole)
{
    c->floor_acc = (whole ? whole : 1u) << FLOOR_FRAC;
    c->floor     = c->floor_acc >> FLOOR_FRAC;
}

static void prime_begin(carrier_t *c)
{
    c->prime_left = c->prime_chips;
    c->prime_min  = 0xFFFFu;
}

void carrier_init(carrier_t *c)
{
    c->fast_shift   = 2;    /* ~4 chips, which is what sets HANDOFF_DETECT_US */
    c->slow_shift   = 8;    /* ~256 chips of ambient, ~64 ms                  */
    c->ratio_num    = 24;   /* present at 3x the ambient floor (24/8)         */
    c->min_delta    = 24;   /* ...and at least this far above it, in LSB      */
    c->hold_chips   = 8;
    c->prime_chips  = 256;  /* see the prime below: long enough to hold quiet */
    c->prime_shift  = 2;
    c->freeze_chips = 12000;

    c->level = 0;
    c->held  = 0;
    floor_set(c, 1);
    carrier_reprime(c);
}

void carrier_reset(carrier_t *c)
{
    /*
     * The floor is deliberately untouched. It describes the room, and a
     * handshake changing state says nothing about the room — throwing it away
     * here is what used to leave the detector blind, because the chip that
     * re-primed it was as likely as not a Manchester high.
     *
     * `level` is the opposite: it is meant to be what is on the channel right
     * now, and after a transmit turn its last value is our own shout with no
     * chips drained since. So it is re-primed from the next chip. Until that
     * chip arrives the detector has no level at all, which during TX_FRAME is a
     * whole frame long and used to print as a dead receiver; primed() is how a
     * status line tells the two apart.
     */
    c->present = false;
    c->hold    = 0;
    c->held    = 0;
    c->primed  = false;
}

void carrier_reprime(carrier_t *c)
{
    carrier_reset(c);
    prime_begin(c);
}

void carrier_push(carrier_t *c, uint16_t chip_energy)
{
    const uint32_t e = chip_energy;

    if (!c->primed) { c->level = e; c->primed = true; }
    else c->level += ((int32_t)e - (int32_t)c->level) >> c->fast_shift;

    if (c->prime_left) {
        /*
         * Prime the floor from the MINIMUM over a window, not from one chip and
         * not from a mean. A mean is where the floor should settle, but a mean
         * taken while somebody is transmitting lands halfway up the carrier,
         * and the average is then perfectly happy to stay there: the floor has
         * met the carrier, the ratio test can no longer see it, and the
         * detector is deaf to that carrier for good. A minimum cannot be set by
         * a Manchester high, because the chip after it is a low one.
         *
         * The window has to be long enough to contain silence, and that is what
         * sets its length rather than any settling argument. A shout is flat
         * tone, not Manchester — HANDOFF_SHOUT_US of it, about 40 chips — so a
         * short window landing inside one has no quiet chip in it at all and
         * primes at the shout's own level. That is measured, not feared: with a
         * 32-chip window the phase sweep lost three offsets outright, because
         * the band that primed inside its peer's shout went deaf and its floor
         * then settled on the frames it could no longer hear. 256 chips is
         * longer than a shout and than §4.3's quiet-wait cap, and a peer's
         * listen window is 200 chips at its shortest, so a peer cannot fill one
         * however the two cycles line up. Presence is decided against the
         * running minimum meanwhile, so this costs sensitivity only until the
         * first quiet chip, not for the whole window.
         *
         * The minimum of noise sits well below its mean, so it is corrected up
         * by prime_shift before it is believed. Undershooting is the safe
         * direction: a floor that is too low is over-sensitive, one that is too
         * high is deaf, and only one of those recovers by itself.
         */
        if (e < c->prime_min) c->prime_min = (uint16_t)e;
        floor_set(c, (uint32_t)c->prime_min << c->prime_shift);
        c->prime_left--;
    } else if (!c->present || c->held >= c->freeze_chips) {
        /*
         * A true average, and it learns only while the channel is quiet. The
         * freeze is what keeps the floor out of the carrier: an average that
         * runs through a frame converges on the carrier's own mean, and it was
         * measured doing exactly that — a receiver on the first assembled board
         * printed `level 548 floor 256 present 0` with a known-good transmitter
         * mid-frame, and ended every receive turn over the top of the frame it
         * was waiting for.
         *
         * The freeze is bounded because a detector that latched would otherwise
         * freeze its own floor for ever. Nothing real holds this channel for
         * freeze_chips: a frame is FRAME_TOTAL_CHIPS and a whole contact budget
         * is twenty of them, so presence lasting longer than that is the
         * floor's fault and the average is let go to find what is really there.
         * It is a backstop, not a mechanism — §4.3's reprime is the fast way
         * out, and normal traffic comes nowhere near the count. Presence is
         * sticky across a transmit turn, when no chips are drained at all, so
         * `held` is chips of unbroken presence rather than elapsed time; a
         * whole handshake measures under 2000.
         */
        const int32_t d = (int32_t)(e << FLOOR_FRAC) - (int32_t)c->floor_acc;

        c->floor_acc = (uint32_t)((int32_t)c->floor_acc + (d >> c->slow_shift));
        if (c->floor_acc < FLOOR_ONE) c->floor_acc = FLOOR_ONE;
        c->floor = c->floor_acc >> FLOOR_FRAC;
    }

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

    if (!c->present) c->held = 0;
    else if (c->held < 0xFFFFu) c->held++;
}

bool     carrier_present(const carrier_t *c) { return c->present; }
uint32_t carrier_level(const carrier_t *c)   { return c->level; }
uint32_t carrier_floor(const carrier_t *c)   { return c->floor; }
bool     carrier_primed(const carrier_t *c)  { return c->primed; }
