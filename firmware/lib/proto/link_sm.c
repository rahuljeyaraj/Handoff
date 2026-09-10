#include "link_sm.h"

#include <string.h>

static const char *const k_names[LINK_STATE_COUNT] = {
    "IDLE", "BACKOFF", "LISTEN", "TX_FRAME", "TURNAROUND",
    "RX_FRAME", "EXCHANGE", "COMPLETE", "ABORT"
};

const char *link_state_name(link_state_t s)
{
    return (s < LINK_STATE_COUNT) ? k_names[s] : "?";
}

void link_cfg_default(link_cfg_t *c)
{
    c->frames_per_turn   = 2;
    c->rx_idle_us        = 6u * HANDOFF_TURNAROUND_US;
    /* Twenty frames. Expressed in frames rather than milliseconds because a
     * budget that is generous at one chip rate is a hard cut-off at another —
     * at HANDOFF_GZ_N 50 a frame is twice as long and a flat 3 s truncated
     * one exchange in ten. */
    c->contact_budget_us = FRAME_AIRTIME_US * 20u;
    c->two_way           = true;
    c->carousel_weight   = CAROUSEL_DEFAULT_WEIGHT;
}

void link_sm_init(link_sm_t *sm, const hal_iface_t *hal,
                  const link_cfg_t *cfg, const frag_tx_t *own)
{
    memset(sm, 0, sizeof *sm);
    sm->hal = hal;
    sm->own = own;
    if (cfg) sm->cfg = *cfg; else link_cfg_default(&sm->cfg);

    elect_init(&sm->elect, hal);
    beacon_init(&sm->beacon, hal);
    carrier_init(&sm->carrier);
    frame_rx_init(&sm->framer);
    frag_rx_init(&sm->rx);
    carousel_init(&sm->car, own ? own->count : 1u, sm->cfg.carousel_weight);
    sm->state = LINK_IDLE;
}

void link_sm_begin(link_sm_t *sm, uint64_t now_us)
{
    frame_rx_init(&sm->framer);
    frag_rx_init(&sm->rx);
    carousel_init(&sm->car, sm->own ? sm->own->count : 1u, sm->cfg.carousel_weight);

    /*
     * The carrier detector IS reset, and on the beacon path that matters more
     * than it looks. A band that woke from a beacon has just spent BEACON_HOLD
     * feeding the far end's full-power carrier into the floor EMA, which leaves
     * the floor several times the true ambient. Carried into the exchange, that
     * floor makes real frames fail the presence test, and handover — which runs
     * on carrier_present and last_carrier_us — starts talking over the reply it
     * asked for. Leaving it primed was measured at 1137 frames sent for the
     * same 300 delivered, against 376 with the reset.
     *
     * The cost is that the election's listen-before-talk spends its window
     * re-priming and cannot see an already-running carrier. That is why
     * elect_assume() takes only the INITIATOR hint, which is sound on its own:
     * two ends cannot both hear the other's beacon from a sniff, because a band
     * is deaf while its own beacon plays.
     */
    carrier_reset(&sm->carrier);

    sm->started_us = now_us;
    sm->turn_frames = 0;
    sm->rx_turn_frames = 0;
    sm->barren_turns = 0;
    sm->peer_has_ours = false;
    sm->sent_ack = false;
    sm->chips_len = 0;

    beacon_stop(&sm->beacon);
    elect_start(&sm->elect, now_us);
    sm->state = LINK_BACKOFF;
}

void link_sm_idle(link_sm_t *sm, uint64_t now_us)
{
    hal_tx_drive(sm->hal, false);
    frame_rx_init(&sm->framer);
    carrier_reset(&sm->carrier);

    /*
     * The received record is NOT cleared. A contact that ended early left a
     * partial card in frag_rx, and architecture §8.4's whole argument is that
     * a partial card is worth something; throwing it away on the way back to
     * idle would discard exactly what the wearer is about to be shown.
     */
    sm->chips_len = 0;
    beacon_start(&sm->beacon, now_us);
    sm->state = LINK_IDLE;
}

/*
 * Drain whatever the DSP layer has produced since the last poll and run it
 * through carrier detection and the framer. Returns how many good frames
 * landed, and leaves the last one in sm->framer.
 */
static uint32_t drain_rx(link_sm_t *sm, bool feed_framer)
{
    uint16_t chips[64];
    uint32_t good = 0;
    size_t n, i;

    while ((n = hal_rx_chips(sm->hal, chips, sizeof chips / sizeof chips[0])) > 0) {
        for (i = 0; i < n; i++) {
            carrier_push(&sm->carrier, chips[i]);
            if (!feed_framer) continue;

            switch (frame_rx_push(&sm->framer, chips[i])) {
            case FRAME_RX_GOOD:
                sm->frames_rx_good++;
                if (frame_rx_hdr(&sm->framer)->flags & FRAME_FLAG_HAVE_YOURS)
                    sm->peer_has_ours = true;
                frag_rx_add(&sm->rx, frame_rx_hdr(&sm->framer),
                            frame_rx_payload(&sm->framer), HANDOFF_FRAG_PAYLOAD);
                good++;
                break;
            case FRAME_RX_BAD_CRC:
                /* Never a corrupted contact: the fragment is simply dropped
                 * and the carousel will come round again (architecture §8.4). */
                sm->frames_rx_bad++;
                break;
            default:
                break;
            }
        }
        if (n < sizeof chips / sizeof chips[0]) break;
    }
    return good;
}

/*
 * Drop everything the DSP produced without looking at it. Used while our own
 * transmitter is driving the shared pad: what core 1 reports then is our own
 * amplifier in saturation, and feeding it to the carrier detector poisons the
 * noise floor for the turn that follows — which showed up as an end that had
 * just transmitted believing the channel was silent, and talking straight over
 * the reply it had asked for.
 */
static void drain_discard(link_sm_t *sm)
{
    uint16_t chips[64];
    while (hal_rx_chips(sm->hal, chips, sizeof chips / sizeof chips[0]) ==
           sizeof chips / sizeof chips[0])
        ;
}

/*
 * Enough to hand the user a contact: we hold their whole record. This is what
 * decides COMPLETE versus ABORT when contact ends, because from the wearer's
 * point of view a card in the address book is the whole point.
 */
static bool have_their_record(const link_sm_t *sm)
{
    return frag_rx_complete(&sm->rx);
}

/*
 * Enough to stop early and stop transmitting. Stricter, and deliberately so:
 * an end that falls silent the moment it is personally satisfied strands the
 * other one halfway through a record. So we also wait until we have sent our
 * whole card at least once AND they have told us they received it.
 *
 * This cannot be made perfectly symmetric — whoever acknowledges last cannot
 * know their acknowledgement arrived, which is the two-army problem and has no
 * solution. What it can do is refuse to walk away early: silence is NOT taken
 * as consent here. An end that stops as soon as it is personally satisfied
 * strands the other one a fragment short, and in marginal conditions that was
 * costing a third of all handshakes.
 *
 * The bounds are elsewhere and they are real: two silent receive turns trigger
 * a redraw (see suspect_collision), the election gives up after
 * ELECT_MAX_REDRAWS, and the contact budget ends it regardless — reporting
 * COMPLETE if their record is in hand, because that is what the wearer sees.
 */
static bool we_are_done(const link_sm_t *sm)
{
    if (!have_their_record(sm)) return false;
    if (!sm->cfg.two_way) return true;
    /*
     * sent_ack is not redundant with peer_has_ours. Without it, the end that
     * finishes first completes the instant it hears their acknowledgement and
     * goes silent BEFORE ever sending one of its own — so the other end never
     * learns it can stop, redraws until the election gives up, and burns the
     * whole contact. Sending the last acknowledgement is the cheap half of the
     * two-army problem, and it is the half that is actually solvable.
     */
    return carousel_full_pass(&sm->car) && sm->peer_has_ours && sm->sent_ack;
}

static void queue_frame(link_sm_t *sm)
{
    const uint8_t idx = carousel_next(&sm->car);
    const uint8_t *payload = NULL;
    const size_t len = frag_get(sm->own, idx, &payload);
    frame_hdr_t h;

    h.frag_index = idx;
    h.frag_count = sm->own ? sm->own->count : 1u;
    h.record_id  = sm->own ? sm->own->record_id : 0u;
    h.flags      = (uint8_t)(have_their_record(sm) ? FRAME_FLAG_HAVE_YOURS : 0u);
    if (elect_role(&sm->elect) == ELECT_ROLE_TARGET) h.flags |= FRAME_FLAG_REPLY;

    sm->chips_len = frame_encode(&h, payload, len, sm->chips, sizeof sm->chips);

    hal_tx_drive(sm->hal, true);
    hal_tx_chips(sm->hal, sm->chips, sm->chips_len);
    if (h.flags & FRAME_FLAG_HAVE_YOURS) sm->sent_ack = true;
    sm->frames_sent++;
    sm->turn_frames++;
}

static void enter_turnaround(link_sm_t *sm, uint64_t now_us, uint32_t settle_us)
{
    /* GP2 to high-Z, not driven low: a driven pad still loads the shared
     * electrode the far end is listening on (design §6.3). */
    hal_tx_drive(sm->hal, false);
    sm->turnarounds++;
    sm->deadline_us = now_us + settle_us;
    sm->state = LINK_TURNAROUND;
}

static void enter_rx(link_sm_t *sm, uint64_t now_us)
{
    /* Discard whatever the framer half-collected while our own transmitter was
     * saturating the amplifier. */
    frame_rx_reset(&sm->framer);
    sm->turn_frames = 0;
    sm->rx_turn_frames = 0;
    sm->last_carrier_us = now_us;

    /*
     * The safety net: one turn's worth of frames, plus a frame of slack, plus
     * the idle gap. Long enough that it can never cut into the far end's last
     * frame, which is exactly the failure it replaced.
     */
    sm->deadline_us = now_us
        + (uint64_t)(sm->cfg.frames_per_turn + 1u)
          * (FRAME_AIRTIME_US + HANDOFF_TURNAROUND_US)
        + sm->cfg.rx_idle_us;

    sm->state = LINK_RX_FRAME;
}

/*
 * Take the channel. Goes through a turnaround twice as long as our own, so the
 * end that just stopped talking is certainly listening before our preamble
 * starts — it is still settling its own amplifier for the first millisecond,
 * and a preamble it is deaf to is a wasted frame.
 */
static void take_channel(link_sm_t *sm, uint64_t now_us)
{
    sm->turn_frames = 0;
    enter_turnaround(sm, now_us, 2u * HANDOFF_TURNAROUND_US);
}

/*
 * Two receive turns in a row with nothing heard at all. Three ways that
 * happens, and the same answer serves all three:
 *
 *  - contact broke
 *  - both ends elected themselves initiator, the tie listen-before-talk
 *    cannot catch, because each is deaf while its own transmitter drives the
 *    shared pad
 *  - the two fell into lockstep, transmitting and listening in step with each
 *    other, so neither ever hears the other despite both talking
 *
 * design §9.6's answer to all of them is the same: redraw and listen again.
 * The redraw is what breaks lockstep. The half-built record is kept — it is
 * still that person's card.
 */
static void suspect_collision(link_sm_t *sm, uint64_t now_us)
{
    hal_tx_drive(sm->hal, false);
    frame_rx_reset(&sm->framer);
    carrier_reset(&sm->carrier);
    sm->barren_turns = 0;

    elect_collision(&sm->elect, now_us);

    if (!elect_gave_up(&sm->elect)) { sm->state = LINK_BACKOFF; return; }

    /* Out of redraws. Still a successful handshake if their card is in hand. */
    sm->state = have_their_record(sm) ? LINK_COMPLETE : LINK_ABORT;
}

/*
 * IDLE is not a parked state. It runs the beacon cycle of beacon.h, which is
 * what actually starts a handshake on a wrist: there is no button and no touch
 * sensor, so the band advertises into the channel and listens for the same,
 * and hearing anything at all means a body has closed the loop.
 *
 * The carrier detector is fed ONLY in the listening phases. Feeding it while
 * our own amplifier is driving is the drain_discard() problem below, and here
 * it has a sharper edge: the band would wake on its own beacon, every period,
 * for ever. Its level and floor are deliberately NOT reset between windows —
 * the ambient floor of a room does not change in the 8 ms we are deaf, and
 * re-priming it against a beacon that is already on would hide that beacon.
 */
static void poll_idle(link_sm_t *sm, uint64_t now_us)
{
    const bool listening = beacon_listening(&sm->beacon);
    beacon_state_t bs;

    if (listening) drain_rx(sm, false); else drain_discard(sm);

    bs = beacon_poll(&sm->beacon, now_us,
                     listening && carrier_present(&sm->carrier));

    if (beacon_take_burst(&sm->beacon)) {
        sm->chips_len = beacon_fill(sm->chips, sizeof sm->chips);
        hal_tx_drive(sm->hal, true);
        hal_tx_chips(sm->hal, sm->chips, sm->chips_len);
    } else if (bs != BEACON_TX) {
        hal_tx_drive(sm->hal, false);
    }

    if (bs == BEACON_CONTACT) {
        link_sm_begin(sm, now_us);
        /*
         * The beacon already knows which end we are — see elect_assume(). It
         * is not merely faster than re-drawing: holding for the beacon to
         * clear releases both ends at the same instant, and a random draw is
         * at its worst exactly then.
         */
        elect_assume(&sm->elect, now_us, beacon_wake_role(&sm->beacon));
    }
}

link_state_t link_sm_poll(link_sm_t *sm, uint64_t now_us)
{
    if (sm->state == LINK_IDLE) { poll_idle(sm, now_us); return sm->state; }

    if (sm->state == LINK_COMPLETE || sm->state == LINK_ABORT)
        return sm->state;

    if (now_us - sm->started_us > sm->cfg.contact_budget_us) {
        hal_tx_drive(sm->hal, false);
        /* Their card is in hand: that is a successful handshake, whether or
         * not they ever got round to telling us they have ours. */
        sm->state = have_their_record(sm) ? LINK_COMPLETE : LINK_ABORT;
        return sm->state;
    }

    switch (sm->state) {
    case LINK_BACKOFF:
    case LINK_LISTEN: {
        /* Listen-before-talk needs the carrier detector but must not feed the
         * framer: a frame that starts before we have a role belongs to the
         * exchange, not to the election. */
        elect_state_t es;
        drain_rx(sm, false);
        es = elect_poll(&sm->elect, now_us, carrier_present(&sm->carrier));

        sm->state = (es == ELECT_BACKOFF) ? LINK_BACKOFF
                  : (es == ELECT_LISTEN)  ? LINK_LISTEN
                  : LINK_EXCHANGE;

        if (sm->state == LINK_EXCHANGE) {
            sm->turn_frames = 0;
            if (elect_role(&sm->elect) == ELECT_ROLE_INITIATOR) {
                sm->state = LINK_TX_FRAME;
                queue_frame(sm);
            } else {   /* target: listen first, design §9.6 */
                enter_rx(sm, now_us);
            }
        }
        break;
    }

    case LINK_TX_FRAME:
        drain_discard(sm);
        if (!hal_tx_busy(sm->hal))
            enter_turnaround(sm, now_us, HANDOFF_TURNAROUND_US);
        break;

    case LINK_TURNAROUND:
        /*
         * Deliberately deaf. design §9.7 allows 1 ms for the amplifier to come
         * out of saturation, and anything the framer sees in that window is
         * our own transmission decaying, not the far end starting.
         */
        drain_discard(sm);
        if (now_us >= sm->deadline_us) {
            if (sm->turn_frames >= sm->cfg.frames_per_turn) {
                if (we_are_done(sm)) { sm->state = LINK_COMPLETE; break; }
                enter_rx(sm, now_us);
            } else {
                sm->state = LINK_TX_FRAME;
                queue_frame(sm);
            }
        }
        break;

    case LINK_RX_FRAME: {
        const uint32_t got = drain_rx(sm, true);

        if (carrier_present(&sm->carrier)) sm->last_carrier_us = now_us;

        if (got) {
            sm->rx_turn_frames = (uint8_t)(sm->rx_turn_frames + got);
            sm->barren_turns = 0;
            if (we_are_done(sm)) { sm->state = LINK_COMPLETE; break; }
        }

        /*
         * The turn is theirs until one of three things happens:
         *
         *  - we have heard a full turn's worth of frames, so they are about to
         *    stop and listen for us
         *  - they have gone quiet for longer than an inter-frame gap
         *  - the safety net expires
         *
         * An earlier version restarted a fixed window on every frame received,
         * which meant a talkative initiator held the channel for the whole
         * contact and the target never sent its card at all.
         */
        if (sm->rx_turn_frames >= sm->cfg.frames_per_turn) {
            if (we_are_done(sm)) { sm->state = LINK_COMPLETE; break; }
            take_channel(sm, now_us);
            break;
        }

        if ((uint64_t)(now_us - sm->last_carrier_us) > sm->cfg.rx_idle_us ||
            now_us >= sm->deadline_us) {
            /* Count the barren turn BEFORE testing: "they have gone quiet" is
             * one of the two ways we_are_done() is allowed to conclude. */
            if (sm->rx_turn_frames == 0) sm->barren_turns++;

            if (we_are_done(sm)) { sm->state = LINK_COMPLETE; break; }

            if (sm->barren_turns >= 2u) { suspect_collision(sm, now_us); break; }
            take_channel(sm, now_us);
        }
        break;
    }

    default:
        break;
    }

    return sm->state;
}

elect_role_t link_sm_role(const link_sm_t *sm) { return elect_role(&sm->elect); }

size_t link_sm_received(const link_sm_t *sm, const uint8_t **blob)
{
    return frag_rx_blob(&sm->rx, blob);
}
