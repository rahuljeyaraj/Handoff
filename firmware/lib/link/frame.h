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
 * THERE ARE TWO FRAME TYPES, and they share everything down to the marker.
 * Link v2 step 7 gives the rendezvous trigger a frame of its own:
 *
 *   | Preamble   | 32 chips  | the SAME alternating run                     |
 *   | Marker     | 8 bits    | 11110101                                     |
 *   | Nonce      | 2 bytes   | who is shouting, proto/beacon.h              |
 *   | Checksum   | 2 bytes   | CRC-16/CCITT over the nonce                  |
 *
 * 112 chips against a card frame's 624 — 28 ms against 156. See the marker
 * section below for why one hunt serves both, and beacon.h for what the
 * nonce is for.
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
 *   after it, and the body begins at the eighth.
 *
 * That resolves chip phase and frame position in one step, and it needs no
 * count of how many preamble chips actually survived.
 *
 * ONE HUNT, TWO TAILS. Those seven chips are also what says WHICH frame this
 * is, and that is the whole cost of adding the beacon. Under Manchester a
 * chip-pair violation happens at every bit transition: 1->0 gives a 00 and
 * 0->1 gives a 11. A marker of the form 1111 0xyz therefore puts its first
 * violation — a 00 — at exactly the same chip as 11110000 does, whatever the
 * low three bits are, so both frame types feed ONE preamble hunt and diverge
 * only at the tail check. Eight bytes qualify:
 *
 *      11110000   tail 1 0 1 0 1 0 1     a card
 *      11110101   tail 1 1 0 0 1 1 0     a beacon
 *
 * WHICH OF THE SEVEN, AND WHY IT IS NOT THE ONE WITH THE FURTHEST TAIL. The
 * obvious criterion is Hamming distance, and on that 11110111 wins with 6 of
 * 7 — but it is the wrong criterion, and the phase sweep said so before this
 * comment was written. 11110101 was chosen instead because it puts FOUR
 * chip-pair violations into the marker where 11110111 puts two, and that is
 * what protects the BODY behind it:
 *
 *   A receiver that joins a frame late never sees the marker's first 00 — it
 *   has too little history to believe one — so it stays hunting THROUGH the
 *   frame. The chips it is hunting through are Manchester, and a run of
 *   identical bits is a run of alternating chips, which is exactly what a
 *   preamble looks like. With two violations in the marker the window is
 *   clean again 25 chips later, which is one chip before the beacon's body
 *   starts: a nonce beginning 0000 then presents a 00 followed by 1 0 1 0 1 0
 *   1, a PERFECT card marker, and the late joiner reads a beacon as the front
 *   of somebody's card. One nonce in sixteen. Measured: two phases in 141 of
 *   the sweep ended up with both bands listening, which beacon.h §2 says is
 *   unreachable.
 *
 *   Four violations keep the window dirty for 29 chips of the body, and a
 *   card marker after that needs 15 identical bits in a row inside 32 bits of
 *   nonce and CRC. The exposure does not go to zero — it goes from structural
 *   to arithmetic, which is the difference between a bug and a rate.
 *
 * Distance 4 is what 11110101 gives back, and four chip errors inside seven
 * is not a thing this link does. test_frame.c enumerates all 256 bytes and
 * re-picks on both criteria, so neither is typed.
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
#define FRAME_MARKER_TAIL     7    /* chips checked after the 00, both types */
#define FRAME_HDR_BYTES       2
#define FRAME_CRC_BYTES       2
#define FRAME_BODY_BYTES      (FRAME_HDR_BYTES + HANDOFF_FRAG_PAYLOAD + FRAME_CRC_BYTES)
#define FRAME_TOTAL_CHIPS     (FRAME_PREAMBLE_CHIPS + FRAME_MARKER_CHIPS + FRAME_BODY_BYTES * 16)
#define FRAME_AIRTIME_US      ((uint32_t)FRAME_TOTAL_CHIPS * HANDOFF_CHIP_US)

/*
 * ---- the beacon frame (link v2 step 7) ----------------------------------
 *
 * The rendezvous trigger, proto/beacon.h. Same preamble, same hunt, its own
 * marker, and a body of exactly what the trigger has to say: who is shouting,
 * and a CRC that says the question "was that a peer?" was answered by
 * arithmetic rather than by a threshold.
 *
 * THERE ARE NO FLAGS, and design link-v2 §7 asked for eight bits of them
 * ("have-your-record, reply"). Neither has a consumer. The reply bit would
 * carry the election, and the election is decided by who decoded whom —
 * beacon.h §2 has the geometry. The have-your-record bit duplicates
 * FRAME_FLAG_HAVE_YOURS, which the card frame already carries and the
 * carousel already reads. §1's rule cuts both ways: a field goes in when a
 * requirement asks for it, and neither of these is asked for. It costs 16 ms
 * of airtime a beacon to be wrong about that, which is 16 ms of extra deafness
 * in every rendezvous cycle.
 */
#define FRAME_BEACON_MARKER_BYTE  0xF5u
#define FRAME_BEACON_NONCE_BYTES  2
#define FRAME_BEACON_BODY_BYTES   (FRAME_BEACON_NONCE_BYTES + FRAME_CRC_BYTES)
#define FRAME_BEACON_TOTAL_CHIPS  (FRAME_PREAMBLE_CHIPS + FRAME_MARKER_CHIPS                                    + FRAME_BEACON_BODY_BYTES * 16)
#define FRAME_BEACON_AIRTIME_US   ((uint32_t)FRAME_BEACON_TOTAL_CHIPS * HANDOFF_CHIP_US)

HANDOFF_STATIC_ASSERT(FRAME_BEACON_BODY_BYTES <= FRAME_BODY_BYTES,
    "one body buffer serves both frame types, and the beacon is the short one");

/* Both markers must start 1111 0..., or they do not share the hunt: the first
 * chip-pair violation would land somewhere else and the 00 the hunt triggers
 * on would not be the same chip. */
HANDOFF_STATIC_ASSERT((FRAME_MARKER_BYTE & 0xF8u) == 0xF0u &&
                      (FRAME_BEACON_MARKER_BYTE & 0xF8u) == 0xF0u,
    "a marker outside 0xF0..0xF7 does not put its first 00 where the hunt "
    "expects it, so the two frame types would need two preamble hunts");

HANDOFF_STATIC_ASSERT(FRAME_MARKER_BYTE != FRAME_BEACON_MARKER_BYTE,
    "the two frame types must be told apart by their marker");

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
 *      P(either tail)        2/128   TWO tails are accepted, not one
 *
 * so the rate per chip is FRAME_ALT_WAYS / (256 * 2^W), and the requirement is
 *
 *      FRAME_ALT_WAYS * chip rate * HANDOFF_FALSE_SYNC_S  <=  256 * 2^W
 *
 * asserted below, together with its own MINIMALITY: W - 1 must fail it. A
 * bigger window is strictly stricter, so the smallest window that meets the
 * rate is the one that throws away least, and pinning both ends means the
 * number cannot be nudged without a test failing. At 4000 chips/s that comes
 * out at W = 30, MIN = 28, one false sync every 41.0 hours.
 *
 * THE SECOND TAIL COST EXACTLY ONE CHIP OF WINDOW, and that is the whole
 * price of the beacon in this file. Step 6 accepted one tail and computed
 * W = 29; accepting two doubles the rate, and the requirement buys it back
 * with one more transition. The divisor below is what records that, so the
 * day somebody adds a third frame type the build recomputes rather than
 * drifts.
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

#define FRAME_ALT_WINDOW      30
#define FRAME_ALT_MIN         (FRAME_ALT_WINDOW - 2 * FRAME_ALT_FLIPS)

/* How many distinct markers the tail check accepts. Two: a card and a beacon. */
#define FRAME_MARKER_TYPES    2

/* P(00) = 1/4, and P(a random tail is one of the accepted ones) is
 * FRAME_MARKER_TYPES / 2^FRAME_MARKER_TAIL. */
#define FRAME_FALSE_SYNC_DIV  ((4u << FRAME_MARKER_TAIL) / FRAME_MARKER_TYPES)

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

/*
 * A beacon as chips. FRAME_BEACON_TOTAL_CHIPS of them, or 0 if the buffer is
 * too small. The nonce is the caller's — beacon.h owns what it means.
 */
size_t frame_beacon_encode(uint16_t nonce, uint8_t *chips, size_t max_chips);

/*
 * Is this nonce safe to transmit?
 *
 * A receiver that joins a frame LATE never believes the marker's first 00 —
 * it has too little history — so it goes on hunting THROUGH the body. The
 * body is Manchester, and a run of identical bits is a run of alternating
 * chips, which is what a preamble looks like; a 1->0 bit transition after one
 * of those runs is a 00, and three 0 bits behind it complete a card marker.
 * The late joiner then decides a card is arriving, waits for a frame nobody is
 * sending, and costs the rendezvous a cycle.
 *
 * The beacon marker's four violations hold that off for the first 29 chips of
 * the body (see the marker section above), which takes it from one nonce in
 * sixteen to 39 in 65536. This takes it to none: THE NONCE IS OURS TO CHOOSE,
 * so we simply do not transmit one whose own frame contains a card marker.
 * beacon.c draws again, about once in 1700 draws.
 *
 * It is one pass over our own 112 chips with the hunt's own rule, and it is
 * exact rather than a heuristic about run lengths: the worst case for a late
 * joiner is a FULL transition history, because an empty history shifts in
 * zeros and counts fewer alternations, so checking the full-history case
 * covers every join offset there is.
 *
 * ONLY THE CARD TAIL IS CHECKED. A beacon body that contains a BEACON marker
 * costs nothing: the late joiner reads four bytes of nonsense, the CRC throws
 * it away, and that is the whole reason the beacon has a CRC. It is the card
 * tail that commits a listener to 156 ms of waiting.
 *
 * AND CARD FRAMES ARE NOT CHECKED, because they cannot be — their body is
 * somebody's name, NOP-padded with 0x00, which is a long run of identical
 * bits by construction. A late joiner on a card frame has already missed that
 * frame, so the worst it loses is the rest of one it was never going to get.
 * The beacon is different only because it is the frame the rendezvous depends
 * on, and because we get to pick its contents.
 */
bool   frame_beacon_nonce_ok(uint16_t nonce);

/* ---- receive ---------------------------------------------------------- */

typedef enum {
    FRAME_RX_NONE = 0,
    FRAME_RX_GOOD,
    FRAME_RX_BAD_CRC,
    /*
     * A beacon whose CRC passed. The nonce is in frame_rx_nonce().
     *
     * A beacon whose CRC FAILED returns FRAME_RX_NONE, not FRAME_RX_BAD_CRC,
     * and that difference is deliberate. A bad card frame is a lost fragment
     * the carousel will send again, so the caller counts it; a bad beacon is
     * not an event at all — acting on one is precisely the failure the CRC is
     * there to prevent, and the only honest report is silence. The count is
     * kept in beacons_bad_crc for a human.
     */
    FRAME_RX_BEACON
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

    /*
     * marker. Both tails are walked at once and the survivors recorded: the
     * two differ at chip 1, so at most one is still alive by the third chip,
     * but nothing here depends on that and a third frame type would need no
     * new shape.
     */
    uint8_t  marker_pos;
    bool     card_alive;
    bool     beacon_alive;

    /* body. The length is whichever type the marker turned out to be. */
    uint16_t chip_pos;
    uint16_t body_bytes;
    bool     body_is_beacon;
    frame_chip_t first_d;
    uint8_t  body[FRAME_BODY_BYTES];
    uint64_t margin_acc;

    /* results */
    frame_hdr_t hdr;
    uint16_t last_nonce;    /* the nonce of the last beacon that passed CRC  */
    uint32_t last_margin;   /* mean |d(first) - d(second)|, as an LSB score */
    uint16_t last_imbalance_pct;  /* tone B against tone A, 100 = equal */

    /* counters, for telemetry and for the BER harness */
    uint32_t frames_good;
    uint32_t frames_bad_crc;
    /*
     * syncs counts CARD syncs only, and link_sm.c depends on that: a sync is
     * how the trigger learns a card is already arriving, and a beacon is the
     * opposite of that news.
     */
    uint32_t syncs;
    uint32_t beacon_syncs;
    uint32_t beacons_good;
    uint32_t beacons_bad_crc;
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

/* The nonce of the beacon that produced the last FRAME_RX_BEACON. */
uint16_t           frame_rx_nonce(const frame_rx_t *r);

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
