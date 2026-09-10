#include "chunk.h"

#include <string.h>

/* ---- sender ----------------------------------------------------------- */

chunk_res_t chunk_tx_init(chunk_tx_t *t, const void *src, size_t len,
                          size_t chunk_bytes)
{
    size_t cap, total;

    memset(t, 0, sizeof *t);

    if (chunk_bytes <= CHUNK_HDR_BYTES) return CHUNK_ERR_HDR;

    cap = chunk_bytes - CHUNK_HDR_BYTES;
    if (cap > 255u) cap = 255u;          /* a full payload must fit in a byte */

    /* Round up, but never to zero: an empty message is one empty chunk. */
    total = (len + cap - 1u) / cap;
    if (total == 0u) total = 1u;
    if (total > CHUNK_MAX_CHUNKS) return CHUNK_ERR_SIZE;

    t->src   = (const uint8_t *)src;
    t->len   = (uint16_t)len;
    t->cap   = (uint8_t)cap;
    t->total = (uint8_t)total;
    return CHUNK_MORE;
}

bool chunk_tx_done(const chunk_tx_t *t) { return t->next >= t->total; }

uint8_t chunk_tx_total(const chunk_tx_t *t) { return t->total; }

size_t chunk_tx_next(chunk_tx_t *t, uint8_t *out, size_t max)
{
    size_t n;

    if (chunk_tx_done(t)) return 0;

    n = (size_t)(t->len - t->sent);
    if (n > t->cap) n = t->cap;
    if (max < CHUNK_HDR_BYTES + n) return 0;

    out[0] = t->next;
    out[1] = t->total;
    memcpy(out + CHUNK_HDR_BYTES, t->src + t->sent, n);

    t->sent += (uint16_t)n;
    t->next++;
    return CHUNK_HDR_BYTES + n;
}

/* ---- receiver --------------------------------------------------------- */

void chunk_rx_init(chunk_rx_t *r)
{
    memset(r, 0, sizeof *r);
}

static chunk_res_t rx_fail(chunk_rx_t *r, chunk_res_t why)
{
    chunk_rx_init(r);
    return why;
}

chunk_res_t chunk_rx_push(chunk_rx_t *r, const void *chunk, size_t n)
{
    const uint8_t *p = (const uint8_t *)chunk;
    uint8_t seq, total;
    size_t payload;
    bool last;

    if (n < CHUNK_HDR_BYTES) return rx_fail(r, CHUNK_ERR_HDR);

    seq     = p[0];
    total   = p[1];
    payload = n - CHUNK_HDR_BYTES;

    if (total == 0u)  return rx_fail(r, CHUNK_ERR_HDR);
    if (seq >= total) return rx_fail(r, CHUNK_ERR_HDR);

    /*
     * seq 0 is unconditionally a fresh message. A sender whose previous
     * transfer was interrupted — the app was killed, the link dropped mid-card
     * — recovers by starting again, and needs no way to say so beyond this.
     */
    if (seq == 0u) {
        chunk_rx_init(r);
        r->cap     = (uint16_t)payload;
        r->total   = total;
        r->started = true;
    } else {
        if (!r->started || seq != r->next || total != r->total)
            return rx_fail(r, CHUNK_ERR_SEQ);
    }

    last = (uint8_t)(seq + 1u) == total;

    /* Only the last chunk may be short. Accepting a ragged middle chunk would
     * shift every byte after it and still produce a plausible-looking card. */
    if (!last && payload != r->cap) return rx_fail(r, CHUNK_ERR_RAGGED);
    if (last && payload > r->cap)   return rx_fail(r, CHUNK_ERR_RAGGED);

    /* Reject on the first chunk rather than after filling the buffer, so a
     * phone claiming 255 chunks costs one rejected write and not 254. */
    if ((size_t)r->total * r->cap > CHUNK_RX_MAX) return rx_fail(r, CHUNK_ERR_SIZE);
    if ((size_t)r->len + payload > CHUNK_RX_MAX)  return rx_fail(r, CHUNK_ERR_SIZE);

    memcpy(r->buf + r->len, p + CHUNK_HDR_BYTES, payload);
    r->len += (uint16_t)payload;
    r->next = (uint8_t)(seq + 1u);

    if (!last) return CHUNK_MORE;

    r->complete = true;
    return CHUNK_COMPLETE;
}

const uint8_t *chunk_rx_data(const chunk_rx_t *r, size_t *len)
{
    if (!r->complete) return NULL;
    if (len) *len = r->len;
    return r->buf;
}

uint8_t chunk_rx_received(const chunk_rx_t *r) { return r->started ? r->next : 0u; }
uint8_t chunk_rx_total(const chunk_rx_t *r)    { return r->total; }
