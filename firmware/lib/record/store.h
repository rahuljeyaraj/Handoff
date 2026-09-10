/*
 * Handoff — the wristband's own record. architecture §9.
 *
 * Provisioning is the delivery path in reverse:
 *
 *   phone --BLE write--> ble.c --> vcard.c --> compact.c --> store.c --> flash
 *
 * The wristband never stores vCard text, only the compact TLV blob it will
 * transmit, so the encoding cost is paid once at provisioning rather than at
 * every handshake.
 *
 * STUB until M2. The backing store is bound then, in hal_pico/flash.c; the
 * in-RAM path below works today so that M1's simulator and the host tests have
 * something to transmit.
 */
#ifndef HANDOFF_STORE_H
#define HANDOFF_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "compact.h"
#include "frame.h"

typedef enum {
    STORE_OK = 0,
    STORE_ERR_EMPTY   = -1,
    STORE_ERR_TOO_BIG = -2,
    STORE_ERR_BACKEND = -3   /* returned by the flash binding until M2 */
} store_err_t;

typedef struct {
    uint8_t blob[COMPACT_MAX_BLOB];
    uint16_t len;
    uint8_t  record_id;      /* 0..63, bumped on every successful write */
    bool     valid;
} store_t;

void        store_init(store_t *s);

/* Replace the stored record. Bumps record_id so a receiver mid-transfer does
 * not merge the old card with the new one (see frag_rx_add). */
store_err_t store_put(store_t *s, const uint8_t *blob, size_t len);

/* Convenience: parse vCard text, sort by priority, encode, store. */
store_err_t store_put_vcard(store_t *s, const char *text, size_t len);

store_err_t store_get(const store_t *s, const uint8_t **blob, size_t *len);
uint8_t     store_record_id(const store_t *s);

/* ---- flash backing, bound at M2 --------------------------------------- */

/* Both return STORE_ERR_BACKEND until hal_pico/flash.c is implemented. */
store_err_t store_load(store_t *s);
store_err_t store_save(const store_t *s);

#endif /* HANDOFF_STORE_H */
