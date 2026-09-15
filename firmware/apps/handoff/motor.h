/*
 * Handoff — the haptic motor, as a switch. What it plays and when is
 * lib/ui (ui.h's table); this is the pin.
 *
 * Hardware: a 10 mm coin motor on VSYS, switched low-side by an N-FET (Q1)
 * whose gate is GP28 (hardware README, "This session: the haptic motor").
 * R17 holds the gate down while GP28 is an input — power-up, and every
 * BOOTSEL reset — so motor_init() has to run before anything else touches
 * the pin, and it claims the pin as an output before its first write.
 *
 * NOT SAFE DURING A BODY-LINK RX WINDOW. The schematic's own note: "GP28
 * high runs the motor - never during an RX window." ui.c enforces it: a
 * pattern requested while the link is active waits, one that is playing
 * when the link starts is cut. Nothing else may call motor_set().
 */
#ifndef HANDOFF_MOTOR_H
#define HANDOFF_MOTOR_H

#include <stdbool.h>

/* GP28 -> R16 -> Q1 gate. hardware/README.md, "HAPTIC" section. */
#define MOTOR_PIN 28

/* Claims GP28 as an output, driven low (motor off). Call once at boot. */
void motor_init(void);

void motor_set(bool on);

#endif /* HANDOFF_MOTOR_H */
