/*
 * Handoff — the product image. M2 fills in its phone half; M14 finishes it.
 *
 * There is deliberately no apps/blelink. firmware-architecture.md §4's app
 * list is final, and the phone link is not a bring-up instrument like txgen or
 * afe_sweep — it is half of the finished device, and development plan M12 says
 * so outright: "M2's fake record is replaced by the real received one". Same
 * image, one hardcoded card swapped for a received one. Building it anywhere
 * else would mean writing the provisioning path twice.
 *
 * WHAT THIS PROVES TODAY (development plan M2, with a phone and no other
 * hardware at all):
 *
 *   - a fake vCard travels rx_vcard -> app database -> system address book
 *   - provisioning round-trips: write a card, power-cycle, read it back
 *   - chunked reassembly works at the 23-byte ATT MTU floor
 *   - the foreground service catches a notify with the screen off, which is
 *     what BLE_CTRL_FAKE_RX's delay argument exists for: arm it, lock the
 *     phone, put it in a pocket, and see whether the card still lands
 *   - the bond survives a Pico power cycle with no fresh pairing dialog
 *
 * WHAT IT CANNOT PROVE: anything whatsoever about the body link. The card
 * below is a constant in flash. Nothing here touches the ADC, the carrier, or
 * a person.
 */
#include <stdio.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "btstack_run_loop.h"

#include "ble.h"
#include "config.h"
#include "flash.h"
#include "power.h"
#include "store.h"
#include "vcard.h"

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

/*
 * The delayed BLE_CTRL_FAKE_RX. A BTstack timer, so the send runs inside the
 * BTstack context like every other call into ble.c — see the note above main()
 * for why it is not the main loop reaching in under the async lock.
 */
static btstack_timer_source_t s_fake_rx_timer;

/* Status is only notified on change, and the battery changes on its own, so
 * a slow tick keeps the phone's battery glyph honest. Thirty seconds: a
 * Li-ion curve does not move faster than that, and each read wakes the
 * CYW43 for the shared GP29. */
static btstack_timer_source_t s_status_timer;
#define STATUS_TICK_MS 30000u

/* ---------------------------------------------------------------------- */

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

    st.own_blob_len = (uint16_t)len;
    st.chunk_errors = ble_chunk_errors();

    /* Version 2: the supply and the image. VSYS is after D1; the app adds
     * the diode drop and draws the battery, or a plug when USB is in. */
    {
        uint32_t mv = power_vsys_mv();
        st.vsys_20mv = (uint8_t)((mv + 10u) / 20u > 255u ? 255u : (mv + 10u) / 20u);
    }
    st.fw_major = HANDOFF_FW_VERSION_MAJOR;
    st.fw_minor = HANDOFF_FW_VERSION_MINOR;
    st.fw_patch = HANDOFF_FW_VERSION_PATCH;

    /* link_state, last_score, frag_bitmap and frame_errors stay zero: there is
     * no body link in this image yet, and reporting a plausible-looking zero
     * is better than reporting a number nothing measured. */

    ble_notify_status(&st);
}

/*
 * Provisioning, architecture §9. Runs in the BTstack context, so it must not
 * block — parsing and encoding a card is a few microseconds, and the flash
 * write is a few milliseconds with interrupts off, which is within what a
 * connection interval tolerates.
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
        /* M4 owns the score stream; accepting the opcode now means the app
         * does not have to grow a new one then. */
        printf("handoff: telemetry decimation requested (M4)\n");
        break;

    case BLE_CTRL_FORGET:
        printf("handoff: forgetting the stored record (%d)\n",
               (int)store_forget(&s_store));
        report_status();
        break;

    case BLE_CTRL_CARRIER:
    case BLE_CTRL_RAW_TRIGGER:
    case BLE_CTRL_FORCE_ROLE:
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
    /* Sent as vCard TEXT, exactly as a received card will be at M12: the app
     * never sees the compact form, so what M2 tests is the format M12 ships. */
    bool sent = ble_notify_rx_vcard(k_fake_card, sizeof k_fake_card - 1u);
    printf("handoff: fake rx_vcard %s (%u bytes, ATT MTU %u)\n",
           sent ? "sent" : "REFUSED", (unsigned)(sizeof k_fake_card - 1u),
           (unsigned)ble_att_mtu());
}

/* ---------------------------------------------------------------------- */

static void print_banner(void)
{
    const uint8_t *blob = NULL;
    size_t len = 0;

    printf("\nhandoff (M2: phone link) v%d.%d.%d\n", HANDOFF_FW_VERSION_MAJOR,
           HANDOFF_FW_VERSION_MINOR, HANDOFF_FW_VERSION_PATCH);
    printf("  carrier %d Hz, %d chips/s, %d bps, Goertzel N=%d bin %d\n",
           HANDOFF_CARRIER_HZ, HANDOFF_CHIP_RATE_HZ,
           HANDOFF_BIT_RATE_BPS, HANDOFF_GZ_N, HANDOFF_GZ_BIN);
    printf("  record sector at 0x%06x, %u bytes\n",
           (unsigned)flash_record_offset(), (unsigned)flash_record_sector_size());
    /* The QR label's content, from the same board id the name comes from.
     * tools/band_label.py turns this line into the label. */
    printf("  name \"%s\", label HANDOFF:%s\n", ble_local_name(), ble_local_name() + 8);

    if (store_get(&s_store, &blob, &len) == STORE_OK)
        printf("  provisioned: %u compact bytes, record id %u\n",
               (unsigned)len, (unsigned)store_record_id(&s_store));
    else
        printf("  not provisioned — write a card to my_vcard\n");
}

/*
 * Everything that touches the radio runs inside the BTstack context: the
 * ble.c callbacks, the fake-card timer, and the LED, which hangs off the
 * CYW43 and is written from ble.c's connection events.
 *
 * Measured at M2, and the reason for that rule: with Bluetooth active, a call
 * into the CYW43 from this thread — the LED ioctl, or ble_notify_rx_vcard()
 * taken under the async-context lock — parked the core until the next
 * Bluetooth interrupt, tens of seconds at a time. A card armed for +10 s went
 * out only when the phone next wrote to the band. The main loop therefore
 * does nothing but sleep; M12 puts the DSP here, and it must keep to the same
 * rule or hand its results to a BTstack timer.
 */
int main(void)
{
    stdio_init_all();

    if (cyw43_arch_init()) {
        printf("handoff: cyw43_arch_init failed\n");
        return 1;
    }

    store_init(&s_store);
    s_flash_ok = flash_record_bind();

    /* An unprovisioned wristband is a normal state, not an error: it is what
     * every band is until its wearer's phone writes to my_vcard. */
    if (store_load(&s_store) != STORE_OK && s_flash_ok)
        printf("handoff: flash holds no readable record\n");

    btstack_run_loop_set_timer_handler(&s_fake_rx_timer, send_fake_card);
    btstack_run_loop_set_timer_handler(&s_status_timer, status_tick);
    btstack_run_loop_set_timer(&s_status_timer, STATUS_TICK_MS);
    btstack_run_loop_add_timer(&s_status_timer);

    ble_set_vcard_handler(on_my_vcard, NULL);
    ble_set_control_handler(on_control, NULL);
    ble_init();

    print_banner();

    for (;;) {
        sleep_ms(1000);
    }
}
