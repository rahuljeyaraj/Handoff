/*
 * Handoff — a test harness small enough to read in one sitting.
 *
 * No framework. The point of M1 is that lib/ has no dependencies, and a test
 * runner that drags one in would be an odd way to make that point.
 */
#ifndef HANDOFF_HF_TEST_H
#define HANDOFF_HF_TEST_H

#include <stdio.h>
#include <string.h>

extern int hf_checks;
extern int hf_failures;
extern const char *hf_case;

void hf_begin(const char *name);
void hf_fail(const char *file, int line, const char *fmt, ...);

#define HF_CHECK(cond)                                                        \
    do {                                                                      \
        hf_checks++;                                                          \
        if (!(cond)) hf_fail(__FILE__, __LINE__, "%s", #cond);                \
    } while (0)

#define HF_CHECK_MSG(cond, ...)                                               \
    do {                                                                      \
        hf_checks++;                                                          \
        if (!(cond)) hf_fail(__FILE__, __LINE__, __VA_ARGS__);                \
    } while (0)

#define HF_EQ_INT(got, want)                                                  \
    do {                                                                      \
        const long long g_ = (long long)(got), w_ = (long long)(want);        \
        hf_checks++;                                                          \
        if (g_ != w_)                                                         \
            hf_fail(__FILE__, __LINE__, "%s: got %lld, want %lld",            \
                    #got, g_, w_);                                            \
    } while (0)

#define HF_EQ_STR(got, want)                                                  \
    do {                                                                      \
        const char *g_ = (got), *w_ = (want);                                 \
        hf_checks++;                                                          \
        if (strcmp(g_, w_) != 0)                                              \
            hf_fail(__FILE__, __LINE__, "%s:\n  got  [%s]\n  want [%s]",      \
                    #got, g_, w_);                                            \
    } while (0)

#define HF_EQ_MEM(got, want, n)                                               \
    do {                                                                      \
        hf_checks++;                                                          \
        if (memcmp((got), (want), (n)) != 0)                                  \
            hf_fail(__FILE__, __LINE__, "%s: %u bytes differ", #got,          \
                    (unsigned)(n));                                           \
    } while (0)

#define HF_NEAR(got, want, tol)                                               \
    do {                                                                      \
        const double g_ = (double)(got), w_ = (double)(want);                 \
        const double d_ = g_ > w_ ? g_ - w_ : w_ - g_;                        \
        hf_checks++;                                                          \
        if (d_ > (double)(tol))                                               \
            hf_fail(__FILE__, __LINE__, "%s: got %.4f, want %.4f +-%.4f",     \
                    #got, g_, w_, (double)(tol));                             \
    } while (0)

#endif /* HANDOFF_HF_TEST_H */
