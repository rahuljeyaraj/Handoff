/*
 * Handoff — carrier presence. Running energy against a tracked noise floor.
 *
 * Two consumers: the contact trigger, which asks whether anyone is on the
 * channel (beacon.h), and the turnaround guard that must not mistake the tail
 * of our own transmission for the other end starting to talk (§9.7).
 *
 * THE FLOOR IS A PROPERTY OF THE ROOM, NOT OF THE STATE MACHINE. It is an
 * estimate of the channel with nobody transmitting, so it is learned only while
 * no carrier is present, and it survives every reset. That is the whole design,
 * and it replaces an earlier one in which reset() threw the floor away and
 * push() re-primed it from the next chip — during a frame that chip is a
 * Manchester chip, high half the time, and a floor primed at the carrier's own
 * level left the detector blind for the rest of the frame. See carrier.c.
 *
 * reset() clears presence and re-primes `level` from the next chip, because
 * `level` is meant to be what is on the channel right now and after a transmit
 * turn its last value is our own shout. It leaves `floor` alone. Between the
 * reset and that next chip the detector has no level; a status line that wants
 * to tell that apart from a dead receiver asks primed().
 *
 * reprime() is the one way to throw the floor away, and only §4.3's quiet-wait
 * cap uses it: the channel has read busy for so long that the floor itself is
 * the likely suspect. It is safe to call mid-frame — the prime is taken from a
 * minimum, which a Manchester high cannot set.
 */
#ifndef HANDOFF_CARRIER_H
#define HANDOFF_CARRIER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t level;        /* fast EMA of chip energy                       */
    uint32_t floor;        /* the ambient estimate, whole LSB               */
    uint32_t floor_acc;    /* the same with fractional bits — carrier.c     */

    uint32_t peak;         /* highest level since carrier_take_peak()       */
    uint32_t peak_floor;   /* and the floor as it stood at that moment      */
    uint32_t floor_hi;     /* highest floor over the same window            */
    uint32_t floor_lo;     /* and the lowest                                */

    uint8_t  fast_shift;
    uint8_t  slow_shift;   /* floor time constant DOWNWARD, as a power of 2  */
    uint8_t  rise_shift;   /* ...and upward, which is deliberately slower    */
    uint8_t  ratio_num;    /* present when level > floor * ratio_num/8      */
    uint8_t  prime_shift;  /* floor primes at (window minimum << this)      */

    uint16_t min_delta;    /* ...and at least this many LSB above it        */
    uint16_t hold_chips;
    uint16_t hold;         /* chips of hysteresis remaining                 */

    uint16_t prime_chips;  /* length of the floor's prime window            */
    uint16_t prime_left;   /* chips of it still to come; 0 once primed      */
    uint16_t prime_min;    /* smallest chip energy seen in the window       */

    uint16_t freeze_chips; /* presence this long stops being believed       */
    uint16_t held;         /* chips of unbroken presence                    */

    bool     present;
    bool     primed;       /* `level` has seen a chip since the last reset  */
} carrier_t;

void     carrier_init(carrier_t *c);
void     carrier_reset(carrier_t *c);
void     carrier_reprime(carrier_t *c);
void     carrier_push(carrier_t *c, uint16_t chip_energy);
bool     carrier_present(const carrier_t *c);
uint32_t carrier_level(const carrier_t *c);
uint32_t carrier_floor(const carrier_t *c);

/*
 * What the detector did between two readings, rather than what it happened to
 * be doing at one instant.
 *
 * WHY A WINDOW AND NOT carrier_level(). A shout is HANDOFF_SHOUT_US long and
 * the flag is up for about 11 ms; a telemetry block leaves the band twice a
 * second. An instantaneous read therefore lands in the quiet between shouts
 * almost every time, and a plot of it is a flat line UNDER the gate while the
 * link is working perfectly — which is exactly how it was misread on 25 Sep
 * 2026, where the gate was in fact tripping about seven times a second.
 *
 * floor_hi and floor_lo are here because "the floor is moving with the level"
 * is a claim about the floor's EXCURSION, and two floors sampled 500 ms apart
 * cannot show one. peak_floor is the floor at the moment of the level peak,
 * which is the only pairing of the two that is a comparison at all.
 *
 * The call clears the window, so there is exactly one consumer.
 */
typedef struct {
    uint32_t level;       /* highest level seen in the window            */
    uint32_t floor_then;  /* the floor at that instant                   */
    uint32_t floor_hi;    /* highest floor in the window                 */
    uint32_t floor_lo;    /* lowest floor in the window                  */
} carrier_peak_t;

void carrier_take_peak(carrier_t *c, carrier_peak_t *out);
bool     carrier_primed(const carrier_t *c);

#endif /* HANDOFF_CARRIER_H */
