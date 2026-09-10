/*
 * Handoff — ADC at 500 ksps and the real-time budget. M4.
 *
 * Hardware: NONE. Leave GP26 at ground or 3V3.
 *
 * This closes design §17's second open item. The arithmetic says it will hold:
 * 500 k samples/s against a 150 MHz core is 300 cycles per sample and a
 * Goertzel inner iteration is single digits, so 2-5 % core load. THE RISK IS
 * DMA, INTERRUPT HANDLING AND RING OVERRUN. Instrument for that specifically.
 *
 * Exit criteria (development plan M4):
 *
 *   - measured sample rate within 0.01 % of 500 ksps, timed over 60 s
 *   - zero dropped DMA blocks in 10 minutes, with a counter proving it
 *   - core-1 loop headroom printed as a percentage
 *   - noise floor of the bare ADC recorded in LSB RMS
 *
 * That last figure is the reference every later amplitude measurement is
 * compared against, so record it rather than glance at it.
 */
#include <stdio.h>

#include "pico/stdlib.h"

#include "config.h"
#include "adc_ring.h"
#include "ipc.h"

int main(void)
{
    stdio_init_all();

    printf("handoff adcbench (M4) — not implemented yet\n");
    printf("carrier %d Hz, %d chips/s, %d bps, Goertzel N=%d bin %d\n",
           HANDOFF_CARRIER_HZ, HANDOFF_CHIP_RATE_HZ,
           HANDOFF_BIT_RATE_BPS, HANDOFF_GZ_N, HANDOFF_GZ_BIN);

    adc_ring_init();
    adc_ring_start();

    for (;;) {
        tight_loop_contents();
    }
}
