/*
 * Handoff — the RGB LED. GP17 / GP18 / GP19 are R / G / B through 330 Ω to
 * a common-cathode 5 mm LED on J3 (hardware README, "RGB LED"). At 3.3 V the
 * red runs ~4 mA and the green and blue ~1 mA, so an equal drive is pink:
 * the three channels are PWM'd and red is scaled down to meet the others.
 *
 * The scale is a guess until the LED is on the board — LED_SCALE_R is the
 * one number to adjust on PCB day, by eye, until the "white" rows of the
 * ui.h table look white.
 *
 * Plain PWM on plain GPIOs: safe to drive from the main loop, nothing here
 * touches the CYW43.
 */
#ifndef HANDOFF_LED_H
#define HANDOFF_LED_H

#include "ui.h"

#define LED_PIN_R 17
#define LED_PIN_G 18
#define LED_PIN_B 19

/* Per-channel scale out of 255 applied after gamma. Red is a quarter: the
 * 330 Ω sets it four times the current of the other two. */
#define LED_SCALE_R 64
#define LED_SCALE_G 255
#define LED_SCALE_B 255

/* Claims the three pins as PWM outputs, all off. */
void led_init(void);

/* Logical colour in, as ui.h's table gives it. */
void led_set(ui_rgb_t c);

#endif /* HANDOFF_LED_H */
