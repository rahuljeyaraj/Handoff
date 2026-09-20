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
 *
 * THE WRITE ADDRESS WRAPS IN HARDWARE. A chained restart does not reset a
 * channel's write pointer, only its count, so a channel the interrupt has
 * not re-armed carries on from the end of its block into whatever follows
 * s_raw in RAM -- the link state machine, the USB device, the flash
 * driver's lockout, this file's own s_dma[]. Anything that keeps DMA_IRQ_0
 * out for more than one block, 4 ms, does that: a flash write holds core
 * 0's interrupts off for a sector erase, ~45 ms, and every `w` on the
 * bench (and every card provisioned from the phone, the same path) was
 * trampling RAM -- seen 15 Sep 2026 as a chip storm, a 100 % core-1 load,
 * a record that did not survive, and a hard hang on the next write. With
 * the DMA ring-wrap set to the block size and the blocks aligned to it,
 * a restart without a re-arm only ever overwrites its own block. A
 * blackout then costs data and never memory; on_dma() counts the lost
 * blocks from the clock so the sample clock stays true. (The flash write also
 * overflowed core 0's stack into core 1's, a separate defect fixed the same
 * day; see store.h. Either one alone was enough to take the band down.)
 */
#include "adc_ring.h"

#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "config.h"
#include "goertzel.h"   /* gz_isqrt64, for the noise floor */

#define ADC_INPUT   0     /* GP26 */
#define ADC_MIDPOINT 2048 /* 12-bit, DC-centred for the DSP */

/* Each block is exactly ADC_RING_WRAP bytes, so aligning the array aligns
 * every block, which is what the hardware wrap needs. */
#define ADC_RING_WRAP_BITS 12
#define ADC_RING_WRAP      (1u << ADC_RING_WRAP_BITS)
static uint16_t s_raw[ADC_RING_BLOCKS][ADC_RING_BLOCK]
    __attribute__((aligned(ADC_RING_WRAP)));
_Static_assert(sizeof s_raw[0] == ADC_RING_WRAP, "block must be one wrap region");
static int16_t  s_out[ADC_RING_BLOCK];

static volatile bool     s_full[ADC_RING_BLOCKS];
static volatile uint32_t s_seq[ADC_RING_BLOCKS];   /* ordinal of the block in it */
static volatile uint32_t s_overruns;
static volatile uint32_t s_blocks;

/* -1 is "not claimed yet", the same sentinel pio_carrier.c uses. Zero will
 * not do: channel 0 is a real channel, and on this board it is the one the
 * ring gets first, so a zero sentinel cannot tell "unclaimed" from "claimed
 * channel 0" and a second init() would claim a third channel and leave the
 * first chained to nothing. */
static int      s_dma[ADC_RING_BLOCKS] = { -1, -1 };
_Static_assert(ADC_RING_BLOCKS == 2, "s_dma's initialiser lists one -1 per block");
static uint64_t s_t0;
static bool     s_running;

/* ---------------------------------------------------------------------- */

/* One block on the sample clock. The ADC and the timer share the crystal,
 * so this is exact, not approximate. */
#define ADC_RING_BLOCK_US ((uint64_t)ADC_RING_BLOCK * 1000000u / (uint64_t)HANDOFF_ADC_FS_HZ)

static void __isr on_dma(void)
{
    bool     good[ADC_RING_BLOCKS];
    uint     i, pending = 0;
    uint32_t expected;

    /*
     * A pending channel that is BUSY completed a block this handler never
     * saw and is already filling the same buffer again -- the wrap keeps it
     * in bounds -- so what is in that buffer is torn. It is dropped and
     * counted, not re-armed mid-flight. In steady state the completed
     * channel is always idle (the other one is filling), so this costs
     * nothing until an interrupt is late by a whole block.
     */
    for (i = 0; i < ADC_RING_BLOCKS; i++) {
        good[i] = false;
        if (!(dma_hw->ints0 & (1u << s_dma[i]))) continue;
        dma_hw->ints0 = 1u << s_dma[i];
        if (dma_channel_is_busy((uint)s_dma[i])) { s_overruns++; continue; }
        good[i] = true;
        pending++;
    }

    /*
     * Blocks the converter finished while this handler could not run are
     * gone -- the channels wrap in place, see the head of the file -- but
     * time is not, and the count has to say so before the blocks in hand
     * are labelled: adc_ring_sample_us() places every chip on the sample
     * clock from this count, and the own-send cut is placed on that. Left
     * uncorrected, eight flash writes put the clock 300 ms behind and the
     * band woke on its own shouts (15 Sep 2026). The ordinal of the block
     * being filled right now is known from the clock alone; rounding makes
     * it robust to the few microseconds between a block ending and this
     * handler running.
     */
    expected = (uint32_t)((time_us_64() - s_t0 + ADC_RING_BLOCK_US / 2u) / ADC_RING_BLOCK_US);
    if (expected > s_blocks + pending) {
        uint32_t lost = expected - s_blocks - pending;
        s_overruns += lost;
        s_blocks   += lost;
    }

    for (i = 0; i < ADC_RING_BLOCKS; i++) {
        if (!good[i]) continue;

        /* Still full means core 1 never took the last one. That is a dropped
         * block, and it is the number this milestone exists to report. */
        if (s_full[i]) s_overruns++;

        s_seq[i]  = s_blocks;
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
        if (s_dma[i] < 0) s_dma[i] = (int)dma_claim_unused_channel(true);
        s_full[i] = false;
    }

    for (i = 0; i < ADC_RING_BLOCKS; i++) {
        dma_channel_config c = dma_channel_get_default_config((uint)s_dma[i]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
        channel_config_set_read_increment(&c, false);
        channel_config_set_write_increment(&c, true);
        channel_config_set_ring(&c, true, ADC_RING_WRAP_BITS);   /* see the head of the file */
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

    /*
     * Re-arm every channel from the top of its block, whatever state stop()
     * left it in. This is not optional on RP2350: TRANS_COUNT there is a
     * reload value, so a channel aborted mid-block restarts with a FULL count
     * from a write pointer partway through its block and DMAs past the end
     * of s_raw -- over s_out, the flags, and s_dma[] itself, after which the
     * IRQ handler clears the wrong bit and storms. That was M4's silent USB.
     */
    for (i = 0; i < ADC_RING_BLOCKS; i++) {
        dma_channel_set_write_addr((uint)s_dma[i], s_raw[i], false);
        dma_channel_set_trans_count((uint)s_dma[i], ADC_RING_BLOCK, false);
        dma_hw->ints0 = 1u << s_dma[i];
        s_full[i] = false;
    }
    s_overruns = 0;
    s_blocks   = 0;

    adc_fifo_drain();
    adc_run(true);

    s_t0 = time_us_64();
    s_running = true;
    dma_channel_start((uint)s_dma[0]);
}

bool adc_ring_running(void) { return s_running; }

/*
 * RP2350-E5, "Interactions between CHAIN_TO and ABORT of active channels".
 * These two channels chain to each other, which is the exact case the
 * erratum calls out: aborting one makes its CHAIN_TO fire, and a channel
 * part-way through an ABORT can still be re-triggered, so aborting them one
 * at a time with EN set restarts the other -- and the loop restarts the
 * first right back. adc_ring_stop() would then return with a channel still
 * armed, and adc_ring_start() would arm the second on top of it. Both would
 * then serve DREQ_ADC, each taking every other sample into its own block:
 * two half-rate blocks instead of one whole one, which is a receiver that
 * decodes nothing and an sps that reads double.
 *
 * The datasheet's sequence (12.6.8.3) is to clear EN on every channel in
 * the chain FIRST, abort them in ONE write, and poll until they come to
 * rest. EN is restored afterwards so the channels are left configured and
 * idle, which is what start() expects. CHAIN_TO is deliberately left alone:
 * the erratum's workaround asks only for EN, start() does not re-establish
 * chaining, and clearing it here would break the ping-pong on restart.
 */
void adc_ring_stop(void)
{
    uint32_t mask = 0;
    uint i;

    adc_run(false);

    for (i = 0; i < ADC_RING_BLOCKS; i++) {
        dma_channel_set_irq0_enabled((uint)s_dma[i], false);
        hw_clear_bits(&dma_channel_hw_addr((uint)s_dma[i])->al1_ctrl,
                      DMA_CH0_CTRL_TRIG_EN_BITS);
        mask |= 1u << s_dma[i];
    }

    dma_hw->abort = mask;
    while (dma_hw->abort & mask) tight_loop_contents();

    /* In-flight transfers cannot be revoked; the datasheet forbids
     * restarting a channel before its BUSY clears. */
    for (i = 0; i < ADC_RING_BLOCKS; i++)
        while (dma_channel_is_busy((uint)s_dma[i])) tight_loop_contents();

    for (i = 0; i < ADC_RING_BLOCKS; i++) {
        dma_hw->ints0 = 1u << s_dma[i];   /* an abort can leave one pending */
        hw_set_bits(&dma_channel_hw_addr((uint)s_dma[i])->al1_ctrl,
                    DMA_CH0_CTRL_TRIG_EN_BITS);
        dma_channel_set_irq0_enabled((uint)s_dma[i], true);
    }

    s_running = false;
    adc_fifo_drain();
}

/* ---------------------------------------------------------------------- */

const int16_t *adc_ring_next_block(size_t *count)
{
    return adc_ring_next_block_seq(count, 0);
}

/*
 * The oldest full block, by ordinal -- not by turn. Alternating 0, 1, 0, 1
 * assumes the consumer saw every completion; after an interrupt blackout
 * it can come back on the wrong foot and, with the other block always
 * full by the time it looks, hand every pair over newest first for the
 * rest of the run. That was a band whose frames all failed their CRC
 * after a flash write (15 Sep 2026): 4 ms of samples swapped, forever.
 */
const int16_t *adc_ring_next_block_seq(size_t *count, uint32_t *seq)
{
    int b = -1;
    size_t i;

    for (i = 0; i < ADC_RING_BLOCKS; i++) {
        if (!s_full[i]) continue;
        if (b < 0 || (int32_t)(s_seq[i] - s_seq[b]) < 0) b = (int)i;
    }
    if (b < 0) {
        if (count) *count = 0;
        return 0;
    }
    if (seq) *seq = s_seq[b];

    /* DC-centre into the caller's view. goertzel.c rejects DC at any bin but
     * a signed sample keeps gz_mag2's headroom arithmetic honest. */
    for (i = 0; i < ADC_RING_BLOCK; i++)
        s_out[i] = (int16_t)((int)s_raw[b][i] - ADC_MIDPOINT);

    s_full[b] = false;

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

uint64_t adc_ring_sample_us(uint64_t idx)
{
    return s_t0 + (idx * 1000000u) / (uint64_t)HANDOFF_ADC_FS_HZ;
}

/*
 * RMS deviation of a bare block, in tenths of an LSB. Blocking, and
 * deliberately so: it is a bench measurement taken with nothing else running,
 * and every later amplitude figure -- M7's amplifier noise, M8's link budget
 * -- is quoted against it.
 *
 * The input is the on-die temperature sensor (ADC channel 4): about 0.71 V,
 * code ~880, a quiet diode, and no external parts. The plan originally said
 * "leave GP26 at ground or 3V3", and that measures nothing: at a rail the
 * ADC clips one side of the noise and reports 0 LSB RMS. Enabling both of
 * the pad's pulls does not help either -- on RP2 that is a bus keeper, not a
 * divider. The ADC core is the same whichever channel is selected, so the
 * figure is the converter's own. Channel 0 is reselected before this
 * returns.
 */
uint32_t adc_ring_noise_floor(int32_t *mean_code)
{
    const int16_t *blk;
    size_t n = 0, i;
    int64_t sum = 0;
    uint64_t sq = 0;
    int32_t mean;
    uint32_t deadline = 1000000u;
    uint32_t rms_tenths = 0;

    adc_set_temp_sensor_enabled(true);
    adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);
    sleep_ms(10);                               /* let the sensor settle */

    /* Discard whatever was in flight while the input changed, then take the
     * first whole block after it. */
    while ((blk = adc_ring_next_block(&n)) == 0 && deadline--)
        tight_loop_contents();
    deadline = 1000000u;
    while ((blk = adc_ring_next_block(&n)) == 0 && deadline--)
        tight_loop_contents();

    if (blk != 0 && n != 0) {
        for (i = 0; i < n; i++) sum += blk[i];
        mean = (int32_t)(sum / (int64_t)n);

        for (i = 0; i < n; i++) {
            int32_t d = blk[i] - mean;
            sq += (uint64_t)((int64_t)d * d);
        }

        /* sqrt(100 * sq / n) is 10 * RMS: tenths of an LSB. */
        rms_tenths = gz_isqrt64((sq * 100u + n / 2u) / n);
        if (mean_code) *mean_code = mean + ADC_MIDPOINT;
    } else if (mean_code) {
        *mean_code = -1;
    }

    adc_select_input(ADC_INPUT);
    adc_set_temp_sensor_enabled(false);
    return rms_tenths;
}

/* ---------------------------------------------------------------------- */

/*
 * The in-ring measurement of adc_ring.h. Same handshake as hal_pico's
 * carrier change: core 0 bumps s_aux_req, the servicer echoes it into
 * s_aux_ack when it is done, and each variable has exactly one writer.
 *
 * Blocks are classified by their ordinal against the swap, not by counting
 * phases: the IRQ's s_blocks is the ordinal of the block the DMA is filling
 * right now, so at the swap in, everything below it is clean and it is the
 * mixed one; at the swap back, everything up to it is dirty. The ordinal is
 * read BEFORE the swap in and AFTER the swap back, so a completion landing
 * between the two only ever costs one extra clean block, never passes a
 * dirty one — the price is a sample or two of the old channel at the head
 * of the measured block, which the settle skip below absorbs. A skipped
 * ordinal (an overrun) falls out the same way.
 *
 * The first samples of the measured block are thrown away for the same
 * reason pico-examples throws three away: the VSYS divider is ~66 kOhm
 * Thevenin, far more than the sample-and-hold likes, and the mux step has
 * to settle through it. 128 samples is 256 us, some forty times that, and
 * it costs nothing out of 2048. It matters when the swap lands right at a
 * block boundary and the transient falls at the head of the measured block
 * instead of in the one discarded before it.
 */
#define AUX_SETTLE_SAMPLES 128

enum { AUX_IDLE, AUX_IN, AUX_OUT };

static volatile uint32_t s_aux_req;      /* core 0 */
static volatile uint8_t  s_aux_chan;     /* core 0 */
static volatile uint8_t  s_aux_gpio;     /* core 0 */
static volatile bool     s_aux_cancel;   /* core 0 */
static volatile uint32_t s_aux_ack;      /* servicer */
static volatile int32_t  s_aux_mean;     /* servicer */
static uint8_t           s_aux_phase;    /* servicer only */
static uint32_t          s_aux_edge;     /* ordinal of the block mixed by the last swap */
static int32_t           s_aux_result;   /* servicer only */

bool adc_ring_aux_request(uint8_t channel, uint8_t gpio)
{
    if (!s_running || s_aux_req != s_aux_ack) return false;
    s_aux_chan   = channel;
    s_aux_gpio   = gpio;
    s_aux_cancel = false;
    __dmb();                    /* the parameters land before the request */
    s_aux_req++;
    return true;
}

bool adc_ring_aux_ready(int32_t *mean_code)
{
    if (s_aux_ack != s_aux_req) return false;
    __dmb();
    if (mean_code) *mean_code = s_aux_mean;
    return true;
}

void adc_ring_aux_cancel(void) { s_aux_cancel = true; }

static void aux_finish(int32_t mean)
{
    s_aux_mean  = mean;
    s_aux_phase = AUX_IDLE;
    __dmb();                    /* the answer lands before the ack */
    s_aux_ack   = s_aux_req;
}

void adc_ring_aux_poll(void)
{
    if (s_aux_phase != AUX_IDLE || s_aux_req == s_aux_ack) return;
    if (s_aux_cancel) { aux_finish(-1); return; }

    /* Everything converted from here is the other channel. */
    s_aux_edge = s_blocks;
    adc_gpio_init(s_aux_gpio);
    adc_select_input(s_aux_chan);
    s_aux_phase  = AUX_IN;
    s_aux_result = -1;
}

bool adc_ring_aux_step(const int16_t *blk, size_t n, uint32_t seq, bool *reinit)
{
    *reinit = false;

    switch (s_aux_phase) {
    case AUX_IN:
        if (seq < s_aux_edge) return false;        /* finished before the swap */
        if (seq == s_aux_edge) return true;        /* the mixed one */
        {
            int64_t sum = 0;
            size_t i, from = n > AUX_SETTLE_SAMPLES ? AUX_SETTLE_SAMPLES : 0;
            for (i = from; i < n; i++) sum += blk[i];
            if (n > from && !s_aux_cancel)
                s_aux_result = (int32_t)(sum / (int64_t)(n - from)) + ADC_MIDPOINT;
        }
        /* Back on the pad. The pin is left in analogue mode: the CYW43
         * driver reclaims it as its SPI clock at the head of its next
         * transfer (power.c). */
        adc_select_input(ADC_INPUT);
        s_aux_edge  = s_blocks;
        s_aux_phase = AUX_OUT;
        return true;

    case AUX_OUT:
        if (seq < s_aux_edge) return true;         /* still the other channel */
        *reinit = true;
        aux_finish(s_aux_cancel ? -1 : s_aux_result);
        return seq == s_aux_edge;                  /* the mixed one; a later one is clean */

    case AUX_IDLE:
    default:
        return false;
    }
}
