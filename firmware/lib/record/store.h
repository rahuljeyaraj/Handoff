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
 * The in-RAM path is M1's. M2 adds persistence, and does it by INVERSION: the
 * backend is registered from below (hal_pico/flash.c calls store_set_backend
 * at start-up) rather than called from here. store.c is in the sandboxed half
 * of architecture §3.2 and may not name a transport, and this is also what
 * lets the persistence logic itself be host-tested against a fake backend
 * instead of only against a board.
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

/* ---- persistence ------------------------------------------------------ */

/*
 * The backing store, registered from below. hal_pico/flash.c implements this
 * against the last usable flash sector; test/host fakes it in RAM.
 *
 * Deliberately three plain functions rather than an opaque handle: the blob is
 * a few hundred bytes and there is exactly one of it, so a backend that cannot
 * be described in three lines is a backend doing too much.
 */
typedef struct {
    bool (*load)(uint8_t *blob, size_t max, size_t *len, uint8_t *record_id);
    bool (*save)(const uint8_t *blob, size_t len, uint8_t record_id);
    bool (*erase)(void);
} store_backend_t;

/* Pass NULL to unbind. The pointer is kept, not copied. */
void store_set_backend(const store_backend_t *b);

/*
 * All three return STORE_ERR_BACKEND when no backend is registered — which is
 * the whole of M1, and every host test that has not deliberately fitted one.
 * They fail loudly rather than silently pretending to have persisted anything.
 */
store_err_t store_load(store_t *s);
store_err_t store_save(const store_t *s);
store_err_t store_forget(store_t *s);

#endif /* HANDOFF_STORE_H */
