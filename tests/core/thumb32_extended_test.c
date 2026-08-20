#include "kinetis_k22.h"

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
    const uint16_t program[] = {first, second, 0xbe00u};
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

    load_instruction(&state, device, 0xfab1u, 0xf081u);
    cortex_m4_set_register(cpu, 1, 0x00001000u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 0) == 19);

    load_instruction(&state, device, 0xfa91u, 0xf0a1u);
    cortex_m4_set_register(cpu, 1, 0x80000001u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 0) == 0x80000001u);

    load_instruction(&state, device, 0xf301u, 0x000fu);
    cortex_m4_set_register(cpu, 1, 0x10000u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 0) == 0x7fffu);
    TEST_EXPECT(&state, (cortex_m4_get_xpsr(cpu) & (1u << 27)) != 0);

    load_instruction(&state, device, 0xf381u, 0x0010u);
    cortex_m4_set_register(cpu, 1, 0xffffffffu);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 0) == 0);

    load_instruction(&state, device, 0xf361u, 0x200fu);
    cortex_m4_set_register(cpu, 0, 0xffff0000u);
    cortex_m4_set_register(cpu, 1, 0x5au);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 0) == 0xffff5a00u);

    load_instruction(&state, device, 0xf36fu, 0x02c7u);
    cortex_m4_set_register(cpu, 2, 0xffffffffu);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 2) == 0xffffff07u);

    load_instruction(&state, device, 0xeb14u, 0x045cu);
    cortex_m4_set_register(cpu, 4, 5);
    cortex_m4_set_register(cpu, 12, 8);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 4) == 9);

    const uint32_t address = 0x20000020u;
    const uint16_t exclusive_program[] = {0xe851u, 0x0f00u, 0xe841u, 0x3200u, 0xbe00u};
    TEST_EXPECT(&state, kinetis_k22_load(device, 0x100, exclusive_program,
                                         sizeof(exclusive_program)));
    TEST_EXPECT(&state, kinetis_k22_reset(device));
    TEST_CONNECT_DEBUGGER(&state, cpu);
    uint32_t memory = 0x11223344u;
    TEST_EXPECT(&state, kinetis_k22_write(device, address, &memory, sizeof(memory)));
    cortex_m4_set_register(cpu, 1, address);
    cortex_m4_set_register(cpu, 3, 0xa55ac33cu);
    CortexM4Result result = cortex_m4_run(cpu, (CortexM4RunLimits){3, 10});
    TEST_EXPECT(&state, result.stop == CORTEX_M4_STOP_BREAKPOINT);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 2) == 0);
    TEST_EXPECT(&state, kinetis_k22_read(device, address, &memory, sizeof(memory)));
    TEST_EXPECT(&state, memory == 0xa55ac33cu);

    const uint8_t table[] = {3, 7, 9};
    TEST_EXPECT(&state, kinetis_k22_load(device, 0x200, table, sizeof(table)));
    load_instruction(&state, device, 0xe8d0u, 0xf001u);
    cortex_m4_set_register(cpu, 0, 0x200u);
    cortex_m4_set_register(cpu, 1, 1);
    const CortexM4Result table_result = cortex_m4_step(cpu);
    TEST_EXPECT(&state, table_result.stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 15) == 0x112u);

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
