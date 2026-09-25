#include "hf_test.h"
#include "manchester.h"
#include "tests.h"

void test_manchester(void)
{
    hf_begin("manchester: a mark is on-then-off");
    {
        const uint8_t b = 0x80;   /* 1000 0000 */
        uint8_t chips[16];
        HF_EQ_INT(manchester_encode(&b, 1, chips, sizeof chips), 16);
        HF_EQ_INT(chips[0], 1);   /* the mark   */
        HF_EQ_INT(chips[1], 0);
        HF_EQ_INT(chips[2], 0);   /* the spaces */
        HF_EQ_INT(chips[3], 1);
    }

    /*
     * The exact chip sequence the sync rule in frame.h depends on. If this
     * changes, the preamble is no longer chip-phase resolvable and the framer
     * stops working — so it is pinned here rather than left implicit.
     */
    hf_begin("manchester: the start marker encodes to 1010101001010101");
    {
        const uint8_t marker = 0xF0;
        const uint8_t want[16] = { 1,0, 1,0, 1,0, 1,0, 0,1, 0,1, 0,1, 0,1 };
        uint8_t chips[16];
        HF_EQ_INT(manchester_encode(&marker, 1, chips, sizeof chips), 16);
        HF_EQ_MEM(chips, want, sizeof want);
    }

    hf_begin("manchester: round trip over every byte value");
    {
        int v;
        for (v = 0; v < 256; v++) {
            const uint8_t in = (uint8_t)v;
            uint8_t chips[16], out = 0;
            manchester_encode(&in, 1, chips, sizeof chips);
            HF_EQ_INT(manchester_decode_chips(chips, 16, &out, 1), 1);
            HF_EQ_INT(out, in);
        }
    }

    hf_begin("manchester: round trip over a whole frame body");
    {
        uint8_t in[36], out[36], chips[36 * 16];
        size_t i;
        for (i = 0; i < sizeof in; i++) in[i] = (uint8_t)(i * 11u + 5u);
        HF_EQ_INT(manchester_encode(in, sizeof in, chips, sizeof chips), sizeof chips);
        HF_EQ_INT(manchester_decode_chips(chips, sizeof chips, out, sizeof out), sizeof out);
        HF_EQ_MEM(out, in, sizeof in);
    }

    hf_begin("manchester: encode refuses a buffer that is one chip short");
    {
        const uint8_t b = 0x5A;
        uint8_t chips[15];
        HF_EQ_INT(manchester_encode(&b, 1, chips, sizeof chips), 0);
    }

    /*
     * MANCHESTER_MAX_RUN_CHIPS, checked rather than argued.
     *
     * link_sm.c's OOK bridge is exactly this long, because on v1 a run of
     * identical chips at level 0 is a run of silence and the bridge has to
     * outlast it. If this number were ever wrong the bridge would be too
     * short, the receiver would read a live frame as a free channel, and
     * nothing here would say so — which is why it is walked rather than
     * reasoned about.
     *
     * Every byte, so every bit pair, including across the byte boundary.
     */
    hf_begin("manchester: no run of identical chips exceeds the stated maximum");
    {
        uint8_t bytes[2], chips[32];
        unsigned a_byte, b_byte;
        size_t i;
        int worst = 0;

        for (a_byte = 0; a_byte < 256u; a_byte++) {
            for (b_byte = 0; b_byte < 256u; b_byte++) {
                int run = 1;

                bytes[0] = (uint8_t)a_byte;
                bytes[1] = (uint8_t)b_byte;
                if (manchester_encode(bytes, 2, chips, sizeof chips) != 32u) {
                    HF_CHECK_MSG(0, "encode refused %02x %02x", a_byte, b_byte);
                    break;
                }
                for (i = 1; i < 32u; i++) {
                    run = (chips[i] == chips[i - 1]) ? run + 1 : 1;
                    if (run > worst) worst = run;
                }
            }
        }
        HF_EQ_INT(worst, MANCHESTER_MAX_RUN_CHIPS);
    }

    /*
     * The decision is relative, never against an absolute threshold — that is
     * the whole reason design §9.2 chose Manchester. A bit whose two halves
     * are 30 and 20 decodes the same as one whose halves are 3000 and 2000.
     */
    hf_begin("manchester: the decision is amplitude-independent");
    {
        HF_CHECK(manchester_bit(30, 20) == true);
        HF_CHECK(manchester_bit(3000, 2000) == true);
        HF_CHECK(manchester_bit(20, 30) == false);
        HF_CHECK(manchester_bit(2000, 3000) == false);
        HF_EQ_INT(manchester_margin(3000, 2000), 1000);
        HF_EQ_INT(manchester_margin(2000, 3000), 1000);
    }
}
