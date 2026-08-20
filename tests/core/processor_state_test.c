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
    vectors[6] = 0x00000201u;
    const uint16_t usage_fault[] = {0xbe00u};
    TEST_EXPECT(state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)));
    TEST_EXPECT(state, kinetis_k22_load(device, 0x200, usage_fault, sizeof(usage_fault)));
    return device;
}

static void load_program(TestState* state, KinetisK22* device, const uint16_t* program,
                         size_t size) {
    TEST_EXPECT(state, kinetis_k22_load(device, 0x100, program, size));
    TEST_EXPECT(state, kinetis_k22_reset(device));
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = create_device(&state);
    CortexM4* cpu = kinetis_k22_cpu(device);
    const uint16_t mask_program[] = {0xb673u, 0xb661u, 0xb662u, 0xbe00u};
    load_program(&state, device, mask_program, sizeof(mask_program));
    TEST_EXPECT(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    const uint16_t read_masks[] = {0xf3efu, 0x8010u, 0xf3efu, 0x8113u};
    TEST_EXPECT(&state, kinetis_k22_load(device, 0x180, read_masks, sizeof(read_masks)));
    cortex_m4_set_register(cpu, 15, 0x180u);
    TEST_EXPECT(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 0) == 1);
    TEST_EXPECT(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 1) == 1);
    cortex_m4_set_register(cpu, 15, 0x102u);
    TEST_EXPECT(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    cortex_m4_set_register(cpu, 15, 0x180u);
    TEST_EXPECT(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 0) == 1);
    cortex_m4_set_register(cpu, 15, 0x104u);
    TEST_EXPECT(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    cortex_m4_set_register(cpu, 15, 0x184u);
    TEST_EXPECT(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 1) == 0);

    const uint16_t unprivileged_program[] = {0xb673u, 0xbe00u};
    load_program(&state, device, unprivileged_program, sizeof(unprivileged_program));
    cortex_m4_set_control(cpu, 1);
    TEST_EXPECT(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    cortex_m4_set_register(cpu, 15, 0x180u);
    TEST_EXPECT(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 0) == 0);
    TEST_EXPECT(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 1) == 0);

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
