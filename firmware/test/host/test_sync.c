#include <stdlib.h>

#include "chan.h"
#include "hf_test.h"
#include "sync.h"
#include "tests.h"

/* An alternating chip pattern is the worst case for a timing tracker and the
 * best case for an edge detector: it is exactly what the preamble is. */
static void alternating(uint8_t *chips, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) chips[i] = (uint8_t)(i & 1u);
}

void test_sync(void)
{
    hf_begin("sync: integrates a chip and drops the guard windows");
    {
        sync_t s;
        uint16_t chip = 0;
        int i;
        int emitted = 0;

        sync_init(&s, 5, 1);
        /* 1000 in the middle three windows, 0 at each end. With guard = 1 the
         * ends are discarded, so the chip energy is the middle mean. */
        for (i = 0; i < 5; i++) {
            const uint32_t score = (i == 0 || i == 4) ? 0u : 1000u;
            if (sync_push(&s, score, &chip)) emitted++;
        }
        HF_EQ_INT(emitted, 1);
        HF_EQ_INT(chip, 1000);
    }

    hf_begin("sync: guard = 0 integrates every window");
    {
        sync_t s;
        uint16_t chip = 0;
        int i;
        sync_init(&s, 5, 0);
        for (i = 0; i < 5; i++) sync_push(&s, (i == 0 || i == 4) ? 0u : 1000u, &chip);
        HF_EQ_INT(chip, 600);   /* (0+1000+1000+1000+0)/5 */
    }

    hf_begin("sync: one chip out per windows_per_chip in");
    {
        sync_t s;
        uint16_t chip;
        int i, n = 0;
        sync_init(&s, HANDOFF_WINDOWS_PER_CHIP, HANDOFF_CHIP_GUARD);
        for (i = 0; i < 100 * HANDOFF_WINDOWS_PER_CHIP; i++)
            if (sync_push(&s, (uint32_t)((i / HANDOFF_WINDOWS_PER_CHIP) & 1u) * 1000u, &chip))
                n++;
        /* Timing corrections lengthen or shorten individual chips, so allow a
         * couple either way over a hundred. */
        HF_CHECK_MSG(n >= 98 && n <= 102, "emitted %d chips from 100", n);
    }

    /*
     * The real job: a receiver sampling a transmitter whose chip clock is
     * wrong by 200 ppm. Development plan M1 asks for that to be four times
     * worse than two real crystals and still work. Feed the tracker a
     * deliberately misaligned stream and check it walks the boundary back.
     */
    hf_begin("sync: recovers from a half-chip initial misalignment");
    {
        sync_t s;
        uint8_t chips[64];
        uint16_t out;
        int i, w;
        int offset = HANDOFF_WINDOWS_PER_CHIP / 2;

        alternating(chips, sizeof chips);
        sync_init(&s, HANDOFF_WINDOWS_PER_CHIP, HANDOFF_CHIP_GUARD);

        for (i = 0; i < (int)sizeof chips; i++) {
            for (w = 0; w < HANDOFF_WINDOWS_PER_CHIP; w++) {
                const int idx = i * HANDOFF_WINDOWS_PER_CHIP + w - offset;
                const int ci = idx < 0 ? 0 : idx / HANDOFF_WINDOWS_PER_CHIP;
                sync_push(&s, chips[ci % sizeof chips] ? 1000u : 20u, &out);
            }
        }
        HF_CHECK_MSG(abs(sync_phase_error(&s)) <= 1,
                     "phase error %d after 64 chips", sync_phase_error(&s));
    }

    /*
     * End to end through the sample-level channel, which is the only way to
     * exercise a genuine sample-clock offset rather than a whole-window one.
     * +-200 ppm is four times worse than two real crystals (development plan
     * M1 exit criteria).
     */
    hf_begin("sync: tracks +-200 ppm of chip clock offset");
    {
        int sign;
        for (sign = -1; sign <= 1; sign += 2) {
            chan_cfg_t c;
            demod_t d;
            uint8_t chips[256];
            static int16_t samples[CHAN_MAX_SAMPLES(256)];
            frame_chip_t out[300];
            size_t ns, nc, i;
            int errors = 0;

            chan_default(&c);
            c.clock_ppm = 200.0 * sign;
            chan_set_snr_db(&c, 20.0);
            alternating(chips, sizeof chips);

            ns = chan_render(&c, chips, sizeof chips, samples, sizeof samples / sizeof samples[0]);
            HF_CHECK(ns > 0);

            demod_init(&d);
            nc = demod_run(&d, samples, ns, out, sizeof out / sizeof out[0]);
            HF_CHECK_MSG(nc + 4 > sizeof chips, "only %u chips out of %u",
                         (unsigned)nc, (unsigned)(sizeof chips));

            /*
             * Alternation, not absolute phase: which input chip lands in
             * out[0] depends on acquisition, but a slip shows up immediately
             * as two consecutive chips at the same level. Skip the transient
             * first — the tracker needs a few chips to find the boundary.
             */
            /* Stop before the burst tail: those chips are silence by design
             * (see chan_cfg_t.tail_chips) and do not alternate. */
            for (i = 21; i + 1 < nc && i + 1 < sizeof chips; i++) {
                const int high = out[i] > out[i + 1];
                if (i > 21 && high == (int)(out[i - 1] > out[i])) errors++;
            }
            HF_CHECK_MSG(errors == 0, "%d chip errors at %+d ppm", errors, 200 * sign);
        }
    }
}
