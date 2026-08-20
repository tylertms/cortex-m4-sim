#include "kinetis_k22.h"

#include <stdint.h>

#include "test.h"

enum {
    NVIC_ENABLE = 0xe000e100u,
    NVIC_PENDING = 0xe000e200u,
    NVIC_CLEAR_PENDING = 0xe000e280u,
    NVIC_PRIORITY = 0xe000e400u,
    SCB_CPUID = 0xe000ed00u,
    SCB_ICTR = 0xe000e004u,
    MPU_TYPE = 0xe000ed90u,
    SCB_ICSR = 0xe000ed04u,
    SCB_AIRCR = 0xe000ed0cu,
    SCB_SHPR = 0xe000ed18u,
    SCB_SHCSR = 0xe000ed24u,
    NVIC_SOFTWARE_TRIGGER = 0xe000ef00u,
    RESET_STATUS_0 = 0x4007f000u,
    RESET_STATUS_1 = 0x4007f001u,
};

static uint32_t read_value(TestState* state, CortexM4* cpu, uint32_t address,
                           uint8_t size) {
    uint32_t value = 0;
    TEST_EXPECT(state, cortex_m4_read_memory(cpu, address, size, &value));
    return value;
}

static KinetisK22* create_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096;
    configuration.sram_size = 65536;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(state, device != NULL);
    const uint32_t vectors[2] = {0x20001000u, 0x00000101u};
    const uint16_t nop = 0xbf00u;
    TEST_EXPECT(state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)));
    TEST_EXPECT(state, kinetis_k22_load(device, 0x100, &nop, sizeof(nop)));
    TEST_EXPECT(state, kinetis_k22_reset(device));
    return device;
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = create_device(&state);
    CortexM4* cpu = kinetis_k22_cpu(device);

    TEST_EXPECT(&state, read_value(&state, cpu, SCB_CPUID, 4) == 0x410fc241u);
    TEST_EXPECT(&state, read_value(&state, cpu, SCB_ICTR, 4) == 2u);
    TEST_EXPECT(&state, read_value(&state, cpu, MPU_TYPE, 4) == 0u);
    cortex_m4_set_irq(cpu, 85, true);
    cortex_m4_set_irq(cpu, 86, true);
    TEST_EXPECT(&state, cortex_m4_get_irq_pending(cpu, 85));
    TEST_EXPECT(&state, !cortex_m4_get_irq_pending(cpu, 86));
    cortex_m4_set_irq(cpu, 85, false);
    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, NVIC_ENABLE, 4, 0x00000100u));
    TEST_EXPECT(&state, read_value(&state, cpu, NVIC_ENABLE, 4) == 0x00000100u);

    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, NVIC_PRIORITY + 8u, 4, 0x12345678u));
    TEST_EXPECT(&state, read_value(&state, cpu, NVIC_PRIORITY + 8u, 4) == 0x10305070u);
    cortex_m4_set_irq(cpu, 8, true);
    TEST_EXPECT(&state, read_value(&state, cpu, NVIC_PENDING, 4) == 0x00000100u);
    TEST_EXPECT(&state, (read_value(&state, cpu, SCB_ICSR, 4) & 0x001ff000u) == 24u << 12);
    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, NVIC_CLEAR_PENDING, 4, 0x00000100u));
    TEST_EXPECT(&state, !cortex_m4_get_irq_pending(cpu, 8));

    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, NVIC_SOFTWARE_TRIGGER, 4, 33));
    TEST_EXPECT(&state, cortex_m4_get_irq_pending(cpu, 33));
    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, SCB_SHPR, 4, 0x12345678u));
    TEST_EXPECT(&state, read_value(&state, cpu, SCB_SHPR, 4) == 0x00305070u);
    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, SCB_SHCSR, 4, 0xffffffffu));
    TEST_EXPECT(&state, read_value(&state, cpu, SCB_SHCSR, 4) == 0x0007f000u);

    TEST_EXPECT(&state,
                (read_value(&state, cpu, SCB_AIRCR, 4) & 0xffff0000u) == 0xfa050000u);
    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, SCB_AIRCR, 4, 0x05fa0300u));
    TEST_EXPECT(&state, (read_value(&state, cpu, SCB_AIRCR, 4) & 0x00000700u) == 0x300u);

    const uint32_t retained = 0x6d345abcu;
    TEST_EXPECT(&state,
                kinetis_k22_write(device, 0x20000000u, &retained, sizeof(retained)));
    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, SCB_AIRCR, 4, 0x05fa0004u));
    cortex_m4_step(cpu);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 15) == 0x100u);
    TEST_EXPECT(&state, read_value(&state, cpu, RESET_STATUS_0, 1) == 0);
    TEST_EXPECT(&state, read_value(&state, cpu, RESET_STATUS_1, 1) == 0x04u);
    TEST_EXPECT(&state, read_value(&state, cpu, 0x20000000u, 4) == retained);

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
