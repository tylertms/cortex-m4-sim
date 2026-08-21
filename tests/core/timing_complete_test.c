#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cortex_m4_internal.h"
#include "test.h"

typedef struct {
    uint32_t calls;
    uint32_t sequential_calls;
    uint32_t instruction_wait;
    uint32_t data_wait;
    uint32_t store_wait;
    uint64_t advanced;
} TimingFixture;

void cortex_m4_advance(CortexM4* cpu, uint32_t cycles) {
    cpu->cycles += cycles;
    if (cpu->bus.context != NULL) {
        TimingFixture* fixture = cpu->bus.context;
        fixture->advanced += cycles;
    }
}

uint32_t cortex_m4_read_register_internal(const CortexM4* cpu, uint8_t index) {
    return cpu->registers[index & 15u];
}

static uint32_t wait_states(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                            bool write, bool sequential) {
    TimingFixture* fixture = context;
    fixture->calls++;
    fixture->sequential_calls += sequential ? 1u : 0u;
    if (write) {
        return fixture->store_wait;
    }
    if (access == CORTEX_M4_ACCESS_INSTRUCTION) {
        return sequential ? fixture->instruction_wait / 2u : fixture->instruction_wait;
    }
    return fixture->data_wait + (address & (size - 1u));
}

static void reset_cpu(CortexM4* cpu, TimingFixture* fixture) {
    memset(cpu, 0, sizeof(*cpu));
    memset(fixture, 0, sizeof(*fixture));
    cpu->bus.context = fixture;
    cortex_m4_timing_reset(cpu);
    cortex_m4_set_wait_states(cpu, wait_states, fixture);
}

static void complete(CortexM4* cpu, uint16_t first, uint16_t second, bool wide, bool executed,
                     uint32_t sequential_pc) {
    cortex_m4_timing_complete_instruction(cpu, first, second, wide, executed, sequential_pc);
}

static void test_wait_states(TestState* state) {
    CortexM4 cpu;
    TimingFixture fixture;
    reset_cpu(&cpu, &fixture);
    fixture.instruction_wait = 4;
    cortex_m4_timing_begin_instruction(&cpu);
    cortex_m4_timing_access(&cpu, 0x100u, 2, CORTEX_M4_ACCESS_INSTRUCTION, false);
    cortex_m4_timing_access(&cpu, 0x102u, 2, CORTEX_M4_ACCESS_INSTRUCTION, false);
    complete(&cpu, 0xf3afu, 0x8000u, true, true, 0x104u);
    expect(state, cpu.cycles == 7, "cpu.cycles == 7");
    expect(state, fixture.calls == 2, "fixture.calls == 2");
    expect(state, fixture.sequential_calls == 1, "fixture.sequential_calls == 1");
    expect(state, fixture.advanced == cpu.cycles, "fixture.advanced == cpu.cycles");

    fixture.data_wait = 3;
    cortex_m4_timing_begin_instruction(&cpu);
    cortex_m4_timing_access(&cpu, 0x104u, 2, CORTEX_M4_ACCESS_INSTRUCTION, false);
    cortex_m4_timing_access(&cpu, 0x20000000u, 4, CORTEX_M4_ACCESS_DATA, false);
    complete(&cpu, 0x6808u, 0, false, true, 0x106u);
    expect(state, cpu.cycles == 14, "cpu.cycles == 14");
    expect(state, fixture.sequential_calls == 2, "fixture.sequential_calls == 2");
}

static void test_instruction_classes(TestState* state) {
    CortexM4 cpu;
    TimingFixture fixture;
    reset_cpu(&cpu, &fixture);

    cpu.registers[15] = 0x120u;
    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xe00eu, 0, false, true, 0x102u);
    expect(state, cpu.cycles == 3, "cpu.cycles == 3");

    cpu.registers[15] = 0x104u;
    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xd000u, 0, false, true, 0x104u);
    expect(state, cpu.cycles == 4, "cpu.cycles == 4");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xc907u, 0, false, true, 0x106u);
    expect(state, cpu.cycles == 8, "cpu.cycles == 8");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xbd03u, 0, false, true, 0x108u);
    expect(state, cpu.cycles == 14, "cpu.cycles == 14");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0x6008u, 0, false, true, 0x10au);
    expect(state, cpu.cycles == 15, "cpu.cycles == 15");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0x6808u, 0, false, true, 0x10cu);
    expect(state, cpu.cycles == 17, "cpu.cycles == 17");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0x4348u, 0, false, true, 0x10eu);
    expect(state, cpu.cycles == 18, "cpu.cycles == 18");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xe000u, 0, false, false, 0x110u);
    expect(state, cpu.cycles == 19, "cpu.cycles == 19");

    cpu.registers[15] = 0x200u;
    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xe8d0u, 0xf001u, true, true, 0x114u);
    expect(state, cpu.cycles == 23, "cpu.cycles == 23");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xe851u, 0x0f00u, true, true, 0x118u);
    expect(state, cpu.cycles == 25, "cpu.cycles == 25");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xf8d1u, 0xf000u, true, true, 0x11cu);
    expect(state, cpu.cycles == 29, "cpu.cycles == 29");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xe9d1u, 0x2300u, true, true, 0x120u);
    expect(state, cpu.cycles == 32, "cpu.cycles == 32");

    cpu.registers[15] = 0x300u;
    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xdf00u, 0, false, true, 0x122u);
    expect(state, cpu.cycles == 33, "cpu.cycles == 33");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0x4800u, 0, false, true, 0x124u);
    expect(state, cpu.cycles == 35, "cpu.cycles == 35");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0x5808u, 0, false, true, 0x126u);
    expect(state, cpu.cycles == 37, "cpu.cycles == 37");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xb403u, 0, false, true, 0x128u);
    expect(state, cpu.cycles == 40, "cpu.cycles == 40");

    cpu.registers[15] = 0x400u;
    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xf000u, 0xd000u, true, true, 0x12cu);
    expect(state, cpu.cycles == 43, "cpu.cycles == 43");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xe8d1u, 0x0f4fu, true, true, 0x130u);
    expect(state, cpu.cycles == 45, "cpu.cycles == 45");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xf991u, 0x0000u, true, true, 0x134u);
    expect(state, cpu.cycles == 47, "cpu.cycles == 47");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xf841u, 0x0000u, true, true, 0x138u);
    expect(state, cpu.cycles == 48, "cpu.cycles == 48");

    cpu.registers[15] = 0x500u;
    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xf000u, 0x8000u, true, true, 0x13cu);
    expect(state, cpu.cycles == 51, "cpu.cycles == 51");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xe841u, 0x3200u, true, true, 0x140u);
    expect(state, cpu.cycles == 53, "cpu.cycles == 53");
}

static void test_division_timing(TestState* state) {
    expect(state, cortex_m4_timing_divide_cycles(1, 0, false) == 2,
           "cortex_m4_timing_divide_cycles(1, 0, false) == 2");
    expect(state, cortex_m4_timing_divide_cycles(3, 7, false) == 2,
           "cortex_m4_timing_divide_cycles(3, 7, false) == 2");
    expect(state, cortex_m4_timing_divide_cycles(8, 1, false) == 3,
           "cortex_m4_timing_divide_cycles(8, 1, false) == 3");
    expect(state, cortex_m4_timing_divide_cycles(UINT32_MAX, 1, false) == 12,
           "cortex_m4_timing_divide_cycles(UINT32_MAX, 1, false) == 12");
    expect(state, cortex_m4_timing_divide_cycles(UINT32_C(0x80000000), 1, true) == 12,
           "cortex_m4_timing_divide_cycles(UINT32_C(0x80000000), 1, true) == 12");
    expect(state, cortex_m4_timing_divide_cycles(UINT32_C(0xfffffff8), 2, true) == 2,
           "cortex_m4_timing_divide_cycles(UINT32_C(0xfffffff8), 2, true) == 2");

    CortexM4 cpu;
    TimingFixture fixture;
    reset_cpu(&cpu, &fixture);
    cpu.registers[1] = 8;
    cpu.registers[2] = 1;
    cortex_m4_timing_begin_instruction(&cpu);
    cortex_m4_timing_prepare_instruction(&cpu, 0xfbb1u, 0xf1f2u, true);
    cpu.registers[1] = 0;
    complete(&cpu, 0xfbb1u, 0xf1f2u, true, true, 0x104u);
    expect(state, cpu.cycles == 3, "cpu.cycles == 3");
}

static void issue_store(CortexM4* cpu, TimingFixture* fixture, uint32_t wait_cycles) {
    fixture->store_wait = wait_cycles;
    cortex_m4_timing_begin_instruction(cpu);
    cortex_m4_timing_access(cpu, 0x20000000u, 4, CORTEX_M4_ACCESS_DATA, true);
    complete(cpu, 0x6008u, 0, false, true, 0x102u);
}

static void test_barriers(TestState* state) {
    CortexM4 cpu;
    TimingFixture fixture;
    reset_cpu(&cpu, &fixture);
    issue_store(&cpu, &fixture, 5);
    expect(state, cpu.cycles == 1, "cpu.cycles == 1");
    expect(state, cpu.timing_pending_store_cycles == 5, "cpu.timing_pending_store_cycles == 5");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xbf00u, 0, false, true, 0x104u);
    expect(state, cpu.timing_pending_store_cycles == 4, "cpu.timing_pending_store_cycles == 4");

    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xf3bfu, 0x8f5fu, true, true, 0x108u);
    expect(state, cpu.timing_pending_store_cycles == 0, "cpu.timing_pending_store_cycles == 0");
    expect(state, cpu.timing_memory_epoch == 1, "cpu.timing_memory_epoch == 1");
    expect(state, cpu.cycles == 7, "cpu.cycles == 7");

    fixture.data_wait = 2;
    cortex_m4_timing_begin_instruction(&cpu);
    cortex_m4_timing_access(&cpu, 0x20000004u, 4, CORTEX_M4_ACCESS_DATA, false);
    complete(&cpu, 0x6808u, 0, false, true, 0x10au);
    expect(state, cpu.cycles == 11, "cpu.cycles == 11");
    expect(state, cpu.timing_pending_store_cycles == 0, "cpu.timing_pending_store_cycles == 0");

    issue_store(&cpu, &fixture, 5);
    const uint64_t before_dsb = cpu.cycles;
    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xf3bfu, 0x8f4fu, true, true, 0x10eu);
    expect(state, cpu.cycles - before_dsb == 6, "cpu.cycles - before_dsb == 6");
    expect(state, cpu.timing_pending_store_cycles == 0, "cpu.timing_pending_store_cycles == 0");
    expect(state, cpu.timing_memory_epoch == 2, "cpu.timing_memory_epoch == 2");

    const uint64_t before_isb = cpu.cycles;
    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xf3bfu, 0x8f6fu, true, true, 0x112u);
    expect(state, cpu.cycles - before_isb == 3, "cpu.cycles - before_isb == 3");
    expect(state, cpu.timing_memory_epoch == 2, "cpu.timing_memory_epoch == 2");
    expect(state, cpu.timing_context_epoch == 1, "cpu.timing_context_epoch == 1");
}

static void test_bus_completion(TestState* state) {
    CortexM4 cpu;
    TimingFixture fixture;
    reset_cpu(&cpu, &fixture);
    issue_store(&cpu, &fixture, 5);
    expect(state, cpu.timing_pending_store_bus == CORTEX_M4_TIMING_BUS_SYSTEM,
           "cpu.timing_pending_store_bus == CORTEX_M4_TIMING_BUS_SYSTEM");

    cortex_m4_timing_begin_instruction(&cpu);
    cortex_m4_timing_access(&cpu, 0x100u, 2, CORTEX_M4_ACCESS_INSTRUCTION, false);
    complete(&cpu, 0xbf00u, 0, false, true, 0x102u);
    expect(state, cpu.cycles == 2, "cpu.cycles == 2");
    expect(state, cpu.timing_pending_store_cycles == 4, "cpu.timing_pending_store_cycles == 4");

    cortex_m4_timing_begin_instruction(&cpu);
    cortex_m4_timing_access(&cpu, 0x20000100u, 2, CORTEX_M4_ACCESS_INSTRUCTION, false);
    complete(&cpu, 0xbf00u, 0, false, true, 0x104u);
    expect(state, cpu.cycles == 7, "cpu.cycles == 7");
    expect(state, cpu.timing_pending_store_cycles == 0, "cpu.timing_pending_store_cycles == 0");

    fixture.store_wait = 6;
    cortex_m4_timing_begin_instruction(&cpu);
    cortex_m4_timing_access(&cpu, 0xe000ed04u, 4, CORTEX_M4_ACCESS_DATA, true);
    complete(&cpu, 0x6008u, 0, false, true, 0x106u);
    expect(state, cpu.cycles == 14, "cpu.cycles == 14");
    expect(state, cpu.timing_pending_store_cycles == 0, "cpu.timing_pending_store_cycles == 0");

    reset_cpu(&cpu, &fixture);
    fixture.store_wait = 3;
    cortex_m4_timing_begin_instruction(&cpu);
    cortex_m4_timing_access(&cpu, 0x20000000u, 4, CORTEX_M4_ACCESS_DATA, true);
    cortex_m4_timing_access(&cpu, 0x20000004u, 4, CORTEX_M4_ACCESS_DATA, true);
    complete(&cpu, 0xe881u, 0x0003u, true, true, 0x104u);
    expect(state, cpu.cycles == 6, "cpu.cycles == 6");
    expect(state, cpu.timing_pending_store_cycles == 3, "cpu.timing_pending_store_cycles == 3");

    reset_cpu(&cpu, &fixture);
    issue_store(&cpu, &fixture, 5);
    fixture.store_wait = 2;
    cortex_m4_timing_begin_instruction(&cpu);
    cortex_m4_timing_access(&cpu, 0x20000004u, 4, CORTEX_M4_ACCESS_DATA, true);
    complete(&cpu, 0x6008u, 0, false, true, 0x104u);
    expect(state, cpu.cycles == 7, "cpu.cycles == 7");
    expect(state, cpu.timing_pending_store_cycles == 2, "cpu.timing_pending_store_cycles == 2");
}

static void test_guards(TestState* state) {
    CortexM4 cpu;
    TimingFixture fixture;
    reset_cpu(&cpu, &fixture);

    cortex_m4_set_wait_states(NULL, wait_states, &fixture);
    expect(state, !cortex_m4_set_exclusive_granule(NULL, 0),
           "!cortex_m4_set_exclusive_granule(NULL, 0)");
    cortex_m4_timing_reset(NULL);
    cortex_m4_timing_begin_instruction(NULL);
    cortex_m4_timing_begin_exception(NULL);
    cortex_m4_timing_abort(NULL);
    cortex_m4_timing_prepare_instruction(NULL, 0, 0, false);
    cortex_m4_timing_prepare_instruction(&cpu, 0, 0, false);
    cortex_m4_timing_begin_instruction(&cpu);
    cortex_m4_timing_prepare_instruction(&cpu, 0xbf00u, 0, false);
    cortex_m4_timing_abort(&cpu);
    cortex_m4_timing_access(NULL, 0, 0, CORTEX_M4_ACCESS_DATA, false);
    cortex_m4_timing_access(&cpu, 0, 0, CORTEX_M4_ACCESS_DATA, false);
    cortex_m4_timing_barrier(NULL, CORTEX_M4_TIMING_BARRIER_DMB);
    cortex_m4_timing_complete_instruction(NULL, 0, 0, false, false, 0);
    cortex_m4_timing_complete_instruction(&cpu, 0, 0, false, false, 0);
    cortex_m4_timing_exception(NULL, CORTEX_M4_TIMING_EXCEPTION_ENTRY);
    cortex_m4_timing_sleep(NULL, 1);
    cortex_m4_timing_sleep(&cpu, 0);
    cortex_m4_timing_reserve(NULL, 0, 4);
    cortex_m4_timing_reserve(&cpu, 0, 3);
    expect(state, !cortex_m4_timing_consume_reservation(NULL, 0, 4),
           "!cortex_m4_timing_consume_reservation(NULL, 0, 4)");
    cortex_m4_timing_observe_write(NULL, 0, 1);
    cortex_m4_timing_observe_write(&cpu, 0, 0);
    cortex_m4_notify_external_write(NULL, 0, 1);

    cpu.registers[1] = 8;
    cpu.registers[2] = 1;
    cortex_m4_timing_begin_instruction(&cpu);
    complete(&cpu, 0xfbb1u, 0xf0f2u, true, true, 0x104u);
    expect(state, cpu.cycles == 3, "cpu.cycles == 3");
}

static void test_exclusive_monitor(TestState* state) {
    CortexM4 cpu;
    TimingFixture fixture;
    reset_cpu(&cpu, &fixture);
    expect(state, cpu.exclusive_granule == 0, "cpu.exclusive_granule == 0");
    expect(state, !cortex_m4_set_exclusive_granule(&cpu, 3),
           "!cortex_m4_set_exclusive_granule(&cpu, 3)");
    expect(state, !cortex_m4_set_exclusive_granule(&cpu, 4096),
           "!cortex_m4_set_exclusive_granule(&cpu, 4096)");
    expect(state, cortex_m4_set_exclusive_granule(&cpu, 32),
           "cortex_m4_set_exclusive_granule(&cpu, 32)");

    cortex_m4_timing_reserve(&cpu, 0x20000104u, 4);
    cortex_m4_notify_external_write(&cpu, 0x20000120u, 4);
    expect(state, cortex_m4_timing_consume_reservation(&cpu, 0x20000104u, 4),
           "cortex_m4_timing_consume_reservation(&cpu, 0x20000104u, 4)");

    cortex_m4_timing_reserve(&cpu, 0x20000104u, 4);
    cortex_m4_notify_external_write(&cpu, 0x2000011fu, 1);
    expect(state, !cortex_m4_timing_consume_reservation(&cpu, 0x20000104u, 4),
           "!cortex_m4_timing_consume_reservation(&cpu, 0x20000104u, 4)");

    cortex_m4_timing_reserve(&cpu, 0x20000104u, 4);
    expect(state, !cortex_m4_timing_consume_reservation(&cpu, 0x20000108u, 4),
           "!cortex_m4_timing_consume_reservation(&cpu, 0x20000108u, 4)");
    expect(state, !cpu.exclusive_valid, "!cpu.exclusive_valid");

    expect(state, cortex_m4_set_exclusive_granule(&cpu, 0),
           "cortex_m4_set_exclusive_granule(&cpu, 0)");
    cortex_m4_timing_reserve(&cpu, 0x20000104u, 4);
    expect(state, cortex_m4_timing_consume_reservation(&cpu, 0x40000000u, 4),
           "cortex_m4_timing_consume_reservation(&cpu, 0x40000000u, 4)");
    cortex_m4_timing_reserve(&cpu, 0x20000104u, 4);
    cortex_m4_notify_external_write(&cpu, 0x40000000u, 1);
    expect(state, !cpu.exclusive_valid, "!cpu.exclusive_valid");

    cortex_m4_timing_reserve(&cpu, 0x20000104u, 4);
    cortex_m4_timing_observe_write(&cpu, 0x20000100u, 4);
    expect(state, !cpu.exclusive_valid, "!cpu.exclusive_valid");
}

static void test_exception_and_sleep_timing(TestState* state) {
    CortexM4 cpu;
    TimingFixture fixture;
    reset_cpu(&cpu, &fixture);
    cortex_m4_timing_reserve(&cpu, 0x20000000u, 4);
    fixture.data_wait = 2;
    fixture.store_wait = 3;
    fixture.instruction_wait = 4;
    cortex_m4_timing_begin_exception(&cpu);
    cortex_m4_timing_access(&cpu, 0x20000000u, 4, CORTEX_M4_ACCESS_DATA, true);
    cortex_m4_timing_access(&cpu, 0x00000040u, 4, CORTEX_M4_ACCESS_INSTRUCTION, false);
    cortex_m4_timing_exception(&cpu, CORTEX_M4_TIMING_EXCEPTION_ENTRY);
    expect(state, cpu.cycles == 19, "cpu.cycles == 19");
    expect(state, !cpu.exclusive_valid, "!cpu.exclusive_valid");

    cortex_m4_timing_exception(&cpu, CORTEX_M4_TIMING_EXCEPTION_LATE_ARRIVAL);
    expect(state, cpu.cycles == 31, "cpu.cycles == 31");
    cortex_m4_timing_exception(&cpu, CORTEX_M4_TIMING_EXCEPTION_TAIL_CHAIN);
    expect(state, cpu.cycles == 37, "cpu.cycles == 37");
    cortex_m4_timing_exception(&cpu, CORTEX_M4_TIMING_EXCEPTION_RETURN);
    expect(state, cpu.cycles == 47, "cpu.cycles == 47");

    cortex_m4_timing_exception(&cpu, CORTEX_M4_TIMING_EXCEPTION_FP_ENTRY);
    expect(state, cpu.cycles == 76, "cpu.cycles == 76");
    cortex_m4_timing_exception(&cpu, CORTEX_M4_TIMING_EXCEPTION_FP_RETURN);
    expect(state, cpu.cycles == 103, "cpu.cycles == 103");

    cpu.timing_pending_store_cycles = 7;
    cortex_m4_timing_sleep(&cpu, 3);
    expect(state, cpu.cycles == 106, "cpu.cycles == 106");
    expect(state, cpu.timing_pending_store_cycles == 4, "cpu.timing_pending_store_cycles == 4");
    cortex_m4_timing_sleep(&cpu, 4);
    expect(state, cpu.cycles == 110, "cpu.cycles == 110");
    expect(state, cpu.timing_pending_store_cycles == 0, "cpu.timing_pending_store_cycles == 0");
}

int main(void) {
    TestState state = {0};
    test_wait_states(&state);
    test_instruction_classes(&state);
    test_division_timing(&state);
    test_barriers(&state);
    test_bus_completion(&state);
    test_guards(&state);
    test_exclusive_monitor(&state);
    test_exception_and_sleep_timing(&state);
    return test_finish(&state);
}
