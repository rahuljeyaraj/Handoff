/*
 * Handoff — free-running ADC into a DMA ring. M4. See adc_ring.h.
 *
 * Two DMA channels ping-pong into two raw blocks and chain to each other, so
 * the ADC is never waiting on software to re-arm it. The interrupt does the
 * least it can: mark the block full, re-arm the channel that just finished,
 * and count an overrun if the consumer had not taken the previous one.
 *
 * That counter is the whole point of the milestone. Development plan M4 says
 * the arithmetic was never the risk -- 300 cycles per sample against a
 * single-digit Goertzel iteration -- and that DMA, interrupts and ring overrun
 * are. So the overrun is counted rather than assumed absent.
 */
#include "adc_ring.h"

#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

#include "config.h"
#include "goertzel.h"   /* gz_isqrt64, for the noise floor */

#define ADC_INPUT   0     /* GP26 */
#define ADC_MIDPOINT 2048 /* 12-bit, DC-centred for the DSP */

static uint16_t s_raw[ADC_RING_BLOCKS][ADC_RING_BLOCK];
static int16_t  s_out[ADC_RING_BLOCK];

static volatile bool     s_full[ADC_RING_BLOCKS];
static volatile uint32_t s_overruns;
static volatile uint32_t s_blocks;
static volatile uint8_t  s_next_irq;    /* block the IRQ will fill next */
static uint8_t           s_next_read;   /* block the consumer wants next */

static int      s_dma[ADC_RING_BLOCKS];
static uint64_t s_t0;
static bool     s_running;

/* ---------------------------------------------------------------------- */

static void __isr on_dma(void)
{
    uint i;

    for (i = 0; i < ADC_RING_BLOCKS; i++) {
        if (!(dma_hw->ints0 & (1u << s_dma[i]))) continue;
        dma_hw->ints0 = 1u << s_dma[i];

        /* Still full means core 1 never took the last one. That is a dropped
         * block, and it is the number this milestone exists to report. */
        if (s_full[i]) s_overruns++;

        s_full[i] = true;
        s_blocks++;

        /* Re-arm without triggering: the other channel's chain does that. */
        dma_channel_set_trans_count(s_dma[i], ADC_RING_BLOCK, false);
        dma_channel_set_write_addr(s_dma[i], s_raw[i], false);
    }
}

/* ---------------------------------------------------------------------- */

void adc_ring_init(void)
{
    uint i;

    adc_init();
    adc_gpio_init(26 + ADC_INPUT);
    adc_select_input(ADC_INPUT);

    /* FIFO on, DREQ on, one sample per request, no error bit, no byte shift:
     * 12-bit right-aligned samples straight into 16-bit DMA transfers. */
    adc_fifo_setup(true, true, 1, false, false);

    /* design §10.2: clkdiv 0 is free-running, 48 MHz / 96 = 500.000 ksps. */
    adc_set_clkdiv(0);

    for (i = 0; i < ADC_RING_BLOCKS; i++) {
        if (s_dma[i] == 0 && !s_running) s_dma[i] = (int)dma_claim_unused_channel(true);
        s_full[i] = false;
    }

    for (i = 0; i < ADC_RING_BLOCKS; i++) {
        dma_channel_config c = dma_channel_get_default_config((uint)s_dma[i]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
        channel_config_set_read_increment(&c, false);
        channel_config_set_write_increment(&c, true);
        channel_config_set_dreq(&c, DREQ_ADC);
        channel_config_set_chain_to(&c, (uint)s_dma[(i + 1) % ADC_RING_BLOCKS]);
        dma_channel_configure((uint)s_dma[i], &c, s_raw[i], &adc_hw->fifo,
                              ADC_RING_BLOCK, false);
        dma_channel_set_irq0_enabled((uint)s_dma[i], true);
    }

    irq_set_exclusive_handler(DMA_IRQ_0, on_dma);
    irq_set_enabled(DMA_IRQ_0, true);
}

void adc_ring_start(void)
{
    uint i;

    for (i = 0; i < ADC_RING_BLOCKS; i++) s_full[i] = false;
    s_overruns = 0;
    s_blocks   = 0;
    s_next_irq = 0;
    s_next_read = 0;

    adc_fifo_drain();
    adc_run(true);

    s_t0 = time_us_64();
    s_running = true;
    dma_channel_start((uint)s_dma[0]);
}

void adc_ring_stop(void)
{
    uint i;

    adc_run(false);
    for (i = 0; i < ADC_RING_BLOCKS; i++)
        if (dma_channel_is_busy((uint)s_dma[i])) dma_channel_abort((uint)s_dma[i]);
    s_running = false;
    adc_fifo_drain();
}

/* ---------------------------------------------------------------------- */

const int16_t *adc_ring_next_block(size_t *count)
{
    uint8_t b = s_next_read;
    size_t i;

    if (!s_full[b]) {
        if (count) *count = 0;
        return 0;
    }

    /* DC-centre into the caller's view. goertzel.c rejects DC at any bin but
     * a signed sample keeps gz_mag2's headroom arithmetic honest. */
    for (i = 0; i < ADC_RING_BLOCK; i++)
        s_out[i] = (int16_t)((int)s_raw[b][i] - ADC_MIDPOINT);

    s_full[b] = false;
    s_next_read = (uint8_t)((b + 1) % ADC_RING_BLOCKS);

    if (count) *count = ADC_RING_BLOCK;
    return s_out;
}

/* ---------------------------------------------------------------------- */

uint32_t adc_ring_measured_sps(void)
{
    uint64_t dt = time_us_64() - s_t0;
    uint64_t n  = (uint64_t)s_blocks * ADC_RING_BLOCK;

    if (dt == 0) return 0;
    return (uint32_t)((n * 1000000u + dt / 2u) / dt);
}

uint32_t adc_ring_overruns(void) { return s_overruns; }

/*
 * RMS deviation of a bare block, in LSB. Blocking, and deliberately so: it is
 * a bench measurement taken with nothing else running, and every later
 * amplitude figure — M7's amplifier noise, M8's link budget — is quoted
 * against it.
 */
uint32_t adc_ring_noise_floor_lsb(void)
{
    const int16_t *blk;
    size_t n = 0, i;
    int64_t sum = 0;
    uint64_t sq = 0;
    int32_t mean;
    uint32_t deadline = 1000000u;

    while ((blk = adc_ring_next_block(&n)) == 0 && deadline--)
        tight_loop_contents();
    if (blk == 0 || n == 0) return 0;

    for (i = 0; i < n; i++) sum += blk[i];
    mean = (int32_t)(sum / (int64_t)n);

    for (i = 0; i < n; i++) {
        int32_t d = blk[i] - mean;
        sq += (uint64_t)((int64_t)d * d);
    }

    /* Round to the nearest LSB; the figure is quoted to one place at most. */
    return gz_isqrt64((sq + n / 2u) / n);
}
