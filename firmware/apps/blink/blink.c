/*
 * Handoff — bring-up step 0.
 *
 * Blinks the onboard LED and prints a heartbeat over USB CDC. This proves
 * four things before any analogue work starts: the board is alive, the
 * toolchain produces a working RP2350 image, the CYW43439 comes up, and the
 * USB console works — the console being the instrument the whole validation
 * campaign depends on (design doc §10.5).
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#define BLINK_PERIOD_MS 1000

int main(void)
{
    stdio_init_all();

    /*
     * On Pico 2 W the LED is not on an RP2350 GPIO at all — it hangs off
     * GPIO 0 of the CYW43439, so the wireless driver has to be up before
     * anything can be blinked. (pico2_w.h deliberately does not define
     * PICO_DEFAULT_LED_PIN; on the non-wireless Pico 2 it is GP25.)
     */
    if (cyw43_arch_init()) {
        return 1;
    }

    /*
     * Design doc §10.4 asks for the SMPS to be pinned into fixed-frequency
     * PWM mode, so its switching noise lands in one predictable place instead
     * of wandering across the ADC band.
     *
     * The doc names GP23 for this. That is correct for the plain Pico 2
     * (PICO_SMPS_MODE_PIN 23) but WRONG for the 2 W: on the wireless boards
     * GP23 drives the CYW43 power enable, and the SMPS mode control moves to
     * WL_GPIO1 — CYW43_WL_GPIO_SMPS_PIN. Enable this when the ADC goes live;
     * it costs idle current and buys nothing yet.
     */
    /* cyw43_arch_gpio_put(CYW43_WL_GPIO_SMPS_PIN, true); */

    uint32_t beat = 0;

    while (true) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(BLINK_PERIOD_MS / 2);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(BLINK_PERIOD_MS / 2);

        printf("handoff: alive, beat %lu\n", (unsigned long)++beat);
    }
}
