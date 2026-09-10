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
