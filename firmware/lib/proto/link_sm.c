#include "link_sm.h"

#include <string.h>

static const char *const k_names[LINK_STATE_COUNT] = {
    "IDLE", "TX_FRAME", "TURNAROUND", "RX_FRAME", "EXCHANGE", "COMPLETE", "ABORT"
};

const char *link_state_name(link_state_t s)
{
    return (s < LINK_STATE_COUNT) ? k_names[s] : "?";
}

void link_cfg_default(link_cfg_t *c)
{
    c->frames_per_turn   = 1;
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

    trig_init(&sm->trig, hal);
    carrier_init(&sm->carrier);
    frame_rx_init(&sm->framer);
    frag_rx_init(&sm->rx);
    carousel_init(&sm->car, own ? own->count : 1u, sm->cfg.carousel_weight);
    sm->state = LINK_IDLE;
    sm->role = LINK_ROLE_NONE;
}

/* ---- forward declarations, so the entry points can read top-down -------- */

static void queue_frame(link_sm_t *sm);
static void enter_rx(link_sm_t *sm, uint64_t now_us, bool reset_framer);

/*
 * A new contact begins here: the previous person's record is dropped and the
 * budget restarts. Deliberately NOT called when the exchange falls back into
 * the trigger to recover from a collision — that keeps what it has, which is
 * the whole of architecture §8.4's argument.
 */
static void open_contact(link_sm_t *sm, uint64_t now_us)
{
    frag_rx_init(&sm->rx);
    carousel_init(&sm->car, sm->own ? sm->own->count : 1u, sm->cfg.carousel_weight);

    sm->started_us = now_us;
    sm->turn_frames = 0;
    sm->rx_turn_frames = 0;
    sm->barren_turns = 0;
    sm->retries = 0;
    sm->peer_has_ours = false;
    sm->sent_ack = false;
    sm->chips_len = 0;
    sm->exchange_open = true;
}

/*
 * Take up the role the trigger handed out and start the exchange.
 *
 * THE CARRIER DETECTOR. A band arriving here has spent its whole listen window
 * feeding either silence or a peer's full-power shout into the floor EMA, so
 * the floor is not necessarily anywhere near true ambient. Carried into the
 * exchange, a floor several times ambient makes real frames fail the presence
 * test, and handover — which runs on carrier_present() and last_carrier_us —
 * starts talking over the reply it asked for.
 *
 * On the SENDER path it is reset, and unlike the design this replaces, there
 * is nothing for the reset to blind: no listen-before-talk follows it. Strictly
 * simpler than it was.
 *
 * On the RECEIVER path it is NOT reset, and that was the open question in
 * §5.1, which asked for a test rather than an argument. Both were built and
 * measured, over 60 triggered handshakes and 50 host-triggered ones:
 *
 *                  frames sent   turnarounds   polls with both ends
 *                                              clocking out a frame
 *   no reset           482           663              0
 *   reset              482           663              0
 *
 * Bit-identical, because handover during a receive turn is driven by counting
 * decoded frames and the framer is untouched either way. So end to end the
 * reset buys nothing — and one layer down it costs something real:
 *
 *   carrier.c re-primes level and floor from the very next chip it is given.
 *   Land that on a LOW Manchester chip and presence returns one chip later.
 *   Land it on a HIGH one and the floor primes at the carrier's own level,
 *   where the >>7 floor EMA falls about two LSB per chip pair — measured, the
 *   detector never regains presence for the whole remaining 624-chip frame.
 *
 * Which chip it lands on is a coin flip. carrier_present() and last_carrier_us
 * are what drive handover, so half the time the reset would blind the thing
 * deciding whose turn it is, for the rest of the frame, to buy nothing. It is
 * therefore not done. test_beacon.c pins the asymmetry so a future change to
 * carrier.c's floor cannot quietly make this the wrong answer.
 *
 * The framer is not reset on this path either, and that one is not a
 * preference — the lock IS the reason we are here.
 */
static void enter_exchange(link_sm_t *sm, uint64_t now_us, link_role_t role)
{
    sm->role = role;

    if (role == LINK_ROLE_SENDER) {
        frame_rx_init(&sm->framer);
        carrier_reset(&sm->carrier);
        sm->turn_frames = 0;
        sm->state = LINK_TX_FRAME;
        queue_frame(sm);
        return;
    }

    enter_rx(sm, now_us, false);
}

void link_sm_begin(link_sm_t *sm, uint64_t now_us, link_role_t role)
{
    /* The carrier detector is deliberately NOT reset here — enter_exchange()
     * owns that decision and it is not the same on both paths. */
    frame_rx_init(&sm->framer);
    trig_stop(&sm->trig);
    open_contact(sm, now_us);
    enter_exchange(sm, now_us, role);
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
     * idle would discard exactly what the wearer is about to be shown. It goes
     * when the NEXT contact opens, in open_contact().
     */
    sm->chips_len = 0;
    sm->role = LINK_ROLE_NONE;
    sm->exchange_open = false;
    sm->idle_syncs = sm->framer.syncs;
    trig_start(&sm->trig, now_us);
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
 * The bounds are elsewhere and they are real: two silent receive turns send the
 * exchange back through the trigger (see suspect_collision), that is capped at
 * LINK_MAX_RETRIES, and the contact budget ends it regardless — reporting
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
     * learns it can stop, retries until it runs out, and burns the whole
     * contact. Sending the last acknowledgement is the cheap half of the
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
    if (sm->role == LINK_ROLE_RECEIVER) h.flags |= FRAME_FLAG_REPLY;

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

/*
 * reset_framer is false on exactly one path: arriving from TRIG_RECEIVE, where
 * the framer is mid-frame and that is the point. Everywhere else it discards
 * whatever the framer half-collected while our own transmitter was saturating
 * the amplifier.
 */
static void enter_rx(link_sm_t *sm, uint64_t now_us, bool reset_framer)
{
    if (reset_framer) frame_rx_reset(&sm->framer);
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
 * Two receive turns in a row with nothing heard at all. Two ways that happens
 * now that the roles cannot both land the same way round:
 *
 *  - contact broke
 *  - the two fell into lockstep, transmitting and listening in step with each
 *    other, so neither ever hears the other despite both talking
 *
 * The answer to both is to stop talking and go back to the trigger, which is
 * the one thing in the system that can tell the two ends apart. It draws a
 * fresh listen window, so lockstep cannot survive it. The half-built record is
 * kept — it is still that person's card — and so is the contact budget, which
 * is what actually bounds this.
 *
 * The retry count bounds it a second time: a pair wedged in a way the trigger
 * cannot fix terminates in COMPLETE or ABORT rather than looping until the
 * budget runs out with nothing reported.
 */
static void suspect_collision(link_sm_t *sm, uint64_t now_us)
{
    hal_tx_drive(sm->hal, false);
    frame_rx_reset(&sm->framer);
    carrier_reset(&sm->carrier);
    sm->barren_turns = 0;
    sm->chips_len = 0;

    if (++sm->retries > LINK_MAX_RETRIES) {
        /* Out of retries. Still a successful handshake if their card is in
         * hand — that is what the wearer sees. */
        sm->state = have_their_record(sm) ? LINK_COMPLETE : LINK_ABORT;
        return;
    }

    sm->role = LINK_ROLE_NONE;
    sm->idle_syncs = sm->framer.syncs;
    trig_start(&sm->trig, now_us);
    sm->state = LINK_IDLE;
}

/*
 * IDLE is not a parked state. It runs the trigger of beacon.h, which is what
 * actually starts a handshake on a wrist: there is no button and no touch
 * sensor, so the band shouts into the channel and listens for the same, and
 * hearing anything at all means a body has closed the loop.
 *
 * The carrier detector and the framer are fed ONLY in the listening phases.
 * Feeding them while our own amplifier is driving is the drain_discard()
 * problem below, and here it has a sharper edge: the band would trigger on its
 * own shout, every cycle, for ever. The detector's level and floor are
 * deliberately NOT reset between cycles — the ambient floor of a room does not
 * change in the 11 ms we are deaf, and re-priming it against a shout that is
 * already on would hide that shout.
 */
static void poll_idle(link_sm_t *sm, uint64_t now_us)
{
    const bool listening = trig_listening(&sm->trig);
    const bool waiting   = (sm->trig.state == TRIG_WAIT);
    bool locked;
    trig_state_t ts;

    /*
     * A retry is still inside the same contact, so the budget still applies —
     * and link_sm_poll() does not check it here, because a band idling on a
     * shelf between handshakes has no budget to run out of.
     */
    if (sm->exchange_open &&
        (uint64_t)(now_us - sm->started_us) > sm->cfg.contact_budget_us) {
        hal_tx_drive(sm->hal, false);
        trig_stop(&sm->trig);
        sm->state = have_their_record(sm) ? LINK_COMPLETE : LINK_ABORT;
        return;
    }

    if (listening) {
        drain_rx(sm, true);
    } else {
        drain_discard(sm);
        /* A framer half-way through a hunt on our own amplifier is worse than
         * no framer at all. */
        frame_rx_reset(&sm->framer);
    }

    /*
     * Only a sync that happened while we were ALREADY waiting answers the
     * question TRIG_WAIT is asking. Noise can drag the framer through a false
     * marker during a long listen — rarely, but this runs continuously — and
     * reading that stale lock as "a card is arriving" would turn a band that
     * should send into one that waits for a frame nobody is sending. Outside
     * WAIT the baseline just follows, so such a sync is absorbed rather than
     * remembered.
     */
    locked = waiting && (sm->framer.syncs != sm->idle_syncs);
    if (!waiting) sm->idle_syncs = sm->framer.syncs;

    ts = trig_poll(&sm->trig, now_us,
                   listening && carrier_present(&sm->carrier), locked);

    /* §4.3: the quiet-wait cap expired, so the floor may genuinely have moved
     * under the detector. Re-prime it rather than stay deaf to a real peer. */
    if (trig_take_carrier_reset(&sm->trig)) carrier_reset(&sm->carrier);

    if (trig_take_burst(&sm->trig)) {
        sm->chips_len = trig_fill(sm->chips, sizeof sm->chips);
        hal_tx_drive(sm->hal, true);
        hal_tx_chips(sm->hal, sm->chips, sm->chips_len);
    } else if (ts != TRIG_SHOUT) {
        hal_tx_drive(sm->hal, false);
    }

    if (ts == TRIG_SEND || ts == TRIG_RECEIVE) {
        trig_stop(&sm->trig);
        if (!sm->exchange_open) open_contact(sm, now_us);
        enter_exchange(sm, now_us,
                       ts == TRIG_SEND ? LINK_ROLE_SENDER : LINK_ROLE_RECEIVER);
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
                enter_rx(sm, now_us, true);
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

link_role_t link_sm_role(const link_sm_t *sm) { return sm->role; }

size_t link_sm_received(const link_sm_t *sm, const uint8_t **blob)
{
    return frag_rx_blob(&sm->rx, blob);
}
