/*
 * Handoff — BLE GATT service. architecture §11.2. STUB UNTIL M2.
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
 * Chunking: ATT MTU is not guaranteed above 23 bytes, so every chunked
 * characteristic carries a 2-byte seq|total header and reassembles client-side.
 * M2 tests that at the 23-byte floor, not at whatever the phone negotiates.
 *
 * telemetry exists because design §13 forbids a mains-tethered USB laptop while
 * anyone is touching an electrode. During body tests BLE is the only legal way
 * data leaves the wristband.
 */
#ifndef HANDOFF_BLE_H
#define HANDOFF_BLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLE_CHUNK_HDR_BYTES 2    /* seq | total */
#define BLE_MIN_ATT_MTU     23   /* the floor everything must work at */

typedef void (*ble_vcard_written_fn)(const char *text, size_t len, void *ctx);

void ble_init(void);
void ble_set_vcard_handler(ble_vcard_written_fn fn, void *ctx);

/* Push a received contact to the phone, chunked. */
bool ble_notify_rx_vcard(const char *text, size_t len);

bool ble_notify_status(const void *status, size_t len);
bool ble_notify_telemetry(const void *scores, size_t len);

bool ble_connected(void);

#endif /* HANDOFF_BLE_H */
