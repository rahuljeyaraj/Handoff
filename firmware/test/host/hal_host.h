/*
 * Handoff — hal.h implemented against a simulated channel. architecture §5.
 *
 * Injects at the CHIP ENERGY layer, which is the core-1/core-0 boundary of
 * architecture §3.3. That is deliberate: protocol tests should exercise the
 * protocol, not re-run the Goertzel a million times. DSP tests inject one
 * layer lower, through chan.h.
 *
 * Time is virtual and advanced by hand, so a 5 ms backoff and a 150 ms frame
 * cost no wall-clock time and role election runs thousands of handshakes per
 * second. Randomness is injectable, so an election tie is forced rather than
 * waited for.
 */
#ifndef HANDOFF_HAL_HOST_H
#define HANDOFF_HAL_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chan.h"
#include "hal.h"

#define HALH_RX_FIFO 4096
#define HALH_TX_FIFO 4096

typedef struct halh_node halh_node_t;

typedef struct {
    /* Chip energies delivered for an on chip and an off chip. Defaults sit at
     * the design §5 link budget: ~200 LSB of carrier, near-zero when gated. */
    uint16_t energy_on;
    uint16_t energy_off;

    uint16_t noise_lsb;       /* uniform jitter added to every chip energy   */
    double   chip_error_prob; /* probability one chip is destroyed outright  */
    double   dropout_prob;    /* probability a burst of chips is lost        */
    int      dropout_chips;
} halh_chan_t;

struct halh_node {
    hal_iface_t   iface;
    halh_node_t  *peer;
    const char   *name;

    uint64_t     *clock_us;   /* shared with the peer                        */

    /*
     * Whether the two wearers are actually touching. FALSE is not "a bad
     * channel": before skin meets skin there is no channel at all and a
     * transmission is simply inaudible. beacon.c is built on exactly that, so
     * a simulator that always couples cannot test the thing it is testing.
     */
    bool          coupled;
    halh_chan_t   chan;
    rng_t         rng;

    /* Injected randomness for elect.c. When forced_len is non-zero these
     * values are handed out in order and then repeat, so a tie is exact. */
    uint32_t      forced[8];
    uint8_t       forced_len;
    uint8_t       forced_pos;

    /* transmit */
    uint8_t       tx[HALH_TX_FIFO];
    size_t        tx_len;
    size_t        tx_sent;        /* chips already pushed onto the medium    */
    uint64_t      tx_start_us;
    bool          driving;

    /* receive */
    uint16_t      rx[HALH_RX_FIFO];
    size_t        rx_head, rx_tail;

    int           dropout_left;

    /* counters */
    uint32_t      chips_tx;
    uint32_t      chips_rx;
    uint32_t      tlm_events;
};

void halh_chan_default(halh_chan_t *c);

/* Wire two nodes to each other and to one shared clock. */
void halh_pair(halh_node_t *a, halh_node_t *b, uint64_t *clock_us, uint64_t seed);

/* A single node with no peer: useful for one-way tests. */
void halh_init(halh_node_t *n, const char *name, uint64_t *clock_us, uint64_t seed);

/*
 * Advance the shared clock, delivering whatever each node put on the medium
 * during that interval to the other node's receive queue. This is the only
 * thing that makes time pass.
 */
void halh_advance(halh_node_t *a, halh_node_t *b, uint64_t us);

/*
 * Skin contact between the two wearers. halh_pair() starts them touching,
 * because every test that predates beacon.c is about what happens during a
 * contact rather than about how one begins.
 */
void halh_set_coupled(halh_node_t *a, halh_node_t *b, bool on);

/* Force the next election draws. Call with n = 0 to return to the RNG. */
void halh_force_random(halh_node_t *n, const uint32_t *values, size_t count);

#endif /* HANDOFF_HAL_HOST_H */
