/*
 * See ui.h. The table lives here as data; the code underneath it is one
 * evaluator for LED patterns and one for motor patterns.
 */
#include "ui.h"

#include <string.h>

/* ---- the palette ---------------------------------------------------- */

#define OFF    { 0,   0,   0   }
#define WHITE  { 255, 255, 255 }
#define BLUE   { 0,   0,   255 }
#define GREEN  { 0,   255, 0   }
#define AMBER  { 255, 120, 0   }
#define RED    { 255, 0,   0   }
#define PURPLE { 160, 0,   255 }

#define PAT(name, period, ...)                                                 \
    static const ui_led_step_t name##_steps[] = { __VA_ARGS__ };               \
    static const ui_led_pat_t  name = {                                        \
        name##_steps, (uint8_t)(sizeof name##_steps / sizeof name##_steps[0]), \
        (period) }

/* ---- backgrounds: period > 0, repeat forever ------------------------ */

PAT(k_bg_fault,      250,  { RED, 125 }, { BLUE, 125 });
PAT(k_bg_rendezvous, 100,  { WHITE, 50 }, { OFF, 50 });
PAT(k_bg_link,       0,    { WHITE, 0 });                    /* solid, held */
PAT(k_bg_pairing,    2000, { BLUE, 80 }, { OFF, 80 }, { BLUE, 80 });
PAT(k_bg_dev,        2000, { PURPLE, 100 });
PAT(k_bg_critical,   5000, { RED, 100 }, { OFF, 100 }, { RED, 100 }, { OFF, 100 }, { RED, 100 });
PAT(k_bg_low,        10000, { RED, 100 });
PAT(k_bg_held,       5000, { AMBER, 100 });

/* ---- foregrounds: period 0, play once ------------------------------- */

PAT(k_fg_boot,       0, { WHITE, 200 });
PAT(k_fg_connected,  0, { BLUE, 2000 });
PAT(k_fg_written,    0, { GREEN, 100 }, { OFF, 100 }, { GREEN, 100 });
PAT(k_fg_complete,   0, { GREEN, 2000 });
/* complete's 2 s and then the blip: a superset, because FORWARDED lands a
 * few milliseconds after COMPLETE and replaces it. */
PAT(k_fg_forwarded,  0, { GREEN, 2000 }, { OFF, 200 }, { GREEN, 100 });
PAT(k_fg_blip,       0, { GREEN, 100 });                      /* forwarded late */
PAT(k_fg_abort,      0, { AMBER, 100 }, { OFF, 100 }, { AMBER, 100 }, { OFF, 100 }, { AMBER, 100 });
PAT(k_fg_no_card,    0, { AMBER, 100 }, { OFF, 100 }, { AMBER, 100 });
PAT(k_fg_bond_clear, 0, { PURPLE, 100 }, { OFF, 100 }, { PURPLE, 100 }, { OFF, 100 },
                        { PURPLE, 100 }, { OFF, 100 }, { PURPLE, 100 });
PAT(k_fg_identify,   0, { WHITE, 100 }, { OFF, 100 }, { WHITE, 100 }, { OFF, 100 }, { WHITE, 100 });
PAT(k_fg_batt_good,  0, { GREEN, 300 });
PAT(k_fg_batt_mid,   0, { AMBER, 300 });
PAT(k_fg_batt_low,   0, { RED, 300 });

/* ---- the motor ------------------------------------------------------ */

/* A coin ERM takes ~50 ms to spin up, so 100 ms is the shortest pulse that
 * is a pulse and not a click. Two of them 120 ms apart still read as two. */
static const uint16_t k_m_tap[]    = { 100 };
static const uint16_t k_m_double[] = { 100, 120, 100 };
static const uint16_t k_m_triple[] = { 100, 120, 100, 120, 100 };
static const uint16_t k_m_buzz[]   = { 250 };
static const uint16_t k_m_long[]   = { 600 };

#define MPAT(name, arr) static const ui_motor_pat_t name = { arr, (uint8_t)(sizeof arr / sizeof arr[0]) }
MPAT(k_motor_tap,    k_m_tap);
MPAT(k_motor_double, k_m_double);
MPAT(k_motor_triple, k_m_triple);
MPAT(k_motor_buzz,   k_m_buzz);
MPAT(k_motor_long,   k_m_long);

/* ---- evaluation ----------------------------------------------------- */

static uint32_t pat_len(const ui_led_pat_t *p)
{
    uint32_t t = 0;
    uint8_t i;
    for (i = 0; i < p->n; i++) t += p->steps[i].ms;
    return t;
}

/* The colour a pattern shows at elapsed ms; false once a one-shot is over. */
static bool pat_at(const ui_led_pat_t *p, uint32_t t, ui_rgb_t *c)
{
    uint8_t i;
    if (p->period_ms) t %= p->period_ms;
    for (i = 0; i < p->n; i++) {
        const ui_led_step_t *s = &p->steps[i];
        if (s->ms == 0 || t < s->ms) { *c = s->c; return true; }
        t -= s->ms;
    }
    memset(c, 0, sizeof *c);
    return p->period_ms != 0;      /* a background rests dark; a foreground ends */
}

static uint32_t motor_len(const ui_motor_pat_t *p)
{
    uint32_t t = 0;
    uint8_t i;
    for (i = 0; i < p->n; i++) t += p->ms[i];
    return t;
}

static bool motor_at(const ui_motor_pat_t *p, uint32_t t, bool *on)
{
    uint8_t i;
    for (i = 0; i < p->n; i++) {
        if (t < p->ms[i]) { *on = (i % 2u) == 0; return true; }
        t -= p->ms[i];
    }
    *on = false;
    return false;
}

static const ui_led_pat_t *background(const ui_t *u)
{
    if (u->fault) return &k_bg_fault;
    switch (u->link) {
    case UI_LINK_RENDEZVOUS: return &k_bg_rendezvous;
    case UI_LINK_SENDING:
    case UI_LINK_RECEIVING:  return &k_bg_link;
    default: break;
    }
    if (u->pairing)                  return &k_bg_pairing;
    if (u->dev)                      return &k_bg_dev;
    if (u->batt == UI_BATT_CRITICAL) return &k_bg_critical;
    if (u->batt == UI_BATT_LOW)      return &k_bg_low;
    if (u->held)                     return &k_bg_held;
    return NULL;
}

/* ---- events --------------------------------------------------------- */

static void fg(ui_t *u, const ui_led_pat_t *p, uint32_t now)
{
    u->fg = p;
    u->fg_at = now;
}

/* Request a motor pattern. Gated by the link: during a handshake it waits,
 * and only the latest request waits. */
static void motor(ui_t *u, const ui_motor_pat_t *p, uint32_t now)
{
    if (ui_link_active(u)) {
        u->motor_pending = p;
        return;
    }
    u->motor = p;
    u->motor_at = now;
}

static void motor_gated(ui_t *u, const ui_motor_pat_t *p, uint32_t now)
{
    if (u->haptic) motor(u, p, now);
}

static void set_link(ui_t *u, ui_link_t l, uint32_t now)
{
    bool was = ui_link_active(u);
    u->link = l;
    if (ui_link_active(u)) {
        /* cut anything playing: the pad is about to be listened to */
        u->motor = NULL;
    } else if (was && u->motor_pending) {
        u->motor = u->motor_pending;
        u->motor_at = now;
        u->motor_pending = NULL;
    }
}

void ui_init(ui_t *u, uint32_t now_ms)
{
    (void)now_ms;
    memset(u, 0, sizeof *u);
    u->haptic = true;
    u->last_event = UI_EV_COUNT;
}

void ui_set_haptic(ui_t *u, bool on) { u->haptic = on; }

bool ui_link_active(const ui_t *u) { return u->link != UI_LINK_IDLE; }

void ui_event(ui_t *u, ui_event_t ev, uint32_t now)
{
    u->last_event = ev;
    u->events++;

    switch (ev) {
    case UI_EV_BOOT:
        fg(u, &k_fg_boot, now);
        motor(u, &k_motor_tap, now);
        break;

    case UI_EV_PAIRING:         u->pairing = true;  break;
    case UI_EV_NOT_PAIRING:     u->pairing = false; break;
    case UI_EV_PHONE_CONNECTED: fg(u, &k_fg_connected, now); break;
    case UI_EV_CARD_WRITTEN:
        fg(u, &k_fg_written, now);
        motor_gated(u, &k_motor_tap, now);
        break;

    case UI_EV_LINK_IDLE:       set_link(u, UI_LINK_IDLE, now);       break;
    case UI_EV_LINK_RENDEZVOUS: set_link(u, UI_LINK_RENDEZVOUS, now); break;
    case UI_EV_LINK_SENDING:    set_link(u, UI_LINK_SENDING, now);    break;
    case UI_EV_LINK_RECEIVING:  set_link(u, UI_LINK_RECEIVING, now);  break;

    case UI_EV_LINK_COMPLETE:
        fg(u, &k_fg_complete, now);
        motor_gated(u, &k_motor_double, now);
        break;
    case UI_EV_LINK_FORWARDED:
        /* inside complete's 2 s: extend it, so the green does not restart.
         * Later than that (a held card the phone finally took): one blip. */
        if (u->fg == &k_fg_complete && now - u->fg_at < pat_len(&k_fg_complete))
            u->fg = &k_fg_forwarded;
        else
            fg(u, &k_fg_blip, now);
        u->held = false;
        break;
    case UI_EV_LINK_HELD:     u->held = true;  break;
    case UI_EV_LINK_RELEASED: u->held = false; break;
    case UI_EV_LINK_ABORT:
        fg(u, &k_fg_abort, now);
        motor_gated(u, &k_motor_long, now);
        break;
    case UI_EV_LINK_NO_CARD:
        fg(u, &k_fg_no_card, now);
        break;

    case UI_EV_BATTERY_OK:
        u->batt = UI_BATT_OK;
        break;
    case UI_EV_BATTERY_LOW:
        /* once, at the crossing; a repeat while already low is silent */
        if (u->batt == UI_BATT_OK) motor(u, &k_motor_buzz, now);
        u->batt = UI_BATT_LOW;
        break;
    case UI_EV_BATTERY_CRITICAL:
        if (u->batt != UI_BATT_CRITICAL) motor(u, &k_motor_long, now);
        u->batt = UI_BATT_CRITICAL;
        break;

    case UI_EV_FAULT:        u->fault = true; break;
    case UI_EV_BOND_CLEARED:
        fg(u, &k_fg_bond_clear, now);
        motor(u, &k_motor_buzz, now);
        break;
    case UI_EV_DEV_ON:       u->dev = true;  break;
    case UI_EV_DEV_OFF:      u->dev = false; break;
    case UI_EV_IDENTIFY:
        fg(u, &k_fg_identify, now);
        motor(u, &k_motor_triple, now);
        break;
    case UI_EV_BATTERY_SHOW_GOOD: fg(u, &k_fg_batt_good, now); break;
    case UI_EV_BATTERY_SHOW_MID:  fg(u, &k_fg_batt_mid, now);  break;
    case UI_EV_BATTERY_SHOW_LOW:  fg(u, &k_fg_batt_low, now);  break;
    case UI_EV_HOLD_REACHED:
        motor(u, &k_motor_tap, now);
        break;

    default:
        break;
    }
}

void ui_step(const ui_t *u, uint32_t now, ui_out_t *out)
{
    const ui_led_pat_t *bg;
    bool shown = false;

    memset(out, 0, sizeof *out);

    /*
     * A finished one-shot is not cleared, only ignored, so this stays a pure
     * function of (state, now) and a test can walk the same interval twice.
     * The cost is that a one-shot nothing has replaced replays once every
     * 2^32 ms — a 200 ms flash every 49.7 days, which is not a concern.
     */
    if (u->fg && now - u->fg_at < pat_len(u->fg))
        shown = pat_at(u->fg, now - u->fg_at, &out->led);
    if (!shown) {
        bg = background(u);
        if (bg) pat_at(bg, now, &out->led);   /* backgrounds are phase-free */
    }

    if (u->motor && !ui_link_active(u) && now - u->motor_at < motor_len(u->motor))
        motor_at(u->motor, now - u->motor_at, &out->motor);
}

/* ---- names ---------------------------------------------------------- */

static const char *const k_names[UI_EV_COUNT] = {
    "boot",
    "pairing", "not-pairing", "phone-connected", "card-written",
    "link-idle", "link-rendezvous", "link-sending", "link-receiving",
    "link-complete", "link-forwarded", "link-held", "link-released",
    "link-abort", "link-no-card",
    "battery-ok", "battery-low", "battery-critical",
    "fault", "bond-cleared", "dev-on", "dev-off", "identify",
    "battery-show-good", "battery-show-mid", "battery-show-low",
    "hold-reached",
};

const char *ui_event_name(ui_event_t ev)
{
    return ev < UI_EV_COUNT ? k_names[ev] : "?";
}

int ui_event_parse(const char *name)
{
    int i;
    for (i = 0; i < (int)UI_EV_COUNT; i++)
        if (strcmp(name, k_names[i]) == 0) return i;
    return -1;
}
