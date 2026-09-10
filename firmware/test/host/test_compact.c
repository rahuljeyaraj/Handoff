#include <string.h>

#include "compact.h"
#include "config.h"
#include "hf_test.h"
#include "tests.h"

static void add_str(compact_rec_t *r, uint8_t tag, const char *s)
{
    HF_EQ_INT(compact_add(r, tag, (const uint8_t *)s, strlen(s)), COMPACT_OK);
}

void test_compact(void)
{
    hf_begin("compact: TLV round trip");
    {
        compact_rec_t in, out;
        uint8_t blob[COMPACT_MAX_BLOB];
        size_t len = 0;

        compact_rec_init(&in);
        add_str(&in, TAG_FN, "Ada Lovelace");
        add_str(&in, TAG_ORG, "Analytical Engines Ltd");
        add_str(&in, TAG_TITLE, "Programmer");

        HF_EQ_INT(compact_encode(&in, blob, sizeof blob, &len), COMPACT_OK);
        HF_EQ_INT(len, 2 + 12 + 2 + 22 + 2 + 10);
        HF_EQ_INT(compact_decode(blob, len, &out), COMPACT_OK);
        HF_EQ_INT(out.n, in.n);
        HF_EQ_INT(memcmp(&out.f[0], &in.f[0], 2 + in.f[0].len), 0);
    }

    hf_begin("compact: NOP padding decodes to nothing");
    {
        compact_rec_t out;
        uint8_t blob[HANDOFF_FRAG_PAYLOAD];
        size_t len = 0;
        compact_rec_t in;

        compact_rec_init(&in);
        add_str(&in, TAG_FN, "Bo");
        memset(blob, TAG_NOP, sizeof blob);
        compact_encode(&in, blob, sizeof blob, &len);
        memset(blob + len, TAG_NOP, sizeof blob - len);

        HF_EQ_INT(compact_decode(blob, sizeof blob, &out), COMPACT_OK);
        HF_EQ_INT(out.n, 1);
        HF_EQ_INT(out.f[0].len, 2);
    }

    hf_begin("compact: NOP is refused as a user tag");
    {
        compact_rec_t r;
        compact_rec_init(&r);
        HF_EQ_INT(compact_add(&r, TAG_NOP, (const uint8_t *)"x", 1), COMPACT_ERR_MALFORMED);
        HF_EQ_INT(compact_add(&r, TAG_CONT, (const uint8_t *)"x", 1), COMPACT_ERR_MALFORMED);
    }

    /*
     * The point of the priority order: after sorting, the first TLVs out are
     * the ones that make a contact usable on their own, so fragment 0 alone is
     * a name and a mobile number (architecture §8.4).
     */
    hf_begin("compact: priority sort puts FN and the mobile first");
    {
        compact_rec_t r;
        uint8_t phone[8];
        size_t pn;

        compact_rec_init(&r);
        add_str(&r, TAG_NOTE, "met at the conference");
        add_str(&r, TAG_ORG, "Acme");
        pn = compact_phone_pack("+44 7700 900123", phone, sizeof phone);
        HF_CHECK(pn > 0);
        HF_EQ_INT(compact_add(&r, TAG_TEL_CELL, phone, pn), COMPACT_OK);
        add_str(&r, TAG_FN, "Ada Lovelace");

        compact_sort_priority(&r);
        HF_EQ_INT(r.f[0].tag, TAG_FN);
        HF_EQ_INT(r.f[1].tag, TAG_TEL_CELL);
        HF_EQ_INT(r.f[2].tag, TAG_ORG);
        HF_EQ_INT(r.f[3].tag, TAG_NOTE);
    }

    hf_begin("compact: the sort is stable across equal priorities");
    {
        compact_rec_t r;
        compact_rec_init(&r);
        add_str(&r, TAG_RAW, "X-FIRST:1");
        add_str(&r, TAG_RAW, "X-SECOND:2");
        add_str(&r, TAG_FN, "Zed");
        compact_sort_priority(&r);
        HF_EQ_INT(r.f[0].tag, TAG_FN);
        HF_EQ_INT(memcmp(r.f[1].val, "X-FIRST:1", 9), 0);
        HF_EQ_INT(memcmp(r.f[2].val, "X-SECOND:2", 10), 0);
    }

    /*
     * Chunking is what lets a long value cross a link whose fragments hold
     * only 30 bytes of TLV, without frag.c ever having to cut a TLV in half.
     */
    hf_begin("compact: an over-long value is chunked and rejoined");
    {
        compact_rec_t in, out;
        uint8_t blob[COMPACT_MAX_BLOB];
        char note[100];
        size_t len = 0, i;

        for (i = 0; i < sizeof note - 1; i++) note[i] = (char)('a' + (i % 26));
        note[sizeof note - 1] = '\0';

        compact_rec_init(&in);
        add_str(&in, TAG_NOTE, note);

        HF_EQ_INT(compact_encode_chunked(&in, 30, blob, sizeof blob, &len), COMPACT_OK);
        HF_EQ_INT(blob[0], TAG_NOTE);
        HF_EQ_INT(blob[1], 30);
        HF_EQ_INT(blob[32], TAG_CONT);

        HF_EQ_INT(compact_decode(blob, len, &out), COMPACT_OK);
        HF_EQ_INT(out.n, 1);
        HF_EQ_INT(out.f[0].tag, TAG_NOTE);
        HF_EQ_INT(out.f[0].len, sizeof note - 1);
        HF_EQ_INT(memcmp(out.f[0].val, note, sizeof note - 1), 0);
    }

    hf_begin("compact: a continuation whose head never arrived is dropped");
    {
        compact_rec_t out;
        const uint8_t blob[] = { TAG_CONT, 3, 'x', 'y', 'z', TAG_FN, 2, 'B', 'o' };
        HF_EQ_INT(compact_decode(blob, sizeof blob, &out), COMPACT_OK);
        HF_EQ_INT(out.n, 1);
        HF_EQ_INT(out.f[0].tag, TAG_FN);
    }

    /*
     * A truncated tail is the normal shape of a partially received blob, not
     * an error. Decoding must keep everything before it — that is the whole
     * mechanism behind architecture §8.4's graceful degradation.
     */
    hf_begin("compact: a truncated tail keeps everything before it");
    {
        compact_rec_t in, out;
        uint8_t blob[COMPACT_MAX_BLOB];
        size_t len = 0, consumed = 0;

        compact_rec_init(&in);
        add_str(&in, TAG_FN, "Ada Lovelace");
        add_str(&in, TAG_ORG, "Analytical Engines Ltd");
        compact_encode(&in, blob, sizeof blob, &len);

        HF_EQ_INT(compact_decode_ex(blob, len - 10, &out, &consumed), COMPACT_OK);
        HF_EQ_INT(out.n, 1);
        HF_EQ_INT(out.f[0].tag, TAG_FN);
        HF_EQ_INT(consumed, 14);
    }

    hf_begin("compact: encode refuses to overrun the buffer");
    {
        compact_rec_t r;
        uint8_t small[8];
        size_t len = 0;
        compact_rec_init(&r);
        add_str(&r, TAG_FN, "a rather long name indeed");
        HF_EQ_INT(compact_encode(&r, small, sizeof small, &len), COMPACT_ERR_TOO_LONG);
    }

    /* ---- phone numbers ---- */

    hf_begin("compact: phone numbers round trip");
    {
        static const char *const cases[] = {
            "+44 7700 900123",
            "+447700900123",
            "+1 555 0100",
            "+15550100",
            "+91 98765 43210",
            "+7 495 1234567",
            "+353 86 1234567",
        };
        static const char *const want[] = {
            "+447700900123", "+447700900123", "+15550100", "+15550100",
            "+919876543210", "+74951234567", "+353861234567",
        };
        size_t i;

        for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            uint8_t packed[16];
            char text[64];
            const size_t n = compact_phone_pack(cases[i], packed, sizeof packed);
            HF_CHECK_MSG(n > 0, "failed to pack %s", cases[i]);
            compact_phone_unpack(packed, n, text, sizeof text);
            HF_EQ_STR(text, want[i]);
        }
    }

    hf_begin("compact: a mobile number costs 7 bytes, not 15");
    {
        uint8_t packed[16];
        /* 2 bytes of country code plus 5 BCD bytes for 10 subscriber digits,
         * against 15 bytes of text. That is architecture §8.2's saving. */
        HF_EQ_INT(compact_phone_pack("+44 7700 900123", packed, sizeof packed), 7);
    }

    hf_begin("compact: an odd digit count pads with 0xF");
    {
        uint8_t packed[16];
        char text[32];
        const size_t n = compact_phone_pack("+44 7700 90012", packed, sizeof packed);
        HF_EQ_INT(packed[n - 1] & 0x0Fu, 0x0Fu);
        compact_phone_unpack(packed, n, text, sizeof text);
        HF_EQ_STR(text, "+44770090012");
    }

    hf_begin("compact: a local number keeps no country code");
    {
        uint8_t packed[16];
        char text[32];
        const size_t n = compact_phone_pack("020 7946 0958", packed, sizeof packed);
        HF_CHECK(n > 0);
        compact_phone_unpack(packed, n, text, sizeof text);
        HF_EQ_STR(text, "02079460958");
    }

    /* ---- domain dictionary ---- */

    hf_begin("compact: the domain dictionary is a byte per common domain");
    {
        size_t i;
        HF_CHECK(compact_domain_count() >= 8);
        HF_EQ_INT(compact_domain_id("gmail.com"), 0);
        HF_EQ_INT(compact_domain_id("not-a-real-domain.example"), -1);
        for (i = 0; i < compact_domain_count(); i++) {
            const char *d = compact_domain_name((uint8_t)i);
            HF_CHECK(d != NULL);
            HF_EQ_INT(compact_domain_id(d), (int)i);
        }
        /* Ids below 0x20 keep the email unpacker's scan unambiguous. */
        HF_CHECK_MSG(compact_domain_count() < 0x20u,
                     "the dictionary has outgrown the email encoding");
    }
}
