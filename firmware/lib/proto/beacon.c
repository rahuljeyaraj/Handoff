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

/*
 * Draw a nonce we are willing to put on the wire.
 *
 * frame_beacon_nonce_ok() rejects the 39 values in 65536 whose own beacon
 * contains a card marker — see frame.h. Rejection sampling rather than a fix
 * to the format, because the nonce is the one field in this protocol whose
 * value is free: nothing reads it but an equality test.
 *
 * The loop is bounded rather than `while (!ok)`. It terminates with
 * probability 1 and it is not allowed to be the reason a band stops
 * beaconing, so after a handful of tries it goes out anyway: a 1-in-1700
 * nonce costs a rendezvous cycle, and an RNG stuck at one value is a much
 * bigger problem than that, which the self-echo counter will show.
 */
#define TRIG_NONCE_TRIES 8

static void draw_nonce(trig_t *t)
{
    unsigned tries;

    for (tries = 0; tries < TRIG_NONCE_TRIES; tries++) {
        t->nonce = (uint16_t)(hal_random_u32(t->hal) & 0xFFFFu);
        if (frame_beacon_nonce_ok(t->nonce)) break;
        t->nonce_rejects++;
    }
    t->nonce_stale = false;
}

/*
 * The redraw happens HERE, on the way into a beacon, and not when the echo
 * that asked for it arrived. beacon.h has the reason: our previous nonce has
 * to keep rejecting the stragglers of our previous beacon right up to the
 * moment we stop claiming it.
 */
static void enter_beacon(trig_t *t, uint64_t now_us)
{
    if (t->nonce_stale) { draw_nonce(t); t->redraws++; }
    t->state = TRIG_BEACON;
    t->deadline_us = now_us + HANDOFF_BEACON_AIRTIME_US;
    t->burst_due = true;
    t->beacons++;
}

/*
 * Every listen window is a fresh draw. LISTEN runs off silent_left_us rather
 * than a deadline, because the whole point of beacon.h §3 is that carrier time
 * does not count against it.
 */
static void enter_listen(trig_t *t)
{
    t->listen_us = draw_listen(t);
    t->silent_left_us = t->listen_us;
    t->state = TRIG_LISTEN;
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
    t->last_poll_us = now_us;
    t->listen_us = 0;
    t->silent_left_us = 0;
    draw_nonce(t);
    enter_beacon(t, now_us);
}

void trig_stop(trig_t *t)
{
    t->state = TRIG_OFF;
    t->burst_due = false;
}

trig_state_t trig_poll(trig_t *t, uint64_t now_us, const trig_in_t *in)
{
    const uint64_t raw = now_us - t->last_poll_us;
    /* A caller that steps the clock backwards, or restarts one instance and
     * not the other, must not be able to underflow the silence budget. */
    const uint32_t elapsed = (now_us < t->last_poll_us) ? 0u
                           : (raw > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)raw;

    t->last_poll_us = now_us;

    switch (t->state) {
    case TRIG_BEACON:
        /*
         * BOTH conditions, and neither is redundant.
         *
         * The airtime alone is not enough: the caller queues the chips AFTER
         * this function returns, and hal_tx_chips() then spends about a
         * millisecond packing them before the DMA starts (hal_pico.h puts a
         * 624-chip card frame at 5.4 ms). So the pad is still driving when the
         * airtime is up, and a settle started here would be a millisecond
         * short at the end where it matters.
         *
         * hal_tx_busy() alone is not enough either: on the first poll after
         * enter_beacon() nothing has been queued yet, so it reads false and
         * the beacon would end before it began. The deadline is what
         * guarantees the queue happened.
         */
        if (now_us >= t->deadline_us && !hal_tx_busy(t->hal)) {
            t->state = TRIG_SETTLE;
            t->deadline_us = now_us + HANDOFF_TRIG_SETTLE_US;
        }
        break;

    case TRIG_SETTLE:
        /*
         * Deliberately deaf, exactly as LINK_TURNAROUND is: what the receive
         * path reports here is our own amplifier coming out of saturation.
         */
        if (now_us >= t->deadline_us) enter_listen(t);
        break;

    case TRIG_LISTEN:
        /*
         * A card already arriving beats everything: the peer decoded our
         * beacon, elected itself sender, and is mid-preamble. Checked first
         * because it is the one input that says the rendezvous is already
         * over.
         */
        if (in->card) {
            t->receives++;
            t->state = TRIG_RECEIVE;
            break;
        }

        if (in->beacon) {
            if (in->nonce == t->nonce) {
                /*
                 * Our own beacon coming back at us, or — once in 2^16 — a
                 * peer that drew the same sixteen bits. The two are
                 * indistinguishable and want the same answer, which is the
                 * neat part: do not send, and go out next time under a
                 * different name. beacon.h §2.
                 */
                t->self_echoes++;
                t->nonce_stale = true;
            } else {
                /*
                 * Somebody else, and the CRC says so rather than a stopwatch.
                 * §2: decoding a peer means we had not yet started our own
                 * beacon this cycle, so the peer has nothing of ours to
                 * decode, so nobody else is about to send. The channel is
                 * ours.
                 */
                t->peers++;
                t->sends++;
                t->state = TRIG_SEND;
            }
            break;
        }

        /*
         * §3: the silence budget is NOT charged while the channel is busy.
         * Listen before talk, and the one consumer design §6 gives presence.
         */
        if (in->busy) break;

        if (elapsed >= t->silent_left_us) {
            t->silent_left_us = 0;
            enter_beacon(t, now_us);
        } else {
            t->silent_left_us -= elapsed;
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
    return t->state == TRIG_LISTEN;
}

bool trig_take_burst(trig_t *t)
{
    const bool due = t->burst_due;
    t->burst_due = false;
    return due;
}

uint32_t trig_listen_us(const trig_t *t) { return t->listen_us; }
uint16_t trig_nonce(const trig_t *t)     { return t->nonce; }

size_t trig_fill(const trig_t *t, uint8_t *chips, size_t max)
{
    return frame_beacon_encode(t->nonce, chips, max);
}
