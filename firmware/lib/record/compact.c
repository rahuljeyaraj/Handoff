#include "compact.h"

#include <string.h>

/*
 * Domain dictionary. Ordering is fixed forever once shipped: the id is on the
 * wire, so inserting into the middle would silently rewrite everyone's email
 * address. Append only.
 */
static const char *const k_domains[] = {
    "gmail.com",
    "outlook.com",
    "hotmail.com",
    "icloud.com",
    "yahoo.com",
    "protonmail.com",
    "live.com",
    "me.com",
    "aol.com",
    "gmx.com",
    "yandex.com",
    "qq.com",
    "163.com",
    "web.de",
    "mail.com",
    "zoho.com",
};

size_t compact_domain_count(void) { return sizeof k_domains / sizeof k_domains[0]; }

int compact_domain_id(const char *domain)
{
    size_t i;
    if (!domain) return -1;
    for (i = 0; i < compact_domain_count(); i++)
        if (strcmp(k_domains[i], domain) == 0) return (int)i;
    return -1;
}

const char *compact_domain_name(uint8_t id)
{
    return (id < compact_domain_count()) ? k_domains[id] : NULL;
}

/* ---- field list ------------------------------------------------------- */

void compact_rec_init(compact_rec_t *r)
{
    r->n = 0;
}

compact_err_t compact_add(compact_rec_t *r, uint8_t tag, const uint8_t *val, size_t len)
{
    if (r->n >= COMPACT_MAX_FIELDS) return COMPACT_ERR_FULL;
    if (len > COMPACT_MAX_VALUE)    return COMPACT_ERR_TOO_LONG;
    if (tag == TAG_NOP || tag == TAG_CONT) return COMPACT_ERR_MALFORMED;

    r->f[r->n].tag = tag;
    r->f[r->n].len = (uint8_t)len;
    if (len && val) memcpy(r->f[r->n].val, val, len);
    r->n++;
    return COMPACT_OK;
}

const compact_field_t *compact_find(const compact_rec_t *r, uint8_t tag)
{
    uint8_t i;
    for (i = 0; i < r->n; i++)
        if (r->f[i].tag == tag) return &r->f[i];
    return NULL;
}

/*
 * Lower number goes on the wire first. This is what makes architecture §8.4's
 * "fragment 0 is a usable contact" true rather than aspirational.
 */
uint8_t compact_tag_priority(uint8_t tag)
{
    switch (tag) {
    case TAG_FN:       return 0;
    case TAG_TEL_CELL: return 1;
    case TAG_EMAIL:    return 2;
    case TAG_N:        return 3;
    case TAG_ORG:      return 4;
    case TAG_TITLE:    return 5;
    case TAG_TEL_WORK: return 6;
    case TAG_URL:      return 7;
    case TAG_ADR:      return 8;
    case TAG_NOTE:     return 9;
    default:           return 10;   /* TAG_RAW and anything unknown */
    }
}

void compact_sort_priority(compact_rec_t *r)
{
    /* Insertion sort: n <= 16, and it is stable, so two RAW lines keep the
     * order they appeared in the source card. */
    uint8_t i, j;
    for (i = 1; i < r->n; i++) {
        compact_field_t key = r->f[i];
        const uint8_t kp = compact_tag_priority(key.tag);
        j = i;
        while (j > 0 && compact_tag_priority(r->f[j - 1].tag) > kp) {
            r->f[j] = r->f[j - 1];
            j--;
        }
        r->f[j] = key;
    }
}

compact_err_t compact_encode(const compact_rec_t *r, uint8_t *out, size_t max, size_t *out_len)
{
    return compact_encode_chunked(r, 255, out, max, out_len);
}

compact_err_t compact_encode_chunked(const compact_rec_t *r, size_t max_value,
                                     uint8_t *out, size_t max, size_t *out_len)
{
    size_t o = 0;
    uint8_t i;

    if (max_value == 0) return COMPACT_ERR_TOO_LONG;
    if (max_value > 255) max_value = 255;

    for (i = 0; i < r->n; i++) {
        const compact_field_t *f = &r->f[i];
        size_t done = 0;

        do {
            const size_t chunk = (f->len - done > max_value) ? max_value : (size_t)(f->len - done);
            const uint8_t tag = done ? TAG_CONT : f->tag;

            if (o + 2u + chunk > max) return COMPACT_ERR_TOO_LONG;
            out[o++] = tag;
            out[o++] = (uint8_t)chunk;
            if (chunk) memcpy(out + o, f->val + done, chunk);
            o += chunk;
            done += chunk;
        } while (done < f->len);
    }

    if (out_len) *out_len = o;
    return COMPACT_OK;
}

compact_err_t compact_decode(const uint8_t *in, size_t len, compact_rec_t *out)
{
    return compact_decode_ex(in, len, out, NULL);
}

compact_err_t compact_decode_ex(const uint8_t *in, size_t len, compact_rec_t *out,
                                size_t *consumed)
{
    size_t i = 0;

    compact_rec_init(out);

    while (i < len) {
        const uint8_t tag = in[i];
        uint8_t l;

        /* NOP padding carries no length byte, so the zeros filling a fixed
         * length frame decode as nothing rather than as thirty empty fields. */
        if (tag == TAG_NOP) { i++; continue; }

        /* A truncated tail is what a partially received blob looks like.
         * Stop, keep what parsed, and let the caller see where we got to. */
        if (i + 1u >= len) break;
        l = in[i + 1];
        if (i + 2u + l > len) break;

        if (tag == TAG_CONT) {
            if (out->n == 0) { i += 2u + l; continue; }   /* head never arrived */
            {
                compact_field_t *prev = &out->f[out->n - 1];
                const size_t room = COMPACT_MAX_VALUE - prev->len;
                const size_t take = (l < room) ? l : room;
                if (take) memcpy(prev->val + prev->len, in + i + 2, take);
                prev->len = (uint8_t)(prev->len + take);
            }
            i += 2u + l;
            continue;
        }

        if (l > COMPACT_MAX_VALUE)        return COMPACT_ERR_TOO_LONG;
        if (out->n >= COMPACT_MAX_FIELDS) return COMPACT_ERR_FULL;

        out->f[out->n].tag = tag;
        out->f[out->n].len = l;
        if (l) memcpy(out->f[out->n].val, in + i + 2, l);
        out->n++;
        i += 2u + l;
    }

    if (consumed) *consumed = i;
    return COMPACT_OK;
}

/* ---- phone numbers ---------------------------------------------------- */

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/*
 * E.164 country codes are prefix-free: zones 1 and 7 are one digit, and every
 * other code is two or three. So the split between country code and subscriber
 * number is decidable from the digits alone, which matters because vCards in
 * the wild write "+447700900123" as often as "+44 7700 900123".
 */
static const uint8_t k_cc2[] = {
    20, 27, 30, 31, 32, 33, 34, 36, 39, 40, 41, 43, 44, 45, 46, 47, 48, 49,
    51, 52, 53, 54, 55, 56, 57, 58, 60, 61, 62, 63, 64, 65, 66,
    81, 82, 84, 86, 90, 91, 92, 93, 94, 95, 98
};

static size_t cc_digit_count(const char *d, size_t nd)
{
    size_t i;
    if (nd >= 1 && (d[0] == '1' || d[0] == '7')) return 1;
    if (nd >= 2) {
        const uint8_t two = (uint8_t)((d[0] - '0') * 10 + (d[1] - '0'));
        for (i = 0; i < sizeof k_cc2 / sizeof k_cc2[0]; i++)
            if (k_cc2[i] == two) return 2;
    }
    return nd >= 3 ? 3u : nd;
}

size_t compact_phone_pack(const char *text, uint8_t *out, size_t max)
{
    char digits[40];
    size_t nd = 0, i, o, ccn = 0;
    unsigned country = 0;
    const char *p = text;
    bool plus = false;

    if (!text || max < 3) return 0;

    if (*p == '+') { plus = true; p++; }

    for (; *p; p++)
        if (is_digit(*p) && nd < sizeof digits) digits[nd++] = *p;

    if (nd == 0) return 0;

    if (plus) {
        ccn = cc_digit_count(digits, nd);
        if (ccn >= nd) return 0;            /* a country code and nothing else */
        for (i = 0; i < ccn; i++) country = country * 10u + (unsigned)(digits[i] - '0');
    }

    if (2u + (nd - ccn + 1u) / 2u > max) return 0;

    out[0] = (uint8_t)(country >> 8);
    out[1] = (uint8_t)(country & 0xFFu);

    o = 2;
    for (i = ccn; i < nd; i += 2) {
        const uint8_t hi = (uint8_t)(digits[i] - '0');
        const uint8_t lo = (i + 1 < nd) ? (uint8_t)(digits[i + 1] - '0') : 0x0Fu;
        out[o++] = (uint8_t)((hi << 4) | lo);
    }
    return o;
}

size_t compact_phone_unpack(const uint8_t *in, size_t len, char *out, size_t max)
{
    unsigned country;
    size_t o = 0, i;

    if (len < 3 || max < 2) return 0;
    country = (unsigned)(((unsigned)in[0] << 8) | in[1]);

    if (country) {
        char tmp[8];
        int  t = 0;
        unsigned v = country;
        while (v && t < (int)sizeof tmp) { tmp[t++] = (char)('0' + (v % 10u)); v /= 10u; }
        if (o + 1u < max) out[o++] = '+';
        while (t-- > 0 && o + 1u < max) out[o++] = tmp[t];
    }

    for (i = 2; i < len; i++) {
        const uint8_t hi = (uint8_t)(in[i] >> 4);
        const uint8_t lo = (uint8_t)(in[i] & 0x0Fu);
        if (hi <= 9 && o + 1u < max) out[o++] = (char)('0' + hi);
        if (lo <= 9 && o + 1u < max) out[o++] = (char)('0' + lo);
    }

    out[o] = '\0';
    return o;
}
