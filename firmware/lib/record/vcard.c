#include "vcard.h"

#include <string.h>

#define VC_MAX_LINE 512

typedef struct { const char *name; uint8_t tag; } prop_map_t;

/*
 * Property NAMES only. Parameters are stripped before the lookup, because a
 * card in the wild writes EMAIL;TYPE=INTERNET, EMAIL;TYPE=HOME or bare EMAIL
 * and all three are the same property. Getting this wrong is quiet and
 * expensive: the property falls through to TAG_RAW, still crosses correctly,
 * and simply costs four times the airtime.
 */
static const prop_map_t k_props[] = {
    { "FN",    TAG_FN },
    { "N",     TAG_N },
    { "TEL",   TAG_TEL },       /* its label comes from the parameters, below */
    { "EMAIL", TAG_EMAIL },
    { "ORG",   TAG_ORG },
    { "TITLE", TAG_TITLE },
    { "URL",   TAG_URL },
    { "ADR",   TAG_ADR },
    { "NOTE",  TAG_NOTE },
};

static const char *const k_tag_names[] = {
    NULL, "FN", "N", "TEL;TYPE=CELL", "TEL;TYPE=WORK", "EMAIL",
    "ORG", "TITLE", "URL", "ADR", "NOTE"
};

/* Indexed by the TEL_LABEL_* byte. CUSTOM and NONE are written by hand. */
static const char *const k_tel_types[] = {
    NULL, "CELL", "WORK", "HOME", "MAIN"
};

static bool ieq(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return false;
    }
    return true;
}

/* Split "PROP;PARAM:value" at the first unquoted colon. */
static const char *split_colon(const char *line, size_t len, size_t *name_len)
{
    size_t i;
    bool q = false;
    for (i = 0; i < len; i++) {
        if (line[i] == '"') q = !q;
        else if (line[i] == ':' && !q) { *name_len = i; return line + i + 1; }
    }
    return NULL;
}

/* Case-insensitive substring search over a bounded, non-terminated span. */
static bool has_param(const char *s, size_t len, const char *needle)
{
    const size_t nl = strlen(needle);
    size_t i;
    if (nl > len) return false;
    for (i = 0; i + nl <= len; i++)
        if (ieq(s + i, needle, nl)) return true;
    return false;
}

static uint8_t map_property(const char *name, size_t len)
{
    size_t base = 0, i;

    while (base < len && name[base] != ';') base++;

    for (i = 0; i < sizeof k_props / sizeof k_props[0]; i++) {
        const size_t pl = strlen(k_props[i].name);
        if (base != pl || !ieq(name, k_props[i].name, pl)) continue;

        return k_props[i].tag;
    }
    return TAG_RAW;
}

/*
 * TEL parameters -> a label byte, and the text of it when it is a custom one.
 *
 * The four named labels are the ones the editor offers. Anything else that
 * looks like a type — Android writes a custom label as TYPE=X-Reception — is
 * kept verbatim as CUSTOM, because a label the wearer typed is a label they
 * meant. A TEL with no type at all stays NONE: it is not a mobile, it is a
 * phone number whose owner never said what it was for, and guessing wrong
 * prints the wrong word under somebody's number.
 */
static uint8_t tel_label(const char *name, size_t len, char *custom, size_t custom_max)
{
    size_t base = 0, i = 0;
    const char *params;
    size_t plen;

    if (custom_max) custom[0] = '\0';

    /* [name] is the whole property as written, "TEL;TYPE=HOME"; the parameters
     * start at the first ';', exactly as map_property finds them. */
    while (base < len && name[base] != ';') base++;
    params = name + base;
    plen = len - base;

    if (has_param(params, plen, "CELL") || has_param(params, plen, "MOBILE"))
        return TEL_LABEL_MOBILE;
    if (has_param(params, plen, "WORK")) return TEL_LABEL_WORK;
    if (has_param(params, plen, "HOME")) return TEL_LABEL_HOME;
    if (has_param(params, plen, "MAIN") || has_param(params, plen, "PREF"))
        return TEL_LABEL_MAIN;

    /* ";TYPE=X-Reception" or ";X-Reception": take what follows the X-. */
    while (i + 2u < plen) {
        if ((params[i] == 'X' || params[i] == 'x') && params[i + 1] == '-') {
            size_t o = 0;
            i += 2;
            while (i < plen && params[i] != ';' && params[i] != ',' && o + 1u < custom_max)
                custom[o++] = params[i++];
            if (custom_max) custom[o] = '\0';
            return o ? TEL_LABEL_CUSTOM : TEL_LABEL_NONE;
        }
        i++;
    }

    return TEL_LABEL_NONE;
}

/*
 * EMAIL "alice@gmail.com" -> "alice" + one domain byte. An unknown domain is
 * carried literally after a 0xFF marker, so nothing is lost — it just costs
 * what it would have cost anyway.
 */
static size_t pack_email(const char *v, size_t vlen, uint8_t *out, size_t max)
{
    size_t at = 0, o;
    char domain[96];
    int id;

    while (at < vlen && v[at] != '@') at++;
    if (at == vlen || at + 1 >= vlen) return 0;
    if (at > max || vlen - at - 1 >= sizeof domain) return 0;

    memcpy(domain, v + at + 1, vlen - at - 1);
    domain[vlen - at - 1] = '\0';
    id = compact_domain_id(domain);

    if (at + 1u > max) return 0;
    memcpy(out, v, at);
    o = at;

    if (id >= 0) {
        if (o + 1u > max) return 0;
        out[o++] = (uint8_t)id;
    } else {
        const size_t dl = vlen - at - 1;
        if (o + 1u + dl > max) return 0;
        out[o++] = COMPACT_DOMAIN_LITERAL;
        memcpy(out + o, domain, dl);
        o += dl;
    }
    return o;
}

static size_t unpack_email(const uint8_t *in, size_t len, char *out, size_t max)
{
    size_t at = 0, o;

    /*
     * The domain byte is the first byte below 0x20 or equal to 0xFF. Local
     * parts are printable ASCII per RFC 5322 in any address a phone will
     * produce, so this is unambiguous without a second length field.
     */
    while (at < len && in[at] >= 0x20u && in[at] != COMPACT_DOMAIN_LITERAL) at++;
    if (at >= len || at + 1u > max) return 0;

    memcpy(out, in, at);
    o = at;
    if (o + 1u >= max) return 0;
    out[o++] = '@';

    if (in[at] == COMPACT_DOMAIN_LITERAL) {
        const size_t dl = len - at - 1u;
        if (o + dl >= max) return 0;
        memcpy(out + o, in + at + 1, dl);
        o += dl;
    } else {
        const char *d = compact_domain_name(in[at]);
        size_t dl;
        if (!d) return 0;
        dl = strlen(d);
        if (o + dl >= max) return 0;
        memcpy(out + o, d, dl);
        o += dl;
    }
    out[o] = '\0';
    return o;
}

compact_err_t vcard_parse(const char *text, size_t len, compact_rec_t *out)
{
    size_t i = 0;
    char line[VC_MAX_LINE];

    compact_rec_init(out);

    while (i < len) {
        size_t ll = 0, name_len = 0, vlen;
        const char *value;
        uint8_t tag;
        uint8_t buf[COMPACT_MAX_VALUE];
        compact_err_t e;

        /* One logical line, unfolding continuations (RFC 2425 section 5.8.1). */
        for (;;) {
            while (i < len && text[i] != '\n' && text[i] != '\r') {
                if (ll + 1u < sizeof line) line[ll++] = text[i];
                i++;
            }
            if (i < len && text[i] == '\r') i++;
            if (i < len && text[i] == '\n') i++;
            if (i < len && (text[i] == ' ' || text[i] == '\t')) { i++; continue; }
            break;
        }
        line[ll] = '\0';
        if (ll == 0) continue;

        value = split_colon(line, ll, &name_len);
        if (!value) continue;
        vlen = ll - name_len - 1u;

        /* Boilerplate is implied by the tag set and never crosses the body. */
        if ((name_len == 5 && ieq(line, "BEGIN", 5)) ||
            (name_len == 3 && ieq(line, "END", 3)) ||
            (name_len == 7 && ieq(line, "VERSION", 7)))
            continue;

        /* architecture §8.2: rejected outright, never truncated. A photo is
         * nine seconds of airtime and the handshake lasts one. */
        if (name_len >= 5 && ieq(line, "PHOTO", 5))
            return COMPACT_ERR_PHOTO;

        tag = map_property(line, name_len);

        if (tag == TAG_TEL) {
            char tmp[64];
            char custom[COMPACT_TEL_LABEL_MAX + 1];
            uint8_t label;
            size_t pn;
            if (vlen >= sizeof tmp) return COMPACT_ERR_TOO_LONG;
            memcpy(tmp, value, vlen);
            tmp[vlen] = '\0';
            label = tel_label(line, name_len, custom, sizeof custom);
            pn = compact_tel_pack(label, custom, tmp, buf, sizeof buf);
            if (pn) {
                e = compact_add(out, tag, buf, pn);
                if (e != COMPACT_OK) return e;
                continue;
            }
            tag = TAG_RAW;   /* not a phone number we can pack; send it verbatim */
        } else if (tag == TAG_EMAIL) {
            const size_t en = pack_email(value, vlen, buf, sizeof buf);
            if (en) {
                e = compact_add(out, tag, buf, en);
                if (e != COMPACT_OK) return e;
                continue;
            }
            tag = TAG_RAW;
        }

        if (tag == TAG_RAW) {
            if (ll > COMPACT_MAX_VALUE) return COMPACT_ERR_TOO_LONG;
            e = compact_add(out, TAG_RAW, (const uint8_t *)line, ll);
        } else {
            if (vlen > COMPACT_MAX_VALUE) return COMPACT_ERR_TOO_LONG;
            e = compact_add(out, tag, (const uint8_t *)value, vlen);
        }
        if (e != COMPACT_OK) return e;
    }

    compact_sort_priority(out);
    return COMPACT_OK;
}

/* ---- render ----------------------------------------------------------- */

typedef struct { char *p; size_t max, o; bool overflow; } wr_t;

static void wr(wr_t *w, const char *s, size_t n)
{
    if (w->o + n >= w->max) { w->overflow = true; return; }
    memcpy(w->p + w->o, s, n);
    w->o += n;
}

static void wrs(wr_t *w, const char *s) { wr(w, s, strlen(s)); }

compact_err_t vcard_render(const compact_rec_t *r, char *out, size_t max, size_t *out_len)
{
    wr_t w;
    uint8_t i;
    const compact_field_t *fn = compact_find(r, TAG_FN);
    const compact_field_t *nn = compact_find(r, TAG_N);

    w.p = out; w.max = max; w.o = 0; w.overflow = false;

    wrs(&w, "BEGIN:VCARD\r\nVERSION:3.0\r\n");

    /*
     * N is mandatory in vCard 3.0 and it is also the field most likely to be
     * in a fragment that never arrived. Derive it from FN when it is missing:
     * "Ada Lovelace" becomes "Lovelace;Ada;;;". Wrong for names that do not
     * split on the last space, and still far better than an invalid card.
     */
    if (!nn && fn && fn->len) {
        size_t sp = fn->len, k;
        for (k = fn->len; k > 0; k--)
            if (fn->val[k - 1] == ' ') { sp = k - 1; break; }

        wrs(&w, "N:");
        if (sp < fn->len) {
            wr(&w, (const char *)fn->val + sp + 1, fn->len - sp - 1);
            wrs(&w, ";");
            wr(&w, (const char *)fn->val, sp);
        } else {
            wr(&w, (const char *)fn->val, fn->len);
            wrs(&w, ";");
        }
        wrs(&w, ";;;\r\n");
    }

    for (i = 0; i < r->n; i++) {
        const compact_field_t *f = &r->f[i];

        if (f->tag == TAG_RAW) {
            wr(&w, (const char *)f->val, f->len);
            wrs(&w, "\r\n");
        } else if (f->tag == TAG_TEL || f->tag == TAG_TEL_CELL || f->tag == TAG_TEL_WORK) {
            char tmp[64];
            char custom[COMPACT_TEL_LABEL_MAX + 1];
            uint8_t label = TEL_LABEL_NONE;
            const size_t tn = compact_tel_unpack(f->tag, f->val, f->len, &label,
                                                 custom, sizeof custom, tmp, sizeof tmp);
            if (!tn) continue;
            wrs(&w, "TEL");
            /* A custom label goes out the way Android writes one, as an X-
             * type, which is also how tel_label reads it back. */
            if (label == TEL_LABEL_CUSTOM) { wrs(&w, ";TYPE=X-"); wrs(&w, custom); }
            else if (label != TEL_LABEL_NONE) { wrs(&w, ";TYPE="); wrs(&w, k_tel_types[label]); }
            wrs(&w, ":");
            wr(&w, tmp, tn);
            wrs(&w, "\r\n");
        } else if (f->tag == TAG_EMAIL) {
            char tmp[160];
            const size_t en = unpack_email(f->val, f->len, tmp, sizeof tmp);
            if (!en) continue;
            wrs(&w, "EMAIL;TYPE=INTERNET:");
            wr(&w, tmp, en);
            wrs(&w, "\r\n");
        } else if (f->tag < sizeof k_tag_names / sizeof k_tag_names[0] && k_tag_names[f->tag]) {
            wrs(&w, k_tag_names[f->tag]);
            wrs(&w, ":");
            wr(&w, (const char *)f->val, f->len);
            wrs(&w, "\r\n");
        }
    }

    wrs(&w, "END:VCARD\r\n");

    if (w.overflow) return COMPACT_ERR_TOO_LONG;
    out[w.o] = '\0';
    if (out_len) *out_len = w.o;
    return COMPACT_OK;
}
