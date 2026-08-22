#include "architecture/cortex_m4/internal.h"

#include <string.h>

#include "test.h"

enum {
    ITM_BASE = 0xe0000000u,
    DWT_BASE = 0xe0001000u,
    FPB_BASE = 0xe0002000u,
    DCRSR = 0xe000edf4u,
    DCRDR = 0xe000edf8u,
    DEMCR = 0xe000edfcu,
    TRACE_ENABLE = 1u << 24u,
};

static CortexM4 create_cpu(void) {
    CortexM4 cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.xpsr = CORTEX_M4_XPSR_T;
    cortex_m4_debug_reset(&cpu);
    return cpu;
}

static void write_register(TestState* state, CortexM4* cpu, uint32_t address, uint32_t value) {
    expect(state,
           cortex_m4_debug_write(cpu, address, 4u, value) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "debug register write succeeds");
}

static uint32_t read_register(TestState* state, CortexM4* cpu, uint32_t address) {
    uint32_t value = 0u;
    expect(state,
           cortex_m4_debug_read(cpu, address, 4u, &value) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "debug register read succeeds");
    return value;
}

static void test_access_boundaries(TestState* state) {
    CortexM4 cpu = create_cpu();
    uint32_t value = 0u;

    expect(state,
           cortex_m4_debug_read(&cpu, ITM_BASE, 3u, &value) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "three-byte debug reads are rejected");
    expect(state,
           cortex_m4_debug_read(&cpu, ITM_BASE + 3u, 2u, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cross-word debug reads are rejected");
    expect(state,
           cortex_m4_debug_write(&cpu, ITM_BASE + 3u, 2u, 0u) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cross-word debug writes are rejected");

    cpu.debug.demcr = 1u << 16u;
    cpu.xpsr = CORTEX_M4_XPSR_T | 12u;
    cortex_m4_debug_breakpoint(&cpu);
    expect(state, (cpu.system_pending & (1u << 12u)) == 0u, "DebugMonitor does not pend itself");
}

static void test_register_transfer_boundaries(TestState* state) {
    CortexM4 cpu = create_cpu();

    cpu.msp = 0x20001000u;
    cpu.psp = 0x20002000u;
    cpu.xpsr = CORTEX_M4_XPSR_T | 3u;
    cpu.control = CORTEX_M4_CONTROL_SPSEL;
    write_register(state, &cpu, DCRSR, 13u);
    expect(state, read_register(state, &cpu, DCRDR) == cpu.msp, "handler stack selection uses MSP");

    cpu.xpsr = CORTEX_M4_XPSR_T;
    cpu.control = 0u;
    write_register(state, &cpu, DCRSR, 13u);
    expect(state, read_register(state, &cpu, DCRDR) == cpu.msp,
           "thread stack selection can use MSP");

    cpu.fp_registers[0] = 0x12345678u;
    cpu.fp_registers[31] = 0x87654321u;
    write_register(state, &cpu, DCRSR, 64u);
    expect(state, read_register(state, &cpu, DCRDR) == 0x12345678u,
           "first floating-point register transfers");
    write_register(state, &cpu, DCRSR, 95u);
    expect(state, read_register(state, &cpu, DCRDR) == 0x87654321u,
           "last floating-point register transfers");
}

static void test_counter_boundaries(TestState* state) {
    CortexM4 cpu = create_cpu();

    cortex_m4_debug_advance(&cpu, 0u, false);
    cortex_m4_debug_advance(&cpu, 1u, false);
    cpu.debug.demcr = TRACE_ENABLE;
    cortex_m4_debug_advance(&cpu, 1u, false);
    cortex_m4_debug_advance(&cpu, 1u, true);
    cortex_m4_debug_cpi_cycles(&cpu, 1u);
    cortex_m4_debug_exception_cycles(&cpu, 1u);
    cortex_m4_debug_lsu_cycles(&cpu, 1u);
    cortex_m4_debug_folded_instruction(&cpu);

    cpu.debug.dwt_control = 1u;
    cpu.debug.dwt_comparators[0].function = 0x84u;
    cpu.debug.dwt_cycle_count = 10u;
    cpu.debug.dwt_comparators[0].comparator = 9u;
    cortex_m4_debug_advance(&cpu, 5u, false);
    expect(state, (cpu.debug.dwt_comparators[0].function & (1u << 24u)) == 0u,
           "non-wrapping cycle ranges exclude earlier targets");

    cpu.debug.dwt_cycle_count = UINT32_MAX - 2u;
    cpu.debug.dwt_comparators[0].function = 0x84u;
    cpu.debug.dwt_comparators[0].comparator = UINT32_MAX - 3u;
    cortex_m4_debug_advance(&cpu, 5u, false);
    expect(state, (cpu.debug.dwt_comparators[0].function & (1u << 24u)) == 0u,
           "wrapping cycle ranges exclude earlier targets");
}

static void test_dwt_match_boundaries(TestState* state) {
    CortexM4 cpu = create_cpu();
    cpu.debug.demcr = TRACE_ENABLE;

    cpu.debug.dwt_comparators[0].comparator = 0x20000000u;
    cpu.debug.dwt_comparators[0].mask = 31u;
    cpu.debug.dwt_comparators[0].function = 4u;
    cortex_m4_debug_instruction_access(&cpu, 0x10000000u);
    expect(state, (cpu.debug.dwt_comparators[0].function & (1u << 24u)) != 0u,
           "maximum DWT mask matches every instruction address");

    cpu.debug.dwt_comparators[0].function = 5u | (3u << 10u);
    cortex_m4_debug_memory_access(&cpu, 0x20000000u, 1u, false, 0u);
    expect(state, (cpu.debug.dwt_comparators[0].function & (1u << 24u)) != 0u,
           "DWT size wildcard accepts byte accesses");

    cpu.debug.dwt_comparators[0].function = 5u | (1u << 8u) | (4u << 12u);
    cortex_m4_debug_memory_access(&cpu, 0x20000000u, 1u, false, 0u);
    expect(state, (cpu.debug.dwt_comparators[0].function & (1u << 24u)) == 0u,
           "invalid linked comparator indices do not match");
}

static void test_fpb_boundaries(TestState* state) {
    CortexM4 cpu = create_cpu();
    uint32_t remapped = 0u;

    expect(state, !cortex_m4_debug_remap_instruction(&cpu, 0u, &remapped),
           "disabled instruction remapping is rejected");
    expect(state, !cortex_m4_debug_remap_instruction(&cpu, 0u, NULL),
           "instruction remapping requires an output");
    expect(state, !cortex_m4_debug_remap_literal(&cpu, 0u, &remapped),
           "disabled literal remapping is rejected");
    expect(state, !cortex_m4_debug_remap_literal(&cpu, 0u, NULL),
           "literal remapping requires an output");
    cpu.debug.fpb_control = 1u;
    expect(state, !cortex_m4_debug_remap_instruction(&cpu, 0x20000000u, &remapped),
           "instruction remapping rejects peripheral addresses");
    expect(state, !cortex_m4_debug_remap_literal(&cpu, 0x20000000u, &remapped),
           "literal remapping rejects peripheral addresses");
}

int main(void) {
    TestState state = {0};
    test_access_boundaries(&state);
    test_register_transfer_boundaries(&state);
    test_counter_boundaries(&state);
    test_dwt_match_boundaries(&state);
    test_fpb_boundaries(&state);
    return test_finish(&state);
}
