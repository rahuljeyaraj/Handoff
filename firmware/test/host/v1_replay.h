/*
 * Handoff — a v1 OOK receiver, kept alive for the captures. Link v2 step 6.
 *
 * WHY THIS IS IN test/host AND NOT IN lib/.
 *
 * firmware/test/vectors/captures holds real ADC recordings from the M5 bench,
 * and one of them is the frame the board lost when a loud transient froze its
 * adaptive slicer. They are evidence, development plan §1 says every hardware
 * failure becomes a host test, and they replay on every run.
 *
 * They are also v1: a gated carrier on one bin, where the receiver has to turn
 * an amplitude into a hard 1 or 0 and therefore has to know what "loud" means.
 * Link v2 step 6 deleted the machinery that did that — with two tones a chip is
 * E_B > E_A and there is no threshold anywhere in the firmware. So the slicer
 * moved here rather than being kept in lib/link for two files.
 *
 * WHAT THE CAPTURES STILL TEST is the part of the framer step 6 did not touch:
 * the preamble hunt, the marker rule, the Manchester pairing and the CRC, run
 * against samples a real amplifier produced. What they can no longer test is
 * the chip decision — correctly, because the decision they were recorded to
 * exercise does not exist any more.
 *
 * The arithmetic is v1's, unchanged, down to the one-LSB minimum step that the
 * first of those two captures is the bug report for.
 */
#ifndef HANDOFF_V1_REPLAY_H
#define HANDOFF_V1_REPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "frame.h"
#include "goertzel.h"
#include "sync.h"

typedef struct {
    gz_t     gz;
    sync_t   sy;
    int32_t  hi, lo;
    bool     primed;
} v1_replay_t;

void v1_replay_init(v1_replay_t *v, uint16_t bin);

/*
 * One sample. True once per chip, with a SIGNED chip in *chip — energy minus
 * the slicing point. Its sign is v1's hard decision and the difference of two
 * of them is v1's Manchester comparison, so an OOK capture reaches
 * frame_rx_push() through exactly the same code path as a two-tone frame.
 */
bool v1_replay_push(v1_replay_t *v, int16_t sample, frame_chip_t *chip);

#endif /* HANDOFF_V1_REPLAY_H */
