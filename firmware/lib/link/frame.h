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
 * How the preamble hunt decides it is looking at a preamble: at least
 * FRAME_ALT_MIN of the last FRAME_ALT_WINDOW chip-to-chip transitions must
 * have alternated.
 *
 * NOT a perfect run. A single flipped chip destroys two transitions, so a
 * perfect-run rule of length N only tolerates corruption in the first (40 - N)
 * of the 40 alternating chips — with N = 24 that is the first 16, and the
 * other 24 positions lose the whole frame. Counting instead of requiring a run
 * tolerates one flipped chip anywhere.
 *
 * The cost is false syncs from noise. Needing 22 of 24 transitions, then a 00,
 * then the seven-chip tail, is about 3e-8 per chip — one spurious sync every
 * couple of hours at this chip rate, and CRC-16 then rejects it with
 * probability 1 - 2^-16. test_frame pins both halves of that.
 */
#define FRAME_ALT_WINDOW      24
#define FRAME_ALT_MIN         22

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

typedef struct {
    frame_rx_state_t state;

    /* adaptive slicer, for the hard chip decisions the preamble hunt needs */
    int32_t  hi, lo;
    bool     primed;

    /* hunt */
    uint8_t  prev_chip;
    bool     have_prev;
    uint32_t alt_hist;   /* bit i set if chip n-i differed from chip n-i-1 */
    uint16_t seen;       /* chips observed, so a short history is not trusted */

    /* marker */
    uint8_t  marker_pos;

    /* body */
    uint16_t chip_pos;
    uint16_t first_energy;
    uint8_t  body[FRAME_BODY_BYTES];
    uint32_t margin_acc;

    /* results */
    frame_hdr_t hdr;
    uint16_t last_margin;   /* mean |first-second| across the frame */

    /* counters, for telemetry and for the BER harness */
    uint32_t frames_good;
    uint32_t frames_bad_crc;
    uint32_t syncs;
    uint32_t false_syncs;
} frame_rx_t;

void              frame_rx_init(frame_rx_t *r);
void              frame_rx_reset(frame_rx_t *r);
frame_rx_result_t frame_rx_push(frame_rx_t *r, uint16_t chip_energy);

const frame_hdr_t *frame_rx_hdr(const frame_rx_t *r);
const uint8_t     *frame_rx_payload(const frame_rx_t *r);   /* HANDOFF_FRAG_PAYLOAD bytes */

#endif /* HANDOFF_FRAME_H */
