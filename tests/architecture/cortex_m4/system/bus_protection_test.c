#include "architecture/cortex_m4/internal.h"

#include <string.h>

#include "test.h"

static bool bus_read(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                     uint32_t* value) {
    (void)context;
    (void)address;
    (void)size;
    (void)access;
    *value = UINT32_C(0x5aa5a55a);
    return true;
}

static bool bus_write(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                      uint32_t value) {
    (void)context;
    (void)address;
    (void)size;
    (void)access;
    (void)value;
    return true;
}

static CortexM4 create_cpu(void) {
    CortexM4 cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.bus = (CortexM4Bus){NULL, bus_read, bus_write, NULL, NULL};
    cpu.external_irq_count = CORTEX_M4_IRQ_COUNT;
    cpu.mpu_region_count = CORTEX_M4_MPU_REGION_COUNT;
    cpu.priority_bits = 8u;
    cpu.xpsr = CORTEX_M4_XPSR_T;
    cortex_m4_mpu_reset(&cpu);
    return cpu;
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    CortexM4 cpu = create_cpu();
    uint32_t value = 0u;

    expect(&state,
           !cortex_m4_bus_read(&cpu, UINT32_C(0xe000ed94), 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA,
                               &value),
           "unprivileged MPU register read is rejected");
    expect(&state,
           !cortex_m4_bus_write(&cpu, UINT32_C(0xe000ed94), 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA,
                                1u),
           "unprivileged MPU register write is rejected");

    cpu.mpu_control = 1u;
    expect(&state,
           !cortex_m4_bus_read(&cpu, UINT32_C(0x20000000), 4u, CORTEX_M4_ACCESS_DATA, &value),
           "MPU blocks an unmapped data read");
    expect(&state,
           !cortex_m4_bus_write(&cpu, UINT32_C(0x20000000), 4u, CORTEX_M4_ACCESS_DATA, value),
           "MPU blocks an unmapped data write");

    return test_finish(&state);
}
