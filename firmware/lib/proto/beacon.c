#include "beacon.h"

#include <string.h>

/*
 * The listen duration, drawn fresh per cycle. Modulo bias across this range
 * from a 32-bit draw is about one part in 10^5 — far below anything that
 * matters — and a rejection loop would make the sequence length depend on the
 * values drawn, which is exactly what makes injected randomness hard to reason
 * about in a test.
 */
static uint32_t draw_listen(trig_t *t)
{
    const uint32_t span = HANDOFF_LISTEN_MAX_US - HANDOFF_LISTEN_MIN_US;
    return HANDOFF_LISTEN_MIN_US + (hal_random_u32(t->hal) % span);
}

static void enter_shout(trig_t *t, uint64_t now_us)
{
    t->state = TRIG_SHOUT;
    t->deadline_us = now_us + HANDOFF_SHOUT_US;
    t->burst_due = true;
    t->shouts++;
}

/*
 * Every listen window is a fresh draw. LISTEN runs off silent_left_us rather
 * than a deadline, because the whole point of the rule is that carrier time
 * does not count against it.
 */
static void enter_listen(trig_t *t)
{
    t->listen_us = draw_listen(t);
    t->silent_left_us = t->listen_us;
    t->state = TRIG_LISTEN;
}

static void enter_wait(trig_t *t, uint64_t now_us)
{
    t->state = TRIG_WAIT;
    t->deadline_us = now_us + HANDOFF_QUIET_WAIT_MAX_US;
    t->waits++;
    t->wait_started_us = now_us;
    /* Silence banked before the carrier appeared. Telemetry — see beacon.h. */
    t->last_silent_us = t->listen_us - t->silent_left_us;
}

/* Telemetry — how long the thing we heard lasted. See beacon.h. */
static void close_wait(trig_t *t, uint64_t now_us)
{
    t->last_wait_us = (uint32_t)(now_us - t->wait_started_us);
}

void trig_init(trig_t *t, const hal_iface_t *hal)
{
    memset(t, 0, sizeof *t);
    t->hal = hal;
    t->state = TRIG_OFF;
}

void trig_start(trig_t *t, uint64_t now_us)
{
    t->burst_due = false;
    t->reprime_due = false;
    t->last_poll_us = now_us;
    t->listen_us = 0;
    t->silent_left_us = 0;
    enter_shout(t, now_us);
}

void trig_stop(trig_t *t)
{
    t->state = TRIG_OFF;
    t->burst_due = false;
}

trig_state_t trig_poll(trig_t *t, uint64_t now_us, bool carrier_heard,
                       bool framer_locked)
{
    const uint64_t raw = now_us - t->last_poll_us;
    /* A caller that steps the clock backwards, or restarts one instance and
     * not the other, must not be able to underflow the silence budget. */
    const uint32_t elapsed = (now_us < t->last_poll_us) ? 0u
                           : (raw > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)raw;

    t->last_poll_us = now_us;

    switch (t->state) {
    case TRIG_SHOUT:
        if (now_us >= t->deadline_us) {
            t->state = TRIG_SETTLE;
            t->deadline_us = now_us + HANDOFF_TRIG_SETTLE_US;
        }
        break;

    case TRIG_SETTLE:
        /*
         * Deliberately deaf, exactly as LINK_TURNAROUND is: what the receive
         * path reports here is our own amplifier coming out of saturation, and
         * believing it would wake the band on its own shout every cycle.
         */
        if (now_us >= t->deadline_us) enter_listen(t);
        break;

    case TRIG_LISTEN:
        /*
         * Anything at all sends us to WAIT, where the question of what it is
         * gets answered. The silence budget is NOT charged for this interval —
         * that is the whole "counts silent time only" rule, and it is what
         * stops a band shouting over a card already in flight.
         */
        if (carrier_heard) { enter_wait(t, now_us); break; }

        if (elapsed >= t->silent_left_us) {
            t->silent_left_us = 0;
            enter_shout(t, now_us);
        } else {
            t->silent_left_us -= elapsed;
        }
        break;

    case TRIG_WAIT:
        /*
         * Which comes first decides it. A lock is checked before silence
         * because a framer that has taken a preamble and a marker is holding
         * the front of a real frame, and the carrier detector's hysteresis can
         * lapse for a chip inside one.
         */
        if (framer_locked) {
            close_wait(t, now_us);
            t->receives++;
            t->state = TRIG_RECEIVE;
            break;
        }
        if (!carrier_heard) {
            close_wait(t, now_us);
            /*
             * Was it long enough to have been a shout? Without this the band
             * cannot tell a peer from a 4 ms burst off the room, or from its
             * own amplifier, and on a real bench it elects itself sender every
             * cycle. See HANDOFF_SHOUT_MIN_US.
             *
             * A carrier too short to be a shout is not an event: go back to
             * listening with the silence budget UNTOUCHED. A fresh draw here
             * would take the band off the air — see the constant's comment.
             */
            if (t->last_wait_us < HANDOFF_SHOUT_MIN_US) {
                t->short_carriers++;
                t->state = TRIG_LISTEN;
                break;
            }
            t->sends++;
            t->state = TRIG_SEND;
            break;
        }
        if (now_us >= t->deadline_us) {
            /*
             * It never stopped. Do not send into a channel that is provably
             * busy — go back to listening with a fresh draw, and ask for the
             * carrier detector to be reset, because the most likely innocent
             * explanation is that the ambient floor has moved under it.
             *
             * The silence still owed from before the carrier appeared is
             * discarded rather than resumed. Resuming it would let a band that
             * had 2 ms left when the carrier arrived shout 2 ms after giving up
             * on it — straight into a channel it has just spent 30 ms watching
             * be busy. A fresh window buys at least LISTEN_MIN_US of looking
             * first, which is what this path is for.
             */
            close_wait(t, now_us);
            t->quiet_timeouts++;
            t->reprime_due = true;
            enter_listen(t);
        }
        break;

    case TRIG_OFF:
    case TRIG_SEND:
    case TRIG_RECEIVE:
    default:
        break;
    }
    return t->state;
}

bool trig_listening(const trig_t *t)
{
    /* WAIT listens too: it is waiting to see the carrier go away, or to see
     * the framer lock, and it cannot do either with its ears shut. */
    return t->state == TRIG_LISTEN || t->state == TRIG_WAIT;
}

bool trig_take_burst(trig_t *t)
{
    const bool due = t->burst_due;
    t->burst_due = false;
    return due;
}

bool trig_take_carrier_reprime(trig_t *t)
{
    const bool due = t->reprime_due;
    t->reprime_due = false;
    return due;
}

uint32_t trig_listen_us(const trig_t *t) { return t->listen_us; }

size_t trig_fill(uint8_t *chips, size_t max)
{
    const size_t n = (max < (size_t)TRIG_SHOUT_CHIPS) ? max
                                                      : (size_t)TRIG_SHOUT_CHIPS;
    /* All on. No transitions, so frame.c's preamble hunt cannot lock to it —
     * see the note in beacon.h. */
    memset(chips, 1, n);
    return n;
}
