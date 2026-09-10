/* Handoff — BLE GATT service. STUB until M2. See ble.h. */
#include "ble.h"

void ble_init(void) { }

void ble_set_vcard_handler(ble_vcard_written_fn fn, void *ctx)
{
    (void)fn; (void)ctx;
}

bool ble_notify_rx_vcard(const char *t, size_t n) { (void)t; (void)n; return false; }
bool ble_notify_status(const void *p, size_t n)   { (void)p; (void)n; return false; }
bool ble_notify_telemetry(const void *p, size_t n){ (void)p; (void)n; return false; }
bool ble_connected(void)                          { return false; }
