/*
 * Handoff — carrier generation, OOK gating and self-measurement. M3, M13.
 * See pio_carrier.h for the contract and pio_carrier.pio for the two programs.
 *
 * The pad is GP2 (design §10.1). Two state machines share it: sm_out drives it
 * one slot per half-period, sm_cnt counts rising edges on it. That is the whole
 * of M3's instrumentation — no jumper, no scope, no second board.
 *
 * Since M13 the stream carries the pad direction as well as its level (§9.8):
 * a space chip releases the pad rather than driving it low. The CPU never
 * touches the running state machine's pin direction; the data does.
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

/* carrier_out is out-pins then out-pindirs with a one-cycle delay: three cycles a slot. */
_Static_assert(HANDOFF_PIO_SLOT_CYCLES == 3, "pio_carrier.pio is written for 3-cycle slots");

/*
 * Two bits per slot and two slots per carrier period, so a chip costs
 * 4 * (carrier_hz / chip_rate) bits: 200 at 200 kHz, 40 at 40 kHz.
 *
 * The buffer is the transmit bound. 4096 words is 131 072 bits, which is 655
 * chips at 200 kHz — comfortably more than the 624 a fragment frame needs
 * (§8.3) — and the send below refuses rather than truncates if a caller ever
 * exceeds it. One word is always kept for the released tail, see send().
 */
#define CARRIER_TX_WORDS 4096u

/* A slot pair per nibble, LSB first: level 1 driven, then level 0 driven. */
#define MARK_WORD 0xBBBBBBBBu

/* Aligned to its own size so the DMA read-address ring wraps on it. */
static uint32_t s_mark[8] __attribute__((aligned(32)));
static uint32_t s_tx[CARRIER_TX_WORDS];

/*
 * M7's tone: a repeating pattern of up to PIO_CARRIER_TONE_SLOTS slots, looped
 * the same way as s_mark. 1024 slots is 2048 bits is 256 bytes, so the ring
 * is 8 address bits and the buffer is aligned to that.
 */
static uint32_t s_tone[PIO_CARRIER_TONE_SLOTS * PIO_CARRIER_SLOT_BITS / 32]
    __attribute__((aligned(256)));

static PIO      s_pio    = pio0;
static uint     s_sm_out = 0;
static uint     s_sm_cnt = 1;
static uint     s_off_out;
static uint     s_off_cnt;
static int      s_dma    = -1;
static uint32_t s_hz;
static uint32_t s_bits_per_chip;
static bool     s_driving;
static bool     s_sense = true;
static bool     s_loaded;
static uint64_t s_started_us;

static void tx_abort(void);

/* ---------------------------------------------------------------------- */

void pio_carrier_init(uint32_t carrier_hz)
{
    pio_sm_config c;
    uint32_t div;
    size_t i;

    s_hz = carrier_hz;

    /* Three state-machine cycles per half-period slot. Both 40 kHz and
     * 200 kHz divide 150 MHz / 6 exactly (design §10.1), which config.h
     * static-asserts; the divider is an integer with no fractional jitter. */
    div = clock_get_hz(clk_sys) / (PIO_CARRIER_SLOT_CYCLES * 2u * carrier_hz);

    s_bits_per_chip = PIO_CARRIER_SLOT_BITS * 2u
                    * (carrier_hz / (uint32_t)HANDOFF_CHIP_RATE_HZ);

    for (i = 0; i < count_of(s_mark); i++)
        s_mark[i] = MARK_WORD;         /* driven square: a continuous carrier */

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
    gpio_set_dir(CARRIER_PIN, GPIO_IN);       /* SIO side: input, so SIO = high-Z */
    gpio_set_input_enabled(CARRIER_PIN, s_sense);
    c = carrier_out_program_get_default_config(s_off_out);
    sm_config_set_out_pins(&c, CARRIER_PIN, 1);
    sm_config_set_set_pins(&c, CARRIER_PIN, 1);
    /* Shift right, autopull at 32: one slot per two bits, refilled for free. */
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv_int_frac(&c, (uint16_t)div, 0);
    pio_sm_init(s_pio, s_sm_out, s_off_out, &c);
    /* The pad starts released and stays that way until a mark slot says
     * otherwise. Set while the state machine is stopped: the running one is
     * never touched from here (the M5 stall suspect, since retired). */
    pio_sm_set_pins_with_mask(s_pio, s_sm_out, 0, 1u << CARRIER_PIN);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm_out, CARRIER_PIN, 1, false);

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
 * The pulls go too, because a pull is a load, and because txgen tests the
 * high-Z claim by seeing whether an internal pull can move the pad at all.
 *
 * The pad's output enable is switched by its FUNCTION SELECT, never by poking
 * the running state machine: switching the pad to SIO (whose direction for
 * GP2 is input, the reset state) makes it high-Z, and switching it back to
 * PIO0 hands it to the generator again, whose own pin direction is released
 * between sends and driven only inside mark slots. The alternative,
 * pio_sm_set_consecutive_pindirs on the RUNNING machine, force-executes an
 * instruction and restores PINCTRL a few cycles later; it was M5's first
 * suspect for the one transmit in ~2 700 that stayed busy forever. The PIO
 * still reads the pad for the edge counter whichever function owns it.
 */
void pio_carrier_drive(bool on)
{
    s_driving = on;
    gpio_set_function(CARRIER_PIN, on ? GPIO_FUNC_PIO0 : GPIO_FUNC_SIO);

    if (!on) {
        gpio_disable_pulls(CARRIER_PIN);
        gpio_set_input_enabled(CARRIER_PIN, false);
        /*
         * RP2350-E9, measured at M3 rather than assumed: once a high-impedance
         * bank-0 pad with its input buffer enabled has been taken high, the
         * internal pull-down cannot bring it back down -- it latches, and a
         * latched pad is driving the electrode that §6.3 requires it to stop
         * loading. With the buffer off (the link's state, see sense()) the
         * leakage path is gone and the latch cannot form, inside a frame or
         * after it; that is the shipped mitigation, and it costs nothing.
         *
         * With the buffer on (the instruments) a latch that already exists
         * has to be discharged while the buffer is off, and M13's bare-pad
         * run showed that a bare toggle does not do it: nothing pulls the
         * pad down in the gap, so it floats at ~2.2 V and re-latches when
         * the buffer comes back. The pull-down does it in microseconds --
         * the M3 test that passed with an instant toggle had the pad driven
         * low at the time. Only instruments come through here with the
         * buffer on, so the brief pull is not a load on any link.
         */
        if (s_sense) {
            gpio_pull_down(CARRIER_PIN);
            busy_wait_us(20);
            gpio_disable_pulls(CARRIER_PIN);
            gpio_set_input_enabled(CARRIER_PIN, true);
        }
    }
}

void pio_carrier_sense(bool on)
{
    s_sense = on;
    gpio_set_input_enabled(CARRIER_PIN, on);
}

/* ---------------------------------------------------------------------- */

/*
 * Stop whatever is being sent and leave the pad released. The state machine
 * is halted first, so the pin-direction write below lands on a stopped
 * machine and the pad goes high-impedance in the same instant -- which is
 * the release M13 times a shout's settling from.
 */
static void tx_abort(void)
{
    if (s_dma >= 0 && dma_channel_is_busy((uint)s_dma))
        dma_channel_abort((uint)s_dma);
    pio_sm_set_enabled(s_pio, s_sm_out, false);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm_out, CARRIER_PIN, 1, false);
    pio_sm_set_pins_with_mask(s_pio, s_sm_out, 0, 1u << CARRIER_PIN);
    pio_sm_clear_fifos(s_pio, s_sm_out);
    pio_sm_restart(s_pio, s_sm_out);
    pio_sm_exec(s_pio, s_sm_out, pio_encode_jmp(s_off_out));
    pio_sm_set_enabled(s_pio, s_sm_out, true);
}

/* ring_bits > 0 loops src forever over a 2^ring_bits-byte ring, which src
 * must be aligned to; 0 sends words once. */
static void tx_start(const uint32_t *src, uint32_t words, uint ring_bits)
{
    dma_channel_config c = dma_channel_get_default_config((uint)s_dma);

    tx_abort();

    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(s_pio, s_sm_out, true));
    if (ring_bits) channel_config_set_ring(&c, false, ring_bits);

    dma_channel_configure((uint)s_dma, &c, &s_pio->txf[s_sm_out], src,
                          ring_bits ? 0xFFFFFFFFu : words, true);
    /* The first word is in the FIFO within a bus cycle and the machine,
     * stalled on autopull, takes it at its next tick: under a slot from now. */
    s_started_us = time_us_64();
}

void pio_carrier_mark_continuous(bool on)
{
    if (on) tx_start(s_mark, count_of(s_mark), 5);   /* 32 bytes = 8 words */
    else    tx_abort();
}

void pio_carrier_hold(int level)
{
    tx_abort();
    if (level < 0) return;
    pio_sm_set_enabled(s_pio, s_sm_out, false);
    pio_sm_set_pins_with_mask(s_pio, s_sm_out, level ? 1u << CARRIER_PIN : 0,
                              1u << CARRIER_PIN);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm_out, CARRIER_PIN, 1, true);
    pio_sm_set_enabled(s_pio, s_sm_out, true);   /* stalls on an empty FIFO */
}

bool pio_carrier_tone(uint32_t sm_div, uint32_t period_slots, uint32_t high_slots)
{
    uint32_t slots = PIO_CARRIER_TONE_SLOTS;
    uint32_t s;

    if (sm_div < 1u || sm_div > 65535u) return false;
    if (period_slots < 2u || period_slots > slots) return false;
    if (period_slots & (period_slots - 1u)) return false;   /* must tile the ring */
    if (high_slots < 1u || high_slots >= period_slots) return false;

    /* Slot s is bits 2s (level) and 2s+1 (driven) of the stream, LSB first,
     * because the OSR shifts right -- the same layout send() uses. Every
     * slot of a tone is driven; the high slots lead each period so a rising
     * edge starts it. */
    memset(s_tone, 0, sizeof s_tone);
    for (s = 0; s < slots; s++) {
        uint32_t b = s * PIO_CARRIER_SLOT_BITS;
        if ((s % period_slots) < high_slots) s_tone[b >> 5] |= 1u << (b & 31u);
        s_tone[(b + 1u) >> 5] |= 1u << ((b + 1u) & 31u);
    }

    tx_abort();
    pio_sm_set_clkdiv_int_frac(s_pio, s_sm_out, (uint16_t)sm_div, 0);
    pio_sm_clkdiv_restart(s_pio, s_sm_out);
    tx_start(s_tone, count_of(s_tone), 8);          /* 256 bytes = 64 words */
    return true;
}

void pio_carrier_send(const uint8_t *chips, size_t n)
{
    uint32_t bits  = (uint32_t)n * s_bits_per_chip;
    uint32_t words = (bits + 31u) / 32u + 1u;   /* + one released word */
    uint32_t slots_per_chip = s_bits_per_chip / PIO_CARRIER_SLOT_BITS;
    uint32_t b = 0;
    size_t i;

    if (n == 0 || words > CARRIER_TX_WORDS) return;

    memset(s_tx, 0, (size_t)words * sizeof s_tx[0]);

    /*
     * A mark is the carrier: level alternating, driven every slot. A space
     * is a released pad: leave the zeros, level and direction both. Bit b of
     * the stream is bit (b % 32) of word (b / 32), LSB first, because the
     * OSR shifts right. A mark starts high and ends low, so the pad is
     * always released from 0 V; on a bare pad with the input buffer on that
     * is the one release that cannot latch (txgen's E9 finding).
     *
     * The trailing word is all released slots. 624 chips at 200 kHz is
     * exactly 3 900 words, so without it the stalled machine would hold the
     * last mark's driven-low slot for as long as the CPU took to notice --
     * and on the product that is the 0.28 V bias step §9.8 exists to avoid.
     */
    for (i = 0; i < n; i++) {
        if (chips[i]) {
            uint32_t j;
            for (j = 0; j < slots_per_chip; j++) {
                uint32_t k = b + j * PIO_CARRIER_SLOT_BITS;
                if (!(j & 1u)) s_tx[k >> 5] |= 1u << (k & 31u);        /* level */
                s_tx[(k + 1u) >> 5] |= 1u << ((k + 1u) & 31u);         /* driven */
            }
        }
        b += s_bits_per_chip;
    }

    tx_start(s_tx, words, 0);
}

bool pio_carrier_busy(void)
{
    if (s_dma >= 0 && dma_channel_is_busy((uint)s_dma)) return true;
    return !pio_sm_is_tx_fifo_empty(s_pio, s_sm_out);
}

uint64_t pio_carrier_started_us(void) { return s_started_us; }

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

void pio_carrier_state(pio_carrier_state_t *st)
{
    dma_channel_hw_t *ch = dma_channel_hw_addr((uint)s_dma);

    st->dma_busy      = s_dma >= 0 && dma_channel_is_busy((uint)s_dma);
    st->dma_remaining = ch->transfer_count;
    st->dma_ctrl      = ch->ctrl_trig;
    st->fifo_level    = (uint8_t)pio_sm_get_tx_fifo_level(s_pio, s_sm_out);
    st->pc            = pio_sm_get_pc(s_pio, s_sm_out);
    st->sm_enabled    = (s_pio->ctrl & (1u << s_sm_out)) != 0;
    st->exec_stalled  = pio_sm_is_exec_stalled(s_pio, s_sm_out);
}

void pio_carrier_reset(void) { tx_abort(); }

uint32_t pio_carrier_bits_per_chip(void) { return s_bits_per_chip; }
size_t   pio_carrier_max_chips(void)
{
    return s_bits_per_chip ? ((CARRIER_TX_WORDS - 1u) * 32u) / s_bits_per_chip : 0;
}
bool     pio_carrier_is_driving(void)    { return s_driving; }
