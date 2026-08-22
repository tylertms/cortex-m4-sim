#include "architecture/cortex_m4/internal.h"

#include <string.h>

#include "test.h"

static bool bus_read(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                     uint32_t* value) {
    (void)context;
    (void)address;
    (void)size;
    (void)access;
    (void)value;
    return false;
}

static bool bus_write(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                      uint32_t value) {
    (void)context;
    (void)address;
    (void)size;
    (void)access;
    (void)value;
    return false;
}

static CortexM4 create_cpu(void) {
    CortexM4 cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.bus = (CortexM4Bus){NULL, bus_read, bus_write, NULL, NULL};
    cpu.external_irq_count = CORTEX_M4_IRQ_COUNT;
    cpu.priority_bits = 8u;
    cpu.registers[13] = UINT32_C(0x20001000);
    cpu.xpsr = CORTEX_M4_XPSR_T;
    return cpu;
}

static void test_push_pop_faults(TestState* state) {
    CortexM4 cpu = create_cpu();
    expect(state, cortex_m4_execute_thumb16(&cpu, UINT16_C(0xb401)),
           "PUSH encoding is recognized when its write faults");
    expect(state, (cpu.cfsr & (1u << 9u)) != 0u, "PUSH records a precise bus fault");

    cpu = create_cpu();
    expect(state, cortex_m4_execute_thumb16(&cpu, UINT16_C(0xbc01)),
           "POP encoding is recognized when its read faults");
    expect(state, (cpu.cfsr & (1u << 9u)) != 0u, "POP records a precise bus fault");
}

static void test_multiple_transfer_faults(TestState* state) {
    CortexM4 cpu = create_cpu();
    cpu.registers[0] = UINT32_C(0x20000000);
    expect(state, cortex_m4_execute_thumb32(&cpu, UINT16_C(0xe890), UINT16_C(0x0001)),
           "LDM encoding is recognized when its read faults");
    expect(state, cpu.bfar == UINT32_C(0x20000000), "LDM reports the failed source address");

    cpu = create_cpu();
    cpu.registers[0] = UINT32_C(0x20000000);
    expect(state, cortex_m4_execute_thumb32(&cpu, UINT16_C(0xe880), UINT16_C(0x0001)),
           "STM encoding is recognized when its write faults");
    expect(state, cpu.bfar == UINT32_C(0x20000000), "STM reports the failed destination address");
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    test_push_pop_faults(&state);
    test_multiple_transfer_faults(&state);
    return test_finish(&state);
}
