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
    expect(fixture->state, kinetis_k22_load(fixture->device, 0x100u, &opcode, sizeof(opcode)),
           "kinetis_k22_load(fixture->device, 0x100u, &opcode, sizeof(opcode))");
    cortex_m4_set_register(fixture->cpu, 15u, 0x100u);
    const CortexM4Result result = cortex_m4_step(fixture->cpu);
    expect(fixture->state, result.stop == CORTEX_M4_STOP_RUNNING,
           "result.stop == CORTEX_M4_STOP_RUNNING");
}

static uint32_t reg(const Fixture* fixture, uint8_t index) {
    return cortex_m4_get_register(fixture->cpu, index);
}

static void test_immediate_and_high_register(Fixture* fixture) {
    cortex_m4_set_register(fixture->cpu, 0u, 9u);
    execute(fixture, 0x3804u);
    expect(fixture->state, reg(fixture, 0u) == 5u, "reg(fixture, 0u) == 5u");

    cortex_m4_set_register(fixture->cpu, 0u, 7u);
    cortex_m4_set_register(fixture->cpu, 8u, 5u);
    execute(fixture, 0x4480u);
    expect(fixture->state, reg(fixture, 8u) == 12u, "reg(fixture, 8u) == 12u");

    cortex_m4_set_register(fixture->cpu, 0u, 12u);
    execute(fixture, 0x4580u);
    expect(fixture->state, (cortex_m4_get_xpsr(fixture->cpu) & (1u << 30u)) != 0u,
           "(cortex_m4_get_xpsr(fixture->cpu) & (1u << 30u)) != 0u");

    cortex_m4_set_register(fixture->cpu, 0u, 0x12345678u);
    execute(fixture, 0x4680u);
    expect(fixture->state, reg(fixture, 8u) == 0x12345678u, "reg(fixture, 8u) == 0x12345678u");

    cortex_m4_set_register(fixture->cpu, 0u, 0x121u);
    execute(fixture, 0x4700u);
    expect(fixture->state, reg(fixture, 15u) == 0x120u, "reg(fixture, 15u) == 0x120u");

    cortex_m4_set_register(fixture->cpu, 0u, 0x141u);
    execute(fixture, 0x4780u);
    expect(fixture->state, reg(fixture, 14u) == 0x103u, "reg(fixture, 14u) == 0x103u");
    expect(fixture->state, reg(fixture, 15u) == 0x140u, "reg(fixture, 15u) == 0x140u");

    cortex_m4_set_register(fixture->cpu, 0u, 0x21u);
    execute(fixture, 0x4487u);
    expect(fixture->state, reg(fixture, 15u) == 0x124u, "reg(fixture, 15u) == 0x124u");
    cortex_m4_set_register(fixture->cpu, 0u, 0x104u);
    execute(fixture, 0x4587u);
    expect(fixture->state, (cortex_m4_get_xpsr(fixture->cpu) & CORTEX_M4_XPSR_Z) != 0u,
           "(cortex_m4_get_xpsr(fixture->cpu) & CORTEX_M4_XPSR_Z) != 0u");
    cortex_m4_set_register(fixture->cpu, 0u, 0x161u);
    execute(fixture, 0x4687u);
    expect(fixture->state, reg(fixture, 15u) == 0x160u, "reg(fixture, 15u) == 0x160u");

    fixture->cpu->cfsr = 0u;
    cortex_m4_set_register(fixture->cpu, 0u, 0x180u);
    execute(fixture, 0x4700u);
    expect(fixture->state, (fixture->cpu->cfsr & (1u << 17u)) != 0u,
           "(fixture->cpu->cfsr & (1u << 17u)) != 0u");
    fixture->cpu->system_pending = 0u;
}

static void test_stack_and_addresses(Fixture* fixture) {
    const uint32_t stack = 0x20000100u;
    cortex_m4_set_register(fixture->cpu, 13u, stack);
    cortex_m4_set_register(fixture->cpu, 0u, 0xa55ac33cu);
    execute(fixture, 0x9000u);
    cortex_m4_set_register(fixture->cpu, 1u, 0u);
    execute(fixture, 0x9900u);
    expect(fixture->state, reg(fixture, 1u) == 0xa55ac33cu, "reg(fixture, 1u) == 0xa55ac33cu");

    execute(fixture, 0xa001u);
    expect(fixture->state, reg(fixture, 0u) == 0x108u, "reg(fixture, 0u) == 0x108u");
    execute(fixture, 0xa801u);
    expect(fixture->state, reg(fixture, 0u) == stack + 4u, "reg(fixture, 0u) == stack + 4u");

    cortex_m4_set_register(fixture->cpu, 13u, stack);
    execute(fixture, 0xb001u);
    expect(fixture->state, reg(fixture, 13u) == stack + 4u, "reg(fixture, 13u) == stack + 4u");
    execute(fixture, 0xb081u);
    expect(fixture->state, reg(fixture, 13u) == stack, "reg(fixture, 13u) == stack");

    const uint32_t literal = 0x76543210u;
    expect(fixture->state, kinetis_k22_load(fixture->device, 0x104u, &literal, sizeof(literal)),
           "kinetis_k22_load(fixture->device, 0x104u, &literal, sizeof(literal))");
    execute(fixture, 0x4800u);
    expect(fixture->state, reg(fixture, 0u) == literal, "reg(fixture, 0u) == literal");
}

static void test_stack_lifecycle(Fixture* fixture) {
    const uint32_t stack = 0x20000200u;
    cortex_m4_set_register(fixture->cpu, 13u, stack);
    cortex_m4_set_register(fixture->cpu, 0u, 0x11223344u);
    cortex_m4_set_register(fixture->cpu, 1u, 0x55667788u);
    cortex_m4_set_register(fixture->cpu, 14u, 0x181u);
    execute(fixture, 0xb503u);
    expect(fixture->state, reg(fixture, 13u) == stack - 12u, "reg(fixture, 13u) == stack - 12u");
    cortex_m4_set_register(fixture->cpu, 0u, 0u);
    cortex_m4_set_register(fixture->cpu, 1u, 0u);
    execute(fixture, 0xbd03u);
    expect(fixture->state, reg(fixture, 0u) == 0x11223344u, "reg(fixture, 0u) == 0x11223344u");
    expect(fixture->state, reg(fixture, 1u) == 0x55667788u, "reg(fixture, 1u) == 0x55667788u");
    expect(fixture->state, reg(fixture, 13u) == stack, "reg(fixture, 13u) == stack");
    expect(fixture->state, reg(fixture, 15u) == 0x180u, "reg(fixture, 15u) == 0x180u");

    cortex_m4_set_register(fixture->cpu, 13u, stack);
    cortex_m4_set_register(fixture->cpu, 0u, 0xaabbccddu);
    cortex_m4_set_register(fixture->cpu, 1u, 0x01020304u);
    fixture->cpu->ici_valid = true;
    fixture->cpu->ici_register = 1u;
    fixture->cpu->ici_address = stack - 4u;
    execute(fixture, 0xb403u);
    uint32_t stored = 0u;
    expect(fixture->state, kinetis_k22_read(fixture->device, stack - 4u, &stored, sizeof(stored)),
           "kinetis_k22_read(fixture->device, stack - 4u, &stored, sizeof(stored))");
    expect(fixture->state, stored == 0x01020304u, "stored == 0x01020304u");
    expect(fixture->state, !fixture->cpu->ici_valid, "!fixture->cpu->ici_valid");

    stored = 0xcafebabeu;
    expect(fixture->state, kinetis_k22_write(fixture->device, stack - 4u, &stored, sizeof(stored)),
           "kinetis_k22_write(fixture->device, stack - 4u, &stored, sizeof(stored))");
    cortex_m4_set_register(fixture->cpu, 13u, stack - 8u);
    cortex_m4_set_register(fixture->cpu, 1u, 0u);
    fixture->cpu->ici_valid = true;
    fixture->cpu->ici_register = 1u;
    fixture->cpu->ici_address = stack - 4u;
    execute(fixture, 0xbc03u);
    expect(fixture->state, reg(fixture, 1u) == 0xcafebabeu, "reg(fixture, 1u) == 0xcafebabeu");
    expect(fixture->state, !fixture->cpu->ici_valid, "!fixture->cpu->ici_valid");

    cortex_m4_set_register(fixture->cpu, 13u, 0x60000000u);
    fixture->cpu->cfsr = 0u;
    execute(fixture, 0xb401u);
    expect(fixture->state, (fixture->cpu->cfsr & (1u << 9u)) != 0u,
           "(fixture->cpu->cfsr & (1u << 9u)) != 0u");
    fixture->cpu->system_pending = 0u;
    fixture->cpu->cfsr = 0u;
    execute(fixture, 0xbc01u);
    expect(fixture->state, (fixture->cpu->cfsr & (1u << 9u)) != 0u,
           "(fixture->cpu->cfsr & (1u << 9u)) != 0u");
    fixture->cpu->system_pending = 0u;
    fixture->cpu->cfsr = 0u;
    execute(fixture, 0xbd00u);
    expect(fixture->state, (fixture->cpu->cfsr & (1u << 9u)) != 0u,
           "(fixture->cpu->cfsr & (1u << 9u)) != 0u");
    fixture->cpu->system_pending = 0u;
}

static void test_multiple_lifecycle(Fixture* fixture) {
    const uint32_t address = 0x20000300u;
    cortex_m4_set_register(fixture->cpu, 0u, address);
    cortex_m4_set_register(fixture->cpu, 1u, 0x10203040u);
    cortex_m4_set_register(fixture->cpu, 2u, 0x50607080u);
    execute(fixture, 0xc006u);
    expect(fixture->state, reg(fixture, 0u) == address + 8u, "reg(fixture, 0u) == address + 8u");
    cortex_m4_set_register(fixture->cpu, 0u, address);
    cortex_m4_set_register(fixture->cpu, 1u, 0u);
    cortex_m4_set_register(fixture->cpu, 2u, 0u);
    execute(fixture, 0xc806u);
    expect(fixture->state, reg(fixture, 1u) == 0x10203040u, "reg(fixture, 1u) == 0x10203040u");
    expect(fixture->state, reg(fixture, 2u) == 0x50607080u, "reg(fixture, 2u) == 0x50607080u");

    cortex_m4_set_register(fixture->cpu, 0u, 0x60000000u);
    fixture->cpu->cfsr = 0u;
    execute(fixture, 0xc801u);
    expect(fixture->state, (fixture->cpu->cfsr & (1u << 9u)) != 0u,
           "(fixture->cpu->cfsr & (1u << 9u)) != 0u");
    fixture->cpu->system_pending = 0u;
}

static void test_branches_and_service(Fixture* fixture) {
    cortex_m4_set_register(fixture->cpu, 0u, 0u);
    execute(fixture, 0xb100u);
    expect(fixture->state, reg(fixture, 15u) == 0x104u, "reg(fixture, 15u) == 0x104u");
    cortex_m4_set_register(fixture->cpu, 0u, 1u);
    execute(fixture, 0xb100u);
    expect(fixture->state, reg(fixture, 15u) == 0x102u, "reg(fixture, 15u) == 0x102u");
    execute(fixture, 0xb900u);
    expect(fixture->state, reg(fixture, 15u) == 0x104u, "reg(fixture, 15u) == 0x104u");

    cortex_m4_set_xpsr(fixture->cpu, (1u << 24u) | (1u << 30u));
    execute(fixture, 0xd000u);
    expect(fixture->state, reg(fixture, 15u) == 0x104u, "reg(fixture, 15u) == 0x104u");
    cortex_m4_set_xpsr(fixture->cpu, 1u << 24u);
    execute(fixture, 0xd000u);
    expect(fixture->state, reg(fixture, 15u) == 0x102u, "reg(fixture, 15u) == 0x102u");
    execute(fixture, 0xe000u);
    expect(fixture->state, reg(fixture, 15u) == 0x104u, "reg(fixture, 15u) == 0x104u");
}

static void test_control(Fixture* fixture) {
    cortex_m4_set_control(fixture->cpu, 0u);
    execute(fixture, 0xb672u);
    expect(fixture->state, fixture->cpu->primask == 1u, "fixture->cpu->primask == 1u");
    execute(fixture, 0xb662u);
    expect(fixture->state, fixture->cpu->primask == 0u, "fixture->cpu->primask == 0u");

    cortex_m4_set_control(fixture->cpu, 1u);
    execute(fixture, 0xb672u);
    expect(fixture->state, fixture->cpu->primask == 0u, "fixture->cpu->primask == 0u");
    cortex_m4_set_control(fixture->cpu, 0u);

    fixture->cpu->sleeping = false;
    fixture->cpu->event_register = false;
    execute(fixture, 0xbf20u);
    expect(fixture->state, fixture->cpu->sleeping, "fixture->cpu->sleeping");
    fixture->cpu->sleeping = false;
}

int main(void) {
    TestState state = {0};
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096u;
    configuration.sram_size = 65536u;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(&state, device != NULL, "device != NULL");
    const uint32_t vectors[2] = {0x20001000u, 0x00000101u};
    expect(&state, kinetis_k22_load(device, 0u, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0u, vectors, sizeof(vectors))");
    expect(&state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    Fixture fixture = {&state, device, kinetis_k22_cpu(device)};
    test_immediate_and_high_register(&fixture);
    test_stack_and_addresses(&fixture);
    test_stack_lifecycle(&fixture);
    test_multiple_lifecycle(&fixture);
    test_branches_and_service(&fixture);
    test_control(&fixture);
    execute(&fixture, 0xdf00u);
    expect(&state, reg(&fixture, 15u) == 0x102u, "reg(&fixture, 15u) == 0x102u");
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
