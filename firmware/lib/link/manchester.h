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
 * WHERE IT IS RELIED ON. It changed at link v2 step 6, and the number did
 * not.
 *
 * It USED to bound a SILENCE: v1 switched the carrier off for a 0, so a run
 * of identical chips was a run of nothing on the pad, and link_sm.c's
 * listen-before-talk carried a bridge that long so a detector with no memory
 * would not call a live frame quiet in its gaps. FSK has no spaces — the pad
 * carries tone A or tone B and never nothing — so that bridge is deleted and
 * this is not what the number is for any more.
 *
 * It now bounds a run of ONE TONE, which is the reason Manchester stays at
 * all. Design link-v2 §8: FSK retires Manchester's threshold job and not its
 * timing job, because dsp/sync.c tracks the chip boundary by where the tone
 * difference changes most, and a long run of one tone has no such change in
 * it. Two chips is the longest gap between edges the line code can produce,
 * so the tracker is never blind for longer than half a bit.
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

/*
 * Soft decision from the two halves of a bit.
 *
 * SIGNED, since link v2 step 6. The halves are no longer energies but chip
 * DIFFERENCES, d = E_B - E_A (frame.h), and the comparison is unchanged:
 * a mark is the louder half first. Under v1 that meant carrier-then-silence;
 * under v2 it means tone-B-then-tone-A, and one expression covers both
 * because both are "first half beats second half".
 *
 * And it is why the 180/200 imbalance needs no correction. Every bit holds
 * one chip of each tone, so the difference of the two halves is
 * +-(S_A + S_B) whatever the two strengths are — symmetric by construction.
 * frame.h has the working.
 */
static inline bool manchester_bit(int32_t first, int32_t second)
{
    return first > second;   /* mark = the louder half first */
}

/* How far apart the two halves were, as a confidence measure. Fits a uint32
 * because both halves come from one bank window and mag^2 stays under 2^31
 * there (gz_bank.h), so the difference cannot reach 2^32. */
static inline uint32_t manchester_margin(int32_t first, int32_t second)
{
    const int64_t d = (int64_t)first - (int64_t)second;
    return (uint32_t)(d < 0 ? -d : d);
}

#endif /* HANDOFF_MANCHESTER_H */
