/*
 * VSYS and VBUS on a Pico 2 W. See power.h for what these do and do not
 * mean electrically.
 *
 * GP29 is ADC3 and the CYW43's SPI clock. Reading it means putting the pad
 * into analogue mode, which takes it away from the driver; the driver sets
 * the function back at the head of its next transfer (start_spi_comms in
 * cyw43_spi.c), so the pin heals itself and is not restored by hand — the
 * right funcsel is private to the driver. What has to be guaranteed is only
 * that no transfer overlaps the window in which the pin is analogue, and
 * that is what holding the lock across the read buys. The chip is woken
 * first, because waking it IS a transfer, and one that ran after the swap
 * would take the clock pin back mid-measurement.
 *
 * THERE IS ONE ADC AND THE RING OWNS IT. From hal_pico_init() on, core 1
 * runs a free-running DMA ring on the converter (adc_ring.c), and
 * pico-examples' read_vsys — adc_init(), another channel, another FIFO,
 * adc_run(false) at the end — took it away for good: the ring stopped, core
 * 1 spun on a block that never came, and the band went deaf until it
 * rebooted (93D1, 15 Sep 2026). Stopping and restarting the ring instead
 * would reset the sample clock the own-send cutting is placed on.
 *
 * So there are two paths, and this file routes between them:
 *
 *   ring running    core 0 owns the PERMISSION — this context, the lock,
 *                   the woken chip — and core 1 owns the CONVERTER: it
 *                   swaps the ring's input to ADC3 between blocks and back,
 *                   with the counters and the sample clock running through
 *                   (hal_pico_read_vsys_mv, adc_ring_aux_step). Core 1
 *                   never takes the lock: it is the only consumer of a ring
 *                   that overruns in 4 ms, and cyw43_thread_enter() blocks.
 *
 *   ring stopped    the standalone read below, which is read_vsys as it was.
 *                   Correct when nothing else owns the converter: the bench
 *                   apps, and boot before the ring starts.
 *
 * The full argument is docs/vsys-in-the-ring.md.
 */
#include "power.h"

#include "hardware/adc.h"
#include "pico/cyw43_arch.h"

#include "adc_ring.h"
#include "hal_pico.h"

#define VSYS_SAMPLES 3

/* The converter is nobody's: take it, read, and leave it stopped. */
static uint16_t vsys_standalone(void)
{
    uint32_t sum = 0;
    int discard = VSYS_SAMPLES;

    adc_init();
    adc_gpio_init(HANDOFF_PIN_VSYS);
    adc_select_input(HANDOFF_VSYS_CHANNEL);

    adc_fifo_setup(true, false, 0, false, false);
    adc_run(true);

    /* The first conversions read low; drain them. */
    while (!adc_fifo_is_empty() || discard-- > 0)
        (void)adc_fifo_get_blocking();

    for (int i = 0; i < VSYS_SAMPLES; i++)
        sum += adc_fifo_get_blocking();

    adc_run(false);
    adc_fifo_drain();

    sum /= VSYS_SAMPLES;

    /* 12-bit against a 3.3 V reference, through the Pico's own 3:1 divider. */
    return (uint16_t)((sum * 3u * 3300u) / 4096u);
}

uint16_t power_vsys_mv(void)
{
    uint16_t mv;

    cyw43_thread_enter();
    /* Awake before the swap: waking it is itself a transfer on GP29. */
    (void)cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN);

    mv = adc_ring_running() ? hal_pico_read_vsys_mv() : vsys_standalone();

    cyw43_thread_exit();
    return mv;
}

bool power_on_usb(void)
{
    bool vbus;
    cyw43_thread_enter();
    vbus = cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN);
    cyw43_thread_exit();
    return vbus;
}
