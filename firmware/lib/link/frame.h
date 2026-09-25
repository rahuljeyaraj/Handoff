/*
 * Handoff — framing. architecture §8.3, superseding design §9.4.
 *
 *   | Preamble   | 32 chips  | alternating 1010...                          |
 *   | Marker     | 8 bits    | 11110000                                     |
 *   | Header     | 2 bytes   | frag index 4b | frag count 4b | rec id 6b |
 *   |            |           | flags 2b                                     |
 *   | Payload    | fixed     | HANDOFF_FRAG_PAYLOAD bytes, NOP-padded       |
 *   | Checksum   | 2 bytes   | CRC-16/CCITT over header + payload           |
 *
 * Two decisions made here that the spec left open:
 *
 * THE SYNC RULE. Under Manchester, an alternating chip run is a run of
 * identical bits, so a 32-chip alternating preamble is chip-phase ambiguous by
 * construction. Marker 11110000 encodes to chips 1010101001010101, and
 * appended to the preamble the whole run stays alternating until one place:
 * the boundary between marker bit 3 (chips 1,0) and bit 4 (chips 0,1), which
 * produces the only 00 pair in the header. So the detector is:
 *
 *   lock to the alternating run, find the first 00, verify the seven chips
 *   after it are 1010101, and the payload begins at the eighth.
 *
 * That resolves chip phase and frame position in one step, and it needs no
 * count of how many preamble chips actually survived.
 *
 * FIXED PAYLOAD LENGTH. The frame carries no length field — architecture §8.3
 * makes the point that a length prefix can itself be corrupted, and the frag
 * count already tells the receiver when it is done. Short fragments are padded
 * with TLV tag 0x00, which the compact decoder skips (see compact.h).
 */
#ifndef HANDOFF_FRAME_H
#define HANDOFF_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

#define FRAME_PREAMBLE_CHIPS  32
#define FRAME_MARKER_BYTE     0xF0u
#define FRAME_MARKER_CHIPS    16
#define FRAME_HDR_BYTES       2
#define FRAME_CRC_BYTES       2
#define FRAME_BODY_BYTES      (FRAME_HDR_BYTES + HANDOFF_FRAG_PAYLOAD + FRAME_CRC_BYTES)
#define FRAME_TOTAL_CHIPS     (FRAME_PREAMBLE_CHIPS + FRAME_MARKER_CHIPS + FRAME_BODY_BYTES * 16)
#define FRAME_AIRTIME_US      ((uint32_t)FRAME_TOTAL_CHIPS * HANDOFF_CHIP_US)

/*
 * ---- the preamble hunt, COMPUTED from a stated rate (link v2 step 6) -----
 *
 * The rule: at least FRAME_ALT_MIN of the last FRAME_ALT_WINDOW chip-to-chip
 * transitions must have alternated. Two numbers, and neither is typed any
 * more — both fall out of one structural choice and one stated requirement.
 *
 * THE STRUCTURAL CHOICE: tolerate one flipped chip. Counting transitions
 * instead of requiring a perfect run exists for exactly this, and nothing
 * else. A perfect-run rule of length N tolerates corruption only in the first
 * (FRAME_ALT_RUN_CHIPS - N) of the alternating run, so every other position
 * loses the whole frame. One flipped chip destroys the transition into it and
 * the transition out of it — two — so
 *
 *      FRAME_ALT_MIN = FRAME_ALT_WINDOW - 2 * FRAME_ALT_FLIPS
 *
 * THE STATED REQUIREMENT: HANDOFF_FALSE_SYNC_S, one false sync per 24 hours
 * of continuous listening (config.h). Against uniform random chips a false
 * sync needs three independent things at once:
 *
 *      P(the 00)             1/4     two chips, both 0
 *      P(the alternation)    W ways of missing by at most 2, over 2^W
 *      P(the seven-chip tail) 1/128
 *
 * so the rate per chip is FRAME_ALT_WAYS / (512 * 2^W), and the requirement is
 *
 *      FRAME_ALT_WAYS * chip rate * HANDOFF_FALSE_SYNC_S  <=  512 * 2^W
 *
 * asserted below, together with its own MINIMALITY: W - 1 must fail it. A
 * bigger window is strictly stricter, so the smallest window that meets the
 * rate is the one that throws away least, and pinning both ends means the
 * number cannot be nudged without a test failing. At 4000 chips/s that comes
 * out at W = 29, MIN = 27, one false sync every 43.8 hours.
 *
 * WHAT THIS REPLACES. 24 and 22, typed, whose own comment here admitted they
 * bought "one spurious sync every couple of hours" — 12x worse than the rate
 * above, and nobody had chosen it. CRC-16 still rejects what gets through
 * with probability 1 - 2^-16; test_frame pins both halves.
 *
 * FRAME_ALT_RUN_CHIPS is the alternating run the sync rule actually gives the
 * hunt: the whole preamble plus the first four marker bits, which stay
 * alternating right up to the 00. The window cannot exceed its transitions.
 */
#define FRAME_ALT_FLIPS       1
#define FRAME_ALT_RUN_CHIPS   (FRAME_PREAMBLE_CHIPS + FRAME_MARKER_CHIPS / 2)

#define FRAME_ALT_WINDOW      29
#define FRAME_ALT_MIN         (FRAME_ALT_WINDOW - 2 * FRAME_ALT_FLIPS)

/* P(00) = 1/4 and P(the seven-chip tail) = 1/128. */
#define FRAME_FALSE_SYNC_DIV  512u

/* Ways to miss the alternation by at most 2 transitions: C(w,0)+C(w,1)+C(w,2).
 * Stated for FRAME_ALT_FLIPS == 1, which is asserted below. */
#define FRAME_ALT_WAYS_AT(w) \
    (1ull + (unsigned long long)(w) \
          + ((unsigned long long)(w) * (unsigned long long)((w) - 1u)) / 2ull)

#define FRAME_ALT_RATE_OK(w)                                            \
    (FRAME_ALT_WAYS_AT(w) * (unsigned long long)HANDOFF_CHIP_RATE_HZ    \
         * (unsigned long long)HANDOFF_FALSE_SYNC_S                     \
     <= (unsigned long long)FRAME_FALSE_SYNC_DIV * (1ull << (w)))

HANDOFF_STATIC_ASSERT(FRAME_ALT_FLIPS == 1,
    "FRAME_ALT_WAYS_AT counts misses of at most two transitions, which is one "
    "flipped chip: a different tolerance needs a different binomial sum");

HANDOFF_STATIC_ASSERT(FRAME_ALT_RATE_OK(FRAME_ALT_WINDOW),
    "the preamble hunt is looser than HANDOFF_FALSE_SYNC_S allows");

HANDOFF_STATIC_ASSERT(!FRAME_ALT_RATE_OK(FRAME_ALT_WINDOW - 1),
    "a shorter window would still meet the stated false-sync rate: this one "
    "is tighter than the requirement, which costs tolerance for nothing");

HANDOFF_STATIC_ASSERT(FRAME_ALT_WINDOW <= FRAME_ALT_RUN_CHIPS - 1,
    "the hunt window is longer than the alternating run that feeds it");

HANDOFF_STATIC_ASSERT(FRAME_ALT_WINDOW <= 32,
    "the transition history is a uint32_t");

HANDOFF_STATIC_ASSERT(FRAME_ALT_MIN > FRAME_ALT_WINDOW / 2,
    "a majority of transitions must alternate or the rule says nothing");

#define FRAME_MAX_FRAGS       16   /* 4-bit frag count */
#define FRAME_MAX_RECORD_ID   63   /* 6-bit           */

typedef struct {
    uint8_t frag_index;   /* 0..15                                   */
    uint8_t frag_count;   /* 1..16, stored on the wire as count - 1  */
    uint8_t record_id;    /* 0..63, bumped on re-provisioning        */
    uint8_t flags;        /* 0..3, see FRAME_FLAG_*                  */
} frame_hdr_t;

/*
 * The sender already holds the receiver's complete record. This is what lets
 * an exchange end deliberately instead of by timeout: without it, the first
 * end to be satisfied simply stops transmitting and strands the other one
 * mid-record. Two bits is all the header has, and this is the one worth
 * spending them on.
 */
#define FRAME_FLAG_HAVE_YOURS 0x1u

#define FRAME_FLAG_REPLY      0x2u  /* target's answer, not the initiator's offer */

void   frame_hdr_pack(const frame_hdr_t *h, uint8_t out[FRAME_HDR_BYTES]);
void   frame_hdr_unpack(const uint8_t in[FRAME_HDR_BYTES], frame_hdr_t *h);

/* Whole frame as chips (one byte per chip, 0 or 1). n may be less than
 * HANDOFF_FRAG_PAYLOAD; the rest is NOP padding. Returns chips written, or 0
 * if the buffer is too small. */
size_t frame_encode(const frame_hdr_t *h, const uint8_t *payload, size_t n,
                    uint8_t *chips, size_t max_chips);

/* ---- receive ---------------------------------------------------------- */

typedef enum {
    FRAME_RX_NONE = 0,
    FRAME_RX_GOOD,
    FRAME_RX_BAD_CRC
} frame_rx_result_t;

typedef enum { FRAME_ST_HUNT = 0, FRAME_ST_MARKER, FRAME_ST_BODY } frame_rx_state_t;

/*
 * ---- A CHIP IS A SIGNED DIFFERENCE, AND THERE IS NOTHING TO SLICE --------
 *
 * link v2 step 6. The pad carries tone A or tone B for every chip, so the
 * receiver scores both bins in the same window and hands core 0 one number:
 *
 *      d = E_B - E_A          in mag^2, the bank's own units
 *
 * Every decision the framer makes is then a comparison, and none of them
 * involves a threshold, a level, or anything remembered:
 *
 *      the chip           d > 0            tone B was louder. That is all.
 *      the preamble       sign(d) alternating
 *      the Manchester bit d(first) > d(second)
 *
 * v1 needed an adaptive slicer here because a single energy stream cannot
 * produce a hard 1/0 without a threshold, and that slicer carried hi, lo,
 * primed and a decay shift — four remembered numbers and a bench-found
 * minimum step, all of which are DELETED. Design link-v2 §8.
 *
 * WHY NO IMBALANCE CORRECTION, AND WHY THAT IS NOT A SHORTCUT. Design §8
 * expected to carry the 180/200 imbalance out of the preamble and into the
 * body decisions, because a raw comparison of two tones of different strength
 * is biased. Step 4 measured that imbalance at 9.1 % and, more usefully,
 * measured the OFF-tone bin as indistinguishable from silence — 62 against 67
 * quiet, while the on-tone bin read 716. So the comparison is never between
 * two signals of unequal strength. It is between a signal and a noise floor,
 * and 9 % cannot change its sign.
 *
 * It cancels exactly in the body, too, and that is structural rather than
 * lucky. Manchester puts one tone-A chip and one tone-B chip in EVERY bit:
 *
 *      mark   d(first) - d(second) = S_B - (-S_A) = +(S_A + S_B)
 *      space  d(first) - d(second) = -S_A - S_B   = -(S_A + S_B)
 *
 * — symmetric whatever the two strengths are. The correction design §8 asked
 * for would be machinery with nothing to correct, and the rule (§1) says a
 * number goes in only when a requirement asks for it. None does.
 *
 * The imbalance is still worth watching, and d alone is enough to watch it:
 * through an alternating run the negative d's average to -S_A and the
 * positive ones to +S_B, so the hunt measures it on the preamble it just
 * synced to and leaves it in last_imbalance_pct. Reported, never fed back —
 * an instrument, not a correction, and unlike `y 3` it reads the far board
 * mid-handshake rather than needing a board dedicated to driving a pattern.
 *
 * WHY mag^2 AND NOT AMPLITUDE. A square root buys nothing a comparison can
 * use, and for non-coherent FSK the comparison of the two bins' POWER is the
 * right metric rather than an approximation to it. Design §6's "no square
 * roots on the hot path" is a requirement, and the root that used to sit
 * there cost 18 points of core 1 at step 5.
 */
typedef int32_t frame_chip_t;

/* hal.h carries the same number across the core boundary and cannot include
 * this header — it sits below link/. One place says they are the same type. */
HANDOFF_STATIC_ASSERT(sizeof(frame_chip_t) == sizeof(int32_t),
    "frame_chip_t and hal.h's rx_chips element type have diverged");

typedef struct {
    frame_rx_state_t state;

    /* hunt */
    uint8_t  prev_chip;
    bool     have_prev;
    uint32_t alt_hist;   /* bit i set if chip n-i differed from chip n-i-1 */
    uint16_t seen;       /* chips observed, so a short history is not trusted */

    /* The imbalance, off the run that leads into a sync. |d| by sign, so a
     * tone-A chip lands in pre_a and a tone-B chip in pre_b. Instrument. */
    uint64_t pre_a, pre_b;
    uint32_t pre_na, pre_nb;

    /* marker */
    uint8_t  marker_pos;

    /* body */
    uint16_t chip_pos;
    frame_chip_t first_d;
    uint8_t  body[FRAME_BODY_BYTES];
    uint64_t margin_acc;

    /* results */
    frame_hdr_t hdr;
    uint32_t last_margin;   /* mean |d(first) - d(second)|, as an LSB score */
    uint16_t last_imbalance_pct;  /* tone B against tone A, 100 = equal */

    /* counters, for telemetry and for the BER harness */
    uint32_t frames_good;
    uint32_t frames_bad_crc;
    uint32_t syncs;
    uint32_t false_syncs;
} frame_rx_t;

void              frame_rx_init(frame_rx_t *r);
void              frame_rx_reset(frame_rx_t *r);
/*
 * One chip. d is E_B - E_A for that chip, in the bank's mag^2 units — see
 * frame_chip_t above. Positive is tone B, which is chip value 1.
 */
frame_rx_result_t frame_rx_push(frame_rx_t *r, frame_chip_t d);

const frame_hdr_t *frame_rx_hdr(const frame_rx_t *r);
const uint8_t     *frame_rx_payload(const frame_rx_t *r);   /* HANDOFF_FRAG_PAYLOAD bytes */

/*
 * The framer has taken a preamble and is collecting a frame. It is a far better
 * answer to "is the far end still transmitting?" than carrier.c is, and that is
 * what it exists for.
 *
 * It was once load-bearing. carrier.c's floor used to climb to meet a carrier
 * that lasted a whole frame, so carrier_present() went false partway through
 * every one — measured on the first assembled PCB, 24 Sep 2026, as a receiver
 * reporting `carrier level 548 floor 256 present 0` while the framer synced 23
 * times and finished exactly zero frames, because the receive turn ended on the
 * detector's silence and reset the framer mid-body every time.
 *
 * carrier.c no longer does that: its floor is frozen while presence is up, so
 * it cannot learn the carrier it is listening to. This stays anyway. A framer
 * holding a frame is direct evidence that the far end is transmitting, where
 * the detector is an energy threshold that a fade can drop; and a turn ending
 * over the top of an arriving frame costs a whole card.
 */
bool               frame_rx_busy(const frame_rx_t *r);

#endif /* HANDOFF_FRAME_H */
