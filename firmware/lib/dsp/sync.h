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
 *
 * ---- WHAT "THE SCORE" IS, SINCE LINK V2 STEP 6 --------------------------
 *
 * A SIGNED tone difference, d = E_B - E_A, and everything below is unchanged
 * by that. The tracker never cared what the number meant; it cared that a
 * chip boundary is where it MOVES. Under v1 that was an envelope going on and
 * off. Under FSK the envelope is constant — that is the point of FSK — so
 * there is no edge in the level at all, and the edge is entirely in which
 * tone is being sent. Design link-v2 §8.
 *
 * So sync_push_d() is the real entry point and takes d. sync_push() is the
 * single-bin form kept for the v1 chain and the console probe: it is the same
 * function with E_A held at zero, which makes d the score and the edge
 * detector bit-for-bit what it was.
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
    int32_t  hist[SYNC_MAX_WPC];     /* the chip in progress, window by window */
    int32_t  prev;                   /* last window, for the edge detector    */
    bool     have_prev;
    /*
     * Leaky per-phase transition energy. 64-bit, and that is the mag^2 domain
     * asking for it rather than caution: a window difference reaches 2^31 at
     * full scale (gz_bank.h) and the leak holds about eight of them, so a
     * 32-bit accumulator would rail on a self-loop capture — and a railed
     * edge histogram has no argmax, which is to say no tracker at all.
     */
    uint64_t edge[SYNC_MAX_WPC];
    int8_t   last_correction;        /* -1, 0 or +1, for telemetry           */
    uint32_t chips_out;
} sync_t;

void sync_init(sync_t *s, uint8_t windows_per_chip, uint8_t guard);
void sync_reset(sync_t *s);

/*
 * Feed one window's tone difference, d = E_B - E_A. Returns true once per
 * chip with the chip's integrated difference in *chip — the signed number
 * frame.c decides on.
 */
bool sync_push_d(sync_t *s, int32_t d, int32_t *chip);

/* The single-bin form: one Goertzel score, one unsigned chip energy. The v1
 * chain and the console probe, not the link. Saturates at 0xFFFF. */
bool sync_push(sync_t *s, uint32_t score, uint16_t *chip);

/* Where the tracker currently believes the boundary is, in windows, relative
 * to where it is sampling. Zero means locked. Tests assert on this. */
int  sync_phase_error(const sync_t *s);

#endif /* HANDOFF_SYNC_H */
