/*
 * Handoff — carrier generation, OOK gating and self-measurement. M3.
 * See pio_carrier.h for the contract and pio_carrier.pio for the two programs.
 *
 * The pad is GP2 (design §10.1). Two state machines share it: sm_out drives it
 * one bit per cycle, sm_cnt counts rising edges on it. That is the whole of
 * M3's instrumentation — no jumper, no scope, no second board.
 */
#include "pio_carrier.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "config.h"
#include "pio_carrier.pio.h"

#define CARRIER_PIN 2

/*
 * One bit per state-machine cycle and two cycles per carrier period, so a chip
 * costs 2 * (carrier_hz / chip_rate) bits: 100 at 200 kHz, 20 at 40 kHz.
 *
 * The buffer is the transmit bound. 2048 words is 65 536 bits, which is 655
 * chips at 200 kHz — comfortably more than the ~600 a full fragment frame
 * needs (§8.3), and the send below refuses rather than truncates if a caller
 * ever exceeds it. M5 is where a streaming double-buffer would earn its
 * complexity; nothing before it sends more than one frame at a time.
 */
#define CARRIER_TX_WORDS 2048u

/* Aligned to its own size so the DMA read-address ring wraps on it. */
static uint32_t s_mark[8] __attribute__((aligned(32)));
static uint32_t s_tx[CARRIER_TX_WORDS];

static PIO      s_pio    = pio0;
static uint     s_sm_out = 0;
static uint     s_sm_cnt = 1;
static uint     s_off_out;
static uint     s_off_cnt;
static int      s_dma    = -1;
static uint32_t s_hz;
static uint32_t s_bits_per_chip;
static bool     s_driving;
static bool     s_loaded;

static void tx_abort(void);

/* ---------------------------------------------------------------------- */

void pio_carrier_init(uint32_t carrier_hz)
{
    pio_sm_config c;
    float div;
    size_t i;

    s_hz = carrier_hz;

    /* Two state-machine cycles per carrier period. Both 40 kHz and 200 kHz
     * divide 150 MHz exactly (design §10.1), which config.h static-asserts. */
    div = (float)clock_get_hz(clk_sys) / (float)(2u * carrier_hz);

    s_bits_per_chip = 2u * (carrier_hz / (uint32_t)HANDOFF_CHIP_RATE_HZ);

    for (i = 0; i < count_of(s_mark); i++)
        s_mark[i] = 0xAAAAAAAAu;      /* alternating: a continuous carrier */

    /* Re-initialising at a different carrier is how M3 walks 40 kHz and
     * 200 kHz in one run; the programs are loaded once or the instruction
     * memory would fill on the second call. */
    if (!s_loaded) {
        s_off_out = pio_add_program(s_pio, &carrier_out_program);
        s_off_cnt = pio_add_program(s_pio, &edge_count_program);
        s_loaded  = true;
    } else {
        tx_abort();
    }

    /* ---- generator ---- */
    pio_gpio_init(s_pio, CARRIER_PIN);
    c = carrier_out_program_get_default_config(s_off_out);
    sm_config_set_out_pins(&c, CARRIER_PIN, 1);
    /* Shift right, autopull at 32: one pad bit per cycle, refilled for free. */
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, div);
    pio_sm_init(s_pio, s_sm_out, s_off_out, &c);

    /* ---- counter ---- */
    c = edge_count_program_get_default_config(s_off_cnt);
    sm_config_set_in_pins(&c, CARRIER_PIN);
    sm_config_set_clkdiv(&c, 1.0f);     /* full speed; 375 cycles per half-period */
    pio_sm_init(s_pio, s_sm_cnt, s_off_cnt, &c);

    if (s_dma < 0) s_dma = (int)dma_claim_unused_channel(true);

    pio_carrier_drive(true);
    pio_sm_set_enabled(s_pio, s_sm_out, true);
}

/* ---------------------------------------------------------------------- */

/*
 * design §6.3: the pad must be genuinely high-Z while receiving, not driven
 * low — a driven pad still loads the electrode the far end is listening on.
 * Clearing the PIO pin direction is what does that; the pulls go with it,
 * because a pull is a load too, and because txgen tests the high-Z claim by
 * seeing whether an internal pull can move the pad at all.
 */
void pio_carrier_drive(bool on)
{
    s_driving = on;
    pio_sm_set_consecutive_pindirs(s_pio, s_sm_out, CARRIER_PIN, 1, on);

    if (!on) {
        gpio_disable_pulls(CARRIER_PIN);
        /*
         * RP2350-E9, measured at M3 rather than assumed: once a high-impedance
         * bank-0 pad has been taken high, the internal pull-down cannot bring
         * it back down -- it latches, and a latched pad is driving the
         * electrode that §6.3 requires it to stop loading. Development plan §6
         * guessed this would be harmless through R1's 1 MOhm; it is not, since
         * a pull-down far stronger than 1 MOhm does not clear it either.
         *
         * Toggling the pad's input buffer does clear it, which txgen confirms
         * both ways. It costs two register writes on the way into receive.
         */
        gpio_set_input_enabled(CARRIER_PIN, false);
        gpio_set_input_enabled(CARRIER_PIN, true);
    }
}

/* ---------------------------------------------------------------------- */

static void tx_abort(void)
{
    if (s_dma >= 0 && dma_channel_is_busy((uint)s_dma))
        dma_channel_abort((uint)s_dma);
    pio_sm_set_enabled(s_pio, s_sm_out, false);
    pio_sm_clear_fifos(s_pio, s_sm_out);
    pio_sm_restart(s_pio, s_sm_out);
    pio_sm_exec(s_pio, s_sm_out, pio_encode_jmp(s_off_out));
    pio_sm_set_enabled(s_pio, s_sm_out, true);
}

static void tx_start(const uint32_t *src, uint32_t words, bool loop)
{
    dma_channel_config c = dma_channel_get_default_config((uint)s_dma);

    tx_abort();

    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(s_pio, s_sm_out, true));
    if (loop) channel_config_set_ring(&c, false, 5);   /* 32 bytes = 8 words */

    dma_channel_configure((uint)s_dma, &c, &s_pio->txf[s_sm_out], src,
                          loop ? 0xFFFFFFFFu : words, true);
}

void pio_carrier_mark_continuous(bool on)
{
    if (on) tx_start(s_mark, count_of(s_mark), true);
    else    tx_abort();
}

void pio_carrier_send(const uint8_t *chips, size_t n)
{
    uint32_t bits = (uint32_t)n * s_bits_per_chip;
    uint32_t words = (bits + 31u) / 32u;
    uint32_t b = 0;
    size_t i;

    if (n == 0 || words > CARRIER_TX_WORDS) return;

    memset(s_tx, 0, (size_t)words * sizeof s_tx[0]);

    /*
     * A mark is the carrier: alternate every cycle. A space is silence: leave
     * the zeros. Bit b of the stream is bit (b % 32) of word (b / 32), LSB
     * first, because the OSR shifts right.
     */
    for (i = 0; i < n; i++) {
        uint32_t k;
        if (chips[i]) {
            for (k = 1; k < s_bits_per_chip; k += 2)
                s_tx[(b + k) >> 5] |= 1u << ((b + k) & 31u);
        }
        b += s_bits_per_chip;
    }

    tx_start(s_tx, words, false);
}

bool pio_carrier_busy(void)
{
    if (s_dma >= 0 && dma_channel_is_busy((uint)s_dma)) return true;
    return !pio_sm_is_tx_fifo_empty(s_pio, s_sm_out);
}

/* ---------------------------------------------------------------------- */

void pio_carrier_count_begin(void)
{
    pio_sm_set_enabled(s_pio, s_sm_cnt, false);
    pio_sm_clear_fifos(s_pio, s_sm_cnt);
    pio_sm_restart(s_pio, s_sm_cnt);
    pio_sm_exec(s_pio, s_sm_cnt, pio_encode_jmp(s_off_cnt));
    /* set only reaches 31, so the all-ones preload is a mov of an inverted
     * null. Edges are counted downward from it. */
    pio_sm_exec(s_pio, s_sm_cnt, pio_encode_mov_not(pio_x, pio_null));

    pio_sm_set_enabled(s_pio, s_sm_cnt, true);
}

uint32_t pio_carrier_count_end(void)
{
    uint32_t x;

    pio_sm_set_enabled(s_pio, s_sm_cnt, false);
    pio_sm_exec(s_pio, s_sm_cnt, pio_encode_mov(pio_isr, pio_x));
    pio_sm_exec(s_pio, s_sm_cnt, pio_encode_push(false, false));
    x = pio_sm_get(s_pio, s_sm_cnt);

    return 0xFFFFFFFFu - x;
}

/*
 * A gate the CPU holds open. busy_wait_us is good to about a microsecond, so a
 * gate of 200 ms carries well under the 0.1 % the exit criterion allows.
 */
uint32_t pio_carrier_measure_hz(uint32_t gate_us)
{
    uint32_t edges;

    if (gate_us == 0) return 0;

    pio_carrier_count_begin();
    busy_wait_us(gate_us);
    edges = pio_carrier_count_end();

    return (uint32_t)(((uint64_t)edges * 1000000u + gate_us / 2u) / gate_us);
}

/* ---------------------------------------------------------------------- */

uint32_t pio_carrier_bits_per_chip(void) { return s_bits_per_chip; }
bool     pio_carrier_is_driving(void)    { return s_driving; }
