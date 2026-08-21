#include "kinetis_k22.h"

#include <stdint.h>

#include "test.h"

static KinetisK22* create_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096;
    configuration.sram_size = 65536;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(state, device != NULL, "device != NULL");
    const uint32_t vectors[2] = {0x20001000u, 0x00000101u};
    expect(state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0, vectors, sizeof(vectors))");
    return device;
}

static void execute(TestState* state, KinetisK22* device, uint16_t opcode) {
    expect(state, kinetis_k22_load(device, 0x100, &opcode, sizeof(opcode)),
           "kinetis_k22_load(device, 0x100, &opcode, sizeof(opcode))");
    cortex_m4_set_register(kinetis_k22_cpu(device), 15, 0x100);
    const CortexM4Result result = cortex_m4_step(kinetis_k22_cpu(device));
    expect(state, result.stop == CORTEX_M4_STOP_RUNNING, "result.stop == CORTEX_M4_STOP_RUNNING");
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = create_device(&state);
    expect(&state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    CortexM4* cpu = kinetis_k22_cpu(device);
    const uint32_t address = 0x20000040u;
    const uint32_t initial = 0x80ff7f01u;
    expect(&state, kinetis_k22_write(device, address, &initial, sizeof(initial)),
           "kinetis_k22_write(device, address, &initial, sizeof(initial))");
    cortex_m4_set_register(cpu, 1, address);
    cortex_m4_set_register(cpu, 2, 0);
    cortex_m4_set_register(cpu, 0, 0xa55ac33cu);

    execute(&state, device, 0x5888u);
    expect(&state, cortex_m4_get_register(cpu, 0) == initial,
           "cortex_m4_get_register(cpu, 0) == initial");
    execute(&state, device, 0x5a88u);
    expect(&state, cortex_m4_get_register(cpu, 0) == 0x7f01u,
           "cortex_m4_get_register(cpu, 0) == 0x7f01u");
    execute(&state, device, 0x5c88u);
    expect(&state, cortex_m4_get_register(cpu, 0) == 1, "cortex_m4_get_register(cpu, 0) == 1");
    cortex_m4_set_register(cpu, 2, 2);
    execute(&state, device, 0x5688u);
    expect(&state, cortex_m4_get_register(cpu, 0) == 0xffffffffu,
           "cortex_m4_get_register(cpu, 0) == 0xffffffffu");
    const int16_t negative_halfword = -128;
    expect(&state,
           kinetis_k22_write(device, address + 4, &negative_halfword, sizeof(negative_halfword)),
           "kinetis_k22_write(device, address + 4, &negative_halfword, "
           "sizeof(negative_halfword))");
    cortex_m4_set_register(cpu, 2, 4);
    execute(&state, device, 0x5e88u);
    expect(&state, cortex_m4_get_register(cpu, 0) == 0xffffff80u,
           "cortex_m4_get_register(cpu, 0) == 0xffffff80u");

    cortex_m4_set_register(cpu, 2, 4);
    cortex_m4_set_register(cpu, 0, 0x11223344u);
    execute(&state, device, 0x5088u);
    uint32_t stored = 0;
    expect(&state, kinetis_k22_read(device, address + 4, &stored, sizeof(stored)),
           "kinetis_k22_read(device, address + 4, &stored, sizeof(stored))");
    expect(&state, stored == 0x11223344u, "stored == 0x11223344u");
    cortex_m4_set_register(cpu, 2, 6);
    cortex_m4_set_register(cpu, 0, 0xabcd1234u);
    execute(&state, device, 0x5288u);
    uint16_t halfword = 0;
    expect(&state, kinetis_k22_read(device, address + 6, &halfword, sizeof(halfword)),
           "kinetis_k22_read(device, address + 6, &halfword, sizeof(halfword))");
    expect(&state, halfword == 0x1234u, "halfword == 0x1234u");
    cortex_m4_set_register(cpu, 2, 8);
    execute(&state, device, 0x5488u);
    uint8_t byte = 0;
    expect(&state, kinetis_k22_read(device, address + 8, &byte, sizeof(byte)),
           "kinetis_k22_read(device, address + 8, &byte, sizeof(byte))");
    expect(&state, byte == 0x34u, "byte == 0x34u");

    cortex_m4_set_register(cpu, 1, address);
    cortex_m4_set_register(cpu, 0, 0x55667788u);
    execute(&state, device, 0x6048u);
    execute(&state, device, 0x684au);
    expect(&state, cortex_m4_get_register(cpu, 2) == 0x55667788u,
           "cortex_m4_get_register(cpu, 2) == 0x55667788u");
    execute(&state, device, 0x7048u);
    execute(&state, device, 0x784au);
    expect(&state, cortex_m4_get_register(cpu, 2) == 0x88u,
           "cortex_m4_get_register(cpu, 2) == 0x88u");
    execute(&state, device, 0x8048u);
    execute(&state, device, 0x884au);
    expect(&state, cortex_m4_get_register(cpu, 2) == 0x7788u,
           "cortex_m4_get_register(cpu, 2) == 0x7788u");

    cortex_m4_set_register(cpu, 0, 0x20000100u);
    cortex_m4_set_register(cpu, 1, 0x11111111u);
    cortex_m4_set_register(cpu, 2, 0x22222222u);
    execute(&state, device, 0xc006u);
    expect(&state, cortex_m4_get_register(cpu, 0) == 0x20000108u,
           "cortex_m4_get_register(cpu, 0) == 0x20000108u");
    cortex_m4_set_register(cpu, 1, 0);
    cortex_m4_set_register(cpu, 2, 0);
    cortex_m4_set_register(cpu, 0, 0x20000100u);
    execute(&state, device, 0xc806u);
    expect(&state, cortex_m4_get_register(cpu, 1) == 0x11111111u,
           "cortex_m4_get_register(cpu, 1) == 0x11111111u");
    expect(&state, cortex_m4_get_register(cpu, 2) == 0x22222222u,
           "cortex_m4_get_register(cpu, 2) == 0x22222222u");

    cortex_m4_set_register(cpu, 0, 0x12345678u);
    cortex_m4_set_register(cpu, 14, 0xabcdef01u);
    const uint32_t original_stack = cortex_m4_get_register(cpu, 13);
    execute(&state, device, 0xb501u);
    expect(&state, cortex_m4_get_register(cpu, 13) == original_stack - 8,
           "cortex_m4_get_register(cpu, 13) == original_stack - 8");
    cortex_m4_set_register(cpu, 0, 0);
    cortex_m4_set_register(cpu, 14, 0);
    execute(&state, device, 0xbc01u);
    expect(&state, cortex_m4_get_register(cpu, 0) == 0x12345678u,
           "cortex_m4_get_register(cpu, 0) == 0x12345678u");
    expect(&state, cortex_m4_get_register(cpu, 13) == original_stack - 4,
           "cortex_m4_get_register(cpu, 13) == original_stack - 4");

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
