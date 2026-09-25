/*
 * Handoff — Manchester line coding. Design §9.2.
 *
 * A mark is on-then-off, a space off-then-on. Chips are transmitted MSB-first
 * within a byte.
 *
 * The receive side never compares against an absolute threshold: it compares
 * the two halves of a bit against each other. That is the whole reason for
 * Manchester here — received amplitude moves with grip, posture and footwear,
 * so an absolute threshold fails during exactly the long runs it is needed for.
 */
#ifndef HANDOFF_MANCHESTER_H
#define HANDOFF_MANCHESTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MANCHESTER_CHIPS_PER_BIT 2

/*
 * The longest run of identical chips this code can produce.
 *
 * Structural, and it falls straight out of the line code: a mark is 1,0 and a
 * space is 0,1, so every bit contributes one of each and the only way two
 * identical chips can meet is across a bit boundary — 1,0 followed by 0,1
 * gives 0,0 in the middle, and 0,1 followed by 1,0 gives 1,1. Three in a row
 * would need a bit that is two chips the same, and there is no such bit.
 *
 * test_manchester.c walks every bit pair and checks it, because this is
 * relied on somewhere that would fail quietly rather than loudly.
 *
 * WHERE IT IS RELIED ON. v1 switches the carrier off for a 0, so a run of
 * identical chips at level 0 is a run of SILENCE, and this is how long a
 * silence inside a live transmission can be. link_sm.c's listen-before-talk
 * needs that number — see the bridge there, and see design link-v2 §4 for why
 * it stops meaning anything once both symbols are tones.
 */
#define MANCHESTER_MAX_RUN_CHIPS 2

/* One byte -> 16 chips, values 0 or 1. Returns chips written, 0 if it would
 * not fit. */
size_t manchester_encode(const uint8_t *bytes, size_t nbytes,
                         uint8_t *chips, size_t max_chips);

/* Hard chip decisions -> bytes. nchips must be a multiple of 16. Returns bytes
 * written; a chip pair of 00 or 11 is a coding violation and decodes by
 * majority rather than failing, because the CRC is the arbiter, not this. */
size_t manchester_decode_chips(const uint8_t *chips, size_t nchips,
                               uint8_t *bytes, size_t max_bytes);

/* Soft decision from a pair of chip energies. */
static inline bool manchester_bit(uint16_t first, uint16_t second)
{
    return first > second;   /* mark = on-then-off */
}

/* How far apart the two halves were, as a confidence measure. */
static inline uint16_t manchester_margin(uint16_t first, uint16_t second)
{
    return (uint16_t)(first > second ? first - second : second - first);
}

#endif /* HANDOFF_MANCHESTER_H */
