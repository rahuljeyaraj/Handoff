/*
 * Handoff — the contact trigger. What takes a band out of LINK_IDLE.
 * docs/simple-trigger-spec.md, superseding architecture §7.3 and §7.6.
 *
 * The filename still says "beacon" to limit churn; the module is the trigger.
 *
 * THE PROBLEM. architecture §7.1 once listed IDLE as exiting on "carrier
 * detected, or host says go", which is not a mechanism: if every band waits to
 * hear a carrier, no band ever emits one and no handshake ever starts. The
 * hardware has no button, no accelerometer and no touch sensor — design §6
 * gives it one drive electrode, one sense electrode and an ADC. So the trigger
 * has to be built out of the link itself.
 *
 * THE OBSERVATION, unchanged from the design this replaces. Before two wearers
 * touch there is no channel: a band's transmission is inaudible to the other
 * one. The moment skin meets skin a channel exists. So a transmission BEING
 * HEARD is itself the contact signal, and no separate sensor is needed — the
 * channel is the sensor. Nothing else on this board has that property.
 *
 * THE RULE. Every band free-runs this loop, unsynchronised with any other:
 *
 *   SHOUT    HANDOFF_SHOUT_US of flat carrier      (deaf — own amp driving)
 *   SETTLE   HANDOFF_TURNAROUND_US                 (deaf — own amp recovering)
 *   LISTEN   50-100 ms, drawn per cycle            (ears open, continuously)
 *
 * While listening, anything heard is one of exactly two things:
 *
 *   flat carrier, framer never locks   somebody's shout   wait for silence,
 *                                                         then send our card
 *   alternating preamble, framer locks a card arriving    receive it
 *   nothing, for the whole drawn window nobody there      shout again
 *
 * THE LISTEN TIMER COUNTS SILENT TIME ONLY. It is held while a carrier is
 * present. Without that a band would shout over a card already in flight.
 *
 * That is the entire trigger. There is no election, no backoff draw, no
 * listen-before-talk, no role hint, no redraw, no tie.
 *
 * WHY THERE IS NOTHING TO ELECT. To hear the other band's shout you must have
 * your ears open before their shout ends. Your own ears open SHOUT_US +
 * TURNAROUND_US after your own shout began — 11 ms here.
 *
 * For two shouts starting at t_A and t_B, with t_A < t_B:
 *
 *   - B's ears are open at t_A, so B always hears A.
 *   - A's ears open at t_A + 11, by which time A's own shout is long over. A
 *     hears B only if B is still shouting then, i.e. t_B + 10 > t_A + 12,
 *     which needs t_B > t_A + 2. But if B heard A first, B stops shouting and
 *     waits.
 *
 * The earlier shouter is always too late; the later shouter is always in time.
 * At most one band can hear the other's shout, so THE SENDER IS DECIDED BY
 * PHYSICS. Both-send and both-listen are unreachable states. That is timing
 * algebra over the constants below, not a machine-checked proof — the phase
 * sweep in test_beacon.c is what turns it into evidence, and it is the single
 * most important test in that file.
 *
 * The one degenerate case is |t_A - t_B| smaller than the detector latency,
 * where neither hears. Both then draw a fresh listen duration and whichever
 * shouts first next round is heard, because the other is listening
 * continuously — there are no deaf gaps except during one's own shout. The
 * draw range is what decorrelates the retry, which is why it is asserted
 * against the detector latency below. test_beacon.c measures the repeat rate;
 * do not carry an arithmetic estimate of it forward.
 *
 * WHY THE SHOUT IS A FLAT CARRIER AND NOT A PREAMBLE. A preamble shout works —
 * the listener sees no marker follow and concludes it was a shout — but it
 * buys nothing, because a flat tone already says a band is present and about
 * to listen, and the listener must wait for silence either way. The cost is
 * real: a preamble is exactly the pattern frame.c hunts for, so every shout
 * would drag the peer's framer into a half-locked state ~10 times a second,
 * and a decaying burst tail can supply a false marker. A flat carrier has no
 * transitions at all, so it cannot be mistaken for a frame — unambiguous by
 * construction rather than by timeout.
 *
 * WHY THE SHOUT IS SHORT, RATHER THAN SENDING THE CARD BLIND. Transmitting the
 * whole 156 ms card every cycle would remove a round trip, but it makes a band
 * deaf most of the time instead of a small fraction of it, so two bands would
 * frequently transmit over each other and lose both cards — and the proof
 * above, which depends on a short deaf window, would collapse. The election
 * would have to come back.
 *
 * WHY LISTENING IS CONTINUOUS. It costs power, and the duty-cycled sniffing
 * this replaces costs about 2.2x less by an estimate from datasheets that has
 * never been measured. That is a deliberate v1 decision on a bench, not an
 * oversight: the proof above depends on there being no deaf gap outside one's
 * own shout, and duty-cycled receiving can be reintroduced later without
 * changing the rule.
 */
#ifndef HANDOFF_BEACON_H
#define HANDOFF_BEACON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal.h"

/*
 * The shout. Long enough that a peer can raise its presence flag and still see
 * the burst end, short enough to stay well inside carrier.c's floor tracking.
 */
#ifndef HANDOFF_SHOUT_US
#define HANDOFF_SHOUT_US          (10u * HANDOFF_DETECT_US)
#endif

/*
 * The listen window, drawn fresh every cycle from [MIN, MAX). The range is not
 * a latency budget — it is the decorrelator: two bands that shouted at the same
 * instant repeat only if their next two draws land within a detector latency of
 * each other, so the range has to be many latencies wide.
 */
#ifndef HANDOFF_LISTEN_MIN_US
#define HANDOFF_LISTEN_MIN_US     50000u
#endif

#ifndef HANDOFF_LISTEN_MAX_US
#define HANDOFF_LISTEN_MAX_US     100000u
#endif

/*
 * Cap on TRIG_WAIT. A carrier that never clears is a stuck transmitter, noise,
 * or a floor that has moved, and waiting on it for ever would take the band off
 * the air. On expiry the band resets its carrier detector — the floor may
 * genuinely have moved — returns to listening, and DOES NOT SEND. That last
 * part is the difference from the beacon design this replaces, which elected
 * anyway: sending into a channel that is provably busy is worse than waiting a
 * cycle.
 */
#ifndef HANDOFF_QUIET_WAIT_MAX_US
#define HANDOFF_QUIET_WAIT_MAX_US 30000u
#endif

/* carrier.c's floor is a slow EMA over ~128 chips; a shout that outlasted it
 * would be absorbed into the floor and stop reading as a carrier — the
 * detector would lose the very signal it is being shown. */
HANDOFF_STATIC_ASSERT(HANDOFF_SHOUT_US <= 64u * HANDOFF_CHIP_US,
    "shout outlasts the carrier detector's noise floor tracking");

HANDOFF_STATIC_ASSERT(HANDOFF_SHOUT_US >= 4u * HANDOFF_DETECT_US,
    "shout too short for a peer to raise its flag and still see it end");

HANDOFF_STATIC_ASSERT(
    HANDOFF_LISTEN_MIN_US > HANDOFF_SHOUT_US + HANDOFF_TURNAROUND_US,
    "listen window can be shorter than the deaf phase: some cycles never listen");

/* The draw range is what decorrelates a simultaneous-shout collision. */
HANDOFF_STATIC_ASSERT(
    (HANDOFF_LISTEN_MAX_US - HANDOFF_LISTEN_MIN_US) >= 16u * HANDOFF_DETECT_US,
    "listen draw range too narrow to decorrelate a simultaneous shout");

HANDOFF_STATIC_ASSERT(
    HANDOFF_QUIET_WAIT_MAX_US > HANDOFF_SHOUT_US + 8u * HANDOFF_CHIP_US,
    "quiet-wait cap can truncate a legal shout plus the detector's hysteresis");

#define TRIG_SHOUT_CHIPS (HANDOFF_SHOUT_US / HANDOFF_CHIP_US)

typedef enum {
    TRIG_OFF = 0,   /* not armed                                            */
    TRIG_SHOUT,     /* driving a flat carrier burst — deaf                  */
    TRIG_SETTLE,    /* deaf, our own amplifier recovering                   */
    TRIG_LISTEN,    /* ears open, counting down silent time                 */
    TRIG_WAIT,      /* heard something: shout, or card? whichever comes     */
                    /* first — the carrier clearing, or the framer locking  */
    TRIG_SEND,      /* terminal: it was a shout, the channel is ours        */
    TRIG_RECEIVE    /* terminal: it was a card, and it is already arriving  */
} trig_state_t;

typedef struct {
    const hal_iface_t *hal;
    trig_state_t state;

    uint64_t deadline_us;    /* end of the current timed phase              */
    uint32_t listen_us;      /* the duration drawn for this listen          */
    uint32_t silent_left_us; /* of it, how much silence is still owed       */
    uint64_t last_poll_us;   /* to charge elapsed time to the right bucket  */

    bool     burst_due;      /* chips not yet handed to the HAL             */
    bool     reset_due;      /* the caller owes the carrier detector a reset*/

    /* counters — telemetry, and the assertions in the tests */
    uint32_t shouts;
    uint32_t waits;
    uint32_t quiet_timeouts;
    uint32_t sends;
    uint32_t receives;
} trig_t;

void         trig_init(trig_t *t, const hal_iface_t *hal);

/* Arm the cycle. Starts with a shout, so a band that has just been put on a
 * wrist announces itself rather than listening out a whole window first. */
void         trig_start(trig_t *t, uint64_t now_us);

void         trig_stop(trig_t *t);

/*
 * Drive one step.
 *
 * carrier_heard and framer_locked are meaningful only while trig_listening()
 * is true; the caller must not feed the carrier detector at all in the deaf
 * phases, for the reason drain_discard() gives in link_sm.c.
 *
 * framer_locked means the framer has taken a preamble AND its marker — not
 * that it is merely hunting. It is the answer to "flat or preamble?", which
 * cannot be answered when the carrier first appears: presence arrives about a
 * detector latency in, and the framer needs a whole preamble and marker. So
 * the question is settled by which happens first, a lock or silence, and the
 * caller must poll often enough not to step over the lock — the body of a
 * frame lasts far longer than any sane poll interval, so this is not tight.
 *
 * Returns TRIG_SEND or TRIG_RECEIVE exactly once per contact, at which point
 * the caller transmits or receives and stops polling this.
 */
trig_state_t trig_poll(trig_t *t, uint64_t now_us, bool carrier_heard,
                       bool framer_locked);

/* True while the receive path should be feeding the carrier detector and the
 * framer. False in the two deaf phases, and in the two terminal states. */
bool         trig_listening(const trig_t *t);

/*
 * True once per shout, when the burst needs queueing. Clears on read, so the
 * caller queues the chips exactly once rather than every poll.
 */
bool         trig_take_burst(trig_t *t);

/*
 * True once when the quiet-wait cap expired and the carrier detector should be
 * reset. Clears on read. Kept as a request rather than done here because the
 * detector belongs to the caller — the trigger is given a bool, not a carrier_t.
 */
bool         trig_take_carrier_reset(trig_t *t);

/* The listen duration currently being counted down. Telemetry, and what the
 * simultaneous-start test inspects to know a redraw really happened. */
uint32_t     trig_listen_us(const trig_t *t);

/* Chips of a shout: all 1, no transitions. Returns chips written. */
size_t       trig_fill(uint8_t *chips, size_t max);

#endif /* HANDOFF_BEACON_H */
