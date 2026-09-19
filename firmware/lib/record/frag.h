/*
 * Handoff — fragmentation and reassembly. architecture §8.3.
 *
 * A TLV blob is cut into at most FRAME_MAX_FRAGS pieces of at most
 * HANDOFF_FRAG_PAYLOAD bytes. Two properties are deliberate:
 *
 * TLV-ALIGNED CUTS. A fragment boundary falls between whole TLVs wherever
 * one fits, so any fragment that arrives is decodable on its own. Without
 * this, architecture §8.4's "250 ms gives you a name and a mobile" is not
 * true — a half-received TLV decodes to nothing. A single TLV larger than
 * the payload is split anyway, and its continuation is marked so the
 * reassembler knows the two halves must both arrive.
 *
 * NO ORDERING ASSUMPTION. The receiver keeps a bitmap and takes fragments in
 * whatever order the carousel and the channel deliver them.
 */
#ifndef HANDOFF_FRAG_H
#define HANDOFF_FRAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "compact.h"
#include "frame.h"

typedef struct {
    uint8_t  data[FRAME_MAX_FRAGS * HANDOFF_FRAG_PAYLOAD];
    uint8_t  len[FRAME_MAX_FRAGS];    /* bytes used in each fragment */
    uint8_t  count;
    uint8_t  record_id;
} frag_tx_t;

typedef enum {
    FRAG_OK = 0,
    FRAG_ERR_TOO_BIG   = -1,   /* would need more than FRAME_MAX_FRAGS */
    FRAG_ERR_BAD_INDEX = -2,
    FRAG_ERR_STALE     = -3    /* different record id: a re-provisioned card */
} frag_err_t;

/* Split a compact TLV blob. Fragment 0 gets the highest-priority TLVs, which
 * is why compact_sort_priority() runs before this. */
frag_err_t frag_split(const uint8_t *blob, size_t len, uint8_t record_id, frag_tx_t *t);

/* Fragment payload, ready for frame_encode(). Returns bytes, 0 on bad index. */
size_t     frag_get(const frag_tx_t *t, uint8_t index, const uint8_t **payload);

/* ---- reassembly ------------------------------------------------------- */

typedef struct {
    uint8_t  buf[FRAME_MAX_FRAGS * HANDOFF_FRAG_PAYLOAD];
    uint16_t have;          /* bitmap, bit i set when fragment i arrived */
    uint8_t  count;         /* from the first fragment seen              */
    uint8_t  record_id;
    bool     started;
} frag_rx_t;

void       frag_rx_init(frag_rx_t *r);
frag_err_t frag_rx_add(frag_rx_t *r, const frame_hdr_t *h,
                       const uint8_t *payload, size_t len);
bool       frag_rx_complete(const frag_rx_t *r);

/* Fragment `index` has arrived. Fragment 0 is the name and the mobile, so this
 * is what decides whether a cut-short contact still gives the wearer a card. */
bool       frag_rx_has(const frag_rx_t *r, uint8_t index);

/* Number of fragments still missing. Drives the status characteristic. */
uint8_t    frag_rx_missing(const frag_rx_t *r);

/*
 * Everything that has arrived, as a TLV blob. Fragments that are missing leave
 * NOP padding in their place, which compact_decode() skips — so this is
 * decodable at any point during a transfer, not only at the end. That is the
 * mechanism behind architecture §8.4's graceful degradation table.
 */
size_t     frag_rx_blob(const frag_rx_t *r, const uint8_t **blob);

/*
 * What a contact cut short can safely hand to the phone, as a TLV blob in
 * `out` (at least FRAME_MAX_FRAGS * HANDOFF_FRAG_PAYLOAD bytes). Returns 0 when
 * fragment 0 — the name and the mobile — is not usable: no name, no card.
 *
 * frag_rx_blob() is not enough here. A field longer than a fragment is a head
 * that fills its fragment and a TAG_CONT that opens the next one, and with one
 * half missing the other decodes as something wrong: a continuation appended
 * to whatever field came before, or an address cut off mid-street. So a head
 * whose continuation did not arrive is dropped, and so is a continuation whose
 * head did not. Short, never wrong. A head that exactly fills its fragment and
 * is followed by a missing one is dropped too — it cannot be told from a split
 * field, and losing it is the safe side.
 */
size_t     frag_rx_partial(const frag_rx_t *r, uint8_t *out, size_t max);

#endif /* HANDOFF_FRAG_H */
