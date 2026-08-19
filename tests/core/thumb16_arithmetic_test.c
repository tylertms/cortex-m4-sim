#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "test.h"

static void execute(TestState* state, KinetisK22* device, uint16_t opcode, uint32_t first,
                    uint32_t second, uint32_t xpsr) {
    const uint32_t vectors[2] = {0x20001000u, 0x00000101u};
    TEST_EXPECT(state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)));
    TEST_EXPECT(state, kinetis_k22_load(device, 0x100, &opcode, sizeof(opcode)));
    TEST_EXPECT(state, kinetis_k22_reset(device));
    CortexM4* cpu = kinetis_k22_cpu(device);
    cortex_m4_set_register(cpu, 0, first);
    cortex_m4_set_register(cpu, 1, second);
    cortex_m4_set_xpsr(cpu, xpsr | (1u << 24));
    const CortexM4Result result = cortex_m4_step(cpu);
    TEST_EXPECT(state, result.stop == CORTEX_M4_STOP_RUNNING);
}

static uint32_t result(KinetisK22* device) {
    return cortex_m4_get_register(kinetis_k22_cpu(device), 0);
}

int main(void) {
    TestState state = {0};
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096;
    configuration.sram_size = 65536;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(&state, device != NULL);

    execute(&state, device, 0x0048u, 0, 0x80000001u, 0);
    TEST_EXPECT(&state, result(device) == 2);
    execute(&state, device, 0x0848u, 0, 3, 0);
    TEST_EXPECT(&state, result(device) == 1);
    execute(&state, device, 0x1048u, 0, 0x80000000u, 0);
    TEST_EXPECT(&state, result(device) == 0xc0000000u);

    execute(&state, device, 0x1888u, 0, 5, 0);
    cortex_m4_set_register(kinetis_k22_cpu(device), 2, 7);
    cortex_m4_set_register(kinetis_k22_cpu(device), 15, 0x100);
    cortex_m4_step(kinetis_k22_cpu(device));
    TEST_EXPECT(&state, result(device) == 12);
    execute(&state, device, 0x1a88u, 0, 9, 0);
    cortex_m4_set_register(kinetis_k22_cpu(device), 2, 4);
    cortex_m4_set_register(kinetis_k22_cpu(device), 15, 0x100);
    cortex_m4_step(kinetis_k22_cpu(device));
    TEST_EXPECT(&state, result(device) == 5);
    execute(&state, device, 0x1cc8u, 0, 9, 0);
    TEST_EXPECT(&state, result(device) == 12);
    execute(&state, device, 0x1ec8u, 0, 9, 0);
    TEST_EXPECT(&state, result(device) == 6);

    const uint16_t operations[] = {0x4008u, 0x4048u, 0x4088u, 0x40c8u, 0x4108u, 0x4148u,
                                   0x4188u, 0x41c8u, 0x4208u, 0x4248u, 0x4288u, 0x42c8u,
                                   0x4308u, 0x4348u, 0x4388u, 0x43c8u};
    const uint32_t expected[] = {
        1,           0xfffffffeu, 0xfffffffeu, 0x7fffffffu, 0xffffffffu, 1,
        0xfffffffeu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
        0xffffffffu, 0xffffffffu, 0xfffffffeu, 0xfffffffeu,
    };
    for (size_t index = 0; index < sizeof(operations) / sizeof(operations[0]); index++) {
        execute(&state, device, operations[index], 0xffffffffu, 1,
                index == 5 || index == 6 ? 1u << 29 : 0);
        TEST_EXPECT(&state, result(device) == expected[index]);
    }

    const uint16_t register_shifts[] = {0x4088u, 0x40c8u, 0x4108u, 0x41c8u};
    for (size_t index = 0; index < sizeof(register_shifts) / sizeof(register_shifts[0]);
         index++) {
        execute(&state, device, register_shifts[index], 0x81234567u, 0, 1u << 29);
        TEST_EXPECT(&state, result(device) == 0x81234567u);
        TEST_EXPECT(&state,
                    (cortex_m4_get_xpsr(kinetis_k22_cpu(device)) & (1u << 29)) != 0);
    }

    execute(&state, device, 0xb208u, 0, 0x00008001u, 0);
    TEST_EXPECT(&state, result(device) == 0xffff8001u);
    execute(&state, device, 0xb248u, 0, 0x00000081u, 0);
    TEST_EXPECT(&state, result(device) == 0xffffff81u);
    execute(&state, device, 0xb288u, 0, 0x12345678u, 0);
    TEST_EXPECT(&state, result(device) == 0x5678u);
    execute(&state, device, 0xb2c8u, 0, 0x12345678u, 0);
    TEST_EXPECT(&state, result(device) == 0x78u);
    execute(&state, device, 0xba08u, 0, 0x12345678u, 0);
    TEST_EXPECT(&state, result(device) == 0x78563412u);
    execute(&state, device, 0xba48u, 0, 0x12345678u, 0);
    TEST_EXPECT(&state, result(device) == 0x34127856u);
    execute(&state, device, 0xbac8u, 0, 0x00008001u, 0);
    TEST_EXPECT(&state, result(device) == 0x00000180u);

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
