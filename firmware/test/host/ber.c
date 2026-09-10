/*
 * Handoff — BER harness. Development plan M1.
 *
 * Sweeps SNR and prints a curve. This is the number M5 has to agree with:
 * "agreement between bench and simulator is the real deliverable" — it is what
 * lets the simulator be trusted for everything after M5.
 *
 *   handoff_ber                 the standard sweep
 *   handoff_ber --frames 200    more frames per point, tighter numbers
 *   handoff_ber --csv           machine-readable, for tools/plot.py
 *   handoff_ber --impairment ramp|offset|hum|dropout|drift
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chan.h"
#include "frame.h"

#define MAX_CHIPS   1024
#define MAX_SAMPLES CHAN_MAX_SAMPLES(MAX_CHIPS)

static int16_t g_samples[MAX_SAMPLES];
static uint8_t g_chips[MAX_CHIPS];

typedef struct {
    uint32_t frames;
    uint32_t good;
    uint32_t bad_crc;
    uint32_t no_sync;
    uint32_t bit_errors;
    uint32_t bits;
} point_t;

/* One frame through the whole chain: encode, modulate, demodulate, decode. */
static void run_frame(const chan_cfg_t *cfg, uint8_t seq, point_t *p)
{
    frame_hdr_t h;
    uint8_t payload[HANDOFF_FRAG_PAYLOAD];
    frame_rx_t rx;
    demod_t d;
    size_t nchips, ns, i;
    int decoded = 0;

    for (i = 0; i < sizeof payload; i++)
        payload[i] = (uint8_t)((i * 31u + seq * 17u) & 0xFFu);

    h.frag_index = (uint8_t)(seq & 0x0Fu);
    h.frag_count = 16;
    h.record_id = (uint8_t)(seq & 0x3Fu);
    h.flags = 0;

    nchips = frame_encode(&h, payload, sizeof payload, g_chips, MAX_CHIPS);
    ns = chan_render(cfg, g_chips, nchips, g_samples, MAX_SAMPLES);

    frame_rx_init(&rx);
    demod_init(&d);

    for (i = 0; i < ns; i++) {
        uint32_t score;
        uint16_t chip;
        frame_rx_result_t res;

        if (!gz_push(&d.gz, g_samples[i], &score)) continue;
        if (!sync_push(&d.sy, score, &chip)) continue;

        res = frame_rx_push(&rx, chip);
        if (res == FRAME_RX_GOOD) {
            const uint8_t *got = frame_rx_payload(&rx);
            size_t b;
            decoded = 1;
            p->good++;
            for (b = 0; b < sizeof payload; b++) {
                uint8_t diff = (uint8_t)(got[b] ^ payload[b]);
                while (diff) { p->bit_errors += diff & 1u; diff >>= 1; }
            }
            p->bits += sizeof payload * 8u;
        } else if (res == FRAME_RX_BAD_CRC) {
            decoded = 1;
            p->bad_crc++;
            /* A frame that failed CRC still tells us its bit error rate, and
             * that is the number the curve needs — frame loss alone hides how
             * close to the edge the link is. */
            {
                const uint8_t *got = frame_rx_payload(&rx);
                size_t b;
                for (b = 0; b < sizeof payload; b++) {
                    uint8_t diff = (uint8_t)(got[b] ^ payload[b]);
                    while (diff) { p->bit_errors += diff & 1u; diff >>= 1; }
                }
                p->bits += sizeof payload * 8u;
            }
        }
    }

    p->frames++;
    if (!decoded) p->no_sync++;
}

static point_t sweep_point(chan_cfg_t cfg, double snr_db, uint32_t frames)
{
    point_t p;
    uint32_t i;

    memset(&p, 0, sizeof p);
    chan_set_snr_db(&cfg, snr_db);

    for (i = 0; i < frames; i++) {
        cfg.seed = 0x1000u + i * 7919u;
        run_frame(&cfg, (uint8_t)i, &p);
    }
    return p;
}

static void apply_impairment(chan_cfg_t *c, const char *name)
{
    if (!name) return;
    if (strcmp(name, "ramp") == 0)         c->ramp_db = -20.0;
    else if (strcmp(name, "offset") == 0)  { c->carrier_ppm = 200.0; c->clock_ppm = 200.0; }
    else if (strcmp(name, "hum") == 0)     c->hum_lsb = 400.0;
    else if (strcmp(name, "drift") == 0)   c->dc_drift_lsb_s = 2000.0;
    else if (strcmp(name, "dropout") == 0) { c->dropout_prob = 0.004; c->dropout_chips = 3; }
    else if (strcmp(name, "none") != 0)    fprintf(stderr, "unknown impairment %s\n", name);
}

int main(int argc, char **argv)
{
    chan_cfg_t cfg;
    uint32_t frames = 60;
    const char *impairment = "none";
    int csv = 0, i;
    double snr;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0) csv = 1;
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--impairment") == 0 && i + 1 < argc) impairment = argv[++i];
        else {
            fprintf(stderr,
                    "usage: %s [--frames N] [--csv] [--impairment ramp|offset|hum|drift|dropout]\n",
                    argv[0]);
            return 2;
        }
    }

    chan_default(&cfg);
    apply_impairment(&cfg, impairment);

    if (csv) {
        printf("snr_db,frames,good,bad_crc,no_sync,fer,ber\n");
    } else {
        printf("Handoff BER sweep — GZ_N %d, %d chips/s, %d bps, impairment %s\n",
               HANDOFF_GZ_N, HANDOFF_CHIP_RATE_HZ, HANDOFF_BIT_RATE_BPS, impairment);
        printf("frame is %d chips, %u us of airtime, %u frames per point\n\n",
               FRAME_TOTAL_CHIPS, (unsigned)FRAME_AIRTIME_US, (unsigned)frames);
        printf("  SNR   good  crc  lost      FER        BER\n");
        printf("  ---------------------------------------------\n");
    }

    for (snr = -6.0; snr <= 24.001; snr += 2.0) {
        const point_t p = sweep_point(cfg, snr, frames);
        const double fer = p.frames ? 1.0 - (double)p.good / (double)p.frames : 1.0;
        const double ber = p.bits ? (double)p.bit_errors / (double)p.bits : 0.5;

        if (csv)
            printf("%.1f,%u,%u,%u,%u,%.6f,%.8f\n", snr,
                   (unsigned)p.frames, (unsigned)p.good, (unsigned)p.bad_crc,
                   (unsigned)p.no_sync, fer, ber);
        else
            printf("%5.1f  %5u %4u %5u  %8.4f  %9.2e\n", snr,
                   (unsigned)p.good, (unsigned)p.bad_crc, (unsigned)p.no_sync, fer, ber);
    }

    if (!csv) {
        printf("\ngood = decoded and CRC passed; crc = synced but failed CRC;\n");
        printf("lost = never synced. BER is measured over frames that synced.\n");
    }
    return 0;
}
