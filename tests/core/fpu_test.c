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
    TEST_EXPECT(state, cortex_m4_write_memory(kinetis_k22_cpu(device), 0xe000ed88u, 4,
                                              0x00f00000u));
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

    load_instruction(&state, device, 0xee07u, 0x3a10u);
    cortex_m4_set_register(cpu, 3, 0x40400000u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_fp_register(cpu, 14) == 0x40400000u);

    load_instruction(&state, device, 0xee17u, 0x3a90u);
    cortex_m4_set_fp_register(cpu, 15, 0x3fc00000u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 3) == 0x3fc00000u);

    load_instruction(&state, device, 0xee37u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14, 0x3fc00000u);
    cortex_m4_set_fp_register(cpu, 15, 0x40000000u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_fp_register(cpu, 14) == 0x40600000u);

    load_instruction(&state, device, 0xee37u, 0x7a67u);
    cortex_m4_set_fp_register(cpu, 14, 0x3fc00000u);
    cortex_m4_set_fp_register(cpu, 15, 0x40000000u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_fp_register(cpu, 14) == 0xbf000000u);

    load_instruction(&state, device, 0xee27u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14, 0x3fc00000u);
    cortex_m4_set_fp_register(cpu, 15, 0x40000000u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_fp_register(cpu, 14) == 0x40400000u);

    load_instruction(&state, device, 0xee87u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14, 0x3fc00000u);
    cortex_m4_set_fp_register(cpu, 15, 0x40000000u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_fp_register(cpu, 14) == 0x3f400000u);

    load_instruction(&state, device, 0xeef8u, 0x7a47u);
    cortex_m4_set_fp_register(cpu, 14, 3);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_fp_register(cpu, 15) == 0x40400000u);

    load_instruction(&state, device, 0xeefdu, 0x7ae7u);
    cortex_m4_set_fp_register(cpu, 15, 0x4079999au);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_fp_register(cpu, 15) == 3);

    load_instruction(&state, device, 0xeeb4u, 0x7ae7u);
    cortex_m4_set_fp_register(cpu, 14, 0x3f800000u);
    cortex_m4_set_fp_register(cpu, 15, 0x40000000u);
    execute(&state, device);
    TEST_EXPECT(&state, (cortex_m4_get_fpscr(cpu) & 0xf0000000u) == 0x80000000u);

    load_instruction(&state, device, 0xed87u, 0x0a01u);
    cortex_m4_set_register(cpu, 7, 0x20000020u);
    cortex_m4_set_fp_register(cpu, 0, 0x41200000u);
    execute(&state, device);
    uint32_t memory = 0;
    TEST_EXPECT(&state, kinetis_k22_read(device, 0x20000024u, &memory, sizeof(memory)));
    TEST_EXPECT(&state, memory == 0x41200000u);

    load_instruction(&state, device, 0xed97u, 0x0a01u);
    memory = 0x41200000u;
    TEST_EXPECT(&state, kinetis_k22_write(device, 0x20000024u, &memory, sizeof(memory)));
    cortex_m4_set_register(cpu, 7, 0x20000020u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_fp_register(cpu, 0) == 0x41200000u);

    load_instruction(&state, device, 0xeeb0u, 0x0a67u);
    cortex_m4_set_fp_register(cpu, 15, 0xc0600000u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_fp_register(cpu, 0) == 0xc0600000u);

    load_instruction(&state, device, 0xeef1u, 0x7a04u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_fp_register(cpu, 15) == 0x40a00000u);

    load_instruction(&state, device, 0xeeb4u, 0x7ae7u);
    cortex_m4_set_fp_register(cpu, 14, 0x3f800000u);
    cortex_m4_set_fp_register(cpu, 15, 0x3f800000u);
    execute(&state, device);
    TEST_EXPECT(&state, (cortex_m4_get_fpscr(cpu) & 0xf0000000u) == 0x60000000u);

    load_instruction(&state, device, 0xeeb4u, 0x7ae7u);
    cortex_m4_set_fp_register(cpu, 14, 0x7fc00000u);
    cortex_m4_set_fp_register(cpu, 15, 0x3f800000u);
    execute(&state, device);
    TEST_EXPECT(&state, (cortex_m4_get_fpscr(cpu) & 0xf0000000u) == 0x30000000u);

    load_instruction(&state, device, 0xeef1u, 0xfa10u);
    cortex_m4_set_fpscr(cpu, 0xa0000000u);
    execute(&state, device);
    TEST_EXPECT(&state, (cortex_m4_get_xpsr(cpu) & 0xf0000000u) == 0xa0000000u);

    load_instruction(&state, device, 0xeefdu, 0x7ae7u);
    cortex_m4_set_fp_register(cpu, 15, 0x7fc00000u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_fp_register(cpu, 15) == 0x80000000u);
    TEST_EXPECT(&state, (cortex_m4_get_fpscr(cpu) & 1u) != 0);

    load_instruction(&state, device, 0xeefcu, 0x7ae7u);
    cortex_m4_set_fp_register(cpu, 15, 0xbf800000u);
    execute(&state, device);
    TEST_EXPECT(&state, cortex_m4_get_fp_register(cpu, 15) == 0);
    TEST_EXPECT(&state, (cortex_m4_get_fpscr(cpu) & 1u) != 0);

    load_instruction(&state, device, 0xee37u, 0x7a27u);
    TEST_EXPECT(&state, cortex_m4_write_memory(cpu, 0xe000ed88u, 4, 0));
    cortex_m4_step(cpu);
    TEST_EXPECT(&state, (cortex_m4_get_fault_status(cpu) & (1u << 19)) != 0);

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
