/*
 * Handoff — host unit test runner. Development plan M1.
 *
 * Every module under lib/dsp, lib/link, lib/record and lib/proto is compiled
 * here with the host compiler. That is not only convenience: it is the
 * enforcement mechanism for architecture §3.2. Reaching for hardware/adc.h in
 * any of those four directories fails this build immediately.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hf_test.h"
#include "tests.h"

int hf_checks = 0;
int hf_failures = 0;
const char *hf_case = "";

void hf_begin(const char *name)
{
    hf_case = name;
}

void hf_fail(const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    hf_failures++;
    fprintf(stderr, "  FAIL %s (%s:%d)\n        ", hf_case, file, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

typedef struct { const char *name; void (*fn)(void); } suite_t;

static const suite_t k_suites[] = {
    { "crc",        test_crc },
    { "manchester", test_manchester },
    { "goertzel",   test_goertzel },
    { "sync",       test_sync },
    { "frame",      test_frame },
    { "chunk",      test_chunk },
    { "compact",    test_compact },
    { "vcard",      test_vcard },
    { "frag",       test_frag },
    { "store",      test_store },
    { "carousel",   test_carousel },
    { "elect",      test_elect },
    { "beacon",     test_beacon },
    { "link",       test_link },
    { "vectors",    test_vectors },
    { "channel",    test_channel },
    { "budget",     test_budget },
};

int main(int argc, char **argv)
{
    const char *only = (argc > 1) ? argv[1] : NULL;
    size_t i;
    int ran = 0;

    for (i = 0; i < sizeof k_suites / sizeof k_suites[0]; i++) {
        int before;
        if (only && strcmp(only, k_suites[i].name) != 0) continue;

        before = hf_failures;
        printf("== %s\n", k_suites[i].name);
        hf_begin(k_suites[i].name);
        k_suites[i].fn();
        ran++;
        if (hf_failures == before) printf("   ok\n");
    }

    if (!ran) {
        fprintf(stderr, "no suite matched '%s'\n", only ? only : "");
        return 2;
    }

    printf("\n%d checks, %d failures\n", hf_checks, hf_failures);
    return hf_failures ? 1 : 0;
}
