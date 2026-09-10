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
