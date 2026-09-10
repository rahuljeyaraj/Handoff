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

#include "ble.h"
#include "config.h"
#include "flash.h"
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

/* Pending BLE_CTRL_FAKE_RX, in milliseconds since boot. 0 = nothing armed. */
static volatile uint32_t s_fake_rx_due_ms;

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

    st.own_blob_len = (uint16_t)len;
    st.chunk_errors = ble_chunk_errors();

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
        s_fake_rx_due_ms = to_ms_since_boot(get_absolute_time()) + delay_s * 1000u;
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

static void send_fake_card(void)
{
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

    printf("\nhandoff (M2: phone link)\n");
    printf("  carrier %d Hz, %d chips/s, %d bps, Goertzel N=%d bin %d\n",
           HANDOFF_CARRIER_HZ, HANDOFF_CHIP_RATE_HZ,
           HANDOFF_BIT_RATE_BPS, HANDOFF_GZ_N, HANDOFF_GZ_BIN);
    printf("  record sector at 0x%06x, %u bytes\n",
           (unsigned)flash_record_offset(), (unsigned)flash_record_sector_size());

    if (store_get(&s_store, &blob, &len) == STORE_OK)
        printf("  provisioned: %u compact bytes, record id %u\n",
               (unsigned)len, (unsigned)store_record_id(&s_store));
    else
        printf("  not provisioned — write a card to my_vcard\n");
}

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

    ble_set_vcard_handler(on_my_vcard, NULL);
    ble_set_control_handler(on_control, NULL);
    ble_init();

    print_banner();

    for (;;) {
        uint32_t due = s_fake_rx_due_ms;

        if (due != 0u && to_ms_since_boot(get_absolute_time()) >= due) {
            s_fake_rx_due_ms = 0;
            /*
             * ble.c's own callbacks run inside the BTstack context, but this
             * one is the main loop reaching in, so it takes the lock. See the
             * threading note at the top of ble.c.
             */
            async_context_t *ctx = cyw43_arch_async_context();
            async_context_acquire_lock_blocking(ctx);
            send_fake_card();
            async_context_release_lock(ctx);
        }

        /* The LED is the only thing that says "advertising" when there is no
         * console attached, which during a §13 body test there will not be. */
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, ble_connected() ? 1 : 0);

        sleep_ms(50);
    }
}
