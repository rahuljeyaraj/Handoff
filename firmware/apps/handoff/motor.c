/*
 * See motor.h. Two on/off pulses, timed by the same btstack_run_loop timer
 * mechanism the fake-card path and the status tick already use, so this
 * needs no thread of its own and stays inside the BTstack context.
 */
#include "motor.h"

#include <stdbool.h>
#include <stdint.h>

#include "hardware/gpio.h"

#include "btstack_run_loop.h"

/* Short enough that two pulses in a RECEIVED pattern read as two distinct
 * taps rather than one long buzz, long enough to actually feel on a wrist. */
#define PULSE_MS 120u
#define GAP_MS   120u

static btstack_timer_source_t s_timer;
static uint8_t                s_pulses_left;
static bool                   s_motor_on;

static void set(bool on)
{
    s_motor_on = on;
    gpio_put(MOTOR_PIN, on);
}

static void tick(btstack_timer_source_t *ts)
{
    if (s_motor_on) {
        set(false);
        if (s_pulses_left == 0) return;
        btstack_run_loop_set_timer(ts, GAP_MS);
    } else {
        if (s_pulses_left == 0) return;
        s_pulses_left--;
        set(true);
        btstack_run_loop_set_timer(ts, PULSE_MS);
    }
    btstack_run_loop_add_timer(ts);
}

void motor_init(void)
{
    gpio_init(MOTOR_PIN);
    gpio_set_dir(MOTOR_PIN, GPIO_OUT);
    gpio_put(MOTOR_PIN, 0);
    btstack_run_loop_set_timer_handler(&s_timer, tick);
}

void motor_play(motor_pattern_t pattern)
{
    btstack_run_loop_remove_timer(&s_timer);
    set(false);
    s_pulses_left = (pattern == MOTOR_PATTERN_SHARED) ? 1u : 2u;
    tick(&s_timer);
}
