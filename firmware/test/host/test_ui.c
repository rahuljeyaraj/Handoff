/*
 * lib/ui: the LED and motor table, walked with a millisecond clock. Every
 * row of ui.h's table has a check here that the pattern is what it says,
 * and the things the table does not say — the motor's link gate, the
 * priority between backgrounds, a foreground masking a background — have
 * their own.
 */
#include "hf_test.h"
#include "tests.h"
#include "ui.h"

#include <string.h>

/* One output change: when, and what it changed to. */
typedef struct {
    uint32_t at;
    ui_out_t out;
} edge_t;

/* Sample every millisecond from `from` to `to` and record the changes. The
 * first sample is always recorded, so edges[0] is the state at `from`. */
static int trace(ui_t *u, uint32_t from, uint32_t to, edge_t *edges, int max)
{
    int n = 0;
    uint32_t t;
    ui_out_t prev, cur;
    memset(&prev, 0xff, sizeof prev);
    for (t = from; t != to; t++) {
        ui_step(u, t, &cur);
        if (memcmp(&cur, &prev, sizeof cur) != 0) {
            if (n < max) { edges[n].at = t; edges[n].out = cur; }
            n++;
            prev = cur;
        }
    }
    return n;
}

static bool rgb_is(ui_rgb_t c, uint8_t r, uint8_t g, uint8_t b)
{
    return c.r == r && c.g == g && c.b == b;
}
#define IS_OFF(c)    rgb_is(c, 0, 0, 0)
#define IS_WHITE(c)  rgb_is(c, 255, 255, 255)
#define IS_BLUE(c)   rgb_is(c, 0, 0, 255)
#define IS_GREEN(c)  rgb_is(c, 0, 255, 0)
#define IS_AMBER(c)  rgb_is(c, 255, 120, 0)
#define IS_RED(c)    rgb_is(c, 255, 0, 0)
#define IS_PURPLE(c) rgb_is(c, 160, 0, 255)

/* How many milliseconds in [from, to) the motor is on, and how many rising
 * edges it has. */
static void motor_stats(ui_t *u, uint32_t from, uint32_t to, uint32_t *on_ms, int *pulses)
{
    uint32_t t;
    bool prev = false;
    ui_out_t o;
    *on_ms = 0;
    *pulses = 0;
    for (t = from; t != to; t++) {
        ui_step(u, t, &o);
        if (o.motor) (*on_ms)++;
        if (o.motor && !prev) (*pulses)++;
        prev = o.motor;
    }
}

/* Count rising edges of "LED not off" in [from, to). */
static int led_flashes(ui_t *u, uint32_t from, uint32_t to)
{
    uint32_t t;
    bool prev = false;
    int n = 0;
    ui_out_t o;
    for (t = from; t != to; t++) {
        bool lit;
        ui_step(u, t, &o);
        lit = !IS_OFF(o.led);
        if (lit && !prev) n++;
        prev = lit;
    }
    return n;
}

void test_ui(void)
{
    edge_t e[64];
    ui_t u;
    int n;

    hf_begin("ui: the steady state is dark and silent");
    {
        ui_init(&u, 0);
        n = trace(&u, 0, 10000, e, 64);
        HF_EQ_INT(n, 1);
        HF_CHECK(IS_OFF(e[0].out.led));
        HF_CHECK(!e[0].out.motor);
    }

    hf_begin("ui: boot is white for 200 ms and one 100 ms tap");
    {
        ui_init(&u, 0);
        ui_event(&u, UI_EV_BOOT, 0);
        n = trace(&u, 0, 1000, e, 64);
        HF_EQ_INT(n, 3);
        HF_CHECK(IS_WHITE(e[0].out.led) && e[0].out.motor);
        HF_EQ_INT(e[1].at, 100);  HF_CHECK(IS_WHITE(e[1].out.led) && !e[1].out.motor);
        HF_EQ_INT(e[2].at, 200);  HF_CHECK(IS_OFF(e[2].out.led));
    }

    hf_begin("ui: pairing is a blue double-flash every 2 s, 80 ms on");
    {
        ui_init(&u, 0);
        ui_event(&u, UI_EV_PAIRING, 0);
        n = trace(&u, 0, 2000, e, 64);
        HF_EQ_INT(n, 4);
        HF_CHECK(IS_BLUE(e[0].out.led));  HF_EQ_INT(e[0].at, 0);
        HF_CHECK(IS_OFF(e[1].out.led));   HF_EQ_INT(e[1].at, 80);
        HF_CHECK(IS_BLUE(e[2].out.led));  HF_EQ_INT(e[2].at, 160);
        HF_CHECK(IS_OFF(e[3].out.led));   HF_EQ_INT(e[3].at, 240);
        HF_EQ_INT(led_flashes(&u, 0, 10000), 10);
        ui_event(&u, UI_EV_NOT_PAIRING, 10000);
        HF_EQ_INT(led_flashes(&u, 10000, 20000), 0);
    }

    hf_begin("ui: phone connected is 2 s of solid blue over the background, then the background");
    {
        ui_init(&u, 0);
        ui_event(&u, UI_EV_PAIRING, 0);
        ui_event(&u, UI_EV_PHONE_CONNECTED, 500);
        n = trace(&u, 500, 2500, e, 64);
        HF_EQ_INT(n, 1);
        HF_CHECK(IS_BLUE(e[0].out.led));
        /* 2500 is 500 into the pairing period: between its two flashes */
        n = trace(&u, 2500, 4500, e, 64);
        HF_CHECK(n >= 3);
        HF_CHECK(IS_OFF(e[0].out.led));
        HF_EQ_INT(e[1].at, 4000);
        HF_CHECK(IS_BLUE(e[1].out.led));
    }

    hf_begin("ui: card written is a green double-flash and a tap");
    {
        uint32_t on; int pulses;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_CARD_WRITTEN, 0);
        HF_EQ_INT(led_flashes(&u, 0, 1000), 2);
        motor_stats(&u, 0, 1000, &on, &pulses);
        HF_EQ_INT(pulses, 1);
        HF_EQ_INT(on, 100);
    }

    hf_begin("ui: rendezvous is white at 10 Hz, sending and receiving are solid white");
    {
        ui_init(&u, 0);
        ui_event(&u, UI_EV_LINK_RENDEZVOUS, 0);
        HF_EQ_INT(led_flashes(&u, 0, 1000), 10);
        n = trace(&u, 0, 100, e, 64);
        HF_EQ_INT(n, 2);
        HF_CHECK(IS_WHITE(e[0].out.led));
        HF_EQ_INT(e[1].at, 50);
        ui_event(&u, UI_EV_LINK_SENDING, 1000);
        n = trace(&u, 1000, 4000, e, 64);
        HF_EQ_INT(n, 1);
        HF_CHECK(IS_WHITE(e[0].out.led));
        ui_event(&u, UI_EV_LINK_RECEIVING, 4000);
        n = trace(&u, 4000, 7000, e, 64);
        HF_EQ_INT(n, 1);
        HF_CHECK(IS_WHITE(e[0].out.led));
        ui_event(&u, UI_EV_LINK_IDLE, 7000);
        n = trace(&u, 7000, 8000, e, 64);
        HF_EQ_INT(n, 1);
        HF_CHECK(IS_OFF(e[0].out.led));
    }

    hf_begin("ui: the motor never runs while the link is active; a request waits for idle");
    {
        uint32_t on; int pulses;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_LINK_SENDING, 0);
        ui_event(&u, UI_EV_IDENTIFY, 100);          /* 3 taps, ungated */
        motor_stats(&u, 0, 3000, &on, &pulses);
        HF_EQ_INT(on, 0);
        ui_event(&u, UI_EV_LINK_IDLE, 3000);
        motor_stats(&u, 3000, 5000, &on, &pulses);
        HF_EQ_INT(pulses, 3);
        HF_EQ_INT(on, 300);
    }

    hf_begin("ui: a motor pattern in progress is cut when a handshake starts");
    {
        uint32_t on; int pulses;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_BOND_CLEARED, 0);         /* 250 ms buzz */
        motor_stats(&u, 0, 50, &on, &pulses);
        HF_EQ_INT(on, 50);
        ui_event(&u, UI_EV_LINK_RENDEZVOUS, 50);
        motor_stats(&u, 50, 1000, &on, &pulses);
        HF_EQ_INT(on, 0);
        /* and it does not resume afterwards: it was playing, not pending */
        ui_event(&u, UI_EV_LINK_IDLE, 1000);
        motor_stats(&u, 1000, 2000, &on, &pulses);
        HF_EQ_INT(on, 0);
    }

    hf_begin("ui: only the latest deferred buzz survives the handshake");
    {
        uint32_t on; int pulses;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_LINK_SENDING, 0);
        ui_event(&u, UI_EV_IDENTIFY, 100);            /* 3 taps */
        ui_event(&u, UI_EV_HOLD_REACHED, 200);        /* 1 tap: replaces it */
        ui_event(&u, UI_EV_LINK_IDLE, 1000);
        motor_stats(&u, 1000, 3000, &on, &pulses);
        HF_EQ_INT(pulses, 1);
    }

    hf_begin("ui: complete then forwarded is one unbroken 2 s green, a gap, a blip; double tap");
    {
        uint32_t on; int pulses;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_LINK_COMPLETE, 0);
        ui_event(&u, UI_EV_LINK_FORWARDED, 3);      /* a link_tick later */
        n = trace(&u, 0, 3000, e, 64);
        /* edges: green+motor, motor off, motor on, motor off, off, green, off */
        HF_EQ_INT(n, 7);
        HF_CHECK(IS_GREEN(e[0].out.led) && e[0].out.motor);
        HF_EQ_INT(e[1].at, 100);  HF_CHECK(!e[1].out.motor && IS_GREEN(e[1].out.led));
        HF_EQ_INT(e[2].at, 220);  HF_CHECK(e[2].out.motor);
        HF_EQ_INT(e[3].at, 320);  HF_CHECK(!e[3].out.motor);
        HF_EQ_INT(e[4].at, 2000); HF_CHECK(IS_OFF(e[4].out.led));
        HF_EQ_INT(e[5].at, 2200); HF_CHECK(IS_GREEN(e[5].out.led));
        HF_EQ_INT(e[6].at, 2300); HF_CHECK(IS_OFF(e[6].out.led));
        motor_stats(&u, 0, 3000, &on, &pulses);
        HF_EQ_INT(pulses, 2);
        HF_EQ_INT(on, 200);
    }

    hf_begin("ui: a card held for the phone shows an amber blip every 5 s after the green");
    {
        ui_init(&u, 0);
        ui_event(&u, UI_EV_LINK_COMPLETE, 0);
        ui_event(&u, UI_EV_LINK_HELD, 3);
        n = trace(&u, 0, 2000, e, 64);
        HF_CHECK(IS_GREEN(e[0].out.led));
        HF_EQ_INT(led_flashes(&u, 2000, 22000), 4);
        n = trace(&u, 5000, 5200, e, 64);
        HF_CHECK(IS_AMBER(e[0].out.led));
        HF_EQ_INT(e[1].at, 5100);
        /* the phone takes it, 30 s on: one green blip and dark */
        ui_event(&u, UI_EV_LINK_FORWARDED, 30000);
        n = trace(&u, 30000, 40000, e, 64);
        HF_EQ_INT(n, 2);
        HF_CHECK(IS_GREEN(e[0].out.led));
        HF_EQ_INT(e[1].at, 30100);
        HF_CHECK(IS_OFF(e[1].out.led));
    }

    hf_begin("ui: abort is three amber flashes and one long buzz");
    {
        uint32_t on; int pulses;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_LINK_ABORT, 0);
        HF_EQ_INT(led_flashes(&u, 0, 1000), 3);
        motor_stats(&u, 0, 1000, &on, &pulses);
        HF_EQ_INT(pulses, 1);
        HF_EQ_INT(on, 600);
    }

    hf_begin("ui: nothing to give is an amber double-flash, no motor");
    {
        uint32_t on; int pulses;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_LINK_NO_CARD, 0);
        HF_EQ_INT(led_flashes(&u, 0, 1000), 2);
        motor_stats(&u, 0, 1000, &on, &pulses);
        HF_EQ_INT(on, 0);
    }

    hf_begin("ui: the vibrate setting masks the card haptics and nothing else");
    {
        uint32_t on; int pulses;
        ui_init(&u, 0);
        ui_set_haptic(&u, false);
        ui_event(&u, UI_EV_LINK_COMPLETE, 0);
        motor_stats(&u, 0, 1000, &on, &pulses);
        HF_EQ_INT(on, 0);
        HF_CHECK(led_flashes(&u, 0, 1000) == 1);       /* the LED still shows it */
        ui_event(&u, UI_EV_LINK_ABORT, 1000);
        motor_stats(&u, 1000, 2000, &on, &pulses);
        HF_EQ_INT(on, 0);
        ui_event(&u, UI_EV_CARD_WRITTEN, 2000);
        motor_stats(&u, 2000, 3000, &on, &pulses);
        HF_EQ_INT(on, 0);
        ui_event(&u, UI_EV_IDENTIFY, 3000);
        motor_stats(&u, 3000, 4000, &on, &pulses);
        HF_EQ_INT(pulses, 3);
        ui_event(&u, UI_EV_BOND_CLEARED, 4000);
        motor_stats(&u, 4000, 5000, &on, &pulses);
        HF_EQ_INT(on, 250);
    }

    hf_begin("ui: battery low buzzes once at the crossing, then blips red every 10 s");
    {
        uint32_t on; int pulses;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_BATTERY_LOW, 0);
        motor_stats(&u, 0, 1000, &on, &pulses);
        HF_EQ_INT(on, 250);
        ui_event(&u, UI_EV_BATTERY_LOW, 1000);          /* the sampler repeats itself */
        motor_stats(&u, 1000, 2000, &on, &pulses);
        HF_EQ_INT(on, 0);
        HF_EQ_INT(led_flashes(&u, 2000, 32000), 3);
        n = trace(&u, 10000, 10200, e, 64);
        HF_CHECK(IS_RED(e[0].out.led));
        HF_EQ_INT(e[1].at, 10100);
        /* critical: one long, three red every 5 s */
        ui_event(&u, UI_EV_BATTERY_CRITICAL, 40000);
        motor_stats(&u, 40000, 41000, &on, &pulses);
        HF_EQ_INT(on, 600);
        HF_EQ_INT(led_flashes(&u, 40000, 50000), 6);
        /* back on the charger: dark, silent, and low buzzes again next time */
        ui_event(&u, UI_EV_BATTERY_OK, 50000);
        n = trace(&u, 50000, 60000, e, 64);
        HF_EQ_INT(n, 1);
        HF_CHECK(IS_OFF(e[0].out.led));
        ui_event(&u, UI_EV_BATTERY_LOW, 60000);
        motor_stats(&u, 60000, 61000, &on, &pulses);
        HF_EQ_INT(on, 250);
    }

    hf_begin("ui: backgrounds by priority — fault > link > pairing > dev > critical > low > held");
    {
        ui_out_t o;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_LINK_HELD, 0);
        ui_step(&u, 0, &o);       HF_CHECK(IS_AMBER(o.led));
        ui_event(&u, UI_EV_BATTERY_LOW, 0);
        ui_step(&u, 0, &o);       HF_CHECK(IS_RED(o.led));
        ui_step(&u, 5000, &o);    HF_CHECK(IS_OFF(o.led));      /* not held's blip */
        ui_event(&u, UI_EV_BATTERY_CRITICAL, 0);
        ui_step(&u, 200, &o);     HF_CHECK(IS_RED(o.led));      /* second of three */
        ui_event(&u, UI_EV_DEV_ON, 0);
        ui_step(&u, 200, &o);     HF_CHECK(IS_OFF(o.led));
        ui_step(&u, 2050, &o);    HF_CHECK(IS_PURPLE(o.led));
        ui_event(&u, UI_EV_PAIRING, 0);
        ui_step(&u, 2050, &o);    HF_CHECK(IS_BLUE(o.led));
        ui_event(&u, UI_EV_LINK_SENDING, 0);
        ui_step(&u, 2050, &o);    HF_CHECK(IS_WHITE(o.led));
        ui_event(&u, UI_EV_FAULT, 0);
        ui_step(&u, 2050, &o);    HF_CHECK(IS_RED(o.led));
        ui_step(&u, 2200, &o);    HF_CHECK(IS_BLUE(o.led));
        HF_EQ_INT(led_flashes(&u, 0, 1000), 1);           /* red/blue alternate: never dark */
        ui_event(&u, UI_EV_DEV_OFF, 0);
        ui_event(&u, UI_EV_LINK_IDLE, 0);
        ui_step(&u, 2050, &o);    HF_CHECK(IS_RED(o.led));      /* fault is sticky */
    }

    hf_begin("ui: bond cleared is four purple flashes and a buzz; identify three white and three taps");
    {
        uint32_t on; int pulses;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_BOND_CLEARED, 0);
        HF_EQ_INT(led_flashes(&u, 0, 2000), 4);
        motor_stats(&u, 0, 2000, &on, &pulses);
        HF_EQ_INT(pulses, 1); HF_EQ_INT(on, 250);
        ui_event(&u, UI_EV_IDENTIFY, 2000);
        HF_EQ_INT(led_flashes(&u, 2000, 4000), 3);
        motor_stats(&u, 2000, 4000, &on, &pulses);
        HF_EQ_INT(pulses, 3); HF_EQ_INT(on, 300);
    }

    hf_begin("ui: the battery check shows one 300 ms colour by level and stays silent");
    {
        uint32_t on; int pulses;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_BATTERY_SHOW_GOOD, 0);
        n = trace(&u, 0, 1000, e, 64);
        HF_EQ_INT(n, 2); HF_CHECK(IS_GREEN(e[0].out.led)); HF_EQ_INT(e[1].at, 300);
        ui_event(&u, UI_EV_BATTERY_SHOW_MID, 1000);
        n = trace(&u, 1000, 2000, e, 64);
        HF_CHECK(IS_AMBER(e[0].out.led));
        ui_event(&u, UI_EV_BATTERY_SHOW_LOW, 2000);
        n = trace(&u, 2000, 3000, e, 64);
        HF_CHECK(IS_RED(e[0].out.led));
        motor_stats(&u, 0, 3000, &on, &pulses);
        HF_EQ_INT(on, 0);
    }

    hf_begin("ui: a battery check with no reading is white, never green");
    {
        uint32_t on; int pulses;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_BATTERY_SHOW_UNKNOWN, 0);
        n = trace(&u, 0, 1000, e, 64);
        HF_EQ_INT(n, 2); HF_CHECK(IS_WHITE(e[0].out.led)); HF_EQ_INT(e[1].at, 300);
        motor_stats(&u, 0, 1000, &on, &pulses);
        HF_EQ_INT(on, 0);
        HF_EQ_INT(ui_event_parse("battery-show-unknown"), (int)UI_EV_BATTERY_SHOW_UNKNOWN);
    }

    hf_begin("ui: a hold threshold is one tap");
    {
        uint32_t on; int pulses;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_HOLD_REACHED, 0);
        motor_stats(&u, 0, 1000, &on, &pulses);
        HF_EQ_INT(pulses, 1); HF_EQ_INT(on, 100);
        HF_EQ_INT(led_flashes(&u, 0, 1000), 0);
    }

    hf_begin("ui: a new foreground replaces the one playing");
    {
        ui_out_t o;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_PHONE_CONNECTED, 0);
        ui_event(&u, UI_EV_CARD_WRITTEN, 500);
        ui_step(&u, 550, &o);  HF_CHECK(IS_GREEN(o.led));
        ui_step(&u, 650, &o);  HF_CHECK(IS_OFF(o.led));      /* the gap, not blue */
        ui_step(&u, 750, &o);  HF_CHECK(IS_GREEN(o.led));
        ui_step(&u, 850, &o);  HF_CHECK(IS_OFF(o.led));      /* over, not blue again */
    }

    hf_begin("ui: the clock wrapping through zero does not restart or lose a pattern");
    {
        ui_out_t o;
        ui_init(&u, 0);
        ui_event(&u, UI_EV_LINK_COMPLETE, 0xFFFFFF00u);   /* 256 ms before the wrap */
        ui_step(&u, 0xFFFFFFF0u, &o); HF_CHECK(IS_GREEN(o.led));
        ui_step(&u, 0x00000010u, &o); HF_CHECK(IS_GREEN(o.led));    /* 272 ms in */
        ui_step(&u, 0x00000600u, &o); HF_CHECK(IS_GREEN(o.led));    /* 1792 ms in */
        ui_step(&u, 0x00000800u, &o); HF_CHECK(IS_OFF(o.led));      /* 2304 ms in */
    }

    hf_begin("ui: every event has a name and parses back");
    {
        int i;
        for (i = 0; i < (int)UI_EV_COUNT; i++) {
            const char *name = ui_event_name((ui_event_t)i);
            HF_CHECK(strcmp(name, "?") != 0);
            HF_EQ_INT(ui_event_parse(name), i);
        }
        HF_EQ_INT(ui_event_parse("no-such-event"), -1);
    }
}
