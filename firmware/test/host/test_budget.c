/*
 * Handoff — M1's exit criteria, as assertions.
 *
 * Development plan M1 lists four things that must be true before the milestone
 * is done. They are written out here rather than left as a number in a
 * document, so that CI fails the day one of them stops being true:
 *
 *   - BER < 1e-4 at the SNR the link budget predicts, with 6 dB of margin
 *   - a packet decodes with a 20 dB amplitude ramp across it
 *   - a packet decodes at +-200 ppm carrier offset and +-200 ppm chip offset
 *   - the whole suite runs on push
 *
 * The fourth is .github/workflows/ci.yml; the first three are below.
 */
#include <math.h>
#include <string.h>

#include "chan.h"
#include "frame.h"
#include "hf_test.h"
#include "tests.h"

#define MAX_CHIPS   1024
#define MAX_SAMPLES CHAN_MAX_SAMPLES(MAX_CHIPS)

static int16_t g_samples[MAX_SAMPLES];
static uint8_t g_chips[MAX_CHIPS];

/*
 * One frame end to end. Returns bit errors, or -1 if the frame never decoded.
 * Counting bit errors rather than just pass/fail is what makes the number a
 * BER instead of a frame error rate — and it is the BER the link budget and
 * M5's bench comparison are stated in.
 */
static int run_frame(const chan_cfg_t *cfg, uint8_t seq, int *bits)
{
    frame_hdr_t h;
    uint8_t payload[HANDOFF_FRAG_PAYLOAD];
    frame_rx_t rx;
    demod_t d;
    size_t nchips, ns, i;

    for (i = 0; i < sizeof payload; i++)
        payload[i] = (uint8_t)((i * 31u + seq * 17u) & 0xFFu);

    h.frag_index = 0;
    h.frag_count = 1;
    h.record_id = (uint8_t)(seq & 0x3Fu);
    h.flags = 0;

    nchips = frame_encode(&h, payload, sizeof payload, g_chips, MAX_CHIPS);
    ns = chan_render(cfg, g_chips, nchips, g_samples, MAX_SAMPLES);
    if (ns == 0) return -1;

    frame_rx_init(&rx);
    demod_init(&d);

    for (i = 0; i < ns; i++) {
        frame_chip_t chip;

        if (!demod_push(&d, g_samples[i], &chip)) continue;

        if (frame_rx_push(&rx, chip) == FRAME_RX_GOOD) {
            const uint8_t *got = frame_rx_payload(&rx);
            int errors = 0;
            size_t b;
            for (b = 0; b < sizeof payload; b++) {
                uint8_t diff = (uint8_t)(got[b] ^ payload[b]);
                while (diff) { errors += diff & 1u; diff >>= 1; }
            }
            if (bits) *bits = (int)(sizeof payload * 8u);
            return errors;
        }
    }
    return -1;
}

static void sweep(chan_cfg_t cfg, int frames, int *decoded, long *bit_errors, long *bits)
{
    int i;
    *decoded = 0; *bit_errors = 0; *bits = 0;

    for (i = 0; i < frames; i++) {
        int n = 0;
        int e;
        cfg.seed = 0x5000u + (uint64_t)i * 7919u;
        e = run_frame(&cfg, (uint8_t)i, &n);
        if (e < 0) continue;
        (*decoded)++;
        *bit_errors += e;
        *bits += n;
    }
}

void test_budget(void)
{
    /*
     * Where the waterfall actually is. Stated as an assertion so that a change
     * which quietly costs 6 dB shows up here rather than at M5, on a bench,
     * against a link budget nobody can then trust.
     */
    hf_begin("budget: the waterfall sits below 0 dB input SNR");
    {
        chan_cfg_t c;
        int decoded;
        long errs, bits;

        chan_default(&c);
        chan_set_snr_db(&c, 0.0);
        sweep(c, 40, &decoded, &errs, &bits);
        HF_CHECK_MSG(decoded >= 38, "only %d of 40 frames decoded at 0 dB", decoded);

        chan_set_snr_db(&c, -8.0);
        sweep(c, 40, &decoded, &errs, &bits);
        HF_CHECK_MSG(decoded < 40,
                     "all 40 frames decoded at -8 dB — the model is too kind "
                     "to be evidence for anything");
    }

    /*
     * M1 exit criterion 1. The link budget (design §5) puts ~200 LSB of
     * carrier at the ADC; M4 measures the bare-ADC noise floor it sits on, and
     * a 12-bit converter's floor is a couple of LSB, so the predicted input
     * SNR is tens of dB. Rather than assert a figure that M4 has not measured
     * yet, assert the margin: 6 dB above the waterfall, nothing must fail.
     */
    hf_begin("budget: BER < 1e-4 with 6 dB of margin over the waterfall");
    {
        chan_cfg_t c;
        int decoded;
        long errs, bits;

        chan_default(&c);
        chan_set_snr_db(&c, 4.0);          /* 6 dB above the -2 dB knee */
        sweep(c, 200, &decoded, &errs, &bits);

        HF_CHECK_MSG(decoded == 200, "only %d of 200 frames decoded", decoded);
        HF_CHECK_MSG(bits > 50000, "only %ld bits measured", bits);
        HF_CHECK_MSG(errs == 0, "%ld bit errors in %ld bits", errs, bits);
    }

    /*
     * M1 exit criterion 2: grip tightens, or loosens, mid-handshake.
     *
     * The noise is pinned to the WEAK end of the ramp, not to the average. Set
     * it from the strong end instead and the tail of a fading packet sits
     * below the waterfall, which tests nothing but arithmetic — no decoder
     * recovers a signal that is not there. What is being tested is that a 20 dB
     * swing does not by itself break the decision, and that is precisely what
     * design §9.2 chose Manchester for: each bit is judged against its own
     * other half, never against a level measured earlier in the packet.
     */
    hf_begin("budget: 20 dB of amplitude ramp across the packet");
    {
        const double weak = 200.0;   /* design §5's figure at the ADC */
        double db;

        for (db = -20.0; db <= 20.001; db += 40.0) {
            chan_cfg_t c;
            int decoded;
            long errs, bits;

            chan_default(&c);
            c.ramp_db = db;
            /* Start high and fade, or start low and grow — either way the
             * weakest chip in the packet sees 10 dB. */
            c.amplitude = (db < 0.0) ? weak * pow(10.0, -db / 20.0) : weak;
            c.noise_rms = (weak / sqrt(2.0)) / pow(10.0, 10.0 / 20.0);

            sweep(c, 40, &decoded, &errs, &bits);
            HF_CHECK_MSG(decoded == 40, "%+.0f dB ramp: only %d of 40 decoded",
                         db, decoded);
            HF_CHECK_MSG(errs == 0, "%+.0f dB ramp: %ld bit errors", db, errs);
        }
    }

    /*
     * M1 exit criterion 3. +-200 ppm is four times worse than two real
     * crystals, which is the point: passing here means M6's independent-clock
     * test should hold no surprises.
     */
    hf_begin("budget: +-200 ppm of carrier and chip clock offset");
    {
        static const double k_ppm[] = { -200.0, -100.0, 100.0, 200.0 };
        size_t i;

        for (i = 0; i < sizeof k_ppm / sizeof k_ppm[0]; i++) {
            chan_cfg_t c;
            int decoded;
            long errs, bits;

            chan_default(&c);
            chan_set_snr_db(&c, 10.0);
            c.carrier_ppm = k_ppm[i];
            c.clock_ppm = k_ppm[i];
            sweep(c, 40, &decoded, &errs, &bits);

            HF_CHECK_MSG(decoded == 40, "%+.0f ppm: only %d of 40 decoded",
                         k_ppm[i], decoded);
            HF_CHECK_MSG(errs == 0, "%+.0f ppm: %ld bit errors", k_ppm[i], errs);
        }
    }

    /* Not an exit criterion, but the environment design §10.4 worries about. */
    hf_begin("budget: mains hum and a drifting bias do not break the link");
    {
        chan_cfg_t c;
        int decoded;
        long errs, bits;

        chan_default(&c);
        chan_set_snr_db(&c, 10.0);
        c.hum_lsb = 400.0;          /* twice the carrier, at 50 Hz */
        c.dc_drift_lsb_s = 2000.0;
        sweep(c, 40, &decoded, &errs, &bits);

        HF_CHECK_MSG(decoded == 40, "hum and drift: only %d of 40 decoded", decoded);
        HF_CHECK_MSG(errs == 0, "hum and drift: %ld bit errors", errs);
    }
}
