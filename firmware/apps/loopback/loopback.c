/*
 * Handoff — The whole link inside one chip. M5.
 *
 * Hardware: ONE JUMPER WIRE, then two resistors.
 *
 *   Phase A  GP2 -> GP26 bare. A 0-3.3 V square straight into the ADC. Safe,
 *            but ~2000 LSB where the link budget expects ~200, so it proves
 *            plumbing and not margin.
 *   Phase B  10 kOhm from GP2 to the ADC node, 680 Ohm to ground. ~210 mV p-p,
 *            matching design §5, from a 640 Ohm source the SAR is happy with.
 *
 * Exit criteria (development plan M5):
 *
 *   - a frame passes CRC across the loop at 40 kHz and at 200 kHz
 *   - 1000 consecutive frames, zero failures, at Phase B amplitude
 *   - BER against a progressively harsher attenuator, agreeing with the M1
 *     simulator to within a few dB
 *   - with GP2 high-Z, the received score drops to the M4 noise floor
 *
 * THE AGREEMENT WITH THE SIMULATOR IS THE REAL DELIVERABLE. It is what lets
 * the simulator be trusted for everything after this.
 *
 * Cannot prove: independent clocks (both PLLs come off the same crystal), or
 * anything a shared codebase bug would cancel out — run M1's independently
 * generated vectors here too.
 */
#include <stdio.h>

#include "pico/stdlib.h"

#include "config.h"

int main(void)
{
    stdio_init_all();

    printf("handoff loopback (M5) — not implemented yet\n");
    printf("carrier %d Hz, %d chips/s, %d bps, Goertzel N=%d bin %d\n",
           HANDOFF_CARRIER_HZ, HANDOFF_CHIP_RATE_HZ,
           HANDOFF_BIT_RATE_BPS, HANDOFF_GZ_N, HANDOFF_GZ_BIN);

    /* M5: hal_pico_init(), then the same lib/ pipeline the host tests use. */

    for (;;) {
        tight_loop_contents();
    }
}
