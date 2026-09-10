/*
 * Handoff — symbol synchronisation. scores -> chip energies.
 *
 * HANDOFF_WINDOWS_PER_CHIP scores make one chip. Which five is the question,
 * and it is not answered once: two independent crystals drift, so the chip
 * boundary walks. Manchester guarantees a transition every bit (design §9.2),
 * so the boundary is wherever the score changes most, and this module tracks
 * that with an early/late gate.
 *
 * Runs on core 1. Output is the chip-rate stream that crosses to core 0.
 */
#ifndef HANDOFF_SYNC_H
#define HANDOFF_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#define SYNC_MAX_WPC 16

typedef struct {
    uint8_t  wpc;                    /* windows per chip                     */
    uint8_t  guard;                  /* windows dropped at each chip end     */
    uint8_t  pos;                    /* windows into the current chip        */
    uint8_t  len;                    /* length of the current chip, wpc +-1  */
    uint32_t hist[SYNC_MAX_WPC];     /* scores of the chip in progress       */
    uint32_t prev;                   /* last score, for the edge detector    */
    bool     have_prev;
    uint32_t edge[SYNC_MAX_WPC];     /* leaky per-phase transition energy    */
    int8_t   last_correction;        /* -1, 0 or +1, for telemetry           */
    uint32_t chips_out;
} sync_t;

void sync_init(sync_t *s, uint8_t windows_per_chip, uint8_t guard);
void sync_reset(sync_t *s);

/* Feed one Goertzel score. Returns true once per chip with the integrated
 * chip energy in *chip. */
bool sync_push(sync_t *s, uint32_t score, uint16_t *chip);

/* Where the tracker currently believes the boundary is, in windows, relative
 * to where it is sampling. Zero means locked. Tests assert on this. */
int  sync_phase_error(const sync_t *s);

#endif /* HANDOFF_SYNC_H */
