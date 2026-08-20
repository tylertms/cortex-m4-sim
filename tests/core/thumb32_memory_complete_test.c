#include "kinetis_k22.h"

#include <stdint.h>
#include <string.h>

#include "cortex_m4_internal.h"
#include "test.h"

enum { SCB_CCR = 0xe000ed14u };

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
        cortex_m4_run(kinetis_k22_cpu(device), (CortexM4RunLimits){2, 32});
    TEST_EXPECT(state, result.stop == CORTEX_M4_STOP_BREAKPOINT);
}

static uint32_t read_word(TestState* state, KinetisK22* device, uint32_t address) {
    uint32_t value = 0;
    TEST_EXPECT(state, kinetis_k22_read(device, address, &value, sizeof(value)));
    return value;
}

static void test_wide_add_subtract(TestState* state, KinetisK22* device) {
    CortexM4* cpu = kinetis_k22_cpu(device);
    load_instruction(state, device, 0xf603u, 0x22bcu);
    cortex_m4_set_register(cpu, 3, 0x12340000u);
    execute(state, device);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 2) == 0x12340abcu);

    load_instruction(state, device, 0xf2a5u, 0x3421u);
    cortex_m4_set_register(cpu, 5, 0x1000u);
    execute(state, device);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 4) == 0xcdfu);

    load_instruction(state, device, 0xf20fu, 0x0600u);
    execute(state, device);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 6) == 0x104u);
}

static void test_doubleword(TestState* state, KinetisK22* device) {
    CortexM4* cpu = kinetis_k22_cpu(device);
    const uint32_t values[2] = {0x11223344u, 0xa55ac33cu};
    load_instruction(state, device, 0xe9d2u, 0x0105u);
    TEST_EXPECT(state, kinetis_k22_write(device, 0x20000034u, values, sizeof(values)));
    cortex_m4_set_register(cpu, 2, 0x20000020u);
    execute(state, device);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == values[0]);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 1) == values[1]);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 2) == 0x20000020u);

    load_instruction(state, device, 0xe8e8u, 0x6704u);
    cortex_m4_set_register(cpu, 6, 0x89abcdefu);
    cortex_m4_set_register(cpu, 7, 0x76543210u);
    cortex_m4_set_register(cpu, 8, 0x20000040u);
    execute(state, device);
    TEST_EXPECT(state, read_word(state, device, 0x20000040u) == 0x89abcdefu);
    TEST_EXPECT(state, read_word(state, device, 0x20000044u) == 0x76543210u);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 8) == 0x20000050u);

    load_instruction(state, device, 0xe975u, 0x3403u);
    TEST_EXPECT(state, kinetis_k22_write(device, 0x20000054u, values, sizeof(values)));
    cortex_m4_set_register(cpu, 5, 0x20000060u);
    execute(state, device);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 3) == values[0]);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 4) == values[1]);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 5) == 0x20000054u);

    load_instruction(state, device, 0xe8e8u, 0x6704u);
    cortex_m4_set_register(cpu, 6, 0x89abcdefu);
    cortex_m4_set_register(cpu, 7, 0x76543210u);
    cortex_m4_set_register(cpu, 8, 0x60000000u);
    TEST_EXPECT(state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 9)) != 0u);

    load_instruction(state, device, 0xe9d2u, 0x0100u);
    cortex_m4_set_register(cpu, 2, 0x60000000u);
    TEST_EXPECT(state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 9)) != 0u);
}

static void test_decrement_before_multiple(TestState* state, KinetisK22* device) {
    CortexM4* cpu = kinetis_k22_cpu(device);
    load_instruction(state, device, 0xe92bu, 0x10f0u);
    cortex_m4_set_register(cpu, 4, 0x44444444u);
    cortex_m4_set_register(cpu, 5, 0x55555555u);
    cortex_m4_set_register(cpu, 6, 0x66666666u);
    cortex_m4_set_register(cpu, 7, 0x77777777u);
    cortex_m4_set_register(cpu, 11, 0x20000080u);
    cortex_m4_set_register(cpu, 12, 0xccccccccu);
    execute(state, device);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 11) == 0x2000006cu);
    TEST_EXPECT(state, read_word(state, device, 0x2000006cu) == 0x44444444u);
    TEST_EXPECT(state, read_word(state, device, 0x20000070u) == 0x55555555u);
    TEST_EXPECT(state, read_word(state, device, 0x20000074u) == 0x66666666u);
    TEST_EXPECT(state, read_word(state, device, 0x20000078u) == 0x77777777u);
    TEST_EXPECT(state, read_word(state, device, 0x2000007cu) == 0xccccccccu);

    const uint32_t values[5] = {1, 2, 3, 4, 10};
    load_instruction(state, device, 0xe939u, 0x040fu);
    TEST_EXPECT(state, kinetis_k22_write(device, 0x2000008cu, values, sizeof(values)));
    cortex_m4_set_register(cpu, 9, 0x200000a0u);
    execute(state, device);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == 1);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 1) == 2);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 2) == 3);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 3) == 4);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 10) == 10);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 9) == 0x2000008cu);

    const uint32_t resumed_values[3] = {0x22222222u, 0x33333333u, 0xaaaaaaaa};
    load_instruction(state, device, 0xe939u, 0x040fu);
    TEST_EXPECT(state, kinetis_k22_write(device, 0x20000094u, resumed_values,
                                         sizeof(resumed_values)));
    cortex_m4_set_register(cpu, 0, 0x10101010u);
    cortex_m4_set_register(cpu, 1, 0x11111111u);
    cortex_m4_set_register(cpu, 9, 0x200000a0u);
    cpu->ici_valid = true;
    cpu->ici_register = 2u;
    cpu->ici_address = 0x20000094u;
    execute(state, device);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == 0x10101010u);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 1) == 0x11111111u);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 2) == 0x22222222u);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 3) == 0x33333333u);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 10) == 0xaaaaaaaau);
    TEST_EXPECT(state, !cpu->ici_valid);
}

static void test_register_offset(TestState* state, KinetisK22* device) {
    CortexM4* cpu = kinetis_k22_cpu(device);
    const uint32_t word = 0x81223344u;
    load_instruction(state, device, 0xf851u, 0x0022u);
    TEST_EXPECT(state, kinetis_k22_write(device, 0x200000b0u, &word, sizeof(word)));
    cortex_m4_set_register(cpu, 1, 0x200000a0u);
    cortex_m4_set_register(cpu, 2, 4);
    execute(state, device);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == word);

    load_instruction(state, device, 0xf91au, 0x900bu);
    TEST_EXPECT(state, kinetis_k22_write(device, 0x200000b0u, &word, sizeof(word)));
    cortex_m4_set_register(cpu, 10, 0x200000a8u);
    cortex_m4_set_register(cpu, 11, 0xbu);
    execute(state, device);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 9) == 0xffffff81u);

    load_instruction(state, device, 0xf844u, 0x3025u);
    cortex_m4_set_register(cpu, 3, 0xdeadbeefu);
    cortex_m4_set_register(cpu, 4, 0x200000c0u);
    cortex_m4_set_register(cpu, 5, 3);
    execute(state, device);
    TEST_EXPECT(state, read_word(state, device, 0x200000ccu) == 0xdeadbeefu);

    const uint32_t branch = 0x00000141u;
    load_instruction(state, device, 0xf852u, 0xf023u);
    TEST_EXPECT(state, kinetis_k22_write(device, 0x200000ecu, &branch, sizeof(branch)));
    cortex_m4_set_register(cpu, 2, 0x200000e0u);
    cortex_m4_set_register(cpu, 3, 3u);
    TEST_EXPECT(state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 15) == 0x140u);
}

static void test_unprivileged(TestState* state, KinetisK22* device) {
    CortexM4* cpu = kinetis_k22_cpu(device);
    const uint32_t value = 0x87654321u;
    load_instruction(state, device, 0xf851u, 0x0e0cu);
    TEST_EXPECT(state, kinetis_k22_write(device, 0x200000dcu, &value, sizeof(value)));
    cortex_m4_set_register(cpu, 1, 0x200000d0u);
    execute(state, device);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == value);

    load_instruction(state, device, 0xf84bu, 0xae0cu);
    cortex_m4_set_register(cpu, 10, 0x5aa55aa5u);
    cortex_m4_set_register(cpu, 11, 0x200000d0u);
    execute(state, device);
    TEST_EXPECT(state, read_word(state, device, 0x200000dcu) == 0x5aa55aa5u);

    const uint32_t signed_values = 0x00008081u;
    load_instruction(state, device, 0xf911u, 0x0e00u);
    TEST_EXPECT(state, kinetis_k22_write(device, 0x200000f0u, &signed_values,
                                         sizeof(signed_values)));
    cortex_m4_set_register(cpu, 1, 0x200000f0u);
    execute(state, device);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == 0xffffff81u);

    load_instruction(state, device, 0xf931u, 0x0e00u);
    TEST_EXPECT(state, kinetis_k22_write(device, 0x200000f0u, &signed_values,
                                         sizeof(signed_values)));
    cortex_m4_set_register(cpu, 1, 0x200000f0u);
    execute(state, device);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == 0xffff8081u);

    uint32_t byte_value = 0;
    TEST_EXPECT(state, !cortex_m4_read_memory(cpu, 0xe000ed00u, 1u, &byte_value));
    load_instruction(state, device, 0xf851u, 0x0e0cu);
    cortex_m4_set_register(cpu, 0, 0xa5a5a5a5u);
    cortex_m4_set_register(cpu, 1, 0xe000ecf4u);
    TEST_EXPECT(state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == 0xa5a5a5a5u);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 9)) != 0);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 16)) == 0);

    load_instruction(state, device, 0xf84bu, 0xae0cu);
    cortex_m4_set_register(cpu, 10, 0x5aa55aa5u);
    cortex_m4_set_register(cpu, 11, 0xe000ecf4u);
    TEST_EXPECT(state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 9)) != 0);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 16)) == 0);

    load_instruction(state, device, 0x6808u, 0xbe00u);
    cortex_m4_set_control(cpu, 1u);
    cortex_m4_set_register(cpu, 0, 0xa5a5a5a5u);
    cortex_m4_set_register(cpu, 1, 0xe000ed00u);
    TEST_EXPECT(state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == 0xa5a5a5a5u);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 9)) != 0);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 16)) == 0);

    load_instruction(state, device, 0x6008u, 0xbe00u);
    cortex_m4_set_register(cpu, 0, 0x5aa55aa5u);
    cortex_m4_set_register(cpu, 1, 0xe000ecf4u);
    TEST_EXPECT(state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 9)) != 0);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 16)) == 0);
}

static void test_alignment_faults(TestState* state, KinetisK22* device) {
    CortexM4* cpu = kinetis_k22_cpu(device);
    load_instruction(state, device, 0xf851u, 0x0022u);
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SCB_CCR, 4, 0x208u));
    cortex_m4_set_register(cpu, 1, 0x20000001u);
    cortex_m4_set_register(cpu, 2, 0);
    TEST_EXPECT(state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 24)) != 0);

    load_instruction(state, device, 0xe9d2u, 0x0100u);
    cortex_m4_set_register(cpu, 2, 0x20000002u);
    TEST_EXPECT(state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 24)) != 0);
}

typedef struct {
    uint8_t memory[1024];
    CortexM4Access last_access;
    uint32_t last_address;
} TrackingBus;

static bool tracking_read(void* context, uint32_t address, uint8_t size,
                          CortexM4Access access, uint32_t* value) {
    TrackingBus* bus = context;
    if ((uint64_t)address + size > sizeof(bus->memory)) {
        return false;
    }
    *value = 0;
    memcpy(value, bus->memory + address, size);
    if (access == CORTEX_M4_ACCESS_UNPRIVILEGED_DATA) {
        bus->last_access = access;
        bus->last_address = address;
    }
    return true;
}

static bool tracking_write(void* context, uint32_t address, uint8_t size,
                           CortexM4Access access, uint32_t value) {
    TrackingBus* bus = context;
    if ((uint64_t)address + size > sizeof(bus->memory)) {
        return false;
    }
    memcpy(bus->memory + address, &value, size);
    bus->last_access = access;
    bus->last_address = address;
    return true;
}

static void test_multiple_read_failure(TestState* state) {
    TrackingBus bus = {0};
    const uint32_t vectors[2] = {0x300u, 0x101u};
    const uint16_t program[] = {0xe939u, 0x0003u};
    memcpy(bus.memory, vectors, sizeof(vectors));
    memcpy(bus.memory + 0x100u, program, sizeof(program));
    CortexM4* cpu =
        cortex_m4_create((CortexM4Bus){&bus, tracking_read, tracking_write, NULL, NULL});
    TEST_EXPECT(state, cpu != NULL);
    TEST_EXPECT(state, cortex_m4_reset(cpu, 0u));
    cortex_m4_set_register(cpu, 9u, 0x408u);
    TEST_EXPECT(state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 9)) != 0u);
    cortex_m4_destroy(cpu);
}

static void test_unprivileged_access_type(TestState* state) {
    TrackingBus bus = {0};
    const uint32_t vectors[2] = {0x300u, 0x101u};
    const uint16_t program[] = {0xf851u, 0x0e0cu, 0xbe00u};
    const uint32_t value = 0x13579bdfu;
    memcpy(bus.memory, vectors, sizeof(vectors));
    memcpy(bus.memory + 0x100, program, sizeof(program));
    memcpy(bus.memory + 0x20c, &value, sizeof(value));
    CortexM4* cpu =
        cortex_m4_create((CortexM4Bus){&bus, tracking_read, tracking_write, NULL, NULL});
    TEST_EXPECT(state, cpu != NULL);
    TEST_EXPECT(state, cortex_m4_reset(cpu, 0));
    TEST_CONNECT_DEBUGGER(state, cpu);
    cortex_m4_set_register(cpu, 1, 0x200u);
    const CortexM4Result result = cortex_m4_run(cpu, (CortexM4RunLimits){2, 32});
    TEST_EXPECT(state, result.stop == CORTEX_M4_STOP_BREAKPOINT);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == value);
    TEST_EXPECT(state, bus.last_access == CORTEX_M4_ACCESS_UNPRIVILEGED_DATA);
    TEST_EXPECT(state, bus.last_address == 0x20cu);
    cortex_m4_destroy(cpu);
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = create_device(&state);
    test_wide_add_subtract(&state, device);
    test_doubleword(&state, device);
    test_decrement_before_multiple(&state, device);
    test_register_offset(&state, device);
    test_unprivileged(&state, device);
    test_alignment_faults(&state, device);
    kinetis_k22_destroy(device);
    test_multiple_read_failure(&state);
    test_unprivileged_access_type(&state);
    return test_finish(&state);
}
