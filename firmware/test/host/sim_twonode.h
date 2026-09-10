/*
 * Handoff — two link_sm instances against one simulated channel.
 * architecture §7.2, development plan M1.
 *
 * Time is virtual, so a handshake that takes a second on a wrist takes
 * microseconds here and thousands of them run in a test suite. That is the
 * only reason the carousel and the turn-taking get real statistical coverage
 * before any hardware exists.
 *
 * This harness starts the exchange DIRECTLY, with the roles handed out rather
 * than discovered: it is testing what happens during a contact, not how one
 * begins. How one begins is beacon.h's job and test_beacon.c's, and mixing the
 * two would put a rendezvous of up to ~110 ms inside every measurement of a
 * 250 ms contact — which is exactly the column architecture §8.4's degradation
 * table is read off.
 */
#ifndef HANDOFF_SIM_TWONODE_H
#define HANDOFF_SIM_TWONODE_H

#include <stdbool.h>
#include <stdint.h>

#include "hal_host.h"
#include "link_sm.h"

typedef struct {
    uint64_t     clock_us;
    halh_node_t  node_a, node_b;
    link_sm_t    sm_a, sm_b;
    frag_tx_t    rec_a, rec_b;
    link_cfg_t   cfg;

    /* Which end talks first. Taken from the seed so a sweep exercises the
     * carousel and the handover in both directions, the way varying election
     * outcomes used to. */
    bool         a_sends;
} sim_t;

typedef struct {
    bool     a_complete, b_complete;
    bool     a_has_b, b_has_a;      /* record fully reassembled */
    uint64_t duration_us;
    uint32_t frames_a, frames_b;
    uint32_t bad_crc_a, bad_crc_b;
    link_role_t  role_a, role_b;
} sim_result_t;

/* Build a sim with the two cards given as vCard text. */
void sim_init(sim_t *s, const char *card_a, const char *card_b,
              const link_cfg_t *cfg, uint64_t seed);

/* Run until both ends complete or the budget expires. contact_us caps how long
 * the two are actually touching, which is the interesting knob: architecture
 * §8.4's degradation table is a sweep of exactly this. */
sim_result_t sim_run(sim_t *s, uint64_t contact_us);

/* What A ended up knowing about B, as vCard text. Returns length. */
size_t sim_card_a_sees(const sim_t *s, char *out, size_t max);
size_t sim_card_b_sees(const sim_t *s, char *out, size_t max);

#endif /* HANDOFF_SIM_TWONODE_H */
