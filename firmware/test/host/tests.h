#ifndef HANDOFF_TESTS_H
#define HANDOFF_TESTS_H

void test_crc(void);
void test_manchester(void);
void test_goertzel(void);
void test_sync(void);
void test_frame(void);
void test_chunk(void);
void test_compact(void);
void test_vcard(void);
void test_frag(void);
void test_store(void);
void test_carousel(void);
void test_elect(void);
void test_beacon(void);
void test_link(void);
void test_vectors(void);
void test_channel(void);
void test_budget(void);

/*
 * Where tools/gen_vectors.py wrote its output. scripts/test.py drops an
 * absolute path into build-host/vectors_path.h so the tests do not care what
 * directory they are run from.
 */
#if defined(__has_include)
#  if __has_include("vectors_path.h")
#    include "vectors_path.h"
#  endif
#endif

#ifndef HANDOFF_VECTOR_DIR
#define HANDOFF_VECTOR_DIR "firmware/test/vectors/generated"
#endif

#endif /* HANDOFF_TESTS_H */
