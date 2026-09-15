/*
 * VSYS and VBUS on a Pico 2 W. See power.h for what these do and do not
 * mean electrically.
 *
 * This is pico-examples/adc/read_vsys/power_status.c, trimmed to the one
 * board this firmware runs on. The shape matters: GP29 is the CYW43's SPI
 * clock as well as ADC3, so the driver must be awake and the read has to
 * happen under its lock, and the first conversions after the pin is handed
 * to the ADC read low, so a few are thrown away.
 *
 * THE RING OWNS THE ADC. M12 landed and the DSP now runs a free-running DMA
 * ring on core 1 (adc_ring.c). Everything below — adc_init(), a different
 * channel, a different FIFO, adc_run(false) at the end — takes the converter
 * away from it and does not give it back: the ring stops dead, core 1 stops
 * producing chips, and the band goes deaf to the body link until it reboots.
 * Measured on 93D1, 15 Sep 2026: one call and `chips` never advanced again.
 *
 * So power_vsys_mv() now declines while the ring is running rather than
 * breaking the link, and 0 means "not measurable here". The real fix is to
 * read VSYS the way adc_ring_noise_floor() reads the temperature sensor —
 * switch the ring's own channel between blocks, on core 1, keeping the DMA
 * and the counters alive — which needs core 1 to hold the CYW43 lock for
 * GP29. That is its own piece of work.
 */
#include "power.h"

#include "hardware/adc.h"
#include "pico/cyw43_arch.h"

#include "adc_ring.h"

#ifndef PICO_VSYS_PIN
#define PICO_VSYS_PIN 29
#endif
#ifndef PICO_FIRST_ADC_PIN
#define PICO_FIRST_ADC_PIN 26
#endif

#define VSYS_SAMPLES 3

uint16_t power_vsys_mv(void)
{
    uint32_t sum = 0;
    int discard = VSYS_SAMPLES;

    /* See the head of this file: taking the ADC from the ring kills the
     * receiver for good. No reading is worth that. */
    if (adc_ring_running()) return 0u;

    cyw43_thread_enter();
    /* Make sure the CYW43 is awake before touching a pin it shares. */
    (void)cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN);

    adc_init();
    adc_gpio_init(PICO_VSYS_PIN);
    adc_select_input(PICO_VSYS_PIN - PICO_FIRST_ADC_PIN);

    adc_fifo_setup(true, false, 0, false, false);
    adc_run(true);

    /* The first conversions read low; drain them. */
    while (!adc_fifo_is_empty() || discard-- > 0)
        (void)adc_fifo_get_blocking();

    for (int i = 0; i < VSYS_SAMPLES; i++)
        sum += adc_fifo_get_blocking();

    adc_run(false);
    adc_fifo_drain();
    cyw43_thread_exit();

    sum /= VSYS_SAMPLES;

    /* 12-bit against a 3.3 V reference, through the Pico's own 3:1 divider. */
    return (uint16_t)((sum * 3u * 3300u) / 4096u);
}

bool power_on_usb(void)
{
    bool vbus;
    cyw43_thread_enter();
    vbus = cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN);
    cyw43_thread_exit();
    return vbus;
}
