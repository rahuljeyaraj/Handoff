/*
 * Handoff — the wearer's side of the band, bound to the pins. lib/ui decides
 * what the LED shows and when the motor runs; lib/ui/button classifies a
 * press. This file is the clock, the pins, and the two contexts.
 *
 * Contexts. Everything in lib/ui runs in the MAIN LOOP, from wear_poll():
 * the LED and the motor are plain GPIO/PWM and the button is a plain input,
 * so nothing here needs the BTstack context and nothing here may call into
 * the CYW43 (M2's rule, see handoff.c). What the BTstack side knows — the
 * connection, the bond, VSYS, a card written by the phone, an app command —
 * reaches here through wear_post() and wear_set_*(), which only set
 * flags; wear_poll() reads them. A flag posted twice before a poll is one
 * event, and a post that lands between a poll's read and clear is lost —
 * the same trade handoff.c's s_rx_ready makes, and for the same reason: an
 * indicator is not a queue.
 *
 * The button. SW2 on GP15, R15 pulling up, active low. Its three lengths:
 *
 *   short          battery check: one flash by level
 *   hold 2 s       dev mode toggle (a flag in the status notify, purple blip)
 *   hold 6 s       clear the bond — done on the BTstack side, which asks
 *                  wear_take_forget_request() and posts BOND_CLEARED after
 *
 * GP15 is also where M14's bench sync pulse lived. The two cannot share a
 * pin — on the product board a press during a driven-high pulse shorts the
 * GPIO — so the pulse is now behind HANDOFF_BENCH_SYNC, off by default,
 * and the button is disabled when it is on.
 *
 * Verification without the parts. `u` on the console traces every LED and
 * motor transition with a timestamp; `u <event>` injects a table row;
 * `b <ms>` presses the button for that long. Every row of ui.h's table can
 * be walked on a bare Pico and read back from the log.
 */
#ifndef HANDOFF_WEAR_H
#define HANDOFF_WEAR_H

#include <stdbool.h>
#include <stdint.h>

#include "link_sm.h"
#include "ui.h"

#define WEAR_PIN_BUTTON 15

/*
 * Battery thresholds, on VSYS after D1 (hardware README: add ~0.35 V for the
 * cell). Low at a 3.5 V cell, critical at 3.3 V, and the button's answer is
 * green above 3.8 V, amber above 3.5 V, red below. 50 mV of hysteresis on
 * the way back up. Anything above 4.3 V is USB, where the cell cannot be
 * seen at all (power.h) and the battery is reported OK.
 */
#define WEAR_VSYS_GOOD_MV     3450u
#define WEAR_VSYS_LOW_MV      3150u
#define WEAR_VSYS_CRIT_MV     2950u
#define WEAR_VSYS_HYST_MV     50u
#define WEAR_VSYS_USB_MV      4300u

/* Pins, ui state, and the boot flash. After motor_init(). */
void wear_init(uint64_t now_us);

/* The main loop's call. Drains posted events, reads the button, steps the
 * table, drives the pins. */
void wear_poll(uint64_t now_us);

/* ---- inputs from the BTstack context ---------------------------------- */

void wear_post(ui_event_t ev);
void wear_set_ble(bool connected, bool bonded);
void wear_set_power(uint16_t vsys_mv, bool on_usb);
void wear_set_haptic(bool on);

/* ---- inputs from the main loop ---------------------------------------- */

/* The body link, after every link_sm_poll: maps its state and the
 * trigger's to the table's idle / rendezvous / sending / receiving. */
void wear_link(const link_sm_t *sm);

/* ---- outputs ---------------------------------------------------------- */

bool wear_dev_mode(void);

/* True once per 6 s hold: the BTstack side erases the bond and posts
 * UI_EV_BOND_CLEARED. */
bool wear_take_forget_request(void);

/* ---- the bench console -------------------------------------------------- */

void wear_trace(bool on);
bool wear_tracing(void);
void wear_inject(ui_event_t ev);
void wear_press(uint32_t ms);

#endif /* HANDOFF_WEAR_H */
