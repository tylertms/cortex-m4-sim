#ifndef CORTEX_M4_TEST_H
#define CORTEX_M4_TEST_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    uint64_t assertions;
} TestState;

static void test_fail(const char* expression, const char* file, int line) {
    fprintf(stderr, "failed: %s at %s:%d\n", expression, file, line);
    exit(EXIT_FAILURE);
}

#define TEST_EXPECT(state, expression)                                                     \
    do {                                                                                   \
        (state)->assertions++;                                                             \
        if (!(expression))                                                                 \
            test_fail(#expression, __FILE__, __LINE__);                                    \
    } while (0)

#define TEST_CONNECT_DEBUGGER(state, cpu)                                                  \
    TEST_EXPECT((state), cortex_m4_write_memory((cpu), UINT32_C(0xe000edf0), 4,            \
                                                UINT32_C(0xa05f0001)))

static int test_finish(const TestState* state) {
    printf("passed: %llu assertions\n", (unsigned long long)state->assertions);
    return EXIT_SUCCESS;
}

#endif
