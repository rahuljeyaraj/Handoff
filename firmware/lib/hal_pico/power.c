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
 * NOT FOR M12. The DSP will own the ADC on core 1 with a free-running DMA
 * ring (adc_ring.c), and adc_select_input() here would steal its channel
 * mid-block. When that lands, either sample VSYS through the ring's own
 * round-robin or read it only while the ring is stopped.
 */
#include "power.h"

#include "hardware/adc.h"
#include "pico/cyw43_arch.h"

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
