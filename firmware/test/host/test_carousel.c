#include "carousel.h"
#include "frame.h"
#include "hf_test.h"
#include "tests.h"

void test_carousel(void)
{
    /* architecture §8.4 states the order literally: 0, 1, 0, 2, 0, 3, 0, 1... */
    /*
     * architecture §8.4's order, still reachable — it is just no longer the
     * default. See the note on CAROUSEL_DEFAULT_WEIGHT for the measurement
     * that changed it.
     */
    hf_begin("carousel: weight 1 gives architecture 8.4's order, verbatim");
    {
        const uint8_t want[] = { 0,1, 0,2, 0,3, 0,1, 0,2, 0,3 };
        carousel_t c;
        size_t i;

        carousel_init(&c, 4, 1);
        for (i = 0; i < sizeof want; i++)
            HF_EQ_INT(carousel_next(&c), want[i]);
    }

    hf_begin("carousel: weight 1 gives fragment 0 half the airtime");
    {
        carousel_t c;
        int i, zeros = 0;
        carousel_init(&c, 5, 1);
        for (i = 0; i < 1000; i++) if (carousel_next(&c) == 0) zeros++;
        HF_CHECK_MSG(zeros == 500, "fragment 0 got %d of 1000 slots", zeros);
    }

    hf_begin("carousel: a heavier weight biases further toward fragment 0");
    {
        carousel_t c;
        int i, zeros = 0;
        carousel_init(&c, 5, 3);
        for (i = 0; i < 1000; i++) if (carousel_next(&c) == 0) zeros++;
        HF_CHECK_MSG(zeros == 750, "fragment 0 got %d of 1000 slots", zeros);
    }

    /* The default since M1's sweep: see CAROUSEL_DEFAULT_WEIGHT. */
    hf_begin("carousel: weight 0 is plain round robin over every fragment");
    {
        const uint8_t want[] = { 0, 1, 2, 3, 0, 1, 2, 3 };
        carousel_t c;
        size_t i;
        carousel_init(&c, 4, 0);
        for (i = 0; i < sizeof want; i++)
            HF_EQ_INT(carousel_next(&c), want[i]);
    }

    hf_begin("carousel: a single-fragment record only ever sends fragment 0");
    {
        carousel_t c;
        int i;
        carousel_init(&c, 1, CAROUSEL_DEFAULT_WEIGHT);
        for (i = 0; i < 20; i++) HF_EQ_INT(carousel_next(&c), 0);
        HF_CHECK(carousel_full_pass(&c));
    }

    /*
     * FRAME_FLAG_LAST_PASS advertises that every fragment has been sent at
     * least once, which is how the far end knows waiting longer will not help.
     */
    hf_begin("carousel: a full pass is only claimed once every fragment has gone");
    {
        carousel_t c;
        int i;
        carousel_init(&c, 4, 1);
        for (i = 0; i < 5; i++) {
            carousel_next(&c);
            HF_CHECK_MSG(!carousel_full_pass(&c), "claimed a full pass after %d sends", i + 1);
        }
        carousel_next(&c);            /* the sixth send is fragment 3 */
        HF_CHECK(carousel_full_pass(&c));
    }

    /*
     * The reason the weighting is a parameter rather than a constant: the
     * sweep at M1 has to be able to move it. Confirm every fragment is still
     * reached at any weight, so a heavy bias cannot starve the tail.
     */
    hf_begin("carousel: no weight starves a fragment");
    {
        uint8_t w;
        for (w = 0; w <= 6; w++) {
            carousel_t c;
            int seen[FRAME_MAX_FRAGS];
            int i, missing = 0;

            for (i = 0; i < FRAME_MAX_FRAGS; i++) seen[i] = 0;
            carousel_init(&c, 8, w);
            for (i = 0; i < 400; i++) seen[carousel_next(&c)] = 1;
            for (i = 0; i < 8; i++) if (!seen[i]) missing++;
            HF_CHECK_MSG(missing == 0, "weight %u starved %d fragments", w, missing);
        }
    }
}
