/* See motor.h. */
#include "motor.h"

#include "hardware/gpio.h"

void motor_init(void)
{
    gpio_init(MOTOR_PIN);
    gpio_set_dir(MOTOR_PIN, GPIO_OUT);
    gpio_put(MOTOR_PIN, 0);
}

void motor_set(bool on)
{
    gpio_put(MOTOR_PIN, on);
}
