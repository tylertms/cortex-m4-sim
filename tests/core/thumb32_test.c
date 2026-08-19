#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "test.h"

static KinetisK22* create_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096;
    configuration.sram_size = 65536;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(state, device != NULL);
    const uint32_t vectors[2] = {0x20001000u, 0x00000101u};
    TEST_EXPECT(state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)));
    return device;
}

static void load_instruction(TestState* state, KinetisK22* device, uint16_t first,
                             uint16_t second) {
    const uint8_t program[] = {(uint8_t)first,
                               (uint8_t)(first >> 8),
                               (uint8_t)second,
                               (uint8_t)(second >> 8),
                               0x00,
                               0xbe};
    TEST_EXPECT(state, kinetis_k22_load(device, 0x100, program, sizeof(program)));
    TEST_EXPECT(state, kinetis_k22_reset(device));
    TEST_CONNECT_DEBUGGER(state, kinetis_k22_cpu(device));
}

static void execute(TestState* state, KinetisK22* device) {
    const CortexM4Result result =
        cortex_m4_run(kinetis_k22_cpu(device), (CortexM4RunLimits){2, 10});
    TEST_EXPECT(state, result.stop == CORTEX_M4_STOP_BREAKPOINT);
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = create_device(&state);
    CortexM4* cpu = kinetis_k22_cpu(device);

    load_instruction(&state, device, 0xfa01u, 0xf303u);
    cortex_m4_set_register(cpu, 1, 1);
    cortex_m4_set_register(cpu, 3, 4);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 3) == 16);

    load_instruction(&state, device, 0xfbb2u, 0xf3f3u);
    cortex_m4_set_register(cpu, 2, 100);
    cortex_m4_set_register(cpu, 3, 7);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 3) == 14);

    load_instruction(&state, device, 0xfb01u, 0xf303u);
    cortex_m4_set_register(cpu, 1, 0x10000u);
    cortex_m4_set_register(cpu, 3, 0x10000u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 3) == 0);

    load_instruction(&state, device, 0xfba3u, 0x1302u);
    cortex_m4_set_register(cpu, 3, 0xffffffffu);
    cortex_m4_set_register(cpu, 2, 2);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 1) == 0xfffffffeu);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 3) == 1);

    load_instruction(&state, device, 0xf3c2u, 0x020eu);
    cortex_m4_set_register(cpu, 2, 0xffffffffu);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 2) == 0x7fffu);

    load_instruction(&state, device, 0xf442u, 0x0270u);
    cortex_m4_set_register(cpu, 2, 1);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 2) == 0x00f00001u);

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
