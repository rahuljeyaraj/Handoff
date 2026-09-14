/*
 * Handoff — PIO carrier generation and OOK gating. design §10.1, §6.3.
 *
 * STUB UNTIL M3.
 *
 *   200 kHz -> 150 MHz / 750     both exact integer divisions, so only the
 *    40 kHz -> 150 MHz / 3750    divider changes between bring-up and operation
 *
 * Two state machines. One drives GP2. The second reads the pin state and counts
 * edges over a gate interval, which is how M3 measures the carrier with no
 * external hardware at all: the PIO input mux is independent of whichever
 * peripheral drives the pad, so no jumper is needed.
 */
#ifndef HANDOFF_PIO_CARRIER_H
#define HANDOFF_PIO_CARRIER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void pio_carrier_init(uint32_t carrier_hz);

/* Clock out chips, one byte per chip. The carrier is gated on for a mark. */
void pio_carrier_send(const uint8_t *chips, size_t n);
bool pio_carrier_busy(void);

/*
 * design §6.3: GP2 must be high-Z while receiving, not merely driven low — a
 * driven pad still loads the shared electrode the far end is listening on.
 *
 * Development plan §6 flags RP2350-E9 right here: the erratum affects high-Z
 * bank-0 pads, and through R1's 1 MOhm it should be harmless. M3 confirms that
 * by measurement. It is not assumed.
 */
void pio_carrier_drive(bool on);

/*
 * Measured carrier in Hz, from the second state machine. M3 exit criterion is
 * within 0.1 % of nominal at both 40 kHz and 200 kHz.
 */
uint32_t pio_carrier_measure_hz(uint32_t gate_us);

/*
 * Drive an unbroken carrier until told to stop.
 *
 * Measurement needs a carrier that outlasts the gate, and pio_carrier_send()
 * is finite by construction. This is a small looping DMA over an alternating
 * pattern, so it costs 32 bytes rather than a buffer proportional to the gate.
 * It is M3 instrumentation: nothing in the link uses it.
 */
void pio_carrier_mark_continuous(bool on);

/*
 * M7 instrumentation: a repeating pattern instead of the carrier. The
 * generator runs at an EXACT integer divider (no fractional jitter, and a
 * frequency the sweep can state as a clock ratio rather than a rounded Hz),
 * and each period of period_bits bits has its first high_bits bits high:
 *
 *   f = clk_sys / (sm_div * period_bits)
 *   fundamental amplitude = (2 V / pi) * sin(pi * high_bits / period_bits)
 *
 * so period 2, high 1 is the plain carrier at any divider up to 65535, and
 * period 32 with high_bits stepped 1..16 is an amplitude sweep with nothing
 * on the bench changing -- the clipping point is found from the console.
 * period_bits must be a power of two, at most PIO_CARRIER_TONE_BITS, so the
 * pattern tiles the DMA ring. Runs until mark_continuous(false) or the next
 * send(); pio_carrier_init() restores the link's divider.
 */
#define PIO_CARRIER_TONE_BITS 2048u
bool pio_carrier_tone(uint32_t sm_div, uint32_t period_bits, uint32_t high_bits);

/*
 * Open and close a gate on the edge counter.
 *
 * The alignment criterion needs an exact edge count over a burst rather than a
 * rate over a fixed window, and the pad emits nothing while idle — so the gate
 * may be opened early and closed late without changing the count.
 */
void     pio_carrier_count_begin(void);
uint32_t pio_carrier_count_end(void);

/* Pad bits per chip — 2 * (carrier / chip rate). What txgen counts against. */
uint32_t pio_carrier_bits_per_chip(void);

/* The most chips one send() will take at the current carrier; a longer send
 * is refused rather than truncated. 655 at 200 kHz, 3276 at 40 kHz. */
size_t   pio_carrier_max_chips(void);

/* Whether the pad is currently driven, for the §6.3 high-Z check. */
bool pio_carrier_is_driving(void);

/*
 * Transmit-path state, for a stall diagnostic. One transmit in ~1700 at M5
 * stayed busy forever; this is what hal_pico prints when that happens, and
 * pio_carrier_reset() is how it recovers: abort the DMA, restart the state
 * machine at its first instruction, FIFOs cleared.
 */
typedef struct {
    bool     dma_busy;
    uint32_t dma_remaining;   /* transfers left, TRANS_COUNT               */
    uint32_t dma_ctrl;
    uint8_t  fifo_level;
    uint8_t  pc;
    bool     sm_enabled;
    bool     exec_stalled;
} pio_carrier_state_t;

void pio_carrier_state(pio_carrier_state_t *st);
void pio_carrier_reset(void);

#endif /* HANDOFF_PIO_CARRIER_H */
