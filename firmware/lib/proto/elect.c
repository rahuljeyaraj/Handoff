#include "elect.h"

static uint32_t draw(elect_t *e)
{
    /*
     * Modulo bias across a 5000 us range from a 32-bit draw is about one part
     * in 10^6 — far below anything that matters here, and a rejection loop
     * would make the sequence length depend on the values drawn, which is
     * exactly what makes injected randomness hard to reason about in tests.
     */
    return hal_random_u32(e->hal) % (uint32_t)HANDOFF_BACKOFF_MAX_US;
}

void elect_init(elect_t *e, const hal_iface_t *hal)
{
    e->hal = hal;
    e->state = ELECT_IDLE;
    e->role = ELECT_ROLE_NONE;
    e->deadline_us = 0;
    e->draw_us = 0;
    e->redraws = 0;
    e->gave_up = false;
}

void elect_start(elect_t *e, uint64_t now_us)
{
    e->role = ELECT_ROLE_NONE;
    e->redraws = 0;
    e->gave_up = false;
    e->draw_us = draw(e);
    e->deadline_us = now_us + e->draw_us;
    e->state = ELECT_BACKOFF;
}

void elect_collision(elect_t *e, uint64_t now_us)
{
    if (++e->redraws > ELECT_MAX_REDRAWS) {
        e->gave_up = true;
        e->state = ELECT_IDLE;
        e->role = ELECT_ROLE_NONE;
        return;
    }
    e->role = ELECT_ROLE_NONE;
    e->draw_us = draw(e);
    e->deadline_us = now_us + e->draw_us;
    e->state = ELECT_BACKOFF;
}

elect_state_t elect_poll(elect_t *e, uint64_t now_us, bool carrier_heard)
{
    switch (e->state) {
    case ELECT_BACKOFF:
        /*
         * Hearing a carrier during the backoff is the common, happy case: the
         * other end drew shorter. Settle immediately as target rather than
         * waiting out a draw whose whole purpose has already been served.
         */
        if (carrier_heard) {
            e->role = ELECT_ROLE_TARGET;
            e->state = ELECT_SETTLED;
            break;
        }
        if (now_us >= e->deadline_us) {
            e->deadline_us = now_us + ELECT_LISTEN_US;
            e->state = ELECT_LISTEN;
        }
        break;

    case ELECT_LISTEN:
        if (carrier_heard) {
            e->role = ELECT_ROLE_TARGET;
            e->state = ELECT_SETTLED;
        } else if (now_us >= e->deadline_us) {
            e->role = ELECT_ROLE_INITIATOR;
            e->state = ELECT_SETTLED;
        }
        break;

    case ELECT_IDLE:
    case ELECT_SETTLED:
    default:
        break;
    }
    return e->state;
}

elect_role_t elect_role(const elect_t *e)   { return e->role; }
uint32_t     elect_draw_us(const elect_t *e) { return e->draw_us; }
bool         elect_gave_up(const elect_t *e) { return e->gave_up; }
