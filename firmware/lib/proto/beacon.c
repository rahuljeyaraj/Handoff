#include "beacon.h"

#include <string.h>

/*
 * Next beacon slot: a fixed period plus a draw, for the collision case the
 * header describes. Same modulo-bias reasoning as elect.c's draw() — one part
 * in 10^4 across this range, and a rejection loop would make the sequence
 * length depend on the values drawn, which is what makes injected randomness
 * hard to reason about in a test.
 */
static void schedule_next(beacon_t *b, uint64_t now_us)
{
    const uint32_t jitter = hal_random_u32(b->hal) % (uint32_t)HANDOFF_BEACON_JITTER_US;
    b->next_tx_us = now_us + HANDOFF_BEACON_PERIOD_US + jitter;
}

static void enter_tx(beacon_t *b, uint64_t now_us)
{
    b->state = BEACON_TX;
    b->deadline_us = now_us + HANDOFF_BEACON_ON_US;
    b->burst_due = true;
    b->beacons++;
    schedule_next(b, now_us);
}

/*
 * Sniff, unless the next beacon slot is already due or would land inside the
 * window we are about to open. Checking the slot here rather than only in the
 * gap keeps the beacon period from being quantised up to a whole sniff cycle.
 */
static void enter_sniff_or_tx(beacon_t *b, uint64_t now_us)
{
    if (now_us >= b->next_tx_us) { enter_tx(b, now_us); return; }
    b->state = BEACON_SNIFF;
    b->deadline_us = now_us + HANDOFF_SNIFF_ON_US;
    b->sniffs++;
}

static void enter_hold(beacon_t *b, uint64_t now_us, elect_role_t role)
{
    /*
     * Recorded HERE, not on the way out of the hold: by then we have been
     * listening for a beacon to end and no longer know which window we were
     * in when it started.
     */
    b->wake_role = role;
    b->state = BEACON_HOLD;
    b->deadline_us = now_us + HANDOFF_BEACON_HOLD_MAX_US;
    b->holds++;
}

void beacon_init(beacon_t *b, const hal_iface_t *hal)
{
    memset(b, 0, sizeof *b);
    b->hal = hal;
    b->state = BEACON_OFF;
}

void beacon_start(beacon_t *b, uint64_t now_us)
{
    b->burst_due = false;
    b->wake_role = ELECT_ROLE_NONE;
    enter_tx(b, now_us);
}

void beacon_stop(beacon_t *b)
{
    b->state = BEACON_OFF;
    b->burst_due = false;
}

beacon_state_t beacon_poll(beacon_t *b, uint64_t now_us, bool carrier_heard)
{
    switch (b->state) {
    case BEACON_TX:
        if (now_us >= b->deadline_us) {
            b->state = BEACON_SETTLE;
            b->deadline_us = now_us + HANDOFF_TURNAROUND_US;
        }
        break;

    case BEACON_SETTLE:
        /*
         * Deliberately deaf, exactly as LINK_TURNAROUND is: what the receive
         * path reports here is our own amplifier coming out of saturation, and
         * believing it would wake the band on its own beacon every period.
         */
        if (now_us >= b->deadline_us) {
            b->state = BEACON_LISTEN;
            b->deadline_us = now_us + HANDOFF_BEACON_LISTEN_US;
        }
        break;

    case BEACON_LISTEN:
        /*
         * Our beacon was answered, so they are already transmitting a FRAME —
         * not a beacon. Wake now and start receiving: holding out for the
         * channel to go quiet would mean waiting out their whole 156 ms frame,
         * and the preamble we need to lock to is in its first 8 ms. Holding
         * here cost the first frame of every contact.
         */
        if (carrier_heard) {
            b->wake_role = ELECT_ROLE_TARGET;
            b->wakes++;
            b->state = BEACON_CONTACT;
            break;
        }
        if (now_us >= b->deadline_us) enter_sniff_or_tx(b, now_us);
        break;

    case BEACON_SNIFF:
        /*
         * Someone else's beacon, caught near its start. Let it finish before
         * answering — see the note in beacon.h — and once it clears they will
         * be listening for exactly this.
         */
        if (carrier_heard) { enter_hold(b, now_us, ELECT_ROLE_INITIATOR); break; }
        if (now_us >= b->deadline_us) {
            b->state = BEACON_GAP;
            b->deadline_us = now_us +
                (HANDOFF_SNIFF_PERIOD_US - HANDOFF_SNIFF_ON_US);
        }
        break;

    case BEACON_GAP:
        if (now_us >= b->next_tx_us)     { enter_tx(b, now_us); break; }
        if (now_us >= b->deadline_us)    enter_sniff_or_tx(b, now_us);
        break;

    case BEACON_HOLD:
        /*
         * Let them finish. Answering into the tail of a beacon costs the front
         * of our own preamble — see the note in beacon.h. carrier.c's
         * hysteresis puts this a couple of chips past the true end, which is
         * where we want it: by then the far end has finished its turnaround
         * and is listening for exactly this.
         */
        if (!carrier_heard || now_us >= b->deadline_us) {
            b->wakes++;
            b->state = BEACON_CONTACT;
        }
        break;

    case BEACON_OFF:
    case BEACON_CONTACT:
    default:
        break;
    }
    return b->state;
}

elect_role_t beacon_wake_role(const beacon_t *b) { return b->wake_role; }

bool beacon_listening(const beacon_t *b)
{
    /* HOLD listens too: it is waiting to see the carrier go away. */
    return b->state == BEACON_LISTEN || b->state == BEACON_SNIFF ||
           b->state == BEACON_HOLD;
}

bool beacon_take_burst(beacon_t *b)
{
    const bool due = b->burst_due;
    b->burst_due = false;
    return due;
}

size_t beacon_fill(uint8_t *chips, size_t max)
{
    const size_t n = (max < (size_t)BEACON_BURST_CHIPS) ? max
                                                        : (size_t)BEACON_BURST_CHIPS;
    /* All on. No transitions, so frame.c's preamble hunt cannot lock to it —
     * see the note in beacon.h. */
    memset(chips, 1, n);
    return n;
}
