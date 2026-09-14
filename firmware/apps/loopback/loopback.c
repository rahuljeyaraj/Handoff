/*
 * Handoff — The whole link inside one chip. M5.
 *
 * Hardware: ONE JUMPER WIRE, then two resistors.
 *
 *   Phase A  GP2 -> GP26 bare. A 0-3.3 V square straight into the ADC. Safe,
 *            but ~2000 LSB where the link budget expects ~200, so it proves
 *            plumbing and not margin.
 *   Phase B  10 kOhm from GP2 to the ADC node, 680 Ohm to ground. ~210 mV p-p,
 *            matching design §5, from a 640 Ohm source the SAR is happy with.
 *
 * Exit criteria (development plan M5):
 *
 *   - a frame passes CRC across the loop at 40 kHz and at 200 kHz
 *   - 1000 consecutive frames, zero failures, at Phase B amplitude
 *   - BER against a progressively harsher attenuator, agreeing with the M1
 *     simulator to within a few dB
 *   - with GP2 high-Z, the received score drops to the M4 noise floor
 *
 * THE AGREEMENT WITH THE SIMULATOR IS THE REAL DELIVERABLE. It is what lets
 * the simulator be trusted for everything after this.
 *
 * Cannot prove: independent clocks (both PLLs come off the same crystal), or
 * anything a shared codebase bug would cancel out — run M1's independently
 * generated vectors here too.
 *
 * HOW IT RUNS. The first two criteria need no hands once the wire is on, so
 * they run at boot: one verbose frame at each carrier, then the quiet-channel
 * measurement. Everything that depends on which resistors are in the loop is
 * a command on the USB console, because the resistors are changed by hand
 * between runs:
 *
 *   a         the boot suite again, at the current carrier
 *   1         one frame, verbose
 *   f [N]     N frames (default 1000): good / bad-CRC / lost, FER, BER
 *   m         measure: on-chip energy, off-chip energy, high-Z leakage,
 *             raw sample RMS, and the SNR the simulator would call this
 *   c 40|200  carrier, kHz. Both ends re-tune; the frame receiver resets
 *   s [N]     stream every Nth chip energy as "s <e>" lines; s 0 stops
 *   r         raw burst: 100 ms of ADC samples across one frame, dumped as
 *             "r <n>" ... "r end" for tools/plot.py
 *   h         this list
 *
 * The SNR printed by `m` is the simulator's definition — 20 log10 of carrier
 * RMS over sample noise RMS, chan_set_snr_db() in test/host/chan.c — so a
 * bench point can be handed straight to `handoff_ber --snr <dB>` and the two
 * FER/BER figures compared. That comparison is the M5 deliverable.
 *
 * The frame receiver runs continuously between frames, the way the real link
 * does, rather than being re-initialised per frame the way test/host/ber.c
 * does. If those two ever disagree that is a finding, not a nuisance.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"

#include "adc_ring.h"
#include "config.h"
#include "frame.h"
#include "hal_pico.h"
#include "pio_carrier.h"
#include "tlm.h"

#define FRAMES_DEFAULT   1000u
#define FRAME_GAP_US     20000u   /* silence between frames: the hunt needs it */
#define RX_LATENCY_US    50000u   /* DMA block + sync + slack, before "lost"   */
#define PROGRESS_EVERY   100u
#define HEARTBEAT_US     10000000u
#define MEASURE_US       250000u  /* per level in `m`                          */

static const hal_iface_t *s_hal;
static frame_rx_t         s_rx;
static uint8_t            s_chips[FRAME_TOTAL_CHIPS];
static uint32_t           s_spurious;   /* frames decoded while nothing was sent */
static int                s_fail;

typedef struct {
    uint32_t frames, good, bad_crc, lost;
    uint32_t bit_errors, bits;
    uint64_t margin_sum;
    uint32_t margin_n;
} stats_t;

/* ---------------------------------------------------------------------- */

static void check(bool ok, const char *what)
{
    printf("    %-52s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) s_fail++;
}

static void print_tenths(uint32_t tenths)
{
    printf("%lu.%lu", (unsigned long)(tenths / 10u), (unsigned long)(tenths % 10u));
}

/* The same payload ber.c uses, so a bench frame and a simulator frame carry
 * identical bits and a BER figure means the same thing in both. */
static void fill_payload(uint8_t *payload, uint8_t seq)
{
    size_t i;
    for (i = 0; i < HANDOFF_FRAG_PAYLOAD; i++)
        payload[i] = (uint8_t)((i * 31u + seq * 17u) & 0xFFu);
}

static size_t encode(uint8_t seq)
{
    frame_hdr_t h;
    uint8_t payload[HANDOFF_FRAG_PAYLOAD];

    fill_payload(payload, seq);
    h.frag_index = (uint8_t)(seq & 0x0Fu);
    h.frag_count = 16;
    h.record_id  = (uint8_t)(seq & 0x3Fu);
    h.flags      = 0;

    return frame_encode(&h, payload, sizeof payload, s_chips, sizeof s_chips);
}

static uint32_t count_bit_errors(const uint8_t *got, uint8_t seq)
{
    uint8_t want[HANDOFF_FRAG_PAYLOAD];
    uint32_t errs = 0;
    size_t b;

    fill_payload(want, seq);
    for (b = 0; b < sizeof want; b++) {
        uint8_t diff = (uint8_t)(got[b] ^ want[b]);
        while (diff) { errs += diff & 1u; diff >>= 1; }
    }
    return errs;
}

/* ---------------------------------------------------------------------- */

static uint16_t s_stream_decimate;

static void report_stalls(void);

/*
 * Core 0's half of §3.3: pop chip energies and push them through the frame
 * receiver. Returns the first frame result seen, or NONE. Called constantly,
 * whether or not a frame is expected, so the receiver is always primed and a
 * spurious decode is counted rather than missed.
 */
static frame_rx_result_t pump(void)
{
    uint16_t chips[64];
    size_t n, i;
    frame_rx_result_t out = FRAME_RX_NONE;

    while ((n = hal_rx_chips(s_hal, chips, count_of(chips))) > 0) {
        for (i = 0; i < n; i++) {
            frame_rx_result_t r;

            if (s_stream_decimate)
                hal_telemetry(s_hal, HAL_TLM_SCORE, &chips[i], sizeof chips[i]);

            r = frame_rx_push(&s_rx, chips[i]);
            if (r != FRAME_RX_NONE && out == FRAME_RX_NONE) out = r;
        }
    }
    return out;
}

static void drain_idle(void)
{
    if (pump() != FRAME_RX_NONE) s_spurious++;
}

/*
 * One frame through the wire. Transmit, then wait for the receiver to report
 * or for the airtime plus latency to run out. The pad is released to high-Z
 * after every frame, which is what the link does and what txgen showed
 * clears an E9 latch.
 */
static frame_rx_result_t run_frame(uint8_t seq, stats_t *st, bool verbose)
{
    size_t n = encode(seq);
    uint64_t t0, deadline;
    bool driving = true;
    frame_rx_result_t res = FRAME_RX_NONE;

    drain_idle();

    hal_tx_drive(s_hal, true);
    if (hal_tx_chips(s_hal, s_chips, n) != n) {
        printf("    tx refused %u chips\n", (unsigned)n);
        hal_tx_drive(s_hal, false);
        st->frames++;
        st->lost++;
        return FRAME_RX_NONE;
    }

    t0 = hal_now_us(s_hal);
    deadline = t0 + FRAME_AIRTIME_US + RX_LATENCY_US;

    while (hal_now_us(s_hal) < deadline) {
        if (driving && !hal_tx_busy(s_hal)) {
            hal_tx_drive(s_hal, false);
            driving = false;
        }
        res = pump();
        if (res != FRAME_RX_NONE) break;
    }
    if (driving) hal_tx_drive(s_hal, false);
    report_stalls();

    st->frames++;
    if (res == FRAME_RX_GOOD || res == FRAME_RX_BAD_CRC) {
        uint32_t errs = count_bit_errors(frame_rx_payload(&s_rx), seq);
        st->bit_errors += errs;
        st->bits       += HANDOFF_FRAG_PAYLOAD * 8u;
        st->margin_sum += s_rx.last_margin;
        st->margin_n++;
        if (res == FRAME_RX_GOOD) st->good++; else st->bad_crc++;

        if (verbose) {
            const frame_hdr_t *h = frame_rx_hdr(&s_rx);
            printf("    frame %3u: %s after %lu us, hdr %u/%u rec %u, "
                   "margin %u, %lu bit errors\n",
                   seq, res == FRAME_RX_GOOD ? "CRC ok " : "CRC BAD",
                   (unsigned long)(hal_now_us(s_hal) - t0),
                   h->frag_index, h->frag_count, h->record_id,
                   s_rx.last_margin, (unsigned long)errs);
        }
    } else {
        st->lost++;
        if (verbose)
            printf("    frame %3u: LOST (no sync in %lu us; %lu false syncs)\n",
                   seq, (unsigned long)(FRAME_AIRTIME_US + RX_LATENCY_US),
                   (unsigned long)s_rx.false_syncs);
    }

    /* Silence, so the next hunt starts from a quiet channel. */
    {
        uint64_t until = hal_now_us(s_hal) + FRAME_GAP_US;
        while (hal_now_us(s_hal) < until) drain_idle();
    }
    return res;
}

static void print_stats(const stats_t *st)
{
    double fer = st->frames ? 1.0 - (double)st->good / (double)st->frames : 1.0;
    double ber = st->bits ? (double)st->bit_errors / (double)st->bits : 0.5;

    printf("    frames %lu  good %lu  crc %lu  lost %lu   FER %.4f   BER %.2e"
           "   margin %lu\n",
           (unsigned long)st->frames, (unsigned long)st->good,
           (unsigned long)st->bad_crc, (unsigned long)st->lost, fer, ber,
           (unsigned long)(st->margin_n ? st->margin_sum / st->margin_n : 0));
}

static uint32_t s_stalls_seen;

static void report_stalls(void)
{
    pio_carrier_state_t st;
    uint32_t n = hal_pico_tx_stalls(&st);

    if (n == s_stalls_seen) return;
    s_stalls_seen = n;
    printf("    TX STALL #%lu recovered: dma %s, %lu transfers left, ctrl %08lx; "
           "sm %s, pc %u, fifo %u, exec %s\n",
           (unsigned long)n, st.dma_busy ? "busy" : "idle",
           (unsigned long)st.dma_remaining, (unsigned long)st.dma_ctrl,
           st.sm_enabled ? "enabled" : "DISABLED", st.pc, st.fifo_level,
           st.exec_stalled ? "STALLED" : "ok");
}

static void cmd_frames(uint32_t n)
{
    stats_t st;
    uint32_t i;
    uint64_t t0 = hal_now_us(s_hal);

    memset(&st, 0, sizeof st);
    printf("\n  --- %lu frames at %lu Hz ---\n",
           (unsigned long)n, (unsigned long)hal_pico_carrier_hz());

    for (i = 0; i < n; i++) {
        run_frame((uint8_t)i, &st, false);
        if ((i + 1u) % PROGRESS_EVERY == 0u || i + 1u == n) {
            printf("  %5lu/%lu  ", (unsigned long)(i + 1u), (unsigned long)n);
            print_stats(&st);
        }
    }

    printf("    %lu s, dma overruns %lu, spurious frames %lu, "
           "false syncs %lu, core-1 load %u%%\n",
           (unsigned long)((hal_now_us(s_hal) - t0) / 1000000u),
           (unsigned long)hal_pico_overruns(), (unsigned long)s_spurious,
           (unsigned long)s_rx.false_syncs, hal_pico_core1_load());
    report_stalls();

    if (n >= FRAMES_DEFAULT)
        check(st.good == n, "1000 consecutive frames, zero failures");
}

/* ---------------------------------------------------------------------- */

/* Mean chip energy over a window, in tenths of an LSB, plus the max. */
static uint32_t mean_energy(uint32_t us, uint32_t *max_out)
{
    uint16_t chips[64];
    uint64_t sum = 0, n = 0;
    uint32_t max = 0;
    uint64_t until = hal_now_us(s_hal) + us;
    size_t k, i;

    while (hal_now_us(s_hal) < until) {
        while ((k = hal_rx_chips(s_hal, chips, count_of(chips))) > 0) {
            for (i = 0; i < k; i++) {
                sum += chips[i];
                if (chips[i] > max) max = chips[i];
            }
            n += k;
        }
    }
    if (max_out) *max_out = max;
    return n ? (uint32_t)((sum * 10u + n / 2u) / n) : 0;
}

/* RMS of a raw burst about its mean, in tenths of an LSB. */
static uint32_t raw_rms(int32_t *mean_code)
{
    const int16_t *s;
    size_t n = 0, i;
    int64_t sum = 0;
    uint64_t sq = 0;
    int32_t mean;

    tlm_usb_raw_trigger();
    while (tlm_usb_raw_busy()) tight_loop_contents();

    s = tlm_usb_raw_samples(&n);
    if (!s || n == 0) { if (mean_code) *mean_code = -1; return 0; }

    for (i = 0; i < n; i++) sum += s[i];
    mean = (int32_t)(sum / (int64_t)n);
    for (i = 0; i < n; i++) {
        int32_t d = s[i] - mean;
        sq += (uint64_t)((int64_t)d * d);
    }
    if (mean_code) *mean_code = mean + 2048;
    return (uint32_t)sqrt((double)sq * 100.0 / (double)n + 0.5);
}

/*
 * The bench point, in the simulator's terms. Three pad states are measured:
 *
 *   carrier on    a continuous mark, so the on-chip energy A is read without
 *                 any Manchester structure in the way
 *   driven low    a space: what an off chip inside a frame looks like
 *   high-Z        design §6.3's receive state: the leakage criterion
 *
 * A is the Goertzel score, which for an on-bin tone is its amplitude in LSB —
 * for the GPIO square it is the fundamental, which is what the link uses.
 * sigma is the raw-sample RMS in silence. The simulator's SNR is
 * 20 log10(A / sqrt 2 / sigma).
 */
static void cmd_measure(void)
{
    uint32_t on, on_max, low, low_max, hiz, hiz_max, sig_low, sig_hiz;
    int32_t  mean_low, mean_hiz, noise_mean;
    uint32_t noise = hal_pico_noise_floor(&noise_mean);

    printf("\n  --- measure at %lu Hz ---\n", (unsigned long)hal_pico_carrier_hz());

    drain_idle();

    hal_tx_drive(s_hal, true);
    pio_carrier_mark_continuous(true);
    sleep_ms(20);
    drain_idle();
    on = mean_energy(MEASURE_US, &on_max);
    pio_carrier_mark_continuous(false);

    /* A starved PIO holds the pad at its last level, which after a mark is
     * high; a short run of space chips is what actually drives it low. */
    {
        uint8_t space[8];
        memset(space, 0, sizeof space);
        hal_tx_chips(s_hal, space, sizeof space);
        while (hal_tx_busy(s_hal)) tight_loop_contents();
    }
    sleep_ms(20);
    drain_idle();
    low = mean_energy(MEASURE_US, &low_max);
    sig_low = raw_rms(&mean_low);

    hal_tx_drive(s_hal, false);
    sleep_ms(20);
    drain_idle();
    hiz = mean_energy(MEASURE_US, &hiz_max);
    sig_hiz = raw_rms(&mean_hiz);

    printf("    carrier on:   chip energy "); print_tenths(on);
    printf(" LSB (max %lu)\n", (unsigned long)on_max);
    printf("    driven low:   chip energy "); print_tenths(low);
    printf(" LSB (max %lu), raw ", (unsigned long)low_max); print_tenths(sig_low);
    printf(" LSB RMS about code %ld\n", (long)mean_low);
    printf("    high-Z:       chip energy "); print_tenths(hiz);
    printf(" LSB (max %lu), raw ", (unsigned long)hiz_max); print_tenths(sig_hiz);
    printf(" LSB RMS about code %ld\n", (long)mean_hiz);
    printf("    M4 floor:     "); print_tenths(noise);
    printf(" LSB RMS about code %ld (on-die sensor, at boot)\n", (long)noise_mean);

    if (on == 0u) {
        printf("    simulator SNR: undefined -- no carrier received; is the wire on?\n");
    } else if (sig_low >= 1u) {
        double a = (double)on / 10.0;
        double sigma = (double)sig_low / 10.0;
        double snr = 20.0 * log10(a / sqrt(2.0) / sigma);
        printf("    simulator SNR: %.1f dB  (A %.1f LSB, sigma %.2f LSB) -> "
               "handoff_ber --snr %.1f\n", snr, a, sigma, snr);
    } else {
        printf("    simulator SNR: undefined -- the off state sits at the rail "
               "and the noise is clipped (M4 finding 2);\n"
               "                   a harsher attenuator lowers A, not the SNR\n");
    }

    /*
     * The leakage criterion. With GP2 released the receiver should see the
     * same nothing M4 saw with no wire at all: a chip energy at or under the
     * bare-ADC floor. One LSB of slack, because the floor is a sample-level
     * RMS and the chip energy is an integrated magnitude.
     */
    check(hiz <= noise + 10u, "GP2 high-Z: received energy at the M4 noise floor");
    check(on > hiz + 100u, "carrier is received well above the released pad");
}

/* ---------------------------------------------------------------------- */

static void cmd_carrier(uint32_t khz)
{
    uint32_t hz = khz * 1000u;

    if (!hal_pico_set_carrier(hz)) {
        printf("    %lu Hz is not a PIO divider on a Goertzel bin centre\n",
               (unsigned long)hz);
        return;
    }
    frame_rx_init(&s_rx);
    printf("    carrier %lu Hz, bin %lu, %lu pad bits per chip\n",
           (unsigned long)hz,
           (unsigned long)(hz / (uint32_t)HANDOFF_WINDOW_RATE_HZ),
           (unsigned long)pio_carrier_bits_per_chip());
}

static void cmd_one(void)
{
    stats_t st;
    char what[64];

    memset(&st, 0, sizeof st);
    printf("\n  --- one frame at %lu Hz ---\n", (unsigned long)hal_pico_carrier_hz());
    run_frame(0, &st, true);
    snprintf(what, sizeof what, "frame passes CRC across the loop at %lu Hz",
             (unsigned long)hal_pico_carrier_hz());
    check(st.good == 1u, what);
}

static void cmd_raw(void)
{
    stats_t st;

    memset(&st, 0, sizeof st);
    printf("\n  --- raw burst across one frame ---\n");
    tlm_usb_raw_trigger();
    run_frame(0, &st, true);
    while (tlm_usb_raw_busy()) tight_loop_contents();
    tlm_usb_raw_dump();
}

static void cmd_suite(void)
{
    cmd_one();
    cmd_measure();
}

static void cmd_help(void)
{
    printf("\n  a         boot suite at the current carrier\n"
           "  1         one frame, verbose\n"
           "  f [N]     N frames, default %u\n"
           "  m         on / off / high-Z energies, raw RMS, simulator SNR\n"
           "  c 40|200  carrier, kHz\n"
           "  s [N]     stream every Nth chip energy; s 0 stops\n"
           "  r         raw burst across one frame, dumped\n"
           "  h         this\n", FRAMES_DEFAULT);
}

static void dispatch(const char *line)
{
    uint32_t arg = 0;
    bool have_arg = false;

    while (*line == ' ') line++;
    if (!*line) return;
    if (line[1]) {
        const char *p = line + 1;
        while (*p == ' ') p++;
        if (*p >= '0' && *p <= '9') { arg = (uint32_t)strtoul(p, 0, 10); have_arg = true; }
    }

    switch (line[0]) {
    case 'a': cmd_suite(); break;
    case '1': cmd_one(); break;
    case 'f': cmd_frames(have_arg ? arg : FRAMES_DEFAULT); break;
    case 'm': cmd_measure(); break;
    case 'c': cmd_carrier(have_arg ? arg : HANDOFF_CARRIER_HZ / 1000u); break;
    case 's':
        s_stream_decimate = (uint16_t)(have_arg ? arg : 1u);
        tlm_usb_init(s_stream_decimate);
        printf("    stream %s\n", s_stream_decimate ? "on" : "off");
        break;
    case 'r': cmd_raw(); break;
    case 'h': case '?': cmd_help(); break;
    default:  printf("    ? (h for help)\n"); break;
    }
}

/* ---------------------------------------------------------------------- */

int main(void)
{
    char line[32];
    size_t len = 0;
    uint64_t next_hb;
    int32_t noise_mean;
    uint32_t noise;

    stdio_init_all();
    sleep_ms(2000);          /* let the USB console attach before the report */

    printf("\nhandoff loopback (M5: the whole link inside one chip)\n");
    printf("  carrier %d Hz, ADC %d Hz, Goertzel N=%d bin %d, %d chips/s, %d bps\n",
           HANDOFF_CARRIER_HZ, HANDOFF_ADC_FS_HZ, HANDOFF_GZ_N, HANDOFF_GZ_BIN,
           HANDOFF_CHIP_RATE_HZ, HANDOFF_BIT_RATE_BPS);
    printf("  frame %d chips, %lu us airtime, %d-byte payload\n",
           FRAME_TOTAL_CHIPS, (unsigned long)FRAME_AIRTIME_US, HANDOFF_FRAG_PAYLOAD);

    s_hal = hal_pico_init();
    frame_rx_init(&s_rx);

    noise = hal_pico_noise_floor(&noise_mean);
    printf("  M4 noise floor "); print_tenths(noise);
    printf(" LSB RMS (mean code %ld), core 1 up\n", (long)noise_mean);

    /* Wire GP2 to GP26 (Phase A) or through the attenuator (Phase B) before
     * this runs; a failing boot suite with no wire on is just an empty loop. */
    cmd_carrier(200);
    cmd_suite();
    cmd_carrier(40);
    cmd_suite();
    cmd_carrier(HANDOFF_CARRIER_HZ / 1000u);

    printf("\n  boot suite: %d failure%s. `f` for the 1000-frame soak, `h` for help.\n",
           s_fail, s_fail == 1 ? "" : "s");

    next_hb = hal_now_us(s_hal) + HEARTBEAT_US;

    for (;;) {
        int ch = getchar_timeout_us(0);

        drain_idle();

        if (ch == PICO_ERROR_TIMEOUT) {
            /* A placed heartbeat: if this stops, the hang is on core 0. */
            if (hal_now_us(s_hal) >= next_hb) {
                next_hb += HEARTBEAT_US;
                printf("  hb chips %lu windows %lu overruns %lu level %lu "
                       "load %u%% spurious %lu stalls %lu\n",
                       (unsigned long)hal_pico_chips(),
                       (unsigned long)hal_pico_windows(),
                       (unsigned long)hal_pico_overruns(),
                       (unsigned long)hal_rx_carrier_level(s_hal),
                       hal_pico_core1_load(), (unsigned long)s_spurious,
                       (unsigned long)hal_pico_tx_stalls(0));
            }
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            line[len] = 0;
            if (len) dispatch(line);
            len = 0;
        } else if (len + 1 < sizeof line) {
            line[len++] = (char)ch;
        }
    }
}
