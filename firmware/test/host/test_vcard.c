#include <stdio.h>
#include <string.h>

#include "hf_test.h"
#include "tests.h"
#include "vcard.h"

static const char k_card[] =
    "BEGIN:VCARD\r\n"
    "VERSION:3.0\r\n"
    "N:Lovelace;Ada;;;\r\n"
    "FN:Ada Lovelace\r\n"
    "ORG:Analytical Engines Ltd\r\n"
    "TITLE:Programmer\r\n"
    "TEL;TYPE=CELL:+44 7700 900123\r\n"
    "EMAIL;TYPE=INTERNET:ada@gmail.com\r\n"
    "END:VCARD\r\n";

static const compact_field_t *need(const compact_rec_t *r, uint8_t tag)
{
    const compact_field_t *f = compact_find(r, tag);
    HF_CHECK_MSG(f != NULL, "tag 0x%02X missing", tag);
    return f;
}

void test_vcard(void)
{
    hf_begin("vcard: parses a realistic card");
    {
        compact_rec_t r;
        const compact_field_t *f;

        HF_EQ_INT(vcard_parse(k_card, strlen(k_card), &r), COMPACT_OK);

        f = need(&r, TAG_FN);
        if (f) { HF_EQ_INT(f->len, 12); HF_EQ_INT(memcmp(f->val, "Ada Lovelace", 12), 0); }
        HF_CHECK(need(&r, TAG_N) != NULL);
        HF_CHECK(need(&r, TAG_ORG) != NULL);
        HF_CHECK(need(&r, TAG_TITLE) != NULL);
        HF_CHECK(need(&r, TAG_TEL_CELL) != NULL);

        /* gmail.com collapses to one byte, so "ada@gmail.com" costs 4. */
        f = need(&r, TAG_EMAIL);
        if (f) { HF_EQ_INT(f->len, 4); HF_EQ_INT(f->val[3], 0); }

        /* Boilerplate never reaches the wire. */
        HF_CHECK(compact_find(&r, TAG_RAW) == NULL);
    }

    /*
     * The claim architecture §8.1 rests on: a realistic card is ~169 bytes of
     * text and must come down to something a one-second handshake can carry.
     */
    hf_begin("vcard: a realistic card compacts to well under half its text size");
    {
        compact_rec_t r;
        uint8_t blob[COMPACT_MAX_BLOB];
        size_t len = 0;

        vcard_parse(k_card, strlen(k_card), &r);
        HF_EQ_INT(compact_encode(&r, blob, sizeof blob, &len), COMPACT_OK);
        HF_CHECK_MSG(len < strlen(k_card) / 2,
                     "compact form is %u bytes against %u of text",
                     (unsigned)len, (unsigned)strlen(k_card));
        /* architecture §8.1's table predicts 79 bytes for a card of this
         * shape. Hold the codec to that neighbourhood. */
        HF_CHECK_MSG(len <= 96, "compact form grew to %u bytes", (unsigned)len);
    }

    hf_begin("vcard: renders a valid card back");
    {
        compact_rec_t r;
        char out[1024];
        size_t n = 0;

        vcard_parse(k_card, strlen(k_card), &r);
        HF_EQ_INT(vcard_render(&r, out, sizeof out, &n), COMPACT_OK);

        HF_CHECK(strstr(out, "BEGIN:VCARD\r\n") == out);
        HF_CHECK(strstr(out, "VERSION:3.0\r\n") != NULL);
        HF_CHECK(strstr(out, "FN:Ada Lovelace\r\n") != NULL);
        HF_CHECK(strstr(out, "N:Lovelace;Ada;;;\r\n") != NULL);
        HF_CHECK(strstr(out, "TEL;TYPE=CELL:+447700900123\r\n") != NULL);
        HF_CHECK(strstr(out, "EMAIL;TYPE=INTERNET:ada@gmail.com\r\n") != NULL);
        HF_CHECK(strstr(out, "ORG:Analytical Engines Ltd\r\n") != NULL);
        HF_CHECK(strstr(out, "END:VCARD\r\n") != NULL);
    }

    hf_begin("vcard: text -> compact -> text -> compact is a fixed point");
    {
        compact_rec_t a, b;
        char once[1024];
        uint8_t ba[COMPACT_MAX_BLOB], bb[COMPACT_MAX_BLOB];
        size_t la = 0, lb = 0, n = 0;

        vcard_parse(k_card, strlen(k_card), &a);
        vcard_render(&a, once, sizeof once, &n);
        vcard_parse(once, n, &b);

        compact_encode(&a, ba, sizeof ba, &la);
        compact_encode(&b, bb, sizeof bb, &lb);
        HF_EQ_INT(lb, la);
        HF_EQ_MEM(bb, ba, la);
    }

    /*
     * architecture §8.5: N is derived from FN when the fragment carrying N did
     * not arrive. An invalid card is worse than an imperfectly split name.
     */
    hf_begin("vcard: N is derived from FN when it is missing");
    {
        compact_rec_t r;
        char out[512];
        size_t n = 0;

        compact_rec_init(&r);
        compact_add(&r, TAG_FN, (const uint8_t *)"Ada Lovelace", 12);
        HF_EQ_INT(vcard_render(&r, out, sizeof out, &n), COMPACT_OK);
        HF_CHECK(strstr(out, "N:Lovelace;Ada;;;\r\n") != NULL);
    }

    hf_begin("vcard: a one-word name still yields a valid N");
    {
        compact_rec_t r;
        char out[512];
        size_t n = 0;

        compact_rec_init(&r);
        compact_add(&r, TAG_FN, (const uint8_t *)"Prince", 6);
        vcard_render(&r, out, sizeof out, &n);
        HF_CHECK(strstr(out, "N:Prince;;;;\r\n") != NULL);
    }

    /*
     * The escape hatch. An unknown property crosses verbatim at full cost;
     * nothing is silently dropped (architecture §8.2).
     */
    hf_begin("vcard: an unknown property survives as a raw line");
    {
        static const char card[] =
            "BEGIN:VCARD\r\nVERSION:3.0\r\n"
            "FN:Bo\r\n"
            "X-SKYPE:bo.tester\r\n"
            "END:VCARD\r\n";
        compact_rec_t r;
        char out[512];
        size_t n = 0;

        HF_EQ_INT(vcard_parse(card, strlen(card), &r), COMPACT_OK);
        HF_CHECK(compact_find(&r, TAG_RAW) != NULL);
        vcard_render(&r, out, sizeof out, &n);
        HF_CHECK(strstr(out, "X-SKYPE:bo.tester\r\n") != NULL);
    }

    hf_begin("vcard: an unknown email domain crosses literally");
    {
        static const char card[] =
            "BEGIN:VCARD\r\nVERSION:3.0\r\n"
            "FN:Bo\r\nEMAIL:bo@example.org\r\n"
            "END:VCARD\r\n";
        compact_rec_t r;
        char out[512];
        size_t n = 0;

        vcard_parse(card, strlen(card), &r);
        vcard_render(&r, out, sizeof out, &n);
        HF_CHECK(strstr(out, "bo@example.org") != NULL);
    }

    hf_begin("vcard: a PHOTO is rejected rather than truncated");
    {
        static const char card[] =
            "BEGIN:VCARD\r\nVERSION:3.0\r\n"
            "FN:Bo\r\nPHOTO;ENCODING=b;TYPE=JPEG:/9j/4AAQSkZJRg==\r\n"
            "END:VCARD\r\n";
        compact_rec_t r;
        HF_EQ_INT(vcard_parse(card, strlen(card), &r), COMPACT_ERR_PHOTO);
    }

    hf_begin("vcard: LF-only line endings parse");
    {
        static const char card[] =
            "BEGIN:VCARD\nVERSION:3.0\nFN:Bo\nTEL;TYPE=CELL:+15550100\nEND:VCARD\n";
        compact_rec_t r;
        HF_EQ_INT(vcard_parse(card, strlen(card), &r), COMPACT_OK);
        HF_CHECK(compact_find(&r, TAG_FN) != NULL);
        HF_CHECK(compact_find(&r, TAG_TEL_CELL) != NULL);
    }

    hf_begin("vcard: folded lines are unfolded");
    {
        static const char card[] =
            "BEGIN:VCARD\r\nVERSION:3.0\r\n"
            "ORG:Analytical Engi\r\n nes Ltd\r\n"
            "FN:Ada\r\nEND:VCARD\r\n";
        compact_rec_t r;
        const compact_field_t *f;

        HF_EQ_INT(vcard_parse(card, strlen(card), &r), COMPACT_OK);
        f = compact_find(&r, TAG_ORG);
        HF_CHECK(f != NULL);
        if (f) HF_EQ_INT(memcmp(f->val, "Analytical Engines Ltd", f->len), 0);
    }

    /* UTF-8: lengths are bytes and must never split a sequence. */
    hf_begin("vcard: a non-ASCII name round trips byte for byte");
    {
        static const char card[] =
            "BEGIN:VCARD\r\nVERSION:3.0\r\n"
            "FN:Bj\xc3\xb6rn Sm\xc3\xa1ri\r\nEND:VCARD\r\n";
        compact_rec_t r;
        char out[512];
        size_t n = 0;

        vcard_parse(card, strlen(card), &r);
        HF_EQ_INT(compact_find(&r, TAG_FN)->len, 13);   /* bytes, not characters */
        vcard_render(&r, out, sizeof out, &n);
        HF_CHECK(strstr(out, "FN:Bj\xc3\xb6rn Sm\xc3\xa1ri\r\n") != NULL);
    }

    hf_begin("vcard: an empty card renders a valid skeleton");
    {
        compact_rec_t r;
        char out[256];
        size_t n = 0;
        compact_rec_init(&r);
        HF_EQ_INT(vcard_render(&r, out, sizeof out, &n), COMPACT_OK);
        HF_EQ_STR(out, "BEGIN:VCARD\r\nVERSION:3.0\r\nEND:VCARD\r\n");
    }

    hf_begin("vcard: render refuses to overrun a short buffer");
    {
        compact_rec_t r;
        char out[16];
        size_t n = 0;
        vcard_parse(k_card, strlen(k_card), &r);
        HF_EQ_INT(vcard_render(&r, out, sizeof out, &n), COMPACT_ERR_TOO_LONG);
    }
}
