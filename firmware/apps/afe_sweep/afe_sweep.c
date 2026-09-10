/*
 * Handoff — Analogue front end, characterised alone. M7.
 *
 * Hardware: MCP6292, passives, perfboard. THE AFE, NOT YET IN THE LINK.
 *
 * Do not put the amplifier in the loop yet. Measure it first, using the Pico as
 * both signal source and instrument: step the PIO carrier across frequency and
 * report ADC amplitude at each point. An amplifier of unknown gain inside an
 * untested loop cannot be debugged, which is the whole reason M7 exists.
 *
 * Exit criteria (development plan M7):
 *
 *   - VREF measures 1.65 V +-5 %
 *   - measured gain against the design's x121, at 40 kHz and at 200 kHz
 *   - measured -3 dB corner against the predicted ~580 kHz (design §6.4)
 *   - output noise floor in LSB RMS, against M4's bare-ADC figure
 *   - the clipping point, found deliberately
 *   - PREAMP INPUT CAPACITANCE IN SITU, from the rolloff against a known
 *     series resistor. This is design §17's first open item and it decides
 *     whether 200 kHz is achievable at all.
 *   - every adjacent MSOP pin pair continuity-tested before power (design §12.1)
 */
#include <stdio.h>

#include "pico/stdlib.h"

#include "config.h"

int main(void)
{
    stdio_init_all();

    printf("handoff afe_sweep (M7) — not implemented yet\n");
    printf("carrier %d Hz, %d chips/s, %d bps, Goertzel N=%d bin %d\n",
           HANDOFF_CARRIER_HZ, HANDOFF_CHIP_RATE_HZ,
           HANDOFF_BIT_RATE_BPS, HANDOFF_GZ_N, HANDOFF_GZ_BIN);

    /* M7: sweep the PIO divider, report ADC amplitude at each point. */

    for (;;) {
        tight_loop_contents();
    }
}
