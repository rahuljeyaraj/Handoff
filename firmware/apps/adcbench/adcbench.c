/*
 * Handoff — ADC at 500 ksps and the real-time budget. M4.
 *
 * Hardware: NONE. Leave GP26 unconnected: the noise floor is taken on the
 * on-die temperature sensor, because a pin at a rail clips the noise.
 *
 * This closes design §17's second open item. The arithmetic says it will hold:
 * 500 k samples/s against a 150 MHz core is 300 cycles per sample and a
 * Goertzel inner iteration is single digits, so 2-5 % core load. THE RISK IS
 * DMA, INTERRUPT HANDLING AND RING OVERRUN. Instrument for that specifically.
 *
 * Exit criteria (development plan M4):
 *
 *   - measured sample rate within 0.01 % of 500 ksps, timed over 60 s
 *   - zero dropped DMA blocks in 10 minutes, with a counter proving it
 *   - core-1 loop headroom printed as a percentage
 *   - noise floor of the bare ADC recorded in LSB RMS
 *
 * That last figure is the reference every later amplitude measurement is
 * compared against, so record it rather than glance at it.
 *
 * The split under test is architecture §3.3's: core 1 owns the ADC block loop
 * and the Goertzel, core 0 owns everything slower. Chip energies cross on
 * ipc.c's lock-free ring, and ipc_dropped() is reported too — a headroom
 * figure that looks healthy while the ring is overflowing would be a lie.
 */
#include <stdio.h>

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "adc_ring.h"
#include "config.h"
#include "goertzel.h"
#include "ipc.h"

#define RATE_SECONDS  60u
#define SOAK_SECONDS  600u
#define REPORT_EVERY  30u

static volatile bool     s_run = true;
static volatile uint64_t s_busy_us;
static volatile uint32_t s_windows;

/* ---------------------------------------------------------------------- */

static void core1_main(void)
{
    gz_t g;
    uint64_t busy = 0;

    gz_init(&g, HANDOFF_GZ_N, HANDOFF_GZ_BIN);

    while (s_run) {
        const int16_t *blk;
        size_t n = 0, i;
        uint64_t t0;

        /*
         * The clock starts before the fetch because next_block() does the
         * DC-centring copy and that is core-1 work like any other. A failed
         * poll returns before the accumulate, so idle spinning is not charged
         * to the budget -- which is the number that would otherwise flatter
         * itself the most.
         */
        t0  = time_us_64();
        blk = adc_ring_next_block(&n);
        if (blk == 0) {
            tight_loop_contents();
            continue;
        }

        for (i = 0; i < n; i++) {
            uint32_t score;
            if (gz_push(&g, blk[i], &score)) {
                s_windows++;
                ipc_push_chip((uint16_t)(score > 0xFFFFu ? 0xFFFFu : score));
            }
        }

        busy += time_us_64() - t0;
        s_busy_us = busy;
    }
}

/* ---------------------------------------------------------------------- */

static int s_fail;

static void check(bool ok, const char *what)
{
    printf("    %-46s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) s_fail++;
}

static uint32_t ppm_err(uint32_t got, uint32_t want)
{
    uint32_t d = got > want ? got - want : want - got;
    return (uint32_t)(((uint64_t)d * 1000000u) / want);
}

/* Tenths of a percent of core 1 left idle. */
static uint32_t headroom_tenths(uint64_t busy_us, uint64_t elapsed_us)
{
    if (elapsed_us == 0) return 0;
    if (busy_us > elapsed_us) return 0;
    return (uint32_t)(((elapsed_us - busy_us) * 1000u) / elapsed_us);
}

int main(void)
{
    uint64_t t_start;
    uint32_t noise, sps, err, hr;
    int32_t  mean_code = -1;
    uint32_t last = 0;
    bool rate_done = false;

    stdio_init_all();
    sleep_ms(2000);

    printf("\nhandoff adcbench (M4: 500 ksps, DMA ring, core-1 budget)\n");
    printf("  ADC %d Hz, Goertzel N=%d bin %d, window rate %d Hz, chip %d Hz\n",
           HANDOFF_ADC_FS_HZ, HANDOFF_GZ_N, HANDOFF_GZ_BIN,
           HANDOFF_WINDOW_RATE_HZ, HANDOFF_CHIP_RATE_HZ);
    printf("  block %d samples, %d blocks, ipc ring %d chips\n",
           ADC_RING_BLOCK, ADC_RING_BLOCKS, IPC_RING_CHIPS);

    ipc_init();
    adc_ring_init();
    adc_ring_start();

    /* Noise floor first, with core 1 not yet running and nothing else
     * touching the ring — a bare-ADC figure has to be taken bare. */
    noise = adc_ring_noise_floor(&mean_code);
    printf("\n  noise floor: %lu.%lu LSB RMS (12-bit, mean code %ld, on-die temperature sensor)\n",
           (unsigned long)(noise / 10u), (unsigned long)(noise % 10u),
           (long)mean_code);

    /* Restart so the rate measurement counts from a known zero. */
    adc_ring_stop();
    adc_ring_start();
    multicore_launch_core1(core1_main);

    t_start = time_us_64();
    printf("\n  elapsed   sps        drops  ipc-drop  headroom\n");

    for (;;) {
        uint32_t secs = (uint32_t)((time_us_64() - t_start) / 1000000u);
        uint16_t chips[64];

        /* Core 0's half of §3.3: drain the ring so a full ring cannot be
         * mistaken for a core-1 problem. */
        while (ipc_pop_chips(chips, count_of(chips)) > 0)
            tight_loop_contents();

        if (secs >= last + REPORT_EVERY) {
            last = secs - (secs % REPORT_EVERY);
            hr = headroom_tenths(s_busy_us, time_us_64() - t_start);
            printf("  %4lus     %-9lu  %-5lu  %-8lu  %lu.%lu%%\n",
                   (unsigned long)secs,
                   (unsigned long)adc_ring_measured_sps(),
                   (unsigned long)adc_ring_overruns(),
                   (unsigned long)ipc_dropped(),
                   (unsigned long)(hr / 10u), (unsigned long)(hr % 10u));
        }

        if (secs >= RATE_SECONDS && !rate_done) {
            rate_done = true;
            sps = adc_ring_measured_sps();
            err = ppm_err(sps, (uint32_t)HANDOFF_ADC_FS_HZ);
            printf("\n  --- %lus rate ---\n", (unsigned long)RATE_SECONDS);
            printf("    measured %lu sps, nominal %d sps, error %lu ppm\n",
                   (unsigned long)sps, HANDOFF_ADC_FS_HZ, (unsigned long)err);
            check(err <= 100u, "sample rate within 0.01% of 500 ksps");
            check(noise > 0u && mean_code > 256 && mean_code < 3840,
                  "bare-ADC noise floor recorded away from the rails");
        }

        if (secs >= SOAK_SECONDS) break;
    }

    hr = headroom_tenths(s_busy_us, time_us_64() - t_start);

    /* Quoted again here: one 2048-sample block is 68 ppm at 60 s and 7 ppm
     * over the soak, so this is the figure to record. */
    sps = adc_ring_measured_sps();
    printf("\n  --- %lu minute soak ---\n", (unsigned long)(SOAK_SECONDS / 60u));
    printf("    sample rate:        %lu sps (%lu ppm)\n", (unsigned long)sps,
           (unsigned long)ppm_err(sps, (uint32_t)HANDOFF_ADC_FS_HZ));
    printf("    dropped DMA blocks: %lu\n", (unsigned long)adc_ring_overruns());
    printf("    dropped ipc chips:  %lu\n", (unsigned long)ipc_dropped());
    printf("    windows scored:     %lu\n", (unsigned long)s_windows);
    printf("    core-1 headroom:    %lu.%lu%%\n",
           (unsigned long)(hr / 10u), (unsigned long)(hr % 10u));

    check(adc_ring_overruns() == 0u, "zero dropped DMA blocks over the soak");
    check(ipc_dropped() == 0u, "zero chips dropped between the cores");
    check(hr > 0u, "core-1 headroom printed as a percentage");

    printf("\n  %s (%d failure%s)\n",
           s_fail == 0 ? "M4 EXIT CRITERIA MET" : "M4 NOT MET",
           s_fail, s_fail == 1 ? "" : "s");

    s_run = false;
    for (;;) tight_loop_contents();
}
