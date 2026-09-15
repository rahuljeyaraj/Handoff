/* See led.h. */
#include "led.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

/*
 * 8-bit PWM at ~2.3 kHz: sysclk / (256 levels x 256 divider). Fast enough
 * not to flicker, slow enough that three switching edges a few millimetres
 * from the ADC0 lane are three edges every 430 µs and not every 2 µs.
 */
#define LED_WRAP 255u
#define LED_DIV  256.0f

static const uint8_t k_pins[3] = { LED_PIN_R, LED_PIN_G, LED_PIN_B };
static const uint8_t k_scale[3] = { LED_SCALE_R, LED_SCALE_G, LED_SCALE_B };

static uint16_t level(uint8_t v, uint8_t scale)
{
    /* squared, for a roughly perceptual ramp: dim rows stay dim */
    uint32_t g = ((uint32_t)v * v + 127u) / 255u;
    return (uint16_t)((g * scale + 127u) / 255u);
}

void led_init(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        uint slice = pwm_gpio_to_slice_num(k_pins[i]);
        gpio_set_function(k_pins[i], GPIO_FUNC_PWM);
        pwm_set_wrap(slice, LED_WRAP);
        pwm_set_clkdiv(slice, LED_DIV);
        pwm_set_gpio_level(k_pins[i], 0);
        pwm_set_enabled(slice, true);
    }
}

void led_set(ui_rgb_t c)
{
    pwm_set_gpio_level(LED_PIN_R, level(c.r, k_scale[0]));
    pwm_set_gpio_level(LED_PIN_G, level(c.g, k_scale[1]));
    pwm_set_gpio_level(LED_PIN_B, level(c.b, k_scale[2]));
}
