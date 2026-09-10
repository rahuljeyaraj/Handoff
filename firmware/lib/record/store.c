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

/*
 * M2 binds these to hal_pico/flash.c: one compact blob plus a record id in the
 * last flash sector. Until then they fail loudly rather than silently pretend
 * to have persisted anything.
 */
store_err_t store_load(store_t *s)       { (void)s; return STORE_ERR_BACKEND; }
store_err_t store_save(const store_t *s) { (void)s; return STORE_ERR_BACKEND; }
