/*
 * Handoff — half-duplex link state machine. architecture §7.
 *
 * A function of (state, event, now_us) producing actions. No blocking, no
 * sleeping, and no hardware access except through hal_iface_t — which is what
 * lets test/host/sim_twonode.c run two instances against a simulated channel
 * with injected time and injected randomness, thousands of handshakes per
 * second, at M1, with no hardware.
 *
 * The machine is driven entirely by polling and injected time, so §7.2's
 * awkward cases — contact lost mid-exchange, a frame lost during turnaround,
 * one end reset while the other keeps talking — are all reachable from a test
 * by advancing a virtual clock. That, rather than the shape of the dispatch,
 * is what §7.2 was really asking for.
 *
 * §7.2's fourth case, the election tie, is gone: there is no election. IDLE
 * runs the trigger of beacon.h, which decides who sends by timing geometry
 * rather than by a draw, and exits straight to TX_FRAME or RX_FRAME. See
 * docs/simple-trigger-spec.md §2.
 */
#ifndef HANDOFF_LINK_SM_H
#define HANDOFF_LINK_SM_H

#include <stdbool.h>
#include <stdint.h>

#include "beacon.h"
#include "carousel.h"
#include "frag.h"
#include "frame.h"
#include "hal.h"
#include "manchester.h"

typedef enum {
    LINK_IDLE = 0,     /* no contact: running the trigger, see beacon.h     */
    LINK_TX_FRAME,     /* clocking chips out                                */
    LINK_TURNAROUND,   /* amplifier recovering, design §9.7                 */
    LINK_RX_FRAME,     /* receiving                                         */
    LINK_EXCHANGE,     /* carousel running                                  */
    LINK_COMPLETE,     /* done, notify the phone                            */
    LINK_ABORT,        /* contact lost or unrecoverable                     */
    LINK_STATE_COUNT
} link_state_t;

const char *link_state_name(link_state_t s);

/*
 * Which end of the exchange this band is. NOT an election result — the trigger
 * hands it out, and §2 of the spec is the argument that exactly one band can
 * get SENDER. It survives as telemetry, and because the frame header carries a
 * REPLY bit and the carousel is asymmetric for the first turn.
 */
typedef enum {
    LINK_ROLE_NONE = 0,
    LINK_ROLE_SENDER,     /* heard a shout: the channel is ours, talk first */
    LINK_ROLE_RECEIVER    /* a card is already arriving: listen first       */
} link_role_t;

/*
 * Times the exchange may fall back into the trigger before giving up. Each
 * retry costs two barren receive turns to notice, so this is worth a second or
 * so of a contact — enough for the trigger to break a lockstep that the
 * geometry says should not happen, and not so many that a wedged pair sits
 * there for the whole budget instead of reporting what it has.
 */
#define LINK_MAX_RETRIES 4

typedef struct {
    /* Frames sent before handing the channel over. One frame is ~156 ms at
     * HANDOFF_GZ_N 25, so a turn of 1 lets two three-frame cards cross whole
     * in one second: A1 B1 A2 B2 A3 B3. A turn of 2 lands B3 at the eighth
     * frame, and a one-second contact completes half as often — measured by
     * `handoff_sweep turn`. */
    uint8_t  frames_per_turn;

    /*
     * A gap of silence after which the channel is considered free. This, and
     * counting the far end's frames, are what actually drive handover.
     *
     * Under link v2 presence has no hold and no hysteresis, so this no longer
     * has to outlast one: it has to outlast the gap a real frame can leave in
     * the busy latch, which frame_rx_busy() covers anyway. It still has to
     * stay well under a frame so handover is prompt.
     *
     * There is deliberately no separate receive-window setting: a fixed window
     * that could be shorter than a turn is a foot-gun — set it a little too
     * small and the receiver walks away in the middle of the last frame of
     * every turn. enter_rx() derives its safety-net deadline from
     * frames_per_turn instead, so the two cannot disagree.
     */
    uint32_t rx_idle_us;

    /* Whole-contact budget. R1 says contact lasts about a second; this is
     * deliberately longer, because giving up early wastes a real handshake. */
    uint32_t contact_budget_us;

    /* False for the one-way link of M12: complete as soon as their record is
     * in, without waiting to have sent our own. */
    bool     two_way;

    uint8_t  carousel_weight;
} link_cfg_t;

void link_cfg_default(link_cfg_t *c);

typedef struct {
    const hal_iface_t *hal;
    link_cfg_t   cfg;
    link_state_t state;
    link_role_t  role;

    trig_t       trig;
    carousel_t   car;
    frame_rx_t   framer;
    frag_rx_t    rx;

    const frag_tx_t *own;     /* our record, already split                 */

    uint64_t     started_us;
    uint64_t     deadline_us;
    uint64_t     last_busy_us;   /* last poll the channel read busy        */
    uint8_t      turn_frames;    /* frames sent in the current turn        */
    uint8_t      rx_turn_frames; /* frames heard since we last transmitted */
    uint8_t      barren_turns;   /* consecutive receive turns that heard nothing */
    uint8_t      retries;        /* trips back through the trigger, §4.5   */
    bool         peer_has_ours;  /* they told us they have our whole record      */
    bool         sent_ack;       /* we have told THEM we have theirs             */

    /*
     * An exchange is open: the trigger is being re-run to recover one, not to
     * start a new contact. It is what keeps suspect_collision() from wiping
     * the half-built record that architecture §8.4 says is worth keeping.
     */
    bool         exchange_open;

    /*
     * frame_rx_t::syncs as of the last poll in which the trigger was not
     * waiting. Only a sync newer than this counts as "a card is arriving" —
     * see poll_idle().
     */
    uint32_t     idle_syncs;

    uint8_t      chips[FRAME_TOTAL_CHIPS];
    size_t       chips_len;

    /* counters — telemetry, and the assertions in sim_twonode */
    uint32_t     frames_sent;
    uint32_t     frames_rx_good;
    uint32_t     frames_rx_bad;
    uint32_t     turnarounds;
} link_sm_t;

void         link_sm_init(link_sm_t *sm, const hal_iface_t *hal,
                          const link_cfg_t *cfg, const frag_tx_t *own);

/*
 * Start an exchange NOW, in the role given, skipping the trigger. Clears the
 * received record and restarts the contact budget.
 *
 * On a wrist nothing calls this: link_sm_idle() is how a handshake starts, and
 * the trigger is what assigns the role. It exists for a host that has decided
 * both halves itself — the two-node simulator, which is testing the exchange
 * rather than the rendezvous, and a bench app driving one end deliberately.
 */
void         link_sm_begin(link_sm_t *sm, uint64_t now_us, link_role_t role);

/*
 * Arm the trigger: the band looks for someone to talk to and enters the
 * exchange itself when it finds one, in whichever role the trigger gives it.
 * This, not link_sm_begin(), is how a band on a wrist starts a handshake — see
 * beacon.h for why the trigger has to be built out of the link rather than out
 * of a sensor.
 *
 * Also how a band returns to service after LINK_COMPLETE or LINK_ABORT.
 */
void         link_sm_idle(link_sm_t *sm, uint64_t now_us);

/* Drive one step. Drains received chips, advances the machine, queues chips
 * to transmit. Call as often as convenient; it is edge-free. */
link_state_t link_sm_poll(link_sm_t *sm, uint64_t now_us);

link_role_t  link_sm_role(const link_sm_t *sm);

/* Their record, as far as it has arrived. Decodable at any point, not only at
 * completion — see frag_rx_blob. */
size_t       link_sm_received(const link_sm_t *sm, const uint8_t **blob);

#endif /* HANDOFF_LINK_SM_H */
