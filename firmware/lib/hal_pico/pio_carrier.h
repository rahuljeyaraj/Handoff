/*
 * Handoff — PIO carrier generation and OOK gating. design §10.1, §6.3, §9.8.
 *
 *   200 kHz -> 150 MHz / 125     three cycles per half-period, both exact
 *    40 kHz -> 150 MHz / 625     integer divisions, so only the divider
 *                                changes between bring-up and operation
 *
 * Two state machines. One drives GP2 from a bit stream that carries the pad
 * LEVEL and the pad DIRECTION for every half-period, so a mark is a driven
 * square and a space is a released pad (§9.8) — the generator, not the CPU,
 * decides slot by slot whether GP2 is driving at all. The second reads the
 * pin state and counts edges over a gate interval, which is how M3 measures
 * the carrier with no external hardware: the PIO input mux is independent of
 * whichever peripheral drives the pad, so no jumper is needed.
 */
#ifndef HANDOFF_PIO_CARRIER_H
#define HANDOFF_PIO_CARRIER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

/* State-machine cycles per half-period slot, and stream bits per slot. */
#define PIO_CARRIER_SLOT_CYCLES ((uint32_t)HANDOFF_PIO_SLOT_CYCLES)
#define PIO_CARRIER_SLOT_BITS   2u

void pio_carrier_init(uint32_t carrier_hz);

/*
 * Clock out chips, one byte per chip. A mark is the carrier, driven; a
 * space is the pad released. The send always ends in a released slot, so
 * when the stream runs out the pad is high-impedance, never parked low.
 */
void pio_carrier_send(const uint8_t *chips, size_t n);
bool pio_carrier_busy(void);

/* When the last send's DMA started, from time_us_64(). The pad's last chip
 * ends at started + n * HANDOFF_CHIP_US, within a slot; M13 measures its
 * settling from there rather than from the CPU noticing busy() clear. */
uint64_t pio_carrier_started_us(void);

/*
 * design §6.3: GP2 must be high-Z while receiving, not merely driven low — a
 * driven pad still loads the shared electrode the far end is listening on.
 *
 * This hands the pad to the generator (true) or to SIO as an input (false).
 * Inside a send the generator releases the pad itself for every space, so
 * this is the frame-level switch and the space-level one is in the stream.
 */
void pio_carrier_drive(bool on);

/*
 * The pad's INPUT BUFFER. RP2350-E9 (development plan §6, measured at M3):
 * a released bank-0 pad with its input buffer enabled latches at ~2.2 V and
 * an internal pull cannot bring it back. With the buffer disabled the
 * leakage path does not exist and there is nothing to latch. The link never
 * reads GP2, so hal_pico turns the buffer off and every release inside a
 * frame is clean; the instruments (txgen's edge counter, afe_sweep's
 * frequency check) turn it on for a measurement on a bare pad, where a pad
 * released from a driven low stays at 0 V. Default after init: on, because
 * the instruments predate this and expect it.
 */
void pio_carrier_sense(bool on);

/*
 * Measured carrier in Hz, from the second state machine. M3 exit criterion is
 * within 0.1 % of nominal at both 40 kHz and 200 kHz. Needs sense(true).
 */
uint32_t pio_carrier_measure_hz(uint32_t gate_us);

/*
 * Drive an unbroken carrier until told to stop.
 *
 * Measurement needs a carrier that outlasts the gate, and pio_carrier_send()
 * is finite by construction. This is a small looping DMA over an alternating
 * pattern, so it costs 32 bytes rather than a buffer proportional to the gate.
 * Stopping releases the pad in the same instant (the state machine is halted
 * and its pin direction cleared), which is what M13's shout relies on.
 */
void pio_carrier_mark_continuous(bool on);

/*
 * Instrument: park the pad DRIVEN at a level (0 or 1) until the next send,
 * tone or continuous mark, or release it now (-1). The link has no use for
 * a parked pad — spaces are released — but txgen's "genuinely driven when
 * told to be" check needs one, and loopback's `m` measures against one.
 */
void pio_carrier_hold(int level);

/*
 * M7 instrumentation: a repeating pattern instead of the carrier. The
 * generator runs at an EXACT integer divider (no fractional jitter, and a
 * frequency the sweep can state as a clock ratio rather than a rounded Hz),
 * and each period of period_slots slots has its first high_slots slots high,
 * every slot driven:
 *
 *   f = clk_sys / (PIO_CARRIER_SLOT_CYCLES * sm_div * period_slots)
 *   fundamental amplitude = (2 V / pi) * sin(pi * high_slots / period_slots)
 *
 * so period 2, high 1 is the plain carrier at any divider up to 65535, and
 * period 32 with high_slots stepped 1..16 is an amplitude sweep with nothing
 * on the bench changing -- the clipping point is found from the console.
 * period_slots must be a power of two, at most PIO_CARRIER_TONE_SLOTS, so the
 * pattern tiles the DMA ring. Runs until mark_continuous(false) or the next
 * send(); pio_carrier_init() restores the link's divider.
 */
#define PIO_CARRIER_TONE_SLOTS 1024u
bool pio_carrier_tone(uint32_t sm_div, uint32_t period_slots, uint32_t high_slots);

/*
 * Open and close a gate on the edge counter.
 *
 * The alignment criterion needs an exact edge count over a burst rather than a
 * rate over a fixed window, and the pad emits nothing while idle — so the gate
 * may be opened early and closed late without changing the count.
 */
void     pio_carrier_count_begin(void);
uint32_t pio_carrier_count_end(void);

/* Stream bits per chip — 2 * 2 * (carrier / chip rate). What txgen counts
 * against, and what sizes the send buffer. */
uint32_t pio_carrier_bits_per_chip(void);

/* The most chips one send() will take at the current carrier; a longer send
 * is refused rather than truncated. 655 at 200 kHz, 3276 at 40 kHz. */
size_t   pio_carrier_max_chips(void);

/* Whether the pad is currently the generator's, for the §6.3 high-Z check. */
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
