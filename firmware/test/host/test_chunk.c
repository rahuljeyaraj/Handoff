/*
 * Chunked transport for the phone link. architecture §11.2.
 *
 * Development plan M2's fourth exit criterion is "chunked reassembly works at
 * the 23-byte ATT MTU floor, not just at whatever MTU your phone happens to
 * negotiate". That is an end-to-end criterion, but the half of it that can
 * fail silently — the framing itself — is proved here, at every capacity from
 * the floor upwards, against every corruption the link can actually produce.
 */
#include <string.h>

#include "chunk.h"
#include "hf_test.h"
#include "tests.h"

/* ATT_MTU 23 minus the 3-byte notification header. This is the number that
 * matters: 18 payload bytes per chunk once chunk.h's 2 come off. */
#define FLOOR_CHUNK_BYTES 20

static size_t round_trip(const uint8_t *msg, size_t len, size_t chunk_bytes,
                         uint8_t *out, size_t out_max, uint8_t *chunks_out)
{
    chunk_tx_t tx;
    chunk_rx_t rx;
    uint8_t buf[512];
    size_t n, got = 0;
    uint8_t chunks = 0;
    chunk_res_t r = CHUNK_MORE;

    HF_EQ_INT(chunk_tx_init(&tx, msg, len, chunk_bytes), CHUNK_MORE);
    chunk_rx_init(&rx);

    while ((n = chunk_tx_next(&tx, buf, sizeof buf)) != 0) {
        HF_CHECK(n <= chunk_bytes);
        r = chunk_rx_push(&rx, buf, n);
        chunks++;
        HF_CHECK(r >= 0);
        if (r < 0) break;
    }

    HF_EQ_INT(r, CHUNK_COMPLETE);
    HF_EQ_INT(chunks, chunk_tx_total(&tx));

    if (r == CHUNK_COMPLETE) {
        const uint8_t *p = chunk_rx_data(&rx, &got);
        HF_CHECK(p != NULL);
        HF_CHECK(got <= out_max);
        if (p && got <= out_max) memcpy(out, p, got);
    }
    if (chunks_out) *chunks_out = chunks;
    return got;
}

static void fill(uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) p[i] = (uint8_t)(i * 7u + 3u);
}

/* ---------------------------------------------------------------------- */

static void round_trips_at_every_capacity(void)
{
    /* A realistic provisioning card is ~176 bytes of text; 300 covers the
     * awkward ones scripts/test.py cross-checks the codec on. */
    static const size_t lengths[] = { 0, 1, 17, 18, 19, 36, 37, 176, 300 };
    uint8_t msg[300], back[300];
    size_t cap, li;

    fill(msg, sizeof msg);

    /* Every chunk size from the ATT floor to a fully negotiated 247-byte MTU.
     * The receiver is never told which was used. */
    for (cap = FLOOR_CHUNK_BYTES; cap <= 244u; cap++) {
        for (li = 0; li < sizeof lengths / sizeof lengths[0]; li++) {
            size_t len = lengths[li];
            size_t got;

            memset(back, 0xAA, sizeof back);
            got = round_trip(msg, len, cap, back, sizeof back, NULL);

            HF_CHECK_MSG(got == len, "cap %u len %u: got %u bytes",
                         (unsigned)cap, (unsigned)len, (unsigned)got);
            if (got == len && len) HF_EQ_MEM(back, msg, len);
        }
    }
}

static void chunk_count_at_the_floor(void)
{
    uint8_t msg[300], back[300], chunks = 0;
    chunk_tx_t tx;

    fill(msg, sizeof msg);

    /* 18 payload bytes per chunk at ATT_MTU 23. */
    round_trip(msg, 176, FLOOR_CHUNK_BYTES, back, sizeof back, &chunks);
    HF_EQ_INT(chunks, (176 + 17) / 18);

    /* Exactly one chunk's worth is one chunk, not two. */
    round_trip(msg, 18, FLOOR_CHUNK_BYTES, back, sizeof back, &chunks);
    HF_EQ_INT(chunks, 1);

    /* An empty message is one empty chunk — "the phone cleared my card" has
     * to be distinguishable from "the phone sent nothing". */
    round_trip(msg, 0, FLOOR_CHUNK_BYTES, back, sizeof back, &chunks);
    HF_EQ_INT(chunks, 1);

    HF_EQ_INT(chunk_tx_init(&tx, msg, 0, FLOOR_CHUNK_BYTES), CHUNK_MORE);
    HF_EQ_INT(chunk_tx_total(&tx), 1);
}

static void tx_rejects_impossible_geometry(void)
{
    chunk_tx_t tx;
    uint8_t msg[8];

    /* A chunk with no room for a payload is not a chunk. */
    HF_EQ_INT(chunk_tx_init(&tx, msg, sizeof msg, CHUNK_HDR_BYTES), CHUNK_ERR_HDR);
    HF_EQ_INT(chunk_tx_init(&tx, msg, sizeof msg, 0), CHUNK_ERR_HDR);

    /* 255 chunks is the ceiling the 1-byte total imposes. At the floor that
     * is 4590 bytes, well past anything the contract carries. */
    HF_EQ_INT(chunk_tx_init(&tx, msg, 255u * 18u, FLOOR_CHUNK_BYTES), CHUNK_MORE);
    HF_EQ_INT(chunk_tx_init(&tx, msg, 255u * 18u + 1u, FLOOR_CHUNK_BYTES),
              CHUNK_ERR_SIZE);
}

static void out_of_order_is_refused_not_spliced(void)
{
    chunk_rx_t rx;
    uint8_t c0[FLOOR_CHUNK_BYTES], c1[FLOOR_CHUNK_BYTES], c2[FLOOR_CHUNK_BYTES];

    memset(c0, 0x11, sizeof c0); c0[0] = 0; c0[1] = 3;
    memset(c1, 0x22, sizeof c1); c1[0] = 1; c1[1] = 3;
    memset(c2, 0x33, sizeof c2); c2[0] = 2; c2[1] = 3;

    /* A skipped chunk must not become a shorter card with a hole in it. */
    chunk_rx_init(&rx);
    HF_EQ_INT(chunk_rx_push(&rx, c0, sizeof c0), CHUNK_MORE);
    HF_EQ_INT(chunk_rx_push(&rx, c2, sizeof c2), CHUNK_ERR_SEQ);
    HF_CHECK(chunk_rx_data(&rx, NULL) == NULL);

    /* And the reset is real: the assembler does not resume mid-message. */
    HF_EQ_INT(chunk_rx_push(&rx, c1, sizeof c1), CHUNK_ERR_SEQ);

    /* Recovery is always "start again at seq 0", with nothing else needed. */
    HF_EQ_INT(chunk_rx_push(&rx, c0, sizeof c0), CHUNK_MORE);
    HF_EQ_INT(chunk_rx_push(&rx, c1, sizeof c1), CHUNK_MORE);
    HF_EQ_INT(chunk_rx_push(&rx, c2, sizeof c2), CHUNK_COMPLETE);
}

static void a_restart_discards_the_abandoned_message(void)
{
    chunk_rx_t rx;
    uint8_t c0[FLOOR_CHUNK_BYTES], again[4];
    const uint8_t *p;
    size_t len = 0;

    memset(c0, 0x55, sizeof c0); c0[0] = 0; c0[1] = 4;

    chunk_rx_init(&rx);
    HF_EQ_INT(chunk_rx_push(&rx, c0, sizeof c0), CHUNK_MORE);

    /* The app died halfway and reconnected. seq 0 of a one-chunk message. */
    again[0] = 0; again[1] = 1; again[2] = 'h'; again[3] = 'i';
    HF_EQ_INT(chunk_rx_push(&rx, again, sizeof again), CHUNK_COMPLETE);

    p = chunk_rx_data(&rx, &len);
    HF_EQ_INT(len, 2);
    HF_CHECK(p && p[0] == 'h' && p[1] == 'i');
}

static void malformed_headers(void)
{
    chunk_rx_t rx;
    uint8_t c[8];

    chunk_rx_init(&rx);

    /* Shorter than the header. */
    HF_EQ_INT(chunk_rx_push(&rx, c, 0), CHUNK_ERR_HDR);
    HF_EQ_INT(chunk_rx_push(&rx, c, 1), CHUNK_ERR_HDR);

    /* total 0 is not a message. */
    c[0] = 0; c[1] = 0;
    HF_EQ_INT(chunk_rx_push(&rx, c, 4), CHUNK_ERR_HDR);

    /* seq past the end of its own message. */
    c[0] = 3; c[1] = 3;
    HF_EQ_INT(chunk_rx_push(&rx, c, 4), CHUNK_ERR_HDR);
}

static void a_ragged_middle_chunk_is_refused(void)
{
    chunk_rx_t rx;
    uint8_t c0[FLOOR_CHUNK_BYTES], c1[FLOOR_CHUNK_BYTES];

    memset(c0, 0x11, sizeof c0); c0[0] = 0; c0[1] = 3;
    memset(c1, 0x22, sizeof c1); c1[0] = 1; c1[1] = 3;

    /*
     * The failure this exists for: a client that sends its last partial chunk
     * in the middle. Accepting it shifts every later byte and still yields a
     * plausible-looking vCard — a wrong phone number rather than a rejection.
     */
    chunk_rx_init(&rx);
    HF_EQ_INT(chunk_rx_push(&rx, c0, sizeof c0), CHUNK_MORE);
    HF_EQ_INT(chunk_rx_push(&rx, c1, sizeof c1 - 3), CHUNK_ERR_RAGGED);

    /* The last chunk being short is the normal case, and stays legal. */
    chunk_rx_init(&rx);
    c0[1] = 2;
    c1[1] = 2;
    HF_EQ_INT(chunk_rx_push(&rx, c0, sizeof c0), CHUNK_MORE);
    HF_EQ_INT(chunk_rx_push(&rx, c1, sizeof c1 - 3), CHUNK_COMPLETE);
}

static void an_oversized_message_is_refused_on_the_first_chunk(void)
{
    chunk_rx_t rx;
    uint8_t c[FLOOR_CHUNK_BYTES];

    /* 255 x 18 = 4590 bytes, well past CHUNK_RX_MAX. A phone cannot make the
     * wristband fill a kilobyte before finding out. */
    memset(c, 0x77, sizeof c);
    c[0] = 0; c[1] = 255;

    chunk_rx_init(&rx);
    HF_EQ_INT(chunk_rx_push(&rx, c, sizeof c), CHUNK_ERR_SIZE);
    HF_EQ_INT(chunk_rx_received(&rx), 0);
}

static void progress_is_reportable(void)
{
    chunk_rx_t rx;
    uint8_t c[FLOOR_CHUNK_BYTES];

    memset(c, 0x99, sizeof c);
    c[1] = 3;

    chunk_rx_init(&rx);
    HF_EQ_INT(chunk_rx_received(&rx), 0);
    HF_EQ_INT(chunk_rx_total(&rx), 0);

    c[0] = 0; chunk_rx_push(&rx, c, sizeof c);
    HF_EQ_INT(chunk_rx_received(&rx), 1);
    HF_EQ_INT(chunk_rx_total(&rx), 3);

    c[0] = 1; chunk_rx_push(&rx, c, sizeof c);
    HF_EQ_INT(chunk_rx_received(&rx), 2);
}

void test_chunk(void)
{
    hf_begin("chunk: round trip at every capacity");
    round_trips_at_every_capacity();

    hf_begin("chunk: chunk count at the ATT floor");
    chunk_count_at_the_floor();

    hf_begin("chunk: sender refuses impossible geometry");
    tx_rejects_impossible_geometry();

    hf_begin("chunk: out of order is refused, not spliced");
    out_of_order_is_refused_not_spliced();

    hf_begin("chunk: a restart discards the abandoned message");
    a_restart_discards_the_abandoned_message();

    hf_begin("chunk: malformed headers");
    malformed_headers();

    hf_begin("chunk: a ragged middle chunk is refused");
    a_ragged_middle_chunk_is_refused();

    hf_begin("chunk: oversized message refused on the first chunk");
    an_oversized_message_is_refused_on_the_first_chunk();

    hf_begin("chunk: reassembly progress is reportable");
    progress_is_reportable();
}
