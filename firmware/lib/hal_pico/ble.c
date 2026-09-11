/*
 * Handoff — BLE GATT service. architecture §11.2. Development plan M2.
 *
 * See ble.h for the contract and ble_service.gatt for the attribute database.
 * This file binds one to the other and does nothing else: no vCard parsing,
 * no storage policy, no link state. Those are lib/record and lib/proto, and
 * they are host-tested. What is left here is the part that genuinely needs a
 * radio, and it is kept as thin as that division allows.
 *
 * Two things are worth knowing before changing anything here.
 *
 * EVERYTHING RUNS IN THE BTSTACK CONTEXT. On this board that is the cyw43
 * async context, not core 1. The handlers below are therefore allowed to call
 * back into lib/record. Do NOT call the notify functions from the main loop,
 * even under the cyw43 lock: measured at M2, a call into the CYW43 from thread
 * mode with Bluetooth up parked that thread until the next Bluetooth
 * interrupt, tens of seconds at a time. Anything that needs to send from
 * outside hands the work to a BTstack timer (see apps/handoff) instead.
 *
 * NOTIFICATIONS ARE PACED, NOT QUEUED. ATT lets one notification be in flight
 * at a time, so a chunked vCard is pumped out of the CAN_SEND_NOW event
 * rather than in a loop. The alternative — busy-waiting inside
 * ble_notify_rx_vcard — would block the receive path for the length of a
 * whole card at the 23-byte MTU floor, which is a dozen round trips.
 */
#include "ble.h"

#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"

#include "ble_service.h"   /* generated from ble_service.gatt at build time */

/* The generated handle names carry the whole UUID. Alias them once. */
#define H_MY_VCARD_VALUE   ATT_CHARACTERISTIC_48414e44_0002_4f46_9b2c_1e0a7d3f5c81_01_VALUE_HANDLE
#define H_RX_VCARD_VALUE   ATT_CHARACTERISTIC_48414e44_0003_4f46_9b2c_1e0a7d3f5c81_01_VALUE_HANDLE
#define H_RX_VCARD_CCC     ATT_CHARACTERISTIC_48414e44_0003_4f46_9b2c_1e0a7d3f5c81_01_CLIENT_CONFIGURATION_HANDLE
#define H_STATUS_VALUE     ATT_CHARACTERISTIC_48414e44_0004_4f46_9b2c_1e0a7d3f5c81_01_VALUE_HANDLE
#define H_STATUS_CCC       ATT_CHARACTERISTIC_48414e44_0004_4f46_9b2c_1e0a7d3f5c81_01_CLIENT_CONFIGURATION_HANDLE
#define H_TELEMETRY_VALUE  ATT_CHARACTERISTIC_48414e44_0005_4f46_9b2c_1e0a7d3f5c81_01_VALUE_HANDLE
#define H_TELEMETRY_CCC    ATT_CHARACTERISTIC_48414e44_0005_4f46_9b2c_1e0a7d3f5c81_01_CLIENT_CONFIGURATION_HANDLE
#define H_CONTROL_VALUE    ATT_CHARACTERISTIC_48414e44_0006_4f46_9b2c_1e0a7d3f5c81_01_VALUE_HANDLE

/* Advertising interval. 500 ms in 0.625 ms units: slow enough to be cheap on
 * a coin cell, fast enough that a CompanionDeviceManager scan finds the band
 * inside the couple of seconds a user will wait for it. */
#define ADV_INTERVAL_UNITS 800

/* ---------------------------------------------------------------------- */

static btstack_packet_callback_registration_t s_hci_cb;
static btstack_packet_callback_registration_t s_sm_cb;

static hci_con_handle_t s_con = HCI_CON_HANDLE_INVALID;
static bool     s_encrypted;
static uint16_t s_mtu = BLE_MIN_ATT_MTU;
static uint16_t s_chunk_errors;

static ble_vcard_written_fn s_vcard_fn;
static void                *s_vcard_ctx;
static ble_control_fn       s_control_fn;
static void                *s_control_ctx;

/* Provisioning, phone -> band. */
static chunk_rx_t s_my_vcard_rx;

/* A received contact, band -> phone. Copied on submission so the caller's
 * buffer — usually frag.c's reassembly area — is free immediately. */
static chunk_tx_t s_rx_vcard_tx;
static uint8_t    s_rx_vcard_buf[BLE_VCARD_MAX];
static bool       s_rx_vcard_active;
static bool       s_status_pending;   /* a status notify waiting for a buffer */

static ble_status_t s_status;

/*
 * Client characteristic configuration, tracked by hand. BTstack routes CCCD
 * writes on a DYNAMIC characteristic to the write callback and keeps no copy
 * of its own, so "is anybody listening" is our bookkeeping or nobody's.
 */
static bool s_sub_rx_vcard;
static bool s_sub_status;
static bool s_sub_telemetry;

/*
 * Advertising data. The 128-bit service UUID has to be here rather than in
 * the scan response: CompanionDeviceManager filters on the advertisement, and
 * a band that only answers an active scan is a band the pairing dialog never
 * offers. That costs 18 of the 31 bytes, which is why the name goes in the
 * scan response instead.
 */
static const uint8_t s_adv_data[] = {
    /* Flags: LE General Discoverable, BR/EDR not supported */
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    /* Complete list of 128-bit service UUIDs, little-endian on the wire */
    0x11, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
    0x81, 0x5c, 0x3f, 0x7d, 0x0a, 0x1e, 0x2c, 0x9b,
    0x46, 0x4f, 0x01, 0x00, 0x44, 0x4e, 0x41, 0x48,
};

/* "Handoff ABCD" — the last four hex digits of the board id, so two
 * wristbands on one bench are distinguishable at M14 without a label. */
static uint8_t s_scan_resp[1 + 1 + 12];
static char    s_name[13];

/* ---------------------------------------------------------------------- */

static void build_name_and_scan_response(void)
{
    pico_unique_board_id_t id;

    pico_get_unique_board_id(&id);
    snprintf(s_name, sizeof s_name, "Handoff %02X%02X",
             id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 2],
             id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1]);

    s_scan_resp[0] = (uint8_t)(1 + strlen(s_name));
    s_scan_resp[1] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(s_scan_resp + 2, s_name, strlen(s_name));
}

static size_t scan_response_len(void)
{
    return 2u + strlen(s_name);
}

static void start_advertising(void)
{
    bd_addr_t null_addr;

    memset(null_addr, 0, sizeof null_addr);
    gap_advertisements_set_params(ADV_INTERVAL_UNITS, ADV_INTERVAL_UNITS,
                                  0 /* connectable undirected */, 0, null_addr,
                                  0x07 /* all channels */, 0x00);
    gap_advertisements_set_data(sizeof s_adv_data, (uint8_t *)s_adv_data);
    gap_scan_response_set_data((uint8_t)scan_response_len(), s_scan_resp);
    gap_advertisements_enable(1);
}

/* ---- the chunked rx_vcard pump ---------------------------------------- */

/* One chunk. Sized for a fully negotiated MTU rather than for the floor, so
 * a phone that asks for 247 gets a card in one notification instead of ten. */
#define BLE_CHUNK_BUF 256

/* Payload per notification: what the MTU allows, capped by the buffer above.
 * Chosen once when a card is submitted and used unchanged for the whole of
 * it, because chunk.c requires every chunk but the last to be payload-full
 * and an MTU renegotiation mid-card would otherwise produce a ragged one. */
static size_t notify_room(void)
{
    size_t room = BLE_CHUNK_BYTES(s_mtu);
    return room > BLE_CHUNK_BUF ? (size_t)BLE_CHUNK_BUF : room;
}

static void pump_rx_vcard(void)
{
    uint8_t buf[BLE_CHUNK_BUF];
    size_t n;

    if (!s_rx_vcard_active) return;

    if (s_con == HCI_CON_HANDLE_INVALID || !s_encrypted) {
        s_rx_vcard_active = false;   /* the phone left mid-card */
        return;
    }

    n = chunk_tx_next(&s_rx_vcard_tx, buf, sizeof buf);
    if (n == 0) {
        s_rx_vcard_active = false;
        return;
    }

    att_server_notify(s_con, H_RX_VCARD_VALUE, buf, (uint16_t)n);

    if (chunk_tx_done(&s_rx_vcard_tx)) s_rx_vcard_active = false;
    else att_server_request_can_send_now_event(s_con);
}

/* ---- ATT ------------------------------------------------------------- */

static uint16_t att_read(hci_con_handle_t con, uint16_t handle, uint16_t offset,
                         uint8_t *buf, uint16_t buf_size)
{
    (void)con;

    /*
     * status is the only readable value; the rest are notifications or
     * writes, and a read of one gets zero bytes rather than a surprise.
     *
     * The three CCCDs are readable too, because compile_gatt.py makes them
     * READ_ANYBODY and DYNAMIC, which routes their reads here. Answering them
     * from our own bookkeeping is what makes "did my subscribe take?"
     * answerable by a client instead of being invisible.
     */
    if (handle == H_STATUS_VALUE)
        return att_read_callback_handle_blob((const uint8_t *)&s_status,
                                             sizeof s_status, offset,
                                             buf, buf_size);

    if (handle == H_RX_VCARD_CCC || handle == H_STATUS_CCC || handle == H_TELEMETRY_CCC) {
        bool on = (handle == H_RX_VCARD_CCC) ? s_sub_rx_vcard
                : (handle == H_STATUS_CCC)   ? s_sub_status
                                             : s_sub_telemetry;
        uint8_t cccd[2];
        little_endian_store_16(cccd, 0,
            on ? GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION : 0u);
        return att_read_callback_handle_blob(cccd, sizeof cccd, offset, buf, buf_size);
    }

    return 0;
}

static void handle_my_vcard_chunk(const uint8_t *data, uint16_t len)
{
    chunk_res_t r = chunk_rx_push(&s_my_vcard_rx, data, len);
    const uint8_t *text;
    size_t text_len = 0;

    if (r < 0) {
        s_chunk_errors++;
        return;
    }
    if (r != CHUNK_COMPLETE) return;

    text = chunk_rx_data(&s_my_vcard_rx, &text_len);
    if (text && s_vcard_fn) s_vcard_fn((const char *)text, text_len, s_vcard_ctx);

    /* Ready for the next provisioning without waiting for a reconnect. */
    chunk_rx_init(&s_my_vcard_rx);
}

static void handle_control(const uint8_t *data, uint16_t len)
{
    uint8_t op;

    if (len < 1) return;
    op = data[0];

    /*
     * control is not an encrypted characteristic — a bench session should not
     * need a pairing dance to select a carrier. But two of its opcodes touch
     * the wearer's identity, so those are gated here rather than by the
     * attribute permissions.
     */
    if ((op == BLE_CTRL_FAKE_RX || op == BLE_CTRL_FORGET) && !s_encrypted) return;

    if (s_control_fn) s_control_fn(op, data + 1, (size_t)(len - 1), s_control_ctx);
}

static bool subscribed(const uint8_t *value, uint16_t len)
{
    return len >= 2u
        && (little_endian_read_16(value, 0)
            & GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION) != 0u;
}

static int att_write(hci_con_handle_t con, uint16_t handle,
                     uint16_t transaction_mode, uint16_t offset,
                     uint8_t *buffer, uint16_t buffer_size)
{
    (void)con;

    /* Prepared writes would let a client assemble a long value server-side,
     * which is exactly the job chunk.c already does and does testably. */
    if (transaction_mode != ATT_TRANSACTION_MODE_NONE) return 0;
    if (offset != 0) return 0;

    switch (handle) {
    case H_MY_VCARD_VALUE:
        handle_my_vcard_chunk(buffer, buffer_size);
        break;
    case H_CONTROL_VALUE:
        handle_control(buffer, buffer_size);
        break;
    case H_RX_VCARD_CCC:
        s_sub_rx_vcard = subscribed(buffer, buffer_size);
        break;
    case H_STATUS_CCC:
        s_sub_status = subscribed(buffer, buffer_size);
        break;
    case H_TELEMETRY_CCC:
        s_sub_telemetry = subscribed(buffer, buffer_size);
        break;
    default:
        break;
    }
    return 0;
}

/* ---- events ----------------------------------------------------------- */

static void reset_connection_state(void)
{
    s_con = HCI_CON_HANDLE_INVALID;
    s_encrypted = false;
    s_mtu = BLE_MIN_ATT_MTU;
    s_rx_vcard_active = false;
    s_sub_rx_vcard = false;
    s_sub_status = false;
    s_sub_telemetry = false;
    s_status_pending = false;
    chunk_rx_init(&s_my_vcard_rx);
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;

    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING)
            start_advertising();
        break;

    case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(packet) == GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
            reset_connection_state();
            s_con = gap_subevent_le_connection_complete_get_connection_handle(packet);
            /*
             * The LED is the only thing that says "connected" when there is
             * no console attached, which during a §13 body test there will
             * not be. It is written here, in the BTstack context, and nowhere
             * else: the LED hangs off the CYW43, and an ioctl to it from the
             * main thread while Bluetooth is up stalled that thread until the
             * next Bluetooth interrupt (M2, measured — see apps/handoff).
             */
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        }
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE:
        reset_connection_state();
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        /*
         * Advertising stops on connection and BTstack does not restart it, so
         * a band that is disconnected while the wearer's phone is out of range
         * would be invisible until the next reset without this.
         */
        gap_advertisements_enable(1);
        break;

    case ATT_EVENT_CONNECTED:
        s_con = att_event_connected_get_handle(packet);
        break;

    case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        s_mtu = att_event_mtu_exchange_complete_get_MTU(packet);
        if (s_mtu < BLE_MIN_ATT_MTU) s_mtu = BLE_MIN_ATT_MTU;
        break;

    case ATT_EVENT_CAN_SEND_NOW:
        /*
         * One notify per can-send-now. A vCard chunk in flight wins; status
         * goes out on the next event, which the pump asks for anyway.
         */
        if (s_rx_vcard_active) {
            pump_rx_vcard();
            if (s_status_pending) att_server_request_can_send_now_event(s_con);
        } else if (s_status_pending && s_sub_status) {
            s_status_pending = false;
            att_server_notify(s_con, H_STATUS_VALUE,
                              (const uint8_t *)&s_status, sizeof s_status);
        }
        break;

    case ATT_EVENT_DISCONNECTED:
        reset_connection_state();
        break;

    default:
        break;
    }
}

static void sm_packet_handler(uint8_t packet_type, uint16_t channel,
                              uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;

    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
    case SM_EVENT_JUST_WORKS_REQUEST:
        /*
         * No display and no keypad, so Just Works is the only pairing method
         * available and it is accepted rather than confirmed. That buys
         * encryption and a bond but NOT protection against a man in the
         * middle at the one moment of pairing, which happens once in a
         * device's life with the band in the wearer's own hand. Recorded in
         * architecture §11.2 rather than left as a surprise.
         */
        sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        break;

    case SM_EVENT_PAIRING_COMPLETE:
        s_encrypted = sm_event_pairing_complete_get_status(packet) == ERROR_CODE_SUCCESS;
        break;

    case SM_EVENT_REENCRYPTION_COMPLETE:
        /* The reconnect path. M2's last exit criterion is that this happens
         * with no fresh OS pairing dialog after a Pico power cycle. */
        s_encrypted = sm_event_reencryption_complete_get_status(packet) == ERROR_CODE_SUCCESS;
        break;

    case SM_EVENT_IDENTITY_RESOLVING_SUCCEEDED:
        break;

    default:
        break;
    }
}

/* ---- public ----------------------------------------------------------- */

void ble_init(void)
{
    reset_connection_state();
    build_name_and_scan_response();

    /* So a status read before the first notify answers with a well-formed
     * struct the app can version-check, rather than sixteen zero bytes. */
    memset(&s_status, 0, sizeof s_status);
    s_status.version = BLE_STATUS_VERSION;

    l2cap_init();

    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    /*
     * Bonding, because the app must reconnect without a pairing dialog after
     * a power cycle. Secure Connections, because architecture §11.2 asks for
     * an encrypted link and LE legacy pairing's Just Works gives one that is
     * trivially passively decryptable.
     */
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING | SM_AUTHREQ_SECURE_CONNECTION);

    att_server_init(profile_data, att_read, att_write);
    att_server_register_packet_handler(packet_handler);

    s_hci_cb.callback = &packet_handler;
    hci_add_event_handler(&s_hci_cb);

    s_sm_cb.callback = &sm_packet_handler;
    sm_add_event_handler(&s_sm_cb);

    /*
     * The advertised name is the scan response built above, not
     * gap_set_local_name(): that call belongs to BTstack's Classic half and
     * does not link in an LE-only build. The GATT device name characteristic
     * stays the generic "Handoff" from ble_service.gatt; it is the scan
     * response that carries the per-board suffix, and that is what a scanner
     * and CompanionDeviceManager actually display.
     */
    hci_power_control(HCI_POWER_ON);
}

void ble_set_vcard_handler(ble_vcard_written_fn fn, void *ctx)
{
    s_vcard_fn = fn;
    s_vcard_ctx = ctx;
}

void ble_set_control_handler(ble_control_fn fn, void *ctx)
{
    s_control_fn = fn;
    s_control_ctx = ctx;
}

bool ble_notify_rx_vcard(const char *text, size_t len)
{
    if (s_con == HCI_CON_HANDLE_INVALID) return false;
    if (!s_encrypted) return false;
    if (s_rx_vcard_active) return false;
    if (len > sizeof s_rx_vcard_buf) return false;
    if (!s_sub_rx_vcard) return false;

    memcpy(s_rx_vcard_buf, text, len);
    if (chunk_tx_init(&s_rx_vcard_tx, s_rx_vcard_buf, len, notify_room())
            != CHUNK_MORE)
        return false;

    s_rx_vcard_active = true;
    att_server_request_can_send_now_event(s_con);
    return true;
}

bool ble_rx_vcard_busy(void) { return s_rx_vcard_active; }

bool ble_notify_status(const ble_status_t *st)
{
    s_status = *st;

    if (s_con == HCI_CON_HANDLE_INVALID) return false;
    if (!s_sub_status) return false;

    /*
     * Status fits one notification at the floor, so it needs no pacing — but
     * it is almost always sent from inside a write callback (provisioning,
     * a control opcode), where the outgoing buffer is reserved for the write
     * response and a direct att_server_notify() fails. Measured at M2: not
     * one status ever reached the phone that way. So it is deferred to the
     * next can-send-now, behind any vCard chunk in flight.
     */
    s_status_pending = true;
    att_server_request_can_send_now_event(s_con);
    return true;
}

bool ble_notify_telemetry(const void *scores, size_t len)
{
    if (s_con == HCI_CON_HANDLE_INVALID) return false;
    if (!s_sub_telemetry) return false;
    if (len > BLE_CHUNK_BYTES(s_mtu)) return false;

    /* Deliberately not chunked and deliberately not queued. A score stream
     * with a gap is a plot with a gap; a score stream with a backlog is a
     * stalled receive path. */
    return att_server_notify(s_con, H_TELEMETRY_VALUE,
                             (const uint8_t *)scores,
                             (uint16_t)len) == ERROR_CODE_SUCCESS;
}

bool ble_connected(void) { return s_con != HCI_CON_HANDLE_INVALID; }
bool ble_encrypted(void) { return s_encrypted; }

bool ble_telemetry_subscribed(void)
{
    return s_con != HCI_CON_HANDLE_INVALID && s_sub_telemetry;
}

uint16_t ble_att_mtu(void)      { return s_mtu; }
uint16_t ble_chunk_errors(void) { return s_chunk_errors; }
