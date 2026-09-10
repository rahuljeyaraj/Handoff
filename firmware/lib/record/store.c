#include "store.h"

#include <string.h>

#include "vcard.h"

void store_init(store_t *s)
{
    memset(s, 0, sizeof *s);
}

store_err_t store_put(store_t *s, const uint8_t *blob, size_t len)
{
    if (len > sizeof s->blob) return STORE_ERR_TOO_BIG;

    memcpy(s->blob, blob, len);
    s->len = (uint16_t)len;
    s->valid = true;
    s->record_id = (uint8_t)((s->record_id + 1u) & FRAME_MAX_RECORD_ID);
    return STORE_OK;
}

store_err_t store_put_vcard(store_t *s, const char *text, size_t len)
{
    compact_rec_t rec;
    uint8_t blob[COMPACT_MAX_BLOB];
    size_t blen = 0;
    compact_err_t e;

    e = vcard_parse(text, len, &rec);
    if (e != COMPACT_OK) return STORE_ERR_TOO_BIG;

    compact_sort_priority(&rec);

    /* Chunk to what a fragment can hold, so frag_split never has to cut a TLV
     * in half. See the note in compact.h. */
    e = compact_encode_chunked(&rec, HANDOFF_FRAG_PAYLOAD - 2u, blob, sizeof blob, &blen);
    if (e != COMPACT_OK) return STORE_ERR_TOO_BIG;

    return store_put(s, blob, blen);
}

store_err_t store_get(const store_t *s, const uint8_t **blob, size_t *len)
{
    if (!s->valid) return STORE_ERR_EMPTY;
    if (blob) *blob = s->blob;
    if (len)  *len = s->len;
    return STORE_OK;
}

uint8_t store_record_id(const store_t *s) { return s->record_id; }

/* ---- persistence ------------------------------------------------------ */

/*
 * Registered from below, so that store.c names no transport and architecture
 * §3.2 survives contact with M2. NULL until something registers one, and that
 * is the honest state for the whole of M1.
 */
static const store_backend_t *s_backend;

void store_set_backend(const store_backend_t *b) { s_backend = b; }

store_err_t store_load(store_t *s)
{
    size_t len = 0;
    uint8_t id = 0;

    if (!s_backend || !s_backend->load) return STORE_ERR_BACKEND;

    store_init(s);
    if (!s_backend->load(s->blob, sizeof s->blob, &len, &id))
        return STORE_ERR_EMPTY;

    /* A backend that hands back more than the blob can hold has already
     * overrun it, so this is a belt-and-braces check on a bug, not on data. */
    if (len > sizeof s->blob) {
        store_init(s);
        return STORE_ERR_TOO_BIG;
    }

    s->len       = (uint16_t)len;
    s->record_id = (uint8_t)(id & FRAME_MAX_RECORD_ID);
    s->valid     = true;
    return STORE_OK;
}

store_err_t store_save(const store_t *s)
{
    if (!s_backend || !s_backend->save) return STORE_ERR_BACKEND;
    if (!s->valid) return STORE_ERR_EMPTY;

    /*
     * record_id is saved with the blob rather than being restarted from zero
     * at every boot. A receiver mid-transfer keys reassembly on it (see
     * frag_rx_add), so a wristband that power-cycled between two frames must
     * not claim to still be sending the record it was sending before.
     */
    return s_backend->save(s->blob, s->len, s->record_id) ? STORE_OK
                                                          : STORE_ERR_BACKEND;
}

store_err_t store_forget(store_t *s)
{
    store_init(s);
    if (!s_backend || !s_backend->erase) return STORE_ERR_BACKEND;
    return s_backend->erase() ? STORE_OK : STORE_ERR_BACKEND;
}
