/* See wear.h. */
#include "wear.h"

#include <stdio.h>
#include <string.h>

#include "hardware/gpio.h"

#include "beacon.h"
#include "button.h"
#include "led.h"
#include "motor.h"

#ifndef HANDOFF_BENCH_SYNC
#define HANDOFF_BENCH_SYNC 0
#endif

static ui_t     s_ui;
static button_t s_btn;

/* written in the BTstack context, read here */
static volatile uint8_t  s_posted[UI_EV_COUNT];
static volatile bool     s_ble_valid, s_ble_connected, s_ble_bonded;
static volatile bool     s_power_valid, s_on_usb;
static volatile uint16_t s_vsys_mv;
static volatile bool     s_haptic = true, s_haptic_dirty;

/* written here, read in the BTstack context */
static volatile bool     s_reset;

static bool      s_pairing, s_connected;
static ui_batt_t s_batt;
static ui_link_t s_link;

static bool     s_trace;
static ui_out_t s_out;
static bool     s_out_valid;

/* a synthetic press from the console: armed with a length, started by the
 * next poll, which has the clock */
static bool     s_press_armed, s_pressing;
static uint32_t s_press_ms, s_press_until;

static uint32_t ms_of(uint64_t now_us) { return (uint32_t)(now_us / 1000u); }

static void event(ui_event_t ev, uint32_t now)
{
    ui_event(&s_ui, ev, now);
    if (s_trace) printf("ui %lu ev %s\n", (unsigned long)now, ui_event_name(ev));
}

/* ---- inputs from the BTstack context ---------------------------------- */

void wear_post(ui_event_t ev)
{
    if (ev < UI_EV_COUNT) s_posted[ev] = 1u;
}

void wear_set_ble(bool connected, bool bonded)
{
    s_ble_connected = connected;
    s_ble_bonded = bonded;
    s_ble_valid = true;
}

/*
 * 0 mV is "not measured" (power.h), and unknown is not good: a band that
 * could not read its cell must not tell the wearer the battery is fine. So
 * a 0 leaves the level where the last real reading put it and makes the
 * button answer "unknown" until the next one lands.
 */
void wear_set_power(uint16_t vsys_mv, bool on_usb)
{
    s_vsys_mv = vsys_mv;
    s_on_usb = on_usb;
    s_power_valid = vsys_mv != 0u;
}

void wear_set_haptic(bool on)
{
    s_haptic = on;
    s_haptic_dirty = true;
}

/* ---- the battery -------------------------------------------------------- */

/* Only ever called with a real reading (s_power_valid); 0 never gets here. */
static ui_batt_t battery_level(uint16_t mv, bool on_usb, ui_batt_t was)
{
    if (on_usb || mv >= WEAR_VSYS_USB_MV) return UI_BATT_OK;
    switch (was) {
    case UI_BATT_CRITICAL:
        if (mv < WEAR_VSYS_CRIT_MV + WEAR_VSYS_HYST_MV) return UI_BATT_CRITICAL;
        return mv < WEAR_VSYS_LOW_MV ? UI_BATT_LOW : UI_BATT_OK;
    case UI_BATT_LOW:
        if (mv < WEAR_VSYS_CRIT_MV) return UI_BATT_CRITICAL;
        if (mv < WEAR_VSYS_LOW_MV + WEAR_VSYS_HYST_MV) return UI_BATT_LOW;
        return UI_BATT_OK;
    default:
        if (mv < WEAR_VSYS_CRIT_MV) return UI_BATT_CRITICAL;
        if (mv < WEAR_VSYS_LOW_MV) return UI_BATT_LOW;
        return UI_BATT_OK;
    }
}

static ui_event_t battery_show(uint16_t mv, bool on_usb)
{
    if (mv == 0u) return UI_EV_BATTERY_SHOW_UNKNOWN;
    if (on_usb || mv >= WEAR_VSYS_USB_MV) return UI_EV_BATTERY_SHOW_GOOD;
    if (mv >= WEAR_VSYS_GOOD_MV) return UI_EV_BATTERY_SHOW_GOOD;
    if (mv >= WEAR_VSYS_LOW_MV)  return UI_EV_BATTERY_SHOW_MID;
    return UI_EV_BATTERY_SHOW_LOW;
}

/* ---- the button --------------------------------------------------------- */

static bool button_raw(uint32_t now)
{
    if (s_press_armed) {
        s_press_armed = false;
        s_pressing = true;
        s_press_until = now + s_press_ms;
    }
    if (s_pressing) {
        if ((int32_t)(s_press_until - now) > 0) return true;
        s_pressing = false;
    }
#if HANDOFF_BENCH_SYNC
    return false;                        /* GP15 is the sync pulse */
#else
    return !gpio_get(WEAR_PIN_BUTTON);   /* R15 pulls up; a press is low */
#endif
}

static void button(uint32_t now)
{
    button_event_t ev = button_feed(&s_btn, button_raw(now), now);
    if (ev == BUTTON_NONE) return;
    if (s_trace) printf("ui %lu btn %s\n", (unsigned long)now, button_event_name(ev));

    switch (ev) {
    case BUTTON_SHORT:
        event(battery_show(s_vsys_mv, s_on_usb), now);
        break;
    case BUTTON_HOLD_REACHED:
        event(UI_EV_HOLD_REACHED, now);
        break;
    case BUTTON_HOLD:
        s_reset = true;
        break;
    default:
        break;
    }
}

/* ---- the body link ------------------------------------------------------ */

/*
 * RENDEZVOUS IS A CONFIRMED PEER, NOT A CARRIER BEING MEASURED. This read
 * TRIG_WAIT, which only meant the trigger heard *something* and was still
 * timing it to find out what. The room supplies those constantly — the hunt
 * behind [[handoff-elects-two-senders]] threw away 715 room bursts on one
 * board — so the band blinked white at 10 Hz essentially without stopping,
 * which is the opposite of ui.h's "listening (idle) OFF" row and of what a
 * wristband should do on a wrist.
 *
 * LINK V2 STEP 7 removed the state this was guarding against. There is no
 * TRIG_WAIT: a band is either transmitting a beacon, deaf, or listening, and
 * the only way out is a frame that passed a CRC. TRIG_SEND and TRIG_RECEIVE
 * still mean another band and nothing else does — but now they mean it
 * because of arithmetic rather than because of a length gate, so the line
 * below is the same line for a better reason.
 */
void wear_link(const link_sm_t *sm)
{
    ui_link_t l;
    switch (sm->state) {
    case LINK_IDLE:
        l = (sm->trig.state == TRIG_SEND || sm->trig.state == TRIG_RECEIVE)
            ? UI_LINK_RENDEZVOUS : UI_LINK_IDLE;
        break;
    case LINK_COMPLETE:
    case LINK_ABORT:
        l = UI_LINK_IDLE;
        break;
    default:
        l = sm->role == LINK_ROLE_SENDER ? UI_LINK_SENDING : UI_LINK_RECEIVING;
        break;
    }
    if (l != s_link) {
        static const ui_event_t map[] = {
            UI_EV_LINK_IDLE, UI_EV_LINK_RENDEZVOUS, UI_EV_LINK_SENDING, UI_EV_LINK_RECEIVING
        };
        s_link = l;
        /* the clock is the caller's; a poll follows within the loop */
        wear_post(map[l]);
    }
}

/* ---- lifecycle ---------------------------------------------------------- */

void wear_init(uint64_t now_us)
{
    uint32_t now = ms_of(now_us);

    led_init();
#if !HANDOFF_BENCH_SYNC
    gpio_init(WEAR_PIN_BUTTON);
    gpio_set_dir(WEAR_PIN_BUTTON, GPIO_IN);
    gpio_pull_up(WEAR_PIN_BUTTON);       /* belt to R15's braces; a bare Pico has no R15 */
#endif
    ui_init(&s_ui, now);
    button_init(&s_btn, now);
    memset((void *)s_posted, 0, sizeof s_posted);
    s_link = UI_LINK_IDLE;
    s_batt = UI_BATT_OK;
    event(UI_EV_BOOT, now);
}

void wear_poll(uint64_t now_us)
{
    uint32_t now = ms_of(now_us);
    ui_out_t out;
    int i;

    for (i = 0; i < (int)UI_EV_COUNT; i++) {
        if (!s_posted[i]) continue;
        s_posted[i] = 0u;
        event((ui_event_t)i, now);
    }

    if (s_ble_valid) {
        /* no owner: blue until someone pairs, forever (band-ownership §1) */
        bool pairing = !s_ble_connected && !s_ble_bonded;
        if (pairing != s_pairing) {
            s_pairing = pairing;
            event(pairing ? UI_EV_PAIRING : UI_EV_NOT_PAIRING, now);
        }
        if (s_ble_connected && !s_connected) event(UI_EV_PHONE_CONNECTED, now);
        s_connected = s_ble_connected;
    }

    if (s_power_valid) {
        ui_batt_t b = battery_level(s_vsys_mv, s_on_usb, s_batt);
        if (b != s_batt) {
            s_batt = b;
            event(b == UI_BATT_CRITICAL ? UI_EV_BATTERY_CRITICAL
                  : b == UI_BATT_LOW    ? UI_EV_BATTERY_LOW
                                        : UI_EV_BATTERY_OK, now);
        }
    }

    if (s_haptic_dirty) {
        s_haptic_dirty = false;
        ui_set_haptic(&s_ui, s_haptic);
    }

    button(now);

    ui_step(&s_ui, now, &out);
    if (!s_out_valid || memcmp(&out, &s_out, sizeof out) != 0) {
        s_out = out;
        s_out_valid = true;
        led_set(out.led);
        motor_set(out.motor);
        if (s_trace)
            printf("ui %lu led %u %u %u motor %u\n", (unsigned long)now,
                   out.led.r, out.led.g, out.led.b, out.motor ? 1u : 0u);
    }
}

/* ---- outputs ------------------------------------------------------------ */

bool wear_take_reset_request(void)
{
    if (!s_reset) return false;
    s_reset = false;
    return true;
}

/* ---- the bench console -------------------------------------------------- */

void wear_trace(bool on) { s_trace = on; }
bool wear_tracing(void)  { return s_trace; }

void wear_inject(ui_event_t ev) { wear_post(ev); }

void wear_press(uint32_t ms)
{
    s_press_ms = ms;
    s_press_armed = true;
}
