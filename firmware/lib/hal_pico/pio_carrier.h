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

/* Whether the pad is currently driven, for the §6.3 high-Z check. */
bool pio_carrier_is_driving(void);

#endif /* HANDOFF_PIO_CARRIER_H */
