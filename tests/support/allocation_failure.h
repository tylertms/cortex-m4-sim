#ifndef K22_TEST_ALLOCATION_FAILURE_H
#define K22_TEST_ALLOCATION_FAILURE_H

#include <stddef.h>

void test_allow_allocations(void);
void test_fail_allocation_after(size_t successful_allocations);

#endif
