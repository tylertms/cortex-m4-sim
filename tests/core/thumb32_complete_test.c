#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "cortex_m4_internal.h"
#include "test.h"

typedef struct {
    TestState* state;
    KinetisK22* device;
    CortexM4* cpu;
} Fixture;

static void execute(Fixture* fixture, uint16_t first, uint16_t second) {
    fixture->cpu->registers[15] = 0x104u;
    TEST_EXPECT(fixture->state, cortex_m4_execute_thumb32(fixture->cpu, first, second));
}

static void test_branches(Fixture* fixture) {
    execute(fixture, 0xf000u, 0xf800u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[14] == 0x105u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[15] == 0x104u);
    execute(fixture, 0xf000u, 0xb800u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[15] == 0x104u);
    fixture->cpu->xpsr |= CORTEX_M4_XPSR_Z;
    execute(fixture, 0xf000u, 0x8000u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[15] == 0x104u);
    fixture->cpu->xpsr &= ~CORTEX_M4_XPSR_Z;
    execute(fixture, 0xf000u, 0x8002u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[15] == 0x104u);
}

static void test_special_registers(Fixture* fixture) {
    fixture->cpu->xpsr = CORTEX_M4_XPSR_T | CORTEX_M4_XPSR_N | CORTEX_M4_XPSR_C | 7u;
    fixture->cpu->msp = 0x20001000u;
    fixture->cpu->psp = 0x20002000u;
    fixture->cpu->primask = 1u;
    fixture->cpu->basepri = 0x80u;
    fixture->cpu->faultmask = 1u;
    fixture->cpu->control = 3u;
    const uint8_t selectors[] = {0u, 1u,  2u,  3u,  5u,  6u,  7u, 8u,
                                 9u, 16u, 17u, 18u, 19u, 20u, 31u};
    const uint32_t expected[] = {0xa0000000u, 0xa0000007u, 0x01000000u, 0xa1000007u, 7u,
                                 0x01000000u, 0x01000007u, 0x20001000u, 0x20002000u, 1u,
                                 0x80u,       0x80u,       1u,          3u,          0u};
    for (size_t index = 0u; index < sizeof(selectors); index++) {
        execute(fixture, 0xf3efu, (uint16_t)(0x8000u | selectors[index]));
        TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == expected[index]);
    }

    fixture->cpu->registers[1] = 0xa00000f7u;
    const uint8_t write_selectors[] = {0u, 8u, 9u, 16u, 17u, 18u, 19u, 20u};
    for (size_t index = 0u; index < sizeof(write_selectors); index++)
        execute(fixture, 0xf381u, (uint16_t)(0x8800u | write_selectors[index]));
    TEST_EXPECT(fixture->state, fixture->cpu->msp == 0xa00000f4u);
    TEST_EXPECT(fixture->state, fixture->cpu->psp == 0xa00000f4u);
    TEST_EXPECT(fixture->state, fixture->cpu->primask == 1u);
    TEST_EXPECT(fixture->state, fixture->cpu->basepri == 0xf0u);
    TEST_EXPECT(fixture->state, fixture->cpu->faultmask == 1u);
    TEST_EXPECT(fixture->state, fixture->cpu->control == 7u);

    fixture->cpu->control = 0u;
    fixture->cpu->basepri = 0xf0u;
    fixture->cpu->registers[1] = 0x80u;
    execute(fixture, 0xf381u, 0x8812u);
    TEST_EXPECT(fixture->state, fixture->cpu->basepri == 0x80u);

    fixture->cpu->xpsr = CORTEX_M4_XPSR_T;
    fixture->cpu->control = CORTEX_M4_CONTROL_NPRIV;
    fixture->cpu->registers[1] = 0u;
    execute(fixture, 0xf381u, 0x8810u);
    TEST_EXPECT(fixture->state, fixture->cpu->primask == 1u);
    fixture->cpu->control = 0u;
}

static void test_immediates(Fixture* fixture) {
    execute(fixture, 0xf240u, 0x0101u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[1] == 1u);
    execute(fixture, 0xf2c1u, 0x2100u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[1] == 0x12000001u);

    const uint16_t immediates[] = {0x0012u, 0x1012u, 0x2012u, 0x3012u, 0x4001u};
    const uint32_t expected[] = {0x00000012u, 0x00120012u, 0x12001200u, 0x12121212u,
                                 0x81000000u};
    for (size_t index = 0u; index < sizeof(immediates) / sizeof(immediates[0]); index++) {
        fixture->cpu->registers[1] = 0u;
        execute(fixture, 0xf041u, immediates[index]);
        TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == expected[index]);
    }

    const uint8_t operations[] = {0u, 1u, 3u, 10u, 11u, 13u, 14u};
    for (size_t index = 0u; index < sizeof(operations); index++) {
        fixture->cpu->registers[1] = 5u;
        fixture->cpu->xpsr |= CORTEX_M4_XPSR_C;
        execute(fixture, (uint16_t)(0xf001u | ((uint16_t)operations[index] << 5u)), 1u);
        TEST_EXPECT(fixture->state, fixture->cpu->registers[0] != 0xdeadbeefu);
    }

    fixture->cpu->registers[1] = 0x121u;
    execute(fixture, 0xf041u, 0x0f00u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[15] == 0x120u);
}

static void test_arithmetic(Fixture* fixture) {
    fixture->cpu->registers[1] = 10u;
    fixture->cpu->registers[2] = 3u;
    execute(fixture, 0xfb91u, 0xf0f2u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 3u);
    execute(fixture, 0xfbb1u, 0xf0f2u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 3u);

    fixture->cpu->ccr = 0u;
    fixture->cpu->registers[2] = 0u;
    execute(fixture, 0xfbb1u, 0xf0f2u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 0u);
    fixture->cpu->ccr = 1u << 4u;
    fixture->cpu->cfsr = 0u;
    execute(fixture, 0xfbb1u, 0xf0f2u);
    TEST_EXPECT(fixture->state, (fixture->cpu->cfsr & (1u << 25u)) != 0u);
    fixture->cpu->system_pending = 0u;
    fixture->cpu->ccr = 0u;

    fixture->cpu->registers[1] = 0x80000000u;
    fixture->cpu->registers[2] = UINT32_MAX;
    execute(fixture, 0xfb91u, 0xf0f2u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 0x80000000u);

    fixture->cpu->registers[1] = 7u;
    fixture->cpu->registers[2] = 6u;
    fixture->cpu->registers[3] = 5u;
    execute(fixture, 0xfb01u, 0x3002u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 47u);

    fixture->cpu->registers[1] = UINT32_MAX;
    fixture->cpu->registers[2] = 2u;
    execute(fixture, 0xfb81u, 0x0302u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 0xfffffffeu);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[3] == UINT32_MAX);
    execute(fixture, 0xfba1u, 0x0302u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 0xfffffffeu);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[3] == 1u);

    fixture->cpu->registers[0] = 1u;
    fixture->cpu->registers[3] = 0u;
    execute(fixture, 0xfbc1u, 0x0302u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == UINT32_MAX);
    execute(fixture, 0xfbe1u, 0x0302u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 0xfffffffdu);

    fixture->cpu->registers[1] = 0x12345678u;
    execute(fixture, 0xf3c1u, 0x001fu);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 0x12345678u);
    fixture->cpu->registers[1] = 0x80u;
    execute(fixture, 0xf341u, 0x0007u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 0xffffff80u);
}

static void test_memory_and_exclusive(Fixture* fixture) {
    const uint32_t address = 0x20000100u;
    uint16_t halfword = 0x1234u;
    uint8_t byte = 0x5au;
    TEST_EXPECT(fixture->state,
                kinetis_k22_write(fixture->device, address, &halfword, sizeof(halfword)));
    TEST_EXPECT(fixture->state,
                kinetis_k22_write(fixture->device, address + 2u, &byte, sizeof(byte)));
    fixture->cpu->registers[1] = address + 2u;
    execute(fixture, 0xe8d1u, 0x0f4fu);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == byte);
    fixture->cpu->registers[2] = 0xa5u;
    execute(fixture, 0xe8c1u, 0x2f40u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 0u);
    TEST_EXPECT(fixture->state,
                kinetis_k22_read(fixture->device, address + 2u, &byte, sizeof(byte)));
    TEST_EXPECT(fixture->state, byte == 0xa5u);

    const uint32_t branch = 0x121u;
    TEST_EXPECT(fixture->state,
                kinetis_k22_load(fixture->device, 0x104u, &branch, sizeof(branch)));
    execute(fixture, 0xf8dfu, 0xf000u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[15] == 0x120u);

    fixture->cpu->registers[1] = address;
    fixture->cpu->registers[0] = 0x11223344u;
    execute(fixture, 0xf8c1u, 0x0000u);
    uint32_t word = 0u;
    TEST_EXPECT(fixture->state,
                kinetis_k22_read(fixture->device, address, &word, sizeof(word)));
    TEST_EXPECT(fixture->state, word == 0x11223344u);
    fixture->cpu->registers[1] = address + 3u;
    execute(fixture, 0xf991u, 0x0000u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 0x11u);

    byte = 0x80u;
    TEST_EXPECT(fixture->state,
                kinetis_k22_write(fixture->device, address + 1u, &byte, sizeof(byte)));
    fixture->cpu->registers[1] = address;
    execute(fixture, 0xf911u, 0x0701u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 0xffffff80u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[1] == address + 1u);
    execute(fixture, 0xf891u, 0xf000u);

    fixture->cpu->registers[0] = 0x01020304u;
    fixture->cpu->registers[2] = 0x11121314u;
    fixture->cpu->registers[4] = address + 0x20u;
    execute(fixture, 0xe8a4u, 0x0005u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[4] == address + 0x28u);
    fixture->cpu->registers[0] = 0u;
    fixture->cpu->registers[2] = 0u;
    fixture->cpu->registers[4] = address + 0x20u;
    execute(fixture, 0xe8b4u, 0x0005u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[0] == 0x01020304u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[2] == 0x11121314u);
    execute(fixture, 0xf3bfu, 0x8f2fu);
    TEST_EXPECT(fixture->state, !fixture->cpu->exclusive_valid);

    fixture->cpu->registers[4] = address + 0x20u;
    fixture->cpu->ici_valid = true;
    fixture->cpu->ici_register = 2u;
    fixture->cpu->ici_address = address + 0x24u;
    fixture->cpu->registers[2] = 0u;
    execute(fixture, 0xe8b4u, 0x0005u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[2] == 0x11121314u);
    TEST_EXPECT(fixture->state, !fixture->cpu->ici_valid);

    fixture->cpu->registers[4] = 0x60000000u;
    fixture->cpu->cfsr = 0u;
    execute(fixture, 0xe8b4u, 0x0001u);
    TEST_EXPECT(fixture->state, (fixture->cpu->cfsr & (1u << 9u)) != 0u);
    fixture->cpu->system_pending = 0u;

    fixture->cpu->registers[1] = address;
    const uint32_t target = 0x181u;
    TEST_EXPECT(fixture->state,
                kinetis_k22_write(fixture->device, address + 4u, &target, sizeof(target)));
    execute(fixture, 0xf851u, 0xf704u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[15] == 0x180u);
    TEST_EXPECT(fixture->state, fixture->cpu->registers[1] == address + 4u);

    fixture->cpu->registers[1] = 0x60000000u;
    fixture->cpu->cfsr = 0u;
    execute(fixture, 0xf851u, 0x0700u);
    TEST_EXPECT(fixture->state, (fixture->cpu->cfsr & (1u << 9u)) != 0u);
    fixture->cpu->system_pending = 0u;
}

int main(void) {
    TestState state = {0};
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096u;
    configuration.sram_size = 65536u;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(&state, device != NULL);
    const uint32_t vectors[2] = {0x20001000u, 0x101u};
    TEST_EXPECT(&state, kinetis_k22_load(device, 0u, vectors, sizeof(vectors)));
    TEST_EXPECT(&state, kinetis_k22_reset(device));
    Fixture fixture = {&state, device, kinetis_k22_cpu(device)};
    test_branches(&fixture);
    test_special_registers(&fixture);
    test_immediates(&fixture);
    test_arithmetic(&fixture);
    test_memory_and_exclusive(&fixture);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
