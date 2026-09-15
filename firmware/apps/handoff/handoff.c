/*
 * Handoff — the product image. M2 filled in its phone half; M14 binds the
 * body link. Same image: one hardcoded card at M2, the wearer's stored one
 * exchanged over the pad from here on.
 *
 * There is deliberately no apps/blelink and no apps/twoway. firmware-
 * architecture.md §4's app list is final, and the phone link and the body
 * link are not bring-up instruments like txgen or afe_sweep — they are the
 * two halves of the finished device. Building either anywhere else would
 * mean writing the provisioning path twice.
 *
 * WHAT M14 ADDS. beacon.c and link_sm.c, tested at M1 against two simulated
 * nodes, bound to hal_pico and free-running on two boards: SHOUT / SETTLE /
 * LISTEN until one hears the other, then the exchange, then back to the
 * trigger. Only the binding is new. The bench it was written on (15 Sep
 * 2026, the AFE not yet built) is the passive divider of M13 with both
 * boards on it: each GP2 -> 10 kOhm -> one node -> 540 Ohm -> GND, the node
 * to both GP26 pins. What the pad does there is what a body does: a
 * board's transmission is heard by the other one, and by itself.
 *
 * Exit criteria (development plan M14), and where each is read:
 *
 *   - 50 handshakes                    `s`: complete / abort counts
 *   - both hold the other's record     the `done` line prints the received
 *                                      card; `r` prints it again
 *   - a synchronised start still gives `y [N]`: a pulse on GP15 restarts
 *     exactly one sender                the trigger on both boards inside
 *                                      the same microsecond; each `done`
 *                                      line says which role it took
 *   - no pair ever transmits at once   every send is logged with its start
 *                                      and end on this board's clock, and
 *                                      the GP15 edge is logged on both, so
 *                                      the two logs share a reference
 *
 * THE ONE THING THE BINDING HAD TO GET RIGHT is in hal_pico.c, not here:
 * the ipc ring runs up to 4 ms behind the ADC, and a band that discarded
 * its own shout by arrival time would be handed the tail of it every time
 * its ears opened, and wake on itself every cycle. hal_rx_chips() cuts a
 * board's own sends out of the stream on the sample clock.
 *
 * TWO CONTEXTS. Everything that touches the radio runs inside BTstack:
 * the ble.c callbacks, the timers, the onboard LED. Measured at M2: a call
 * into the CYW43 from the main loop parked the core for tens of seconds.
 * The main loop therefore polls the link, the console and the wearer's
 * side (wear.c: the RGB LED, the motor, the button — plain pins) and
 * nothing else; a card that has arrived is handed to a BTstack timer,
 * which notifies the phone.
 *
 * WHAT THE WEARER SEES is lib/ui, bound in wear.c; ui.h carries the table.
 * A card that could not be handed to the phone (no bonded phone
 * subscribed) is HELD — the LED says so — and handed over when one
 * subscribes.
 *
 * CONSOLE (the wristband has none; the bench does):
 *
 *   g         link on / off (on at boot if a card is stored)
 *   y [N]     sync pulse on GP15 now, or N of them 3 s apart — only in a
 *             build with HANDOFF_BENCH_SYNC=ON; GP15 is the button otherwise
 *   d [ms]    pause after a handshake before the trigger re-arms (1500)
 *   w         store a bench card named after this board (needs no phone)
 *   r         print the last received card
 *   s         stats           z   zero the stats
 *   v         per-send lines on / off (on)
 *   c 40|200  carrier, kHz    h   this list
 *   u         trace the LED and motor on / off
 *   u <event> inject a ui.h event by name (u ? lists them)
 *   b <ms>    press the button for that long
 *
 * LOG LINES, all with times in microseconds on this board's clock:
 *
 *   tx S <start> <end>       a 10 ms shout was on the pad
 *   tx F <start> <end>       a frame was on the pad
 *   sync <t>                 the GP15 edge; the common reference
 *   cut <t>                  a send in flight was aborted at t (a sync
 *                            landed mid-send): its tx line's end is void
 *   st <t> <STATE> <role>    the link changed state
 *   done ...                 a handshake ended, and what it brought
 *   ui <ms> ...              wear.c's trace, in milliseconds (`u`)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/gpio.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "btstack_run_loop.h"

#include "ble.h"
#include "compact.h"
#include "config.h"
#include "flash.h"
#include "frag.h"
#include "hal_pico.h"
#include "link_sm.h"
#include "motor.h"
#include "power.h"
#include "store.h"
#include "vcard.h"
#include "wear.h"

#ifndef HANDOFF_BENCH_SYNC
#define HANDOFF_BENCH_SYNC 0
#endif

#define PIN_SYNC          15
#define POWER_SAMPLE_TICKS (10000u / LINK_TICK_MS)   /* VSYS every 10 s      */
#define SYNC_PULSE_US     200u
#define SYNC_PERIOD_US    3000000u
#define REIDLE_DEFAULT_US 1500000u
#define HEARTBEAT_US      10000000u
#define LINK_TICK_MS      250u

/*
 * The stand-in for a received contact. Deliberately not a round number and
 * deliberately awkward: a two-part name, an accented character, and a domain
 * that is in compact.c's dictionary, so what lands in the address book
 * exercises the codec rather than the happy path.
 */
static const char k_fake_card[] =
    "BEGIN:VCARD\r\n"
    "VERSION:3.0\r\n"
    "N:Smári;Björn;;;\r\n"
    "FN:Björn Smári\r\n"
    "ORG:Æther Ltd\r\n"
    "TITLE:Field Engineer\r\n"
    "TEL;TYPE=CELL:+354 555 1234\r\n"
    "EMAIL;TYPE=INTERNET:bjorn@gmail.com\r\n"
    "END:VCARD\r\n";

static store_t s_store;
static bool    s_flash_ok;

/* ---- the body link ---------------------------------------------------- */

static const hal_iface_t *s_hal;
static link_sm_t          s_sm;
static link_cfg_t         s_cfg;
static frag_tx_t          s_own;
static bool               s_have_own;
static bool               s_link_on;
static bool               s_parked;        /* COMPLETE/ABORT, waiting to re-arm */
static uint64_t           s_reidle_at;
static uint32_t           s_reidle_us = REIDLE_DEFAULT_US;
static bool               s_verbose = true;

/* written in the BTstack context, read by the main loop */
static volatile bool      s_own_dirty;     /* the store changed: re-split   */
static volatile int       s_force_role = -1;

/* written by the main loop, read by the BTstack timer */
static volatile bool      s_rx_ready;      /* a card arrived: notify the phone */
static bool               s_rx_pending;    /* ...and could not be: held for one */
static volatile bool      s_status_dirty;
static char               s_rx_text[BLE_VCARD_MAX];
static size_t             s_rx_text_len;

#if HANDOFF_BENCH_SYNC
/* the GP15 edge, from its interrupt */
static volatile bool      s_sync_hit;
static volatile uint64_t  s_sync_us;
static uint32_t           s_sync_left;     /* pulses still to send, `y N`   */
static uint64_t           s_sync_next_us;
#endif

typedef struct {
    uint64_t since;
    uint32_t complete, abort;
    uint32_t as_sender, as_receiver;
    uint32_t syncs;
    uint32_t dur_max_ms, dur_sum_ms;
    uint32_t sends;                        /* shouts + frames on the pad     */
    uint32_t shouts_at_zero, frames_at_zero;
    uint32_t good_at_zero, bad_at_zero, turns_at_zero;
} stats_t;

static stats_t s_st;

/* ---------------------------------------------------------------------- */

static uint32_t elapsed_s(uint64_t since)
{
    return (uint32_t)((hal_now_us(s_hal) - since) / 1000000u);
}

static void zero_stats(void)
{
    memset(&s_st, 0, sizeof s_st);
    s_st.since          = hal_now_us(s_hal);
    s_st.shouts_at_zero = s_sm.trig.shouts;
    s_st.frames_at_zero = s_sm.frames_sent;
    s_st.good_at_zero   = s_sm.frames_rx_good;
    s_st.bad_at_zero    = s_sm.frames_rx_bad;
    s_st.turns_at_zero  = s_sm.turnarounds;
}

static const char *role_name(link_role_t r)
{
    return r == LINK_ROLE_SENDER ? "S" : r == LINK_ROLE_RECEIVER ? "R" : "-";
}

/* ---- the phone half, as at M2 ------------------------------------------ */

static btstack_timer_source_t s_fake_rx_timer;
static btstack_timer_source_t s_status_timer;
static btstack_timer_source_t s_link_timer;
#define STATUS_TICK_MS 30000u

static void report_status(void)
{
    ble_status_t st;
    const uint8_t *blob = NULL;
    size_t len = 0;

    memset(&st, 0, sizeof st);
    st.version = BLE_STATUS_VERSION;
    st.record_id = store_record_id(&s_store);

    if (ble_encrypted())                            st.flags |= BLE_ST_ENCRYPTED;
    if (store_get(&s_store, &blob, &len) == STORE_OK) st.flags |= BLE_ST_PROVISIONED;
    if (s_flash_ok)                                 st.flags |= BLE_ST_FLASH_OK;
    if (ble_telemetry_subscribed())                 st.flags |= BLE_ST_TLM_ON;
    if (power_on_usb())                             st.flags |= BLE_ST_USB_POWER;
    if (store_haptic_on(&s_store))                  st.flags |= BLE_ST_HAPTIC_ON;
    if (wear_dev_mode())                            st.flags |= BLE_ST_DEV_MODE;

    st.own_blob_len = (uint16_t)len;
    st.chunk_errors = ble_chunk_errors();

    /* The body link, live from M14. */
    st.link_state   = (uint8_t)s_sm.state;
    st.frag_bitmap  = s_sm.rx.have;
    st.frame_errors = (uint16_t)s_sm.frames_rx_bad;
    st.last_score   = (uint16_t)hal_rx_carrier_level(s_hal);

    {
        uint32_t mv = power_vsys_mv();
        st.vsys_20mv = (uint8_t)((mv + 10u) / 20u > 255u ? 255u : (mv + 10u) / 20u);
    }
    st.fw_major = HANDOFF_FW_VERSION_MAJOR;
    st.fw_minor = HANDOFF_FW_VERSION_MINOR;
    st.fw_patch = HANDOFF_FW_VERSION_PATCH;

    ble_notify_status(&st);
}

/*
 * Provisioning, architecture §9. Runs in the BTstack context, so it must not
 * block — parsing and encoding a card is a few microseconds, and the flash
 * write is a few milliseconds with interrupts off and core 1 parked, which
 * is within what a connection interval tolerates.
 */
static void on_my_vcard(const char *text, size_t len, void *ctx)
{
    store_err_t e;

    (void)ctx;

    e = store_put_vcard(&s_store, text, len);
    if (e != STORE_OK) {
        printf("handoff: provisioning rejected (%d), %u bytes of card\n",
               (int)e, (unsigned)len);
        report_status();
        return;
    }

    e = store_save(&s_store);
    printf("handoff: provisioned, %u bytes of text -> %u compact, record id %u, "
           "flash %s\n",
           (unsigned)len, (unsigned)s_store.len, (unsigned)store_record_id(&s_store),
           e == STORE_OK ? "written" : "FAILED");

    s_own_dirty = true;
    /* The card is in RAM and will transmit, but the band cannot keep it: a
     * wearer whose card is gone after a charge should have seen something. */
    wear_post(e == STORE_OK ? UI_EV_CARD_WRITTEN : UI_EV_FAULT);
    report_status();
}

static void on_control(uint8_t op, const uint8_t *arg, size_t len, void *ctx)
{
    (void)ctx;

    switch (op) {
    case BLE_CTRL_FAKE_RX: {
        uint32_t delay_s = (len >= 1u) ? arg[0] : 0u;
        btstack_run_loop_remove_timer(&s_fake_rx_timer);
        btstack_run_loop_set_timer(&s_fake_rx_timer, delay_s * 1000u);
        btstack_run_loop_add_timer(&s_fake_rx_timer);
        printf("handoff: fake rx_vcard armed for +%u s\n", (unsigned)delay_s);
        break;
    }

    case BLE_CTRL_TLM_DECIMATE:
        printf("handoff: telemetry decimation requested (M4)\n");
        break;

    case BLE_CTRL_FORGET:
        printf("handoff: forgetting the stored record (%d)\n",
               (int)store_forget(&s_store));
        s_own_dirty = true;
        report_status();
        break;

    case BLE_CTRL_HAPTIC: {
        bool on = len >= 1u && arg[0] != 0u;
        bool persisted;

        store_set_haptic(&s_store, on);
        wear_set_haptic(on);
        persisted = store_get(&s_store, NULL, NULL) == STORE_OK
            && store_save(&s_store) == STORE_OK;
        printf("handoff: haptic %s%s\n", on ? "on" : "off",
               persisted ? ", persisted" : ", not yet persisted (no card)");
        report_status();
        break;
    }

    case BLE_CTRL_FORCE_ROLE:
        /* 0 = target (receive first), 1 = initiator (send first). The main
         * loop takes it up; link_sm_begin skips the trigger. */
        s_force_role = (len >= 1u && arg[0] != 0u) ? 1 : 0;
        printf("handoff: forced role %s\n", s_force_role ? "sender" : "receiver");
        break;

    case BLE_CTRL_IDENTIFY:
        wear_post(UI_EV_IDENTIFY);
        printf("handoff: identify\n");
        break;

    case BLE_CTRL_CARRIER:
    case BLE_CTRL_RAW_TRIGGER:
        printf("handoff: control 0x%02x is not implemented in this image\n", op);
        break;

    default:
        break;
    }
}

static void status_tick(btstack_timer_source_t *ts)
{
    if (ble_connected()) report_status();
    btstack_run_loop_set_timer(ts, STATUS_TICK_MS);
    btstack_run_loop_add_timer(ts);
}

static void send_fake_card(btstack_timer_source_t *ts)
{
    (void)ts;
    bool sent = ble_notify_rx_vcard(k_fake_card, sizeof k_fake_card - 1u);
    printf("handoff: fake rx_vcard %s (%u bytes, ATT MTU %u)\n",
           sent ? "sent" : "REFUSED", (unsigned)(sizeof k_fake_card - 1u),
           (unsigned)ble_att_mtu());
    if (sent) {
        wear_post(UI_EV_LINK_COMPLETE);
        wear_post(UI_EV_LINK_FORWARDED);
    }
}

/*
 * The main loop's way into the BTstack context. A card that arrived over the
 * pad is handed to the phone from here, never from the loop that decoded it.
 */
static void link_tick(btstack_timer_source_t *ts)
{
    static uint32_t ticks;

    if (s_rx_ready) {
        bool sent;
        s_rx_ready = false;
        sent = ble_notify_rx_vcard(s_rx_text, s_rx_text_len);
        printf("handoff: rx_vcard to the phone %s (%u bytes)\n",
               sent ? "sent" : "held (no bonded phone subscribed)",
               (unsigned)s_rx_text_len);
        s_rx_pending = !sent;
        wear_post(sent ? UI_EV_LINK_FORWARDED : UI_EV_LINK_HELD);
    } else if (s_rx_pending && ble_rx_vcard_subscribed() && !ble_rx_vcard_busy()) {
        /* the phone came back: the held card goes now, unchanged */
        if (ble_notify_rx_vcard(s_rx_text, s_rx_text_len)) {
            s_rx_pending = false;
            wear_post(UI_EV_LINK_FORWARDED);
            printf("handoff: held rx_vcard sent to the phone (%u bytes)\n",
                   (unsigned)s_rx_text_len);
        }
    }

    if (s_status_dirty) {
        s_status_dirty = false;
        if (ble_connected()) report_status();
    }

    /* the wearer's side: what only this context can know */
    wear_set_ble(ble_connected(), ble_has_bond());
    if (wear_take_forget_request()) {
        ble_forget_bonds();
        printf("handoff: bonds cleared from the button\n");
        wear_post(UI_EV_BOND_CLEARED);
    }
    /* VSYS only when the receiver is not using the converter (power.c). On
     * the product image the ring runs from boot, so this reads 0 = unknown
     * and the battery rows stay dark; see power.c for what would fix it. */
    if (ticks++ % POWER_SAMPLE_TICKS == 0u)
        wear_set_power(power_vsys_mv(), power_on_usb());

    btstack_run_loop_set_timer(ts, LINK_TICK_MS);
    btstack_run_loop_add_timer(ts);
}

/*
 * The link's configuration on this HAL. The defaults were swept at M1 on a
 * simulator whose chips arrive the instant they are sampled; here they
 * arrive up to a DMA block late, and the one timing that waits to HEAR the
 * far end has to allow for it. rx_idle_us decides "they have gone quiet":
 * after our release the far end settles for 2 ms, its preamble needs a
 * detector latency to register, and the ring can then hold it for another
 * block — so at the 6 ms default a receiver gave up on a reply that was
 * still in the ring and talked over it, every turn, and a two-fragment
 * exchange took thirty turnarounds and the whole budget.
 *
 * The chain from our pad going idle to the reply registering here: the far
 * end sees our last frame end (a ring block), turns around (2 ms), packs its
 * frame (HAL_PICO_TX_SETUP_US), the detector needs a chip-group, and the
 * ring holds that for another block. With two blocks and no setup time the
 * margin was about a millisecond, and on a synchronised start it went
 * negative once in four: both boards clocking frames out at once, for three
 * frames, until the barren turns sent them back through the trigger. So:
 * the default, plus two blocks, plus the setup time.
 */
static void link_cfg_pico(link_cfg_t *c)
{
    link_cfg_default(c);
    c->rx_idle_us += 2u * HAL_PICO_RX_LATENCY_US + HAL_PICO_TX_SETUP_US;
}

/* ---- own record ------------------------------------------------------- */

static bool split_own(void)
{
    const uint8_t *blob = NULL;
    size_t len = 0;

    s_have_own = false;
    if (store_get(&s_store, &blob, &len) != STORE_OK) return false;
    if (frag_split(blob, len, store_record_id(&s_store), &s_own) != FRAG_OK) {
        printf("handoff: stored record does not split into fragments\n");
        return false;
    }
    s_have_own = true;
    return true;
}

/*
 * A card for a board with no phone, named after the board so the far end's
 * console says which one it heard. Small enough to be two fragments, which
 * is the smallest exchange that still exercises the carousel.
 */
static void write_bench_card(void)
{
    char card[256];
    const char *label = ble_local_name() + 13;
    int n;

    n = snprintf(card, sizeof card,
                 "BEGIN:VCARD\r\nVERSION:3.0\r\n"
                 "N:%s;Band;;;\r\nFN:Band %s\r\nORG:Handoff bench\r\n"
                 "TEL;TYPE=CELL:+44 7700 900%c%c%c\r\n"
                 "EMAIL;TYPE=INTERNET:band%s@gmail.com\r\nEND:VCARD\r\n",
                 label, label, label[1], label[2], label[3], label);
    if (n <= 0 || (size_t)n >= sizeof card) return;

    if (store_put_vcard(&s_store, card, (size_t)n) != STORE_OK) {
        printf("    bench card rejected\n");
        return;
    }
    printf("    bench card \"Band %s\": %u compact bytes, record id %u, flash %s\n",
           label, (unsigned)s_store.len, (unsigned)store_record_id(&s_store),
           store_save(&s_store) == STORE_OK ? "written" : "FAILED");
    s_own_dirty = true;
}

/* ---- the sync pin ------------------------------------------------------- */

#if HANDOFF_BENCH_SYNC
/*
 * M14's bench-only pin. On the product board GP15 is the push button, with
 * R15 pulling it up: a press during a driven-high pulse would short the
 * GPIO, so the two are exclusive by build. The two-board bench of M14 is
 * `-DHANDOFF_BENCH_SYNC=ON`; the button is disabled in that image.
 */
static void on_sync_irq(void)
{
    uint32_t ev = gpio_get_irq_event_mask(PIN_SYNC);
    if (!(ev & GPIO_IRQ_EDGE_RISE)) return;
    gpio_acknowledge_irq(PIN_SYNC, GPIO_IRQ_EDGE_RISE);
    s_sync_us  = time_us_64();
    s_sync_hit = true;
}

static void sync_init(void)
{
    gpio_init(PIN_SYNC);
    gpio_set_dir(PIN_SYNC, GPIO_IN);
    gpio_pull_down(PIN_SYNC);
    /* A raw handler, not the shared callback: the CYW43 driver owns that. */
    gpio_add_raw_irq_handler(PIN_SYNC, on_sync_irq);
    gpio_set_irq_enabled(PIN_SYNC, GPIO_IRQ_EDGE_RISE, true);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

/* Drive the shared line high for a moment. Our own edge interrupt fires too,
 * so the driving board restarts on the same edge as the other one. */
static void sync_pulse(void)
{
    gpio_put(PIN_SYNC, 0);
    gpio_set_dir(PIN_SYNC, GPIO_OUT);
    gpio_put(PIN_SYNC, 1);
    busy_wait_us(SYNC_PULSE_US);
    gpio_put(PIN_SYNC, 0);
    gpio_set_dir(PIN_SYNC, GPIO_IN);
}
#else
static void sync_init(void) {}
#endif

/* ---- the link, from the main loop --------------------------------------- */

/* counters at the last arm, so a done line reads per handshake */
static uint32_t s_arm_sent, s_arm_good, s_arm_bad, s_arm_turns;

static void arm(uint64_t now)
{
    s_parked = false;
    s_arm_sent  = s_sm.frames_sent;
    s_arm_good  = s_sm.frames_rx_good;
    s_arm_bad   = s_sm.frames_rx_bad;
    s_arm_turns = s_sm.turnarounds;
    link_sm_idle(&s_sm, now);
}

/*
 * A handshake ended. Decode what came, print it, and hand it to the phone
 * half. Then park until `d` has passed, so the far end — which may be a turn
 * behind us, the two-army problem — finishes before the trigger re-arms.
 */
static void on_done(uint64_t now)
{
    const uint8_t *blob = NULL;
    size_t n = link_sm_received(&s_sm, &blob);
    compact_rec_t rec;
    uint32_t ms = (uint32_t)((now - s_sm.started_us) / 1000u);
    bool ok = s_sm.state == LINK_COMPLETE;

    if (ok) s_st.complete++; else s_st.abort++;
    if (s_sm.role == LINK_ROLE_SENDER) s_st.as_sender++;
    else if (s_sm.role == LINK_ROLE_RECEIVER) s_st.as_receiver++;
    s_st.dur_sum_ms += ms;
    if (ms > s_st.dur_max_ms) s_st.dur_max_ms = ms;
    wear_post(ok ? UI_EV_LINK_COMPLETE : UI_EV_LINK_ABORT);

    printf("done %llu %s #%lu role %s %lu ms sent %lu good %lu bad %lu turns %lu "
           "retries %u frags %u/%u\n",
           (unsigned long long)now, ok ? "COMPLETE" : "ABORT",
           (unsigned long)(s_st.complete + s_st.abort), role_name(s_sm.role),
           (unsigned long)ms, (unsigned long)(s_sm.frames_sent - s_arm_sent),
           (unsigned long)(s_sm.frames_rx_good - s_arm_good),
           (unsigned long)(s_sm.frames_rx_bad - s_arm_bad),
           (unsigned long)(s_sm.turnarounds - s_arm_turns),
           (unsigned)s_sm.retries,
           (unsigned)(s_sm.rx.count - frag_rx_missing(&s_sm.rx)), (unsigned)s_sm.rx.count);

    s_rx_text_len = 0;
    if (n && compact_decode(blob, n, &rec) == COMPACT_OK &&
        vcard_render(&rec, s_rx_text, sizeof s_rx_text, &s_rx_text_len) == COMPACT_OK) {
        size_t i;
        printf("  got %u bytes:", (unsigned)n);
        /* The card, one line, so the log stays one line per event. */
        for (i = 0; i < s_rx_text_len; i++) {
            char c = s_rx_text[i];
            if (c == '\r') continue;
            putchar(c == '\n' ? '|' : c);
        }
        putchar('\n');
        if (ok) s_rx_ready = true;          /* the phone bonus, on 93D1 */
    } else {
        printf("  got %u bytes, not a decodable record\n", (unsigned)n);
    }

    s_status_dirty = true;
    s_parked = true;
    s_reidle_at = now + s_reidle_us;
}

static void poll_link(uint64_t now)
{
    static link_state_t last_state = LINK_STATE_COUNT;
    static link_role_t  last_role;
    static uint64_t     last_send;
    link_state_t st;

    if (s_own_dirty) {
        s_own_dirty = false;
        if (s_sm.state == LINK_IDLE || s_parked || !s_link_on) {
            split_own();
            link_sm_init(&s_sm, s_hal, &s_cfg, &s_own);
            /* the machine restarted its counters; the baselines follow */
            s_st.shouts_at_zero = s_st.frames_at_zero = s_st.good_at_zero = 0;
            s_st.bad_at_zero = s_st.turns_at_zero = 0;
            s_arm_sent = s_arm_good = s_arm_bad = s_arm_turns = 0;
            if (s_link_on && s_have_own) arm(now);
        } else {
            s_own_dirty = true;          /* mid-exchange: try again later */
        }
    }

#if HANDOFF_BENCH_SYNC
    if (s_sync_hit) {
        uint64_t t;
        s_sync_hit = false;
        t = s_sync_us;
        s_st.syncs++;
        if (s_link_on && s_have_own) {
            /* A send in flight ends here, not at the end its tx line gave. */
            if (hal_pico_tx_abort())
                printf("cut %llu\n", (unsigned long long)hal_pico_tx_pad_idle_us());
            arm(now);
        }
        printf("sync %llu\n", (unsigned long long)t);
    }

    if (s_sync_left && now >= s_sync_next_us) {
        s_sync_left--;
        s_sync_next_us = now + SYNC_PERIOD_US;
        sync_pulse();
    }
#endif

    if (!s_link_on || !s_have_own) {
        uint16_t sink[64];
        while (hal_rx_chips(s_hal, sink, 64) == 64) {}
        return;
    }

    if (s_force_role >= 0) {
        link_role_t r = s_force_role ? LINK_ROLE_SENDER : LINK_ROLE_RECEIVER;
        s_force_role = -1;
        s_parked = false;
        link_sm_begin(&s_sm, now, r);
    }

    if (s_parked) {
        uint16_t sink[64];
        while (hal_rx_chips(s_hal, sink, 64) == 64) {}
        if (now >= s_reidle_at) arm(now);
        return;
    }

    st = link_sm_poll(&s_sm, now);

    /* Every send, from the HAL's own account of it: DMA start to pad idle. */
    if (hal_pico_tx_started_us() != last_send) {
        last_send = hal_pico_tx_started_us();
        s_st.sends++;
        if (s_verbose)
            printf("tx %c %llu %llu\n", st == LINK_IDLE ? 'S' : 'F',
                   (unsigned long long)last_send,
                   (unsigned long long)hal_pico_tx_pad_idle_us());
    }

    if (st != last_state || s_sm.role != last_role) {
        last_state = st;
        last_role  = s_sm.role;
        if (s_verbose && st != LINK_COMPLETE && st != LINK_ABORT)
            printf("st %llu %s %s\n", (unsigned long long)now,
                   link_state_name(st), role_name(s_sm.role));
    }

    if (st == LINK_COMPLETE || st == LINK_ABORT) on_done(now);
}

/* ---- console ------------------------------------------------------------ */

static void print_stats(void)
{
    uint32_t n = s_st.complete + s_st.abort;

    printf("  %6lu s  handshakes %lu complete %lu abort, as sender %lu receiver %lu; "
           "syncs %lu\n",
           (unsigned long)elapsed_s(s_st.since), (unsigned long)n,
           (unsigned long)s_st.complete, (unsigned long)s_st.abort,
           (unsigned long)s_st.as_sender, (unsigned long)s_st.as_receiver,
           (unsigned long)s_st.syncs);
    printf("           duration mean %lu max %lu ms; shouts %lu frames sent %lu good %lu "
           "bad %lu turnarounds %lu\n",
           (unsigned long)(n ? s_st.dur_sum_ms / n : 0), (unsigned long)s_st.dur_max_ms,
           (unsigned long)(s_sm.trig.shouts - s_st.shouts_at_zero),
           (unsigned long)(s_sm.frames_sent - s_st.frames_at_zero),
           (unsigned long)(s_sm.frames_rx_good - s_st.good_at_zero),
           (unsigned long)(s_sm.frames_rx_bad - s_st.bad_at_zero),
           (unsigned long)(s_sm.turnarounds - s_st.turns_at_zero));
    printf("           state %s role %s; own-chips cut %lu, false syncs %lu, overruns %lu, "
           "stalls %lu, core-1 load %u%%\n",
           link_state_name(s_sm.state), role_name(s_sm.role),
           (unsigned long)hal_pico_rx_cut(), (unsigned long)s_sm.framer.false_syncs,
           (unsigned long)hal_pico_overruns(), (unsigned long)hal_pico_tx_stalls(0),
           hal_pico_core1_load());
}

static void print_received(void)
{
    size_t i;
    if (!s_rx_text_len) { printf("    nothing received yet\n"); return; }
    for (i = 0; i < s_rx_text_len; i++)
        if (s_rx_text[i] != '\r') putchar(s_rx_text[i]);
}

static void cmd_carrier(uint32_t khz)
{
    if (!hal_pico_set_carrier(khz * 1000u)) {
        printf("    %lu kHz is not a PIO divider on a Goertzel bin centre\n",
               (unsigned long)khz);
        return;
    }
    if (s_link_on && s_have_own) arm(hal_now_us(s_hal));
    printf("    carrier %lu kHz\n", (unsigned long)khz);
}

static void help(void)
{
    printf("\n  g         link on / off (now %s)\n"
           "  y [N]     sync pulse on GP15, or N of them 3 s apart\n"
           "  d [ms]    pause after a handshake before re-arming (now %lu)\n"
           "  w         store a bench card named after this board\n"
           "  r         print the last received card\n"
           "  s         stats           z  zero\n"
           "  v         per-send lines %s\n"
           "  c 40|200  carrier, kHz    h  this\n"
           "  u         LED / motor trace %s;  u <event> inject one (u ? lists)\n"
           "  b <ms>    press the button for that long\n",
           s_link_on ? "on" : "off", (unsigned long)(s_reidle_us / 1000u),
           s_verbose ? "(on)" : "(off)", wear_tracing() ? "(on)" : "(off)");
}

static void cmd_ui(const char *line)
{
    const char *p = line + 1;
    int ev;

    while (*p == ' ') p++;
    if (!*p) {
        wear_trace(!wear_tracing());
        printf("    ui trace %s\n", wear_tracing() ? "on" : "off");
        return;
    }
    if (*p == '?') {
        int i;
        printf("   ");
        for (i = 0; i < (int)UI_EV_COUNT; i++) printf(" %s", ui_event_name((ui_event_t)i));
        printf("\n");
        return;
    }
    ev = ui_event_parse(p);
    if (ev < 0) { printf("    unknown event (u ? lists them)\n"); return; }
    wear_inject((ui_event_t)ev);
    printf("    injected %s\n", ui_event_name((ui_event_t)ev));
}

static char parse(const char *line, uint32_t *arg, bool *have_arg)
{
    const char *p;

    while (*line == ' ') line++;
    *have_arg = false;
    *arg = 0;
    if (!*line) return 0;
    for (p = line + 1; *p == ' '; p++) {}
    if (*p >= '0' && *p <= '9') { *arg = (uint32_t)strtoul(p, 0, 10); *have_arg = true; }
    return line[0];
}

static void dispatch(const char *line)
{
    uint32_t arg;
    bool have_arg;
    uint64_t now = hal_now_us(s_hal);

    switch (parse(line, &arg, &have_arg)) {
    case 0: return;
    case 'g':
        s_link_on = !s_link_on;
        if (s_link_on) {
            if (!s_have_own) { printf("    no card stored: `w` first\n"); s_link_on = false; }
            else arm(now);
        } else {
            hal_pico_tx_abort();
            hal_tx_drive(s_hal, false);
            trig_stop(&s_sm.trig);
        }
        printf("    link %s\n", s_link_on ? "on" : "off");
        break;
    case 'y':
#if HANDOFF_BENCH_SYNC
        s_sync_left = have_arg && arg ? arg : 1u;
        s_sync_next_us = now;
        printf("    %lu sync pulse%s\n", (unsigned long)s_sync_left, s_sync_left == 1 ? "" : "s");
#else
        printf("    not in this build: GP15 is the button (HANDOFF_BENCH_SYNC=ON for the pulse)\n");
#endif
        break;
    case 'u': cmd_ui(line); break;
    case 'b':
        wear_press(have_arg ? arg : 100u);
        printf("    button down for %lu ms\n", (unsigned long)(have_arg ? arg : 100u));
        break;
    case 'd':
        if (have_arg) s_reidle_us = arg * 1000u;
        printf("    re-arm %lu ms after a handshake\n", (unsigned long)(s_reidle_us / 1000u));
        break;
    case 'w': write_bench_card(); break;
    case 'r': print_received(); break;
    case 's': print_stats(); break;
    case 'z': zero_stats(); printf("    stats zeroed\n"); break;
    case 'v':
        s_verbose = !s_verbose;
        printf("    per-send lines %s\n", s_verbose ? "on" : "off");
        break;
    case 'c': cmd_carrier(have_arg ? arg : HANDOFF_CARRIER_HZ / 1000u); break;
    case 'h': case '?': help(); break;
    default:  printf("    ? (h for help)\n"); break;
    }
}

/* ---------------------------------------------------------------------- */

static void print_banner(void)
{
    const uint8_t *blob = NULL;
    size_t len = 0;
    int32_t noise_mean;
    uint32_t noise;

    printf("\nhandoff (M14: two-way over the pad) v%d.%d.%d\n", HANDOFF_FW_VERSION_MAJOR,
           HANDOFF_FW_VERSION_MINOR, HANDOFF_FW_VERSION_PATCH);
    printf("  carrier %d Hz, %d chips/s, %d bps, Goertzel N=%d bin %d\n",
           HANDOFF_CARRIER_HZ, HANDOFF_CHIP_RATE_HZ,
           HANDOFF_BIT_RATE_BPS, HANDOFF_GZ_N, HANDOFF_GZ_BIN);
    printf("  shout %lu ms, listen %lu-%lu ms, frame %lu ms, turnaround %d us\n",
           (unsigned long)(HANDOFF_SHOUT_US / 1000u),
           (unsigned long)(HANDOFF_LISTEN_MIN_US / 1000u),
           (unsigned long)(HANDOFF_LISTEN_MAX_US / 1000u),
           (unsigned long)(FRAME_AIRTIME_US / 1000u), HANDOFF_TURNAROUND_US);
    printf("  rx idle %lu ms (ring latency %lu ms), %u frames/turn, budget %lu ms\n",
           (unsigned long)(s_cfg.rx_idle_us / 1000u),
           (unsigned long)(HAL_PICO_RX_LATENCY_US / 1000u), s_cfg.frames_per_turn,
           (unsigned long)(s_cfg.contact_budget_us / 1000u));
    printf("  record sector at 0x%06x, %u bytes\n",
           (unsigned)flash_record_offset(), (unsigned)flash_record_sector_size());
    printf("  name \"%s\", label %s\n", ble_local_name(), ble_local_name() + 13);

    noise = hal_pico_noise_floor(&noise_mean);
    printf("  M4 noise floor %lu.%lu LSB RMS (mean code %ld), core 1 up\n",
           (unsigned long)(noise / 10u), (unsigned long)(noise % 10u), (long)noise_mean);

    if (store_get(&s_store, &blob, &len) == STORE_OK)
        printf("  provisioned: %u compact bytes, record id %u, %u fragments\n",
               (unsigned)len, (unsigned)store_record_id(&s_store), (unsigned)s_own.count);
    else
        printf("  not provisioned — write a card to my_vcard, or `w` for a bench card\n");
}

int main(void)
{
    char line[32];
    size_t len = 0;
    uint64_t next_hb;

    stdio_init_all();

    /*
     * First thing, not merely early: R17 only holds Q1's gate down while
     * GP28 is an input, which is the reset state and stays that way until
     * this call claims it (hardware README, "R17 ... Not optional").
     */
    motor_init();

    if (cyw43_arch_init()) {
        printf("handoff: cyw43_arch_init failed\n");
        return 1;
    }

    store_init(&s_store);
    s_flash_ok = flash_record_bind();

    if (store_load(&s_store) != STORE_OK && s_flash_ok)
        printf("handoff: flash holds no readable record\n");

    btstack_run_loop_set_timer_handler(&s_fake_rx_timer, send_fake_card);
    btstack_run_loop_set_timer_handler(&s_status_timer, status_tick);
    btstack_run_loop_set_timer(&s_status_timer, STATUS_TICK_MS);
    btstack_run_loop_add_timer(&s_status_timer);
    btstack_run_loop_set_timer_handler(&s_link_timer, link_tick);
    btstack_run_loop_set_timer(&s_link_timer, LINK_TICK_MS);
    btstack_run_loop_add_timer(&s_link_timer);

    ble_set_vcard_handler(on_my_vcard, NULL);
    ble_set_control_handler(on_control, NULL);
    ble_init();

    /* The body link. After the radio, so core 1 comes up with the CYW43
     * already claiming its PIO (it takes the highest one; ours is pio0). */
    s_hal = hal_pico_init();
    sync_init();
    split_own();
    link_cfg_pico(&s_cfg);
    link_sm_init(&s_sm, s_hal, &s_cfg, &s_own);
    zero_stats();

    /* The wearer's side. After the HAL, so its pins are the last claimed;
     * the boot flash happens here. */
    wear_init(hal_now_us(s_hal));
    wear_set_haptic(store_haptic_on(&s_store));

    sleep_ms(1500);          /* let the USB console attach before the banner */
    print_banner();

    if (s_have_own) {
        s_link_on = true;
        arm(hal_now_us(s_hal));
        printf("  link on: free-running the trigger. `h` for help.\n");
    } else {
        printf("  link off until a card is stored.\n");
    }

    next_hb = hal_now_us(s_hal) + HEARTBEAT_US;

    for (;;) {
        int ch = getchar_timeout_us(0);
        uint64_t now = hal_now_us(s_hal);

        poll_link(now);
        wear_link(&s_sm);
        wear_poll(now);

        if (ch == PICO_ERROR_TIMEOUT) {
            if (now >= next_hb) {
                next_hb += HEARTBEAT_US;
                printf("hb %lu s %s %s ok %lu abort %lu shouts %lu frames %lu/%lu/%lu "
                       "cut %lu stalls %lu load %u%%\n",
                       (unsigned long)elapsed_s(s_st.since), link_state_name(s_sm.state),
                       s_link_on ? (s_parked ? "parked" : "on") : "off",
                       (unsigned long)s_st.complete, (unsigned long)s_st.abort,
                       (unsigned long)(s_sm.trig.shouts - s_st.shouts_at_zero),
                       (unsigned long)(s_sm.frames_sent - s_st.frames_at_zero),
                       (unsigned long)(s_sm.frames_rx_good - s_st.good_at_zero),
                       (unsigned long)(s_sm.frames_rx_bad - s_st.bad_at_zero),
                       (unsigned long)hal_pico_rx_cut(),
                       (unsigned long)hal_pico_tx_stalls(0), hal_pico_core1_load());
            }
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            line[len] = 0;
            if (len) dispatch(line);
            len = 0;
        } else if (len + 1 < sizeof line) {
            line[len++] = (char)ch;
        }
    }
}
