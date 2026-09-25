#include "frame.h"

#include <string.h>

#include "crc.h"
#include "manchester.h"

/* The seven chips that must follow the 00, per the sync rule in frame.h. */
static const uint8_t k_marker_tail[7] = { 1, 0, 1, 0, 1, 0, 1 };

void frame_hdr_pack(const frame_hdr_t *h, uint8_t out[FRAME_HDR_BYTES])
{
    const uint8_t count = (uint8_t)((h->frag_count ? h->frag_count - 1u : 0u) & 0x0Fu);
    const uint16_t w = (uint16_t)(((uint16_t)(h->frag_index & 0x0Fu) << 12)
                                | ((uint16_t)count << 8)
                                | ((uint16_t)(h->record_id & 0x3Fu) << 2)
                                | (uint16_t)(h->flags & 0x03u));
    out[0] = (uint8_t)(w >> 8);
    out[1] = (uint8_t)(w & 0xFFu);
}

void frame_hdr_unpack(const uint8_t in[FRAME_HDR_BYTES], frame_hdr_t *h)
{
    const uint16_t w = (uint16_t)(((uint16_t)in[0] << 8) | in[1]);
    h->frag_index = (uint8_t)((w >> 12) & 0x0Fu);
    h->frag_count = (uint8_t)(((w >> 8) & 0x0Fu) + 1u);
    h->record_id  = (uint8_t)((w >> 2) & 0x3Fu);
    h->flags      = (uint8_t)(w & 0x03u);
}

size_t frame_encode(const frame_hdr_t *h, const uint8_t *payload, size_t n,
                    uint8_t *chips, size_t max_chips)
{
    uint8_t body[FRAME_BODY_BYTES];
    uint16_t crc;
    size_t out = 0, i;

    if (n > HANDOFF_FRAG_PAYLOAD) return 0;
    if (max_chips < (size_t)FRAME_TOTAL_CHIPS) return 0;

    for (i = 0; i < FRAME_PREAMBLE_CHIPS; i++)
        chips[out++] = (uint8_t)((i % 2u) ? 0u : 1u);   /* 1010... */

    frame_hdr_pack(h, body);
    memset(body + FRAME_HDR_BYTES, 0, HANDOFF_FRAG_PAYLOAD);   /* tag 0 = NOP */
    if (n && payload) memcpy(body + FRAME_HDR_BYTES, payload, n);

    crc = crc16(body, FRAME_HDR_BYTES + HANDOFF_FRAG_PAYLOAD);
    body[FRAME_HDR_BYTES + HANDOFF_FRAG_PAYLOAD]     = (uint8_t)(crc >> 8);
    body[FRAME_HDR_BYTES + HANDOFF_FRAG_PAYLOAD + 1] = (uint8_t)(crc & 0xFFu);

    {
        const uint8_t marker = FRAME_MARKER_BYTE;
        out += manchester_encode(&marker, 1, chips + out, max_chips - out);
        out += manchester_encode(body, FRAME_BODY_BYTES, chips + out, max_chips - out);
    }

    return out;
}

/* ---- receive ---------------------------------------------------------- */

void frame_rx_init(frame_rx_t *r)
{
    memset(r, 0, sizeof *r);
    frame_rx_reset(r);
}

void frame_rx_reset(frame_rx_t *r)
{
    r->state = FRAME_ST_HUNT;
    r->have_prev = false;
    r->alt_hist = 0;
    r->seen = 0;
    r->marker_pos = 0;
    r->chip_pos = 0;
    r->margin_acc = 0;
    r->pre_a = r->pre_b = 0;
    r->pre_na = r->pre_nb = 0;
}

/* Record one chip in the transition history the preamble hunt runs on. */
static void observe(frame_rx_t *r, uint8_t c)
{
    r->alt_hist = (r->alt_hist << 1) | (uint32_t)(c != r->prev_chip);
    if (r->seen < 0xFFFFu) r->seen++;
    r->prev_chip = c;
}

/*
 * Integer square root, for the two numbers a HUMAN reads: the margin and the
 * imbalance. Both are computed once per frame on core 0, so design §6's "no
 * square roots" — which is about a hot path running twenty thousand times a
 * second — does not reach here. Written out rather than taken from
 * dsp/goertzel.h so that lib/link keeps owing lib/dsp nothing.
 */
static uint32_t isqrt_u64(uint64_t v)
{
    uint64_t root = 0, bit;

    for (bit = 1ull << 31; bit; bit >>= 1) {
        const uint64_t t = root | bit;
        if (t * t <= v) root = t;
    }
    return (uint32_t)root;
}

/*
 * mag^2 to the amplitude-like LSB score every console in this tree prints.
 * The same relation dsp/goertzel.h states: score = 2 * sqrt(mag2) / N.
 */
static uint32_t score_of(uint64_t mag2)
{
    return (uint32_t)((2ull * isqrt_u64(mag2)) / (uint64_t)HANDOFF_GZ_N);
}

/*
 * The 180/200 imbalance, taken off the run that led into this sync. Not a
 * correction — see frame.h. Through an alternating run the negative d's are
 * tone A and the positive ones tone B, so their magnitudes are the two tones
 * measured through the same coupling within a few chips of each other.
 *
 * mag^2, so the percentage is taken on the square root to be an AMPLITUDE
 * ratio: that is what step 4's 9.1 % is stated in, and a reading that could
 * not be compared with it would be worth nothing.
 */
static uint16_t imbalance_pct(const frame_rx_t *r)
{
    uint64_t a, b, root;

    if (!r->pre_na || !r->pre_nb) return 0;
    a = r->pre_a / r->pre_na;
    b = r->pre_b / r->pre_nb;
    if (!a) return 0;

    /* 100 * sqrt(b/a) = sqrt(10000 * b / a), in integers. */
    root = isqrt_u64(b * 10000ull / a);
    return (uint16_t)(root > 0xFFFFu ? 0xFFFFu : root);
}

/* Collect one chip into the imbalance instrument. Hunt and marker only: the
 * body is data, and a run of one tone would bias it. */
static void observe_tone(frame_rx_t *r, frame_chip_t d)
{
    if (d > 0)      { r->pre_b += (uint64_t)d;  r->pre_nb++; }
    else if (d < 0) { r->pre_a += (uint64_t)(-(int64_t)d); r->pre_na++; }
}

/* How many of the last FRAME_ALT_WINDOW transitions alternated. */
static unsigned alt_count(const frame_rx_t *r)
{
    uint32_t v = r->alt_hist & ((1u << FRAME_ALT_WINDOW) - 1u);
    unsigned n = 0;
    while (v) { n += v & 1u; v >>= 1; }
    return n;
}

static frame_rx_result_t finish_body(frame_rx_t *r)
{
    const uint16_t want = crc16(r->body, FRAME_HDR_BYTES + HANDOFF_FRAG_PAYLOAD);
    const uint16_t got  = (uint16_t)(((uint16_t)r->body[FRAME_HDR_BYTES + HANDOFF_FRAG_PAYLOAD] << 8)
                                    | r->body[FRAME_HDR_BYTES + HANDOFF_FRAG_PAYLOAD + 1]);
    const uint64_t mean = r->margin_acc / (FRAME_BODY_BYTES * 8u);

    /*
     * THE MARGIN IS REPORTED IN LSB, not in mag^2, so that a bench number
     * means the same thing it did before step 6. |d(first) - d(second)| is
     * S_A^2 + S_B^2 — both tones, because every Manchester bit holds one of
     * each — so its root is sqrt(2) times the amplitude of one tone where
     * they are equal. v1's margin was one tone's amplitude minus silence.
     * Same units, within that factor; nothing else to know.
     */
    r->last_margin = score_of(mean);
    frame_hdr_unpack(r->body, &r->hdr);
    frame_rx_reset(r);

    if (want == got) { r->frames_good++; return FRAME_RX_GOOD; }

    r->frames_bad_crc++;
    return FRAME_RX_BAD_CRC;
}

frame_rx_result_t frame_rx_push(frame_rx_t *r, frame_chip_t d)
{
    /* THE WHOLE CHIP DECISION. No slicer, no threshold, nothing remembered:
     * tone B louder than tone A, measured in the same window. */
    const uint8_t c = (d > 0) ? 1u : 0u;

    switch (r->state) {
    case FRAME_ST_HUNT:
        observe_tone(r, d);
        if (!r->have_prev) {
            r->prev_chip = c;
            r->have_prev = true;
            r->seen = 1;
            return FRAME_RX_NONE;
        }

        /*
         * A 00 is the only chip-pair violation preamble-plus-marker can
         * produce, and it sits at chips 39 and 40 — see the sync rule in
         * frame.h. Believe it only if enough of the run leading up to it
         * actually alternated.
         */
        if (c == 0u && r->prev_chip == 0u &&
            r->seen > FRAME_ALT_WINDOW && alt_count(r) >= FRAME_ALT_MIN) {
            r->state = FRAME_ST_MARKER;
            r->marker_pos = 0;
        }
        observe(r, c);
        return FRAME_RX_NONE;

    case FRAME_ST_MARKER:
        if (c != k_marker_tail[r->marker_pos]) {
            /*
             * A corrupted preamble chip can manufacture a 00; the tail check
             * is what rejects it. Resume hunting — but KEEP the transition
             * history, because those chips really did happen. Discarding it
             * here was costing the whole frame: a false trigger at chip 24
             * left too few chips before the real marker to rebuild a window,
             * so a single flipped chip anywhere in the second half of the
             * preamble lost the packet.
             */
            r->false_syncs++;
            r->state = FRAME_ST_HUNT;
            observe_tone(r, d);
            observe(r, c);
            return FRAME_RX_NONE;
        }
        observe_tone(r, d);
        observe(r, c);
        if (++r->marker_pos == sizeof k_marker_tail) {
            r->state = FRAME_ST_BODY;
            r->chip_pos = 0;
            r->margin_acc = 0;
            r->last_imbalance_pct = imbalance_pct(r);
            r->syncs++;
        }
        return FRAME_RX_NONE;

    case FRAME_ST_BODY:
    default:
        if ((r->chip_pos & 1u) == 0u) {
            r->first_d = d;
            r->chip_pos++;
            return FRAME_RX_NONE;
        }
        {
            const uint16_t bit_index = (uint16_t)(r->chip_pos >> 1);
            const size_t   byte = bit_index >> 3;
            const int      shift = 7 - (bit_index & 7u);
            const bool     bit = manchester_bit(r->first_d, d);

            r->margin_acc += manchester_margin(r->first_d, d);

            if (byte < FRAME_BODY_BYTES) {
                if (shift == 7) r->body[byte] = 0;
                r->body[byte] = (uint8_t)(r->body[byte] | ((bit ? 1u : 0u) << shift));
            }
            r->chip_pos++;

            if (r->chip_pos >= (uint16_t)(FRAME_BODY_BYTES * 16))
                return finish_body(r);
        }
        return FRAME_RX_NONE;
    }
}

const frame_hdr_t *frame_rx_hdr(const frame_rx_t *r)     { return &r->hdr; }
const uint8_t     *frame_rx_payload(const frame_rx_t *r) { return r->body + FRAME_HDR_BYTES; }

/* MARKER counts as well as BODY: the preamble really did arrive, so somebody
 * is transmitting, and walking away between the marker and the body would
 * throw the frame away just as surely. */
bool frame_rx_busy(const frame_rx_t *r)
{
    return r->state != FRAME_ST_HUNT;
}
