#include "frag.h"

#include <string.h>

frag_err_t frag_split(const uint8_t *blob, size_t len, uint8_t record_id, frag_tx_t *t)
{
    size_t i = 0;
    uint8_t f = 0;

    memset(t, 0, sizeof *t);
    t->record_id = (uint8_t)(record_id & FRAME_MAX_RECORD_ID);

    while (i < len) {
        size_t used = 0;

        if (f >= FRAME_MAX_FRAGS) return FRAG_ERR_TOO_BIG;

        /*
         * Whole TLVs only. A fragment that holds half a TLV is undecodable on
         * its own, which would quietly undo architecture §8.4 — the carousel
         * repeats fragment 0 precisely so that one surviving fragment is a
         * usable contact. compact_encode_chunked() has already guaranteed no
         * single TLV is larger than a fragment.
         */
        while (i < len) {
            size_t tlv;

            if (blob[i] == TAG_NOP) { i++; continue; }
            if (i + 1u >= len) { i = len; break; }      /* malformed tail */

            tlv = 2u + blob[i + 1];
            if (i + tlv > len) { i = len; break; }
            if (used + tlv > HANDOFF_FRAG_PAYLOAD) {
                if (used == 0) return FRAG_ERR_TOO_BIG;  /* will never fit */
                break;
            }

            memcpy(t->data + (size_t)f * HANDOFF_FRAG_PAYLOAD + used, blob + i, tlv);
            used += tlv;
            i += tlv;
        }

        if (used == 0) break;
        t->len[f] = (uint8_t)used;
        f++;
    }

    t->count = f ? f : 1u;   /* an empty card is still one (empty) fragment */
    return FRAG_OK;
}

size_t frag_get(const frag_tx_t *t, uint8_t index, const uint8_t **payload)
{
    if (index >= t->count) return 0;
    if (payload) *payload = t->data + (size_t)index * HANDOFF_FRAG_PAYLOAD;
    return t->len[index];
}

/* ---- reassembly ------------------------------------------------------- */

void frag_rx_init(frag_rx_t *r)
{
    memset(r, 0, sizeof *r);
}

frag_err_t frag_rx_add(frag_rx_t *r, const frame_hdr_t *h,
                       const uint8_t *payload, size_t len)
{
    if (h->frag_index >= FRAME_MAX_FRAGS) return FRAG_ERR_BAD_INDEX;
    if (h->frag_index >= h->frag_count)   return FRAG_ERR_BAD_INDEX;

    /*
     * A record id change means the far end was re-provisioned mid-contact, or
     * we are hearing a different wristband. Either way the half-built blob is
     * worthless: start over rather than merge two people into one contact.
     */
    if (r->started && h->record_id != r->record_id) {
        frag_rx_init(r);
    }

    if (!r->started) {
        r->started   = true;
        r->record_id = h->record_id;
        r->count     = h->frag_count;
    } else if (h->frag_count != r->count) {
        return FRAG_ERR_STALE;
    }

    if (len > HANDOFF_FRAG_PAYLOAD) len = HANDOFF_FRAG_PAYLOAD;
    memcpy(r->buf + (size_t)h->frag_index * HANDOFF_FRAG_PAYLOAD, payload, len);
    if (len < HANDOFF_FRAG_PAYLOAD)
        memset(r->buf + (size_t)h->frag_index * HANDOFF_FRAG_PAYLOAD + len, TAG_NOP,
               HANDOFF_FRAG_PAYLOAD - len);

    r->have |= (uint16_t)(1u << h->frag_index);
    return FRAG_OK;
}

bool frag_rx_complete(const frag_rx_t *r)
{
    const uint16_t want = (uint16_t)((r->count >= 16) ? 0xFFFFu : ((1u << r->count) - 1u));
    return r->started && (r->have & want) == want;
}

uint8_t frag_rx_missing(const frag_rx_t *r)
{
    uint8_t i, miss = 0;
    if (!r->started) return 0;
    for (i = 0; i < r->count; i++)
        if (!(r->have & (1u << i))) miss++;
    return miss;
}

size_t frag_rx_blob(const frag_rx_t *r, const uint8_t **blob)
{
    if (blob) *blob = r->buf;
    return r->started ? (size_t)r->count * HANDOFF_FRAG_PAYLOAD : 0u;
}
