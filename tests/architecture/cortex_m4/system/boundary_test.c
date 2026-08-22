#include "architecture/cortex_m4/internal.h"

#include <string.h>

#include "test.h"

enum { MEMORY_SIZE = 256 };

typedef struct {
    uint8_t memory[MEMORY_SIZE];
    bool reject;
} TestBus;

static bool bus_read(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                     uint32_t* value) {
    TestBus* bus = context;
    (void)access;
    if (bus->reject || address < 0x20000000u ||
        address - 0x20000000u > (uint32_t)MEMORY_SIZE - size) {
        return false;
    }
    memcpy(value, &bus->memory[address - 0x20000000u], size);
    return true;
}

static bool bus_write(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                      uint32_t value) {
    TestBus* bus = context;
    (void)access;
    if (bus->reject || address < 0x20000000u ||
        address - 0x20000000u > (uint32_t)MEMORY_SIZE - size) {
        return false;
    }
    memcpy(&bus->memory[address - 0x20000000u], &value, size);
    return true;
}

static CortexM4* create_cpu(TestState* state, TestBus* bus) {
    const CortexM4Bus interface = {bus, bus_read, bus_write, NULL, NULL};
    CortexM4* cpu = cortex_m4_create(interface);
    expect(state, cpu != NULL, "cpu != NULL");
    return cpu;
}

static void test_system_access_guards(TestState* state, CortexM4* cpu) {
    uint32_t value = 0u;
    expect(state,
           cortex_m4_system_read(cpu, 0xe0080000u, 4u, CORTEX_M4_ACCESS_DEBUG, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_OUTSIDE,
           "system read gap is outside");
    expect(state,
           cortex_m4_system_read(NULL, 0xe000e000u, 4u, CORTEX_M4_ACCESS_DEBUG, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "system read rejects null cpu");
    expect(state,
           cortex_m4_system_read(cpu, 0xe000e000u, 4u, CORTEX_M4_ACCESS_DEBUG, NULL) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "system read rejects null value");
    expect(state,
           cortex_m4_system_write(cpu, 0xe0080000u, 4u, CORTEX_M4_ACCESS_DEBUG, value) ==
               CORTEX_M4_SYSTEM_ACCESS_OUTSIDE,
           "system write gap is outside");
    expect(state,
           cortex_m4_system_write(NULL, 0xe000e000u, 4u, CORTEX_M4_ACCESS_DEBUG, value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "system write rejects null cpu");
    expect(state,
           cortex_m4_system_write(cpu, 0xe000ef00u, 1u, CORTEX_M4_ACCESS_DEBUG, 0u) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "software interrupt requires a word write");
}

static void test_exception_guards(TestState* state, CortexM4* cpu, TestBus* bus) {
    cortex_m4_system_reset(NULL);
    cortex_m4_system_set_pending(NULL, 2u, true);
    cortex_m4_system_set_pending(cpu, 1u, true);
    expect(state, !cortex_m4_system_exception_before(cpu, 0u, 2u),
           "no exception sorts before an active exception");
    expect(state, !cortex_m4_system_exception_before(cpu, 1u, 2u),
           "reserved exception does not precede nonmaskable interrupt");
    expect(state, !cortex_m4_system_valid_exception_return(NULL, 0xfffffff9u),
           "null cpu has no valid exception return");
    cpu->xpsr = CORTEX_M4_XPSR_T | 3u;
    expect(state, !cortex_m4_system_valid_exception_return(cpu, 0x12345678u),
           "noncanonical exception return is rejected");
    uint32_t stack_pointer = 0x20000100u;
    uint32_t return_value = 0u;
    cpu->exception_frame_depth = CORTEX_M4_EXCEPTION_FRAME_LIMIT;
    expect(state, !cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value),
           "full exception frame stack is rejected");
    cpu->exception_frame_depth = 0u;
    bus->reject = true;
    expect(state, !cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value),
           "failed stack write rejects exception frame");
}

int main(void) {
    TestState state = {0};
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(&state, &bus);
    test_system_access_guards(&state, cpu);
    test_exception_guards(&state, cpu, &bus);
    cortex_m4_destroy(cpu);
    return test_finish(&state);
}
