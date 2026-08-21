#ifndef CORTEX_M4_TEST_H
#define CORTEX_M4_TEST_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cortex_m4.h"

typedef struct {
    uint32_t cases;
    uint32_t passed;
    uint32_t failed;
} TestState;

static inline void expect(TestState* state, bool condition, const char* name) {
    state->cases++;
    if (condition) {
        state->passed++;
        return;
    }
    state->failed++;
    printf("[failed] %s\n", name);
}

static inline void test_connect_debugger(TestState* state, CortexM4* cpu) {
    expect(state, cortex_m4_write_memory(cpu, UINT32_C(0xe000edf0), 4, UINT32_C(0xa05f0001)),
           "connect debugger");
}

static inline int test_finish(const TestState* state) {
    printf("[summary] cases=%" PRIu32 " passed=%" PRIu32 " failed=%" PRIu32 "\n", state->cases,
           state->passed, state->failed);
    return state->failed == 0u ? 0 : 1;
}

#endif
