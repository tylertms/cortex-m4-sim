#include "kinetis_k22.h"

#include <stdint.h>

#include "test.h"

static KinetisK22* create_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096;
    configuration.sram_size = 65536;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(state, device != NULL, "device != NULL");
    uint32_t vectors[16] = {0};
    vectors[0] = 0x20001000u;
    vectors[1] = 0x00000101u;
    vectors[11] = 0x00000201u;
    vectors[15] = 0x00000221u;
    const uint8_t thread[] = {0x00, 0xdf, 0x00, 0xbf, 0x00, 0xbf, 0x00, 0xbe};
    const uint16_t svc_handler[] = {0x2155u, 0x4770u};
    const uint16_t systick_handler[] = {0x2266u, 0x4770u};
    expect(state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0, vectors, sizeof(vectors))");
    expect(state, kinetis_k22_load(device, 0x100, thread, sizeof(thread)),
           "kinetis_k22_load(device, 0x100, thread, sizeof(thread))");
    expect(state, kinetis_k22_load(device, 0x200, svc_handler, sizeof(svc_handler)),
           "kinetis_k22_load(device, 0x200, svc_handler, sizeof(svc_handler))");
    expect(state, kinetis_k22_load(device, 0x220, systick_handler, sizeof(systick_handler)),
           "kinetis_k22_load(device, 0x220, systick_handler, sizeof(systick_handler))");
    expect(state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    return device;
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = create_device(&state);
    CortexM4* cpu = kinetis_k22_cpu(device);

    cortex_m4_step(cpu);
    expect(&state, cortex_m4_get_register(cpu, 15) == 0x102u,
           "cortex_m4_get_register(cpu, 15) == 0x102u");
    cortex_m4_step(cpu);
    expect(&state, cortex_m4_get_register(cpu, 1) == 0x55u,
           "cortex_m4_get_register(cpu, 1) == 0x55u");
    cortex_m4_step(cpu);
    expect(&state, cortex_m4_get_register(cpu, 15) == 0x102u,
           "cortex_m4_get_register(cpu, 15) == 0x102u");

    expect(&state, cortex_m4_write_memory(cpu, 0xe000e014u, 4, 1),
           "cortex_m4_write_memory(cpu, 0xe000e014u, 4, 1)");
    expect(&state, cortex_m4_write_memory(cpu, 0xe000e018u, 4, 0),
           "cortex_m4_write_memory(cpu, 0xe000e018u, 4, 0)");
    expect(&state, cortex_m4_write_memory(cpu, 0xe000e010u, 4, 3),
           "cortex_m4_write_memory(cpu, 0xe000e010u, 4, 3)");
    cortex_m4_step(cpu);
    uint32_t current = 0;
    uint32_t control = 0;
    expect(&state, cortex_m4_read_memory(cpu, 0xe000e018u, 4, &current),
           "cortex_m4_read_memory(cpu, 0xe000e018u, 4, &current)");
    expect(&state, current == 1, "current == 1");
    expect(&state, cortex_m4_read_memory(cpu, 0xe000e010u, 4, &control),
           "cortex_m4_read_memory(cpu, 0xe000e010u, 4, &control)");
    expect(&state, (control & (1u << 16)) == 0, "(control & (1u << 16)) == 0");
    cortex_m4_step(cpu);
    expect(&state, cortex_m4_read_memory(cpu, 0xe000e018u, 4, &current),
           "cortex_m4_read_memory(cpu, 0xe000e018u, 4, &current)");
    expect(&state, current == 0, "current == 0");
    expect(&state, cortex_m4_read_memory(cpu, 0xe000e010u, 4, &control),
           "cortex_m4_read_memory(cpu, 0xe000e010u, 4, &control)");
    expect(&state, (control & (1u << 16)) != 0, "(control & (1u << 16)) != 0");
    cortex_m4_step(cpu);
    expect(&state, cortex_m4_get_register(cpu, 2) == 0x66u,
           "cortex_m4_get_register(cpu, 2) == 0x66u");
    expect(&state, cortex_m4_write_memory(cpu, 0xe000e010u, 4, 0),
           "cortex_m4_write_memory(cpu, 0xe000e010u, 4, 0)");
    expect(&state, cortex_m4_write_memory(cpu, 0xe000ed04u, 4, 1u << 25),
           "cortex_m4_write_memory(cpu, 0xe000ed04u, 4, 1u << 25)");
    cortex_m4_step(cpu);
    expect(&state, cortex_m4_get_register(cpu, 15) == 0x106u,
           "cortex_m4_get_register(cpu, 15) == 0x106u");

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
