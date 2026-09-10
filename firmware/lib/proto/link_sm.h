/*
 * Handoff — half-duplex link state machine. architecture §7.
 *
 * A function of (state, event, now_us) producing actions. No blocking, no
 * sleeping, and no hardware access except through hal_iface_t — which is what
 * lets test/host/sim_twonode.c run two instances against a simulated channel
 * with injected time and injected randomness, thousands of handshakes per
 * second, at M1, with no hardware.
 *
 * The machine is driven entirely by polling and injected time, so §7.2's four
 * awkward cases — the election tie, contact lost mid-exchange, a frame lost
 * during turnaround, one end reset while the other keeps talking — are all
 * reachable from a test by advancing a virtual clock. That, rather than the
 * shape of the dispatch, is what §7.2 was really asking for.
 */
#ifndef HANDOFF_LINK_SM_H
#define HANDOFF_LINK_SM_H

#include <stdbool.h>
#include <stdint.h>

#include "carousel.h"
#include "carrier.h"
#include "elect.h"
#include "frag.h"
#include "frame.h"
#include "hal.h"

typedef enum {
    LINK_IDLE = 0,     /* no contact                                       */
    LINK_BACKOFF,      /* random draw, design §9.6                         */
    LINK_LISTEN,       /* measuring carrier                                */
    LINK_TX_FRAME,     /* clocking chips out                               */
    LINK_TURNAROUND,   /* amplifier recovering, design §9.7                */
    LINK_RX_FRAME,     /* receiving                                        */
    LINK_EXCHANGE,     /* carousel running                                 */
    LINK_COMPLETE,     /* done, notify the phone                           */
    LINK_ABORT,        /* contact lost or unrecoverable                    */
    LINK_STATE_COUNT
} link_state_t;

const char *link_state_name(link_state_t s);

typedef struct {
    /* Frames sent before handing the channel over. One frame is ~150 ms at
     * HANDOFF_GZ_N 25, so a turn of 2 is about a third of a one-second
     * contact — swept at M1 alongside the carousel weighting. */
    uint8_t  frames_per_turn;

    /*
     * A gap of silence after which the channel is considered free. This, and
     * counting the far end's frames, are what actually drive handover. Must
     * exceed the carrier detector's hold (8 chips) so it cannot fire between
     * the chips of a frame, and stay well under a frame so handover is prompt.
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

    elect_t      elect;
    carousel_t   car;
    carrier_t    carrier;
    frame_rx_t   framer;
    frag_rx_t    rx;

    const frag_tx_t *own;     /* our record, already split                 */

    uint64_t     started_us;
    uint64_t     deadline_us;
    uint64_t     last_carrier_us;
    uint8_t      turn_frames;    /* frames sent in the current turn        */
    uint8_t      rx_turn_frames; /* frames heard since we last transmitted */
    uint8_t      barren_turns;   /* consecutive receive turns that heard nothing */
    bool         peer_has_ours;  /* they told us they have our whole record      */
    bool         sent_ack;       /* we have told THEM we have theirs             */

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

/* Contact detected (or the host said go). Starts the election. */
void         link_sm_begin(link_sm_t *sm, uint64_t now_us);

/* Drive one step. Drains received chips, advances the machine, queues chips
 * to transmit. Call as often as convenient; it is edge-free. */
link_state_t link_sm_poll(link_sm_t *sm, uint64_t now_us);

elect_role_t link_sm_role(const link_sm_t *sm);

/* Their record, as far as it has arrived. Decodable at any point, not only at
 * completion — see frag_rx_blob. */
size_t       link_sm_received(const link_sm_t *sm, const uint8_t **blob);

#endif /* HANDOFF_LINK_SM_H */
