/*
 * Handoff — One-way link between two boards, role fixed at boot. M6.
 *
 * Hardware: A SECOND BOARD and two resistors. TX GP2 -> 10 kOhm -> RX GP26,
 * 540 Ohm from GP26 to AGND, grounds commoned. The same divider M5 verified
 * at 132 LSB, so the only thing new in the loop is the second crystal. The
 * series leg is what gets swapped for the attenuation curve.
 *
 * The transmitting board only ever transmits, and transmitting is a gated
 * square wave, so it does not need to be a Pico 2 W or even RP2350. Only the
 * RECEIVER needs RP2350, because of the RP2040 ADC differential-non-linearity
 * defect (design §10.2).
 *
 * ROLE. GP14 is read once at boot with its pull-up on: strapped to GND
 * (hardware JP8, or a jumper across pins 18 and 19) makes the board the
 * transmitter; open makes it the receiver. -DLINKTEST_ROLE=1 (TX) or =2 (RX)
 * overrides the strap for a board with no free header.
 *
 * Exit criteria (development plan M6):
 *
 *   - frames cross between two independently clocked boards, at both carriers
 *   - an hour free-running with no cumulative timing failure
 *   - BER against series resistance, 100 kOhm to 10 MOhm, plotted
 *
 * That last curve is the link's attenuation budget in dB as a single number,
 * and it is what every later analogue result gets compared against. With a
 * bare GP26 the sweep is bounded by M5's finding that the ADC pin cannot be
 * fed from more than ~160 kOhm; the 1 MOhm end of the range is M8's, once
 * R2 feeds an amplifier instead.
 *
 * This is the first test of two independent crystals, which loopback
 * structurally cannot do. Loopback also knew when a frame was sent and could
 * call one "lost"; here the receiver only sees what arrives, so loss is
 * counted from the sequence number the transmitter puts in every frame.
 *
 * HOW IT RUNS. The transmitter sends numbered frames forever, one every
 * airtime plus a gap, and reports every hundred. The receiver listens
 * forever, reports every hundred frames it sees, and keeps running totals
 * that `z` resets — so a soak is: `z`, wait an hour, `s`. Each board has its
 * own USB console:
 *
 *   TX   c 40|200  carrier, kHz            RX   c 40|200  carrier, kHz
 *        g [ms]    gap between frames           s         stats now
 *        p         pause / resume               z         zero the stats
 *        1         one frame                    v         per-frame lines on / off
 *        h         this list                    m         chip energy and raw RMS
 *                                               r         raw burst across the next frame
 *                                               t [N]     stream every Nth chip energy
 *                                               h         this list
 *
 * Both boards must be on the same carrier; `c` is issued on each. The
 * receiver's `m` gives the received fundamental A in LSB while frames are
 * flowing (the max), and the raw RMS sigma with the transmitter paused —
 * the two numbers `handoff_ber --amplitude A --noise sigma` wants.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"

#include "adc_ring.h"
#include "config.h"
#include "frame.h"
#include "hal_pico.h"
#include "pio_carrier.h"
#include "tlm.h"

#define PIN_ROLE          14
#define ROLE_TX           1
#define ROLE_RX           2

#define GAP_DEFAULT_US    20000u   /* silence between frames: the hunt needs it */
#define PROGRESS_EVERY    100u
#define HEARTBEAT_US      10000000u
#define MEASURE_US        500000u

static const hal_iface_t *s_hal;
static int                s_role;

/* ---------------------------------------------------------------------- */

static void print_tenths(uint32_t tenths)
{
    printf("%lu.%lu", (unsigned long)(tenths / 10u), (unsigned long)(tenths % 10u));
}

static uint32_t elapsed_s(uint64_t since)
{
    return (uint32_t)((hal_now_us(s_hal) - since) / 1000000u);
}

/*
 * Payload: the 16-bit sequence number first, then the pattern ber.c and
 * loopback use, so a bit-error figure means the same thing on every bench.
 * The header carries the low ten bits too, for a frame whose CRC failed.
 */
static void fill_payload(uint8_t *payload, uint16_t seq)
{
    size_t i;
    payload[0] = (uint8_t)(seq & 0xFFu);
    payload[1] = (uint8_t)(seq >> 8);
    for (i = 2; i < HANDOFF_FRAG_PAYLOAD; i++)
        payload[i] = (uint8_t)((i * 31u + (seq & 0xFFu) * 17u) & 0xFFu);
}

static uint16_t payload_seq(const uint8_t *payload)
{
    return (uint16_t)(payload[0] | ((uint16_t)payload[1] << 8));
}

static uint32_t count_bit_errors(const uint8_t *got, uint16_t seq)
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

static void cmd_carrier(uint32_t khz, frame_rx_t *rx)
{
    uint32_t hz = khz * 1000u;

    if (!hal_pico_set_carrier(hz)) {
        printf("    %lu Hz is not a PIO divider on a Goertzel bin centre\n",
               (unsigned long)hz);
        return;
    }
    if (rx) frame_rx_init(rx);
    printf("    carrier %lu Hz, bin %lu, %lu pad bits per chip\n",
           (unsigned long)hz,
           (unsigned long)(hz / (uint32_t)HANDOFF_WINDOW_RATE_HZ),
           (unsigned long)pio_carrier_bits_per_chip());
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

/* One console line: a letter, optionally a number. */
static char parse(const char *line, uint32_t *arg, bool *have_arg)
{
    const char *p;

    while (*line == ' ') line++;
    *have_arg = false;
    *arg = 0;
    if (!*line) return 0;
    for (p = line + 1; *p == ' '; p++) {}
    if (*p >= '0' && *p <= '9') { *arg = (uint32_t)strtoul(p, 0, 10); *have_arg = true; }
    return line[0];
}

/* ======================================================================
 * Transmitter
 * ====================================================================== */

static uint8_t  s_chips[FRAME_TOTAL_CHIPS];
static uint16_t s_tx_seq;
static uint32_t s_tx_sent, s_tx_refused;
static uint32_t s_gap_us = GAP_DEFAULT_US;
static bool     s_paused;

static size_t encode(uint16_t seq)
{
    frame_hdr_t h;
    uint8_t payload[HANDOFF_FRAG_PAYLOAD];

    fill_payload(payload, seq);
    h.frag_index = (uint8_t)(seq & 0x0Fu);
    h.frag_count = 16;
    h.record_id  = (uint8_t)((seq >> 4) & 0x3Fu);
    h.flags      = 0;

    return frame_encode(&h, payload, sizeof payload, s_chips, sizeof s_chips);
}

/* Start one frame; the main loop watches for its end. The pad is released to
 * high-Z after every frame, which is what the link does and what txgen showed
 * clears an E9 latch. */
static bool tx_start(void)
{
    size_t n = encode(s_tx_seq);

    hal_tx_drive(s_hal, true);
    if (hal_tx_chips(s_hal, s_chips, n) != n) {
        hal_tx_drive(s_hal, false);
        s_tx_refused++;
        printf("    tx refused %u chips (seq %u)\n", (unsigned)n, s_tx_seq);
        return false;
    }
    return true;
}

/*
 * Link v2 step 1 instrument. The clock tree, gated against the crystal by the
 * RP2350 frequency counter rather than read back from the SDK.
 *
 * What the bench is looking for: sys at HANDOFF_SYS_CLK_HZ, and usb and adc
 * both still 48000 kHz after the move off 150 MHz. clk_adc is what sets the
 * 500 ksps sample rate (48 MHz / 96), and the whole of the receiver is
 * calibrated against it, so it moving would be silent and fatal.
 */
static void clocks_print(void)
{
    hal_pico_clocks_t m;

    hal_pico_clocks(&m);

    printf("\n  clocks, measured against the crystal:\n"
           "    clk_ref   %8lu kHz\n"
           "    clk_sys   %8lu kHz   (sdk says %lu, config.h says %lu)\n"
           "    clk_usb   %8lu kHz   (48000 or USB is gone)\n"
           "    clk_adc   %8lu kHz   (48000 or the sample rate moved)\n"
           "    clk_peri  %8lu kHz\n"
           "    adc rate  %8lu sps  (want %lu)\n",
           (unsigned long)m.ref_khz,
           (unsigned long)m.sys_khz, (unsigned long)m.sys_cfg_khz,
           (unsigned long)(HANDOFF_SYS_CLK_HZ / 1000),
           (unsigned long)m.usb_khz,
           (unsigned long)m.adc_khz,
           (unsigned long)m.peri_khz,
           (unsigned long)hal_pico_sps(),
           (unsigned long)HANDOFF_ADC_FS_HZ);
}

static void tx_help(void)
{
    printf("\n  c 40|200  carrier, kHz\n"
           "  g [ms]    gap between frames, default %lu\n"
           "  p         pause / resume\n"
           "  1         one frame (while paused)\n"
           "  f         clock tree, measured\n"
           "  h         this\n", (unsigned long)(GAP_DEFAULT_US / 1000u));
}

static void tx_dispatch(const char *line, bool *one_shot)
{
    uint32_t arg;
    bool have_arg;

    switch (parse(line, &arg, &have_arg)) {
    case 0: return;
    case 'c': cmd_carrier(have_arg ? arg : HANDOFF_CARRIER_HZ / 1000u, 0); break;
    case 'g':
        s_gap_us = (have_arg ? arg : GAP_DEFAULT_US / 1000u) * 1000u;
        printf("    gap %lu ms\n", (unsigned long)(s_gap_us / 1000u));
        break;
    case 'p':
        s_paused = !s_paused;
        printf("    %s\n", s_paused ? "paused: pad high-Z" : "running");
        break;
    case '1': *one_shot = true; break;
    case 'f': clocks_print(); break;
    case 'h': case '?': tx_help(); break;
    default:  printf("    ? (h for help)\n"); break;
    }
}

static void run_tx(void)
{
    char line[32];
    size_t len = 0;
    bool driving = false, one_shot = false;
    uint64_t t0 = hal_now_us(s_hal);
    uint64_t next_tx = t0, next_hb = t0 + HEARTBEAT_US;

    printf("  role TX: numbered frames every %lu + %lu us, `h` for help\n",
           (unsigned long)FRAME_AIRTIME_US, (unsigned long)s_gap_us);

    for (;;) {
        int ch = getchar_timeout_us(0);
        uint64_t now = hal_now_us(s_hal);

        if (driving && !hal_tx_busy(s_hal)) {
            hal_tx_drive(s_hal, false);
            driving = false;
            report_stalls();
            s_tx_sent++;
            s_tx_seq++;
            next_tx = hal_now_us(s_hal) + s_gap_us;
            if (s_tx_sent % PROGRESS_EVERY == 0u)
                printf("  sent %lu frames, next seq %u, %lu s, refused %lu, stalls %lu\n",
                       (unsigned long)s_tx_sent, (unsigned)s_tx_seq, (unsigned long)elapsed_s(t0),
                       (unsigned long)s_tx_refused, (unsigned long)hal_pico_tx_stalls(0));
        }

        if (!driving && (one_shot || (!s_paused && now >= next_tx))) {
            one_shot = false;
            if (tx_start()) driving = true;
            else next_tx = now + s_gap_us;
        }

        if (ch == PICO_ERROR_TIMEOUT) {
            if (now >= next_hb) {
                next_hb += HEARTBEAT_US;
                printf("  hb sent %lu seq %u %s stalls %lu\n",
                       (unsigned long)s_tx_sent, (unsigned)s_tx_seq,
                       s_paused ? "paused" : "running",
                       (unsigned long)hal_pico_tx_stalls(0));
            }
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            line[len] = 0;
            if (len) tx_dispatch(line, &one_shot);
            len = 0;
        } else if (len + 1 < sizeof line) {
            line[len++] = (char)ch;
        }
    }
}

/* ======================================================================
 * Receiver
 * ====================================================================== */

typedef struct {
    uint64_t since;
    uint32_t seen, good, bad_crc, lost;
    uint32_t bit_errors, bits;
    uint64_t margin_sum;
    uint32_t last_fail_s;     /* seconds into the run of the last non-good event */
    uint32_t worst_gap;       /* longest run of missing frames */
} rx_stats_t;

static frame_rx_t s_rx;
static rx_stats_t s_st;
static uint16_t   s_expect;
static bool       s_synced;      /* s_expect is meaningful */
static bool       s_verbose;
static bool       s_raw_armed;
static uint16_t   s_stream_decimate;

static void rx_zero(void)
{
    memset(&s_st, 0, sizeof s_st);
    s_st.since = hal_now_us(s_hal);
    s_synced = false;
}

static void rx_print_stats(void)
{
    uint32_t total = s_st.good + s_st.bad_crc + s_st.lost;
    double fer = total ? 1.0 - (double)s_st.good / (double)total : 1.0;
    double ber = s_st.bits ? (double)s_st.bit_errors / (double)s_st.bits : 0.5;

    printf("  %6lu s  good %lu  crc %lu  lost %lu   FER %.4f   BER %.2e   "
           "margin %lu  worst gap %lu  last fail %lu s\n",
           (unsigned long)elapsed_s(s_st.since),
           (unsigned long)s_st.good, (unsigned long)s_st.bad_crc,
           (unsigned long)s_st.lost, fer, ber,
           (unsigned long)(s_st.good + s_st.bad_crc
                           ? s_st.margin_sum / (s_st.good + s_st.bad_crc) : 0),
           (unsigned long)s_st.worst_gap, (unsigned long)s_st.last_fail_s);
    printf("           false syncs %lu, dma overruns %lu, core-1 load %u%%\n",
           (unsigned long)s_rx.false_syncs, (unsigned long)hal_pico_overruns(),
           hal_pico_core1_load());
}

/*
 * A frame arrived. Good frames carry a trusted sequence number, and a jump
 * in it is that many frames lost. A bad-CRC frame is assumed to be the next
 * one expected: its header is probably right, its payload is not trusted.
 * A jump backwards means the transmitter rebooted; resynchronise silently.
 *
 * Bit errors are counted in bad-CRC frames too, against the pattern the
 * sequence implies -- otherwise the column reads 0 at the knee while FER
 * reads 0.13, which M6 found and left. The sequence for a failed frame is
 * the header's ten bits where they agree with the next one expected (or
 * run a little ahead of it, a lost frame), else the expected one; a wrong
 * guess costs at most one frame's worth of half-wrong bits.
 */
static uint16_t failed_frame_seq(const frame_hdr_t *h)
{
    uint16_t low  = (uint16_t)(h->frag_index | ((uint16_t)h->record_id << 4));
    uint16_t seq  = (uint16_t)((s_expect & 0xFC00u) | low);
    uint16_t gap  = (uint16_t)(seq - s_expect);

    if (!s_synced) return seq;
    if (gap < 16u) return seq;                       /* header agrees, or ahead */
    if ((uint16_t)(seq + 0x400u - s_expect) < 16u) return (uint16_t)(seq + 0x400u);
    return s_expect;
}

static void rx_frame(frame_rx_result_t res)
{
    uint32_t now_s = elapsed_s(s_st.since);

    s_st.seen++;
    if (s_raw_armed) { tlm_usb_raw_trigger(); s_raw_armed = false; }

    if (res == FRAME_RX_GOOD) {
        uint16_t seq = payload_seq(frame_rx_payload(&s_rx));
        uint32_t errs = count_bit_errors(frame_rx_payload(&s_rx), seq);
        uint16_t gap = (uint16_t)(seq - s_expect);

        if (s_synced && gap != 0u && gap < 0x8000u) {
            s_st.lost += gap;
            s_st.last_fail_s = now_s;
            if (gap > s_st.worst_gap) s_st.worst_gap = gap;
            if (s_verbose)
                printf("    %lu frames missing before seq %u\n", (unsigned long)gap, seq);
        }
        s_expect = (uint16_t)(seq + 1u);
        s_synced = true;

        s_st.good++;
        s_st.bit_errors += errs;
        s_st.bits       += HANDOFF_FRAG_PAYLOAD * 8u;
        s_st.margin_sum += s_rx.last_margin;

        if (s_verbose)
            printf("    seq %5u: CRC ok  margin %u  %lu bit errors\n",
                   (unsigned)seq, (unsigned)s_rx.last_margin, (unsigned long)errs);
    } else {
        const frame_hdr_t *h = frame_rx_hdr(&s_rx);
        uint16_t seq  = failed_frame_seq(h);
        uint32_t errs = count_bit_errors(frame_rx_payload(&s_rx), seq);

        s_st.bad_crc++;
        s_st.last_fail_s = now_s;
        s_st.margin_sum += s_rx.last_margin;
        s_st.bit_errors += errs;
        s_st.bits       += HANDOFF_FRAG_PAYLOAD * 8u;
        if (s_synced) s_expect++;
        if (s_verbose)
            printf("    seq %5u?: CRC BAD  margin %u  hdr %u/%u rec %u  %lu bit errors\n",
                   (unsigned)seq, (unsigned)s_rx.last_margin,
                   h->frag_index, h->frag_count, h->record_id, (unsigned long)errs);
    }

    if (s_st.seen % PROGRESS_EVERY == 0u) rx_print_stats();
}

/* Core 0's half of §3.3: pop chip energies and push them through the frame
 * receiver, always, so the receiver is never anything but primed. */
static void rx_pump(void)
{
    uint16_t chips[64];
    size_t n, i;

    while ((n = hal_rx_chips(s_hal, chips, count_of(chips))) > 0) {
        for (i = 0; i < n; i++) {
            frame_rx_result_t r;

            if (s_stream_decimate)
                hal_telemetry(s_hal, HAL_TLM_SCORE, &chips[i], sizeof chips[i]);

            r = frame_rx_push(&s_rx, chips[i]);
            if (r != FRAME_RX_NONE) rx_frame(r);
        }
    }
}

/* The self loop re-queues its carrier a frame at a time, so anything that
 * spins waiting must keep pumping it or the transmitter falls silent in the
 * middle of the measurement and `m` reads the quiet it just caused. */
static void selfloop_pump(void);

/* Chip energy over a window, mean and max, in tenths of an LSB. The frame
 * receiver keeps running underneath, so the window costs no frames. */
static uint32_t rx_energy(uint32_t us, uint32_t *max_out)
{
    uint16_t chips[64];
    uint64_t sum = 0, n = 0;
    uint32_t max = 0;
    uint64_t until = hal_now_us(s_hal) + us;
    size_t k, i;

    while (hal_now_us(s_hal) < until) {
        selfloop_pump();
        while ((k = hal_rx_chips(s_hal, chips, count_of(chips))) > 0) {
            for (i = 0; i < k; i++) {
                frame_rx_result_t r;
                sum += chips[i];
                if (chips[i] > max) max = chips[i];
                r = frame_rx_push(&s_rx, chips[i]);
                if (r != FRAME_RX_NONE) rx_frame(r);
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
    while (tlm_usb_raw_busy()) { rx_pump(); selfloop_pump(); }

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
 * The bench point. With frames flowing, the max chip energy is the received
 * fundamental A in LSB (a mark chip), and the mean is about half of it
 * (Manchester is half marks). With the transmitter paused, the raw RMS is
 * sigma. Both together are the simulator's operating point.
 */
static void rx_measure(void)
{
    uint32_t mean, max, sig, floor;
    int32_t  mean_code, floor_mean;

    printf("\n  --- measure at %lu Hz, %lu ms ---\n",
           (unsigned long)hal_pico_carrier_hz(), (unsigned long)(MEASURE_US / 1000u));
    mean = rx_energy(MEASURE_US, &max);
    sig  = raw_rms(&mean_code);
    floor = hal_pico_noise_floor(&floor_mean);

    printf("    chip energy:  mean "); print_tenths(mean);
    printf(" LSB, max %lu LSB (A, if frames are flowing)\n", (unsigned long)max);
    printf("    raw samples:  "); print_tenths(sig);
    printf(" LSB RMS about code %ld (sigma, if the transmitter is paused)\n",
           (long)mean_code);
    printf("    M4 floor:     "); print_tenths(floor);
    printf(" LSB RMS about code %ld (on-die sensor, at boot)\n", (long)floor_mean);
    if (max == 0u)
        printf("    nothing received: is the other board transmitting on this carrier?\n");
}

/* ---- self loop ---------------------------------------------------------
 *
 * Drive this board's own carrier while this board is listening, so `m` reads
 * what its OWN transmitter puts into its OWN receiver. No jumper is needed:
 * on the PCB the transmitter reaches the pad through R1 and the receiver
 * leaves it through R2, so the pad node is already a loop. apps/loopback is
 * the M5 jumper-wire version and proves the DSP; this proves the board.
 *
 * It splits the one question a quiet receiver cannot answer about itself:
 *
 *   loud  - the pad node, R1, R2 and the whole AFE are intact, so a quiet
 *           channel really is a quiet channel: nothing is coupling in.
 *   dead  - the break is on this board. A pad node shorted to ground reads
 *           dead here too, because the short takes the transmitter with it.
 *
 * Marks only, re-queued a frame at a time, so the carrier is continuous
 * rather than Manchester: this is an amplitude measurement, not a decode.
 */
static bool s_selfloop;

static void selfloop_pump(void)
{
    if (!s_selfloop) return;
    if (hal_tx_busy(s_hal)) return;

    memset(s_chips, 1, sizeof s_chips);
    hal_tx_drive(s_hal, true);
    if (hal_tx_chips(s_hal, s_chips, sizeof s_chips) != sizeof s_chips) {
        hal_tx_drive(s_hal, false);
        s_selfloop = false;
        printf("    self loop: the transmitter refused the chips, stopped\n");
    }
}

static void selfloop_set(bool on)
{
    s_selfloop = on;
    if (!on) hal_tx_drive(s_hal, false);
    printf("  self loop %s%s\n", on ? "ON" : "off",
           on ? " - own carrier into our own receiver; `m` now reads it" : "");
}

static void rx_help(void)
{
    printf("\n  c 40|200  carrier, kHz\n"
           "  s         stats now\n"
           "  z         zero the stats\n"
           "  v         per-frame lines on / off\n"
           "  m         chip energy mean / max, raw RMS\n"
           "  r         raw burst across the next frame, dumped\n"
           "  t [N]     stream every Nth chip energy; t 0 stops\n"
           "  x [0|1]   self loop: our own carrier into our own receiver\n"
           "  f         clock tree, measured\n"
           "  h         this\n");
}

static void rx_dispatch(const char *line)
{
    uint32_t arg;
    bool have_arg;

    switch (parse(line, &arg, &have_arg)) {
    case 0: return;
    case 'c':
        cmd_carrier(have_arg ? arg : HANDOFF_CARRIER_HZ / 1000u, &s_rx);
        rx_zero();
        break;
    case 's': rx_print_stats(); break;
    case 'z': rx_zero(); printf("    stats zeroed\n"); break;
    case 'v':
        s_verbose = !s_verbose;
        printf("    per-frame lines %s\n", s_verbose ? "on" : "off");
        break;
    case 'm': rx_measure(); break;
    case 'r':
        /* Arm on the end of a frame, so the 200 ms burst starts in the gap
         * and holds the whole of the next one, preamble included. */
        printf("\n  --- raw burst across the next frame ---\n");
        s_raw_armed = true;
        while (s_raw_armed) { rx_pump(); selfloop_pump(); }
        while (tlm_usb_raw_busy()) { rx_pump(); selfloop_pump(); }
        tlm_usb_raw_dump();
        break;
    case 't':
        s_stream_decimate = (uint16_t)(have_arg ? arg : 1u);
        tlm_usb_init(s_stream_decimate);
        printf("    stream %s\n", s_stream_decimate ? "on" : "off");
        break;
    case 'x':
        selfloop_set(have_arg ? arg != 0u : !s_selfloop);
        break;
    case 'f': clocks_print(); break;
    case 'h': case '?': rx_help(); break;
    default:  printf("    ? (h for help)\n"); break;
    }
}

static void run_rx(void)
{
    char line[32];
    size_t len = 0;
    uint64_t next_hb;

    frame_rx_init(&s_rx);
    rx_zero();
    next_hb = hal_now_us(s_hal) + HEARTBEAT_US;

    printf("  role RX: listening, stats every %u frames, `h` for help\n", PROGRESS_EVERY);

    for (;;) {
        int ch = getchar_timeout_us(0);

        rx_pump();
        selfloop_pump();

        if (ch == PICO_ERROR_TIMEOUT) {
            /* A placed heartbeat: if this stops, the hang is on core 0. */
            if (hal_now_us(s_hal) >= next_hb) {
                next_hb += HEARTBEAT_US;
                printf("  hb %lu s seen %lu good %lu crc %lu lost %lu level %lu "
                       "overruns %lu load %u%%\n",
                       (unsigned long)elapsed_s(s_st.since),
                       (unsigned long)s_st.seen, (unsigned long)s_st.good,
                       (unsigned long)s_st.bad_crc, (unsigned long)s_st.lost,
                       (unsigned long)hal_rx_carrier_level(s_hal),
                       (unsigned long)hal_pico_overruns(), hal_pico_core1_load());
            }
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            line[len] = 0;
            if (len) rx_dispatch(line);
            len = 0;
        } else if (len + 1 < sizeof line) {
            line[len++] = (char)ch;
        }
    }
}

/* ---------------------------------------------------------------------- */

static int read_role(void)
{
#if defined(LINKTEST_ROLE)
    return LINKTEST_ROLE;
#else
    gpio_init(PIN_ROLE);
    gpio_set_dir(PIN_ROLE, GPIO_IN);
    gpio_pull_up(PIN_ROLE);
    sleep_ms(2);
    return gpio_get(PIN_ROLE) ? ROLE_RX : ROLE_TX;
#endif
}

int main(void)
{
    int32_t noise_mean;
    uint32_t noise;

    stdio_init_all();
    sleep_ms(2000);          /* let the USB console attach before the report */

    s_role = read_role();

#if defined(LINKTEST_ROLE)
    printf("\nhandoff linktest (M6: two boards over a wire) — %s (role compiled in)\n",
           s_role == ROLE_TX ? "TRANSMITTER" : "RECEIVER");
#else
    printf("\nhandoff linktest (M6: two boards over a wire) — %s\n",
           s_role == ROLE_TX ? "TRANSMITTER (GP14 strapped)" : "RECEIVER (GP14 open)");
#endif
    printf("  carrier %d Hz, ADC %d Hz, Goertzel N=%d bin %d, %d chips/s, %d bps\n",
           HANDOFF_CARRIER_HZ, HANDOFF_ADC_FS_HZ, HANDOFF_GZ_N, HANDOFF_GZ_BIN,
           HANDOFF_CHIP_RATE_HZ, HANDOFF_BIT_RATE_BPS);
    printf("  frame %d chips, %lu us airtime, %d-byte payload\n",
           FRAME_TOTAL_CHIPS, (unsigned long)FRAME_AIRTIME_US, HANDOFF_FRAG_PAYLOAD);

    /*
     * Design §10.4: pin the SMPS into fixed-frequency PWM before the ADC goes
     * live, so its switching noise lands in one predictable place instead of
     * wandering across the ADC band. GP23 is the plain Pico 2's mode pin; on
     * the 2 W it is the CYW43 power enable and the mode control moves to
     * WL_GPIO1. Measured 24 Sep: without this the 200 kHz bin idles at mean
     * 34 LSB with bursts to 400, against a skin signal of 10-16.
     */
    if (cyw43_arch_init() == 0)
        cyw43_arch_gpio_put(CYW43_WL_GPIO_SMPS_PIN, true);
    else
        printf("  cyw43_arch_init failed: SMPS still in power-save\n");

    s_hal = hal_pico_init();

    noise = hal_pico_noise_floor(&noise_mean);
    printf("  M4 noise floor "); print_tenths(noise);
    printf(" LSB RMS (mean code %ld), core 1 up\n", (long)noise_mean);

    if (s_role == ROLE_TX) run_tx();
    else                   run_rx();
}
