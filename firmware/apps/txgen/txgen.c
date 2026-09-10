/*
 * Handoff — Carrier generation, self-measured. M3.
 *
 * Hardware: NONE. That is the point — a second PIO state machine reads the pin
 * state of GP2 and counts edges over a gate interval, and the PIO input mux is
 * independent of whichever peripheral drives the pad. No jumper, no scope.
 *
 * Exit criteria (development plan M3):
 *
 *   - measured carrier within 0.1 % of 40 000 Hz and of 200 000 Hz
 *   - chip timing jitter under 1 us against a 500 us chip
 *   - gating on and off is chip-aligned, verified by counting edges per chip
 *   - GP2 reads as high-Z when told to be, RP2350-E9 notwithstanding
 *
 * Cannot prove: signal amplitude, or anything at all about the receive side.
 */
#include <stdio.h>

#include "pico/stdlib.h"

#include "config.h"
#include "pio_carrier.h"

int main(void)
{
    stdio_init_all();

    printf("handoff txgen (M3) — not implemented yet\n");
    printf("carrier %d Hz, %d chips/s, %d bps, Goertzel N=%d bin %d\n",
           HANDOFF_CARRIER_HZ, HANDOFF_CHIP_RATE_HZ,
           HANDOFF_BIT_RATE_BPS, HANDOFF_GZ_N, HANDOFF_GZ_BIN);

    pio_carrier_init(HANDOFF_CARRIER_HZ);

    for (;;) {
        tight_loop_contents();
    }
}
