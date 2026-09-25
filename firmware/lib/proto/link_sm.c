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
    frame_rx_init(&sm->framer);
    frag_rx_init(&sm->rx);
    carousel_init(&sm->car, own ? own->count : 1u, sm->cfg.carousel_weight);
    sm->state = LINK_IDLE;
    sm->role = LINK_ROLE_NONE;
}

/* ---- forward declarations, so the entry points can read top-down -------- */

static void queue_frame(link_sm_t *sm);
static void forget_rx_busy(link_sm_t *sm);
static void enter_rx(link_sm_t *sm, uint64_t now_us, bool reset_framer);
static void enter_turnaround(link_sm_t *sm, uint64_t now_us, uint32_t settle_us);

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
 * THIS USED TO BE THE LONGEST COMMENT IN THE FILE, and link v2 step 5 deleted
 * what it was about. It argued whether the carrier detector should be reset on
 * each path, because v1's floor arrived here having spent a whole listen
 * window averaging either silence or a peer's full-power shout, and a floor
 * several times ambient made real frames fail the presence test. Resetting it
 * was worse still: carrier.c re-primed from the very next chip, and landing
 * that on a Manchester HIGH primed the floor at the carrier's own level, where
 * the detector never regained presence for the rest of the frame.
 *
 * Neither hazard exists now. v2's noise reference is three bins the signal
 * cannot enter, measured in the same windows as the signal, so a listen window
 * full of a peer's shout leaves it exactly where it was. There is nothing to
 * reset on either path, and both paths are the same.
 *
 * The measurement that settled the old question is worth keeping, because it
 * is what said the question did not matter end to end: over 60 triggered
 * handshakes and 50 host-triggered ones, reset and no-reset gave bit-identical
 * counts — 482 frames sent, 663 turnarounds, 0 polls with both ends clocking
 * out a frame — because handover during a receive turn is driven by counting
 * decoded frames and the framer is untouched either way.
 *
 * The framer is still not reset on the RECEIVER path, and that one is not a
 * preference — the lock IS the reason we are here.
 */
/*
 * from_trigger says the role came from TRIG_SEND — we decoded somebody's
 * beacon and the channel is ours. That matters for one reason only, and it is
 * the same reason take_channel() exists: THE PEER IS STILL DEAF.
 *
 * Step 7 made that TIGHTER, not looser. We decode a beacon on its very last
 * chip, which is the instant the peer stops driving and enters
 * HANDOFF_TRIG_SETTLE_US of its own amplifier recovering — so our decision now
 * lands at the START of the peer's deaf window rather than a detector's hold
 * after it. Sending a preamble immediately would put it squarely inside a
 * window the peer cannot hear, and a preamble missed is not merely a lost
 * frame, because frame.c's hunt locks on the preamble's start and cannot join
 * one in progress. The peer would hear a long carrier it could never decode,
 * beacon again, and the pair would never rendezvous at all.
 *
 * This went unnoticed while the settle was HANDOFF_TURNAROUND_US: at 1 ms the
 * peer's ears happened to open before the preamble by luck. The phase sweep in
 * test_beacon.c fails from 3 ms up, which is exactly where the luck runs out.
 */
static void enter_exchange(link_sm_t *sm, uint64_t now_us, link_role_t role,
                           bool from_trigger)
{
    sm->role = role;

    if (role == LINK_ROLE_SENDER) {
        frame_rx_init(&sm->framer);
        sm->turn_frames = 0;
        if (from_trigger) {
            /* Their settle, plus our own amplifier's, so the preamble starts
             * after their ears are certainly open. */
            enter_turnaround(sm, now_us,
                             HANDOFF_TRIG_SETTLE_US + HANDOFF_TURNAROUND_US);
            return;
        }
        sm->state = LINK_TX_FRAME;
        queue_frame(sm);
        return;
    }

    enter_rx(sm, now_us, false);
}

void link_sm_begin(link_sm_t *sm, uint64_t now_us, link_role_t role)
{
    frame_rx_init(&sm->framer);
    trig_stop(&sm->trig);
    open_contact(sm, now_us);
    /* The caller decided the roles itself, so there is no peer coming out of a
     * beacon to wait for — see enter_exchange(). */
    enter_exchange(sm, now_us, role, false);
}

void link_sm_idle(link_sm_t *sm, uint64_t now_us)
{
    hal_tx_drive(sm->hal, false);
    frame_rx_init(&sm->framer);
    forget_rx_busy(sm);             /* what we heard last was us */

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
    trig_start(&sm->trig, now_us);
    sm->state = LINK_IDLE;
}

/*
 * Drain whatever the DSP layer has produced since the last poll and run it
 * through the framer. Returns how many good CARD frames landed, and leaves
 * the last one in sm->framer. A beacon is reported through trig_in, which is
 * NULL everywhere but IDLE.
 *
 * Presence is NOT drained here. It is decided on core 1 out of the five-bin
 * bank, not out of these chips, and the callers that want it ask
 * hal_rx_busy() where they want it — which is not the same set of places.
 */
static uint32_t drain_rx(link_sm_t *sm, bool feed_framer, trig_in_t *trig_in)
{
    int32_t chips[64];
    uint32_t good = 0;
    size_t n, i;

    while ((n = hal_rx_chips(sm->hal, chips, sizeof chips / sizeof chips[0])) > 0) {
        for (i = 0; i < n; i++) {
            if (!feed_framer) continue;

            switch (frame_rx_push(&sm->framer, chips[i])) {
            case FRAME_RX_BEACON:
                /*
                 * Somebody's rendezvous beacon. Only IDLE has anywhere to put
                 * one — inside an exchange the two ends are talking cards and
                 * a beacon is a third band, or a straggler, and either way
                 * the carousel is not interested.
                 */
                if (trig_in) {
                    trig_in->beacon = true;
                    trig_in->nonce = frame_rx_nonce(&sm->framer);
                }
                break;
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
 * transmitter is driving the shared pad, and while the amplifier is coming
 * back out of saturation afterwards: what core 1 reports then is us.
 *
 * THE BUSY LATCH IS DRAINED HERE TOO, AND IT HAS TO BE. hal.h's rx_busy is
 * sticky until read, so our own transmission raises it and it would still be
 * up when the ears open — the band would hear itself, every turn. v1 had the
 * same hazard in a different shape and answered it the same way, by not
 * feeding the detector while driving. Here the detector runs regardless, on
 * core 1, so the discard has to happen on the reading side instead.
 */
static void drain_discard(link_sm_t *sm)
{
    int32_t chips[64];
    while (hal_rx_chips(sm->hal, chips, sizeof chips / sizeof chips[0]) ==
           sizeof chips / sizeof chips[0])
        ;
    forget_rx_busy(sm);
}

/*
 * ---- THE OOK BRIDGE IS GONE, AND THIS IS WHY IT WAS HERE ----------------
 *
 * Until step 6 this file held LINK_OOK_BRIDGE_US: the channel was treated as
 * occupied for MANCHESTER_MAX_RUN_CHIPS + 1 chips after the last busy
 * reading. It existed because v1 SWITCHED THE CARRIER OFF FOR A ZERO, so half
 * of every frame was silence and dsp/presence.c — which has no hold and no
 * memory, by design — answered "nobody is transmitting" in each of those
 * gaps, truthfully. Measured in this simulator: a band listening to a v1
 * frame flapped busy/quiet at the chip rate and no rendezvous completed at
 * any phase.
 *
 * FSK HAS NO SPACES. The pad carries tone A or tone B and never nothing, so
 * presence reads busy for every chip of a frame and there is no gap left to
 * bridge. Nothing exercises it, and a bridge nothing exercises is machinery
 * this branch exists to remove. Design link-v2 §4's second "free" benefit of
 * a constant envelope, arriving as a deletion.
 *
 * WHAT REPLACED IT IS NOTHING. Not a shorter hold, not a hysteresis — the
 * detector's own verdict, which is what §6 says presence is.
 */

/*
 * Presence, taken once.
 *
 * Still a named wrapper, for the half of the old reason that survives:
 * hal_rx_busy() CLEARS its latch (hal.h), so two callers in one poll leave
 * the second one reading a quiet channel. Each poll reads it exactly once,
 * early, and passes the answer around.
 */
static bool drain_rx_busy(link_sm_t *sm, uint64_t now_us)
{
    (void)now_us;
    return hal_rx_busy(sm->hal);
}

/*
 * Forget that the channel was busy. For the paths that have just stopped
 * driving the pad, or are about to open their ears after being deliberately
 * deaf: what they heard last was themselves. drain_discard() is the usual
 * way in.
 */
static void forget_rx_busy(link_sm_t *sm)
{
    (void)hal_rx_busy(sm->hal);
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
    sm->last_busy_us = now_us;

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
    forget_rx_busy(sm);             /* what we heard last was us */
    sm->barren_turns = 0;
    sm->chips_len = 0;

    if (++sm->retries > LINK_MAX_RETRIES) {
        /* Out of retries. Still a successful handshake if their card is in
         * hand — that is what the wearer sees. */
        sm->state = have_their_record(sm) ? LINK_COMPLETE : LINK_ABORT;
        return;
    }

    sm->role = LINK_ROLE_NONE;
    trig_start(&sm->trig, now_us);
    sm->state = LINK_IDLE;
}

/*
 * IDLE is not a parked state. It runs the trigger of beacon.h, which is what
 * actually starts a handshake on a wrist: there is no button and no touch
 * sensor, so the band beacons into the channel and listens for the same, and
 * decoding a beacon means a body has closed the loop.
 *
 * The framer is fed, and the busy latch believed, ONLY in the listening
 * phase. Reading either while our own amplifier is driving is the
 * drain_discard() problem, and here it used to have a sharper edge: under v1
 * the band would trigger on its own shout, every cycle, for ever. It cannot
 * now — a beacon of ours that survives the discard carries our own nonce and
 * beacon.c throws it away — but the discard stays, because a framer chewing
 * on our own amplifier is wasted work and one more way to reach a false sync.
 *
 * Under link v2 there is nothing to prime, nothing to reprime and nothing to
 * hold across a cycle. The noise reference lives on core 1, in three bins our
 * transmitter cannot enter, and it keeps running through our own beacon
 * because our own beacon is not in it.
 */
static void poll_idle(link_sm_t *sm, uint64_t now_us)
{
    const bool listening = trig_listening(&sm->trig);
    trig_in_t in;
    trig_state_t ts;

    memset(&in, 0, sizeof in);

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
        const uint32_t syncs_before = sm->framer.syncs;

        in.busy = drain_rx_busy(sm, now_us);
        drain_rx(sm, true, &in);

        /*
         * A CARD sync, not a beacon one — frame.c counts the two separately
         * for exactly this. It means the peer decoded our beacon, elected
         * itself sender, and is already clocking a fragment at us.
         *
         * v1 believed a sync only while TRIG_WAIT was open, on the grounds
         * that noise can drag the framer through a false marker during a long
         * listen and a band that should send would then wait for a frame
         * nobody is sending. There is no TRIG_WAIT any more, so this is
         * believed whenever the ears are open, and the exposure is what
         * frame.h computes: one false sync per 41 hours across BOTH tails, so
         * about one per 82 hours for this one. It costs a receive turn and two
         * barren turns, and suspect_collision() puts the band back in the
         * trigger. Once every few days, for a few hundred milliseconds.
         */
        in.card = (sm->framer.syncs != syncs_before);
    } else {
        drain_discard(sm);
        /* A framer half-way through a hunt on our own amplifier is worse than
         * no framer at all. */
        frame_rx_reset(&sm->framer);
    }

    ts = trig_poll(&sm->trig, now_us, &in);

    if (trig_take_burst(&sm->trig)) {
        sm->chips_len = trig_fill(&sm->trig, sm->chips, sizeof sm->chips);
        hal_tx_drive(sm->hal, true);
        hal_tx_chips(sm->hal, sm->chips, sm->chips_len);
    } else if (ts != TRIG_BEACON) {
        hal_tx_drive(sm->hal, false);
    }

    if (ts == TRIG_SEND || ts == TRIG_RECEIVE) {
        trig_stop(&sm->trig);
        if (!sm->exchange_open) open_contact(sm, now_us);
        enter_exchange(sm, now_us,
                       ts == TRIG_SEND ? LINK_ROLE_SENDER : LINK_ROLE_RECEIVER,
                       true);
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
        /* Before drain_rx(), because both read the HAL and only one of them
         * clears the latch. */
        const bool     busy = drain_rx_busy(sm, now_us);
        const uint32_t got  = drain_rx(sm, true, NULL);

        /*
         * A frame in progress keeps the turn alive on its own account.
         *
         * v1 needed that because its detector could not do the job: the floor
         * tracked up to meet a carrier lasting a whole frame, so it reported
         * silence partway through every one and the turn was handed back over
         * the top of the frame it was waiting for. v2's noise reference cannot
         * be pulled up by the signal, so presence should hold for the whole
         * frame — but frame_rx_busy() stays, because it is the stronger of the
         * two statements and costs nothing. See frame_rx_busy().
         */
        if (busy || frame_rx_busy(&sm->framer))
            sm->last_busy_us = now_us;

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

        if ((uint64_t)(now_us - sm->last_busy_us) > sm->cfg.rx_idle_us ||
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
