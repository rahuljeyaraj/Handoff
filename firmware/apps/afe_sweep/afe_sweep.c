/*
 * Handoff — Analogue front end, characterised alone. M7.
 *
 * Hardware: MCP6292, passives, perfboard. THE AFE, NOT YET IN THE LINK.
 *
 * Do not put the amplifier in the loop yet. Measure it first, using the Pico as
 * both signal source and instrument: GP2 is stepped across frequency and
 * amplitude, GP26 reports what came out. An amplifier of unknown gain inside an
 * untested loop cannot be debugged, which is the whole reason M7 exists.
 *
 * Exit criteria (development plan M7), and the command that measures each:
 *
 *   v   VREF measures 1.65 V +-5 %             (GP27 on the VREF node)
 *   s   gain against the design's x121, at 40 kHz and at 200 kHz
 *   c   the -3 dB corner against the predicted ~580 kHz (design §6.4)
 *   n   output noise floor in LSB RMS, against M4's bare-ADC figure
 *   a   the clipping point, found deliberately: input amplitude stepped up
 *       until the output stops growing
 *   x   PREAMP INPUT CAPACITANCE IN SITU, from the rolloff against R2's
 *       1 MOhm. Design §17's first open item; it decides whether 200 kHz is
 *       achievable at all.
 *   -   every adjacent MSOP pin pair continuity-tested before power (§12.1):
 *       a meter job, not a firmware one.
 *
 * THE BENCH. GP2 -> series R -> node -> shunt R -> AGND is the source; the
 * node feeds the thing under test; GP26 reads its output. `k shunt series`
 * tells the app the divider so it can print gain directly: the input
 * fundamental is (2/pi) * 3.3 V * shunt/(shunt+series), in LSB-equivalents,
 * and gain is what GP26 saw divided by that. 3.3 V is the ADC's full scale
 * too, so the figure is ratiometric and the rail's exact voltage cancels.
 *
 *   - 100 kOhm / 220 Ohm is 7.2 mV p-p at the node: x121 makes ~870 mV p-p,
 *     ~690 LSB of fundamental at the ADC, well inside the rails. Bigger
 *     shunts are for finding the clipping point.
 *   - Without the AFE, 10 kOhm / 540 Ohm straight into GP26 is the passive
 *     channel M5 and M6 read at 132-134 LSB; this app must read the same,
 *     flat, from 20 to 240 kHz. That is its own acceptance test, and it
 *     passed on 14 Sep 2026: 131 LSB +-0.3 from 20 kHz to 950 kHz through
 *     the alias, gain 0.98 (the resistors' tolerance and the GPIO's drop
 *     into 10 kOhm), noise 0.90 LSB RMS against 0.9 on-die.
 *   - THE SOURCE MUST BE AC-COUPLED INTO THE AFE. The design has no input
 *     capacitor because the pad is insulated; a DC-coupled bench source
 *     through R2 (1 M) against R3 (10 M to VREF) biases stage 1's input at
 *     ~0.15 V and pins its output on the rail. A 100 nF from the divider
 *     node to R2 fixes it (0.14 Hz corner; 1.1 s to settle a DC step, which
 *     only the amplitude sweep makes -- see `t`).
 *   - Gain and corner (`s`, `c`, `a`) are measured with the source at the
 *     stage-1 input, i.e. injected at the R2/R3 junction; through R2 the
 *     input capacitance rolls the response off first, and R3 shaves 0.8 dB.
 *     `x` is measured with the source at the PAD end of R2, which is the
 *     point of it.
 *
 * FREQUENCY. The generator is pio_carrier_tone(): an integer PIO divider and
 * a bit pattern, so every point is an exact clock ratio and the DFT is
 * evaluated at f/fs = clk_sys / (div * period * 500 000) with no rounding.
 * pio_carrier_measure_hz() counts the pad for a check column, not for the
 * number. Both clocks come from the one crystal.
 *
 * AMPLITUDE. The production Goertzel (N=25) resolves 20 kHz bins; the sweep
 * uses a Hann-windowed float DFT at the exact tone frequency over a 16 384
 * sample raw capture (30 Hz bins), and reports the PEAK of the fundamental
 * in LSB -- the same A that `m` in loopback and linktest reports and the
 * simulator takes. RMS in the same tables is the whole capture about its
 * mean, which is sigma when the tone is off.
 *
 * ALIASING. The ADC samples at 500 ksps; a tone above 250 kHz folds to
 * |f - k*500 kHz| and its amplitude there is the analogue response AT f, so
 * the corner is measured THROUGH THE ALIAS -- the table says where each
 * point folded to, and the corner report says whether it was above Nyquist.
 * Deliberate: the ~580 kHz predicted by §6.4 is only reachable that way, and
 * what actually matters to the link, the response at the ADC pin, is what
 * the fold measures. The one trap is a harmonic of the square wave folding
 * onto the fundamental (a 50 kHz square's 9th harmonic lands at 500-450 =
 * 50 kHz); every row checks harmonics to the 40th and flags one that lands
 * within 4 bins of the fundamental with `!k`. Read a flagged row as +-1/k.
 * The link's own 200 kHz is the worst case: 200/500 = 2/5, the ADC meets the
 * square at the same five phases forever, and harmonics 9, 11, 19, 21 ...
 * all fold onto it -- on the bare divider the reading moves +-2 % with the
 * phase the tone happened to start at, which is where M5/M6's 132-134 LSB
 * spread came from. Behind R9/C2 those harmonics are gone.
 *
 * The console pattern is loopback's: one letter, optional numbers, `h`
 * lists them. A `hb` line every 10 s says core 0 is alive.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include "adc_ring.h"
#include "config.h"
#include "pio_carrier.h"

#define FS_HZ             ((double)HANDOFF_ADC_FS_HZ)
#define LSB_PER_VOLT      (4096.0 / 3.3)
#define TWO_OVER_PI       0.63661977236758134
#define PI                3.14159265358979324

#define VREF_PIN          27          /* ADC1: wire it to the VREF node    */
#define VREF_ADC_INPUT    1

#define CAP_BLOCKS_MAX    16          /* 32 768 samples, 65 ms, 64 KB      */
#define CAP_BLOCKS_DEF    8
#define FREQ_GATE_US      100000u
#define HEARTBEAT_US      10000000u
#define AMP_PERIOD_BITS   32u         /* 16 amplitude steps, sin(pi h/32)  */
#define HARMONIC_MAX      40u
#define COLLIDE_BINS      4.0

/* Bench parameters, all from the console. */
static double   s_ratio      = 540.0 / 10540.0;   /* the M5/M6 passive divider */
static double   s_series_r   = 1.0e6;             /* R2, for the C_in fit      */
static uint32_t s_settle_ms  = 50;
static size_t   s_cap_blocks = CAP_BLOCKS_DEF;

static int16_t  s_cap[CAP_BLOCKS_MAX * ADC_RING_BLOCK];

/* ======================================================================
 * The tone
 * ====================================================================== */

typedef struct {
    bool     on;
    uint32_t div, period, high;
    double   f_set;         /* clk_sys / (div * period), exact               */
    uint64_t num, den;      /* f / fs as a reduced fraction                  */
} tone_t;

static tone_t s_tone;

static uint64_t gcd64(uint64_t a, uint64_t b)
{
    while (b) { uint64_t t = a % b; a = b; b = t; }
    return a;
}

/*
 * period 0 picks the smallest power of two whose divider fits, at 50 %
 * duty: period 2 down to 1144 Hz, then 4, 8 ... so the sweep keeps the
 * finest frequency steps it can at every point.
 */
static bool tone_set(uint32_t hz, uint32_t period, uint32_t high)
{
    uint64_t clk = clock_get_hz(clk_sys);
    uint64_t div, g;

    if (hz == 0) return false;
    if (period == 0) {
        period = 2;
        while (clk / ((uint64_t)period * hz) > 65535u && period < PIO_CARRIER_TONE_BITS)
            period <<= 1;
        high = period / 2u;
    }
    div = (clk + (uint64_t)period * hz / 2u) / ((uint64_t)period * hz);
    if (div < 1u) div = 1u;
    if (div > 65535u) return false;

    if (!pio_carrier_is_driving()) pio_carrier_drive(true);
    if (!pio_carrier_tone((uint32_t)div, period, high)) return false;

    s_tone.on     = true;
    s_tone.div    = (uint32_t)div;
    s_tone.period = period;
    s_tone.high   = high;
    s_tone.f_set  = (double)clk / (double)(div * period);
    s_tone.num    = clk;
    s_tone.den    = div * period * (uint64_t)HANDOFF_ADC_FS_HZ;
    g = gcd64(s_tone.num, s_tone.den);
    s_tone.num /= g;
    s_tone.den /= g;
    return true;
}

static void tone_off(void)
{
    pio_carrier_mark_continuous(false);
    pio_carrier_drive(false);            /* high-Z: the node sees only its shunt */
    s_tone.on = false;
}

/* Where the k-th harmonic lands after sampling, as a fraction of fs in
 * [0, 0.5]. k = 1 is the tone itself. */
static double folded(uint32_t k)
{
    uint64_t r = ((uint64_t)k * s_tone.num) % s_tone.den;
    double x = (double)r / (double)s_tone.den;
    return x > 0.5 ? 1.0 - x : x;
}

/*
 * Lowest harmonic that folds onto the fundamental, or 0. A 50 % square has
 * no even harmonics, so those are only checked for the amplitude steps.
 * 1 means the fundamental itself landed on DC or Nyquist: a tone at a
 * multiple of fs reads 0, and at an odd multiple of fs/2 the two samples
 * per period sit on the square's flats and read pi/2 too high.
 */
static uint32_t harmonic_collision(size_t n)
{
    double f1 = folded(1), guard = COLLIDE_BINS / (double)n;
    uint32_t k, step = (2u * s_tone.high == s_tone.period) ? 2u : 1u;

    if (f1 < guard || 0.5 - f1 < guard) return 1;
    for (k = 1u + step; k <= HARMONIC_MAX; k += step)
        if (fabs(folded(k) - f1) * (double)n < COLLIDE_BINS) return k;
    return 0;
}

/* Fundamental the bench is putting in, peak, in LSB-equivalents: a square of
 * duty d has fundamental (2 V / pi) sin(pi d). */
static double input_lsb(void)
{
    double d = (double)s_tone.high / (double)s_tone.period;
    return s_ratio * 4096.0 * TWO_OVER_PI * sin(PI * d);
}

/* ======================================================================
 * Capture and measure
 * ====================================================================== */

typedef struct {
    size_t   n;
    double   mean;          /* raw code                                 */
    double   rms;           /* about the mean, LSB                      */
    int      min, max;      /* raw codes                                */
    uint32_t overruns;
} stats_t;

/*
 * blocks consecutive, contiguous blocks into s_cap. The ring is restarted
 * first: while the console is idle nobody drains it, both blocks are full,
 * and the consumer's round-robin pointer can be one behind the DMA -- the
 * second block taken would then be OLDER than the first, and a DFT across
 * that seam measures the seam. A restart puts the DMA and the pointer both
 * at block 0, and from there the copy keeps up (20 us against 4 ms), which
 * the overrun counter, zeroed by the restart, confirms per capture.
 */
static bool capture(size_t blocks, stats_t *st)
{
    const int16_t *blk;
    size_t n = 0, got = 0, i;
    int64_t sum = 0;
    uint64_t sq = 0;

    if (blocks > CAP_BLOCKS_MAX) blocks = CAP_BLOCKS_MAX;

    adc_ring_stop();
    adc_ring_start();

    while (got < blocks) {
        uint32_t deadline = 4000000u;
        while ((blk = adc_ring_next_block(&n)) == 0 && deadline--)
            tight_loop_contents();
        if (blk == 0) return false;
        memcpy(&s_cap[got * ADC_RING_BLOCK], blk, n * sizeof blk[0]);
        got++;
    }
    /* Read now: the ring keeps running and nobody drains it after this. */
    st->overruns = adc_ring_overruns();

    /* Integer sums -- software doubles here would take longer than a block. */
    st->n = got * ADC_RING_BLOCK;
    st->min = 4095; st->max = 0;
    for (i = 0; i < st->n; i++) {
        int c = s_cap[i] + 2048;
        sum += c;
        sq  += (uint64_t)((int64_t)c * c);
        if (c < st->min) st->min = c;
        if (c > st->max) st->max = c;
    }
    st->mean = (double)sum / (double)st->n;
    st->rms  = sqrt((double)sq / (double)st->n - st->mean * st->mean);
    return true;
}

/*
 * Peak amplitude of the fundamental at the tone frequency, in LSB. Hann
 * window: the image at -f and any harmonic more than a few bins away are
 * down by the sidelobes rather than the rectangular window's 1/N, which at
 * 1 kHz would otherwise cost several percent. The phase accumulator is the
 * exact fraction num/den, so a 30 ms capture drifts by nothing; a tone
 * above Nyquist folds by itself, since the fraction is taken modulo 1.
 */
static double dft_peak(const int16_t *x, size_t n, double mean)
{
    uint64_t num = s_tone.num % s_tone.den, acc = 0;
    float den = (float)s_tone.den;
    float re = 0, im = 0, wsum = 0;
    float dw = (float)(2.0 * PI / (double)n);
    float m = (float)(mean - 2048.0);
    size_t i;

    for (i = 0; i < n; i++) {
        float w = 0.5f - 0.5f * cosf(dw * (float)i);
        float ang = (float)(2.0 * PI) * ((float)acc / den);
        float v = w * ((float)x[i] - m);
        re += v * cosf(ang);
        im -= v * sinf(ang);
        wsum += w;
        acc += num;
        if (acc >= s_tone.den) acc -= s_tone.den;
    }
    return 2.0 * sqrt((double)re * re + (double)im * im) / (double)wsum;
}

typedef struct {
    stats_t  st;
    uint32_t f_count;       /* pad edges per second, the check column   */
    double   f_fold;        /* where the fundamental landed, Hz         */
    double   a;             /* fundamental peak, LSB                    */
    double   in;            /* what went in, LSB-equivalents            */
    double   gain;
    uint32_t collide;
} meas_t;

static bool measure(meas_t *m)
{
    memset(m, 0, sizeof *m);
    if (!s_tone.on) return false;

    sleep_ms(s_settle_ms);
    m->f_count = pio_carrier_measure_hz(FREQ_GATE_US);
    if (!capture(s_cap_blocks, &m->st)) return false;

    m->f_fold  = folded(1) * FS_HZ;
    m->a       = dft_peak(s_cap, m->st.n, m->st.mean);
    m->in      = input_lsb();
    m->gain    = m->in > 0 ? m->a / m->in : 0;
    m->collide = harmonic_collision(m->st.n);
    return true;
}

static double db(double g) { return g > 0 ? 20.0 * log10(g) : -999.0; }

static void row_header(void)
{
    printf("    f set     f count   folded at    A LSB    in LSB    gain      dB    rms   mean   min   max\n");
}

static void row(const meas_t *m)
{
    printf("  %8.0f  %8lu  %8.0f  %8.1f  %8.2f  %7.3f  %+7.2f  %5.1f  %5.0f  %4d  %4d",
           s_tone.f_set, (unsigned long)m->f_count, m->f_fold, m->a, m->in,
           m->gain, db(m->gain), m->st.rms, m->st.mean, m->st.min, m->st.max);
    if (m->collide == 1) printf("  !DC/NYQ");
    else if (m->collide) printf("  !%lu", (unsigned long)m->collide);
    if (m->st.overruns) printf("  OVERRUN %lu", (unsigned long)m->st.overruns);
    if (m->st.min <= 8 || m->st.max >= 4087) printf("  RAIL");
    printf("\n");
}

static void point(uint32_t hz, uint32_t period, uint32_t high, meas_t *m)
{
    if (!tone_set(hz, period, high)) {
        printf("  %8lu  cannot generate\n", (unsigned long)hz);
        memset(m, 0, sizeof *m);
        return;
    }
    if (!measure(m)) printf("  %8lu  capture failed\n", (unsigned long)hz);
    else row(m);
}

/* ======================================================================
 * The criteria
 * ====================================================================== */

static void cmd_vref(void)
{
    stats_t st;
    uint32_t sum = 0, i;
    double code, volts, out_v;

    adc_ring_stop();
    adc_select_input(VREF_ADC_INPUT);
    sleep_ms(2);
    for (i = 0; i < 256; i++) sum += adc_read();
    adc_select_input(0);
    adc_ring_start();

    code  = sum / 256.0;
    volts = code / LSB_PER_VOLT;
    printf("\n  --- VREF ---\n");
    printf("    GP27 (VREF node):   code %6.1f = %.3f V = %.4f of the rail   %s\n",
           code, volts, code / 4096.0,
           (volts >= 1.65 * 0.95 && volts <= 1.65 * 1.05) ? "within 5 %" : "OUTSIDE 5 %");

    if (capture(2, &st)) {
        out_v = st.mean / LSB_PER_VOLT;
        printf("    GP26 (AFE output):  code %6.1f = %.3f V, DC offset from VREF %+.0f mV "
               "(x11 stage-2 Vos)\n", st.mean, out_v, (out_v - volts) * 1000.0);
    }
    printf("    the ratio is the honest figure: R10/R11 divide the same 3V3 the ADC "
           "measures against\n");
}

static void cmd_noise(void)
{
    stats_t st;
    int32_t die_mean;
    uint32_t die;
    size_t b;
    double bmin = 1e9, bmax = 0;
    bool was_on = s_tone.on;
    uint32_t hz = (uint32_t)(s_tone.f_set + 0.5);

    tone_off();
    sleep_ms(s_settle_ms);

    printf("\n  --- noise, GP2 high-Z ---\n");
    if (!capture(s_cap_blocks, &st)) { printf("    capture failed\n"); return; }

    /* Per-block spread: mains or USB hum shows as blocks that disagree. */
    for (b = 0; b < st.n / ADC_RING_BLOCK; b++) {
        int64_t sum = 0, sq = 0;
        double mean, rms;
        size_t i;
        for (i = 0; i < ADC_RING_BLOCK; i++) {
            int c = s_cap[b * ADC_RING_BLOCK + i];
            sum += c;
            sq  += (int64_t)c * c;
        }
        mean = (double)sum / ADC_RING_BLOCK;
        rms  = sqrt((double)sq / ADC_RING_BLOCK - mean * mean);
        if (rms < bmin) bmin = rms;
        if (rms > bmax) bmax = rms;
    }

    printf("    output:  %.2f LSB RMS about code %.1f (%.3f V), min %d max %d, "
           "%lu samples; per block %.2f-%.2f\n",
           st.rms, st.mean, st.mean / LSB_PER_VOLT, st.min, st.max,
           (unsigned long)st.n, bmin, bmax);

    die = adc_ring_noise_floor(&die_mean);
    printf("    on-die:  %lu.%lu LSB RMS about code %ld (M4 method, same converter, now)\n",
           (unsigned long)(die / 10u), (unsigned long)(die % 10u), (long)die_mean);
    printf("    above the on-die floor by sqrt(out^2 - die^2) = %.2f LSB RMS\n",
           st.rms > die / 10.0 ? sqrt(st.rms * st.rms - (die / 10.0) * (die / 10.0)) : 0.0);

    if (was_on && hz) tone_set(hz, 0, 0);
    else printf("    tone left off\n");
}

static void cmd_sweep(uint32_t f0, uint32_t f1, uint32_t step)
{
    meas_t m;
    double g40 = -1, g200 = -1;
    uint32_t f;

    if (step == 0 || f1 < f0) { printf("    ? s f0 f1 step, Hz\n"); return; }

    printf("\n  --- sweep %lu..%lu Hz step %lu, divider %.5f (in %.1f LSB at 50 %%) ---\n",
           (unsigned long)f0, (unsigned long)f1, (unsigned long)step, s_ratio,
           s_ratio * 4096.0 * TWO_OVER_PI);
    row_header();
    for (f = f0; f <= f1; f += step) {
        point(f, 0, 0, &m);
        if (f == 40000u)  g40  = m.gain;
        if (f == 200000u) g200 = m.gain;
    }
    if (g40 > 0)
        printf("    gain at  40 kHz: %.2f = %+.2f dB, %+.2f dB against x121\n",
               g40, db(g40), db(g40 / 121.0));
    if (g200 > 0)
        printf("    gain at 200 kHz: %.2f = %+.2f dB, %+.2f dB against x121\n",
               g200, db(g200), db(g200 / 121.0));
}

/*
 * A point at a divider that no harmonic folds onto: the requested one, else
 * one to three either side of it. False if none is clean, and the caller
 * skips it -- a collided point used as a corner bracket fakes a corner.
 */
static bool point_clean(uint32_t div, meas_t *m)
{
    static const int nudge[] = {0, 1, -1, 2, -2, 3, -3};
    uint64_t clk = clock_get_hz(clk_sys);
    size_t i;

    for (i = 0; i < count_of(nudge); i++) {
        uint32_t d = (uint32_t)((int)div + nudge[i]);
        if (d < 1u || d > 65535u) continue;
        point((uint32_t)(clk / (2u * (uint64_t)d)), 0, 0, m);
        if (m->a > 0 && !m->collide) return true;
        if (m->a > 0)
            printf("    (%s folds onto it; divider %lu %s)\n",
                   m->collide == 1 ? "DC or Nyquist" : "a harmonic",
                   (unsigned long)d, i + 1 < count_of(nudge) ? "nudged" : "skipped");
    }
    return false;
}

/*
 * Walk up from a 40 kHz reference until the response is 3 dB down, then
 * bisect the divider between the last point above and the first below.
 * Period is 2 for everything above 1144 Hz, so the divider IS the frequency
 * and every candidate is an exact clock ratio.
 */
static void cmd_corner(void)
{
    static const uint32_t walk[] = {
        100000, 140000, 180000, 220000, 260000, 300000, 350000, 400000, 450000,
        550000, 600000, 650000, 700000, 800000, 900000, 1000000, 1200000, 1500000
    };
    meas_t ref, m, lo_m, hi_m;
    uint64_t clk = clock_get_hz(clk_sys);
    uint32_t lo_div, hi_div = 0, i;
    double thresh;

    memset(&hi_m, 0, sizeof hi_m);
    printf("\n  --- -3 dB corner, reference 40 kHz ---\n");
    row_header();
    if (!point_clean((uint32_t)(clk / 80000u), &ref)) { printf("    no reference\n"); return; }
    thresh = ref.a / sqrt(2.0);
    lo_m = ref; lo_div = s_tone.div;

    for (i = 0; i < count_of(walk); i++) {
        if (!point_clean((uint32_t)(clk / (2u * (uint64_t)walk[i])), &m)) continue;
        if (m.a < thresh) { hi_m = m; hi_div = s_tone.div; break; }
        lo_m = m; lo_div = s_tone.div;
    }
    if (hi_div == 0) {
        printf("    within 3 dB of the reference all the way to %.0f Hz: the corner is above "
               "what the generator was asked for\n", (double)clk / (2.0 * lo_div));
        return;
    }

    while (lo_div > hi_div + 1u) {
        uint32_t mid = (lo_div + hi_div) / 2u;
        if (!point_clean(mid, &m)) break;
        if (s_tone.div <= hi_div || s_tone.div >= lo_div) break;   /* nudged out */
        if (m.a < thresh) { hi_m = m; hi_div = s_tone.div; }
        else              { lo_m = m; lo_div = s_tone.div; }
    }

    {
        double f_lo = (double)clk / (2.0 * lo_div), f_hi = (double)clk / (2.0 * hi_div);
        double d_lo = db(lo_m.a / ref.a), d_hi = db(hi_m.a / ref.a);
        double fc = f_lo + (f_hi - f_lo) * ((-3.0103 - d_lo) / (d_hi - d_lo));

        printf("    -3 dB corner: %.0f Hz (between %.0f Hz at %+.2f dB and %.0f Hz at "
               "%+.2f dB)%s\n", fc, f_lo, d_lo, f_hi, d_hi,
               fc > FS_HZ / 2 ? ", measured through the alias" : "");
        printf("    design §6.4 predicts ~580 kHz for the amplifier alone; R9/C2 at "
               "321 kHz brings the pair to ~280 kHz\n");
    }
}

/*
 * Amplitude stepped in 16 steps at one frequency: a 32-bit pattern with h
 * bits high has fundamental sin(pi h/32) of the square wave. Nothing on the
 * bench changes between rows. The clip is where gain falls away from the
 * small-signal figure.
 */
static void cmd_amplitude(uint32_t hz)
{
    meas_t m;
    double g_ref = 0, clip_in = 0, clip_a = 0;
    uint32_t h;
    int clip_min = 0, clip_max = 0;

    printf("\n  --- amplitude at ~%lu Hz, %u steps, divider %.5f ---\n",
           (unsigned long)hz, AMP_PERIOD_BITS / 2u, s_ratio);
    printf("    (a 32-bit pattern: the frequency quantises to clk/(32 div); the DC "
           "of the source moves with duty, so `t` must cover the coupling cap)\n");
    row_header();
    for (h = 1; h <= AMP_PERIOD_BITS / 2u; h++) {
        point(hz, AMP_PERIOD_BITS, h, &m);
        if (m.a <= 0) continue;
        if (h <= 2) { g_ref = g_ref > m.gain ? g_ref : m.gain; continue; }
        if (clip_in == 0 && m.gain < 0.9 * g_ref) {
            clip_in = m.in; clip_a = m.a; clip_min = m.st.min; clip_max = m.st.max;
        }
    }
    if (clip_in > 0)
        printf("    output stopped growing at in = %.1f LSB (%.2f mV peak of fundamental): "
               "A %.0f LSB, codes %d..%d, small-signal gain %.2f\n",
               clip_in, clip_in / LSB_PER_VOLT * 1000.0, clip_a, clip_min, clip_max, g_ref);
    else
        printf("    still linear at full amplitude (gain within 10 %% of %.2f): "
               "a bigger shunt is needed to find the clip\n", g_ref);

    tone_set(hz, 0, 0);
}

/*
 * Input capacitance: with the source through a series R, A(f)^2 =
 * A0^2 / (1 + (f/fc)^2), so 1/A^2 is linear in f^2: intercept 1/A0^2,
 * slope 1/(A0 fc)^2. A straight-line fit gives fc without needing a point
 * far enough below it to count as flat, and C = 1 / (2 pi R fc).
 */
static void cmd_capacitance(uint32_t f0, uint32_t f1)
{
    meas_t m;
    double sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0;
    double f = f0, a0, fc, c, slope, icept, denom;

    if (f0 == 0 || f1 <= f0) { printf("    ? x f0 f1, Hz\n"); return; }

    printf("\n  --- input capacitance through %.0f kOhm, %lu..%lu Hz ---\n",
           s_series_r / 1000.0, (unsigned long)f0, (unsigned long)f1);
    row_header();
    while (f <= (double)f1 * 1.001) {
        point((uint32_t)(f + 0.5), 0, 0, &m);
        if (m.a > 0) {
            double x = s_tone.f_set * s_tone.f_set, y = 1.0 / (m.a * m.a);
            sx += x; sy += y; sxx += x * x; sxy += x * y; n += 1;
        }
        f *= 1.2599;                             /* three points per octave */
    }
    if (n < 3) { printf("    too few points\n"); return; }

    denom = n * sxx - sx * sx;
    slope = (n * sxy - sx * sy) / denom;
    icept = (sy - slope * sx) / n;
    if (icept <= 0) { printf("    fit failed (intercept %.3g)\n", icept); return; }
    a0 = 1.0 / sqrt(icept);
    if (slope <= 0) {
        printf("    no rolloff below %lu Hz: A0 %.1f LSB (gain %.2f); C < ~%.1f pF\n",
               (unsigned long)f1, a0, a0 / input_lsb(),
               1e12 / (2.0 * PI * s_series_r * 3.0 * f1));
        return;
    }
    fc = sqrt(icept / slope);
    c  = 1.0 / (2.0 * PI * s_series_r * fc);
    printf("    fit over %.0f points: A0 %.1f LSB (gain %.2f), fc %.0f Hz -> C_in = %.1f pF\n",
           n, a0, a0 / input_lsb(), fc, c * 1e12);
    if (fc > (double)f1)
        printf("    fc is above the swept range: extrapolated, sweep higher to confirm\n");
    printf("    design §5 budgets ~10 pF (-20 dB at 200 kHz through R2); 200 kHz gets "
           "%+.1f dB with this C\n",
           db(1.0 / sqrt(1.0 + (200000.0 / fc) * (200000.0 / fc))));
}

/* ======================================================================
 * Console
 * ====================================================================== */

static void help(void)
{
    printf("\n  v              VREF on GP27, and the output DC on GP26\n"
           "  s [f0 f1 step] sweep, Hz; default 20000 240000 10000\n"
           "  c              -3 dB corner from a 40 kHz reference, through the alias\n"
           "  n              noise floor, tone off, against the on-die figure\n"
           "  a [hz]         amplitude steps at one frequency until it clips\n"
           "  x [f0 f1]      input capacitance through the series R; default 1000 60000\n"
           "  f hz           one tone, one row\n"
           "  m              measure again at the same tone\n"
           "  o              tone off, GP2 high-Z\n"
           "  k shunt series divider, Ohm (ratio now %.5f)\n"
           "  r ohm          series R for x (now %.0f)\n"
           "  t ms           settle before each point (now %lu)\n"
           "  b blocks       capture length, 2048-sample blocks (now %u)\n"
           "  h              this\n",
           s_ratio, s_series_r,
           (unsigned long)s_settle_ms, (unsigned)s_cap_blocks);
}

/* One console line: a letter, then up to three numbers. */
static char parse(const char *line, uint32_t *arg, int *nargs)
{
    char cmd;

    while (*line == ' ') line++;
    *nargs = 0;
    if (!*line) return 0;
    cmd = *line++;
    for (;;) {
        char *end;
        while (*line == ' ') line++;
        if (*line < '0' || *line > '9' || *nargs == 3) break;
        arg[(*nargs)++] = (uint32_t)strtoul(line, &end, 10);
        line = end;
    }
    return cmd;
}

static void dispatch(const char *line)
{
    uint32_t a[3] = {0, 0, 0};
    int n;
    meas_t m;

    switch (parse(line, a, &n)) {
    case 0: return;
    case 'v': cmd_vref(); break;
    case 's':
        if (n == 3) cmd_sweep(a[0], a[1], a[2]);
        else        cmd_sweep(20000, 240000, 10000);
        break;
    case 'c': cmd_corner(); break;
    case 'n': cmd_noise(); break;
    case 'a': cmd_amplitude(n >= 1 ? a[0] : (s_tone.on ? (uint32_t)(s_tone.f_set + 0.5) : 200000u)); break;
    case 'x':
        if (n == 2) cmd_capacitance(a[0], a[1]);
        else        cmd_capacitance(1000, 60000);
        break;
    case 'f':
        if (n < 1) { printf("    ? f hz\n"); break; }
        row_header();
        point(a[0], 0, 0, &m);
        break;
    case 'm':
        if (!s_tone.on) { printf("    tone is off\n"); break; }
        row_header();
        if (measure(&m)) row(&m);
        break;
    case 'o': tone_off(); printf("    tone off, GP2 high-Z\n"); break;
    case 'k':
        if (n < 2 || a[0] == 0) { printf("    ? k shunt series\n"); break; }
        s_ratio = (double)a[0] / (double)(a[0] + a[1]);
        printf("    divider %lu / (%lu + %lu) = %.5f: %.2f mV p-p, %.1f LSB of fundamental "
               "at 50 %%\n", (unsigned long)a[0], (unsigned long)a[0], (unsigned long)a[1],
               s_ratio, 3300.0 * s_ratio, s_ratio * 4096.0 * TWO_OVER_PI);
        break;
    case 'r':
        if (n < 1 || a[0] == 0) { printf("    ? r ohm\n"); break; }
        s_series_r = a[0];
        printf("    series R %.0f Ohm\n", s_series_r);
        break;
    case 't':
        s_settle_ms = n >= 1 ? a[0] : 50;
        printf("    settle %lu ms\n", (unsigned long)s_settle_ms);
        break;
    case 'b':
        s_cap_blocks = n >= 1 && a[0] >= 1 ? (a[0] > CAP_BLOCKS_MAX ? CAP_BLOCKS_MAX : a[0]) : CAP_BLOCKS_DEF;
        printf("    capture %u blocks, %u samples, %.1f Hz bins\n", (unsigned)s_cap_blocks,
               (unsigned)(s_cap_blocks * ADC_RING_BLOCK),
               FS_HZ / (double)(s_cap_blocks * ADC_RING_BLOCK));
        break;
    case 'h': case '?': help(); break;
    default:  printf("    ? (h for help)\n"); break;
    }
}

int main(void)
{
    char line[48];
    size_t len = 0;
    int32_t die_mean;
    uint32_t die;
    uint64_t next_hb;
    meas_t m;

    stdio_init_all();
    sleep_ms(2000);          /* let the USB console attach before the report */

    printf("\nhandoff afe_sweep (M7: the analogue front end, characterised alone)\n");
    printf("  sys %lu Hz, ADC %d sps, capture %u samples, GP2 source, GP26 output, "
           "GP27 VREF\n", (unsigned long)clock_get_hz(clk_sys), HANDOFF_ADC_FS_HZ,
           (unsigned)(CAP_BLOCKS_DEF * ADC_RING_BLOCK));

    adc_ring_init();
    adc_gpio_init(VREF_PIN);
    adc_ring_start();

    die = adc_ring_noise_floor(&die_mean);
    printf("  M4 noise floor %lu.%lu LSB RMS (on-die, mean code %ld)\n",
           (unsigned long)(die / 10u), (unsigned long)(die % 10u), (long)die_mean);

    pio_carrier_init(HANDOFF_CARRIER_HZ);
    printf("  divider ratio %.5f (`k` to change), series R %.0f (`r`), `h` for help\n",
           s_ratio, s_series_r);

    /* One row unprompted, so a log with no commands still says something. */
    row_header();
    point(HANDOFF_CARRIER_HZ, 0, 0, &m);

    next_hb = time_us_64() + HEARTBEAT_US;
    for (;;) {
        int ch = getchar_timeout_us(0);

        if (ch == PICO_ERROR_TIMEOUT) {
            if (time_us_64() >= next_hb) {
                next_hb += HEARTBEAT_US;
                printf("  hb tone %s %.0f Hz\n",
                       s_tone.on ? "on" : "off", s_tone.on ? s_tone.f_set : 0.0);
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
