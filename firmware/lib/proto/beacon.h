/*
 * Handoff — the contact trigger. What takes a band out of LINK_IDLE.
 * docs/link-v2-design.md §7, docs/link-v2-brief.md §8. Link v2 step 7.
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
 * THE OBSERVATION, unchanged since the first design. Before two wearers touch
 * there is no channel: a band's transmission is inaudible to the other one.
 * The moment skin meets skin a channel exists. So a transmission BEING HEARD
 * is itself the contact signal, and no separate sensor is needed — the channel
 * is the sensor. Nothing else on this board has that property.
 *
 * ---- 1. WHAT STEP 7 CHANGED, AND WHY ------------------------------------
 *
 * v1 shouted a FLAT TONE and asked three questions of it by timing:
 *
 *      is that a peer?        it lasted longer than HANDOFF_SHOUT_MIN_US
 *      is that my own echo?   my ears were shut when I shouted, probably
 *      who sends?             whoever heard the other one first
 *
 * Every one of those is a judgement about an event with NO CONTENT, and
 * design §2 is the argument that a featureless trigger is what forces every
 * threshold downstream. Four bugs on 24 Sep came out of it, and the worst was
 * a band alone on a bench electing itself sender on 60 shouts out of 60,
 * because a 10 ms tone decaying in its own amplifier is indistinguishable
 * from a 10 ms tone arriving from somebody else.
 *
 * v2 shouts a FRAME, frame.h's second frame type, carrying a 16-bit nonce
 * under a CRC-16:
 *
 *      is that a peer?        the CRC passed. 2^-16 by construction
 *      is that my own echo?   the nonce is mine. STRUCTURAL
 *      who sends?             I decoded it, so I did not shout over it. §2
 *
 * The middle row is the whole point. "Own shout heard" is not fixed here, it
 * is UNWRITABLE: identity is in the payload rather than inferred from when
 * the band's ears happened to open. HANDOFF_SHOUT_MIN_US, HANDOFF_SHOUT_US,
 * HANDOFF_QUIET_WAIT_MAX_US and the whole TRIG_WAIT state are gone with it.
 *
 * ---- 2. WHY THERE IS STILL NOTHING TO ELECT ------------------------------
 *
 * Design §7's table says the sender is the band with the HIGHER NONCE. It is
 * not, and the reason is the same shape as step 6 deriving away the imbalance
 * correction: the requirement a nonce comparison would serve does not exist.
 *
 * Every band free-runs this loop, unsynchronised with any other:
 *
 *      BEACON   FRAME_BEACON_AIRTIME_US of frame   (deaf — own amp driving)
 *      SETTLE   HANDOFF_TRIG_SETTLE_US             (deaf — own amp recovering)
 *      LISTEN   drawn per cycle                    (ears open, continuously)
 *
 * so a band is deaf for D = beacon + settle starting at its own beacon. Take
 * two bands at relative phase p, and ask who can decode whom:
 *
 *   - B decodes A only if A's whole beacon misses B's deaf window. A decoder
 *     therefore has NOT yet started its own beacon this cycle.
 *   - A decoder stops beaconing: it leaves the trigger as the sender. So its
 *     beacon never goes out, and the band it decoded has nothing to decode.
 *   - If B could not decode A because the two beacons OVERLAP, then A could
 *     not decode B either — B's beacon began inside A's own deaf window.
 *
 * AT MOST ONE BAND EVER DECODES THE OTHER. Both-send and both-listen are
 * unreachable, exactly as they were under v1's timing algebra, and for the
 * same reason: hearing the other band is what stops you shouting. What is new
 * is that "hearing" now means a CRC passed rather than a tone outlasting a
 * constant somebody chose on a bench.
 *
 * That is timing algebra, not a machine-checked proof. test_beacon.c's phase
 * sweep is what turns it into evidence, and it is the most important test in
 * that file.
 *
 * SO WHAT IS THE NONCE FOR? Own-echo rejection, which is a real requirement
 * with a real bug behind it — and, through that, the tie: two bands that drew
 * the same 16 bits read each other as an echo, so neither sends, and both
 * redraw before their next beacon. Probability 2^-16 = 1.5e-5 per contact,
 * cost one cycle. A nonce COMPARISON would be machinery with nothing to
 * compare, because the geometry above never puts two decoders in one cycle.
 *
 * ---- 3. THE LISTEN TIMER COUNTS SILENT TIME ONLY -------------------------
 *
 * It is held while the channel reads busy. Without that a band would beacon
 * over a card already in flight, and this is the one consumer design §6 gives
 * presence: listen before talk. Everything else that used to ask
 * carrier_present() now asks the frame decoder, which is self-validating.
 */
#ifndef HANDOFF_BEACON_H
#define HANDOFF_BEACON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "frame.h"
#include "hal.h"

/*
 * ---- the deaf window after our own beacon, MEASURED 25 Sep 2026 ----------
 *
 * Two things have to have finished before the ears open, and the surprise is
 * which of them is the big one.
 *
 *   THE AMPLIFIER, coming out of saturation. A beacon is flat-out drive into
 *   a gain of 11 that hard-limits, and C6 couples the drive track straight
 *   into its input.
 *
 *   THE RING. Core 1 sees ADC samples only once the DMA has filled a block,
 *   so samples taken while the amplifier was still hot keep arriving for
 *   HANDOFF_RX_LATENCY_US after it has gone cold. link_sm.c throws them away
 *   while the trigger is deaf; the deafness has to last long enough that it
 *   throws away all of them.
 *
 * WHAT THE BENCH SAID, and brief §8 asked for exactly this reading. `y 4` in
 * linktest drives the board's own pad, releases it, and watches presence —
 * the detector the trigger listens through — for 200 ms. Three drive lengths,
 * eight runs each, far board silent, 93D1:
 *
 *      drive     last busy: min    mean     max
 *       7 ms                2630    3230    3650 us
 *      14 ms                3616    3949    4219 us
 *      28 ms (a beacon)     2391    3010    3956 us
 *
 * TWO FINDINGS, AND BOTH CHANGE WHAT THIS CONSTANT IS.
 *
 * 1. IT IS NOT THE AFE's HIGH-PASS RC. Brief §8 expected to measure one, and
 *    there is not one to measure: the signal bin reads ~670 at 0, 1 and 2 ms
 *    and ~20 by 4 ms, which is a step, not a decay — and a step at exactly
 *    one DMA block. The verdicts stay hot while the RING drains and go cold
 *    the moment it has. The amplifier is back inside 4219 - 4096 = 123 us.
 *    The old 6000 came from a 24 Sep reading taken through v1's carrier
 *    detector, which had an 8-chip hold of its own on top of the ring.
 *
 * 2. IT DOES NOT GROW WITH THE BEACON. That was the real worry, because step
 *    7 took the transmission from a 10 ms shout to a 28 ms frame: if the
 *    recovery were a coupling cap charging, the beacon would pay for its
 *    length twice. The three means are 3230, 3949 and 3010 us — flat, and
 *    the longest drive is not the worst. It pays once.
 *
 * SO THE VALUE IS DERIVED, from one structural number and one that this
 * project has measured since M8:
 *
 *      one DMA block        HANDOFF_RX_LATENCY_US    4096 us   structural
 *      the amplifier        HANDOFF_TURNAROUND_US    1000 us   design §9.7
 *                                                    -------
 *                                                    5096 us
 *
 * HANDOFF_TURNAROUND_US is already this board's measured figure for "the
 * amplifier coming out of saturation" — it is what the exchange waits between
 * frames — and it is eight times the 123 us above, so it covers it with room
 * for a louder board. Nothing new is typed, and the worst run measured is
 * 4219 us against 5096.
 *
 * AND IF IT IS WRONG, IT IS NO LONGER DANGEROUS. Under v1 a settle that was
 * too short meant a band heard its own shout and elected itself sender, sixty
 * times out of sixty. A self-echo now carries our own nonce and is thrown
 * away by §1's middle row and counted in self_echoes. What a short settle
 * costs today is airtime, and what a long one costs is the same airtime at
 * the other end of the cycle.
 */
#ifndef HANDOFF_TRIG_SETTLE_US
#define HANDOFF_TRIG_SETTLE_US    (HANDOFF_RX_LATENCY_US + HANDOFF_TURNAROUND_US)
#endif

/*
 * ---- the beacon period, DERIVED by minimising the rendezvous -------------
 *
 * There is no number to choose here. §2's geometry gives the probability that
 * a cycle rendezvous, and the cycle length that minimises the expected time
 * falls out of it — a stationary point, not a preference.
 *
 * Write T for the beacon's airtime, S for the settle, L for the listen, and
 * C = T + S + L for the whole cycle. §2 walks the cases and leaves exactly
 * one that fails: the two beacons OVERLAP, which is a relative phase inside
 * T of either end of the cycle. So
 *
 *      P(a cycle fails)  =  2T / C
 *      E[cycles]         =  C / (C - 2T)
 *      E[time]           =  C^2 / (C - 2T)
 *
 * and d/dC of that is zero at
 *
 *      2C(C - 2T) - C^2 = 0   ->   C = 4T   ->   E[time] = 8T
 *
 * A shorter cycle collides more often; a longer one wastes the time it saved.
 * So the cycle is four beacons long and the listen is what is left:
 *
 *      mean L = 4T - T - S = 3T - S
 *
 * THE DRAW. A FIXED period is a lock-up, not a nicety: two bands whose
 * beacons overlap would overlap for ever, sliding apart only at the crystals'
 * tens of ppm — at 40 ppm it takes 800 seconds to move one beacon's width.
 * So L is drawn fresh every cycle, and the range has to be at least as wide
 * as the region it must escape, which is the 2T failure region. Mean plus or
 * minus T:
 *
 *      L in [2T - S, 4T - S)
 *
 * At T = 28 ms and S = 5.096 ms that is 50.9 to 106.9 ms, a mean cycle of
 * 112 ms — which is 4T exactly, however the settle moves, because the settle
 * is subtracted from the listen — and an expected rendezvous of 224 ms.
 *
 * THE 224 IS A BOUND, NOT A PREDICTION, and the measurement is better than it
 * by a factor of nearly three. The model above holds the relative phase still
 * and gives the pair one shared cycle length; the real thing redraws L every
 * cycle, so the two beacons sweep through each other instead of sitting in
 * the failure region, and a pair that collides once is somewhere else next
 * time. test_beacon.c's steady-state measurement — two bands already running,
 * touched at every offset across a cycle — reads a mean of 86 ms and a worst
 * of 145 ms. The bound is still what the period is CHOSEN from, because a
 * period cannot be chosen from a simulator run without becoming the kind of
 * number §1 forbids; the measurement is what says the choice was not a bad
 * one.
 *
 * AND IT IS BARELY SLOWER THAN v1, WHICH IS NOT WHAT THE ARITHMETIC SAYS.
 * 8T is the floor for any scheme of this shape, so a 28 ms beacon against
 * v1's 10 ms flat shout should have cost 2.8x. Measured on the same
 * simulator, by the same method, from a `main` worktree:
 *
 *      touched mid-cycle      v1          v2
 *      mean                   64 ms       86 ms
 *      worst                  152 ms      145 ms
 *
 * 22 ms on the mean and seven milliseconds BETTER on the worst case. The
 * beacon gets most of the ratio back at the other end: v1 could not act on a
 * shout until the carrier had gone away again, then measured how long it had
 * lasted against HANDOFF_SHOUT_MIN_US, and could sit in TRIG_WAIT for
 * HANDOFF_QUIET_WAIT_MAX_US before giving up. A beacon is decided on its last
 * chip, because the CRC is the decision.
 *
 * The beacon is 2.8x the airtime because a CRC-checked nonce needs a preamble
 * the hunt can lock to (32 chips), a marker (16) and a body (64). frame.h
 * §"the beacon frame" is where the 16 ms of body is argued down to the two
 * fields that have a consumer.
 */
#define HANDOFF_BEACON_AIRTIME_US  FRAME_BEACON_AIRTIME_US

#define HANDOFF_LISTEN_MIN_US  (2u * HANDOFF_BEACON_AIRTIME_US - HANDOFF_TRIG_SETTLE_US)
#define HANDOFF_LISTEN_MAX_US  (4u * HANDOFF_BEACON_AIRTIME_US - HANDOFF_TRIG_SETTLE_US)

/* The cycle this produces on average, for the tests and the console. */
#define HANDOFF_BEACON_CYCLE_US                                        \
    (HANDOFF_BEACON_AIRTIME_US + HANDOFF_TRIG_SETTLE_US +              \
     (HANDOFF_LISTEN_MIN_US + HANDOFF_LISTEN_MAX_US) / 2u)

/* The whole derivation in one line: the mean cycle IS four beacons. Anything
 * that moves the settle or the beacon body keeps that true or fails here. */
HANDOFF_STATIC_ASSERT(HANDOFF_BEACON_CYCLE_US == 4u * HANDOFF_BEACON_AIRTIME_US,
    "the mean cycle is no longer the 4T that minimises the rendezvous");

/*
 * The settle cannot be so long that the listen window it leaves is shorter
 * than the deaf phase, which would be a band that spends more of its life
 * transmitting and recovering than listening.
 */
HANDOFF_STATIC_ASSERT(HANDOFF_TRIG_SETTLE_US < HANDOFF_BEACON_AIRTIME_US,
    "the settle is longer than the beacon: the draw range would go negative");

/*
 * A band that has just beaconed must not beacon AGAIN before the peer that
 * decoded it can answer. The peer waits HANDOFF_TRIG_SETTLE_US for our
 * amplifier plus HANDOFF_TURNAROUND_US for its own before its preamble starts
 * — see enter_exchange() in link_sm.c — and once it is transmitting, §3's
 * listen-before-talk holds us off. This is the window before that.
 */
HANDOFF_STATIC_ASSERT(
    HANDOFF_LISTEN_MIN_US > HANDOFF_TRIG_SETTLE_US + HANDOFF_TURNAROUND_US,
    "a band can beacon again before the peer that decoded it has replied");

/* True by construction above, and asserted because a board that overrides the
 * settle must not set it below the amplifier's own figure: the trigger would
 * then be deaf for less time than the exchange is, which is the bug this
 * constant was added to fix. */
HANDOFF_STATIC_ASSERT(HANDOFF_TRIG_SETTLE_US >= HANDOFF_TURNAROUND_US,
    "trigger settles for less time than the exchange turnaround");

/* And it must outlast the ring, or samples of our own beacon are still being
 * delivered when the ears open — which is what the 25 Sep reading showed the
 * settle is really made of. */
HANDOFF_STATIC_ASSERT(HANDOFF_TRIG_SETTLE_US > HANDOFF_RX_LATENCY_US,
    "the ears open while the DMA is still delivering our own beacon");

/* The draw has to be able to move the phase out of the region that fails. */
HANDOFF_STATIC_ASSERT(
    (HANDOFF_LISTEN_MAX_US - HANDOFF_LISTEN_MIN_US) >= 2u * HANDOFF_BEACON_AIRTIME_US,
    "the draw range is narrower than the overlap region it must escape");

typedef enum {
    TRIG_OFF = 0,   /* not armed                                            */
    TRIG_BEACON,    /* clocking a beacon frame out — deaf. Ends on the      */
                    /* airtime AND on the pad going quiet, not either one   */
    TRIG_SETTLE,    /* deaf, our own amplifier recovering                   */
    TRIG_LISTEN,    /* ears open, counting down silent time                 */
    TRIG_SEND,      /* terminal: we decoded a peer, so the channel is ours  */
    TRIG_RECEIVE    /* terminal: a card is already arriving                 */
} trig_state_t;

/*
 * What the caller heard since the last poll. All four are meaningful only
 * while trig_listening() is true; in the deaf phases the caller must throw
 * the receive path away rather than report it, for the reason
 * drain_discard() gives in link_sm.c.
 */
typedef struct {
    bool     busy;    /* presence: somebody is on the channel RIGHT NOW     */
    bool     beacon;  /* a beacon frame passed its CRC                      */
    uint16_t nonce;   /* ...and this is what it carried                     */
    /*
     * The framer has taken a CARD preamble AND its marker — not that it is
     * merely hunting, and not a beacon. It is the answer to "is somebody
     * already sending me a card?", which happens when the peer decoded our
     * beacon and we never decoded theirs. frame.c counts beacon syncs
     * separately for exactly this.
     */
    bool     card;
} trig_in_t;

typedef struct {
    const hal_iface_t *hal;
    trig_state_t state;

    uint64_t deadline_us;    /* end of the current timed phase              */
    uint32_t listen_us;      /* the duration drawn for this listen          */
    uint32_t silent_left_us; /* of it, how much silence is still owed       */
    uint64_t last_poll_us;   /* to charge elapsed time to the right bucket  */

    bool     burst_due;      /* a beacon not yet handed to the HAL          */

    /*
     * Who we are, for this arming. §2: the only thing it has to do is tell
     * our own beacon coming back at us from somebody else's.
     *
     * REDRAWN AT THE NEXT BEACON, NOT WHEN THE ECHO ARRIVES, and that
     * ordering is the whole safety of it. A beacon of ours is still on the
     * wire, and in the ipc ring, for milliseconds after we stop driving;
     * redrawing the instant one came back would leave the stragglers carrying
     * a nonce that is no longer ours, which is to say it would turn our own
     * echo into a peer — precisely the bug the nonce exists to make
     * unwritable. So the current nonce keeps rejecting them, and the fresh
     * one goes out with the next beacon.
     */
    uint16_t nonce;
    bool     nonce_stale;    /* a beacon carrying ours came back: redraw    */

    /* counters — telemetry, and the assertions in the tests */
    uint32_t beacons;        /* beacons transmitted                         */
    uint32_t peers;          /* beacons decoded carrying somebody else      */
    /*
     * Beacons decoded carrying OUR nonce. On a wrist this should be zero: the
     * settle is supposed to outlast our own amplifier. A number here is not a
     * fault — it is the trigger reporting that it caught what v1 could not
     * even see, and the same counter is the 2^-16 nonce tie.
     */
    uint32_t self_echoes;
    uint32_t redraws;
    /* Draws thrown away by frame_beacon_nonce_ok(). About one in 1700. */
    uint32_t nonce_rejects;
    uint32_t sends;
    uint32_t receives;
} trig_t;

void         trig_init(trig_t *t, const hal_iface_t *hal);

/* Arm the cycle. Starts with a beacon, so a band that has just been put on a
 * wrist announces itself rather than listening out a whole window first. */
void         trig_start(trig_t *t, uint64_t now_us);

void         trig_stop(trig_t *t);

/*
 * Drive one step.
 *
 * Returns TRIG_SEND or TRIG_RECEIVE exactly once per contact, at which point
 * the caller transmits or receives and stops polling this.
 */
trig_state_t trig_poll(trig_t *t, uint64_t now_us, const trig_in_t *in);

/* True while the receive path should be feeding presence and the framer.
 * False in the two deaf phases, and in the two terminal states. */
bool         trig_listening(const trig_t *t);

/*
 * True once per beacon, when the frame needs queueing. Clears on read, so the
 * caller queues the chips exactly once rather than every poll.
 */
bool         trig_take_burst(trig_t *t);

/* The listen duration currently being counted down. Telemetry, and what the
 * simultaneous-start test inspects to know a redraw really happened. */
uint32_t     trig_listen_us(const trig_t *t);

/* Our nonce, as the next beacon will carry it. Telemetry and the tests. */
uint16_t     trig_nonce(const trig_t *t);

/*
 * The beacon as chips: FRAME_BEACON_TOTAL_CHIPS of them, carrying our nonce.
 * Returns chips written.
 *
 * UNLIKE v1's SHOUT, THIS IS A PREAMBLE. v1 drove a flat carrier and argued
 * the point at length: a preamble shout would drag the peer's framer into a
 * half-locked state ten times a second for nothing, because a flat tone
 * already said a band was present and the listener had to wait for silence
 * either way. The beacon has content, so the framer locking on it is not a
 * cost, it is the mechanism — and frame.h's second marker is what stops the
 * peer's framer mistaking it for the front of a card.
 */
size_t       trig_fill(const trig_t *t, uint8_t *chips, size_t max);

#endif /* HANDOFF_BEACON_H */
