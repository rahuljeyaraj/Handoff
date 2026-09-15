/*
 * Handoff — the RP2350 binding of hal.h. architecture §4, §12.
 *
 * M5. This is the file that first ties the whole HAL together, and the
 * development plan is deliberate about not doing that until the pieces it
 * binds have each been proven alone:
 *
 *   M3  pio_carrier.c   carrier generation, self-measured
 *   M4  adc_ring.c      500 ksps into a DMA ring, and ipc.c across the cores
 *   M5  hal_pico.c      the two of them, wired to lib/
 *
 * Everything above lib/hal is already tested against test/host's simulator, so
 * what M5 adds is only the binding — which is the entire point of the seam.
 */
#ifndef HANDOFF_HAL_PICO_H
#define HANDOFF_HAL_PICO_H

#include "hal.h"
#include "pio_carrier.h"

/*
 * Pins, from design §6 and §10.2. Collected here rather than scattered through
 * the drivers, because the netlist in design §7 is the authority and one file
 * should be diffable against it.
 */
#define HANDOFF_PIN_TX      2    /* GP2 -> R1 -> pad,  design §6.3  */
#define HANDOFF_PIN_ADC     26   /* GP26 = ADC0,       design §10.2 */
#define HANDOFF_ADC_CHANNEL 0

/*
 * Fills in the interface and starts core 1. Idempotent: a second call returns
 * the same interface and starts nothing.
 *
 * Boot order matters and is fixed here: the ring is started, the bare-ADC
 * noise floor is taken with nothing else consuming it (M4's reference figure,
 * see hal_pico_noise_floor), the ring is restarted from zero, the carrier is
 * brought up and released to high-Z, and only then is core 1 launched.
 */
const hal_iface_t *hal_pico_init(void);

/* Core-1 busy time as a percentage of wall time since core 1 started.
 * Headroom is 100 minus this. M4 exit criterion. */
uint8_t hal_pico_core1_load(void);

/*
 * DMA blocks dropped since boot. M4 requires this to stay at zero for ten
 * minutes, and it is the number that closes design §17's second open item —
 * or sends the analogue mixer back into the design.
 */
uint32_t hal_pico_overruns(void);

/* Bare-ADC noise floor in tenths of an LSB RMS, taken at init on the on-die
 * temperature sensor. *mean_code gets the raw 12-bit mean. This is M4's
 * reference figure and the one M5's leakage criterion is compared against. */
uint32_t hal_pico_noise_floor(int32_t *mean_code);

/*
 * Bring-up only: move both ends of the link to another carrier at run time.
 * The PIO divider changes on core 0 and core 1 re-tunes its Goertzel bin, so
 * one image can walk 40 kHz and 200 kHz the way txgen does. Returns false and
 * changes nothing if hz is not an exact PIO divider on a Goertzel bin centre.
 * Blocks until core 1 has re-tuned; chips in flight across the change are
 * meaningless and the caller should reset its frame receiver.
 */
bool hal_pico_set_carrier(uint32_t hz);
uint32_t hal_pico_carrier_hz(void);

/* Transmits that stayed busy past their airtime and were reset, with the
 * hardware state captured at the last one. Should be zero; see hal_pico.c. */
uint32_t hal_pico_tx_stalls(pio_carrier_state_t *last);

/* Chips produced by core 1 since start, and Goertzel windows scored. */
uint32_t hal_pico_chips(void);
uint32_t hal_pico_windows(void);

/*
 * M13 instrumentation. The chip stream with each chip's place on the ADC
 * sample clock (the number of its last sample), and that clock in
 * time_us_64() terms, so a bench app can say which chips were sampled
 * inside a window rather than which arrived during it -- the ring's latency
 * is up to a DMA block, 4 ms, which is four turnaround budgets. Same ring
 * as hal_rx_chips(); pop from one or the other.
 */
size_t   hal_pico_rx_chips_at(uint16_t *dst, uint32_t *idx, size_t max);
uint64_t hal_pico_sample_us(uint64_t idx);

/* When the last send's final chip ended on the pad (the DMA start plus the
 * airtime), which is when the generator released it. */
uint64_t hal_pico_tx_pad_idle_us(void);

#endif /* HANDOFF_HAL_PICO_H */
