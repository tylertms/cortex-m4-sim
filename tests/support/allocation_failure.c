#include "allocation_failure.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef K22_TEST_ALLOCATION_FAILURE
static size_t allocations_remaining = SIZE_MAX;

void* __real_malloc(size_t size);
void* __real_calloc(size_t count, size_t size);
void* __real_realloc(void* allocation, size_t size);

static bool reject_allocation(void) {
    if (allocations_remaining == SIZE_MAX)
        return false;
    if (allocations_remaining == 0u)
        return true;
    allocations_remaining--;
    return false;
}

void* __wrap_malloc(size_t size) {
    return reject_allocation() ? NULL : __real_malloc(size);
}

void* __wrap_calloc(size_t count, size_t size) {
    return reject_allocation() ? NULL : __real_calloc(count, size);
}

void* __wrap_realloc(void* allocation, size_t size) {
    return reject_allocation() ? NULL : __real_realloc(allocation, size);
}

void test_allow_allocations(void) { allocations_remaining = SIZE_MAX; }

void test_fail_allocation_after(size_t successful_allocations) {
    allocations_remaining = successful_allocations;
}
#else
void test_allow_allocations(void) {}

void test_fail_allocation_after(size_t successful_allocations) {
    (void)successful_allocations;
}
#endif
