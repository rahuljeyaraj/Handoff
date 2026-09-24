/*
 * Handoff — what the wearer sees and feels. The RGB LED and the coin motor,
 * as a table of patterns driven by events, with no hardware in it.
 *
 * The board's three wearer-facing parts were not in the design doc: an RGB
 * LED on GP17/18/19, a coin motor on GP28 and a push button on GP15 (hardware
 * README). This module is the LED's and the motor's behaviour; button.h is
 * the button's. The binding — PWM, GPIO, the clock — is apps/handoff/wear.c.
 *
 * The model is two layers over the LED and one over the motor:
 *
 *   background   a repeating pattern chosen from the band's STATE, by
 *                priority: fault > body link active > pairing >
 *                battery critical > battery low > holding a card > off.
 *                Off is the product's steady state — a wristband that is
 *                listening for a touch shows nothing, like a WHOOP, not a
 *                5-second heartbeat like a headphone.
 *   foreground   a one-shot sequence played on an EVENT (boot, connected,
 *                card received...). Masks the background while it plays; a
 *                new one replaces whatever was playing.
 *   motor        a one-shot sequence on an event. NEVER while the body link
 *                is active: 100 mA of commutating motor on VSYS is what the
 *                x121 front end must not see (hardware README, "HAPTIC").
 *                A sequence requested during a handshake waits for idle;
 *                one that is playing when a handshake starts is cut.
 *
 * The rows below are the table this was designed from. Colours are logical
 * (255 = full); the binding scales red down to match the 1 mA green and blue.
 *
 *   state / event                    LED                          motor
 *   boot                             white 200 ms                 tap
 *   no owner (advertising, no bond)  blue double-flash / 2 s      -
 *   bonded, phone away               off                          -
 *   phone connected                  blue solid 2 s               -
 *   phone connected, idle            off                          -
 *   card written by the phone        green double-flash           tap (h)
 *   listening (idle)                 OFF                          -
 *   rendezvous (a peer confirmed)    white 10 Hz                  none (link)
 *   handshake, sending / receiving   white solid                  none (link)
 *   complete, card received          green solid 2 s              double tap (h)
 *   card forwarded to the phone      + one green blip             -
 *   card held (no phone yet)         amber blip / 5 s             -
 *   handshake aborted                amber triple                 long (h)
 *   nothing to give at a handshake   amber double-flash           -
 *   battery low                      red blip / 10 s              buzz, once
 *   battery critical                 red triple / 5 s             long, once
 *   fault                            red / blue 4 Hz              -
 *   reset for a new wearer           purple x4                    buzz
 *   identify (from the app)          white x3 fast                3 taps
 *   battery check (button)           green / amber / red 300 ms   -
 *   battery check, level unknown     white 300 ms                 -
 *   hold threshold reached (5 s)     -                            tap
 *
 * (h): masked by the app's vibrate setting. Everything else always plays.
 *
 * Time is milliseconds in a uint32_t; every comparison is a wrap-safe
 * subtraction. ui_step() is a pure function of (state, now): call it as
 * often or as rarely as the binding likes, it never accumulates.
 */
#ifndef HANDOFF_UI_H
#define HANDOFF_UI_H

#include <stdbool.h>
#include <stdint.h>

typedef struct { uint8_t r, g, b; } ui_rgb_t;

typedef struct {
    ui_rgb_t led;
    bool     motor;
} ui_out_t;

typedef enum {
    UI_EV_BOOT = 0,
    /* the phone link */
    UI_EV_PAIRING,           /* state: no owner — advertising, no bond     */
    UI_EV_NOT_PAIRING,       /* state: owned, or connected                 */
    UI_EV_PHONE_CONNECTED,
    UI_EV_CARD_WRITTEN,
    /* the body link, states */
    UI_EV_LINK_IDLE,
    UI_EV_LINK_RENDEZVOUS,
    UI_EV_LINK_SENDING,
    UI_EV_LINK_RECEIVING,
    /* the body link, events */
    UI_EV_LINK_COMPLETE,
    UI_EV_LINK_FORWARDED,    /* the received card reached the phone        */
    UI_EV_LINK_HELD,         /* state: received, no phone to give it to    */
    UI_EV_LINK_RELEASED,     /* state: the held card was collected         */
    UI_EV_LINK_ABORT,
    UI_EV_LINK_NO_CARD,      /* a handshake began with nothing to send     */
    /* power, states */
    UI_EV_BATTERY_OK,
    UI_EV_BATTERY_LOW,
    UI_EV_BATTERY_CRITICAL,
    /* the rest */
    UI_EV_FAULT,             /* state, sticky until reboot                 */
    UI_EV_BOND_CLEARED,      /* reset for a new wearer: button or app      */
    UI_EV_IDENTIFY,
    UI_EV_BATTERY_SHOW_GOOD, /* the button's answer, by level              */
    UI_EV_BATTERY_SHOW_MID,
    UI_EV_BATTERY_SHOW_LOW,
    UI_EV_BATTERY_SHOW_UNKNOWN, /* ...or no reading yet: not the same as good */
    UI_EV_HOLD_REACHED,      /* the button crossed a hold threshold        */
    UI_EV_COUNT
} ui_event_t;

typedef enum {
    UI_LINK_IDLE = 0,
    UI_LINK_RENDEZVOUS,
    UI_LINK_SENDING,
    UI_LINK_RECEIVING
} ui_link_t;

typedef enum { UI_BATT_OK = 0, UI_BATT_LOW, UI_BATT_CRITICAL } ui_batt_t;

/* One LED pattern: steps of colour and duration. A step of 0 ms lasts until
 * the pattern is replaced. period_ms > 0 repeats (a background); 0 plays
 * once and ends after the last step (a foreground). */
typedef struct {
    ui_rgb_t c;
    uint16_t ms;
} ui_led_step_t;

typedef struct {
    const ui_led_step_t *steps;
    uint8_t              n;
    uint16_t             period_ms;
} ui_led_pat_t;

/* One motor pattern: durations alternating on, off, on... starting on. */
typedef struct {
    const uint16_t *ms;
    uint8_t         n;
} ui_motor_pat_t;

typedef struct {
    /* state the background is derived from */
    bool      fault;
    bool      pairing;
    bool      held;
    ui_link_t link;
    ui_batt_t batt;
    bool      haptic;                 /* the app's vibrate setting          */

    /* the foreground, if any */
    const ui_led_pat_t *fg;
    uint32_t            fg_at;

    /* the motor: what is playing, and what waits for the link to go idle */
    const ui_motor_pat_t *motor;
    uint32_t              motor_at;
    const ui_motor_pat_t *motor_pending;

    /* for the binding's trace and the tests */
    ui_event_t last_event;
    uint32_t   events;
} ui_t;

void ui_init(ui_t *u, uint32_t now_ms);
void ui_set_haptic(ui_t *u, bool on);
void ui_event(ui_t *u, ui_event_t ev, uint32_t now_ms);
void ui_step(const ui_t *u, uint32_t now_ms, ui_out_t *out);

/* True while the body link is active: the window in which the motor is
 * silent. The binding uses it to know a buzz has been deferred. */
bool ui_link_active(const ui_t *u);

const char *ui_event_name(ui_event_t ev);

/* Parse a name back, for the bench console. -1 if unknown. */
int ui_event_parse(const char *name);

#endif /* HANDOFF_UI_H */
