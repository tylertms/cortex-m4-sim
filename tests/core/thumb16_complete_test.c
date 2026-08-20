#include "kinetis_k22.h"

#include <stdint.h>

#include "cortex_m4_internal.h"
#include "test.h"

typedef struct {
    TestState* state;
    KinetisK22* device;
    CortexM4* cpu;
} Fixture;

static void execute(Fixture* fixture, uint16_t opcode) {
    TEST_EXPECT(fixture->state,
                kinetis_k22_load(fixture->device, 0x100u, &opcode, sizeof(opcode)));
    cortex_m4_set_register(fixture->cpu, 15u, 0x100u);
    const CortexM4Result result = cortex_m4_step(fixture->cpu);
    TEST_EXPECT(fixture->state, result.stop == CORTEX_M4_STOP_RUNNING);
}

static uint32_t reg(const Fixture* fixture, uint8_t index) {
    return cortex_m4_get_register(fixture->cpu, index);
}

static void test_immediate_and_high_register(Fixture* fixture) {
    cortex_m4_set_register(fixture->cpu, 0u, 9u);
    execute(fixture, 0x3804u);
    TEST_EXPECT(fixture->state, reg(fixture, 0u) == 5u);

    cortex_m4_set_register(fixture->cpu, 0u, 7u);
    cortex_m4_set_register(fixture->cpu, 8u, 5u);
    execute(fixture, 0x4480u);
    TEST_EXPECT(fixture->state, reg(fixture, 8u) == 12u);

    cortex_m4_set_register(fixture->cpu, 0u, 12u);
    execute(fixture, 0x4580u);
    TEST_EXPECT(fixture->state, (cortex_m4_get_xpsr(fixture->cpu) & (1u << 30u)) != 0u);

    cortex_m4_set_register(fixture->cpu, 0u, 0x12345678u);
    execute(fixture, 0x4680u);
    TEST_EXPECT(fixture->state, reg(fixture, 8u) == 0x12345678u);

    cortex_m4_set_register(fixture->cpu, 0u, 0x121u);
    execute(fixture, 0x4700u);
    TEST_EXPECT(fixture->state, reg(fixture, 15u) == 0x120u);

    cortex_m4_set_register(fixture->cpu, 0u, 0x141u);
    execute(fixture, 0x4780u);
    TEST_EXPECT(fixture->state, reg(fixture, 14u) == 0x103u);
    TEST_EXPECT(fixture->state, reg(fixture, 15u) == 0x140u);

    cortex_m4_set_register(fixture->cpu, 0u, 0x21u);
    execute(fixture, 0x4487u);
    TEST_EXPECT(fixture->state, reg(fixture, 15u) == 0x124u);
    cortex_m4_set_register(fixture->cpu, 0u, 0x104u);
    execute(fixture, 0x4587u);
    TEST_EXPECT(fixture->state,
                (cortex_m4_get_xpsr(fixture->cpu) & CORTEX_M4_XPSR_Z) != 0u);
    cortex_m4_set_register(fixture->cpu, 0u, 0x161u);
    execute(fixture, 0x4687u);
    TEST_EXPECT(fixture->state, reg(fixture, 15u) == 0x160u);

    fixture->cpu->cfsr = 0u;
    cortex_m4_set_register(fixture->cpu, 0u, 0x180u);
    execute(fixture, 0x4700u);
    TEST_EXPECT(fixture->state, (fixture->cpu->cfsr & (1u << 17u)) != 0u);
    fixture->cpu->system_pending = 0u;
}

static void test_stack_and_addresses(Fixture* fixture) {
    const uint32_t stack = 0x20000100u;
    cortex_m4_set_register(fixture->cpu, 13u, stack);
    cortex_m4_set_register(fixture->cpu, 0u, 0xa55ac33cu);
    execute(fixture, 0x9000u);
    cortex_m4_set_register(fixture->cpu, 1u, 0u);
    execute(fixture, 0x9900u);
    TEST_EXPECT(fixture->state, reg(fixture, 1u) == 0xa55ac33cu);

    execute(fixture, 0xa001u);
    TEST_EXPECT(fixture->state, reg(fixture, 0u) == 0x108u);
    execute(fixture, 0xa801u);
    TEST_EXPECT(fixture->state, reg(fixture, 0u) == stack + 4u);

    cortex_m4_set_register(fixture->cpu, 13u, stack);
    execute(fixture, 0xb001u);
    TEST_EXPECT(fixture->state, reg(fixture, 13u) == stack + 4u);
    execute(fixture, 0xb081u);
    TEST_EXPECT(fixture->state, reg(fixture, 13u) == stack);

    const uint32_t literal = 0x76543210u;
    TEST_EXPECT(fixture->state,
                kinetis_k22_load(fixture->device, 0x104u, &literal, sizeof(literal)));
    execute(fixture, 0x4800u);
    TEST_EXPECT(fixture->state, reg(fixture, 0u) == literal);
}

static void test_stack_lifecycle(Fixture* fixture) {
    const uint32_t stack = 0x20000200u;
    cortex_m4_set_register(fixture->cpu, 13u, stack);
    cortex_m4_set_register(fixture->cpu, 0u, 0x11223344u);
    cortex_m4_set_register(fixture->cpu, 1u, 0x55667788u);
    cortex_m4_set_register(fixture->cpu, 14u, 0x181u);
    execute(fixture, 0xb503u);
    TEST_EXPECT(fixture->state, reg(fixture, 13u) == stack - 12u);
    cortex_m4_set_register(fixture->cpu, 0u, 0u);
    cortex_m4_set_register(fixture->cpu, 1u, 0u);
    execute(fixture, 0xbd03u);
    TEST_EXPECT(fixture->state, reg(fixture, 0u) == 0x11223344u);
    TEST_EXPECT(fixture->state, reg(fixture, 1u) == 0x55667788u);
    TEST_EXPECT(fixture->state, reg(fixture, 13u) == stack);
    TEST_EXPECT(fixture->state, reg(fixture, 15u) == 0x180u);

    cortex_m4_set_register(fixture->cpu, 13u, stack);
    cortex_m4_set_register(fixture->cpu, 0u, 0xaabbccddu);
    cortex_m4_set_register(fixture->cpu, 1u, 0x01020304u);
    fixture->cpu->ici_valid = true;
    fixture->cpu->ici_register = 1u;
    fixture->cpu->ici_address = stack - 4u;
    execute(fixture, 0xb403u);
    uint32_t stored = 0u;
    TEST_EXPECT(fixture->state,
                kinetis_k22_read(fixture->device, stack - 4u, &stored, sizeof(stored)));
    TEST_EXPECT(fixture->state, stored == 0x01020304u);
    TEST_EXPECT(fixture->state, !fixture->cpu->ici_valid);

    stored = 0xcafebabeu;
    TEST_EXPECT(fixture->state,
                kinetis_k22_write(fixture->device, stack - 4u, &stored, sizeof(stored)));
    cortex_m4_set_register(fixture->cpu, 13u, stack - 8u);
    cortex_m4_set_register(fixture->cpu, 1u, 0u);
    fixture->cpu->ici_valid = true;
    fixture->cpu->ici_register = 1u;
    fixture->cpu->ici_address = stack - 4u;
    execute(fixture, 0xbc03u);
    TEST_EXPECT(fixture->state, reg(fixture, 1u) == 0xcafebabeu);
    TEST_EXPECT(fixture->state, !fixture->cpu->ici_valid);

    cortex_m4_set_register(fixture->cpu, 13u, 0x60000000u);
    fixture->cpu->cfsr = 0u;
    execute(fixture, 0xb401u);
    TEST_EXPECT(fixture->state, (fixture->cpu->cfsr & (1u << 9u)) != 0u);
    fixture->cpu->system_pending = 0u;
    fixture->cpu->cfsr = 0u;
    execute(fixture, 0xbc01u);
    TEST_EXPECT(fixture->state, (fixture->cpu->cfsr & (1u << 9u)) != 0u);
    fixture->cpu->system_pending = 0u;
    fixture->cpu->cfsr = 0u;
    execute(fixture, 0xbd00u);
    TEST_EXPECT(fixture->state, (fixture->cpu->cfsr & (1u << 9u)) != 0u);
    fixture->cpu->system_pending = 0u;
}

static void test_multiple_lifecycle(Fixture* fixture) {
    const uint32_t address = 0x20000300u;
    cortex_m4_set_register(fixture->cpu, 0u, address);
    cortex_m4_set_register(fixture->cpu, 1u, 0x10203040u);
    cortex_m4_set_register(fixture->cpu, 2u, 0x50607080u);
    execute(fixture, 0xc006u);
    TEST_EXPECT(fixture->state, reg(fixture, 0u) == address + 8u);
    cortex_m4_set_register(fixture->cpu, 0u, address);
    cortex_m4_set_register(fixture->cpu, 1u, 0u);
    cortex_m4_set_register(fixture->cpu, 2u, 0u);
    execute(fixture, 0xc806u);
    TEST_EXPECT(fixture->state, reg(fixture, 1u) == 0x10203040u);
    TEST_EXPECT(fixture->state, reg(fixture, 2u) == 0x50607080u);

    cortex_m4_set_register(fixture->cpu, 0u, 0x60000000u);
    fixture->cpu->cfsr = 0u;
    execute(fixture, 0xc801u);
    TEST_EXPECT(fixture->state, (fixture->cpu->cfsr & (1u << 9u)) != 0u);
    fixture->cpu->system_pending = 0u;
}

static void test_branches_and_service(Fixture* fixture) {
    cortex_m4_set_register(fixture->cpu, 0u, 0u);
    execute(fixture, 0xb100u);
    TEST_EXPECT(fixture->state, reg(fixture, 15u) == 0x104u);
    cortex_m4_set_register(fixture->cpu, 0u, 1u);
    execute(fixture, 0xb100u);
    TEST_EXPECT(fixture->state, reg(fixture, 15u) == 0x102u);
    execute(fixture, 0xb900u);
    TEST_EXPECT(fixture->state, reg(fixture, 15u) == 0x104u);

    cortex_m4_set_xpsr(fixture->cpu, (1u << 24u) | (1u << 30u));
    execute(fixture, 0xd000u);
    TEST_EXPECT(fixture->state, reg(fixture, 15u) == 0x104u);
    cortex_m4_set_xpsr(fixture->cpu, 1u << 24u);
    execute(fixture, 0xd000u);
    TEST_EXPECT(fixture->state, reg(fixture, 15u) == 0x102u);
    execute(fixture, 0xe000u);
    TEST_EXPECT(fixture->state, reg(fixture, 15u) == 0x104u);
}

static void test_control(Fixture* fixture) {
    cortex_m4_set_control(fixture->cpu, 0u);
    execute(fixture, 0xb672u);
    TEST_EXPECT(fixture->state, fixture->cpu->primask == 1u);
    execute(fixture, 0xb662u);
    TEST_EXPECT(fixture->state, fixture->cpu->primask == 0u);

    cortex_m4_set_control(fixture->cpu, 1u);
    execute(fixture, 0xb672u);
    TEST_EXPECT(fixture->state, fixture->cpu->primask == 0u);
    cortex_m4_set_control(fixture->cpu, 0u);

    fixture->cpu->sleeping = false;
    fixture->cpu->event_register = false;
    execute(fixture, 0xbf20u);
    TEST_EXPECT(fixture->state, fixture->cpu->sleeping);
    fixture->cpu->sleeping = false;
}

int main(void) {
    TestState state = {0};
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096u;
    configuration.sram_size = 65536u;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(&state, device != NULL);
    const uint32_t vectors[2] = {0x20001000u, 0x00000101u};
    TEST_EXPECT(&state, kinetis_k22_load(device, 0u, vectors, sizeof(vectors)));
    TEST_EXPECT(&state, kinetis_k22_reset(device));
    Fixture fixture = {&state, device, kinetis_k22_cpu(device)};
    test_immediate_and_high_register(&fixture);
    test_stack_and_addresses(&fixture);
    test_stack_lifecycle(&fixture);
    test_multiple_lifecycle(&fixture);
    test_branches_and_service(&fixture);
    test_control(&fixture);
    execute(&fixture, 0xdf00u);
    TEST_EXPECT(&state, reg(&fixture, 15u) == 0x102u);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
