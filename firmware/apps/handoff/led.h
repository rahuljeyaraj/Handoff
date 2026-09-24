/*
 * Handoff — the RGB LED. GP17 / GP18 / GP19 are R / G / B through 330 Ω to
 * a common-cathode 5 mm LED on J3 (hardware README, "RGB LED"). The three
 * channels are PWM'd so the mix can be balanced.
 *
 * LED_SCALE_R was 64 on the reasoning that at 3.3 V the red draws ~4 mA
 * against ~1 mA for green and blue, so an equal drive would be pink. Set by
 * eye on the first assembled board (24 Sep 2026) that reasoning is wrong:
 * at 64 the "white" rows read plainly cyan, at 160 they were brighter cyan,
 * and only full red makes white. Red alone lights correctly, so this is the
 * mix and not the channel. Current is not brightness across three different
 * dies, and this LED's red die is the weak one.
 *
 * So there is no reduction: all three scales are 255. The PWM stays, because
 * it is what lets the scale be a number at all, and the `l` console command
 * sets it live for the next LED that needs balancing — that is how this one
 * was measured.
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

/* Per-channel scale out of 255, applied after gamma. Measured, not guessed:
 * see the note above. */
#define LED_SCALE_R 255
#define LED_SCALE_G 255
#define LED_SCALE_B 255

/* Claims the three pins as PWM outputs, all off. */
void led_init(void);

/* Logical colour in, as ui.h's table gives it. */
void led_set(ui_rgb_t c);

/*
 * The bench knob the comment above asks for: set the red scale by eye on a
 * real board, then write the answer into LED_SCALE_R. Re-applies the colour
 * showing now, so the change is visible without waiting for the next event.
 */
void led_red_scale(uint8_t scale);
uint8_t led_red_scale_now(void);

#endif /* HANDOFF_LED_H */
