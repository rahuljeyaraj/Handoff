/*
 * Handoff — Half-duplex turnaround on one board. M13.
 *
 * Hardware: NONE ADDED. One board alternates transmitting and receiving on
 * its own pad forever: GP2 driving, then released, the receiver deaf for the
 * settling window of design §9.7, then trusting data. The bench it was
 * written on (15 Sep 2026, the AFE not yet built) is the passive divider,
 * GP2 -> 10 kOhm -> GP26, 540 Ohm -> GND, so "settling" here is the pad and
 * the ADC pipeline and nothing else; the amplifier's figure is the hardware
 * day's, with this app unchanged. A second board (linktest, TX) feeding the
 * same node through its own 10 kOhm gives the receiver real frames to
 * decode after each window -- the positive control, without which "no false
 * detection" is also what a dead receiver reports.
 *
 * Exit criteria (development plan M13):
 *
 *   - 10 000 turnarounds with no false carrier detection during the
 *     recovery window
 *   - measured settling time against the 1 ms budget
 *
 * WHAT IS COUNTED, per turn. Every chip core 1 produces carries the number
 * of its last ADC sample, so a chip is placed on the sample clock rather
 * than on the moment it arrived (the ring is up to a DMA block late, 4 ms,
 * four budgets). With t_rel the instant the generator released the pad
 * (the DMA start plus the airtime, or the shout's abort) and W the window:
 *
 *   own        sampled before or across t_rel: our own transmission, dropped
 *              unseen, as link_sm's drain_discard() does
 *   window     sampled wholly inside (t_rel, t_rel + W]: dropped by the link,
 *              but here fed to a COPY of the carrier detector taken before
 *              the turn -- if that copy declares a carrier, the window is
 *              doing its job and the count says how often it had to
 *   after      sampled after the window: fed to the link's detector (which
 *              was not touched during the turn, exactly as in link_sm) and
 *              to the framer. A detector that declares a carrier in the
 *              first 5 ms after the window, on a channel with nobody on it,
 *              has mistaken the tail of our own transmission for the far end
 *              -- design §9.7's failure, and the number that must be zero.
 *              A frame decoded here is the far end heard: the control.
 *
 * SETTLING. A raw capture is armed for the release before it happens (the
 * time is known in advance), from 250 us before t_rel; the tail of the
 * capture is the resting level and its noise, and the settling time is the
 * first point after which the signal stays inside that band for 0.5 ms,
 * relative to t_rel. It can be negative: on the bare divider the pad
 * settles in nanoseconds, so what this reads is the ADC pipeline's own
 * offset, and that is the zero the AFE figure is later measured against.
 * Spikes after that point are the ambient (they are there with no
 * transmission at all) and are counted, not folded into the settling.
 * Kept per turn: min, max, mean, and the count over budget; the worst
 * capture is held for `r`.
 *
 * HOW IT RUNS. Boots paused with a pad check through the divider (the
 * released-space claim of §9.8, which txgen can only test on a bare pad),
 * then `g` runs turns until `n` of them or `g` again:
 *
 *   k f|s     each turn sends a frame (default) or a 10 ms shout (§7.6)
 *   w [us]    settling window, default HANDOFF_TURNAROUND_US
 *   l [ms]    listen after the window, default 100
 *   n [N]     turns per run, default 10000; 0 runs forever
 *   b [ms]    raw capture length after release, default 5
 *   g         go / pause
 *   s         stats now            z   zero the stats
 *   v         per-turn lines       p   pad check through the divider again
 *   r         dump the worst settling capture (r <n> ... r end)
 *   c 40|200  carrier, kHz         h   this list
 *
 * Two phases on the bench, from the other board's console: `p` (paused,
 * pad high-Z) for the false-detection soak, where any detection after the
 * window is false; running, for the frames-after-window count, where
 * detections are the far end and only the frame count is read.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "adc_ring.h"
#include "carrier.h"
#include "config.h"
#include "frame.h"
#include "hal_pico.h"
#include "pio_carrier.h"
#include "tlm.h"

#define TURNS_DEFAULT     10000u
#define LISTEN_DEFAULT_US 100000u
#define SHOUT_US          10000u        /* firmware-architecture §7.6         */
#define CAP_PRE_US        250u          /* capture starts this long before t_rel */
#define CAP_DEFAULT_MS    5u
#define CAP_MAX_MS        40u           /* 40x the budget is a fail either way */
#define EARLY_US          5000u         /* "just after the window" for a detection */
#define PROGRESS_EVERY    100u
#define HEARTBEAT_US      10000000u
#define PIN_TX            2

static const hal_iface_t *s_hal;

/* knobs */
static bool     s_shout;
static uint32_t s_window_us = HANDOFF_TURNAROUND_US;
static uint32_t s_listen_us = LISTEN_DEFAULT_US;
static uint32_t s_turns     = TURNS_DEFAULT;
static uint32_t s_cap_ms    = CAP_DEFAULT_MS;
static bool     s_running;
static bool     s_verbose;

/* the receiver, as the link keeps it */
static frame_rx_t s_rx;
static carrier_t  s_det;

typedef struct {
    uint64_t since;
    uint32_t turns;
    uint32_t chips_own, chips_straddle, chips_window, chips_after;
    uint32_t window_max, window_sum;           /* chip energy in the window */
    uint32_t window_detects;                   /* turns the copy fired      */
    uint32_t early_detects;                    /* det present < 5 ms after  */
    uint32_t listen_detects;                   /* det present any time after*/
    uint32_t frames_good, frames_bad;          /* after a window            */
    uint32_t frames_idle;                      /* paused or between turns   */
    uint32_t caps;                             /* settling captures analysed*/
    int32_t  settle_min, settle_max;
    int64_t  settle_sum;
    uint32_t settle_over;                      /* settle > window           */
    uint32_t late_spikes;                      /* samples out of band after settling */
    int32_t  before_mean;                      /* level before release, LSB */
} stats_t;

static stats_t s_st;

/* the worst capture, for `r` */
static int16_t  s_worst[(HANDOFF_ADC_FS_HZ / 1000) * CAP_MAX_MS + 200];
static size_t   s_worst_n;
static int32_t  s_worst_rel;      /* index of t_rel inside it */
static int32_t  s_worst_settle;
static uint32_t s_worst_turn;

static uint8_t s_chips[FRAME_TOTAL_CHIPS];
static uint16_t s_seq;

/* ---------------------------------------------------------------------- */

static void print_tenths(uint32_t tenths)
{
    printf("%lu.%lu", (unsigned long)(tenths / 10u), (unsigned long)(tenths % 10u));
}

static uint32_t elapsed_s(uint64_t since)
{
    return (uint32_t)((hal_now_us(s_hal) - since) / 1000000u);
}

static void zero_stats(void)
{
    memset(&s_st, 0, sizeof s_st);
    s_st.since = hal_now_us(s_hal);
    s_st.settle_min = INT32_MAX;
    s_st.settle_max = INT32_MIN;
    s_worst_n = 0;
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

/* Same payload as linktest, so a frame we send looks like one we receive. */
static size_t encode(uint16_t seq)
{
    frame_hdr_t h;
    uint8_t payload[HANDOFF_FRAG_PAYLOAD];
    size_t i;

    payload[0] = (uint8_t)(seq & 0xFFu);
    payload[1] = (uint8_t)(seq >> 8);
    for (i = 2; i < HANDOFF_FRAG_PAYLOAD; i++)
        payload[i] = (uint8_t)((i * 31u + (seq & 0xFFu) * 17u) & 0xFFu);
    h.frag_index = (uint8_t)(seq & 0x0Fu);
    h.frag_count = 16;
    h.record_id  = (uint8_t)((seq >> 4) & 0x3Fu);
    h.flags      = 0;
    return frame_encode(&h, payload, sizeof payload, s_chips, sizeof s_chips);
}

/* ======================================================================
 * The turn
 * ====================================================================== */

typedef struct {
    uint64_t  t_rel;          /* pad released                                 */
    uint64_t  t_open;         /* t_rel + window: ears open                    */
    carrier_t det_copy;       /* detector as it was before the turn           */
    bool      window_fired, early_fired, listen_fired, det_was_present;
    bool      framer_reset;
    uint32_t  chips_window, window_max, window_sum;
    uint32_t  good, bad;
} turn_t;

static turn_t s_turn;

/*
 * Pop chips with their sample numbers and sort them into the turn. Called
 * throughout the turn, including while transmitting, so the ring never
 * backs up and every chip is placed by when it was sampled.
 */
static uint64_t s_idx_hi;
static uint32_t s_idx_last;

static void pump(bool in_turn)
{
    uint16_t chips[64];
    uint32_t idx[64];
    size_t n, i;

    while ((n = hal_pico_rx_chips_at(chips, idx, count_of(chips))) > 0) {
        for (i = 0; i < n; i++) {
            uint64_t t_end, t_start;
            frame_rx_result_t r;

            /* The ring carries the low 32 bits of the sample number, which
             * wrap every 2.4 hours; chips only ever arrive in order. */
            if (idx[i] < s_idx_last) s_idx_hi += 1ull << 32;
            s_idx_last = idx[i];
            t_end   = hal_pico_sample_us(s_idx_hi | idx[i]);
            t_start = t_end - (uint64_t)HANDOFF_CHIP_US;

            if (!in_turn) {
                /* Between turns and while paused: the plain receiver. */
                carrier_push(&s_det, chips[i]);
                r = frame_rx_push(&s_rx, chips[i]);
                if (r == FRAME_RX_GOOD || r == FRAME_RX_BAD_CRC) s_st.frames_idle++;
                continue;
            }

            if (t_end <= s_turn.t_rel) { s_st.chips_own++; continue; }
            if (t_start < s_turn.t_rel) { s_st.chips_straddle++; continue; }

            if (t_end <= s_turn.t_open) {
                s_st.chips_window++;
                s_turn.chips_window++;
                s_turn.window_sum += chips[i];
                if (chips[i] > s_turn.window_max) s_turn.window_max = chips[i];
                carrier_push(&s_turn.det_copy, chips[i]);
                if (carrier_present(&s_turn.det_copy)) s_turn.window_fired = true;
                continue;
            }

            /* After the window: what the link would see. */
            s_st.chips_after++;
            if (!s_turn.framer_reset) {
                frame_rx_reset(&s_rx);          /* enter_rx() does this */
                s_turn.framer_reset = true;
            }
            carrier_push(&s_det, chips[i]);
            if (carrier_present(&s_det) && !s_turn.det_was_present) {
                if (t_end <= s_turn.t_open + EARLY_US) s_turn.early_fired = true;
                s_turn.listen_fired = true;
            }
            s_turn.det_was_present = carrier_present(&s_det);

            r = frame_rx_push(&s_rx, chips[i]);
            if (r == FRAME_RX_GOOD) s_turn.good++;
            else if (r == FRAME_RX_BAD_CRC) s_turn.bad++;
        }
    }
}

/*
 * Settling from the capture. The resting level and its noise are the last
 * tenth of the capture. A sample is OUT if either:
 *
 *   - it is more than max(6 sigma, 8 LSB) from rest: the transmission
 *     itself, or an amplifier still on a rail. Well above the noise's own
 *     outliers, which on a rest level pinned at the rail (the bare divider
 *     reads code ~2) are one-sided and reach +6 LSB.
 *   - the 25-sample (50 us) mean centred on it is more than max(sigma,
 *     2 LSB) from rest: a slow tail, the AFE's DC recovering, which no
 *     single sample shows.
 *
 * Settled is the first sample from which nothing is OUT for SETTLE_HOLD
 * samples (0.5 ms): the usual definition, enter the band and stay there.
 * Samples OUT after that are ambient spikes -- the first 10 000-shout soak
 * showed six in 10 000 turns, 1-4 ms after a release, with zero carrier
 * energy in the same window -- and are counted separately. Negative means
 * the pad was quiet before the generator's own account of the release,
 * which on the bare divider is the ADC pipeline's offset.
 */
#define SETTLE_WIN  25
#define SETTLE_HOLD 250

static uint8_t s_out[(HANDOFF_ADC_FS_HZ / 1000) * CAP_MAX_MS + 200];

static bool analyse_capture(int32_t *settle_us, int32_t *before, size_t *rel_idx,
                            uint32_t *spikes)
{
    const int16_t *s;
    size_t n = 0, tail, i, i_rel, run, first = 0;
    uint64_t t0 = tlm_usb_raw_start_us();
    int64_t sum = 0, win;
    uint64_t sq = 0;
    int32_t base, thr_big, thr_small;
    double sigma;
    bool settled = false;

    s = tlm_usb_raw_samples(&n);
    if (!s || n < 100 || n > count_of(s_out)) return false;

    tail = n / 10;
    for (i = n - tail; i < n; i++) sum += s[i];
    base = (int32_t)(sum / (int64_t)tail);
    for (i = n - tail; i < n; i++) {
        int32_t d = s[i] - base;
        sq += (uint64_t)((int64_t)d * d);
    }
    sigma     = sqrt((double)sq / (double)tail);
    thr_big   = (int32_t)(6.0 * sigma + 0.5); if (thr_big < 8) thr_big = 8;
    thr_small = (int32_t)(sigma + 0.5);       if (thr_small < 2) thr_small = 2;

    if (s_turn.t_rel < t0) return false;
    i_rel = (size_t)(((s_turn.t_rel - t0) * (uint64_t)HANDOFF_ADC_FS_HZ) / 1000000u);
    if (i_rel >= n - tail) return false;

    /* Flag every sample: single-sample test, then the centred moving mean. */
    for (i = 0; i < n; i++) {
        int32_t d = s[i] - base;
        s_out[i] = (d > thr_big || d < -thr_big) ? 1u : 0u;
    }
    win = 0;
    for (i = 0; i < SETTLE_WIN; i++) win += s[i];
    for (i = SETTLE_WIN; i < n; i++) {
        int64_t d = win - (int64_t)base * SETTLE_WIN;      /* SETTLE_WIN x mean dev */
        if (d > (int64_t)thr_small * SETTLE_WIN || d < -(int64_t)thr_small * SETTLE_WIN)
            s_out[i - SETTLE_WIN / 2 - 1] = 1u;
        win += s[i];
        win -= s[i - SETTLE_WIN];
    }

    /* First sample from which nothing is OUT for SETTLE_HOLD samples. */
    run = 0;
    for (i = 0; i < n; i++) {
        if (s_out[i]) { run = 0; continue; }
        if (++run == SETTLE_HOLD) { first = i + 1 - SETTLE_HOLD; settled = true; break; }
    }
    *spikes = 0;
    if (settled)
        for (i = first + SETTLE_HOLD; i < n - tail; i++) *spikes += s_out[i];
    else
        first = n;

    *settle_us = ((int32_t)first - (int32_t)i_rel) * (int32_t)(1000000u / HANDOFF_ADC_FS_HZ);
    sum = 0;
    for (i = 0; i < i_rel; i++) sum += s[i];
    *before  = i_rel ? (int32_t)(sum / (int64_t)i_rel) - base : 0;
    *rel_idx = i_rel;
    return true;
}

static void keep_worst(int32_t settle, size_t rel_idx)
{
    const int16_t *s;
    size_t n = 0;

    if (s_worst_n && settle <= s_worst_settle) return;
    s = tlm_usb_raw_samples(&n);
    if (!s) return;
    if (n > count_of(s_worst)) n = count_of(s_worst);
    memcpy(s_worst, s, n * sizeof s_worst[0]);
    s_worst_n      = n;
    s_worst_rel    = (int32_t)rel_idx;
    s_worst_settle = settle;
    s_worst_turn   = s_st.turns;
}

/* One turnaround: transmit, release, window, listen. Blocks for the turn. */
static void run_turn(void)
{
    uint64_t t_start, t_stop;
    size_t   cap_n = (size_t)(HANDOFF_ADC_FS_HZ / 1000u) * s_cap_ms
                   + (size_t)(HANDOFF_ADC_FS_HZ / 1000000u) * CAP_PRE_US;

    memset(&s_turn, 0, sizeof s_turn);
    s_turn.det_copy = s_det;

    hal_tx_drive(s_hal, true);

    if (s_shout) {
        t_start = hal_now_us(s_hal);
        t_stop  = t_start + SHOUT_US;
        pio_carrier_mark_continuous(true);
        s_turn.t_rel  = t_stop;                          /* provisional */
        s_turn.t_open = t_stop + s_window_us;
        tlm_usb_raw_trigger_at(t_stop - CAP_PRE_US, cap_n);
        while (hal_now_us(s_hal) < t_stop) pump(true);
        pio_carrier_mark_continuous(false);              /* released now */
        s_turn.t_rel  = hal_now_us(s_hal);
        s_turn.t_open = s_turn.t_rel + s_window_us;
    } else {
        size_t n = encode(s_seq++);
        if (hal_tx_chips(s_hal, s_chips, n) != n) {
            printf("    tx refused %u chips\n", (unsigned)n);
            hal_tx_drive(s_hal, false);
            return;
        }
        s_turn.t_rel  = hal_pico_tx_pad_idle_us();
        s_turn.t_open = s_turn.t_rel + s_window_us;
        tlm_usb_raw_trigger_at(s_turn.t_rel - CAP_PRE_US, cap_n);
        while (hal_tx_busy(s_hal)) pump(true);
    }

    hal_tx_drive(s_hal, false);
    report_stalls();
    s_st.turns++;

    /* Window and listen: chips are sorted by sample time, so this loop only
     * has to outlast the ring's latency past the end of the listen. */
    {
        uint64_t until = s_turn.t_open + s_listen_us + 10000u;
        while (hal_now_us(s_hal) < until) pump(true);
    }

    /* The capture finished long ago; score it. */
    {
        int32_t settle, before;
        size_t rel_idx;
        uint32_t spin = 2000000u, spikes;

        while (tlm_usb_raw_busy() && spin--) pump(true);
        if (!tlm_usb_raw_busy() && analyse_capture(&settle, &before, &rel_idx, &spikes)) {
            s_st.caps++;
            if (settle < s_st.settle_min) s_st.settle_min = settle;
            if (settle > s_st.settle_max) s_st.settle_max = settle;
            s_st.settle_sum += settle;
            if (settle > (int32_t)s_window_us) s_st.settle_over++;
            s_st.late_spikes += spikes;
            s_st.before_mean = before;
            keep_worst(settle, rel_idx);
            if (s_verbose)
                printf("    turn %5lu: settle %ld us, before %+ld LSB, spikes %lu, window %lu chips "
                       "max %lu%s, after %s, frames %lu/%lu\n",
                       (unsigned long)s_st.turns, (long)settle, (long)before, (unsigned long)spikes,
                       (unsigned long)s_turn.chips_window, (unsigned long)s_turn.window_max,
                       s_turn.window_fired ? " FIRED" : "",
                       s_turn.early_fired ? "DETECT<5ms" : (s_turn.listen_fired ? "detect" : "quiet"),
                       (unsigned long)s_turn.good, (unsigned long)s_turn.bad);
        } else if (s_verbose) {
            printf("    turn %5lu: no capture\n", (unsigned long)s_st.turns);
        }
    }

    if (s_turn.window_fired) s_st.window_detects++;
    if (s_turn.early_fired)  s_st.early_detects++;
    if (s_turn.listen_fired) s_st.listen_detects++;
    s_st.window_sum += s_turn.window_sum;
    if (s_turn.window_max > s_st.window_max) s_st.window_max = s_turn.window_max;
    s_st.frames_good += s_turn.good;
    s_st.frames_bad  += s_turn.bad;
}

static void print_stats(void)
{
    printf("  %6lu s  turns %lu  window: %lu chips, max %lu LSB, mean ",
           (unsigned long)elapsed_s(s_st.since), (unsigned long)s_st.turns,
           (unsigned long)s_st.chips_window, (unsigned long)s_st.window_max);
    print_tenths(s_st.chips_window ? (s_st.window_sum * 10u) / s_st.chips_window : 0);
    printf(" LSB, fired %lu\n", (unsigned long)s_st.window_detects);
    printf("           after: detect <5 ms %lu, detect in listen %lu, frames good %lu bad %lu "
           "(idle %lu)\n",
           (unsigned long)s_st.early_detects, (unsigned long)s_st.listen_detects,
           (unsigned long)s_st.frames_good, (unsigned long)s_st.frames_bad,
           (unsigned long)s_st.frames_idle);
    if (s_st.caps)
        printf("           settle: min %ld  mean %ld  max %ld us (turn %lu), over %lu us budget %lu; "
               "ambient spikes after settling %lu; level before release %+ld LSB\n",
               (long)s_st.settle_min, (long)(s_st.settle_sum / (int64_t)s_st.caps),
               (long)s_st.settle_max, (unsigned long)s_worst_turn,
               (unsigned long)s_window_us, (unsigned long)s_st.settle_over,
               (unsigned long)s_st.late_spikes, (long)s_st.before_mean);
    printf("           chips own %lu straddle %lu after %lu; false syncs %lu, overruns %lu, "
           "stalls %lu, core-1 load %u%%\n",
           (unsigned long)s_st.chips_own, (unsigned long)s_st.chips_straddle,
           (unsigned long)s_st.chips_after, (unsigned long)s_rx.false_syncs,
           (unsigned long)hal_pico_overruns(), (unsigned long)hal_pico_tx_stalls(0),
           hal_pico_core1_load());
}

/* ======================================================================
 * Pad check through the divider
 * ====================================================================== */

/* Mean ADC code over a short capture starting at t0 (0 = now). */
static int32_t node_mean(uint64_t t0, uint32_t ms)
{
    const int16_t *s;
    size_t n = 0, i;
    int64_t sum = 0;
    uint32_t spin = 4000000u;

    tlm_usb_raw_trigger_at(t0, (size_t)(HANDOFF_ADC_FS_HZ / 1000u) * ms);
    while (tlm_usb_raw_busy() && spin--) pump(false);
    s = tlm_usb_raw_samples(&n);
    if (!s || !n) return -1;
    for (i = 0; i < n; i++) sum += s[i];
    return (int32_t)(sum / (int64_t)n) + 2048;
}

/*
 * The §9.8 claim, tested where txgen cannot: with the divider on GP2 an
 * internal pull cannot move a bare pad far enough for gpio_get(), but it
 * moves the NODE by a readable amount. Released with the pull-up on, the
 * pad sits at ~0.57 V (the pull against 10.5 kOhm) and the node reads
 * ~35 LSB; driven low it reads 0; driven high, ~210. The space run is
 * measured mid-stream, 2 ms into 64 chips of it, so it is the generator's
 * per-slot release being read and not the tail every send ends in.
 */
static void pad_check(void)
{
    int32_t low, high, space, released, mark;
    uint8_t spaces[64];

    printf("\n  --- pad through the divider (node = GP26, LSB) ---\n");
    memset(spaces, 0, sizeof spaces);

    hal_tx_drive(s_hal, true);
    pio_carrier_hold(0);
    low = node_mean(0, 4);
    pio_carrier_hold(1);
    high = node_mean(0, 4);
    pio_carrier_hold(-1);

    gpio_pull_up(PIN_TX);
    pio_carrier_send(spaces, sizeof spaces);
    space = node_mean(pio_carrier_started_us() + 2000u, 4);
    while (pio_carrier_busy()) pump(false);
    hal_tx_drive(s_hal, false);
    gpio_pull_up(PIN_TX);
    released = node_mean(0, 4);
    gpio_disable_pulls(PIN_TX);

    hal_tx_drive(s_hal, true);
    pio_carrier_mark_continuous(true);
    mark = node_mean(0, 4);
    pio_carrier_mark_continuous(false);
    hal_tx_drive(s_hal, false);

    printf("    driven low %ld   driven high %ld   carrier %ld (mean of the square)\n",
           (long)low, (long)high, (long)mark);
    printf("    space chips + pull-up %ld   drive(false) + pull-up %ld\n",
           (long)space, (long)released);
    printf("    %-52s %s\n", "space chips release the pad (pull-up moves the node)",
           (space > low + 10 && space < high / 2) ? "PASS" : "FAIL");
    printf("    %-52s %s\n", "marks drive it both ways (carrier mean ~ high/2)",
           (mark > high / 3 && mark < 2 * high / 3) ? "PASS" : "FAIL");
    printf("    %-52s %s\n", "drive(false) releases it the same way",
           (released > low + 10 && released < high / 2) ? "PASS" : "FAIL");

    /* The detector and framer saw our own carrier during this; start clean. */
    frame_rx_init(&s_rx);
    carrier_init(&s_det);
}

/* ======================================================================
 * Console
 * ====================================================================== */

static void cmd_dump_worst(void)
{
    size_t i;

    if (!s_worst_n) { printf("    no capture yet\n"); return; }
    printf("    worst settle %ld us at turn %lu; release at sample %ld of %u, 2 us each\n",
           (long)s_worst_settle, (unsigned long)s_worst_turn,
           (long)s_worst_rel, (unsigned)s_worst_n);
    printf("r %u\n", (unsigned)s_worst_n);
    for (i = 0; i < s_worst_n; i++) printf("%d\n", (int)s_worst[i]);
    printf("r end\n");
}

static void cmd_carrier(uint32_t khz)
{
    uint32_t hz = khz * 1000u;

    if (!hal_pico_set_carrier(hz)) {
        printf("    %lu Hz is not a PIO divider on a Goertzel bin centre\n",
               (unsigned long)hz);
        return;
    }
    frame_rx_init(&s_rx);
    carrier_init(&s_det);
    printf("    carrier %lu Hz, bin %lu, %lu stream bits per chip\n",
           (unsigned long)hz,
           (unsigned long)(hz / (uint32_t)HANDOFF_WINDOW_RATE_HZ),
           (unsigned long)pio_carrier_bits_per_chip());
}

static void help(void)
{
    printf("\n  k f|s     each turn sends a frame or a %lu ms shout (now %s)\n"
           "  w [us]    settling window (now %lu)\n"
           "  l [ms]    listen after the window (now %lu)\n"
           "  n [N]     turns per run, 0 forever (now %lu)\n"
           "  b [ms]    raw capture after release (now %lu)\n"
           "  g         go / pause          s  stats      z  zero\n"
           "  v         per-turn lines      p  pad check  r  dump worst capture\n"
           "  c 40|200  carrier, kHz        h  this\n",
           (unsigned long)(SHOUT_US / 1000u), s_shout ? "shout" : "frame",
           (unsigned long)s_window_us, (unsigned long)(s_listen_us / 1000u),
           (unsigned long)s_turns, (unsigned long)s_cap_ms);
}

static char parse(const char *line, uint32_t *arg, bool *have_arg, char *word)
{
    const char *p;

    while (*line == ' ') line++;
    *have_arg = false;
    *arg = 0;
    *word = 0;
    if (!*line) return 0;
    for (p = line + 1; *p == ' '; p++) {}
    if (*p >= '0' && *p <= '9') { *arg = (uint32_t)strtoul(p, 0, 10); *have_arg = true; }
    else if (*p) *word = *p;
    return line[0];
}

static void dispatch(const char *line)
{
    uint32_t arg;
    bool have_arg;
    char word;

    switch (parse(line, &arg, &have_arg, &word)) {
    case 0: return;
    case 'k':
        if (word == 's') s_shout = true;
        else if (word == 'f') s_shout = false;
        printf("    each turn: %s\n", s_shout ? "10 ms shout" : "one frame");
        break;
    case 'w':
        if (have_arg) s_window_us = arg;
        printf("    window %lu us\n", (unsigned long)s_window_us);
        break;
    case 'l':
        if (have_arg) s_listen_us = arg * 1000u;
        printf("    listen %lu ms\n", (unsigned long)(s_listen_us / 1000u));
        break;
    case 'n':
        if (have_arg) s_turns = arg;
        printf("    %lu turns per run%s\n", (unsigned long)s_turns, s_turns ? "" : " (forever)");
        break;
    case 'b':
        if (have_arg) s_cap_ms = arg < 1u ? 1u : (arg > CAP_MAX_MS ? CAP_MAX_MS : arg);
        printf("    capture %lu ms after release\n", (unsigned long)s_cap_ms);
        break;
    case 'g':
        s_running = !s_running;
        if (s_running) {
            printf("    running: %s, window %lu us, listen %lu ms, %lu turns\n",
                   s_shout ? "shout" : "frame", (unsigned long)s_window_us,
                   (unsigned long)(s_listen_us / 1000u), (unsigned long)s_turns);
        } else {
            printf("    paused: pad high-Z\n");
        }
        break;
    case 's': print_stats(); break;
    case 'z': zero_stats(); printf("    stats zeroed\n"); break;
    case 'v':
        s_verbose = !s_verbose;
        printf("    per-turn lines %s\n", s_verbose ? "on" : "off");
        break;
    case 'p': pad_check(); break;
    case 'r': cmd_dump_worst(); break;
    case 'c': cmd_carrier(have_arg ? arg : HANDOFF_CARRIER_HZ / 1000u); break;
    case 'h': case '?': help(); break;
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
    uint32_t noise, run_turns = 0;

    stdio_init_all();
    sleep_ms(2000);          /* let the USB console attach before the report */

    printf("\nhandoff turnaround (M13: half-duplex turnaround on one board)\n");
    printf("  carrier %d Hz, ADC %d Hz, Goertzel N=%d bin %d, %d chips/s, chip %d us\n",
           HANDOFF_CARRIER_HZ, HANDOFF_ADC_FS_HZ, HANDOFF_GZ_N, HANDOFF_GZ_BIN,
           HANDOFF_CHIP_RATE_HZ, HANDOFF_CHIP_US);
    printf("  frame %d chips, %lu us airtime; window %d us (HANDOFF_TURNAROUND_US)\n",
           FRAME_TOTAL_CHIPS, (unsigned long)FRAME_AIRTIME_US, HANDOFF_TURNAROUND_US);

    s_hal = hal_pico_init();
    frame_rx_init(&s_rx);
    carrier_init(&s_det);
    zero_stats();

    noise = hal_pico_noise_floor(&noise_mean);
    printf("  M4 noise floor "); print_tenths(noise);
    printf(" LSB RMS (mean code %ld), core 1 up\n", (long)noise_mean);

    pad_check();
    printf("\n  paused. `g` to run %lu turns, `h` for help.\n", (unsigned long)s_turns);

    next_hb = hal_now_us(s_hal) + HEARTBEAT_US;

    for (;;) {
        int ch = getchar_timeout_us(0);

        if (s_running) {
            run_turn();
            run_turns++;
            if (s_st.turns % PROGRESS_EVERY == 0u) print_stats();
            if (s_turns && run_turns >= s_turns) {
                s_running = false;
                run_turns = 0;
                printf("\n  --- run complete: %lu turns ---\n", (unsigned long)s_st.turns);
                print_stats();
                printf("    %-52s %s\n", "no carrier detected just after the window",
                       s_st.early_detects == 0 ? "PASS" : "FAIL");
                printf("    %-52s %s\n", "settling inside the budget every turn",
                       s_st.caps && s_st.settle_over == 0 ? "PASS" : "FAIL");
                printf("    (bare divider: settling is the pad and ADC pipeline only)\n");
            }
        } else {
            pump(false);
        }

        if (ch == PICO_ERROR_TIMEOUT) {
            /* A placed heartbeat: if this stops, the hang is on core 0. */
            if (hal_now_us(s_hal) >= next_hb) {
                next_hb += HEARTBEAT_US;
                printf("  hb %lu s turns %lu win-fired %lu early %lu listen %lu frames %lu/%lu "
                       "settle max %ld us %s stalls %lu load %u%%\n",
                       (unsigned long)elapsed_s(s_st.since), (unsigned long)s_st.turns,
                       (unsigned long)s_st.window_detects, (unsigned long)s_st.early_detects,
                       (unsigned long)s_st.listen_detects, (unsigned long)s_st.frames_good,
                       (unsigned long)s_st.frames_bad,
                       (long)(s_st.caps ? s_st.settle_max : 0),
                       s_running ? "running" : "paused",
                       (unsigned long)hal_pico_tx_stalls(0), hal_pico_core1_load());
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
