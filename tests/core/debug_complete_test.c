#include "cortex_m4_internal.h"

#include <string.h>

#include "test.h"

#define ITM_BASE 0xe0000000u
#define DWT_BASE 0xe0001000u
#define FPB_BASE 0xe0002000u
#define TPIU_BASE 0xe0040000u
#define DHCSR 0xe000edf0u
#define DCRSR 0xe000edf4u
#define DCRDR 0xe000edf8u
#define DEMCR 0xe000edfcu
#define CORESIGHT_LAR 0xfb0u
#define CORESIGHT_LSR 0xfb4u
#define CORESIGHT_UNLOCK 0xc5acce55u

static uint32_t debug_read(TestState* state, CortexM4* cpu, uint32_t address, uint8_t size) {
    uint32_t value = 0xdeadbeefu;
    expect(state,
           cortex_m4_debug_read(cpu, address, size, &value) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_debug_read(cpu, address, size, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    return value;
}

static void debug_write(TestState* state, CortexM4* cpu, uint32_t address, uint8_t size,
                        uint32_t value) {
    expect(state,
           cortex_m4_debug_write(cpu, address, size, value) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_debug_write(cpu, address, size, value) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
}

static CortexM4 create_cpu(void) {
    CortexM4 cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.xpsr = CORTEX_M4_XPSR_T;
    cpu.stop = CORTEX_M4_STOP_RUNNING;
    cortex_m4_debug_reset(&cpu);
    return cpu;
}

static void test_reset_and_boundaries(TestState* state) {
    CortexM4 cpu = create_cpu();
    uint32_t value = 0x55aa55aau;
    expect(state, cpu.debug.reset_sticky, "cpu.debug.reset_sticky");
    expect(state, cpu.debug.itm_locked, "cpu.debug.itm_locked");
    expect(state, !cpu.debug.dwt_locked, "!cpu.debug.dwt_locked");
    expect(state, debug_read(state, &cpu, DHCSR, 4) == 0x02010000u,
           "debug_read(state, &cpu, DHCSR, 4) == 0x02010000u");
    expect(state, !cpu.debug.reset_sticky, "!cpu.debug.reset_sticky");
    expect(state, debug_read(state, &cpu, DHCSR, 4) == 0x00010000u,
           "debug_read(state, &cpu, DHCSR, 4) == 0x00010000u");
    expect(state,
           cortex_m4_debug_read(&cpu, 0x40000000u, 4, &value) == CORTEX_M4_SYSTEM_ACCESS_OUTSIDE,
           "cortex_m4_debug_read(&cpu, 0x40000000u, 4, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_OUTSIDE");
    expect(state, value == 0x55aa55aau, "value == 0x55aa55aau");
    expect(state, cortex_m4_debug_write(&cpu, 0x40000000u, 4, 0) == CORTEX_M4_SYSTEM_ACCESS_OUTSIDE,
           "cortex_m4_debug_write(&cpu, 0x40000000u, 4, 0) == "
           "CORTEX_M4_SYSTEM_ACCESS_OUTSIDE");
    expect(state,
           cortex_m4_debug_read(&cpu, DWT_BASE + 2u, 4, &value) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_debug_read(&cpu, DWT_BASE + 2u, 4, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_debug_read(&cpu, DWT_BASE, 2, &value) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_debug_read(&cpu, DWT_BASE, 2, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_debug_read(&cpu, DWT_BASE + 0x100u, 4, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_debug_read(&cpu, DWT_BASE + 0x100u, 4, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state, cortex_m4_debug_read(NULL, DHCSR, 4, &value) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_debug_read(NULL, DHCSR, 4, &value) == CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state, cortex_m4_debug_read(&cpu, DHCSR, 4, NULL) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_debug_read(&cpu, DHCSR, 4, NULL) == CORTEX_M4_SYSTEM_ACCESS_REJECTED");
}

static void test_halt_and_step(TestState* state) {
    CortexM4 cpu = create_cpu();
    debug_write(state, &cpu, DHCSR, 4, 0x00000003u);
    expect(state, !cpu.debug.halted, "!cpu.debug.halted");
    debug_write(state, &cpu, DHCSR, 4, 0xa05f0003u);
    expect(state, cpu.debug.halted, "cpu.debug.halted");
    expect(state, !cortex_m4_debug_execution_allowed(&cpu),
           "!cortex_m4_debug_execution_allowed(&cpu)");
    expect(state, (debug_read(state, &cpu, DHCSR, 4) & 0x00030003u) == 0x00030003u,
           "(debug_read(state, &cpu, DHCSR, 4) & 0x00030003u) == 0x00030003u");
    debug_write(state, &cpu, DHCSR, 4, 0xa05f0005u);
    expect(state, !cpu.debug.halted, "!cpu.debug.halted");
    expect(state, cpu.debug.step_armed, "cpu.debug.step_armed");
    expect(state, cortex_m4_debug_execution_allowed(&cpu),
           "cortex_m4_debug_execution_allowed(&cpu)");
    cortex_m4_debug_instruction_retired(&cpu);
    expect(state, cpu.debug.halted, "cpu.debug.halted");
    expect(state, !cpu.debug.step_armed, "!cpu.debug.step_armed");
    expect(state, (cpu.dfsr & 1u) != 0, "(cpu.dfsr & 1u) != 0");
    expect(state, (debug_read(state, &cpu, DHCSR, 4) & (1u << 24)) != 0,
           "(debug_read(state, &cpu, DHCSR, 4) & (1u << 24)) != 0");
    expect(state, (debug_read(state, &cpu, DHCSR, 4) & (1u << 24)) == 0,
           "(debug_read(state, &cpu, DHCSR, 4) & (1u << 24)) == 0");
    cpu.sleeping = true;
    cpu.stop = CORTEX_M4_STOP_LOCKUP;
    expect(state, (debug_read(state, &cpu, DHCSR, 4) & 0x000c0000u) == 0x000c0000u,
           "(debug_read(state, &cpu, DHCSR, 4) & 0x000c0000u) == 0x000c0000u");
    cpu.stop = CORTEX_M4_STOP_BREAKPOINT;
    debug_write(state, &cpu, DHCSR, 4, 0xa05f0000u);
    expect(state, !cpu.debug.halted, "!cpu.debug.halted");
    expect(state, !cpu.debug.step_armed, "!cpu.debug.step_armed");
    expect(state, cpu.stop == CORTEX_M4_STOP_RUNNING, "cpu.stop == CORTEX_M4_STOP_RUNNING");
}

static void test_register_transfer(TestState* state) {
    CortexM4 cpu = create_cpu();
    cpu.registers[3] = 0x12345678u;
    debug_write(state, &cpu, DCRSR, 4, 3u);
    expect(state, debug_read(state, &cpu, DCRDR, 4) == 0x12345678u,
           "debug_read(state, &cpu, DCRDR, 4) == 0x12345678u");
    debug_write(state, &cpu, DCRDR, 4, 0x89abcdefu);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 3u);
    expect(state, cpu.registers[3] == 0x89abcdefu, "cpu.registers[3] == 0x89abcdefu");
    debug_write(state, &cpu, DCRDR, 4, 0x00000101u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 16u);
    expect(state, (cpu.xpsr & CORTEX_M4_XPSR_T) != 0, "(cpu.xpsr & CORTEX_M4_XPSR_T) != 0");
    debug_write(state, &cpu, DCRDR, 4, 0x20001003u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 17u);
    expect(state, cpu.msp == 0x20001000u, "cpu.msp == 0x20001000u");
    debug_write(state, &cpu, DCRDR, 4, 0x20002003u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 18u);
    expect(state, cpu.psp == 0x20002000u, "cpu.psp == 0x20002000u");
    debug_write(state, &cpu, DCRDR, 4, 0x0701a501u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 20u);
    expect(state, cpu.control == 7u, "cpu.control == 7u");
    expect(state, cpu.faultmask == 1u, "cpu.faultmask == 1u");
    expect(state, cpu.basepri == 0xa0u, "cpu.basepri == 0xa0u");
    expect(state, cpu.primask == 1u, "cpu.primask == 1u");
    debug_write(state, &cpu, DCRSR, 4, 20u);
    expect(state, debug_read(state, &cpu, DCRDR, 4) == 0x0701a001u,
           "debug_read(state, &cpu, DCRDR, 4) == 0x0701a001u");
    debug_write(state, &cpu, DCRDR, 4, 0x87654321u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 33u);
    expect(state, cpu.fpscr == 0x87654321u, "cpu.fpscr == 0x87654321u");
    debug_write(state, &cpu, DCRDR, 4, 0x3f800000u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 71u);
    expect(state, cpu.fp_registers[7] == 0x3f800000u, "cpu.fp_registers[7] == 0x3f800000u");
    expect(state, (debug_read(state, &cpu, DHCSR, 4) & (1u << 16)) != 0,
           "(debug_read(state, &cpu, DHCSR, 4) & (1u << 16)) != 0");
    cpu.xpsr = CORTEX_M4_XPSR_T;
    cpu.control = CORTEX_M4_CONTROL_SPSEL;
    cpu.psp = 0x20003000u;
    debug_write(state, &cpu, DCRSR, 4, 13u);
    expect(state, debug_read(state, &cpu, DCRDR, 4) == 0x20003000u,
           "debug_read(state, &cpu, DCRDR, 4) == 0x20003000u");
    debug_write(state, &cpu, DCRSR, 4, 16u);
    expect(state, (debug_read(state, &cpu, DCRDR, 4) & CORTEX_M4_XPSR_T) != 0,
           "(debug_read(state, &cpu, DCRDR, 4) & CORTEX_M4_XPSR_T) != 0");
    debug_write(state, &cpu, DCRSR, 4, 17u);
    expect(state, debug_read(state, &cpu, DCRDR, 4) == 0x20001000u,
           "debug_read(state, &cpu, DCRDR, 4) == 0x20001000u");
    debug_write(state, &cpu, DCRSR, 4, 18u);
    expect(state, debug_read(state, &cpu, DCRDR, 4) == 0x20003000u,
           "debug_read(state, &cpu, DCRDR, 4) == 0x20003000u");
    debug_write(state, &cpu, DCRSR, 4, 33u);
    expect(state, debug_read(state, &cpu, DCRDR, 4) == 0x87654321u,
           "debug_read(state, &cpu, DCRDR, 4) == 0x87654321u");
    debug_write(state, &cpu, DCRSR, 4, 71u);
    expect(state, debug_read(state, &cpu, DCRDR, 4) == 0x3f800000u,
           "debug_read(state, &cpu, DCRDR, 4) == 0x3f800000u");
    debug_write(state, &cpu, DCRSR, 4, 127u);
    expect(state, debug_read(state, &cpu, DCRDR, 4) == 0u,
           "debug_read(state, &cpu, DCRDR, 4) == 0u");
    debug_write(state, &cpu, DCRDR, 4, 0x20004003u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 13u);
    expect(state, cpu.psp == 0x20004000u, "cpu.psp == 0x20004000u");
    cpu.control = 0u;
    debug_write(state, &cpu, DCRDR, 4, 0x20005003u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 13u);
    expect(state, cpu.msp == 0x20005000u, "cpu.msp == 0x20005000u");
    debug_write(state, &cpu, DCRDR, 4, 0x12345679u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 15u);
    expect(state, cpu.registers[15] == 0x12345678u, "cpu.registers[15] == 0x12345678u");
    expect(state, debug_read(state, &cpu, DCRSR, 4) == 0x0001000fu,
           "debug_read(state, &cpu, DCRSR, 4) == 0x0001000fu");
    expect(state, debug_read(state, &cpu, DEMCR, 4) == cpu.debug.demcr,
           "debug_read(state, &cpu, DEMCR, 4) == cpu.debug.demcr");
}

static void test_debug_monitor_and_vector_catch(TestState* state) {
    CortexM4 cpu = create_cpu();
    debug_write(state, &cpu, DEMCR, 4, (1u << 16) | (1u << 17));
    expect(state, cpu.debug.demcr == 0x00030000u, "cpu.debug.demcr == 0x00030000u");
    expect(state, (cpu.system_pending & (1u << 12)) != 0, "(cpu.system_pending & (1u << 12)) != 0");
    debug_write(state, &cpu, DEMCR, 4, 1u << 16);
    expect(state, (cpu.system_pending & (1u << 12)) == 0, "(cpu.system_pending & (1u << 12)) == 0");
    cortex_m4_debug_breakpoint(&cpu);
    expect(state, (cpu.dfsr & (1u << 1)) != 0, "(cpu.dfsr & (1u << 1)) != 0");
    expect(state, (cpu.system_pending & (1u << 12)) != 0, "(cpu.system_pending & (1u << 12)) != 0");
    expect(state, (cpu.system_pending & (1u << 3)) == 0, "(cpu.system_pending & (1u << 3)) == 0");
    cpu.system_pending = 0;
    cpu.debug.demcr = 0;
    cortex_m4_debug_breakpoint(&cpu);
    expect(state, (cpu.hfsr & (1u << 31)) != 0, "(cpu.hfsr & (1u << 31)) != 0");
    expect(state, (cpu.system_pending & (1u << 3)) != 0, "(cpu.system_pending & (1u << 3)) != 0");
    cpu.system_pending = 0;
    cpu.hfsr = 0;
    debug_write(state, &cpu, DHCSR, 4, 0xa05f0001u);
    cortex_m4_debug_breakpoint(&cpu);
    expect(state, cpu.debug.halted, "cpu.debug.halted");
    expect(state, (cpu.system_pending & ((1u << 3) | (1u << 12))) == 0,
           "(cpu.system_pending & ((1u << 3) | (1u << 12))) == 0");
    cpu.debug.halted = false;
    debug_write(state, &cpu, DEMCR, 4, (1u << 10));
    cortex_m4_debug_exception(&cpu, 3u);
    expect(state, cpu.debug.halted, "cpu.debug.halted");
    expect(state, (cpu.dfsr & (1u << 3)) != 0, "(cpu.dfsr & (1u << 3)) != 0");
    cpu.debug.dhcsr_control = 0;
    cpu.debug.halted = false;
    cpu.system_pending = 0;
    debug_write(state, &cpu, DEMCR, 4, (1u << 16) | (1u << 18));
    cortex_m4_debug_instruction_retired(&cpu);
    expect(state, (cpu.system_pending & (1u << 12)) != 0, "(cpu.system_pending & (1u << 12)) != 0");
    const uint8_t caught[] = {1u, 4u, 5u, 6u, 15u};
    const uint32_t masks[] = {1u, 1u << 4, 1u << 8, 7u << 5, 1u << 9};
    cpu.debug.dhcsr_control = 1u;
    for (uint8_t index = 0; index < sizeof(caught); index++) {
        cpu.debug.halted = false;
        cpu.debug.demcr = masks[index];
        cortex_m4_debug_exception(&cpu, caught[index]);
        expect(state, cpu.debug.halted, "cpu.debug.halted");
    }
    cpu.debug.halted = false;
    cpu.debug.dhcsr_control = 1u;
    debug_write(state, &cpu, DEMCR, 4, 1u << 19u);
    expect(state, cpu.debug.halted, "cpu.debug.halted");
}

static void test_dwt(TestState* state) {
    CortexM4 cpu = create_cpu();
    expect(state, debug_read(state, &cpu, DWT_BASE, 4) == 0x40000000u,
           "debug_read(state, &cpu, DWT_BASE, 4) == 0x40000000u");
    expect(state, debug_read(state, &cpu, DWT_BASE + CORESIGHT_LSR, 4) == 1u,
           "debug_read(state, &cpu, DWT_BASE + CORESIGHT_LSR, 4) == 1u");
    debug_write(state, &cpu, DWT_BASE, 4,
                1u | (1u << 17) | (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21));
    debug_write(state, &cpu, DEMCR, 4, 1u << 24);
    cortex_m4_debug_advance(&cpu, 11u, false);
    expect(state, debug_read(state, &cpu, DWT_BASE + 4u, 4) == 11u,
           "debug_read(state, &cpu, DWT_BASE + 4u, 4) == 11u");
    cortex_m4_debug_advance(&cpu, 5u, true);
    expect(state, debug_read(state, &cpu, DWT_BASE + 4u, 4) == 16u,
           "debug_read(state, &cpu, DWT_BASE + 4u, 4) == 16u");
    expect(state, debug_read(state, &cpu, DWT_BASE + 0x10u, 4) == 5u,
           "debug_read(state, &cpu, DWT_BASE + 0x10u, 4) == 5u");
    cortex_m4_debug_cpi_cycles(&cpu, 3u);
    expect(state, debug_read(state, &cpu, DWT_BASE + 8u, 4) == 3u,
           "debug_read(state, &cpu, DWT_BASE + 8u, 4) == 3u");
    cortex_m4_debug_exception_cycles(&cpu, 12u);
    expect(state, debug_read(state, &cpu, DWT_BASE + 0x0cu, 4) == 12u,
           "debug_read(state, &cpu, DWT_BASE + 0x0cu, 4) == 12u");
    cortex_m4_debug_lsu_cycles(&cpu, 2u);
    cortex_m4_debug_memory_access(&cpu, 0x20000000u, 4u, true, 0u);
    expect(state, debug_read(state, &cpu, DWT_BASE + 0x14u, 4) == 2u,
           "debug_read(state, &cpu, DWT_BASE + 0x14u, 4) == 2u");
    cortex_m4_debug_folded_instruction(&cpu);
    expect(state, debug_read(state, &cpu, DWT_BASE + 0x18u, 4) == 1u,
           "debug_read(state, &cpu, DWT_BASE + 0x18u, 4) == 1u");
    debug_write(state, &cpu, DWT_BASE + 0x20u, 4, 0x20000100u);
    debug_write(state, &cpu, DWT_BASE + 0x24u, 4, 2u);
    debug_write(state, &cpu, DWT_BASE + 0x28u, 4, 7u | (2u << 10));
    cpu.debug.demcr |= 1u << 16;
    cortex_m4_debug_memory_access(&cpu, 0x20000103u, 4u, false, 0u);
    expect(state, (cpu.debug.dwt_comparators[0].function & (1u << 24)) != 0,
           "(cpu.debug.dwt_comparators[0].function & (1u << 24)) != 0");
    expect(state, (cpu.system_pending & (1u << 12)) != 0, "(cpu.system_pending & (1u << 12)) != 0");
    cpu.system_pending = 0;
    debug_write(state, &cpu, DWT_BASE + 0x30u, 4, 0x00001000u);
    debug_write(state, &cpu, DWT_BASE + 0x34u, 4, 0u);
    debug_write(state, &cpu, DWT_BASE + 0x38u, 4, 4u);
    cortex_m4_debug_instruction_access(&cpu, 0x00001000u);
    expect(state, (cpu.debug.dwt_comparators[1].function & (1u << 24)) != 0,
           "(cpu.debug.dwt_comparators[1].function & (1u << 24)) != 0");
    expect(state, (debug_read(state, &cpu, DWT_BASE + 0x38u, 4) & (1u << 24)) != 0,
           "(debug_read(state, &cpu, DWT_BASE + 0x38u, 4) & (1u << 24)) != 0");
    expect(state, (cpu.debug.dwt_comparators[1].function & (1u << 24)) == 0,
           "(cpu.debug.dwt_comparators[1].function & (1u << 24)) == 0");
    debug_write(state, &cpu, DWT_BASE + CORESIGHT_LAR, 4, 0u);
    expect(state, debug_read(state, &cpu, DWT_BASE + CORESIGHT_LSR, 4) == 3u,
           "debug_read(state, &cpu, DWT_BASE + CORESIGHT_LSR, 4) == 3u");
    debug_write(state, &cpu, DWT_BASE + 4u, 4, 99u);
    expect(state, debug_read(state, &cpu, DWT_BASE + 4u, 4) == 16u,
           "debug_read(state, &cpu, DWT_BASE + 4u, 4) == 16u");
    debug_write(state, &cpu, DWT_BASE + CORESIGHT_LAR, 4, CORESIGHT_UNLOCK);
    debug_write(state, &cpu, DWT_BASE + 4u, 4, 99u);
    expect(state, debug_read(state, &cpu, DWT_BASE + 4u, 4) == 99u,
           "debug_read(state, &cpu, DWT_BASE + 4u, 4) == 99u");
    cpu.system_pending = 0;
    debug_write(state, &cpu, DWT_BASE + 0x20u, 4, 105u);
    debug_write(state, &cpu, DWT_BASE + 0x28u, 4, 0x84u);
    cortex_m4_debug_advance(&cpu, 6u, false);
    expect(state, (cpu.debug.dwt_comparators[0].function & (1u << 24)) != 0,
           "(cpu.debug.dwt_comparators[0].function & (1u << 24)) != 0");
    expect(state, (cpu.system_pending & (1u << 12)) != 0, "(cpu.system_pending & (1u << 12)) != 0");
    expect(state, debug_read(state, &cpu, DWT_BASE + 0x20u, 4) == 105u,
           "debug_read(state, &cpu, DWT_BASE + 0x20u, 4) == 105u");
    expect(state, debug_read(state, &cpu, DWT_BASE + 0x24u, 4) == 2u,
           "debug_read(state, &cpu, DWT_BASE + 0x24u, 4) == 2u");
    debug_write(state, &cpu, DWT_BASE + 4u, 4, UINT32_MAX - 2u);
    debug_write(state, &cpu, DWT_BASE + 0x20u, 4, 1u);
    debug_write(state, &cpu, DWT_BASE + 0x28u, 4, 0x84u);
    cortex_m4_debug_advance(&cpu, 5u, false);
    expect(state, (cpu.debug.dwt_comparators[0].function & (1u << 24)) != 0u,
           "(cpu.debug.dwt_comparators[0].function & (1u << 24)) != 0u");
    cpu.registers[15] = 0x1234u;
    expect(state, debug_read(state, &cpu, DWT_BASE + 0x1cu, 4) == 0x1234u,
           "debug_read(state, &cpu, DWT_BASE + 0x1cu, 4) == 0x1234u");
    debug_write(state, &cpu, DWT_BASE + 8u, 4, 0x1ffu);
    expect(state, debug_read(state, &cpu, DWT_BASE + 8u, 4) == 0xffu,
           "debug_read(state, &cpu, DWT_BASE + 8u, 4) == 0xffu");
    debug_write(state, &cpu, DWT_BASE + CORESIGHT_LSR, 4, 0xffffffffu);
    uint32_t rejected = 0;
    expect(state,
           cortex_m4_debug_read(&cpu, DWT_BASE + 0x64u, 4, &rejected) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_debug_read(&cpu, DWT_BASE + 0x64u, 4, &rejected) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_debug_write(&cpu, DWT_BASE + 0x64u, 4, 0) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_debug_write(&cpu, DWT_BASE + 0x64u, 4, 0) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    debug_write(state, &cpu, DWT_BASE + 0x30u, 4, 0x20000200u);
    debug_write(state, &cpu, DWT_BASE + 0x34u, 4, 0u);
    debug_write(state, &cpu, DWT_BASE + 0x40u, 4, 0x5au);
    debug_write(state, &cpu, DWT_BASE + 0x48u, 4, 5u | (1u << 8) | (1u << 12));
    cpu.system_pending = 0;
    cortex_m4_debug_memory_access(&cpu, 0x20000200u, 1u, false, 0x5au);
    expect(state, (cpu.debug.dwt_comparators[2].function & (1u << 24)) != 0,
           "(cpu.debug.dwt_comparators[2].function & (1u << 24)) != 0");
    expect(state, (cpu.system_pending & (1u << 12)) != 0, "(cpu.system_pending & (1u << 12)) != 0");
    debug_write(state, &cpu, DWT_BASE + 0x40u, 4, 0xa55au);
    debug_write(state, &cpu, DWT_BASE + 0x38u, 4, 4u | (1u << 10));
    debug_write(state, &cpu, DWT_BASE + 0x48u, 4, 5u | (1u << 8) | (1u << 10) | (1u << 12));
    cortex_m4_debug_memory_access(&cpu, 0x20000200u, 2u, false, 0xa55au);
    expect(state, (cpu.debug.dwt_comparators[2].function & (1u << 24)) != 0u,
           "(cpu.debug.dwt_comparators[2].function & (1u << 24)) != 0u");
}

static void test_fpb(TestState* state) {
    CortexM4 cpu = create_cpu();
    uint32_t remapped = 0;
    expect(state, debug_read(state, &cpu, FPB_BASE, 4) == 0x00000260u,
           "debug_read(state, &cpu, FPB_BASE, 4) == 0x00000260u");
    expect(state, debug_read(state, &cpu, FPB_BASE + CORESIGHT_LSR, 4) == 1u,
           "debug_read(state, &cpu, FPB_BASE + CORESIGHT_LSR, 4) == 1u");
    debug_write(state, &cpu, FPB_BASE, 4, 1u);
    expect(state, (debug_read(state, &cpu, FPB_BASE, 4) & 1u) == 0,
           "(debug_read(state, &cpu, FPB_BASE, 4) & 1u) == 0");
    debug_write(state, &cpu, FPB_BASE, 4, 3u);
    debug_write(state, &cpu, FPB_BASE + 4u, 4, 0x2000101fu);
    expect(state, debug_read(state, &cpu, FPB_BASE + 4u, 4) == 0x00001000u,
           "debug_read(state, &cpu, FPB_BASE + 4u, 4) == 0x00001000u");
    debug_write(state, &cpu, FPB_BASE + 8u, 4, 0x40000101u);
    expect(state, cortex_m4_debug_remap_instruction(&cpu, 0x00000100u, &remapped),
           "cortex_m4_debug_remap_instruction(&cpu, 0x00000100u, &remapped)");
    expect(state, remapped == 0x00001000u, "remapped == 0x00001000u");
    expect(state, !cortex_m4_debug_remap_instruction(&cpu, 0x00000102u, &remapped),
           "!cortex_m4_debug_remap_instruction(&cpu, 0x00000102u, &remapped)");
    debug_write(state, &cpu, FPB_BASE + 8u, 4, 0xc0000101u);
    expect(state, cortex_m4_debug_remap_instruction(&cpu, 0x00000102u, &remapped),
           "cortex_m4_debug_remap_instruction(&cpu, 0x00000102u, &remapped)");
    expect(state, remapped == 0x00001002u, "remapped == 0x00001002u");
    expect(state, !cortex_m4_debug_remap_instruction(&cpu, 0x20000102u, &remapped),
           "!cortex_m4_debug_remap_instruction(&cpu, 0x20000102u, &remapped)");
    debug_write(state, &cpu, FPB_BASE + 0x20u, 4, 0x00000301u);
    expect(state, cortex_m4_debug_remap_literal(&cpu, 0x00000300u, &remapped),
           "cortex_m4_debug_remap_literal(&cpu, 0x00000300u, &remapped)");
    expect(state, remapped == 0x00001018u, "remapped == 0x00001018u");
    expect(state, !cortex_m4_debug_remap_literal(&cpu, 0x00000400u, &remapped),
           "!cortex_m4_debug_remap_literal(&cpu, 0x00000400u, &remapped)");
    debug_write(state, &cpu, FPB_BASE + CORESIGHT_LAR, 4, 0u);
    debug_write(state, &cpu, FPB_BASE + 8u, 4, 0u);
    expect(state, debug_read(state, &cpu, FPB_BASE + 8u, 4) == 0xc0000101u,
           "debug_read(state, &cpu, FPB_BASE + 8u, 4) == 0xc0000101u");
    debug_write(state, &cpu, FPB_BASE + CORESIGHT_LAR, 4, CORESIGHT_UNLOCK);
    debug_write(state, &cpu, FPB_BASE + 8u, 4, 0u);
    expect(state, debug_read(state, &cpu, FPB_BASE + 8u, 4) == 0u,
           "debug_read(state, &cpu, FPB_BASE + 8u, 4) == 0u");
    uint32_t rejected = 0;
    expect(state,
           cortex_m4_debug_read(&cpu, FPB_BASE, 2, &rejected) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_debug_read(&cpu, FPB_BASE, 2, &rejected) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_debug_read(&cpu, FPB_BASE + 0x2cu, 4, &rejected) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_debug_read(&cpu, FPB_BASE + 0x2cu, 4, &rejected) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_debug_write(&cpu, FPB_BASE + 0x2cu, 4, 0) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_debug_write(&cpu, FPB_BASE + 0x2cu, 4, 0) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
}

static void test_itm_and_tpiu(TestState* state) {
    CortexM4 cpu = create_cpu();
    debug_write(state, &cpu, ITM_BASE + 0xe00u, 4, 1u);
    expect(state, debug_read(state, &cpu, ITM_BASE + 0xe00u, 4) == 0u,
           "debug_read(state, &cpu, ITM_BASE + 0xe00u, 4) == 0u");
    expect(state, debug_read(state, &cpu, ITM_BASE + CORESIGHT_LSR, 4) == 3u,
           "debug_read(state, &cpu, ITM_BASE + CORESIGHT_LSR, 4) == 3u");
    debug_write(state, &cpu, ITM_BASE + CORESIGHT_LAR, 4, CORESIGHT_UNLOCK);
    expect(state, debug_read(state, &cpu, ITM_BASE + CORESIGHT_LSR, 4) == 1u,
           "debug_read(state, &cpu, ITM_BASE + CORESIGHT_LSR, 4) == 1u");
    debug_write(state, &cpu, ITM_BASE + 0xe00u, 4, 1u);
    debug_write(state, &cpu, ITM_BASE + 0xe40u, 4, 0xffffffffu);
    debug_write(state, &cpu, ITM_BASE + 0xe80u, 4, 0xffffffffu);
    expect(state, debug_read(state, &cpu, ITM_BASE + 0xe80u, 4) == 0x007f0f1fu,
           "debug_read(state, &cpu, ITM_BASE + 0xe80u, 4) == 0x007f0f1fu");
    debug_write(state, &cpu, ITM_BASE + 0xe80u, 4, 1u);
    debug_write(state, &cpu, DEMCR, 4, 1u << 24);
    expect(state, debug_read(state, &cpu, ITM_BASE, 4) == 1u,
           "debug_read(state, &cpu, ITM_BASE, 4) == 1u");
    debug_write(state, &cpu, ITM_BASE, 1, 0x5au);
    expect(state, cpu.debug.itm_stimulus[0] == 0x5au, "cpu.debug.itm_stimulus[0] == 0x5au");
    debug_write(state, &cpu, ITM_BASE + 1u, 1, 0xa5u);
    expect(state, cpu.debug.itm_stimulus[0] == 0xa55au, "cpu.debug.itm_stimulus[0] == 0xa55au");
    debug_write(state, &cpu, ITM_BASE + 2u, 2, 0x3cc3u);
    expect(state, cpu.debug.itm_stimulus[0] == 0x3cc3a55au,
           "cpu.debug.itm_stimulus[0] == 0x3cc3a55au");
    expect(state, debug_read(state, &cpu, ITM_BASE, 1) == 1u,
           "debug_read(state, &cpu, ITM_BASE, 1) == 1u");
    expect(state, debug_read(state, &cpu, ITM_BASE, 2) == 1u,
           "debug_read(state, &cpu, ITM_BASE, 2) == 1u");
    expect(state, debug_read(state, &cpu, ITM_BASE + 0xe80u, 4) == 1u,
           "debug_read(state, &cpu, ITM_BASE + 0xe80u, 4) == 1u");
    expect(state, debug_read(state, &cpu, ITM_BASE + 0xe40u, 4) == 0x0fu,
           "debug_read(state, &cpu, ITM_BASE + 0xe40u, 4) == 0x0fu");
    debug_write(state, &cpu, ITM_BASE + 0xef8u, 4, 3u);
    debug_write(state, &cpu, ITM_BASE + 0xefcu, 4, 2u);
    debug_write(state, &cpu, ITM_BASE + 0xf00u, 4, 1u);
    expect(state, debug_read(state, &cpu, ITM_BASE + 0xef8u, 4) == 1u,
           "debug_read(state, &cpu, ITM_BASE + 0xef8u, 4) == 1u");
    expect(state, debug_read(state, &cpu, ITM_BASE + 0xefcu, 4) == 0u,
           "debug_read(state, &cpu, ITM_BASE + 0xefcu, 4) == 0u");
    expect(state, debug_read(state, &cpu, ITM_BASE + 0xf00u, 4) == 1u,
           "debug_read(state, &cpu, ITM_BASE + 0xf00u, 4) == 1u");
    expect(state, debug_read(state, &cpu, TPIU_BASE, 4) == 1u,
           "debug_read(state, &cpu, TPIU_BASE, 4) == 1u");
    expect(state, debug_read(state, &cpu, TPIU_BASE + 4u, 4) == 1u,
           "debug_read(state, &cpu, TPIU_BASE + 4u, 4) == 1u");
    debug_write(state, &cpu, TPIU_BASE + 0x10u, 4, 0xffffffffu);
    debug_write(state, &cpu, TPIU_BASE + 0xf0u, 4, 2u);
    debug_write(state, &cpu, TPIU_BASE + 0x304u, 4, 0xffffffffu);
    expect(state, debug_read(state, &cpu, TPIU_BASE + 0x10u, 4) == 0x1fffu,
           "debug_read(state, &cpu, TPIU_BASE + 0x10u, 4) == 0x1fffu");
    expect(state, debug_read(state, &cpu, TPIU_BASE + 0xf0u, 4) == 2u,
           "debug_read(state, &cpu, TPIU_BASE + 0xf0u, 4) == 2u");
    expect(state, debug_read(state, &cpu, TPIU_BASE + 0x300u, 4) == 8u,
           "debug_read(state, &cpu, TPIU_BASE + 0x300u, 4) == 8u");
    expect(state, debug_read(state, &cpu, TPIU_BASE + 0x304u, 4) == 0x103u,
           "debug_read(state, &cpu, TPIU_BASE + 0x304u, 4) == 0x103u");
    expect(state, debug_read(state, &cpu, TPIU_BASE + 0xfc8u, 4) == 0xcau,
           "debug_read(state, &cpu, TPIU_BASE + 0xfc8u, 4) == 0xcau");
    expect(state, debug_read(state, &cpu, TPIU_BASE + 0xfccu, 4) == 0x11u,
           "debug_read(state, &cpu, TPIU_BASE + 0xfccu, 4) == 0x11u");
    expect(state, debug_read(state, &cpu, TPIU_BASE + CORESIGHT_LSR, 4) == 1u,
           "debug_read(state, &cpu, TPIU_BASE + CORESIGHT_LSR, 4) == 1u");
    debug_write(state, &cpu, TPIU_BASE + CORESIGHT_LAR, 4, 0u);
    debug_write(state, &cpu, TPIU_BASE + 4u, 4, 0u);
    expect(state, debug_read(state, &cpu, TPIU_BASE + 4u, 4) == 1u,
           "debug_read(state, &cpu, TPIU_BASE + 4u, 4) == 1u");
    debug_write(state, &cpu, TPIU_BASE + CORESIGHT_LAR, 4, CORESIGHT_UNLOCK);
    debug_write(state, &cpu, TPIU_BASE + 4u, 4, 0u);
    expect(state, debug_read(state, &cpu, TPIU_BASE + 4u, 4) == 0u,
           "debug_read(state, &cpu, TPIU_BASE + 4u, 4) == 0u");
    debug_write(state, &cpu, TPIU_BASE + CORESIGHT_LSR, 4, 0u);
    uint32_t rejected = 0;
    expect(state,
           cortex_m4_debug_read(&cpu, TPIU_BASE, 2, &rejected) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_debug_read(&cpu, TPIU_BASE, 2, &rejected) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_debug_read(&cpu, TPIU_BASE + 0x100u, 4, &rejected) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_debug_read(&cpu, TPIU_BASE + 0x100u, 4, &rejected) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_debug_write(&cpu, TPIU_BASE + 0x100u, 4, 0) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_debug_write(&cpu, TPIU_BASE + 0x100u, 4, 0) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
}

static void test_null_hooks(TestState* state) {
    uint32_t address = 0;
    cortex_m4_debug_reset(NULL);
    cortex_m4_debug_advance(NULL, 1u, false);
    cortex_m4_debug_cpi_cycles(NULL, 1u);
    cortex_m4_debug_exception_cycles(NULL, 1u);
    cortex_m4_debug_lsu_cycles(NULL, 1u);
    cortex_m4_debug_folded_instruction(NULL);
    cortex_m4_debug_instruction_retired(NULL);
    cortex_m4_debug_breakpoint(NULL);
    cortex_m4_debug_exception(NULL, 3u);
    cortex_m4_debug_instruction_access(NULL, 0u);
    cortex_m4_debug_memory_access(NULL, 0u, 4u, false, 0u);
    expect(state, !cortex_m4_debug_execution_allowed(NULL),
           "!cortex_m4_debug_execution_allowed(NULL)");
    expect(state, !cortex_m4_debug_remap_instruction(NULL, 0u, &address),
           "!cortex_m4_debug_remap_instruction(NULL, 0u, &address)");
    expect(state, !cortex_m4_debug_remap_literal(NULL, 0u, &address),
           "!cortex_m4_debug_remap_literal(NULL, 0u, &address)");
}

int main(void) {
    TestState state = {0};
    test_reset_and_boundaries(&state);
    test_halt_and_step(&state);
    test_register_transfer(&state);
    test_debug_monitor_and_vector_catch(&state);
    test_dwt(&state);
    test_fpb(&state);
    test_itm_and_tpiu(&state);
    test_null_hooks(&state);
    return test_finish(&state);
}
