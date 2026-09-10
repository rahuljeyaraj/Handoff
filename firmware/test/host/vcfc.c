/*
 * Handoff — the C codec as a command-line tool, so it can be diffed against
 * tools/vcf.py. Development plan M1: "cross-check the C codec against
 * tools/vcf.py in both directions".
 *
 * Both directions matters. Checking only C-encode against Python-decode leaves
 * a symmetric misreading of architecture §8.2 invisible: both sides would agree
 * about a field that neither reads the way the document specifies. Running it
 * the other way round as well is what pins the wire format rather than the
 * agreement.
 *
 *   handoff_vcf encode <file.vcf>      compact TLV, as hex
 *   handoff_vcf decode <hex>           reconstructed vCard text
 *
 * scripts/test.py --check-codec drives both against vcf.py.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * vCard 3.0 requires CRLF line endings, and a Windows text-mode stdout would
 * turn every one of them into CR CR LF. That is not a test artefact — it would
 * corrupt any .vcf this tool is piped into — so stdout goes to binary mode.
 */
#ifdef _WIN32
#  include <fcntl.h>
#  include <io.h>
#  define BINARY_STDOUT() _setmode(_fileno(stdout), _O_BINARY)
#else
#  define BINARY_STDOUT() ((void)0)
#endif

#include "compact.h"
#include "vcard.h"

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int do_encode(const char *path, size_t max_value)
{
    static char text[8192];
    uint8_t blob[COMPACT_MAX_BLOB];
    compact_rec_t rec;
    size_t n, len = 0, i;
    FILE *f = fopen(path, "rb");

    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 2; }
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = '\0';

    switch (vcard_parse(text, n, &rec)) {
    case COMPACT_OK: break;
    case COMPACT_ERR_PHOTO:
        fprintf(stderr, "rejected: PHOTO\n");
        return 1;
    default:
        fprintf(stderr, "parse failed\n");
        return 1;
    }

    compact_sort_priority(&rec);
    if (compact_encode_chunked(&rec, max_value, blob, sizeof blob, &len) != COMPACT_OK) {
        fprintf(stderr, "encode failed\n");
        return 1;
    }

    for (i = 0; i < len; i++) printf("%02x", blob[i]);
    printf("\n");
    return 0;
}

static int do_decode(const char *hex)
{
    uint8_t blob[COMPACT_MAX_BLOB];
    compact_rec_t rec;
    char out[8192];
    size_t n = 0, len = 0;

    while (hex[0] && hex[1] && n < sizeof blob) {
        const int hi = hexval(hex[0]), lo = hexval(hex[1]);
        if (hi < 0 || lo < 0) break;
        blob[n++] = (uint8_t)((hi << 4) | lo);
        hex += 2;
    }

    if (compact_decode(blob, n, &rec) != COMPACT_OK) {
        fprintf(stderr, "decode failed\n");
        return 1;
    }
    if (vcard_render(&rec, out, sizeof out, &len) != COMPACT_OK) {
        fprintf(stderr, "render failed\n");
        return 1;
    }
    fwrite(out, 1, len, stdout);
    return 0;
}

int main(int argc, char **argv)
{
    size_t max_value = 255;
    int i;
    const char *action = NULL, *arg = NULL;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--max-value") == 0 && i + 1 < argc)
            max_value = (size_t)atoi(argv[++i]);
        else if (!action) action = argv[i];
        else if (!arg)    arg = argv[i];
    }

    if (!action || !arg) {
        fprintf(stderr, "usage: %s [--max-value N] encode <file.vcf>\n"
                        "       %s [--max-value N] decode <hex>\n",
                argv[0], argv[0]);
        return 2;
    }

    BINARY_STDOUT();

    if (strcmp(action, "encode") == 0) return do_encode(arg, max_value);
    if (strcmp(action, "decode") == 0) return do_decode(arg);

    fprintf(stderr, "unknown action %s\n", action);
    return 2;
}
