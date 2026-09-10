/*
 * Handoff — The real thing. M14.
 *
 * Hardware: two complete wristbands.
 *
 * Composes what every milestone before it built. Nothing new is invented here:
 * the link state machine and role election were written and tested at M1
 * against two simulated nodes, including the tie, the redraw and the backoff,
 * and M14 only binds them to real hardware.
 *
 * Exit criteria (development plan M14):
 *
 *   - 50 handshakes; both parties end up with each other's contact
 *   - role collisions resolve within the backoff budget
 *   - the tie case, forced deliberately from a synchronised trigger, resolves
 */
#include <stdio.h>

#include "pico/stdlib.h"

#include "config.h"

int main(void)
{
    stdio_init_all();

    printf("handoff handoff (M14) — not implemented yet\n");
    printf("carrier %d Hz, %d chips/s, %d bps, Goertzel N=%d bin %d\n",
           HANDOFF_CARRIER_HZ, HANDOFF_CHIP_RATE_HZ,
           HANDOFF_BIT_RATE_BPS, HANDOFF_GZ_N, HANDOFF_GZ_BIN);

    /* M14: hal_pico_init(), store_load(), link_sm over the real HAL. */

    for (;;) {
        tight_loop_contents();
    }
}
