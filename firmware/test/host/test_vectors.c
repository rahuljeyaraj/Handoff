/*
 * Handoff — replay the independently generated vectors.
 *
 * tools/gen_vectors.py writes these from the design documents without ever
 * consulting the C. Decoding them here is what catches a misreading of the
 * spec that the C encoder and C decoder share — the one class of bug that a
 * loopback test passes and the bench then fails on.
 *
 * Missing vectors are a failure, not a skip. The build regenerates them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chan.h"
#include "frame.h"
#include "hf_test.h"
#include "tests.h"
#include "v1_replay.h"

#define MAX_CHIPS   1024
#define MAX_SAMPLES CHAN_MAX_SAMPLES(MAX_CHIPS)

static int16_t g_samples[MAX_SAMPLES];
static uint8_t g_chips[MAX_CHIPS];

static FILE *open_vector(const char *name, const char *ext, const char *mode)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s%s", HANDOFF_VECTOR_DIR, name, ext);
    return fopen(path, mode);
}

static size_t load_chips(const char *name)
{
    FILE *f = open_vector(name, ".chips", "r");
    size_t n = 0;
    int v;

    if (!f) return 0;
    while (n < MAX_CHIPS && fscanf(f, "%d", &v) == 1) g_chips[n++] = (uint8_t)(v ? 1 : 0);
    fclose(f);
    return n;
}

static size_t load_samples(const char *name)
{
    FILE *f = open_vector(name, ".s16", "rb");
    size_t n;

    if (!f) return 0;
    n = fread(g_samples, sizeof g_samples[0], MAX_SAMPLES, f);
    fclose(f);
    return n;
}

static size_t load_capture(const char *file)
{
    char path[512];
    FILE *f;
    size_t n;

    snprintf(path, sizeof path, "%s/%s", HANDOFF_CAPTURE_DIR, file);
    f = fopen(path, "rb");
    if (!f) return 0;
    n = fread(g_samples, sizeof g_samples[0], MAX_SAMPLES, f);
    fclose(f);
    return n;
}

/* Push chips through the framer at ideal tone differences: tone B is a
 * positive d, tone A a negative one, and both are clean. */
static frame_rx_result_t decode_chips(frame_rx_t *r, const uint8_t *chips, size_t n)
{
    frame_rx_result_t last = FRAME_RX_NONE;
    size_t i;
    for (i = 0; i < n; i++) {
        const frame_rx_result_t res = frame_rx_push(r, chips[i] ? 1000 : -1000);
        if (res != FRAME_RX_NONE) last = res;
    }
    return last;
}

/* Full path: samples through both tone bins, sync, then the framer. */
static frame_rx_result_t decode_samples(frame_rx_t *r, const int16_t *s, size_t n)
{
    demod_t d;
    frame_rx_result_t last = FRAME_RX_NONE;
    frame_chip_t chip;
    size_t i;

    demod_init(&d);
    for (i = 0; i < n; i++) {
        frame_rx_result_t res;

        if (!demod_push(&d, s[i], &chip)) continue;
        res = frame_rx_push(r, chip);
        if (res != FRAME_RX_NONE) last = res;
    }
    return last;
}

/*
 * The v1 captures. The slicer they need lives in v1_replay.c, and its header
 * says why it is there rather than in lib/link.
 */
static frame_rx_result_t decode_v1_samples(frame_rx_t *r, const int16_t *s, size_t n)
{
    v1_replay_t v;
    frame_rx_result_t last = FRAME_RX_NONE;
    frame_chip_t chip;
    size_t i;

    v1_replay_init(&v, HANDOFF_GZ_BIN);
    for (i = 0; i < n; i++) {
        frame_rx_result_t res;
        if (!v1_replay_push(&v, s[i], &chip)) continue;
        res = frame_rx_push(r, chip);
        if (res != FRAME_RX_NONE) last = res;
    }
    return last;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t unhex(const char *hex, uint8_t *out, size_t max)
{
    size_t n = 0;
    while (hex[0] && hex[1] && n < max) {
        const int hi = hexval(hex[0]), lo = hexval(hex[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (uint8_t)((hi << 4) | lo);
        hex += 2;
    }
    return n;
}

typedef struct {
    const char *name;
    const char *payload_hex;
    int         expect_sample_decode;   /* impaired vectors may legitimately fail */
    uint8_t     frag_index, frag_count, record_id, flags;
} vec_t;

/*
 * Mirrors tools/gen_vectors.py's vector list. Kept by hand rather than parsed
 * from manifest.json, because a JSON parser in the test would be one more
 * thing that could agree with the generator for the wrong reason.
 */
static const vec_t k_vectors[] = {
    { "clean_short",         "deadbeef",                                         1, 0, 1, 0,  0 },
    { "clean_full",          NULL,                                               1, 0, 1, 0,  0 },
    { "frag_3_of_5",         "deadbeef",                                         1, 3, 5, 42, 1 },
    { "noisy_10db",          NULL,                                               1, 0, 1, 0,  0 },
    { "ramp_20db",           NULL,                                               1, 0, 1, 0,  0 },
    { "offset_200ppm",       NULL,                                               1, 0, 1, 0,  0 },
    { "offset_minus_200ppm", NULL,                                               1, 0, 1, 0,  0 },
    { "weak_signal",         NULL,                                               1, 0, 1, 0,  0 },
};

/* The 32-byte payload gen_vectors.py builds as (i * 7 + 3) & 0xFF. */
static void expected_full(uint8_t *out)
{
    size_t i;
    for (i = 0; i < HANDOFF_FRAG_PAYLOAD; i++) out[i] = (uint8_t)((i * 7u + 3u) & 0xFFu);
}

void test_vectors(void)
{
    size_t v;

    hf_begin("vectors: the generated set is present");
    {
        FILE *f = open_vector("manifest", ".json", "r");
        HF_CHECK_MSG(f != NULL,
                     "no vectors in %s — run tools/gen_vectors.py",
                     HANDOFF_VECTOR_DIR);
        if (!f) return;
        fclose(f);
    }

    for (v = 0; v < sizeof k_vectors / sizeof k_vectors[0]; v++) {
        const vec_t *vec = &k_vectors[v];
        uint8_t want[HANDOFF_FRAG_PAYLOAD];
        size_t nchips, nsamples;

        if (vec->payload_hex) {
            const size_t n = unhex(vec->payload_hex, want, sizeof want);
            memset(want + n, 0, sizeof want - n);
        } else {
            expected_full(want);
        }

        /*
         * Chip level first. A failure here means the framing, the marker or
         * the CRC disagree with the spec — independent of any DSP.
         */
        hf_begin(vec->name);
        nchips = load_chips(vec->name);
        HF_CHECK_MSG(nchips > 0, "%s.chips missing or empty", vec->name);

        if (nchips) {
            frame_rx_t r;
            frame_rx_result_t res;

            HF_CHECK_MSG(nchips == FRAME_TOTAL_CHIPS,
                         "%s has %u chips, this build expects %u",
                         vec->name, (unsigned)nchips, (unsigned)FRAME_TOTAL_CHIPS);

            frame_rx_init(&r);
            res = decode_chips(&r, g_chips, nchips);
            HF_CHECK_MSG(res == FRAME_RX_GOOD,
                         "%s did not decode from chips (result %d)", vec->name, (int)res);

            if (res == FRAME_RX_GOOD) {
                HF_EQ_INT(frame_rx_hdr(&r)->frag_index, vec->frag_index);
                HF_EQ_INT(frame_rx_hdr(&r)->frag_count, vec->frag_count);
                HF_EQ_INT(frame_rx_hdr(&r)->record_id, vec->record_id);
                HF_EQ_INT(frame_rx_hdr(&r)->flags, vec->flags);
                HF_EQ_MEM(frame_rx_payload(&r), want, sizeof want);
            }
        }

        /* Then the whole receive chain, from ADC samples. */
        nsamples = load_samples(vec->name);
        HF_CHECK_MSG(nsamples > 0, "%s.s16 missing or empty", vec->name);

        if (nsamples && vec->expect_sample_decode) {
            frame_rx_t r;
            frame_rx_result_t res;

            frame_rx_init(&r);
            res = decode_samples(&r, g_samples, nsamples);
            HF_CHECK_MSG(res == FRAME_RX_GOOD,
                         "%s did not decode from samples (result %d)",
                         vec->name, (int)res);
            if (res == FRAME_RX_GOOD)
                HF_EQ_MEM(frame_rx_payload(&r), want, sizeof want);
        }
    }

    /*
     * Development plan §1: every hardware failure becomes a host test. The
     * captures tools/replay.py adopts are listed in captures/index.json and
     * each one is decoded here, from raw ADC samples, on every run. The
     * index is our own JSON, so the scan below is all the parser it needs:
     * a "file" and, when the payload is loopback's pattern, a "seq".
     */
    hf_begin("captures: every adopted v1 hardware capture still frames");
    {
        char path[512];
        FILE *f;
        long len;
        char *json = NULL;
        const char *p;
        int replayed = 0;

        snprintf(path, sizeof path, "%s/index.json", HANDOFF_CAPTURE_DIR);
        f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            len = ftell(f);
            fseek(f, 0, SEEK_SET);
            json = (char *)calloc((size_t)len + 1u, 1u);
            if (json && fread(json, 1, (size_t)len, f) != (size_t)len) { free(json); json = NULL; }
            fclose(f);
        }

        for (p = json; p && (p = strstr(p, "\"file\"")) != NULL; ) {
            char file[128];
            const char *q, *e, *s;
            long seq = -1;
            size_t nsamples;
            frame_rx_t r;
            frame_rx_result_t res;

            q = strchr(p + 6, '"');
            e = q ? strchr(q + 1, '"') : NULL;
            if (!q || !e || (size_t)(e - q - 1) >= sizeof file) break;
            memcpy(file, q + 1, (size_t)(e - q - 1));
            file[e - q - 1] = 0;
            p = e + 1;

            /* The seq, if any, sits inside this entry: before the next '}'. */
            s = strstr(e, "\"seq\"");
            if (s && (strchr(e, '}') == NULL || s < strchr(e, '}')))
                seq = strtol(strchr(s, ':') + 1, NULL, 10);

            nsamples = load_capture(file);
            HF_CHECK_MSG(nsamples > 0, "capture %s missing or empty", file);
            if (!nsamples) continue;

            frame_rx_init(&r);
            res = decode_v1_samples(&r, g_samples, nsamples);
            HF_CHECK_MSG(res == FRAME_RX_GOOD,
                         "capture %s no longer decodes (result %d)", file, (int)res);
            replayed++;

            if (res == FRAME_RX_GOOD && seq >= 0) {
                uint8_t want[HANDOFF_FRAG_PAYLOAD];
                size_t i;
                for (i = 0; i < sizeof want; i++)
                    want[i] = (uint8_t)((i * 31u + (size_t)seq * 17u) & 0xFFu);
                HF_EQ_MEM(frame_rx_payload(&r), want, sizeof want);
            }
        }
        free(json);
        HF_CHECK_MSG(replayed > 0, "no captures replayed; index.json missing or empty");
    }

    /*
     * The generator and chan.c are separate implementations of the same
     * modulation. If they disagree by more than rounding, one of them has
     * misread the spec — which is exactly what this whole file exists to
     * detect.
     */
    hf_begin("vectors: the C modulator agrees with the Python one");
    {
        const size_t nchips = load_chips("clean_full");
        const size_t nsamples = load_samples("clean_full");

        if (nchips && nsamples) {
            static int16_t mine[MAX_SAMPLES];
            chan_cfg_t c;
            size_t n, i, differ = 0;
            long worst = 0;

            chan_default(&c);
            n = chan_render(&c, g_chips, nchips, mine, MAX_SAMPLES);
            HF_EQ_INT(n, nsamples);

            for (i = 0; i < n && i < nsamples; i++) {
                long d = (long)mine[i] - (long)g_samples[i];
                if (d < 0) d = -d;
                if (d > worst) worst = d;
                if (d > 2) differ++;
            }
            /* Two LSB covers rounding and the phase-integration difference. */
            HF_CHECK_MSG(differ == 0,
                         "%u of %u samples differ by more than 2 LSB (worst %ld)",
                         (unsigned)differ, (unsigned)n, worst);
        }
    }
}
