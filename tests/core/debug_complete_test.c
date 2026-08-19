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

static uint32_t debug_read(TestState* state, CortexM4* cpu, uint32_t address,
                           uint8_t size) {
    uint32_t value = 0xdeadbeefu;
    TEST_EXPECT(state, cortex_m4_debug_read(cpu, address, size, &value) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    return value;
}

static void debug_write(TestState* state, CortexM4* cpu, uint32_t address, uint8_t size,
                        uint32_t value) {
    TEST_EXPECT(state, cortex_m4_debug_write(cpu, address, size, value) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
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
    TEST_EXPECT(state, cpu.debug.reset_sticky);
    TEST_EXPECT(state, cpu.debug.itm_locked);
    TEST_EXPECT(state, !cpu.debug.dwt_locked);
    TEST_EXPECT(state, debug_read(state, &cpu, DHCSR, 4) == 0x02010000u);
    TEST_EXPECT(state, !cpu.debug.reset_sticky);
    TEST_EXPECT(state, debug_read(state, &cpu, DHCSR, 4) == 0x00010000u);
    TEST_EXPECT(state, cortex_m4_debug_read(&cpu, 0x40000000u, 4, &value) ==
                           CORTEX_M4_SYSTEM_ACCESS_OUTSIDE);
    TEST_EXPECT(state, value == 0x55aa55aau);
    TEST_EXPECT(state, cortex_m4_debug_write(&cpu, 0x40000000u, 4, 0) ==
                           CORTEX_M4_SYSTEM_ACCESS_OUTSIDE);
    TEST_EXPECT(state, cortex_m4_debug_read(&cpu, DWT_BASE + 2u, 4, &value) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
    TEST_EXPECT(state, cortex_m4_debug_read(&cpu, DWT_BASE, 2, &value) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
    TEST_EXPECT(state, cortex_m4_debug_read(&cpu, DWT_BASE + 0x100u, 4, &value) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
    TEST_EXPECT(state, cortex_m4_debug_read(NULL, DHCSR, 4, &value) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
    TEST_EXPECT(state, cortex_m4_debug_read(&cpu, DHCSR, 4, NULL) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
}

static void test_halt_and_step(TestState* state) {
    CortexM4 cpu = create_cpu();
    debug_write(state, &cpu, DHCSR, 4, 0x00000003u);
    TEST_EXPECT(state, !cpu.debug.halted);
    debug_write(state, &cpu, DHCSR, 4, 0xa05f0003u);
    TEST_EXPECT(state, cpu.debug.halted);
    TEST_EXPECT(state, !cortex_m4_debug_execution_allowed(&cpu));
    TEST_EXPECT(state, (debug_read(state, &cpu, DHCSR, 4) & 0x00030003u) == 0x00030003u);
    debug_write(state, &cpu, DHCSR, 4, 0xa05f0005u);
    TEST_EXPECT(state, !cpu.debug.halted);
    TEST_EXPECT(state, cpu.debug.step_armed);
    TEST_EXPECT(state, cortex_m4_debug_execution_allowed(&cpu));
    cortex_m4_debug_instruction_retired(&cpu);
    TEST_EXPECT(state, cpu.debug.halted);
    TEST_EXPECT(state, !cpu.debug.step_armed);
    TEST_EXPECT(state, (cpu.dfsr & 1u) != 0);
    TEST_EXPECT(state, (debug_read(state, &cpu, DHCSR, 4) & (1u << 24)) != 0);
    TEST_EXPECT(state, (debug_read(state, &cpu, DHCSR, 4) & (1u << 24)) == 0);
    cpu.sleeping = true;
    cpu.stop = CORTEX_M4_STOP_LOCKUP;
    TEST_EXPECT(state, (debug_read(state, &cpu, DHCSR, 4) & 0x000c0000u) == 0x000c0000u);
    cpu.stop = CORTEX_M4_STOP_BREAKPOINT;
    debug_write(state, &cpu, DHCSR, 4, 0xa05f0000u);
    TEST_EXPECT(state, !cpu.debug.halted);
    TEST_EXPECT(state, !cpu.debug.step_armed);
    TEST_EXPECT(state, cpu.stop == CORTEX_M4_STOP_RUNNING);
}

static void test_register_transfer(TestState* state) {
    CortexM4 cpu = create_cpu();
    cpu.registers[3] = 0x12345678u;
    debug_write(state, &cpu, DCRSR, 4, 3u);
    TEST_EXPECT(state, debug_read(state, &cpu, DCRDR, 4) == 0x12345678u);
    debug_write(state, &cpu, DCRDR, 4, 0x89abcdefu);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 3u);
    TEST_EXPECT(state, cpu.registers[3] == 0x89abcdefu);
    debug_write(state, &cpu, DCRDR, 4, 0x00000101u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 16u);
    TEST_EXPECT(state, (cpu.xpsr & CORTEX_M4_XPSR_T) != 0);
    debug_write(state, &cpu, DCRDR, 4, 0x20001003u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 17u);
    TEST_EXPECT(state, cpu.msp == 0x20001000u);
    debug_write(state, &cpu, DCRDR, 4, 0x20002003u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 18u);
    TEST_EXPECT(state, cpu.psp == 0x20002000u);
    debug_write(state, &cpu, DCRDR, 4, 0x0701a501u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 20u);
    TEST_EXPECT(state, cpu.control == 7u);
    TEST_EXPECT(state, cpu.faultmask == 1u);
    TEST_EXPECT(state, cpu.basepri == 0xa0u);
    TEST_EXPECT(state, cpu.primask == 1u);
    debug_write(state, &cpu, DCRSR, 4, 20u);
    TEST_EXPECT(state, debug_read(state, &cpu, DCRDR, 4) == 0x0701a001u);
    debug_write(state, &cpu, DCRDR, 4, 0x87654321u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 33u);
    TEST_EXPECT(state, cpu.fpscr == 0x87654321u);
    debug_write(state, &cpu, DCRDR, 4, 0x3f800000u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 71u);
    TEST_EXPECT(state, cpu.fp_registers[7] == 0x3f800000u);
    TEST_EXPECT(state, (debug_read(state, &cpu, DHCSR, 4) & (1u << 16)) != 0);
    cpu.xpsr = CORTEX_M4_XPSR_T;
    cpu.control = CORTEX_M4_CONTROL_SPSEL;
    cpu.psp = 0x20003000u;
    debug_write(state, &cpu, DCRSR, 4, 13u);
    TEST_EXPECT(state, debug_read(state, &cpu, DCRDR, 4) == 0x20003000u);
    debug_write(state, &cpu, DCRSR, 4, 16u);
    TEST_EXPECT(state, (debug_read(state, &cpu, DCRDR, 4) & CORTEX_M4_XPSR_T) != 0);
    debug_write(state, &cpu, DCRSR, 4, 17u);
    TEST_EXPECT(state, debug_read(state, &cpu, DCRDR, 4) == 0x20001000u);
    debug_write(state, &cpu, DCRSR, 4, 18u);
    TEST_EXPECT(state, debug_read(state, &cpu, DCRDR, 4) == 0x20003000u);
    debug_write(state, &cpu, DCRSR, 4, 33u);
    TEST_EXPECT(state, debug_read(state, &cpu, DCRDR, 4) == 0x87654321u);
    debug_write(state, &cpu, DCRSR, 4, 71u);
    TEST_EXPECT(state, debug_read(state, &cpu, DCRDR, 4) == 0x3f800000u);
    debug_write(state, &cpu, DCRSR, 4, 127u);
    TEST_EXPECT(state, debug_read(state, &cpu, DCRDR, 4) == 0u);
    debug_write(state, &cpu, DCRDR, 4, 0x20004003u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 13u);
    TEST_EXPECT(state, cpu.psp == 0x20004000u);
    cpu.control = 0u;
    debug_write(state, &cpu, DCRDR, 4, 0x20005003u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 13u);
    TEST_EXPECT(state, cpu.msp == 0x20005000u);
    debug_write(state, &cpu, DCRDR, 4, 0x12345679u);
    debug_write(state, &cpu, DCRSR, 4, (1u << 16) | 15u);
    TEST_EXPECT(state, cpu.registers[15] == 0x12345678u);
    TEST_EXPECT(state, debug_read(state, &cpu, DCRSR, 4) == 0x0001000fu);
    TEST_EXPECT(state, debug_read(state, &cpu, DEMCR, 4) == cpu.debug.demcr);
}

static void test_debug_monitor_and_vector_catch(TestState* state) {
    CortexM4 cpu = create_cpu();
    debug_write(state, &cpu, DEMCR, 4, (1u << 16) | (1u << 17));
    TEST_EXPECT(state, cpu.debug.demcr == 0x00030000u);
    TEST_EXPECT(state, (cpu.system_pending & (1u << 12)) != 0);
    debug_write(state, &cpu, DEMCR, 4, 1u << 16);
    TEST_EXPECT(state, (cpu.system_pending & (1u << 12)) == 0);
    cortex_m4_debug_breakpoint(&cpu);
    TEST_EXPECT(state, (cpu.dfsr & (1u << 1)) != 0);
    TEST_EXPECT(state, (cpu.system_pending & (1u << 12)) != 0);
    TEST_EXPECT(state, (cpu.system_pending & (1u << 3)) == 0);
    cpu.system_pending = 0;
    cpu.debug.demcr = 0;
    cortex_m4_debug_breakpoint(&cpu);
    TEST_EXPECT(state, (cpu.hfsr & (1u << 31)) != 0);
    TEST_EXPECT(state, (cpu.system_pending & (1u << 3)) != 0);
    cpu.system_pending = 0;
    cpu.hfsr = 0;
    debug_write(state, &cpu, DHCSR, 4, 0xa05f0001u);
    cortex_m4_debug_breakpoint(&cpu);
    TEST_EXPECT(state, cpu.debug.halted);
    TEST_EXPECT(state, (cpu.system_pending & ((1u << 3) | (1u << 12))) == 0);
    cpu.debug.halted = false;
    debug_write(state, &cpu, DEMCR, 4, (1u << 10));
    cortex_m4_debug_exception(&cpu, 3u);
    TEST_EXPECT(state, cpu.debug.halted);
    TEST_EXPECT(state, (cpu.dfsr & (1u << 3)) != 0);
    cpu.debug.dhcsr_control = 0;
    cpu.debug.halted = false;
    cpu.system_pending = 0;
    debug_write(state, &cpu, DEMCR, 4, (1u << 16) | (1u << 18));
    cortex_m4_debug_instruction_retired(&cpu);
    TEST_EXPECT(state, (cpu.system_pending & (1u << 12)) != 0);
    const uint8_t caught[] = {1u, 4u, 5u, 6u, 15u};
    const uint32_t masks[] = {1u, 1u << 4, 1u << 8, 7u << 5, 1u << 9};
    cpu.debug.dhcsr_control = 1u;
    for (uint8_t index = 0; index < sizeof(caught); index++) {
        cpu.debug.halted = false;
        cpu.debug.demcr = masks[index];
        cortex_m4_debug_exception(&cpu, caught[index]);
        TEST_EXPECT(state, cpu.debug.halted);
    }
    cpu.debug.halted = false;
    cpu.debug.dhcsr_control = 1u;
    debug_write(state, &cpu, DEMCR, 4, 1u << 19u);
    TEST_EXPECT(state, cpu.debug.halted);
}

static void test_dwt(TestState* state) {
    CortexM4 cpu = create_cpu();
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE, 4) == 0x40000000u);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + CORESIGHT_LSR, 4) == 1u);
    debug_write(state, &cpu, DWT_BASE, 4,
                1u | (1u << 17) | (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21));
    debug_write(state, &cpu, DEMCR, 4, 1u << 24);
    cortex_m4_debug_advance(&cpu, 11u, false);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + 4u, 4) == 11u);
    cortex_m4_debug_advance(&cpu, 5u, true);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + 4u, 4) == 16u);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + 0x10u, 4) == 5u);
    cortex_m4_debug_cpi_cycles(&cpu, 3u);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + 8u, 4) == 3u);
    cortex_m4_debug_exception_cycles(&cpu, 12u);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + 0x0cu, 4) == 12u);
    cortex_m4_debug_lsu_cycles(&cpu, 2u);
    cortex_m4_debug_memory_access(&cpu, 0x20000000u, 4u, true, 0u);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + 0x14u, 4) == 2u);
    cortex_m4_debug_folded_instruction(&cpu);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + 0x18u, 4) == 1u);
    debug_write(state, &cpu, DWT_BASE + 0x20u, 4, 0x20000100u);
    debug_write(state, &cpu, DWT_BASE + 0x24u, 4, 2u);
    debug_write(state, &cpu, DWT_BASE + 0x28u, 4, 7u | (2u << 10));
    cpu.debug.demcr |= 1u << 16;
    cortex_m4_debug_memory_access(&cpu, 0x20000103u, 4u, false, 0u);
    TEST_EXPECT(state, (cpu.debug.dwt_comparators[0].function & (1u << 24)) != 0);
    TEST_EXPECT(state, (cpu.system_pending & (1u << 12)) != 0);
    cpu.system_pending = 0;
    debug_write(state, &cpu, DWT_BASE + 0x30u, 4, 0x00001000u);
    debug_write(state, &cpu, DWT_BASE + 0x34u, 4, 0u);
    debug_write(state, &cpu, DWT_BASE + 0x38u, 4, 4u);
    cortex_m4_debug_instruction_access(&cpu, 0x00001000u);
    TEST_EXPECT(state, (cpu.debug.dwt_comparators[1].function & (1u << 24)) != 0);
    TEST_EXPECT(state, (debug_read(state, &cpu, DWT_BASE + 0x38u, 4) & (1u << 24)) != 0);
    TEST_EXPECT(state, (cpu.debug.dwt_comparators[1].function & (1u << 24)) == 0);
    debug_write(state, &cpu, DWT_BASE + CORESIGHT_LAR, 4, 0u);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + CORESIGHT_LSR, 4) == 3u);
    debug_write(state, &cpu, DWT_BASE + 4u, 4, 99u);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + 4u, 4) == 16u);
    debug_write(state, &cpu, DWT_BASE + CORESIGHT_LAR, 4, CORESIGHT_UNLOCK);
    debug_write(state, &cpu, DWT_BASE + 4u, 4, 99u);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + 4u, 4) == 99u);
    cpu.system_pending = 0;
    debug_write(state, &cpu, DWT_BASE + 0x20u, 4, 105u);
    debug_write(state, &cpu, DWT_BASE + 0x28u, 4, 0x84u);
    cortex_m4_debug_advance(&cpu, 6u, false);
    TEST_EXPECT(state, (cpu.debug.dwt_comparators[0].function & (1u << 24)) != 0);
    TEST_EXPECT(state, (cpu.system_pending & (1u << 12)) != 0);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + 0x20u, 4) == 105u);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + 0x24u, 4) == 2u);
    debug_write(state, &cpu, DWT_BASE + 4u, 4, UINT32_MAX - 2u);
    debug_write(state, &cpu, DWT_BASE + 0x20u, 4, 1u);
    debug_write(state, &cpu, DWT_BASE + 0x28u, 4, 0x84u);
    cortex_m4_debug_advance(&cpu, 5u, false);
    TEST_EXPECT(state, (cpu.debug.dwt_comparators[0].function & (1u << 24)) != 0u);
    cpu.registers[15] = 0x1234u;
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + 0x1cu, 4) == 0x1234u);
    debug_write(state, &cpu, DWT_BASE + 8u, 4, 0x1ffu);
    TEST_EXPECT(state, debug_read(state, &cpu, DWT_BASE + 8u, 4) == 0xffu);
    debug_write(state, &cpu, DWT_BASE + CORESIGHT_LSR, 4, 0xffffffffu);
    uint32_t rejected = 0;
    TEST_EXPECT(state, cortex_m4_debug_read(&cpu, DWT_BASE + 0x64u, 4, &rejected) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
    TEST_EXPECT(state, cortex_m4_debug_write(&cpu, DWT_BASE + 0x64u, 4, 0) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
    debug_write(state, &cpu, DWT_BASE + 0x30u, 4, 0x20000200u);
    debug_write(state, &cpu, DWT_BASE + 0x34u, 4, 0u);
    debug_write(state, &cpu, DWT_BASE + 0x40u, 4, 0x5au);
    debug_write(state, &cpu, DWT_BASE + 0x48u, 4, 5u | (1u << 8) | (1u << 12));
    cpu.system_pending = 0;
    cortex_m4_debug_memory_access(&cpu, 0x20000200u, 1u, false, 0x5au);
    TEST_EXPECT(state, (cpu.debug.dwt_comparators[2].function & (1u << 24)) != 0);
    TEST_EXPECT(state, (cpu.system_pending & (1u << 12)) != 0);
    debug_write(state, &cpu, DWT_BASE + 0x40u, 4, 0xa55au);
    debug_write(state, &cpu, DWT_BASE + 0x38u, 4, 4u | (1u << 10));
    debug_write(state, &cpu, DWT_BASE + 0x48u, 4, 5u | (1u << 8) | (1u << 10) | (1u << 12));
    cortex_m4_debug_memory_access(&cpu, 0x20000200u, 2u, false, 0xa55au);
    TEST_EXPECT(state, (cpu.debug.dwt_comparators[2].function & (1u << 24)) != 0u);
}

static void test_fpb(TestState* state) {
    CortexM4 cpu = create_cpu();
    uint32_t remapped = 0;
    TEST_EXPECT(state, debug_read(state, &cpu, FPB_BASE, 4) == 0x00000260u);
    TEST_EXPECT(state, debug_read(state, &cpu, FPB_BASE + CORESIGHT_LSR, 4) == 1u);
    debug_write(state, &cpu, FPB_BASE, 4, 1u);
    TEST_EXPECT(state, (debug_read(state, &cpu, FPB_BASE, 4) & 1u) == 0);
    debug_write(state, &cpu, FPB_BASE, 4, 3u);
    debug_write(state, &cpu, FPB_BASE + 4u, 4, 0x2000101fu);
    TEST_EXPECT(state, debug_read(state, &cpu, FPB_BASE + 4u, 4) == 0x00001000u);
    debug_write(state, &cpu, FPB_BASE + 8u, 4, 0x40000101u);
    TEST_EXPECT(state, cortex_m4_debug_remap_instruction(&cpu, 0x00000100u, &remapped));
    TEST_EXPECT(state, remapped == 0x00001000u);
    TEST_EXPECT(state, !cortex_m4_debug_remap_instruction(&cpu, 0x00000102u, &remapped));
    debug_write(state, &cpu, FPB_BASE + 8u, 4, 0xc0000101u);
    TEST_EXPECT(state, cortex_m4_debug_remap_instruction(&cpu, 0x00000102u, &remapped));
    TEST_EXPECT(state, remapped == 0x00001002u);
    TEST_EXPECT(state, !cortex_m4_debug_remap_instruction(&cpu, 0x20000102u, &remapped));
    debug_write(state, &cpu, FPB_BASE + 0x20u, 4, 0x00000301u);
    TEST_EXPECT(state, cortex_m4_debug_remap_literal(&cpu, 0x00000300u, &remapped));
    TEST_EXPECT(state, remapped == 0x00001018u);
    TEST_EXPECT(state, !cortex_m4_debug_remap_literal(&cpu, 0x00000400u, &remapped));
    debug_write(state, &cpu, FPB_BASE + CORESIGHT_LAR, 4, 0u);
    debug_write(state, &cpu, FPB_BASE + 8u, 4, 0u);
    TEST_EXPECT(state, debug_read(state, &cpu, FPB_BASE + 8u, 4) == 0xc0000101u);
    debug_write(state, &cpu, FPB_BASE + CORESIGHT_LAR, 4, CORESIGHT_UNLOCK);
    debug_write(state, &cpu, FPB_BASE + 8u, 4, 0u);
    TEST_EXPECT(state, debug_read(state, &cpu, FPB_BASE + 8u, 4) == 0u);
    uint32_t rejected = 0;
    TEST_EXPECT(state, cortex_m4_debug_read(&cpu, FPB_BASE, 2, &rejected) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
    TEST_EXPECT(state, cortex_m4_debug_read(&cpu, FPB_BASE + 0x2cu, 4, &rejected) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
    TEST_EXPECT(state, cortex_m4_debug_write(&cpu, FPB_BASE + 0x2cu, 4, 0) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
}

static void test_itm_and_tpiu(TestState* state) {
    CortexM4 cpu = create_cpu();
    debug_write(state, &cpu, ITM_BASE + 0xe00u, 4, 1u);
    TEST_EXPECT(state, debug_read(state, &cpu, ITM_BASE + 0xe00u, 4) == 0u);
    TEST_EXPECT(state, debug_read(state, &cpu, ITM_BASE + CORESIGHT_LSR, 4) == 3u);
    debug_write(state, &cpu, ITM_BASE + CORESIGHT_LAR, 4, CORESIGHT_UNLOCK);
    TEST_EXPECT(state, debug_read(state, &cpu, ITM_BASE + CORESIGHT_LSR, 4) == 1u);
    debug_write(state, &cpu, ITM_BASE + 0xe00u, 4, 1u);
    debug_write(state, &cpu, ITM_BASE + 0xe40u, 4, 0xffffffffu);
    debug_write(state, &cpu, ITM_BASE + 0xe80u, 4, 0xffffffffu);
    TEST_EXPECT(state, debug_read(state, &cpu, ITM_BASE + 0xe80u, 4) == 0x007f0f1fu);
    debug_write(state, &cpu, ITM_BASE + 0xe80u, 4, 1u);
    debug_write(state, &cpu, DEMCR, 4, 1u << 24);
    TEST_EXPECT(state, debug_read(state, &cpu, ITM_BASE, 4) == 1u);
    debug_write(state, &cpu, ITM_BASE, 1, 0x5au);
    TEST_EXPECT(state, cpu.debug.itm_stimulus[0] == 0x5au);
    debug_write(state, &cpu, ITM_BASE + 1u, 1, 0xa5u);
    TEST_EXPECT(state, cpu.debug.itm_stimulus[0] == 0xa55au);
    debug_write(state, &cpu, ITM_BASE + 2u, 2, 0x3cc3u);
    TEST_EXPECT(state, cpu.debug.itm_stimulus[0] == 0x3cc3a55au);
    TEST_EXPECT(state, debug_read(state, &cpu, ITM_BASE, 1) == 1u);
    TEST_EXPECT(state, debug_read(state, &cpu, ITM_BASE, 2) == 1u);
    TEST_EXPECT(state, debug_read(state, &cpu, ITM_BASE + 0xe80u, 4) == 1u);
    TEST_EXPECT(state, debug_read(state, &cpu, ITM_BASE + 0xe40u, 4) == 0x0fu);
    debug_write(state, &cpu, ITM_BASE + 0xef8u, 4, 3u);
    debug_write(state, &cpu, ITM_BASE + 0xefcu, 4, 2u);
    debug_write(state, &cpu, ITM_BASE + 0xf00u, 4, 1u);
    TEST_EXPECT(state, debug_read(state, &cpu, ITM_BASE + 0xef8u, 4) == 1u);
    TEST_EXPECT(state, debug_read(state, &cpu, ITM_BASE + 0xefcu, 4) == 0u);
    TEST_EXPECT(state, debug_read(state, &cpu, ITM_BASE + 0xf00u, 4) == 1u);
    TEST_EXPECT(state, debug_read(state, &cpu, TPIU_BASE, 4) == 1u);
    TEST_EXPECT(state, debug_read(state, &cpu, TPIU_BASE + 4u, 4) == 1u);
    debug_write(state, &cpu, TPIU_BASE + 0x10u, 4, 0xffffffffu);
    debug_write(state, &cpu, TPIU_BASE + 0xf0u, 4, 2u);
    debug_write(state, &cpu, TPIU_BASE + 0x304u, 4, 0xffffffffu);
    TEST_EXPECT(state, debug_read(state, &cpu, TPIU_BASE + 0x10u, 4) == 0x1fffu);
    TEST_EXPECT(state, debug_read(state, &cpu, TPIU_BASE + 0xf0u, 4) == 2u);
    TEST_EXPECT(state, debug_read(state, &cpu, TPIU_BASE + 0x300u, 4) == 8u);
    TEST_EXPECT(state, debug_read(state, &cpu, TPIU_BASE + 0x304u, 4) == 0x103u);
    TEST_EXPECT(state, debug_read(state, &cpu, TPIU_BASE + 0xfc8u, 4) == 0xcau);
    TEST_EXPECT(state, debug_read(state, &cpu, TPIU_BASE + 0xfccu, 4) == 0x11u);
    TEST_EXPECT(state, debug_read(state, &cpu, TPIU_BASE + CORESIGHT_LSR, 4) == 1u);
    debug_write(state, &cpu, TPIU_BASE + CORESIGHT_LAR, 4, 0u);
    debug_write(state, &cpu, TPIU_BASE + 4u, 4, 0u);
    TEST_EXPECT(state, debug_read(state, &cpu, TPIU_BASE + 4u, 4) == 1u);
    debug_write(state, &cpu, TPIU_BASE + CORESIGHT_LAR, 4, CORESIGHT_UNLOCK);
    debug_write(state, &cpu, TPIU_BASE + 4u, 4, 0u);
    TEST_EXPECT(state, debug_read(state, &cpu, TPIU_BASE + 4u, 4) == 0u);
    debug_write(state, &cpu, TPIU_BASE + CORESIGHT_LSR, 4, 0u);
    uint32_t rejected = 0;
    TEST_EXPECT(state, cortex_m4_debug_read(&cpu, TPIU_BASE, 2, &rejected) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
    TEST_EXPECT(state, cortex_m4_debug_read(&cpu, TPIU_BASE + 0x100u, 4, &rejected) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
    TEST_EXPECT(state, cortex_m4_debug_write(&cpu, TPIU_BASE + 0x100u, 4, 0) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
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
    TEST_EXPECT(state, !cortex_m4_debug_execution_allowed(NULL));
    TEST_EXPECT(state, !cortex_m4_debug_remap_instruction(NULL, 0u, &address));
    TEST_EXPECT(state, !cortex_m4_debug_remap_literal(NULL, 0u, &address));
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
