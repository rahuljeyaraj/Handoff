/*
 * Handoff — the RP2350 binding of hal.h. architecture §4, §12.
 *
 * STUB UNTIL M5. This is the file that first ties the whole HAL together, and
 * the development plan is deliberate about not doing that until the pieces it
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

/*
 * Pins, from design §6 and §10.2. Collected here rather than scattered through
 * the drivers, because the netlist in design §7 is the authority and one file
 * should be diffable against it.
 */
#define HANDOFF_PIN_TX      2    /* GP2 -> R1 -> pad,  design §6.3  */
#define HANDOFF_PIN_ADC     26   /* GP26 = ADC0,       design §10.2 */
#define HANDOFF_ADC_CHANNEL 0

/* Fills in the interface and starts core 1. Returns NULL until M5. */
const hal_iface_t *hal_pico_init(void);

/* Core-1 loop headroom as a percentage. M4 exit criterion. */
uint8_t hal_pico_core1_load(void);

/*
 * DMA blocks dropped since boot. M4 requires this to stay at zero for ten
 * minutes, and it is the number that closes design §17's second open item —
 * or sends the analogue mixer back into the design.
 */
uint32_t hal_pico_overruns(void);

#endif /* HANDOFF_HAL_PICO_H */
