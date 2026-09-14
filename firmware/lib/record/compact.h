/*
 * Handoff — compact TLV. field list <-> wire bytes. architecture §8.2.
 *
 *   tag(1) | len(1) | value(len)
 *
 * A realistic vCard is ~169 bytes of text and contact lasts about a second
 * (R1), so the text itself does not fit — not even one pass. The wire carries
 * this encoding and vcard.c rebuilds a full RFC-compliant card at the far end.
 * The user still gets a real .vcf; only the boilerplate stops crossing the body.
 *
 * Tag 0x00 is NOP padding, emitted by frame.c to fill a fixed-length payload
 * and skipped on decode. Tag 0xFF carries any property not in the registry
 * verbatim, at full cost — nothing is silently dropped.
 *
 * All lengths are BYTES, not characters. The codec is UTF-8 throughout and a
 * len never splits a multi-byte sequence.
 */
#ifndef HANDOFF_COMPACT_H
#define HANDOFF_COMPACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- tag registry (architecture §8.2) --------------------------------- */

#define TAG_NOP        0x00u   /* padding; no length byte follows           */
#define TAG_FN         0x01u   /* UTF-8                                     */
#define TAG_N          0x02u   /* UTF-8, ';' separated                      */
#define TAG_TEL_CELL   0x03u   /* DEPRECATED, decode only: see TAG_TEL      */
#define TAG_TEL_WORK   0x04u   /* DEPRECATED, decode only                   */
#define TAG_EMAIL      0x05u   /* UTF-8 local part + 1 byte domain id       */
#define TAG_ORG        0x06u
#define TAG_TITLE      0x07u
#define TAG_URL        0x08u
#define TAG_ADR        0x09u   /* UTF-8, ';' separated                      */
#define TAG_NOTE       0x0Au
#define TAG_TEL        0x0Bu   /* label byte + u16 country BE + packed BCD  */
#define TAG_CONT       0xFEu   /* continuation of the previous TLV          */
#define TAG_RAW        0xFFu   /* a whole vCard line, verbatim              */

/* ---- phone labels ------------------------------------------------------
 *
 * A phone is a number AND what it is for, which the wearer picks the way the
 * phone's own Contacts app offers it: four named labels and a custom one.
 * TAG_TEL_CELL and TAG_TEL_WORK were one tag per label, which could carry
 * neither a custom label nor the same label twice (two mobiles); the label
 * moved into the value instead, so one tag now covers every phone a card has.
 *
 *   label(1) | [ len(1) | UTF-8 label text ]  <- the bracket only when CUSTOM
 *            | u16 country code BE | packed BCD
 *
 * The whole phone — label, custom text and number — is ONE TLV, so frag.c
 * either places all of it in a fragment or none of it, and a number can never
 * arrive wearing the wrong label. TEL_LABEL_NONE is a TEL that named no type
 * at all; it is a real state, not a default, and vcard.c writes it back out
 * as a bare TEL.
 *
 * Bands flashed before this change still send TAG_TEL_CELL and TAG_TEL_WORK.
 * Both are decoded forever, as MOBILE and WORK; neither is ever encoded.
 */
#define TEL_LABEL_NONE    0x00u
#define TEL_LABEL_MOBILE  0x01u
#define TEL_LABEL_WORK    0x02u
#define TEL_LABEL_HOME    0x03u
#define TEL_LABEL_MAIN    0x04u
#define TEL_LABEL_CUSTOM  0xFFu

/* Long enough for a label somebody would actually type on a phone keyboard,
 * short enough that it cannot crowd the number out of fragment 0. */
#define COMPACT_TEL_LABEL_MAX  32

#define COMPACT_MAX_FIELDS   16
#define COMPACT_MAX_VALUE    128
#define COMPACT_MAX_BLOB     512

/* Domain dictionary. Index 0 is reserved so that 0xFF can mean "literal",
 * and a decoder that reads 0 knows it read padding, not gmail.com. */
#define COMPACT_DOMAIN_LITERAL 0xFFu

typedef struct {
    uint8_t tag;
    uint8_t len;
    uint8_t val[COMPACT_MAX_VALUE];
} compact_field_t;

typedef struct {
    compact_field_t f[COMPACT_MAX_FIELDS];
    uint8_t n;
} compact_rec_t;

typedef enum {
    COMPACT_OK = 0,
    COMPACT_ERR_FULL      = -1,
    COMPACT_ERR_TOO_LONG  = -2,
    COMPACT_ERR_MALFORMED = -3,
    COMPACT_ERR_PHOTO     = -4   /* architecture §8.2: rejected, not truncated */
} compact_err_t;

void         compact_rec_init(compact_rec_t *r);
compact_err_t compact_add(compact_rec_t *r, uint8_t tag, const uint8_t *val, size_t len);
const compact_field_t *compact_find(const compact_rec_t *r, uint8_t tag);

/* Priority order, most useful first: FN, TEL;CELL, EMAIL, then the rest.
 * frag.c packs whole TLVs in this order, so fragment 0 is a usable contact on
 * its own and a 250 ms handshake still lands a name and a mobile number. */
void         compact_sort_priority(compact_rec_t *r);
uint8_t      compact_tag_priority(uint8_t tag);

compact_err_t compact_encode(const compact_rec_t *r, uint8_t *out, size_t max, size_t *out_len);

/*
 * As compact_encode, but no single TLV value exceeds max_value bytes: a longer
 * value becomes a head TLV under its own tag followed by TAG_CONT TLVs.
 *
 * This is what keeps fragments self-contained. frag.c refuses to cut a TLV in
 * half, so without chunking a 100-byte NOTE could not be sent at all; with it,
 * every fragment holds whole TLVs and decodes on its own, and a lost middle
 * fragment costs one field rather than resynchronising the whole blob.
 */
compact_err_t compact_encode_chunked(const compact_rec_t *r, size_t max_value,
                                     uint8_t *out, size_t max, size_t *out_len);

/*
 * TAG_CONT values are appended to the field before them. A CONT with no
 * predecessor — its head was in a fragment that never arrived — is dropped.
 *
 * A truncated final TLV is NOT an error: it is the normal shape of a partially
 * received blob. Decoding stops there and keeps everything before it. Pass
 * consumed to find out where it stopped.
 */
compact_err_t compact_decode(const uint8_t *in, size_t len, compact_rec_t *out);
compact_err_t compact_decode_ex(const uint8_t *in, size_t len, compact_rec_t *out,
                                size_t *consumed);

/* ---- helpers the vCard layer and the tests both need ------------------ */

/* "+44 7700 900123" -> u16 country BE + packed BCD, nibble 0xF pads a final
 * odd digit. Returns bytes written, 0 on failure. */
size_t       compact_phone_pack(const char *text, uint8_t *out, size_t max);
size_t       compact_phone_unpack(const uint8_t *in, size_t len, char *out, size_t max);

/*
 * A whole TAG_TEL value: the label, its text when the label is CUSTOM, and the
 * packed number. [custom] is ignored unless [label] is TEL_LABEL_CUSTOM, and a
 * CUSTOM with no text is written as TEL_LABEL_NONE rather than as a label that
 * says nothing. Returns bytes written, 0 on failure.
 */
size_t       compact_tel_pack(uint8_t label, const char *custom, const char *number,
                              uint8_t *out, size_t max);

/*
 * The reverse. [label] and [number] are always written; [custom] is written
 * (NUL-terminated, possibly empty) whenever custom_max is non-zero. Accepts a
 * TAG_TEL_CELL or TAG_TEL_WORK value too — pass the tag as [tag] and the label
 * comes back MOBILE or WORK, which is what those tags always meant. Returns
 * the number's length in characters, 0 on failure.
 */
size_t       compact_tel_unpack(uint8_t tag, const uint8_t *in, size_t len,
                                uint8_t *label, char *custom, size_t custom_max,
                                char *number, size_t number_max);

int          compact_domain_id(const char *domain);       /* -1 if not in dict */
const char  *compact_domain_name(uint8_t id);             /* NULL if not in dict */
size_t       compact_domain_count(void);

#endif /* HANDOFF_COMPACT_H */
