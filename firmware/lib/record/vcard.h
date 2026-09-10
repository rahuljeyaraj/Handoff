/*
 * Handoff — vCard 3.0 text <-> compact field list. architecture §8.5.
 *
 * Parsing maps known properties onto the tag registry and anything else onto
 * TAG_RAW verbatim, so nothing is silently dropped. Rendering reconstructs a
 * well-formed card from whatever fragments arrived: BEGIN, VERSION and END are
 * synthesised, and N is derived from FN when N did not make it across.
 *
 * tools/vcf.py is an independent reference implementation of the same codec.
 * M1 cross-checks C against Python in both directions — the same discipline
 * the development plan applies to the modulation vectors, and for the same
 * reason: a shared misreading cancels out and passes.
 */
#ifndef HANDOFF_VCARD_H
#define HANDOFF_VCARD_H

#include <stddef.h>

#include "compact.h"

/* text -> fields. Accepts CRLF or LF, and RFC 2425 line folding. */
compact_err_t vcard_parse(const char *text, size_t len, compact_rec_t *out);

/* fields -> text. Emits CRLF line endings, as vCard 3.0 requires. */
compact_err_t vcard_render(const compact_rec_t *r, char *out, size_t max, size_t *out_len);

#endif /* HANDOFF_VCARD_H */
