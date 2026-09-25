/*
 * Handoff — decode a hardware capture with the firmware's own pipeline.
 *
 * Development plan M5. apps/loopback dumps 100 ms of raw ADC across a frame;
 * this runs exactly what core 1 and core 0 run on those samples — both tone
 * bins, symbol sync, frame_rx_push, the same lib/ code — and says what came
 * out. It is how a bench result is split into "the firmware differs from the
 * host" (this decodes, the board did not) and "the channel differs from the
 * model" (neither decodes at an SNR where the simulator says it should).
 *
 *   handoff_decode capture.s16 [--seq N] [--chips]
 *   handoff_decode capture.s16 --v1 [--carrier 40000] ...
 *
 * LINK V2 STEP 6: the default is the two-tone receiver, because that is what
 * the firmware is. --v1 replays an OOK capture through the slicer the
 * firmware no longer has — see v1_replay.h for why that lives in test/host.
 * The captures in firmware/test/vectors/captures are all v1.
 *
 * Input is little-endian int16, DC-centred, 500 ksps — the format tlm_usb
 * dumps and tools/replay.py adopts. With --seq the payload is checked against
 * the pattern loopback and ber.c both use, so a BER is printed too.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chan.h"
#include "config.h"
#include "frame.h"
#include "goertzel.h"
#include "sync.h"
#include "v1_replay.h"

#define MAX_SAMPLES 4000000

static int16_t g_samples[MAX_SAMPLES];

static void fill_payload(uint8_t *payload, uint8_t seq)
{
    size_t i;
    for (i = 0; i < HANDOFF_FRAG_PAYLOAD; i++)
        payload[i] = (uint8_t)((i * 31u + seq * 17u) & 0xFFu);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    uint32_t carrier = HANDOFF_CARRIER_HZ;
    int seq = -1, show_chips = 0, v1 = 0, i;
    FILE *f;
    size_t n, k;
    demod_t d;
    v1_replay_t v1rx;
    frame_rx_t rx;
    uint32_t chips = 0;
    int64_t chip_sum = 0;
    int32_t chip_max = 0;
    int good = 0, bad = 0;
    uint32_t bit_errors = 0, bits = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--carrier") == 0 && i + 1 < argc) carrier = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--v1") == 0) v1 = 1;
        else if (strcmp(argv[i], "--seq") == 0 && i + 1 < argc) seq = atoi(argv[++i]);
        else if (strcmp(argv[i], "--chips") == 0) show_chips = 1;
        else if (!path) path = argv[i];
        else { fprintf(stderr, "usage: %s capture.s16 [--v1] [--carrier hz] [--seq n] [--chips]\n", argv[0]); return 2; }
    }
    if (!path) { fprintf(stderr, "usage: %s capture.s16 [--v1] [--carrier hz] [--seq n] [--chips]\n", argv[0]); return 2; }

    f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    n = fread(g_samples, sizeof g_samples[0], MAX_SAMPLES, f);
    fclose(f);
    if (n == 0) { fprintf(stderr, "%s: empty\n", path); return 1; }

    if (v1 && carrier % (uint32_t)HANDOFF_WINDOW_RATE_HZ != 0) {
        fprintf(stderr, "%u Hz is not on a Goertzel bin centre\n", (unsigned)carrier);
        return 2;
    }

    demod_init(&d);
    v1_replay_init(&v1rx, (uint16_t)(carrier / (uint32_t)HANDOFF_WINDOW_RATE_HZ));
    frame_rx_init(&rx);

    if (v1)
        printf("%s: %u samples, %.1f ms, V1 OOK, carrier %u Hz bin %u\n",
               path, (unsigned)n, (double)n / (HANDOFF_ADC_FS_HZ / 1000.0),
               (unsigned)carrier,
               (unsigned)(carrier / (uint32_t)HANDOFF_WINDOW_RATE_HZ));
    else
        printf("%s: %u samples, %.1f ms, two tones on bins %u/%u (%u/%u Hz)\n",
               path, (unsigned)n, (double)n / (HANDOFF_ADC_FS_HZ / 1000.0),
               (unsigned)HANDOFF_TONE_A_BIN, (unsigned)HANDOFF_TONE_B_BIN,
               (unsigned)HANDOFF_TONE_A_HZ, (unsigned)HANDOFF_TONE_B_HZ);

    for (k = 0; k < n; k++) {
        frame_chip_t chip;
        frame_rx_result_t res;

        if (v1) { if (!v1_replay_push(&v1rx, g_samples[k], &chip)) continue; }
        else    { if (!demod_push(&d, g_samples[k], &chip)) continue; }

        chips++;
        chip_sum += chip;
        if (chip > chip_max) chip_max = chip;
        if (show_chips) printf("%+d%c", (int)chip, (chips % 16) ? ' ' : '\n');

        res = frame_rx_push(&rx, chip);
        if (res == FRAME_RX_NONE) continue;

        if (show_chips) printf("\n");
        printf("  chip %u (%.1f ms): %s, hdr %u/%u rec %u, margin %u\n",
               (unsigned)chips, (double)chips * HANDOFF_CHIP_US / 1000.0,
               res == FRAME_RX_GOOD ? "CRC ok" : "CRC BAD",
               rx.hdr.frag_index, rx.hdr.frag_count, rx.hdr.record_id, (unsigned)rx.last_margin);
        if (res == FRAME_RX_GOOD) good++; else bad++;

        if (seq >= 0) {
            uint8_t want[HANDOFF_FRAG_PAYLOAD];
            const uint8_t *got = frame_rx_payload(&rx);
            size_t b;
            fill_payload(want, (uint8_t)seq);
            for (b = 0; b < sizeof want; b++) {
                uint8_t d = (uint8_t)(got[b] ^ want[b]);
                while (d) { bit_errors += d & 1u; d >>= 1; }
            }
            bits += sizeof want * 8u;
        }
    }
    if (show_chips) printf("\n");

    printf("  %u chips, mean tone difference %.2f, max %+d\n",
           (unsigned)chips, chips ? (double)chip_sum / chips : 0.0,
           (int)chip_max);
    printf("  frames: %d good, %d bad CRC, %u syncs, %u false syncs\n",
           good, bad, (unsigned)rx.syncs, (unsigned)rx.false_syncs);
    if (bits)
        printf("  BER %.3e over %u payload bits\n", (double)bit_errors / bits, (unsigned)bits);
    if (good + bad == 0) printf("  LOST: no frame decoded\n");

    return good > 0 ? 0 : 1;
}
