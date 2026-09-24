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
 *   SETTLE   HANDOFF_TRIG_SETTLE_US                (deaf — own amp recovering)
 *   LISTEN   50-100 ms, drawn per cycle            (ears open, continuously)
 *
 * While listening, anything heard is one of exactly three things:
 *
 *   flat carrier lasting HANDOFF_SHOUT_MIN_US or more, framer never locks
 *                                      somebody's shout   wait for silence,
 *                                                         then send our card
 *   alternating preamble, framer locks a card arriving    receive it
 *   flat carrier, but too short        not a band at all  keep listening, and
 *                                                         do not count it
 *   nothing, for the whole drawn window nobody there      shout again
 *
 * THE THIRD CASE IS NOT OPTIONAL, and leaving it out is what broke the first
 * assembled boards. Without a length test the band cannot tell a peer from its
 * own amplifier or from the room, and a band ALONE on a bench, with no peer
 * powered at all, elected itself sender on 60 shouts out of 60 — see
 * HANDOFF_SHOUT_MIN_US and HANDOFF_TRIG_SETTLE_US for the measurements.
 *
 * THE LISTEN TIMER COUNTS SILENT TIME ONLY. It is held while a carrier is
 * present. Without that a band would shout over a card already in flight.
 *
 * That is the entire trigger. There is no election, no backoff draw, no
 * listen-before-talk, no role hint, no redraw, no tie.
 *
 * WHY THERE IS NOTHING TO ELECT. To hear the other band's shout you must have
 * your ears open before their shout ends. Your own ears open SHOUT_US +
 * TRIG_SETTLE_US after your own shout began — 16 ms here.
 *
 * The algebra below is written with the old 11 ms and still holds in shape: the
 * deaf window got longer, which only makes the earlier shouter MORE certainly
 * the one that fails to hear. What the longer window does cost is rendezvous
 * time, because a band is now deaf for a larger fraction of each cycle and so
 * more often misses the start of a peer's shout and fails the length gate. That
 * costs a retry, not a handshake, and the phase sweep in test_beacon.c is what
 * bounds it — read the number it prints, do not estimate it.
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
 * Deaf time after our own shout, while the amplifier comes out of saturation.
 *
 * NOT HANDOFF_TURNAROUND_US, which is design §9.7's 1 ms measured at M8 on the
 * breadboard and governs the gap between frames inside an exchange. This is the
 * same physical quantity measured on the first assembled PCB, where it is much
 * longer: a shout is 10 ms of flat drive into an amplifier with a gain of 11
 * that hard-limits, and C6 couples the drive track straight into its input.
 *
 * Measured 24 Sep 2026. Freshly armed, a band printed `trig silent 0 us`
 * repeatedly — the carrier detector was already up the instant its ears opened
 * — and the carrier then lasted about 4.2 ms. Taking off the detector's 8-chip
 * hold, the band's own shout is still above threshold about 3.2 ms after the
 * pad goes idle, so 1 ms of settle opened the ears squarely into it.
 *
 * The cost of getting this wrong is not subtle. With 1 ms, a band ALONE on a
 * bench, with no peer powered at all, heard its own shout, read it as somebody
 * else's, and elected itself sender on 60 shouts out of 60. TRIG_RECEIVE never
 * fired once. 6 ms is the measured 3.2 with room for a louder board.
 */
#ifndef HANDOFF_TRIG_SETTLE_US
#define HANDOFF_TRIG_SETTLE_US    6000u
#endif

/*
 * How long a carrier must have lasted before it counts as somebody's shout.
 *
 * The second half of the same 24 Sep 2026 finding. A settle long enough to miss
 * our own shout still leaves the room, which on this bench puts ~4 ms bursts
 * into the receive band often enough to land in most listen windows — and
 * TRIG_WAIT, as first written, read ANY carrier that came and went as a shout.
 * It never asked how long it lasted, so noise and a peer were the same event.
 *
 * They are not, and the two populations do not overlap. A peer's shout is
 * HANDOFF_SHOUT_US of flat tone; the detector raises its flag about
 * HANDOFF_DETECT_US in and drops it hold_chips after the tone stops, so a fully
 * heard one measures about 11 ms, and board two measured board one's real
 * shouts at 13.5-16.5 ms. Everything the bench produced that was not a shout
 * measured 8.2 ms or less.
 *
 * Sit between them, nearer the junk: a gate set too high rejects real shouts
 * and breaks the rendezvous outright, while one set too low only wastes a
 * contact that the next cycle retries. That asymmetry is why this is 9 and not
 * 11.
 *
 * A carrier that fails this test is not an event at all — the band returns to
 * LISTEN *keeping its silence budget*, rather than drawing a fresh window. A
 * fresh draw here would be a lock-up: bursts arriving every ~25 ms against a
 * 50-100 ms draw mean the window would never once run out, so the band would
 * never shout again and would go off the air completely.
 */
#ifndef HANDOFF_SHOUT_MIN_US
#define HANDOFF_SHOUT_MIN_US      9000u
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
    HANDOFF_LISTEN_MIN_US > HANDOFF_SHOUT_US + HANDOFF_TRIG_SETTLE_US,
    "listen window can be shorter than the deaf phase: some cycles never listen");

/* The settle exists because the PCB's amplifier outlasts §9.7's 1 ms. If it
 * were ever set shorter than that, the trigger would be deaf for less time than
 * the exchange is, which is the bug this constant was added to fix. */
HANDOFF_STATIC_ASSERT(HANDOFF_TRIG_SETTLE_US >= HANDOFF_TURNAROUND_US,
    "trigger settles for less time than the exchange turnaround");

/* A gate at or above what a fully heard shout measures rejects every real
 * shout, and the rendezvous stops working altogether rather than degrading. */
HANDOFF_STATIC_ASSERT(
    HANDOFF_SHOUT_MIN_US < HANDOFF_SHOUT_US - HANDOFF_DETECT_US + 8u * HANDOFF_CHIP_US,
    "shout gate is above what a fully heard shout measures: nothing can pass it");

/* The gate has to be reachable inside the quiet-wait cap, or a legal shout is
 * cut short by the timeout before it can ever be long enough to count. */
HANDOFF_STATIC_ASSERT(HANDOFF_QUIET_WAIT_MAX_US > HANDOFF_SHOUT_MIN_US,
    "quiet-wait cap expires before a shout can satisfy the gate");

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
    /* Carriers rejected by HANDOFF_SHOUT_MIN_US: heard, but too short to have
     * been anybody's shout. On a quiet channel this stays at zero; on a loud
     * bench it is the count of contacts the gate saved. */
    uint32_t short_carriers;

    /*
     * The anatomy of the last trip through WAIT, which is the only way to tell
     * a band triggering on a peer from one triggering on itself or on the room.
     *
     *   last_silent_us   silence already banked when the carrier appeared. Near
     *                    zero every cycle means the band is hearing its own
     *                    shout decay; scattered across the listen window means
     *                    it is hearing the room.
     *   last_wait_us     how long the carrier then lasted. A peer's shout is
     *                    HANDOFF_SHOUT_US of flat tone; an impulse from the
     *                    room clears in about the detector's hold.
     *
     * Both are set when WAIT resolves, either way, and are telemetry only —
     * nothing in the machine reads them.
     */
    uint64_t wait_started_us;
    uint32_t last_silent_us;
    uint32_t last_wait_us;
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
