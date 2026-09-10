/*
 * Handoff — contact detection. What takes a band out of LINK_IDLE.
 *
 * THE PROBLEM. architecture §7.1 lists IDLE as exiting on "carrier detected,
 * or host says go", which is not a mechanism: if every band waits to hear a
 * carrier, no band ever emits one and no handshake ever starts. The hardware
 * has no button, no accelerometer and no touch sensor — design §6 gives it one
 * drive electrode, one sense electrode and an ADC. So the trigger has to be
 * built out of the link itself.
 *
 * THE OBSERVATION. Before two wearers touch there is no channel: a band's
 * transmission is inaudible to the other one. The moment skin meets skin a
 * channel exists. So a transmission BEING HEARD is itself the contact signal,
 * and no separate sensor is needed — the channel is the sensor. Nothing else
 * on this board has that property.
 *
 * THE CYCLE. Each band free-runs, unsynchronised with any other:
 *
 *   BEACON   drive a plain carrier for BEACON_ON_US
 *   SETTLE   deaf, HANDOFF_TURNAROUND_US, while our own amplifier recovers
 *   LISTEN   listen for BEACON_LISTEN_US, long enough to cover a peer that
 *            woke on that beacon and is now backing off before it replies
 *   SNIFF    listen SNIFF_ON_US out of every SNIFF_PERIOD_US until the next
 *            beacon slot, which is BEACON_PERIOD_US away plus a random jitter
 *
 * THE GUARANTEE. A band is deaf for at most SNIFF_PERIOD_US - SNIFF_ON_US
 * between sniff windows, so a beacon longer than that gap plus the detector's
 * latency cannot fall entirely inside it:
 *
 *   BEACON_ON_US >= (SNIFF_PERIOD_US - SNIFF_ON_US) + ELECT_DETECT_US
 *
 * That is asserted below. It makes rendezvous deterministic rather than lucky:
 * whatever the phase between two free-running bands, a beacon lands in a sniff
 * window, and worst-case latency is one beacon period rather than a tail that
 * only shows up on a demo day.
 *
 * ANSWERING AT THE RIGHT MOMENT, WHICH IS NOT THE SAME ON BOTH SIDES.
 *
 * A band that hears a beacon in a SNIFF has caught it near its start, and the
 * far end is still transmitting and still deaf. Answering immediately loses the
 * front of our own preamble, so a sniff wake goes through BEACON_HOLD and
 * elects only once the channel is quiet. carrier.c's hysteresis lands that
 * about 2 ms after the beacon truly ends — which is when the far end has
 * finished its turnaround and is listening for exactly this.
 *
 * A band that hears something in its own post-beacon LISTEN is in the opposite
 * position: what it hears is a FRAME, not a beacon, because the only reason
 * anyone is transmitting into that window is that they woke on our beacon. It
 * must wake NOW. Holding for quiet there means waiting out their whole 156 ms
 * frame, and the preamble it needs is in the first 8 ms of it.
 *
 * Getting this asymmetry wrong is expensive and it is not obvious from the
 * state machine, so the numbers are worth keeping. Against a host-triggered
 * start over 60 seeds, mean time to a completed exchange:
 *
 *   host call, no beacon at all              973 ms    376 frames sent
 *   answer immediately on both sides        1452 ms          -   (20 bad CRC)
 *   hold on both sides                      1930 ms    935 frames sent
 *   hold on sniff, answer at once on listen 1061 ms    396 frames sent
 *
 * The last is the rendezvous cost and nothing else.
 *
 * THE ELECTION STILL RUNS, AND IT IS NOT REDUNDANT. The wake reason is an
 * asymmetry the beacon gets for free, so elect_assume() takes it rather than
 * re-deriving it with a draw — and re-deriving is not free, because waiting for
 * a beacon to clear releases both ends at the SAME instant, which is the worst
 * case for a random draw. But only the INITIATOR half of the hint is sound; see
 * elect_assume() for why the TARGET half is not, and falls back to the draw.
 *
 * THE ONE HOLE, AND THE JITTER. The guarantee covers every deaf gap except our
 * own beacon: a band cannot hear a peer while its own amplifier is driving. So
 * two bands that beacon at the same instant miss each other, and the sniff
 * cycle cannot fix that because neither is sniffing. This is the same shape as
 * the election tie in elect.h and takes the same answer — decorrelate and
 * retry. The next slot is BEACON_PERIOD_US plus a draw from 0..JITTER_US, so a
 * repeat collision needs the draws to land within a beacon of each other,
 * about one round in five, and two rounds in a row is one in twenty-five.
 *
 * WHY A PLAIN CARRIER AND NOT AN ALTERNATING PATTERN. frame.c hunts for a run
 * of alternating chips followed by a 00, so a beacon of 1010... IS a preamble
 * and would manufacture false syncs in any band that happened to be framing. A
 * constant-on burst contains no transitions at all, so the framer can never
 * mistake it for a frame. Beacon and frame are unambiguous by construction
 * rather than by a length check.
 *
 * WHY THE BEACON IS SHORT. carrier.c tracks the ambient floor with a slow
 * symmetric EMA, ~128 chips. A burst that ran much past that would be absorbed
 * into the floor and stop reading as a carrier — the detector would lose the
 * very signal it is being shown. BEACON_ON_US is asserted well inside that.
 *
 * WHY SNIFF AT ALL, RATHER THAN JUST LISTENING CONTINUOUSLY. Listening the
 * whole time between beacons would work and would be simpler — there would be
 * no deaf gap, so the guarantee above would hold trivially and the beacon could
 * shrink to about two detection latencies. It is rejected on power, and the
 * direction of that trade is the opposite of a radio's.
 *
 * There is no PA here. design §6.3 makes the transmitter a GPIO through a 1 M
 * resistor into a capacitive pad drawing ~12 uA, and architecture §3.3 keeps
 * core 1 out of the transmit path entirely — DMA and one PIO state machine do
 * it. Receiving is the expensive half: the ADC free-running at 500 ksps, its
 * DMA, and core 1 running the Goertzel flat out at 150 MHz.
 *
 * Estimated from the datasheets, pending measurement at M2:
 *
 *              RX duty   TX duty   mean current (excl. AFE)
 *   sniffing     29 %      6.7 %      ~3.9 mA
 *   always on    98 %      1.3 %     ~10.8 mA
 *
 * taking RX as ~11 mA (core 1 ~10, ADC+DMA ~1), TX as ~0.3 mA and idle ~1 mA.
 * Sniffing wins by about 2.2x — on a 500 mAh cell, roughly 85 hours of idle
 * against 39. So the longer beacon that sniffing forces is nearly free, and
 * the listening it avoids is what actually costs.
 *
 * HOW MUCH FURTHER THIS COULD GO, AND WHY IT DOES NOT. If receiving dominates,
 * the obvious move is a lower sniff duty and a longer beacon to cover the
 * bigger gap. The carrier detector caps that: the floor-tracking limit above
 * holds the beacon under ~16 ms, which holds the sniff duty above ~12 %. The
 * default 20 % is within about 12 % of the best current-draw that constraint
 * allows, and taking it would leave the beacon sitting on the assert with no
 * margin. The remaining headroom is not here — it is the 20 ms post-beacon
 * listen, and the MCP6292 pair, which design §6.1 leaves powered from 3V3 with
 * no way for firmware to gate them.
 */
#ifndef HANDOFF_BEACON_H
#define HANDOFF_BEACON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "elect.h"
#include "hal.h"

/*
 * Listening duty cycle while idle. SNIFF_ON_US is two detection latencies, so
 * a beacon overlapping the window has time to raise the flag inside it rather
 * than just after it.
 */
#ifndef HANDOFF_SNIFF_ON_US
#define HANDOFF_SNIFF_ON_US      (2u * ELECT_DETECT_US)
#endif

#ifndef HANDOFF_SNIFF_PERIOD_US
#define HANDOFF_SNIFF_PERIOD_US  (10u * ELECT_DETECT_US)
#endif

/* Long enough to satisfy the guarantee above, short enough to stay well inside
 * the carrier detector's floor time constant. */
#ifndef HANDOFF_BEACON_ON_US
#define HANDOFF_BEACON_ON_US     (10u * ELECT_DETECT_US)
#endif

/*
 * How long we listen after our own beacon. A peer that woke on it holds until
 * our beacon clears, then waits out a draw of up to HANDOFF_BACKOFF_MAX_US and
 * an election listen before it transmits — so this window has to cover the
 * carrier detector's hold, the draw and the listen, or we hang up on the reply
 * we asked for. The slack is deliberate: a few more milliseconds of receiving
 * is cheap, and missing the reply costs a whole frame.
 */
#ifndef HANDOFF_BEACON_LISTEN_US
#define HANDOFF_BEACON_LISTEN_US (HANDOFF_BACKOFF_MAX_US + ELECT_LISTEN_US + \
                                  6u * ELECT_DETECT_US)
#endif

/*
 * Cap on BEACON_HOLD. A carrier that never clears is a stuck transmitter or a
 * noise floor that has moved, and waiting on it for ever would take the band
 * off the air; electing anyway is the same answer §7.3 gives to an election
 * that will not settle. Sized past the longest legitimate beacon.
 */
#ifndef HANDOFF_BEACON_HOLD_MAX_US
#define HANDOFF_BEACON_HOLD_MAX_US (HANDOFF_BEACON_ON_US + 8u * ELECT_DETECT_US)
#endif

/*
 * Beacon spacing. This is the whole rendezvous latency budget, and what it
 * trades against is receive duty cycle, not transmit: a longer period means
 * more sniffing between beacons and a longer wait to be found. Worst case is
 * one period plus jitter — roughly 200 ms of R1's one-second contact.
 */
#ifndef HANDOFF_BEACON_PERIOD_US
#define HANDOFF_BEACON_PERIOD_US 100000u
#endif

/* Draw range for the anti-collision jitter. Comparable to the period, so two
 * bands that collided once are very unlikely to collide again. */
#ifndef HANDOFF_BEACON_JITTER_US
#define HANDOFF_BEACON_JITTER_US HANDOFF_BEACON_PERIOD_US
#endif

/* The rendezvous guarantee, as a build failure rather than a comment. */
HANDOFF_STATIC_ASSERT(
    HANDOFF_BEACON_ON_US >= (HANDOFF_SNIFF_PERIOD_US - HANDOFF_SNIFF_ON_US)
                            + ELECT_DETECT_US,
    "a beacon can fall entirely inside the peer's deaf gap: rendezvous is not "
    "guaranteed");

HANDOFF_STATIC_ASSERT(HANDOFF_SNIFF_ON_US >= 2u * ELECT_DETECT_US,
    "sniff window too short for the carrier detector to raise its flag");

HANDOFF_STATIC_ASSERT(HANDOFF_SNIFF_PERIOD_US > HANDOFF_SNIFF_ON_US,
    "sniff window cannot be longer than its period");

/* carrier.c's floor is a slow EMA over ~128 chips; a beacon that outlasts it
 * is absorbed into the floor and stops reading as a carrier. */
HANDOFF_STATIC_ASSERT(HANDOFF_BEACON_ON_US <= 64u * HANDOFF_CHIP_US,
    "beacon outlasts the carrier detector's noise floor tracking");

HANDOFF_STATIC_ASSERT(
    HANDOFF_BEACON_LISTEN_US >= HANDOFF_BACKOFF_MAX_US + ELECT_LISTEN_US,
    "post-beacon listen is shorter than the reply it is waiting for");

HANDOFF_STATIC_ASSERT(
    HANDOFF_BEACON_PERIOD_US > HANDOFF_BEACON_ON_US + HANDOFF_BEACON_LISTEN_US,
    "beacon slots overlap: there is no idle time left to sniff in");

#define BEACON_BURST_CHIPS (HANDOFF_BEACON_ON_US / HANDOFF_CHIP_US)

typedef enum {
    BEACON_OFF = 0,    /* not armed                                        */
    BEACON_TX,         /* driving a plain carrier burst                    */
    BEACON_SETTLE,     /* deaf, our own amplifier recovering               */
    BEACON_LISTEN,     /* listening for a peer that woke on our beacon     */
    BEACON_SNIFF,      /* short listening window                           */
    BEACON_GAP,        /* deaf, between sniff windows                      */
    BEACON_HOLD,       /* heard something: waiting for it to finish        */
    BEACON_CONTACT     /* heard a peer: there is a channel, so there is a  */
                       /* body in it                                       */
} beacon_state_t;

typedef struct {
    const hal_iface_t *hal;
    beacon_state_t state;
    uint64_t       deadline_us;   /* end of the current phase              */
    uint64_t       next_tx_us;    /* start of the next beacon slot         */
    bool           burst_due;     /* chips not yet handed to the HAL       */
    elect_role_t   wake_role;     /* what the wake reason implies          */

    /* counters — telemetry, and the assertions in the tests */
    uint32_t       beacons;
    uint32_t       sniffs;
    uint32_t       holds;
    uint32_t       wakes;
} beacon_t;

void           beacon_init(beacon_t *b, const hal_iface_t *hal);

/* Arm the cycle. Starts with a beacon, so a band that has just been put on a
 * wrist announces itself rather than waiting out a period first. */
void           beacon_start(beacon_t *b, uint64_t now_us);

void           beacon_stop(beacon_t *b);

/*
 * Drive one step. carrier_heard is meaningful only while beacon_listening() is
 * true; the caller must not feed the carrier detector at all in the deaf
 * phases, for the reason drain_discard() gives in link_sm.c.
 *
 * Returns BEACON_CONTACT exactly once per wake, at which point the caller
 * should start the election.
 */
beacon_state_t beacon_poll(beacon_t *b, uint64_t now_us, bool carrier_heard);

/*
 * The role the wake implies, valid once beacon_poll returns BEACON_CONTACT.
 * Woke in a sniff — we heard their beacon and they are now listening for an
 * answer — means INITIATOR. Woke in our own post-beacon listen means someone
 * answered us, so TARGET. See elect_assume().
 */
elect_role_t   beacon_wake_role(const beacon_t *b);

/* True while the receive path should be feeding the carrier detector. */
bool           beacon_listening(const beacon_t *b);

/*
 * True once per beacon, when the burst needs queueing. Clears on read, so the
 * caller queues the chips exactly once rather than every poll.
 */
bool           beacon_take_burst(beacon_t *b);

/* Chips of a beacon burst: all 1, no transitions. Returns chips written. */
size_t         beacon_fill(uint8_t *chips, size_t max);

#endif /* HANDOFF_BEACON_H */
