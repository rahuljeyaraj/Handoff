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
#include "beacon.h"   /* the settle and the cycle it belongs to */
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
 * Link v2 step 2 -- the two-tone generator, self-measured
 * ======================================================================
 *
 * No scope and no second board: pio_carrier_measure_hz() runs a second state
 * machine counting rising edges on the pad, independent of whatever is driving
 * it. Check 2a is that both tones come out within 0.1 % of nominal.
 *
 * The input buffer has to be on for the counter to see the pad, and the
 * shipped link keeps it off (RP2350-E9, see pio_carrier_sense) -- so it is
 * turned on around the gate and off again, which is what bringup does.
 */
#define FSK_GATE_US 200000u

/* Both live in the transmitter section below; the alignment check clocks a
 * real encoded frame out of the two-tone generator. */
static uint8_t s_chips[FRAME_TOTAL_CHIPS];
static size_t  encode(uint16_t seq);

static uint32_t ppm_err(uint32_t got, uint32_t want)
{
    uint32_t d = got > want ? got - want : want - got;
    return want ? (uint32_t)(((uint64_t)d * 1000000u + want / 2u) / want) : 0u;
}

static void fsk_start(void)
{
    if (!pio_carrier_fsk_active()) {
        pio_carrier_fsk_init();
        printf("    two-tone generator has the pad; `y 9` hands it back\n");
    }
}

/*
 * Hand the pad back to the v1 generator.
 *
 * LINK V2 STEP 6 CHANGED WHAT THIS COSTS. The two-tone generator is the
 * LINK's transmitter now, not an instrument borrowing the pad, so this stops
 * the link from transmitting at all until `y` takes it back. Before step 6 it
 * simply undid an instrument. The warning is the whole change.
 */
static void fsk_stop(void)
{
    if (!pio_carrier_fsk_active()) return;
    pio_carrier_sense(false);
    pio_carrier_drive(false);
    pio_carrier_init(hal_pico_carrier_hz());   /* v1 generator, v1 divider */
    pio_carrier_sense(false);
    pio_carrier_drive(false);
    printf("    v1 generator has the pad again, pad high-Z\n");
    printf("    THE LINK CANNOT TRANSMIT until `y 0`, `y 1` or `y 3`\n");
}

static uint32_t fsk_measure(int tone, uint32_t want, bool verdict)
{
    uint32_t got, err, duty;

    fsk_start();
    pio_carrier_fsk_tone(tone);
    pio_carrier_sense(true);
    got  = pio_carrier_measure_hz(FSK_GATE_US);
    duty = pio_carrier_duty_ppm(FSK_GATE_US);
    pio_carrier_sense(false);

    err = ppm_err(got, want);
    printf("    tone %c  bin %2u  nominal %lu Hz  measured %lu Hz  %lu ppm",
           tone ? 'B' : 'A',
           (unsigned)(tone ? HANDOFF_TONE_B_BIN : HANDOFF_TONE_A_BIN),
           (unsigned long)want, (unsigned long)got, (unsigned long)err);
    if (verdict) printf("   %s", err <= 1000u ? "PASS" : "FAIL");   /* 0.1 % */
    /*
     * The duty, ON THE PAD. This is the number the guard bins are supposed to
     * be a proxy for, and reading it here is how a generator that slipped is
     * told apart from an amplifier that distorted.
     */
    printf("\n            duty %lu.%04lu %% of the gate",
           (unsigned long)(duty / 10000u), (unsigned long)(duty % 10000u));
    if (verdict) printf("   %s",
           (duty > 495000u && duty < 505000u) ? "50 % within 0.5 pt" : "NOT 50 %");
    printf("\n");
    return err;
}

/*
 * Check 2a end to end, with the arithmetic it is checking printed beside the
 * reading -- so the transcript records what the words were as well as what
 * came out of the pad.
 */
static void fsk_walk(void)
{
    uint32_t ea, eb;

    printf("\n  --- two-tone generator, %lu ms gate ---\n",
           (unsigned long)(FSK_GATE_US / 1000u));
    printf("    chip word A %08lX = %u periods of %u cycles (loop %u, y %u)\n",
           (unsigned long)HANDOFF_FSK_WORD_A, (unsigned)HANDOFF_FSK_PERIODS_A,
           (unsigned)HANDOFF_FSK_PERIOD_A, (unsigned)HANDOFF_FSK_ISR_A,
           (unsigned)HANDOFF_FSK_Y_A);
    printf("    chip word B %08lX = %u periods of %u cycles (loop %u, y %u)\n",
           (unsigned long)HANDOFF_FSK_WORD_B, (unsigned)HANDOFF_FSK_PERIODS_B,
           (unsigned)HANDOFF_FSK_PERIOD_B, (unsigned)HANDOFF_FSK_ISR_B,
           (unsigned)HANDOFF_FSK_Y_B);
    printf("    both chips %u cycles = %lu us\n",
           (unsigned)HANDOFF_FSK_CHIP_CYCLES, (unsigned long)HANDOFF_CHIP_US);

    ea = fsk_measure(0, (uint32_t)HANDOFF_TONE_A_HZ, true);
    eb = fsk_measure(1, (uint32_t)HANDOFF_TONE_B_HZ, true);

    printf("    2a %s\n", (ea <= 1000u && eb <= 1000u)
           ? "PASSED: both tones within 0.1 % of nominal"
           : "FAILED: a tone is off by more than 0.1 %");
}

/*
 * Check 2c, done at the pad rather than through the receiver.
 *
 * The hazard pio_carrier.pio warns about is a cycle-count imbalance between
 * the two symbol paths: it shifts every chip after the first, so errors
 * accumulate down the frame rather than scattering. The brief proposes finding
 * that by decoding a known 624-chip pattern through the self loop -- but the
 * self loop saturates the receiver (mean code 3564 of 4095 on this board, and
 * 3638 for the v1 generator, so it is the bench and not the generator), and a
 * decode through a railed ADC proves nothing either way.
 *
 * Counting edges proves MORE, and needs no receiver at all. Each chip is a
 * whole number of tone periods, and one period is one rising edge:
 *
 *      tone A chip  ->  HANDOFF_FSK_PERIODS_A rising edges
 *      tone B chip  ->  HANDOFF_FSK_PERIODS_B rising edges
 *
 * so the edge count across any chip pattern is exact arithmetic, and it is
 * exact only if every chip got precisely its own cycles. One chip short or
 * long anywhere in the frame shows up in the total. An imbalance that shifted
 * chips without losing periods would still hold the count -- which is why the
 * duty and frequency measurements above are run as well: together they pin
 * the period, its two halves, and the chip boundary.
 */
static void fsk_align_one(const char *what, const uint8_t *chips, size_t n)
{
    uint32_t want = 0, got;
    uint64_t until;
    size_t i;

    for (i = 0; i < n; i++)
        want += chips[i] ? (uint32_t)HANDOFF_FSK_PERIODS_B
                         : (uint32_t)HANDOFF_FSK_PERIODS_A;

    fsk_start();
    /* The check leaves the pad released when it finishes, so a second run
     * would count a high-Z pad: take it back every time. */
    pio_carrier_drive(true);
    /*
     * Stop whatever the generator was doing BEFORE opening the gate. A 2a
     * measurement leaves a tone looping, and fsk_send() spends tens of
     * microseconds building 624 words before its DMA starts -- so a gate
     * opened first counts that tail and reads eleven edges long. Found by
     * this check disagreeing with itself on its first pattern only.
     */
    pio_carrier_reset();
    pio_carrier_sense(true);
    pio_carrier_count_begin();
    pio_carrier_fsk_send(chips, n);

    /*
     * busy() clears when the DMA is done and the FIFO empty, but the joined
     * FIFO holds eight words -- eight chips, 2 ms -- and the OSR one more. So
     * wait out the airtime from the DMA start instead, plus a chip of margin.
     */
    until = pio_carrier_started_us() + (uint64_t)(n + 1u) * HANDOFF_CHIP_US;
    while (hal_now_us(s_hal) < until) tight_loop_contents();

    got = pio_carrier_count_end();
    pio_carrier_sense(false);

    printf("    %-22s %4u chips  want %6lu edges  got %6lu   %s\n",
           what, (unsigned)n, (unsigned long)want, (unsigned long)got,
           got == want ? "EXACT" : "MISMATCH");
}

static void fsk_align(void)
{
    static uint8_t pat[FRAME_TOTAL_CHIPS];
    size_t n = FRAME_TOTAL_CHIPS, i, frame_n;

    printf("\n  --- chip alignment by edge count, at the pad ---\n");
    printf("    tone A chip = %u periods, tone B chip = %u periods\n",
           (unsigned)HANDOFF_FSK_PERIODS_A, (unsigned)HANDOFF_FSK_PERIODS_B);

    memset(pat, 0, n);
    fsk_align_one("all tone A", pat, n);

    memset(pat, 1, n);
    fsk_align_one("all tone B", pat, n);

    for (i = 0; i < n; i++) pat[i] = (uint8_t)(i & 1u);
    fsk_align_one("alternating A/B", pat, n);

    /* A real encoded frame: preamble, marker, Manchester body and CRC, which
     * is the pattern the link will actually clock out. */
    frame_n = encode(0);
    fsk_align_one("an encoded frame", s_chips, frame_n);

    /* And the pad back to where the link leaves it. */
    pio_carrier_drive(false);
}

/*
 * ---- y 4: THE SETTLE, MEASURED (link v2 step 7) -------------------------
 *
 * HANDOFF_TRIG_SETTLE_US is how long a band stays deaf after its own beacon,
 * and until now it was 6000 because 6000 worked: brief §8 asks for the AFE's
 * own recovery read directly instead. This reads it.
 *
 * Drive one beacon's worth of tone into our own pad, release it, and watch
 * the presence detector — which is the same detector the trigger listens
 * through — until it stops calling the channel busy. That interval IS the
 * quantity, and it needs no second board and no scope: the thing being
 * measured is our own amplifier coming out of saturation into our own input,
 * which is a one-board fault by definition.
 *
 * WHAT IT MEASURES IS NOT ONLY THE AMPLIFIER. The reading includes the ADC
 * block latency, because presence is decided on core 1 from samples the DMA
 * has already delivered, and the trigger sees the channel through that same
 * delay. That is correct rather than a contaminant: the settle has to cover
 * both, and a figure that left the ring out would be short by exactly the
 * amount that matters.
 *
 * THE LAST BUSY WINDOW, NOT THE FIRST QUIET ONE. A decaying burst crosses the
 * CFAR threshold and comes back over it, so the first quiet verdict is an
 * underestimate and a noisy one. The whole observation window is watched and
 * the LAST busy verdict in it is what is reported.
 */
#define SETTLE_RUNS       8u
#define SETTLE_WATCH_US   200000u
#define SETTLE_QUIET_US   400000u   /* between runs, for the CFAR boxcar */
#define SETTLE_TRACE      24u
#define SETTLE_TRACE_STEP  1000u   /* the interesting part is the first ms */

/* One burst. Returns the time of the last busy verdict inside the watch,
 * or 0 if the detector never read busy at all. */
static uint32_t settle_once(uint32_t drive_us, uint32_t *trace_us,
                            uint32_t *trace_sig, uint32_t *trace_noi,
                            uint32_t trace_n)
{
    hal_pico_presence_t pr;
    absolute_time_t t0;
    uint32_t last_busy_us = 0, next_trace = 0;
    bool saw_busy = false;

    pio_carrier_fsk_tone(1);
    pio_carrier_drive(true);
    sleep_us(drive_us);

    /* High-Z, not driven low: a driven pad still loads the electrode. */
    pio_carrier_drive(false);
    t0 = get_absolute_time();

    for (;;) {
        const uint32_t at = (uint32_t)absolute_time_diff_us(t0, get_absolute_time());
        if (at >= SETTLE_WATCH_US) break;
        hal_pico_presence(&pr);
        if (pr.busy) { last_busy_us = at; saw_busy = true; }
        if (trace_n && next_trace < trace_n &&
            at >= next_trace * SETTLE_TRACE_STEP) {
            trace_us[next_trace]  = at;
            trace_sig[next_trace] = pr.signal;
            trace_noi[next_trace] = pr.noise;
            next_trace++;
        }
    }
    sleep_ms(SETTLE_QUIET_US / 1000u);
    return saw_busy ? last_busy_us : 0u;
}

/*
 * ---- y 4: THE SETTLE, MEASURED (link v2 step 7) -------------------------
 *
 * HANDOFF_TRIG_SETTLE_US is how long a band stays deaf after its own beacon,
 * and until now it was 6000 because 6000 worked: brief S8 asks for the AFE's
 * own recovery read directly instead. This reads it.
 *
 * Drive tone into our own pad, release it, and watch the presence detector --
 * the same detector the trigger listens through -- until it stops calling the
 * channel busy. No second board and no scope: the thing being measured is our
 * own amplifier coming out of saturation into our own input, which is a
 * one-board fault by definition.
 *
 * THREE DRIVE LENGTHS, because one would not be a measurement. If the
 * recovery is an RC discharging a coupling cap that the drive charged, it
 * grows with how long the drive lasted -- and that matters a great deal here,
 * because step 7 took the transmission from v1's 10 ms shout to a 28 ms
 * beacon. A settle that is flat across the three is a fixed recovery; one
 * that grows is the beacon paying for its own length twice.
 *
 * WHAT IT MEASURES IS NOT ONLY THE AMPLIFIER. The reading includes the ADC
 * block latency, because presence is decided on core 1 from samples the DMA
 * has already delivered, and the trigger sees the channel through that same
 * delay. That is correct rather than a contaminant: the settle has to cover
 * both, and a figure that left the ring out would be short by exactly the
 * amount that matters.
 *
 * THE LAST BUSY WINDOW, NOT THE FIRST QUIET ONE. A decaying burst crosses the
 * CFAR threshold and comes back over it -- and the reference itself moves,
 * because a saturated receiver puts distortion in the guard bins too and the
 * boxcar holds that for a preamble afterwards. The whole observation window
 * is watched and the LAST busy verdict in it is reported, with a trace of the
 * two sides of the comparison beside it so the two effects can be told apart.
 */
static void fsk_settle(void)
{
    static const uint32_t k_drive_us[] = {
        FRAME_BEACON_AIRTIME_US / 4u,       /* about v1's flat shout      */
        FRAME_BEACON_AIRTIME_US / 2u,
        FRAME_BEACON_AIRTIME_US,            /* one beacon                 */
    };
    uint32_t trace_us[SETTLE_TRACE], trace_sig[SETTLE_TRACE], trace_noi[SETTLE_TRACE];
    size_t d;

    if (!hal_pico_bank_on()) {
        printf("    the bank is OFF, so there is no detector. `n 1` first.\n");
        return;
    }

    printf("\n  --- settle: our own amplifier, after its own drive ---\n");
    printf("    watching %lu us after release, %lu runs each, %lu us quiet between\n",
           (unsigned long)SETTLE_WATCH_US, (unsigned long)SETTLE_RUNS,
           (unsigned long)SETTLE_QUIET_US);

    fsk_start();

    for (d = 0; d < sizeof k_drive_us / sizeof k_drive_us[0]; d++) {
        const uint32_t drive = k_drive_us[d];
        uint32_t run, worst = 0, sum = 0, best = 0xFFFFFFFFu, never = 0;

        for (run = 0; run < SETTLE_RUNS; run++) {
            const bool want_trace = (run == 0u);
            const uint32_t last = settle_once(drive,
                                              trace_us, trace_sig, trace_noi,
                                              want_trace ? SETTLE_TRACE : 0u);
            if (!last) { never++; continue; }
            sum += last;
            if (last > worst) worst = last;
            if (last < best)  best  = last;
        }

        if (never == SETTLE_RUNS) {
            printf("    drive %6lu us: never busy -- the pad is not driven, "
                   "or the bank is not scoring\n", (unsigned long)drive);
            continue;
        }

        printf("    drive %6lu us: last busy min %6lu mean %6lu max %6lu us"
               " (%lu runs, %lu silent)\n",
               (unsigned long)drive, (unsigned long)best,
               (unsigned long)(sum / (SETTLE_RUNS - never)),
               (unsigned long)worst,
               (unsigned long)(SETTLE_RUNS - never), (unsigned long)never);

        {
            size_t i;
            printf("        ms:signal/noise ");
            for (i = 0; i < SETTLE_TRACE; i++)
                printf(" %lu:%lu/%lu", (unsigned long)(trace_us[i] / 1000u),
                       (unsigned long)trace_sig[i], (unsigned long)trace_noi[i]);
            printf("\n");
        }
    }

    printf("    ADC block latency %lu us is inside every figure above\n",
           (unsigned long)HAL_PICO_RX_LATENCY_US);
    printf("    HANDOFF_TRIG_SETTLE_US is %lu us\n",
           (unsigned long)HANDOFF_TRIG_SETTLE_US);
}

static void fsk_dispatch(uint32_t arg, bool have_arg)
{
    if (!have_arg)    { fsk_walk(); return; }
    if (arg == 2u)    { fsk_align(); return; }
    if (arg == 4u)    { fsk_settle(); return; }
    if (arg == 9u)    { fsk_stop(); return; }

    fsk_start();
    /*
     * Take the pad, every time, and say so. fsk_init() takes it on the first
     * call, but `y 2` hands it back when it finishes -- so on a bench where
     * this board is the transmitter for another one, a `y 0` after an
     * alignment check would drive a high-Z pad and the far board would read a
     * quiet room. That is a reading the transcript cannot tell from a dead
     * coupling path, so the state is printed beside the tone.
     */
    pio_carrier_drive(true);

    if (arg == 3u) {
        pio_carrier_fsk_alt();
        printf("    driving tone A / tone B on ALTERNATE chips, unbroken\n");
    } else {
        pio_carrier_fsk_tone(arg ? 1 : 0);
        printf("    driving tone %c (%lu Hz) unbroken\n",
               arg ? 'B' : 'A',
               (unsigned long)(arg ? HANDOFF_TONE_B_HZ : HANDOFF_TONE_A_HZ));
    }
    printf("    pad %s; `y 9` stops\n",
           pio_carrier_is_driving() ? "DRIVEN" : "high-Z -- nothing is going out");
}

/* ======================================================================
 * Transmitter
 * ====================================================================== */

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
           "  y [0..4|9] two tones: check, drive A / B, 2 align,\n"
           "              3 alternating chips, 4 measure the settle\n"
           "              after one beacon, 9 stop\n"
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
    case 'y': fsk_dispatch(arg, have_arg); break;
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
    int32_t chips[64];
    size_t n, i;

    while ((n = hal_rx_chips(s_hal, chips, count_of(chips))) > 0) {
        for (i = 0; i < n; i++) {
            frame_rx_result_t r;

            if (s_stream_decimate) {
                /* The score stream is 16-bit by contract (hal.h, TLM_SCORE),
                 * so a signed chip goes out as its magnitude: the sign is the
                 * decision and the magnitude is the level, and a trace of a
                 * live link wants the level. */
                const int32_t d = chips[i];
                const uint32_t a = (uint32_t)(d < 0 ? -(int64_t)d : d);
                const uint16_t mag = (uint16_t)(a > 0xFFFFu ? 0xFFFFu : a);
                hal_telemetry(s_hal, HAL_TLM_SCORE, &mag, sizeof mag);
            }

            r = frame_rx_push(&s_rx, chips[i]);
            if (r != FRAME_RX_NONE) rx_frame(r);
        }
    }
}

/* The self loop re-queues its carrier a frame at a time, so anything that
 * spins waiting must keep pumping it or the transmitter falls silent in the
 * middle of the measurement and `m` reads the quiet it just caused. */
static void selfloop_pump(void);

/*
 * Level on the probe bin, mean and max, in tenths of an LSB.
 *
 * LINK V2 STEP 6: this used to read CHIP energies off the link's own stream,
 * because the link WAS one bin and retuning it was how `b` walked the bank.
 * The link is two tone bins and a signed difference now, and the retunable
 * Goertzel is an instrument with nothing framing behind it -- so what comes
 * back is Goertzel WINDOW scores rather than chip energies.
 *
 * Two things follow at the console. The max is one window rather than the
 * mean of three, so it reads a little higher than the same bench did before
 * step 6. And retuning costs the receiver nothing, because the receiver is
 * not on this bin any more.
 *
 * TAKEN IN SHORT CHUNKS, and that is the self-loop pump trap, not caution:
 * `x` re-queues its carrier a frame at a time, so anything that spins for
 * two hundred milliseconds without pumping lets the transmitter fall silent
 * and then measures the quiet it caused. The frame receiver is pumped in the
 * same gaps, so a bin walk costs no frames either.
 */
#define PROBE_CHUNK_US 10000u

static uint32_t probe_level(uint32_t us, uint32_t *max_out)
{
    uint64_t sum = 0, n = 0;
    uint32_t max = 0;
    uint32_t left = us;

    while (left) {
        const uint32_t want = left > PROBE_CHUNK_US ? PROBE_CHUNK_US : left;
        const uint32_t windows = (uint32_t)(((uint64_t)want
                                             * (uint32_t)HANDOFF_WINDOW_RATE_HZ)
                                            / 1000000u);
        hal_pico_probe_t p;

        rx_pump();
        selfloop_pump();

        memset(&p, 0, sizeof p);
        (void)hal_pico_probe(windows ? windows : 1u, want + 20000u, &p);
        if (p.windows) {
            sum += (uint64_t)p.mean_tenths * p.windows;
            n   += p.windows;
            if (p.max > max) max = p.max;
        }
        left -= want;
    }

    rx_pump();
    selfloop_pump();
    if (max_out) *max_out = max;
    return n ? (uint32_t)(sum / n) : 0u;
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

    printf("\n  --- measure on bin %u (%lu kHz), %lu ms ---\n",
           (unsigned)hal_pico_rx_bin(),
           (unsigned long)(hal_pico_rx_bin() * (uint32_t)HANDOFF_WINDOW_RATE_HZ / 1000u),
           (unsigned long)(MEASURE_US / 1000u));
    mean = probe_level(MEASURE_US, &max);
    sig  = raw_rms(&mean_code);
    floor = hal_pico_noise_floor(&floor_mean);

    printf("    bin level:    mean "); print_tenths(mean);
    printf(" LSB, max %lu LSB (one Goertzel window)\n", (unsigned long)max);
    printf("    raw samples:  "); print_tenths(sig);
    printf(" LSB RMS about code %ld (sigma, if the transmitter is paused)\n",
           (long)mean_code);
    printf("    M4 floor:     "); print_tenths(floor);
    printf(" LSB RMS about code %ld (on-die sensor, at boot)\n", (long)floor_mean);
    if (max == 0u)
        printf("    nothing on this bin: is the other board transmitting?\n");
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

/*
 * Link v2 step 2b -- the bin bank, one bin at a time.
 *
 * The real bank is step 3; this walks the five bins SEQUENTIALLY with the one
 * Goertzel core 1 already has, retuning between reads. That cannot show the
 * two tones alternating -- for that the bins must be scored in the same window
 * -- but it does show the thing step 4 calls its kill switch, and shows it a
 * step early and on one board: whether a GUARD bin rises while we transmit.
 *
 * Drive one tone with `y 0` or `y 1`, then `b`. Bins 9 and 10 are the tones,
 * 7, 8 and 11 the guards. A guard reading anywhere near the driven tone means
 * the transmitter is leaking outside its bins and the noise reference is
 * poisoned, which is v1's floor in a new hat.
 */
#define BANK_SETTLE_US 60000u    /* the retune transient, several chips */
#define BANK_US       200000u

/*
 * `b 1` sweeps every bin below Nyquist rather than the five the design uses.
 * That is what turns "a guard bin rose" into "here is the harmonic comb", and
 * a comb is readable: a duty-cycle error puts energy in the EVEN harmonics
 * only, while a non-linearity anywhere in the amplifier fills in bins no
 * harmonic of the tone can reach at all.
 */
static void rx_bank_full(void)
{
    uint16_t back = hal_pico_rx_bin();
    uint16_t bin;

    printf("\n  --- every bin, %lu ms each, generator %s ---\n",
           (unsigned long)(BANK_US / 1000u),
           pio_carrier_fsk_active()
               ? (pio_carrier_is_driving() ? "two-tone, DRIVING" : "two-tone, idle")
               : "v1");

    for (bin = 1u; 2u * bin < (uint16_t)HANDOFF_GZ_N; bin++) {
        uint32_t mean, max;

        if (!hal_pico_set_rx_bin(bin)) continue;
        frame_rx_init(&s_rx);
        (void)probe_level(BANK_SETTLE_US, 0);
        mean = probe_level(BANK_US, &max);

        printf("    bin %2u  %3lu kHz  mean ", (unsigned)bin,
               (unsigned long)(bin * (uint32_t)HANDOFF_WINDOW_RATE_HZ / 1000u));
        print_tenths(mean);
        printf(" LSB  max %lu LSB\n", (unsigned long)max);
    }

    hal_pico_set_rx_bin(back);
    frame_rx_init(&s_rx);
    rx_zero();
    printf("    back on bin %u\n", (unsigned)back);
}

static void rx_bank(void)
{
    static const uint16_t k_bins[] = {
        HANDOFF_GUARD_LO_BIN, HANDOFF_GUARD_MID_BIN,
        HANDOFF_TONE_A_BIN, HANDOFF_TONE_B_BIN, HANDOFF_GUARD_HI_BIN
    };
    uint16_t back = hal_pico_rx_bin();
    uint32_t tone_max = 0, guard_max = 0;
    size_t i;

    printf("\n  --- bin bank, %lu ms a bin, generator %s ---\n",
           (unsigned long)(BANK_US / 1000u),
           pio_carrier_fsk_active()
               ? (pio_carrier_is_driving() ? "two-tone, DRIVING" : "two-tone, idle")
               : "v1");

    for (i = 0; i < count_of(k_bins); i++) {
        uint16_t bin = k_bins[i];
        bool tone = bin == HANDOFF_TONE_A_BIN || bin == HANDOFF_TONE_B_BIN;
        uint32_t mean, max;

        if (!hal_pico_set_rx_bin(bin)) { printf("    bin %u refused\n", bin); continue; }
        frame_rx_init(&s_rx);
        (void)probe_level(BANK_SETTLE_US, 0);        /* discard the transient */
        mean = probe_level(BANK_US, &max);

        printf("    bin %2u  %3lu kHz  %-7s  mean ", (unsigned)bin,
               (unsigned long)(bin * (uint32_t)HANDOFF_WINDOW_RATE_HZ / 1000u),
               tone ? "TONE" : "guard");
        print_tenths(mean);
        printf(" LSB  max %lu LSB\n", (unsigned long)max);

        if (tone) { if (max > tone_max)  tone_max  = max; }
        else      { if (max > guard_max) guard_max = max; }
    }

    hal_pico_set_rx_bin(back);
    frame_rx_init(&s_rx);
    rx_zero();

    printf("    loudest tone bin %lu LSB, loudest guard %lu LSB",
           (unsigned long)tone_max, (unsigned long)guard_max);
    if (guard_max) printf("  (ratio %lu:1)", (unsigned long)(tone_max / guard_max));
    printf("\n    back on bin %u\n", (unsigned)back);
}

/* ---- link v2 step 3: the real bank -------------------------------------
 *
 * `b` walks the five bins one after another, retuning between them. This
 * reads all five in the SAME window, which is the only reading the design
 * ever asks for: a ratio between two numbers that shared a gain, a body and
 * an amplifier. Sequential reads cannot answer that question at all.
 *
 *   n        the budget: bank off, then bank on, in this image
 *   n 0 / 1  bank off / on and leave it there
 *   n 2      one capture -- five bins, the guard median, the ratio
 */
#define BANK_CAP_WINDOWS  4000u     /* 200 ms at the window rate */
#define BANK_CAP_WAIT_US  1000000u
#define BANK_BUDGET_US    2000000u
#define BANK_ON_SETTLE_US  200000u

static void bank_wait(uint32_t us)
{
    uint64_t until = hal_now_us(s_hal) + us;
    while (hal_now_us(s_hal) < until) { rx_pump(); selfloop_pump(); }
}

static const char *bank_generator(void)
{
    if (!pio_carrier_fsk_active()) return "v1";
    return pio_carrier_is_driving() ? "two-tone, DRIVING" : "two-tone, idle";
}

/*
 * The operating point, printed under every capture -- because a guard-bin
 * reading only means something at a LINEAR level (design S10, "what 2b could
 * not do"), and nothing else on the console says whether this capture was
 * taken at one.
 *
 * Two independent tells, neither of them a tuned number:
 *
 *   the excursion  an on-bin tone is a sinusoid at the converter, so its peak
 *                  is sqrt(2) x RMS. Add that to the mean code and compare
 *                  against the converter's own 0 and 4095. Crest factor and
 *                  full scale, nothing chosen.
 *   the mean code   saturation rectifies, so the operating point WALKS. A
 *                  quiet board and a loud one reading different mean codes is
 *                  a distortion signature that needs no crest factor assumed
 *                  at all -- it is the one that settled the back-to-back
 *                  bench, where the code went 2309 -> 3622.
 */
static void bank_operating_point(void)
{
    int32_t mean_code;
    uint32_t rms = raw_rms(&mean_code);          /* both in tenths of an LSB */
    int32_t  peak = (int32_t)((uint64_t)rms * 1414u / 1000u / 10u);
    bool railed = mean_code + peak > 4095 || mean_code - peak < 0;

    printf("    raw ");
    print_tenths(rms);
    printf(" LSB RMS about code %ld, so %ld..%ld of 0..4095   %s\n",
           (long)mean_code, (long)(mean_code - peak), (long)(mean_code + peak),
           railed ? "RAILED -- guard bins mean nothing here" : "linear");
}

static void rx_bank_capture(void)
{
    static const char *k_name[GZB_BINS] = { "TONE A", "TONE B",
                                            "guard", "guard", "guard" };
    static const uint16_t k_bin[GZB_BINS] = {
        HANDOFF_TONE_A_BIN, HANDOFF_TONE_B_BIN,
        HANDOFF_GUARD_LO_BIN, HANDOFF_GUARD_MID_BIN, HANDOFF_GUARD_HI_BIN
    };
    hal_pico_bank_t cap;
    uint32_t sig = 0, noise;
    bool was_on = hal_pico_bank_on();
    size_t i;

    if (!was_on) { hal_pico_set_bank(true); bank_wait(BANK_ON_SETTLE_US); }

    if (!hal_pico_bank_capture(BANK_CAP_WINDOWS, BANK_CAP_WAIT_US, &cap)) {
        printf("    capture did not finish: %lu of %lu windows\n",
               (unsigned long)cap.windows, (unsigned long)BANK_CAP_WINDOWS);
        if (!was_on) hal_pico_set_bank(false);
        return;
    }

    printf("\n  --- bank, %lu windows in ONE pass, generator %s ---\n",
           (unsigned long)cap.windows, bank_generator());

    for (i = 0; i < GZB_BINS; i++) {
        const uint32_t div = (i >= GZB_G_LO) ? cap.guard_windows : cap.windows;
        const uint32_t mean = div ? gzb_score(cap.sum[i] / div) : 0u;
        const uint32_t max  = gzb_score(cap.max[i]);

        printf("    bin %2u  %3lu kHz  %-6s  mean %4lu LSB  max %4lu LSB\n",
               (unsigned)k_bin[i],
               (unsigned long)(k_bin[i] * (uint32_t)HANDOFF_WINDOW_RATE_HZ / 1000u),
               k_name[i], (unsigned long)mean, (unsigned long)max);

        if (i < GZB_G_LO && mean > sig) sig = mean;
    }

    noise = cap.windows ? gzb_score(cap.noise_sum / cap.windows) : 0u;
    printf("    guards ran in %lu of %lu windows (every %d)\n",
           (unsigned long)cap.guard_windows, (unsigned long)cap.windows,
           HANDOFF_GUARD_DECIM);
    printf("    signal %lu LSB, guard median %lu LSB", (unsigned long)sig,
           (unsigned long)noise);
    if (noise) printf(", ratio %lu:1", (unsigned long)(sig / noise));
    printf("\n");

    bank_operating_point();

    if (!was_on) hal_pico_set_bank(false);
}

/*
 * ---- link v2 step 5: presence, over an interval ------------------------
 *
 * `n 2` answers "what are the five bins doing". This answers the only
 * question step 5 is gated on: DOES busy TRACK REALITY AS THE LEVEL MOVES.
 *
 * It takes two readings of the detector's own counters and divides, so what
 * comes out is the fraction of windows that read busy over the interval —
 * a rate, which is what a threshold has to be judged as. One instantaneous
 * read cannot tell 100 % busy from 5 %.
 *
 * It does NOT go through hal_rx_busy(): that clears the latch, and an
 * instrument must never take an event away from the link.
 *
 * HOW TO SWEEP THE LEVEL ON THIS BENCH. There is no gain knob and no
 * attenuator, so geometry is the only control (step 4, the hard way). Close
 * the gap in steps with hands withdrawn between them — a hand near a band
 * beats the transmitter — and alternate the far board silent (`y 9`) with it
 * driving (`y 0`, `y 1` or `y 3`), so every hold gives a matched pair. The
 * coupling drifts on its own, so a quiet reading from an earlier run is not a
 * control for this one.
 *
 * WHAT PASSING LOOKS LIKE. Near 0 % busy with the far board silent, at every
 * level; near 100 % with it driving, at every level down to the weakest the
 * link is expected to work at. The number in between is the margin.
 */
#define PRES_SECONDS_DEFAULT 3u
#define PRES_SECONDS_MAX     60u

static void rx_presence(uint32_t seconds)
{
    hal_pico_presence_t a, b;
    uint32_t windows, busy;

    if (seconds == 0u) seconds = PRES_SECONDS_DEFAULT;
    if (seconds > PRES_SECONDS_MAX) seconds = PRES_SECONDS_MAX;

    if (!hal_pico_bank_on()) {
        printf("    the bank is OFF, so there is no detector. `n 1` first.\n");
        return;
    }

    hal_pico_presence(&a);
    if (!a.ready) {
        printf("    the CFAR reference is not full yet (%d cells, one "
               "preamble) — give it a moment\n", (int)HANDOFF_CFAR_CELLS);
        return;
    }

    printf("\n  --- presence, %lu s, generator %s ---\n",
           (unsigned long)seconds, bank_generator());

    bank_wait((uint32_t)seconds * 1000000u);
    hal_pico_presence(&b);

    windows = b.windows - a.windows;
    busy    = b.busy_windows - a.busy_windows;

    printf("    busy in %lu of %lu windows", (unsigned long)busy,
           (unsigned long)windows);
    if (windows) {
        const uint32_t pct = (uint32_t)((uint64_t)busy * 1000u / windows);
        printf("  = %lu.%lu %%", (unsigned long)(pct / 10u),
               (unsigned long)(pct % 10u));
    }
    printf("\n");

    /*
     * THE RATIO IS PRINTED IN POWER, because k is a power ratio and the two
     * have to be comparable on one line. The scores either side are
     * amplitudes -- gz_score_of() takes a square root -- so a signal 18x the
     * reference in LSB is 324x in power, against a k of 16.8. Printing the
     * amplitude ratio beside k made a 19x margin read as though it were
     * scraping past the threshold.
     */
    printf("    signal %lu LSB vs reference %lu LSB", (unsigned long)b.signal,
           (unsigned long)b.noise);
    if (b.noise) {
        const uint32_t pwr = (uint32_t)(((uint64_t)b.signal * b.signal)
                                        / ((uint64_t)b.noise * b.noise));
        printf(", ratio %lu:1 in POWER", (unsigned long)pwr);
        printf("  (k = %lu.%lu, so %lu x clear)",
               (unsigned long)(HANDOFF_CFAR_K_NUM / HANDOFF_CFAR_K_DEN),
               (unsigned long)((HANDOFF_CFAR_K_NUM * 10u / HANDOFF_CFAR_K_DEN) % 10u),
               (unsigned long)(pwr * HANDOFF_CFAR_K_DEN / HANDOFF_CFAR_K_NUM));
    }
    printf("\n");

    bank_operating_point();
}

/*
 * The budget, measured the one way that means anything: the same image, the
 * same board, the same minute, with the bank switched off and then on.
 *
 * Step 1 found core-1 load moving 4 points between two builds of the SAME
 * code, from one unrelated function and where the linker put the image, so a
 * number from another image is not evidence. The difference below is.
 *
 * Cycles a sample is the figure to quote: busy microseconds times the clock,
 * over the samples core 1 actually saw in the interval.
 */
static void bank_budget_leg(const char *what, bool on)
{
    uint64_t b0, t0, b1, t1;
    uint32_t w0, w1, sm0, sm1, ov0, ov1, sps;
    uint64_t busy_us, wall_us, samples, ctenths;

    hal_pico_set_bank(on);
    bank_wait(BANK_ON_SETTLE_US);

    ov0 = hal_pico_overruns();
    w0  = hal_pico_windows();
    sm0 = hal_pico_samples();
    hal_pico_core1_busy(&b0, &t0);

    bank_wait(BANK_BUDGET_US);

    hal_pico_core1_busy(&b1, &t1);
    w1  = hal_pico_windows();
    sm1 = hal_pico_samples();
    ov1 = hal_pico_overruns();
    sps = hal_pico_sps();

    busy_us = b1 - b0;
    wall_us = t1 - t0;
    /* SAMPLES, not windows times GZ_N. With the bank off nothing scores a
     * window at all since step 6, and that is one of this instrument's two
     * legs -- the old expression would divide by zero on it. */
    samples = (uint64_t)(sm1 - sm0);
    ctenths = samples
        ? busy_us * ((uint32_t)HANDOFF_SYS_CLK_HZ / 1000000u) * 10u / samples
        : 0u;

    printf("    %-9s load %2lu %%   windows %6lu   %lu sps   overruns %lu   ",
           what,
           (unsigned long)(wall_us ? busy_us * 100u / wall_us : 0u),
           (unsigned long)(w1 - w0), (unsigned long)sps,
           (unsigned long)(ov1 - ov0));
    print_tenths((uint32_t)ctenths);
    printf(" cycles/sample\n");
}

static void rx_bank_budget(void)
{
    bool was_on = hal_pico_bank_on();

    printf("\n  --- core 1 budget, ONE image, %lu ms a leg, generator %s ---\n",
           (unsigned long)(BANK_BUDGET_US / 1000u), bank_generator());
    printf("    (compare like with like: the load moves with whether frames\n"
           "     are being decoded, so run this idle or with `x` on, not one\n"
           "     leg of each)\n");

    bank_budget_leg("bank off", false);
    bank_budget_leg("bank on", true);

    hal_pico_set_bank(was_on);
    printf("    bank left %s\n", was_on ? "ON" : "off");
}

static void rx_bank_dispatch(uint32_t arg, bool have_arg)
{
    if (!have_arg) { rx_bank_budget(); return; }

    switch (arg) {
    case 0:
    case 1:
        hal_pico_set_bank(arg != 0u);
        printf("    five-bin bank %s%s\n", arg ? "ON" : "off",
               arg ? "" : " - THE RECEIVER IS OFF: no chips, no presence");
        break;
    case 2: rx_bank_capture(); break;
    default: printf("    n [0|1|2]\n"); break;
    }
}

static void rx_help(void)
{
    printf("\n  c 40|200  carrier, kHz\n"
           "  s         stats now\n"
           "  z         zero the stats\n"
           "  v         per-frame lines on / off\n"
           "  m         probe-bin level mean / max, raw RMS\n"
           "  r         raw burst across the next frame, dumped\n"
           "  t [N]     stream every Nth chip magnitude; t 0 stops\n"
           "  x [0|1]   self loop: our own carrier into our own receiver\n"
           "  f         clock tree, measured\n"
           "  y [0..4|9] two tones: check, drive A / B, 2 align,\n"
           "              3 alternating chips, 4 measure the settle\n"
           "              after one beacon, 9 stop\n"
           "  k [bin]   move the PROBE Goertzel to a bin (7..11); the link\n"
           "              is on bins 9 and 10 and does not move\n"
           "  b [1]     walk bins 7,8,9,10,11; b 1 walks every bin\n"
           "  n [0|1|2] five-bin bank: budget, 0 off, 1 on, 2 one capture\n"
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
    case 'y': fsk_dispatch(arg, have_arg); break;
    case 'k':
        if (!have_arg) printf("    receive bin %u\n", (unsigned)hal_pico_rx_bin());
        else if (!hal_pico_set_rx_bin((uint16_t)arg))
            printf("    bin %lu is DC or above Nyquist for N=%d\n",
                   (unsigned long)arg, HANDOFF_GZ_N);
        else {
            frame_rx_init(&s_rx);
            rx_zero();
            printf("    receive bin %lu (%lu kHz), frame receiver reset\n",
                   (unsigned long)arg,
                   (unsigned long)(arg * (uint32_t)HANDOFF_WINDOW_RATE_HZ / 1000u));
        }
        break;
    case 'b': if (have_arg && arg) rx_bank_full(); else rx_bank(); break;
    case 'p': rx_presence(have_arg ? arg : 0u); break;
    case 'n': rx_bank_dispatch(arg, have_arg); break;
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
