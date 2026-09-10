/*
 * Handoff — role election. design §9.6, architecture §7.3.
 *
 * Listen before talk. Draw 0..HANDOFF_BACKOFF_MAX_US, wait, then listen for a
 * carrier: heard means the other end went first, so become TARGET; silence
 * means become INITIATOR. Ties — both ends drawing close enough that neither
 * hears the other — are broken by redraw.
 *
 * The draw comes from hal->random, so a test forces a collision directly
 * instead of waiting for one. §7.3's tie-and-redraw path is otherwise very
 * nearly untestable, and it is exactly the path that will bite on a demo day.
 *
 * design §9.6's rejection of burned-in priority IDs stands: body coupling has
 * no dominant/recessive state, so there is no collision detection to arbitrate
 * with and the lowest-numbered device would starve the rest.
 */
#ifndef HANDOFF_ELECT_H
#define HANDOFF_ELECT_H

#include <stdbool.h>
#include <stdint.h>

#include "hal.h"

typedef enum {
    ELECT_IDLE = 0,
    ELECT_BACKOFF,   /* waiting out the draw            */
    ELECT_LISTEN,    /* measuring carrier               */
    ELECT_SETTLED
} elect_state_t;

typedef enum {
    ELECT_ROLE_NONE = 0,
    ELECT_ROLE_INITIATOR,
    ELECT_ROLE_TARGET
} elect_role_t;

/*
 * Listen window after the backoff expires. It only has to outlast the carrier
 * detector's latency below — NOT a whole preamble, which at HANDOFF_GZ_N 25 is
 * 32 chips and 8 ms, longer than the entire backoff range. Carrier presence is
 * an energy question and answerable in a few chips; nothing here needs to
 * decode anything.
 */
#define ELECT_LISTEN_US   2000

/*
 * How long carrier_t takes to raise its flag once a carrier appears — about
 * four chips at the default fast EMA. It matters because it, not the listen
 * window, sets the collision rate: two ends whose draws land within this of
 * each other both start transmitting before either can hear the other.
 *
 * The first-attempt collision rate is then 1 - (1 - detect/backoff_max)^2,
 * which is why HANDOFF_BACKOFF_MAX_US is derived from this rather than fixed
 * at design §9.6's flat 5 ms. See the note in config.h. test_elect pins the
 * resulting rate, so it stays a measured property rather than a lucky one.
 */
#define ELECT_DETECT_US   HANDOFF_DETECT_US

/*
 * At roughly a one-in-three collision per attempt (see above), eight redraws
 * leave a 1-in-10000 chance of a handshake that never elects a role — visible
 * over a demo afternoon. Sixteen takes it to 1 in 30 million, and costs at
 * worst 16 * (5 ms + 2 ms) = 112 ms, comfortably inside R1's one second.
 */
#define ELECT_MAX_REDRAWS 16

typedef struct {
    const hal_iface_t *hal;
    elect_state_t state;
    elect_role_t  role;
    uint64_t      deadline_us;
    uint32_t      draw_us;
    uint8_t       redraws;
    bool          gave_up;
} elect_t;

void          elect_init(elect_t *e, const hal_iface_t *hal);
void          elect_start(elect_t *e, uint64_t now_us);

/*
 * Start from a role the caller already knows, instead of from a draw.
 *
 * beacon.c knows it: a band that woke in its own post-beacon listen was
 * answered and is the TARGET, and a band that woke in a sniff heard someone
 * else's beacon and is the INITIATOR. Those two cases cannot both happen to
 * both ends, because a band is deaf while its own beacon is playing — so the
 * wake reason is an asymmetry the draw would only be re-deriving.
 *
 * It is not re-derived for free, either. Waiting for the beacon to clear
 * releases both ends at the SAME instant, which is the worst case for a random
 * draw and made election ties markedly more likely than the 12 % §7.3 sizes
 * for. Taking the role that is already known removes the tie instead of
 * re-rolling it.
 *
 * TARGET settles immediately. INITIATOR still listens before talking, because
 * a hint is not a measurement: if the channel turns out to be busy it becomes
 * the target after all, exactly as a drawn election would.
 */
void          elect_assume(elect_t *e, uint64_t now_us, elect_role_t role);

/* Drive the election. carrier_heard is the listen-before-talk observation for
 * this instant, normally hal_rx_carrier_level() against a threshold. */
elect_state_t elect_poll(elect_t *e, uint64_t now_us, bool carrier_heard);

/*
 * Called when we transmitted and then discovered the other end was
 * transmitting too — the tie that listen-before-talk did not catch, because
 * both draws landed inside each other's turn-on time. Redraws and restarts.
 */
void          elect_collision(elect_t *e, uint64_t now_us);

elect_role_t  elect_role(const elect_t *e);
uint32_t      elect_draw_us(const elect_t *e);
bool          elect_gave_up(const elect_t *e);

#endif /* HANDOFF_ELECT_H */
