#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "test.h"

static KinetisK22* create_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096;
    configuration.sram_size = 65536;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(state, device != NULL);
    uint32_t vectors[16] = {0};
    vectors[0] = 0x20001000u;
    vectors[1] = 0x00000101u;
    vectors[11] = 0x00000201u;
    vectors[15] = 0x00000221u;
    const uint8_t thread[] = {0x00, 0xdf, 0x00, 0xbf, 0x00, 0xbf, 0x00, 0xbe};
    const uint16_t svc_handler[] = {0x2155u, 0x4770u};
    const uint16_t systick_handler[] = {0x2266u, 0x4770u};
    TEST_EXPECT(state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)));
    TEST_EXPECT(state, kinetis_k22_load(device, 0x100, thread, sizeof(thread)));
    TEST_EXPECT(state, kinetis_k22_load(device, 0x200, svc_handler, sizeof(svc_handler)));
    TEST_EXPECT(state,
                kinetis_k22_load(device, 0x220, systick_handler, sizeof(systick_handler)));
    TEST_EXPECT(state, kinetis_k22_reset(device));
    return device;
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = create_device(&state);
    CortexM4* cpu = kinetis_k22_cpu(device);

    cortex_m4_step(cpu);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 15) == 0x102u);
    cortex_m4_step(cpu);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 1) == 0x55u);
    cortex_m4_step(cpu);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 15) == 0x102u);

    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, 0xe000e014u, 4, 1));
    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, 0xe000e018u, 4, 0));
    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, 0xe000e010u, 4, 3));
    cortex_m4_step(cpu);
    uint32_t current = 0;
    uint32_t control = 0;
    TEST_EXPECT(&state, cortex_m4_read_memory(cpu, 0xe000e018u, 4, &current));
    TEST_EXPECT(&state, current == 1);
    TEST_EXPECT(&state, cortex_m4_read_memory(cpu, 0xe000e010u, 4, &control));
    TEST_EXPECT(&state, (control & (1u << 16)) == 0);
    cortex_m4_step(cpu);
    TEST_EXPECT(&state, cortex_m4_read_memory(cpu, 0xe000e018u, 4, &current));
    TEST_EXPECT(&state, current == 0);
    TEST_EXPECT(&state, cortex_m4_read_memory(cpu, 0xe000e010u, 4, &control));
    TEST_EXPECT(&state, (control & (1u << 16)) != 0);
    cortex_m4_step(cpu);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 2) == 0x66u);
    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, 0xe000e010u, 4, 0));
    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, 0xe000ed04u, 4, 1u << 25));
    cortex_m4_step(cpu);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 15) == 0x106u);

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
