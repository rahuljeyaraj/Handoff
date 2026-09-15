/*
 * Handoff — hal.h on RP2350. M5. See hal_pico.h.
 *
 * Nothing here is new signal processing. Transmit is pio_carrier.c (M3),
 * receive is adc_ring.c and ipc.c (M4), and the DSP between them is the same
 * lib/dsp the host simulator runs. What this file adds is the core split of
 * architecture §3.3, made real:
 *
 *   core 1   block loop: ADC ring -> Goertzel -> symbol sync -> carrier
 *            detect -> ipc ring. Nothing slower than the chip rate lives
 *            here, and nothing here calls printf.
 *   core 0   everything the HAL hands out: chips popped from the ipc ring,
 *            chips queued to the PIO, time, telemetry, randomness.
 *
 * Transmit never touches core 1 at all. Core 0 fills a chip buffer, DMA clocks
 * it into the PIO, the PIO gates the carrier. TX is essentially free.
 */
#include "hal_pico.h"

#include <string.h>

#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

#include "adc_ring.h"
#include "carrier.h"
#include "config.h"
#include "goertzel.h"
#include "ipc.h"
#include "pio_carrier.h"
#include "sync.h"
#include "tlm.h"

/* Deliberately not in tlm.h: only the owner of the block loop may feed the
 * raw burst, and after M5 that owner is this file. See tlm_usb.c. */
void tlm_usb_raw_feed(const int16_t *samples, size_t n, uint64_t first_idx);

static hal_iface_t s_iface;
static bool        s_inited;

static uint32_t s_noise_tenths;
static int32_t  s_noise_mean = -1;

/* ---- core 1 <-> core 0 ------------------------------------------------- */

/*
 * Each of these is written by exactly one core. The carrier request is the
 * one handshake: core 0 writes s_carrier_req, core 1 re-tunes and echoes it
 * into s_carrier_ack, and core 0 spins on the echo. The DSP state itself
 * never leaves core 1.
 */
static volatile uint32_t s_carrier_req = HANDOFF_CARRIER_HZ;
static volatile uint32_t s_carrier_ack;
static volatile uint32_t s_level;         /* carrier_level(), for §7.3 */
static volatile uint32_t s_chips;
static volatile uint32_t s_windows;
static volatile uint64_t s_busy_us;
static volatile bool     s_core1_up;
static uint64_t          s_core1_t0;

static void core1_dsp_init(gz_t *g, sync_t *sy, carrier_t *car, uint32_t hz)
{
    /* Bin spacing equals the window rate, so the bin index is just the
     * carrier divided by it — config.h static-asserts this for the default
     * carrier and hal_pico_set_carrier() checks it for any other. */
    gz_init(g, HANDOFF_GZ_N, (uint16_t)(hz / (uint32_t)HANDOFF_WINDOW_RATE_HZ));
    sync_init(sy, HANDOFF_WINDOWS_PER_CHIP, HANDOFF_CHIP_GUARD);
    carrier_init(car);
}

static void core1_main(void)
{
    gz_t      g;
    sync_t    sy;
    carrier_t car;
    uint32_t  hz = s_carrier_req;
    uint64_t  busy = 0;

    core1_dsp_init(&g, &sy, &car, hz);
    s_carrier_ack = hz;
    s_core1_up = true;

    for (;;) {
        const int16_t *blk;
        size_t n = 0, i;
        uint64_t t0, base;
        uint32_t seq = 0;

        if (s_carrier_req != hz) {
            hz = s_carrier_req;
            core1_dsp_init(&g, &sy, &car, hz);
            s_carrier_ack = hz;
        }

        /* Same accounting as M4's adcbench: the DC-centring copy inside
         * next_block() is core-1 work, a failed poll is not. */
        t0  = time_us_64();
        blk = adc_ring_next_block_seq(&n, &seq);
        if (blk == 0) {
            tight_loop_contents();
            continue;
        }
        base = (uint64_t)seq * ADC_RING_BLOCK;

        tlm_usb_raw_feed(blk, n, base);

        for (i = 0; i < n; i++) {
            uint32_t score;
            uint16_t chip;

            if (!gz_push(&g, blk[i], &score)) continue;
            s_windows++;

            if (!sync_push(&sy, score, &chip)) continue;

            carrier_push(&car, chip);
            s_level = carrier_level(&car);
            s_chips++;
            ipc_push_chip(chip, (uint32_t)(base + i));
        }

        busy += time_us_64() - t0;
        s_busy_us = busy;
    }
}

/* ---- hal_iface_t bindings --------------------------------------------- */

/*
 * When the PIO will have finished clocking out the last chip. pio_carrier_busy()
 * clears when the DMA is done and the TX FIFO is empty, but the OSR can still
 * hold up to 32 stream bits — the released word every send ends in, 40 us at
 * 200 kHz, 200 us at 40 kHz. The last chips of a frame are the CRC, so busy
 * has to cover them: it is held until the chips have had their airtime plus
 * that tail. The airtime is counted from the DMA start the generator
 * reports, not from when send() returned, so the pad-idle instant is known
 * to a slot and M13 can time settling from it.
 */
static uint64_t s_tx_until;
static uint64_t s_tx_pad_idle;   /* when the last chip of the last send ends */

/*
 * A transmit that is still busy well after its airtime has stalled: the
 * state machine stopped consuming, or the DMA never finished. Seen once at
 * M5, after 48 good frames. Until the cause is pinned this is counted,
 * the hardware state at the moment is kept for the console, and the path is
 * reset so the link carries on rather than refusing every frame after it.
 */
#define TX_STALL_GRACE_US 20000u

static uint32_t            s_tx_stalls;
static pio_carrier_state_t s_tx_stall_state;

static bool tx_stalled_and_reset(void)
{
    if (!pio_carrier_busy() || time_us_64() < s_tx_until + TX_STALL_GRACE_US)
        return false;
    pio_carrier_state(&s_tx_stall_state);
    s_tx_stalls++;
    pio_carrier_reset();
    return true;
}

static void p_tx_drive(void *ctx, bool on)
{
    (void)ctx;
    pio_carrier_drive(on);
}

static size_t p_tx_chips(void *ctx, const uint8_t *chips, size_t n)
{
    uint32_t tail_us;
    (void)ctx;

    if (n == 0 || n > pio_carrier_max_chips()) return 0;
    if (tx_stalled_and_reset()) { /* recovered: fall through and send */ }
    else if (pio_carrier_busy() || time_us_64() < s_tx_until) return 0;

    /* The released word the send appends: 32 stream bits at the chip's
     * bits-per-chip -- 40 us at 200 kHz, 200 us at 40 kHz. */
    tail_us = 32u * (uint32_t)HANDOFF_CHIP_US / pio_carrier_bits_per_chip();

    pio_carrier_send(chips, n);
    s_tx_pad_idle = pio_carrier_started_us() + (uint64_t)n * HANDOFF_CHIP_US;
    s_tx_until    = s_tx_pad_idle + tail_us;
    return n;
}

static bool p_tx_busy(void *ctx)
{
    (void)ctx;
    if (tx_stalled_and_reset()) return false;
    return pio_carrier_busy() || time_us_64() < s_tx_until;
}

static size_t p_rx_chips(void *ctx, uint16_t *dst, size_t max)
{
    (void)ctx;
    return ipc_pop_chips(dst, 0, max);
}

static uint32_t p_rx_carrier_level(void *ctx)
{
    (void)ctx;
    return s_level;
}

static uint64_t p_now_us(void *ctx)
{
    (void)ctx;
    return time_us_64();
}

static void p_random(void *ctx, void *dst, size_t n)
{
    uint8_t *b = (uint8_t *)dst;
    (void)ctx;

    while (n) {
        uint32_t v = get_rand_32();
        size_t k = n < 4 ? n : 4;
        memcpy(b, &v, k);
        b += k;
        n -= k;
    }
}

/* ---- construction ------------------------------------------------------ */

const hal_iface_t *hal_pico_init(void)
{
    if (s_inited) return &s_iface;

    ipc_init();
    adc_ring_init();
    adc_ring_start();

    /* M4's reference figure, taken bare: nothing consuming the ring yet. */
    s_noise_tenths = adc_ring_noise_floor(&s_noise_mean);

    /* Restart so the rate and overrun counters start from a known zero. */
    adc_ring_stop();
    adc_ring_start();

    /* The carrier comes up owned by the generator; the resting state of a
     * wristband is listening, and design §6.3 says listening means high-Z.
     * The pad's input buffer goes off for good: the link never reads GP2,
     * and with it off RP2350-E9 cannot latch a released pad (§9.8). */
    pio_carrier_init(s_carrier_req);
    pio_carrier_sense(false);
    pio_carrier_drive(false);

    tlm_usb_init(0);

    s_iface.ctx              = 0;
    s_iface.tx_drive         = p_tx_drive;
    s_iface.tx_chips         = p_tx_chips;
    s_iface.tx_busy          = p_tx_busy;
    s_iface.rx_chips         = p_rx_chips;
    s_iface.rx_carrier_level = p_rx_carrier_level;
    s_iface.now_us           = p_now_us;
    s_iface.telemetry        = tlm_sink;
    s_iface.random           = p_random;

    s_core1_t0 = time_us_64();
    multicore_launch_core1(core1_main);
    while (!s_core1_up) tight_loop_contents();

    s_inited = true;
    return &s_iface;
}

/* ---- instrumentation --------------------------------------------------- */

uint8_t hal_pico_core1_load(void)
{
    uint64_t elapsed = time_us_64() - s_core1_t0;
    uint64_t busy    = s_busy_us;

    if (!s_inited || elapsed == 0) return 0;
    if (busy > elapsed) return 100;
    return (uint8_t)((busy * 100u) / elapsed);
}

uint32_t hal_pico_overruns(void) { return adc_ring_overruns(); }

uint32_t hal_pico_noise_floor(int32_t *mean_code)
{
    if (mean_code) *mean_code = s_noise_mean;
    return s_noise_tenths;
}

bool hal_pico_set_carrier(uint32_t hz)
{
    uint32_t bin;

    /* The same three conditions config.h static-asserts for the default. */
    if (hz == 0 || hz % (uint32_t)HANDOFF_WINDOW_RATE_HZ != 0) return false;
    bin = hz / (uint32_t)HANDOFF_WINDOW_RATE_HZ;
    if (bin == 0 || 2u * bin >= (uint32_t)HANDOFF_GZ_N) return false;
    if ((uint32_t)HANDOFF_SYS_CLK_HZ % (2u * PIO_CARRIER_SLOT_CYCLES * hz) != 0) return false;

    if (!s_inited) {
        s_carrier_req = hz;
        return true;
    }

    while (p_tx_busy(0)) tight_loop_contents();

    pio_carrier_init(hz);
    pio_carrier_sense(false);
    pio_carrier_drive(false);

    s_carrier_req = hz;
    while (s_carrier_ack != hz) tight_loop_contents();
    return true;
}

uint32_t hal_pico_carrier_hz(void) { return s_carrier_req; }

uint32_t hal_pico_tx_stalls(pio_carrier_state_t *last)
{
    if (last) *last = s_tx_stall_state;
    return s_tx_stalls;
}
uint32_t hal_pico_chips(void)      { return s_chips; }
uint32_t hal_pico_windows(void)    { return s_windows; }

size_t hal_pico_rx_chips_at(uint16_t *dst, uint32_t *idx, size_t max)
{
    return ipc_pop_chips(dst, idx, max);
}

uint64_t hal_pico_sample_us(uint64_t idx)  { return adc_ring_sample_us(idx); }
uint64_t hal_pico_tx_pad_idle_us(void)     { return s_tx_pad_idle; }
