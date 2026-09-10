#include <string.h>

#include "chan.h"
#include "frame.h"
#include "hf_test.h"
#include "tests.h"

#define E_ON  1000u
#define E_OFF 20u

static void push_chips(frame_rx_t *r, const uint8_t *chips, size_t n,
                       frame_rx_result_t *last, int *good)
{
    size_t i;
    for (i = 0; i < n; i++) {
        const frame_rx_result_t res = frame_rx_push(r, chips[i] ? E_ON : E_OFF);
        if (res != FRAME_RX_NONE) { *last = res; if (res == FRAME_RX_GOOD) (*good)++; }
    }
}

/* Silence before a frame, so the framer starts from a realistic state. */
static void push_silence(frame_rx_t *r, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) frame_rx_push(r, E_OFF);
}

void test_frame(void)
{
    hf_begin("frame: header packs and unpacks every field");
    {
        frame_hdr_t in, out;
        uint8_t w[FRAME_HDR_BYTES];
        int idx, cnt, rid, fl;

        for (idx = 0; idx < 16; idx += 5)
        for (cnt = 1; cnt <= 16; cnt += 5)
        for (rid = 0; rid < 64; rid += 21)
        for (fl = 0; fl < 4; fl++) {
            in.frag_index = (uint8_t)idx;
            in.frag_count = (uint8_t)cnt;
            in.record_id = (uint8_t)rid;
            in.flags = (uint8_t)fl;
            frame_hdr_pack(&in, w);
            frame_hdr_unpack(w, &out);
            HF_EQ_INT(out.frag_index, in.frag_index);
            HF_EQ_INT(out.frag_count, in.frag_count);
            HF_EQ_INT(out.record_id, in.record_id);
            HF_EQ_INT(out.flags, in.flags);
        }
    }

    /*
     * The sync rule, asserted directly against the wire. There must be exactly
     * one 00 pair in preamble-plus-marker, it must sit at chips 39 and 40, and
     * the payload must start eight chips later. Everything in frame_rx_push
     * follows from these three facts.
     */
    hf_begin("frame: the preamble and marker contain exactly one 00");
    {
        frame_hdr_t h = { 0, 1, 0, 0 };
        uint8_t chips[FRAME_TOTAL_CHIPS];
        size_t i, doubles = 0, first = 0;

        HF_EQ_INT(frame_encode(&h, NULL, 0, chips, sizeof chips), FRAME_TOTAL_CHIPS);

        for (i = 1; i < FRAME_PREAMBLE_CHIPS + FRAME_MARKER_CHIPS; i++) {
            if (chips[i] == chips[i - 1]) {
                if (!doubles) first = i - 1;
                doubles++;
                HF_EQ_INT(chips[i], 0);   /* it is a 00, never a 11 */
            }
        }
        HF_EQ_INT(doubles, 1);
        HF_EQ_INT(first, 39);
        HF_EQ_INT(FRAME_PREAMBLE_CHIPS + FRAME_MARKER_CHIPS, (int)first + 9);
    }

    hf_begin("frame: clean round trip");
    {
        frame_hdr_t h = { 3, 5, 42, FRAME_FLAG_HAVE_YOURS };
        uint8_t payload[HANDOFF_FRAG_PAYLOAD];
        uint8_t chips[FRAME_TOTAL_CHIPS];
        frame_rx_t r;
        frame_rx_result_t last = FRAME_RX_NONE;
        int good = 0;
        size_t i;

        for (i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(i * 3u + 1u);
        HF_EQ_INT(frame_encode(&h, payload, sizeof payload, chips, sizeof chips),
                  FRAME_TOTAL_CHIPS);

        frame_rx_init(&r);
        push_silence(&r, 40);
        push_chips(&r, chips, sizeof chips, &last, &good);

        HF_EQ_INT(last, FRAME_RX_GOOD);
        HF_EQ_INT(good, 1);
        HF_EQ_INT(frame_rx_hdr(&r)->frag_index, 3);
        HF_EQ_INT(frame_rx_hdr(&r)->frag_count, 5);
        HF_EQ_INT(frame_rx_hdr(&r)->record_id, 42);
        HF_EQ_INT(frame_rx_hdr(&r)->flags, FRAME_FLAG_HAVE_YOURS);
        HF_EQ_MEM(frame_rx_payload(&r), payload, sizeof payload);
    }

    hf_begin("frame: a short payload is NOP-padded and still checks out");
    {
        frame_hdr_t h = { 0, 1, 7, 0 };
        const uint8_t payload[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
        uint8_t chips[FRAME_TOTAL_CHIPS];
        frame_rx_t r;
        frame_rx_result_t last = FRAME_RX_NONE;
        int good = 0;
        size_t i;

        frame_encode(&h, payload, sizeof payload, chips, sizeof chips);
        frame_rx_init(&r);
        push_chips(&r, chips, sizeof chips, &last, &good);

        HF_EQ_INT(last, FRAME_RX_GOOD);
        HF_EQ_MEM(frame_rx_payload(&r), payload, sizeof payload);
        for (i = sizeof payload; i < HANDOFF_FRAG_PAYLOAD; i++)
            HF_EQ_INT(frame_rx_payload(&r)[i], 0);
    }

    hf_begin("frame: back-to-back frames both decode");
    {
        frame_hdr_t h = { 1, 2, 9, 0 };
        uint8_t chips[FRAME_TOTAL_CHIPS];
        frame_rx_t r;
        frame_rx_result_t last = FRAME_RX_NONE;
        int good = 0;

        frame_encode(&h, NULL, 0, chips, sizeof chips);
        frame_rx_init(&r);
        push_chips(&r, chips, sizeof chips, &last, &good);
        push_chips(&r, chips, sizeof chips, &last, &good);
        HF_EQ_INT(good, 2);
    }

    hf_begin("frame: a corrupted payload chip fails CRC rather than decoding");
    {
        frame_hdr_t h = { 0, 1, 1, 0 };
        uint8_t payload[HANDOFF_FRAG_PAYLOAD];
        uint8_t chips[FRAME_TOTAL_CHIPS];
        frame_rx_t r;
        frame_rx_result_t last = FRAME_RX_NONE;
        int good = 0;
        size_t i;

        for (i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)i;
        frame_encode(&h, payload, sizeof payload, chips, sizeof chips);

        /*
         * Swap the two chips of one bit, which always inverts it. Flipping a
         * single chip does not: it turns 01 into 11, and the decision rule
         * takes the first chip, so half the time the bit survives — which is
         * a property of Manchester worth knowing rather than a weakness to
         * test around.
         */
        {
            const size_t b = FRAME_PREAMBLE_CHIPS + FRAME_MARKER_CHIPS + 100;
            const uint8_t t = chips[b];
            chips[b] = chips[b + 1];
            chips[b + 1] = t;
        }

        frame_rx_init(&r);
        push_chips(&r, chips, sizeof chips, &last, &good);
        HF_EQ_INT(last, FRAME_RX_BAD_CRC);
        HF_EQ_INT(good, 0);
    }

    /*
     * Development plan M1 asks for this one by name: "test it against a
     * preamble corrupted at the last chip". A corrupted preamble chip is the
     * one thing that can manufacture a false 00, and the seven-chip tail check
     * is the only thing standing between that and a frame decoded at the wrong
     * offset. Try every single-chip corruption in the preamble and marker and
     * require that none of them produces a frame the CRC accepts.
     */
    hf_begin("frame: no single preamble corruption yields a false decode");
    {
        frame_hdr_t h = { 2, 4, 33, 0 };
        uint8_t payload[HANDOFF_FRAG_PAYLOAD];
        uint8_t clean[FRAME_TOTAL_CHIPS];
        size_t i, k;
        int false_accepts = 0, recovered = 0;

        for (i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(i ^ 0x5Au);
        frame_encode(&h, payload, sizeof payload, clean, sizeof clean);

        for (k = 0; k < FRAME_PREAMBLE_CHIPS + FRAME_MARKER_CHIPS; k++) {
            uint8_t chips[FRAME_TOTAL_CHIPS];
            frame_rx_t r;
            frame_rx_result_t last = FRAME_RX_NONE;
            int good = 0;

            memcpy(chips, clean, sizeof chips);
            chips[k] ^= 1u;

            frame_rx_init(&r);
            push_silence(&r, 64);
            push_chips(&r, chips, sizeof chips, &last, &good);

            if (good) {
                /* Decoding anyway is fine — as long as it decoded correctly. */
                if (memcmp(frame_rx_payload(&r), payload, sizeof payload) == 0 &&
                    frame_rx_hdr(&r)->frag_index == h.frag_index)
                    recovered++;
                else
                    false_accepts++;
            }
        }
        HF_CHECK_MSG(false_accepts == 0,
                     "%d corrupted preambles produced a wrong frame that passed CRC",
                     false_accepts);

        /*
         * Nine of the 48 positions are structurally unrecoverable, and it is
         * worth being precise about which rather than picking a loose bound:
         *
         *   39, 40   the 00 itself — corrupt either and the marker is gone
         *   41..47   the seven-chip tail the detector verifies
         *
         * Everything else — the whole preamble and the marker's leading half —
         * must survive a single flipped chip.
         */
        HF_CHECK_MSG(recovered >= 39, "only %d of %d recovered", recovered,
                     (int)(FRAME_PREAMBLE_CHIPS + FRAME_MARKER_CHIPS));
    }

    hf_begin("frame: pure noise never syncs into a passing frame");
    {
        frame_rx_t r;
        rng_t rng;
        int i, good = 0;

        frame_rx_init(&r);
        rng_seed(&rng, 7);
        for (i = 0; i < 2000000; i++)
            if (frame_rx_push(&r, (uint16_t)(rng_u32(&rng) % 1024u)) == FRAME_RX_GOOD)
                good++;
        HF_CHECK_MSG(good == 0, "%d frames decoded from noise", good);
    }

    hf_begin("frame: airtime matches the configured rate");
    {
        /* A sanity net over config.h: if someone changes GZ_N and the derived
         * chip rate stops being what the carousel budget assumed, this moves. */
        HF_EQ_INT(FRAME_TOTAL_CHIPS,
                  FRAME_PREAMBLE_CHIPS + FRAME_MARKER_CHIPS + FRAME_BODY_BYTES * 16);
        HF_CHECK_MSG(FRAME_AIRTIME_US < 400000u,
                     "one frame is %u us — more than a handshake can afford",
                     (unsigned)FRAME_AIRTIME_US);
    }
}
