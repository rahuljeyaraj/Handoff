/*
 * Handoff — BLE GATT service. architecture §11.2. Development plan M2.
 *
 * The phone client is settled: a NATIVE ANDROID APP, no web client, no iOS
 * (architecture §11). A backgrounded browser tab drops the GATT link, and the
 * phone is in a pocket during a handshake — which is the one thing this link
 * has to survive. The app lives in android/ and is not built by the Pico SDK.
 *
 * The firmware is insulated from that decision regardless: it exposes this
 * service and nothing above it.
 *
 * The contract:
 *
 *   my_vcard    write, chunked   the phone provisions the wristband (§9)
 *   rx_vcard    notify, chunked  received contact, as reconstructed vCard text
 *   status      notify           link state, last score, fragment bitmap, errors
 *   telemetry   notify           decimated score stream for §14.1 body tests
 *   control     write            carrier select, trigger raw capture, force role
 *
 * The UUIDs and the security properties are in handoff.gatt, which is the
 * actual contract with android/ and is compiled into the attribute database.
 *
 * Chunking: ATT MTU is not guaranteed above 23 bytes, so every chunked
 * characteristic carries a 2-byte seq|total header and reassembles at the far
 * end. The framing itself is lib/link/chunk.c — host-testable, and tested at
 * the 23-byte floor rather than at whatever a developer's handset negotiates.
 * This file only binds it to ATT.
 *
 * telemetry exists because design §13 forbids a mains-tethered USB laptop
 * while anyone is touching an electrode. During body tests BLE is the only
 * legal way data leaves the wristband.
 */
#ifndef HANDOFF_BLE_H
#define HANDOFF_BLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chunk.h"

#define BLE_CHUNK_HDR_BYTES CHUNK_HDR_BYTES
#define BLE_MIN_ATT_MTU     23   /* the floor everything must work at */

/* An ATT notification spends 3 bytes on opcode and handle. Everything else in
 * the MTU is ours, and chunk.c takes 2 of what is left. */
#define BLE_ATT_NOTIFY_OVERHEAD 3
#define BLE_CHUNK_BYTES(mtu)    ((size_t)(mtu) - BLE_ATT_NOTIFY_OVERHEAD)

/* The largest vCard the phone may provision, and the largest we will notify.
 * Bounded by the reassembly buffer at the far end, not by BLE. */
#define BLE_VCARD_MAX CHUNK_RX_MAX

/* ---- control opcodes -------------------------------------------------- */

/*
 * One byte of opcode, then that opcode's arguments. Unknown opcodes are
 * counted and ignored rather than refused, so an app built against a later
 * firmware does not have to feature-detect before it can connect.
 *
 * Most of these are the hooks the later milestones bind to. They are listed
 * now because architecture §1 says the seams are created before they are
 * filled, and an app that can already send them is one less thing to change
 * at M4 and M14.
 */
typedef enum {
    BLE_CTRL_NOP          = 0x00,
    BLE_CTRL_CARRIER      = 0x01,  /* u8: 0 = 40 kHz, 1 = 200 kHz     (M3)  */
    BLE_CTRL_RAW_TRIGGER  = 0x02,  /* no args, triggered burst        (M4)  */
    BLE_CTRL_FORCE_ROLE   = 0x03,  /* u8: 0 = target, 1 = initiator   (M14) */
    BLE_CTRL_TLM_DECIMATE = 0x04,  /* u16 LE, 0 disables telemetry    (M4)  */
    BLE_CTRL_FAKE_RX      = 0x05,  /* u8 delay seconds                (M2)  */
    BLE_CTRL_FORGET       = 0x06   /* erase the provisioned record    (M2)  */
} ble_ctrl_op_t;

/* ---- status ----------------------------------------------------------- */

#define BLE_STATUS_VERSION 2

#define BLE_ST_ENCRYPTED   0x01u  /* the link is bonded and encrypted        */
#define BLE_ST_PROVISIONED 0x02u  /* a record is stored and transmittable    */
#define BLE_ST_FLASH_OK    0x04u  /* the flash backend registered at boot    */
#define BLE_ST_TLM_ON      0x08u  /* telemetry notifications are subscribed  */
#define BLE_ST_USB_POWER   0x10u  /* VBUS present at the Pico (v2). NOT charging: the
                                     charger is off-board on J5 and invisible here */

/*
 * 20 bytes, which is exactly one notification at the 23-byte ATT floor
 * (3 bytes of opcode and handle). Little-endian, and versioned, because
 * android/ parses it by offset.
 *
 * Version 1 was the first 16 bytes. Version 2 appended the supply and the
 * firmware version; the app reads any version >= 1 by offset and ignores
 * what it does not know, so a field is only ever appended, never moved.
 * There is no room left at the floor: a version 3 needs a second notify.
 */
typedef struct {
    uint8_t  version;
    uint8_t  flags;         /* BLE_ST_*                                     */
    uint8_t  link_state;    /* proto/link_sm.h state; IDLE until M13         */
    uint8_t  record_id;     /* the wristband's own record, 0..63             */
    uint16_t last_score;    /* most recent Goertzel score      (M4 onward)   */
    uint16_t own_blob_len;  /* compact TLV bytes stored                      */
    uint32_t frag_bitmap;   /* reassembly of the incoming record  (§8.3)     */
    uint16_t chunk_errors;  /* phone-link chunks rejected since boot         */
    uint16_t frame_errors;  /* body-link CRC failures since boot             */
    /* ---- version 2 ---- */
    uint8_t  vsys_20mv;     /* VSYS after D1, in 20 mV steps (0..5.1 V); 0 = not read */
    uint8_t  fw_major;      /* HANDOFF_FW_VERSION_*                          */
    uint8_t  fw_minor;
    uint8_t  fw_patch;
} ble_status_t;

_Static_assert(sizeof(ble_status_t) == 20, "status must fit one notify at the 23-byte floor");

/* ---- handlers --------------------------------------------------------- */

/* A complete provisioning card. The text is NOT null-terminated and the
 * buffer is reused as soon as this returns. */
typedef void (*ble_vcard_written_fn)(const char *text, size_t len, void *ctx);

/* One control write. arg points at the bytes after the opcode. */
typedef void (*ble_control_fn)(uint8_t op, const uint8_t *arg, size_t len, void *ctx);

/* ---- lifecycle -------------------------------------------------------- */

/*
 * cyw43_arch_init() must already have succeeded: BTstack runs inside the
 * cyw43 async context, and on this board the Bluetooth controller and the
 * wireless chip are the same die on the same bus.
 *
 * Starts advertising and returns; nothing here blocks.
 */
void ble_init(void);

void ble_set_vcard_handler(ble_vcard_written_fn fn, void *ctx);
void ble_set_control_handler(ble_control_fn fn, void *ctx);

/* ---- notifications ---------------------------------------------------- */

/*
 * Push a received contact to the phone, chunked. The text is copied, so the
 * caller's buffer is free on return; the chunks are then paced out as the
 * controller allows.
 *
 * Refuses — returning false — when there is no subscriber, when the link is
 * not encrypted, when a previous card is still going out, or when the text is
 * longer than BLE_VCARD_MAX.
 */
bool ble_notify_rx_vcard(const char *text, size_t len);
bool ble_rx_vcard_busy(void);

bool ble_notify_status(const ble_status_t *st);

/* Decimated scores, design §13. Dropped rather than queued when the
 * controller is busy: a gap in a score stream is a gap in a plot, and a
 * backlog is a stall in the receive path. */
bool ble_notify_telemetry(const void *scores, size_t len);

/* ---- state ------------------------------------------------------------ */

bool     ble_connected(void);

/* The advertised name, "Handoff 7A3C". Valid after ble_init(). */
const char *ble_local_name(void);
bool     ble_encrypted(void);
bool     ble_telemetry_subscribed(void);

/* What the phone actually negotiated. BLE_MIN_ATT_MTU until it asks for more,
 * and the number every chunked transfer is sized from. */
uint16_t ble_att_mtu(void);

/* Chunks rejected by the reassembler since boot, for ble_status_t. */
uint16_t ble_chunk_errors(void);

#endif /* HANDOFF_BLE_H */
