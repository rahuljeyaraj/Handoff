/*
 * Handoff — parameter sweeps. Development plan M1, architecture §13.
 *
 * Four decisions the documents deliberately left open, each to be closed with
 * a number rather than an argument:
 *
 *   gz_n       50 vs 25 — 3 dB of processing gain against 2x the data rate
 *   payload    fragment payload size, swept against frame loss
 *   carousel   the 0,1,0,2,... weighting, swept against contact duration
 *   guard      windows discarded at each chip boundary
 *   turn       frames sent before handing the channel over
 *
 * gz_n cannot be swept at runtime — it is a compile-time constant that sizes
 * buffers and is _Static_assert-ed against the carrier — so that one is run by
 * building this twice. scripts/test.py --sweep does it.
 *
 *   handoff_sweep gz_n | payload | carousel | guard | turn | all
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chan.h"
#include "frame.h"
#include "sim_twonode.h"
#include "vcard.h"

#define MAX_CHIPS   1024
#define MAX_SAMPLES CHAN_MAX_SAMPLES(MAX_CHIPS)

static int16_t g_samples[MAX_SAMPLES];
static uint8_t g_chips[MAX_CHIPS];

static const char k_card[] =
    "BEGIN:VCARD\r\nVERSION:3.0\r\n"
    "FN:Ada Lovelace\r\n"
    "TEL;TYPE=CELL:+44 7700 900123\r\n"
    "EMAIL;TYPE=INTERNET:ada@gmail.com\r\n"
    "ORG:Analytical Engines Ltd\r\n"
    "TITLE:Programmer\r\n"
    "URL:https://example.com/ada\r\n"
    "END:VCARD\r\n";

/* ---- shared: frame success rate at a given SNR and guard ---------------- */

static double frame_success(double snr_db, uint8_t guard, uint32_t frames)
{
    uint32_t i, good = 0;

    for (i = 0; i < frames; i++) {
        chan_cfg_t cfg;
        frame_hdr_t h;
        uint8_t payload[HANDOFF_FRAG_PAYLOAD];
        frame_rx_t rx;
        gz_t gz;
        sync_t sy;
        size_t nchips, ns, s;

        chan_default(&cfg);
        chan_set_snr_db(&cfg, snr_db);
        cfg.seed = 0x2000u + i * 7717u;

        for (s = 0; s < sizeof payload; s++) payload[s] = (uint8_t)(s * 13u + i);
        h.frag_index = 0; h.frag_count = 1; h.record_id = 1; h.flags = 0;

        nchips = frame_encode(&h, payload, sizeof payload, g_chips, MAX_CHIPS);
        ns = chan_render(&cfg, g_chips, nchips, g_samples, MAX_SAMPLES);

        gz_init(&gz, HANDOFF_GZ_N, HANDOFF_GZ_BIN);
        sync_init(&sy, HANDOFF_WINDOWS_PER_CHIP, guard);
        frame_rx_init(&rx);

        for (s = 0; s < ns; s++) {
            uint32_t score;
            uint16_t chip;
            if (!gz_push(&gz, g_samples[s], &score)) continue;
            if (!sync_push(&sy, score, &chip)) continue;
            if (frame_rx_push(&rx, chip) == FRAME_RX_GOOD &&
                memcmp(frame_rx_payload(&rx), payload, sizeof payload) == 0) {
                good++;
                break;
            }
        }
    }
    return (double)good / (double)frames;
}

/* ---- gz_n -------------------------------------------------------------- */

static void sweep_gz_n(void)
{
    double snr;

    printf("== GZ_N (this build: %d)\n\n", HANDOFF_GZ_N);
    printf("  window %d samples, %d us   bin %d   chip %d us\n",
           HANDOFF_GZ_N, HANDOFF_GZ_N * 1000000 / HANDOFF_ADC_FS_HZ,
           HANDOFF_GZ_BIN, HANDOFF_CHIP_US);
    printf("  chip rate %d/s   data rate %d bps\n", HANDOFF_CHIP_RATE_HZ, HANDOFF_BIT_RATE_BPS);
    printf("  one frame %d chips = %u us of airtime\n\n",
           FRAME_TOTAL_CHIPS, (unsigned)FRAME_AIRTIME_US);

    printf("   SNR   frame success\n");
    printf("   -------------------\n");
    for (snr = -4.0; snr <= 16.001; snr += 2.0)
        printf("  %5.1f      %6.1f%%\n", snr, 100.0 * frame_success(snr, HANDOFF_CHIP_GUARD, 80));

    printf("\n  Build with -DHANDOFF_GZ_N=50 and compare. The decision is not\n");
    printf("  the SNR curve alone: at N=25 a frame costs %u us, so a one-second\n",
           (unsigned)FRAME_AIRTIME_US);
    printf("  contact fits %u frames — architecture 8.1's whole argument.\n",
           (unsigned)(1000000u / FRAME_AIRTIME_US));
}

/* ---- chip guard -------------------------------------------------------- */

static void sweep_guard(void)
{
    uint8_t g;

    printf("== chip guard windows (this build: %d)\n\n", HANDOFF_CHIP_GUARD);
    printf("  guard  integrated  success @ 4 dB  @ 8 dB  @ 12 dB\n");
    printf("  ------------------------------------------------------\n");

    for (g = 0; g * 2u + 1u <= HANDOFF_WINDOWS_PER_CHIP; g++)
        printf("  %5u  %10d  %12.1f%%  %5.1f%%  %6.1f%%\n",
               g, HANDOFF_WINDOWS_PER_CHIP - 2 * g,
               100.0 * frame_success(4.0, g, 60),
               100.0 * frame_success(8.0, g, 60),
               100.0 * frame_success(12.0, g, 60));

    printf("\n  Guarding costs processing gain and buys timing tolerance. It only\n");
    printf("  pays once the far end's clock is genuinely independent, which is\n");
    printf("  M6 — so treat this as the number to re-measure there.\n");
}

/* ---- fragment payload size --------------------------------------------- */

static void sweep_payload(void)
{
    compact_rec_t rec;
    uint8_t blob[COMPACT_MAX_BLOB];
    size_t blen = 0;
    unsigned p;

    vcard_parse(k_card, strlen(k_card), &rec);
    compact_sort_priority(&rec);
    compact_encode(&rec, blob, sizeof blob, &blen);

    printf("== fragment payload size (this build: %d)\n\n", HANDOFF_FRAG_PAYLOAD);
    printf("  the card is %u compact bytes\n\n", (unsigned)blen);
    printf("  payload  frame bytes  chips  airtime   frags  overhead  full pass\n");
    printf("  ------------------------------------------------------------------\n");

    for (p = 8; p <= 64; p += 8) {
        const unsigned body = p + FRAME_HDR_BYTES + FRAME_CRC_BYTES;
        const unsigned chips = FRAME_PREAMBLE_CHIPS + FRAME_MARKER_CHIPS + body * 16u;
        const unsigned us = chips * (unsigned)HANDOFF_CHIP_US;
        /* TLV-aligned packing wastes a little at each boundary; approximate
         * with a 10% allowance, which is what the real splitter costs on this
         * card. */
        const unsigned frags = (unsigned)((blen * 11u / 10u + p - 1u) / p);
        const double overhead = 100.0 * (double)(body - p) / (double)body;

        printf("  %7u  %11u  %5u  %6u   %5u  %7.1f%%  %8u us%s\n",
               p, body, chips, us, frags, overhead, frags * us,
               p == HANDOFF_FRAG_PAYLOAD ? "  <- this build" : "");
    }

    printf("\n  Smaller payloads lose less per corrupted frame and cost more\n");
    printf("  header. The number that matters is the last column against R1's\n");
    printf("  one second of contact, and it wants the largest payload whose\n");
    printf("  frame still survives the channel — measure that at M5, not here.\n");
}

/* ---- carousel weighting ------------------------------------------------ */

static void sweep_carousel(void)
{
    static const char *const k_labels[] = {
        "round robin (default)", "0,1,0,2", "0,0,1,0,0,2", "0x3 then one", "0x4 then one"
    };
    uint8_t w;

    printf("== carousel weighting\n\n");
    printf("  Contact duration against what the far end ends up holding.\n");
    printf("  architecture 8.4's table, measured rather than asserted.\n\n");
    printf("  weight  order              250ms   500ms    1s      2s   complete@1s\n");
    printf("  ----------------------------------------------------------------------\n");

    for (w = 0; w <= 4; w++) {
        static const uint64_t durations[] = { 250000, 500000, 1000000, 2000000 };
        double got[4];
        int complete_1s = 0;
        size_t di;

        for (di = 0; di < 4; di++) {
            int trial, fields = 0;
            const int trials = 24;

            for (trial = 0; trial < trials; trial++) {
                sim_t s;
                link_cfg_t cfg;
                compact_rec_t seen;
                const uint8_t *blob = NULL;
                size_t len;

                link_cfg_default(&cfg);
                cfg.carousel_weight = w;
                sim_init(&s, k_card, k_card, &cfg, (uint64_t)trial * 313u + w + 1u);
                sim_run(&s, durations[di]);

                len = link_sm_received(&s.sm_b, &blob);
                if (len && compact_decode(blob, len, &seen) == COMPACT_OK) fields += seen.n;
                if (durations[di] == 1000000 && frag_rx_complete(&s.sm_b.rx)) complete_1s++;
            }
            got[di] = (double)fields / (double)trials;
        }

        printf("  %6u  %-17s  %5.1f   %5.1f  %5.1f   %5.1f   %3d%%\n",
               w, k_labels[w], got[0], got[1], got[2], got[3],
               complete_1s * 100 / 24);
    }

    printf("\n  Columns are mean fields received. Fragment 0 carries the name and\n");
    printf("  the mobile, so a high weight protects the usable minimum at the\n");
    printf("  cost of ever finishing the card.\n");
}

/* ---- frames per turn ---------------------------------------------------- */

/* Name and mobile, email, organisation: three fragments, the card the post
 * draws. k_card above is four. */
static const char k_card3[] =
    "BEGIN:VCARD\r\nVERSION:3.0\r\n"
    "FN:Rohit Menon\r\n"
    "TEL;TYPE=CELL:+91 98765 43210\r\n"
    "EMAIL:rohit.menon@gmail.com\r\n"
    "ORG:Menon Instruments\r\n"
    "END:VCARD\r\n";

static void sweep_turn_card(const char *card)
{
    static const uint64_t durations[] = { 500000, 1000000, 1500000, 2000000 };
    uint8_t fpt;
    size_t di;

    {
        sim_t s;
        sim_init(&s, card, card, NULL, 1u);
        printf("  a %u-fragment card\n\n", s.rec_a.count);
    }
    printf("  per turn   contact   name+number   whole card\n");
    printf("  ---------------------------------------------\n");

    for (fpt = 1; fpt <= 3; fpt++) {
        for (di = 0; di < 4; di++) {
            int trial, first = 0, whole = 0;
            const int trials = 48;

            for (trial = 0; trial < trials; trial++) {
                sim_t s;
                link_cfg_t cfg;

                link_cfg_default(&cfg);
                cfg.frames_per_turn = fpt;
                sim_init(&s, card, card, &cfg, (uint64_t)trial * 313u + fpt + 1u);
                sim_run(&s, durations[di]);

                first += frag_rx_has(&s.sm_a.rx, 0) + frag_rx_has(&s.sm_b.rx, 0);
                whole += frag_rx_complete(&s.sm_a.rx) + frag_rx_complete(&s.sm_b.rx);
            }
            printf("  %8u  %6lu ms  %10d%%  %10d%%\n", fpt,
                   (unsigned long)(durations[di] / 1000u),
                   first * 100 / (2 * trials), whole * 100 / (2 * trials));
        }
    }
    printf("\n");
}

static void sweep_turn(void)
{
    printf("== frames per turn\n\n");
    printf("  Both ends, both directions: how often each end holds the other's\n");
    printf("  name and number (fragment 0), and how often the whole card.\n\n");
    sweep_turn_card(k_card3);
    sweep_turn_card(k_card);
}

int main(int argc, char **argv)
{
    const char *what = (argc > 1) ? argv[1] : "all";
    const int all = (strcmp(what, "all") == 0);

    if (all || strcmp(what, "gz_n") == 0)     { sweep_gz_n();     printf("\n"); }
    if (all || strcmp(what, "guard") == 0)    { sweep_guard();    printf("\n"); }
    if (all || strcmp(what, "payload") == 0)  { sweep_payload();  printf("\n"); }
    if (all || strcmp(what, "carousel") == 0) { sweep_carousel(); printf("\n"); }
    if (all || strcmp(what, "turn") == 0)     { sweep_turn();     printf("\n"); }

    if (!all && strcmp(what, "gz_n") && strcmp(what, "guard") &&
        strcmp(what, "payload") && strcmp(what, "carousel") && strcmp(what, "turn")) {
        fprintf(stderr, "usage: %s [gz_n|guard|payload|carousel|turn|all]\n", argv[0]);
        return 2;
    }
    return 0;
}
