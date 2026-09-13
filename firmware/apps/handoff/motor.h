/*
 * Handoff — the haptic motor. Review item 11: a card successfully shared or
 * received buzzes the wrist, a different pattern for each, instead of
 * relying on the phone's own notification to be felt through a pocket.
 *
 * Hardware: a 10 mm coin motor on VSYS, switched low-side by an N-FET (Q1)
 * whose gate is GP28 (hardware README, "This session: the haptic motor").
 * R17 holds the gate down while GP28 is an input — power-up, and every
 * BOOTSEL reset — so motor_init() has to run before anything else touches
 * the pin, and it claims the pin as an output before its first write.
 *
 * NOT SAFE DURING A BODY-LINK RX WINDOW (M12). The schematic's own note:
 * "GP28 high runs the motor - never during an RX window." This image has no
 * body link yet, so nothing here can violate that; the caller at M12 must
 * gate motor_play() against proto/link_sm.h's receive state.
 *
 * Driven from a BTstack timer, the same as the LED and the fake-card path —
 * see ble.c's header comment for why nothing here may run from the main
 * loop under the cyw43 lock.
 */
#ifndef HANDOFF_MOTOR_H
#define HANDOFF_MOTOR_H

/* GP28 -> R16 -> Q1 gate. hardware/README.md, "HAPTIC" section. */
#define MOTOR_PIN 28

/* Claims GP28 as an output, driven low (motor off). Call once at boot. */
void motor_init(void);

typedef enum {
    MOTOR_PATTERN_SHARED,    /* one buzz: your card left the band       */
    MOTOR_PATTERN_RECEIVED,  /* two buzzes: a card arrived               */
} motor_pattern_t;

/*
 * Play a pattern. A pattern already playing is cancelled and restarted
 * rather than queued — a second handshake before the first buzz finishes
 * is still just "a handshake happened", not two things to feel separately.
 */
void motor_play(motor_pattern_t pattern);

#endif /* HANDOFF_MOTOR_H */
