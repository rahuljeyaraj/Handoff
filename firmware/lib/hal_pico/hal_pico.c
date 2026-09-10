/*
 * Handoff — hal.h on RP2350. STUB until M5. See hal_pico.h.
 *
 * No Pico SDK headers yet, so this compiles anywhere. That changes at M5;
 * until then the file exists so the seam is visible and M5 has something to
 * fill in rather than a decision to make.
 */
#include "hal_pico.h"

const hal_iface_t *hal_pico_init(void)
{
    /*
     * M5. Until then nothing may pretend to have hardware: returning NULL
     * makes every hal_* wrapper in hal.h a visible no-op rather than a silent
     * lie about a link that is not there.
     */
    return 0;
}

uint8_t  hal_pico_core1_load(void) { return 0; }
uint32_t hal_pico_overruns(void)   { return 0; }
