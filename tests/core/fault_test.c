#include "kinetis_k22.h"

#include <stdint.h>

#include "test.h"

enum { SCB_SHCSR = 0xe000ed24u };

static KinetisK22* create_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096;
    configuration.sram_size = 65536;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(state, device != NULL);
    uint32_t vectors[7] = {0};
    vectors[0] = 0x20001000u;
    vectors[1] = 0x00000101u;
    vectors[3] = 0x00000201u;
    vectors[5] = 0x00000221u;
    vectors[6] = 0x00000241u;
    const uint16_t hard_fault[] = {0x2455u, 0x4770u};
    const uint16_t bus_fault[] = {0x2466u, 0x4770u};
    const uint16_t usage_fault[] = {0x2477u, 0x4770u};
    TEST_EXPECT(state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)));
    TEST_EXPECT(state, kinetis_k22_load(device, 0x200, hard_fault, sizeof(hard_fault)));
    TEST_EXPECT(state, kinetis_k22_load(device, 0x220, bus_fault, sizeof(bus_fault)));
    TEST_EXPECT(state, kinetis_k22_load(device, 0x240, usage_fault, sizeof(usage_fault)));
    return device;
}

static void load_main(TestState* state, KinetisK22* device, uint16_t opcode) {
    const uint16_t program[] = {opcode, 0xbe00u};
    TEST_EXPECT(state, kinetis_k22_load(device, 0x100, program, sizeof(program)));
    TEST_EXPECT(state, kinetis_k22_reset(device));
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = create_device(&state);
    CortexM4* cpu = kinetis_k22_cpu(device);

    load_main(&state, device, 0x6808u);
    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, SCB_SHCSR, 4, 1u << 17));
    cortex_m4_set_register(cpu, 1, 0x60000000u);
    TEST_EXPECT(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(&state, (cortex_m4_get_fault_status(cpu) & (1u << 9)) != 0);
    TEST_EXPECT(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(&state, (cortex_m4_get_xpsr(cpu) & 0x1ffu) == 5);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 4) == 0x66u);
    cortex_m4_step(cpu);
    TEST_EXPECT(&state, (cortex_m4_get_xpsr(cpu) & 0x1ffu) == 0);

    load_main(&state, device, 0x6808u);
    cortex_m4_set_register(cpu, 1, 0x60000000u);
    cortex_m4_step(cpu);
    cortex_m4_step(cpu);
    TEST_EXPECT(&state, (cortex_m4_get_xpsr(cpu) & 0x1ffu) == 3);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 4) == 0x55u);

    load_main(&state, device, 0xde00u);
    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, SCB_SHCSR, 4, 1u << 18));
    cortex_m4_step(cpu);
    TEST_EXPECT(&state, (cortex_m4_get_fault_status(cpu) & (1u << 16)) != 0);
    cortex_m4_step(cpu);
    TEST_EXPECT(&state, (cortex_m4_get_xpsr(cpu) & 0x1ffu) == 6);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 4) == 0x77u);

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
