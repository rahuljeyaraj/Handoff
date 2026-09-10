#include "carousel.h"

void carousel_init(carousel_t *c, uint8_t frag_count, uint8_t weight)
{
    c->count = frag_count ? frag_count : 1u;
    c->weight = weight;
    c->phase = 0;
    c->next_tail = weight ? 1u : 0u;
    c->sent = 0;
    c->passes = 0;
}

uint8_t carousel_next(carousel_t *c)
{
    uint8_t out;

    c->sent++;

    /* A single-fragment record has nothing to interleave. */
    if (c->count <= 1u) { c->passes++; return 0; }

    /*
     * Weight 0 means no priority at all: a plain round robin over every
     * fragment, fragment 0 included. Treating it as "never send 0" would
     * starve the one fragment that matters most, which is the opposite of
     * what a zero weight should mean.
     */
    if (c->weight == 0u) {
        out = c->next_tail;
        if (++c->next_tail >= c->count) { c->next_tail = 0; c->passes++; }
        return out;
    }

    if (c->phase < c->weight) {
        c->phase++;
        return 0;
    }

    c->phase = 0;
    out = c->next_tail;

    if (++c->next_tail >= c->count) {
        c->next_tail = 1;
        c->passes++;
    }
    return out;
}

bool carousel_full_pass(const carousel_t *c)
{
    return c->passes > 0;
}
