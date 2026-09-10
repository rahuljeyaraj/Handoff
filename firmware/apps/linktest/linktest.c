/*
 * Handoff — One-way link between two boards, role fixed at boot. M6.
 *
 * Hardware: A SECOND BOARD and two resistors. TX GP2 -> 1 MOhm -> RX pad node,
 * grounds commoned: a resistive stand-in for the body with none of its
 * variability.
 *
 * The transmitting board only ever transmits, and transmitting is a gated
 * square wave, so it does not need to be a Pico 2 W or even RP2350. Only the
 * RECEIVER needs RP2350, because of the RP2040 ADC differential-non-linearity
 * defect (design §10.2).
 *
 * Exit criteria (development plan M6):
 *
 *   - frames cross between two independently clocked boards, at both carriers
 *   - an hour free-running with no cumulative timing failure
 *   - BER against series resistance, 100 kOhm to 10 MOhm, plotted
 *
 * That last curve is the link's attenuation budget in dB as a single number,
 * and it is what every later analogue result gets compared against.
 *
 * This is the first test of two independent crystals, which loopback
 * structurally cannot do.
 */
#include <stdio.h>

#include "pico/stdlib.h"

#include "config.h"

int main(void)
{
    stdio_init_all();

    printf("handoff linktest (M6) — not implemented yet\n");
    printf("carrier %d Hz, %d chips/s, %d bps, Goertzel N=%d bin %d\n",
           HANDOFF_CARRIER_HZ, HANDOFF_CHIP_RATE_HZ,
           HANDOFF_BIT_RATE_BPS, HANDOFF_GZ_N, HANDOFF_GZ_BIN);

    /* M6: role from a GPIO strap or a build flag; the rest is M5's pipeline. */

    for (;;) {
        tight_loop_contents();
    }
}
