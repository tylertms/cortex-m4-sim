#include "cortex_m4_internal.h"

#include <string.h>

#include "test.h"

typedef struct {
    uint8_t memory[4096];
    CortexM4* cpu;
    uint32_t interrupt_address;
    bool reject_reads;
    bool inject_interrupt;
} TestBus;

static bool bus_read(void* context, uint32_t address, uint8_t size,
                     CortexM4Access access, uint32_t* value) {
    TestBus* bus = context;
    (void)access;
    if (bus->reject_reads || address + size > sizeof(bus->memory)) {
        return false;
    }
    *value = 0;
    for (uint8_t index = 0; index < size; index++) {
        *value |= (uint32_t)bus->memory[address + index] << (index * 8u);
    }
    if (bus->inject_interrupt && address == bus->interrupt_address) {
        bus->inject_interrupt = false;
        cortex_m4_set_irq(bus->cpu, 0u, true);
    }
    return true;
}

static bool bus_write(void* context, uint32_t address, uint8_t size,
                      CortexM4Access access, uint32_t value) {
    TestBus* bus = context;
    (void)access;
    if (address + size > sizeof(bus->memory)) {
        return false;
    }
    for (uint8_t index = 0; index < size; index++) {
        bus->memory[address + index] = (uint8_t)(value >> (index * 8u));
    }
    return true;
}

static CortexM4* create_cpu(TestState* state, TestBus* bus) {
    CortexM4Bus interface = {bus, bus_read, bus_write, NULL, NULL};
    CortexM4* cpu = cortex_m4_create(interface);
    TEST_EXPECT(state, cpu != NULL);
    if (cpu != NULL) {
        cortex_m4_system_reset(cpu);
        cpu->xpsr = CORTEX_M4_XPSR_T;
        bus->cpu = cpu;
    }
    return cpu;
}

static void write_vector(TestBus* bus, uint16_t exception, uint32_t vector) {
    const uint32_t address = (uint32_t)exception * 4u;
    for (uint8_t index = 0; index < 4u; index++) {
        bus->memory[address + index] = (uint8_t)(vector >> (index * 8u));
    }
}

static void prepare_frame(CortexM4* cpu, uint32_t return_value) {
    CortexM4ExceptionFrame* frame = &cpu->exception_frames[cpu->exception_frame_depth++];
    memset(frame, 0, sizeof(*frame));
    frame->return_value = return_value;
}

static void test_active_stack(TestState* state) {
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cortex_m4_exception_advanced_reset(cpu);
    TEST_EXPECT(state, !cortex_m4_exception_advanced_active(cpu, 16u));
    cortex_m4_exception_advanced_commit_entry(cpu, 16u);
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    cortex_m4_exception_advanced_commit_entry(cpu, 17u);
    cpu->xpsr = CORTEX_M4_XPSR_T | 17u;
    TEST_EXPECT(state, cpu->exception_depth == 2u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_active(cpu, 16u));
    TEST_EXPECT(state, cortex_m4_exception_advanced_active(cpu, 17u));
    TEST_EXPECT(state, (cpu->irq_active[0] & 3u) == 3u);
    cpu->faultmask = 1u;
    cortex_m4_exception_advanced_commit_return(cpu, 17u, false);
    TEST_EXPECT(state, cpu->exception_depth == 1u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_active(cpu, 16u));
    TEST_EXPECT(state, !cortex_m4_exception_advanced_active(cpu, 17u));
    TEST_EXPECT(state, (cpu->irq_active[0] & 2u) == 0u);
    TEST_EXPECT(state, cpu->faultmask == 0u);
    cpu->irq_level[0] |= 1u;
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    cortex_m4_exception_advanced_commit_return(cpu, 16u, true);
    TEST_EXPECT(state, (cpu->irq_pending[0] & 1u) != 0u);
    cpu->cfsr = 0;
    cortex_m4_exception_advanced_commit_return(cpu, 17u, false);
    TEST_EXPECT(state, (cpu->cfsr & (1u << 18)) != 0u);
    cortex_m4_destroy(cpu);
}

static void test_late_arrival(TestState* state) {
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->irq_enabled[0] = 7u;
    cpu->irq_pending[0] = 7u;
    cpu->irq_priority[0] = 0x80u;
    cpu->irq_priority[1] = 0x10u;
    cpu->irq_priority[2] = 0xc0u;
    TEST_EXPECT(state,
                cortex_m4_exception_advanced_late_arrival(cpu, 16u) == 17u);
    TEST_EXPECT(state, cpu->cycles == 0u);
    cpu->primask = 1u;
    TEST_EXPECT(state,
                cortex_m4_exception_advanced_late_arrival(cpu, 16u) == 16u);
    TEST_EXPECT(state,
                cortex_m4_exception_advanced_late_arrival(NULL, 16u) == 16u);
    cpu->primask = 0u;
    cpu->system_pending = 1u << 15;
    cpu->system_priority[11] = 0xe0u;
    cpu->irq_pending[0] = 2u;
    TEST_EXPECT(state,
                cortex_m4_exception_advanced_late_arrival(cpu, 15u) == 17u);
    cpu->irq_priority[3] = 0xf0u;
    TEST_EXPECT(state,
                cortex_m4_exception_advanced_late_arrival(cpu, 19u) == 17u);
    cortex_m4_destroy(cpu);
}

static void test_tail_chain(TestState* state) {
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->irq_enabled[0] = 3u;
    cpu->irq_priority[0] = 0x80u;
    cpu->irq_priority[1] = 0x20u;
    cpu->irq_pending[0] = 1u;
    cortex_m4_exception_advanced_commit_entry(cpu, 16u);
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    prepare_frame(cpu, 0xfffffff9u);
    write_vector(&bus, 17u, 0x301u);
    cpu->irq_pending[0] |= 2u;
    cpu->faultmask = 1u;
    cpu->faultmask = 0u;
    TEST_EXPECT(state,
                cortex_m4_exception_advanced_tail_chain(cpu, 0xfffffff9u, 16u) ==
                    CORTEX_M4_EXCEPTION_CHAIN_TAKEN);
    TEST_EXPECT(state, cpu->exception_depth == 1u);
    TEST_EXPECT(state, !cortex_m4_exception_advanced_active(cpu, 16u));
    TEST_EXPECT(state, cortex_m4_exception_advanced_active(cpu, 17u));
    TEST_EXPECT(state, cpu->registers[15] == 0x300u);
    TEST_EXPECT(state, cpu->registers[14] == 0xfffffff9u);
    TEST_EXPECT(state, (cpu->xpsr & 0x1ffu) == 17u);
    TEST_EXPECT(state, (cpu->irq_active[0] & 2u) != 0u);
    TEST_EXPECT(state, (cpu->irq_pending[0] & 2u) == 0u);
    TEST_EXPECT(state,
                cortex_m4_exception_advanced_tail_chain(cpu, 0xfffffff9u, 17u) ==
                    CORTEX_M4_EXCEPTION_CHAIN_NONE);
    cortex_m4_destroy(cpu);
}

static void test_tail_chain_vector_fault(TestState* state) {
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->irq_enabled[0] = 3u;
    cpu->irq_pending[0] = 1u;
    cortex_m4_exception_advanced_commit_entry(cpu, 16u);
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    prepare_frame(cpu, 0xfffffff9u);
    cpu->irq_pending[0] |= 2u;
    write_vector(&bus, 17u, 0x300u);
    write_vector(&bus, 3u, 0x701u);
    TEST_EXPECT(state,
                cortex_m4_exception_advanced_tail_chain(cpu, 0xfffffff9u, 16u) ==
                    CORTEX_M4_EXCEPTION_CHAIN_TAKEN);
    TEST_EXPECT(state, (cpu->hfsr & (1u << 1)) != 0u);
    TEST_EXPECT(state, (cpu->system_pending & (1u << 3)) == 0u);
    TEST_EXPECT(state, !cortex_m4_exception_advanced_active(cpu, 16u));
    TEST_EXPECT(state, cortex_m4_exception_advanced_active(cpu, 3u));
    TEST_EXPECT(state, cpu->registers[15] == 0x700u);
    cortex_m4_destroy(cpu);

    memset(&bus, 0, sizeof(bus));
    cpu = create_cpu(state, &bus);
    cpu->irq_enabled[0] = 3u;
    cpu->irq_pending[0] = 1u;
    cortex_m4_exception_advanced_commit_entry(cpu, 16u);
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    prepare_frame(cpu, 0xfffffff9u);
    cpu->irq_pending[0] |= 2u;
    bus.reject_reads = true;
    TEST_EXPECT(state,
                cortex_m4_exception_advanced_tail_chain(cpu, 0xfffffff9u, 16u) ==
                    CORTEX_M4_EXCEPTION_CHAIN_FAULT);
    bus.reject_reads = false;
    cpu->xpsr = CORTEX_M4_XPSR_T | 3u;
    cortex_m4_exception_advanced_vector_fault(cpu);
    TEST_EXPECT(state, cpu->stop == CORTEX_M4_STOP_LOCKUP);
    cortex_m4_destroy(cpu);
}

static void test_return_validation(TestState* state) {
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->irq_pending[0] = 1u;
    cortex_m4_exception_advanced_commit_entry(cpu, 16u);
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    prepare_frame(cpu, 0xfffffff9u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_valid_return(cpu, 0xfffffff9u));
    TEST_EXPECT(state,
                !cortex_m4_exception_advanced_valid_return(cpu, 0xfffffffdu));
    TEST_EXPECT(state,
                !cortex_m4_exception_advanced_valid_return(cpu, 0xfffffff8u));
    TEST_EXPECT(state, cortex_m4_exception_advanced_valid_stacked_xpsr(
                           cpu, CORTEX_M4_XPSR_T, 0xfffffff9u));
    TEST_EXPECT(state, !cortex_m4_exception_advanced_valid_stacked_xpsr(
                           cpu, 0u, 0xfffffff9u));
    cpu->exception_frame_depth = 0u;
    TEST_EXPECT(state, !cortex_m4_exception_advanced_valid_stacked_xpsr(
                           cpu, CORTEX_M4_XPSR_T | 0x4000u, 0xfffffff9u));
    cpu->exception_frame_depth = 1u;
    cpu->exception_frames[0].ici_valid = false;
    TEST_EXPECT(state, !cortex_m4_exception_advanced_valid_stacked_xpsr(
                           cpu, CORTEX_M4_XPSR_T | 0x4000u, 0xfffffff9u));
    cpu->irq_pending[0] = 2u;
    cortex_m4_exception_advanced_commit_entry(cpu, 17u);
    cpu->xpsr = CORTEX_M4_XPSR_T | 17u;
    prepare_frame(cpu, 0xfffffff1u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_valid_return(cpu, 0xfffffff1u));
    TEST_EXPECT(state,
                cortex_m4_exception_advanced_tail_chain(cpu, 0xfffffff1u, 17u) ==
                    CORTEX_M4_EXCEPTION_CHAIN_NONE);
    TEST_EXPECT(state,
                !cortex_m4_exception_advanced_valid_return(cpu, 0xfffffff9u));
    cpu->ccr |= 1u;
    TEST_EXPECT(state, cortex_m4_exception_advanced_valid_return(cpu, 0xfffffff9u));
    TEST_EXPECT(state, cortex_m4_exception_advanced_valid_stacked_xpsr(
                           cpu, CORTEX_M4_XPSR_T | 16u, 0xfffffff1u));
    TEST_EXPECT(state, !cortex_m4_exception_advanced_valid_stacked_xpsr(
                           cpu, CORTEX_M4_XPSR_T | 18u, 0xfffffff1u));
    TEST_EXPECT(state,
                cortex_m4_exception_advanced_tail_chain(cpu, 0xfffffffdu, 17u) ==
                    CORTEX_M4_EXCEPTION_CHAIN_FAULT);
    cortex_m4_destroy(cpu);
}

static void test_nested_tail_chain(TestState* state) {
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->irq_enabled[0] = 7u;
    cpu->irq_pending[0] = 1u;
    cpu->irq_priority[0] = 0x80u;
    cpu->irq_priority[1] = 0x40u;
    cpu->irq_priority[2] = 0x10u;
    cortex_m4_exception_advanced_commit_entry(cpu, 16u);
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    prepare_frame(cpu, 0xfffffff9u);
    cpu->irq_pending[0] = 2u;
    cortex_m4_exception_advanced_commit_entry(cpu, 17u);
    cpu->xpsr = CORTEX_M4_XPSR_T | 17u;
    prepare_frame(cpu, 0xfffffff1u);
    cpu->irq_pending[0] = 5u;
    cpu->ccr |= 1u;
    write_vector(&bus, 18u, 0x501u);
    TEST_EXPECT(state,
                cortex_m4_exception_advanced_tail_chain(cpu, 0xfffffff9u, 17u) ==
                    CORTEX_M4_EXCEPTION_CHAIN_TAKEN);
    TEST_EXPECT(state, cpu->exception_depth == 2u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_active(cpu, 16u));
    TEST_EXPECT(state, !cortex_m4_exception_advanced_active(cpu, 17u));
    TEST_EXPECT(state, cortex_m4_exception_advanced_active(cpu, 18u));
    TEST_EXPECT(state, cpu->registers[15] == 0x500u);
    cortex_m4_destroy(cpu);
}

static void test_entry_vector_fault_reuses_frame(TestState* state) {
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->msp = 0x800u;
    cpu->registers[15] = 0x100u;
    cpu->irq_enabled[0] = 1u;
    cpu->irq_pending[0] = 1u;
    write_vector(&bus, 3u, 0x701u);
    write_vector(&bus, 16u, 0x300u);
    TEST_EXPECT(state, cortex_m4_take_pending_exception(cpu));
    TEST_EXPECT(state, cpu->msp == 0x7e0u);
    TEST_EXPECT(state, cpu->exception_frame_depth == 1u);
    TEST_EXPECT(state, cpu->exception_depth == 1u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_active(cpu, 3u));
    TEST_EXPECT(state, !cortex_m4_exception_advanced_active(cpu, 16u));
    TEST_EXPECT(state, cpu->registers[15] == 0x700u);
    TEST_EXPECT(state, (cpu->hfsr & (1u << 1)) != 0u);
    cortex_m4_destroy(cpu);
}

static void test_fault_metadata(TestState* state) {
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->shcsr = (1u << 16) | (1u << 17);
    cortex_m4_exception_advanced_fault(cpu, CORTEX_M4_FAULT_STACKING, true);
    TEST_EXPECT(state, (cpu->cfsr & (1u << 4)) != 0u);
    TEST_EXPECT(state, (cpu->system_pending & (1u << 4)) != 0u);
    cortex_m4_exception_advanced_fault(cpu, CORTEX_M4_FAULT_UNSTACKING, false);
    TEST_EXPECT(state, (cpu->cfsr & (1u << 11)) != 0u);
    TEST_EXPECT(state, (cpu->system_pending & (1u << 5)) != 0u);
    cortex_m4_exception_advanced_fault(cpu, CORTEX_M4_FAULT_LAZY_FP, true);
    TEST_EXPECT(state, (cpu->cfsr & (1u << 5)) != 0u);
    cortex_m4_exception_advanced_fault(cpu, CORTEX_M4_FAULT_LAZY_FP, false);
    TEST_EXPECT(state, (cpu->cfsr & (1u << 13)) != 0u);
    cpu->bfar = 0x12345678u;
    cortex_m4_exception_advanced_imprecise_fault(cpu);
    TEST_EXPECT(state, (cpu->cfsr & (1u << 10)) != 0u);
    TEST_EXPECT(state, cpu->bfar == 0x12345678u);
    const uint32_t status = cpu->cfsr;
    cortex_m4_exception_advanced_fault(cpu, (CortexM4ExceptionFaultStage)3, true);
    TEST_EXPECT(state, cpu->cfsr == status);
    cpu->system_pending = 0u;
    cpu->hfsr = 0u;
    cortex_m4_exception_advanced_entry_fault(
        cpu, 4u, CORTEX_M4_FAULT_STACKING, true);
    TEST_EXPECT(state, (cpu->hfsr & (1u << 30)) != 0u);
    TEST_EXPECT(state, (cpu->system_pending & (1u << 3)) != 0u);
    TEST_EXPECT(state, (cpu->system_pending & (1u << 4)) == 0u);
    cpu->system_pending = 0u;
    cpu->hfsr = 0u;
    cortex_m4_exception_advanced_entry_fault(
        cpu, 16u, CORTEX_M4_FAULT_STACKING, true);
    TEST_EXPECT(state, (cpu->system_pending & (1u << 4)) != 0u);
    cpu->stop = CORTEX_M4_STOP_RUNNING;
    cortex_m4_exception_advanced_entry_fault(
        cpu, 3u, CORTEX_M4_FAULT_STACKING, false);
    TEST_EXPECT(state, cpu->stop == CORTEX_M4_STOP_LOCKUP);
    cortex_m4_destroy(cpu);
}

static void test_unstack_failure_classification(TestState* state) {
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->irq_pending[0] = 1u;
    cortex_m4_exception_advanced_commit_entry(cpu, 16u);
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    prepare_frame(cpu, 0xfffffff9u);
    uint32_t stack_pointer = 0x200u;
    TEST_EXPECT(state, !cortex_m4_system_unstack_exception_frame(
                           cpu, &stack_pointer, 0xfffffff9u, 16u));
    TEST_EXPECT(state, !cpu->exception_unstack_memory_fault);
    bus.reject_reads = true;
    cpu->cfsr = 0u;
    TEST_EXPECT(state, !cortex_m4_system_unstack_exception_frame(
                           cpu, &stack_pointer, 0xfffffff9u, 16u));
    TEST_EXPECT(state, cpu->exception_unstack_memory_fault);
    TEST_EXPECT(state, cpu->cfsr == 0u);
    cortex_m4_destroy(cpu);
}

static void test_ici_state(TestState* state) {
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->registers[15] = 0x102u;
    TEST_EXPECT(state,
                !cortex_m4_exception_advanced_multiple_suspend(cpu, 4u, 2u,
                                                               0x204u));
    cpu->irq_enabled[0] = 1u;
    cpu->irq_pending[0] = 1u;
    TEST_EXPECT(state,
                cortex_m4_exception_advanced_multiple_suspend(cpu, 4u, 2u,
                                                              0x204u));
    TEST_EXPECT(state, cpu->registers[15] == 0x100u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_multiple_resume(cpu) == 4u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_multiple_address(cpu, 0u) ==
                           0x204u);
    const uint32_t encoded =
        cortex_m4_exception_advanced_xpsr(cpu, CORTEX_M4_XPSR_T | 0x0600fc00u);
    TEST_EXPECT(state, (encoded & 0x06000c00u) == 0u);
    TEST_EXPECT(state, (encoded & 0x0000f000u) == 0x00004000u);
    cpu->exception_frame_depth = 1u;
    cpu->exception_frames[0].ici_address = 0x204u;
    cpu->exception_frames[0].ici_register = 4u;
    cpu->exception_frames[0].ici_valid = true;
    cortex_m4_exception_advanced_commit_entry(cpu, 16u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_multiple_resume(cpu) == 0u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_xpsr(cpu, 0x12345678u) ==
                           0x12345678u);
    cortex_m4_exception_advanced_load_xpsr(cpu, encoded);
    TEST_EXPECT(state, cortex_m4_exception_advanced_multiple_resume(cpu) == 4u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_multiple_address(cpu, 0u) ==
                           0x204u);
    TEST_EXPECT(state, cpu->it_state == 0u);
    cortex_m4_exception_advanced_multiple_complete(cpu);
    TEST_EXPECT(state, cortex_m4_exception_advanced_multiple_resume(cpu) == 0u);
    TEST_EXPECT(state,
                !cortex_m4_exception_advanced_multiple_suspend(cpu, 0u, 2u, 0u));
    TEST_EXPECT(state,
                !cortex_m4_exception_advanced_multiple_suspend(cpu, 4u, 3u, 0u));
    cortex_m4_exception_advanced_load_xpsr(cpu, 0x02000800u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_multiple_resume(cpu) == 0u);
    cortex_m4_destroy(cpu);
}

static void test_ici_execution_address(TestState* state) {
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->msp = 0x800u;
    cpu->registers[0] = 0x400u;
    cpu->registers[15] = 0x100u;
    cpu->irq_enabled[0] = 1u;
    write_vector(&bus, 16u, 0x301u);
    bus.memory[0x100u] = 0x03u;
    bus.memory[0x101u] = 0xc8u;
    bus.memory[0x300u] = 0x70u;
    bus.memory[0x301u] = 0x47u;
    bus.memory[0x400u] = 0x00u;
    bus.memory[0x401u] = 0x09u;
    bus.memory[0x404u] = 0xbbu;
    bus.memory[0x405u] = 0xaau;
    bus.interrupt_address = 0x400u;
    bus.inject_interrupt = true;
    cortex_m4_step(cpu);
    TEST_EXPECT(state, cpu->registers[0] == 0x900u);
    TEST_EXPECT(state, cpu->registers[15] == 0x100u);
    TEST_EXPECT(state, cpu->ici_valid);
    TEST_EXPECT(state, cpu->ici_address == 0x404u);
    cortex_m4_step(cpu);
    TEST_EXPECT(state, cpu->registers[15] == 0x100u);
    TEST_EXPECT(state, cpu->ici_valid);
    TEST_EXPECT(state, cpu->ici_address == 0x404u);
    cortex_m4_step(cpu);
    TEST_EXPECT(state, cpu->registers[0] == 0x900u);
    TEST_EXPECT(state, cpu->registers[1] == 0xaabbu);
    TEST_EXPECT(state, cpu->registers[15] == 0x102u);
    TEST_EXPECT(state, !cpu->ici_valid);
    cortex_m4_destroy(cpu);
}

static void test_boundaries(TestState* state) {
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    TEST_EXPECT(state, !cortex_m4_exception_advanced_active(NULL, 16u));
    TEST_EXPECT(state, !cortex_m4_exception_advanced_active(cpu, 1u));
    cortex_m4_exception_advanced_reset(NULL);
    cortex_m4_exception_advanced_commit_entry(NULL, 16u);
    cortex_m4_exception_advanced_commit_entry(cpu, 1u);
    TEST_EXPECT(state, cpu->exception_depth == 0u);
    cpu->exception_depth = CORTEX_M4_EXCEPTION_FRAME_LIMIT;
    cortex_m4_exception_advanced_commit_entry(cpu, 16u);
    TEST_EXPECT(state, cpu->stop == CORTEX_M4_STOP_LOCKUP);
    cortex_m4_exception_advanced_reset(cpu);
    TEST_EXPECT(state, !cortex_m4_exception_advanced_valid_return(NULL, 0xfffffff9u));
    TEST_EXPECT(state, !cortex_m4_exception_advanced_valid_return(cpu, 0xfffffff9u));
    cortex_m4_exception_advanced_fault(NULL, CORTEX_M4_FAULT_STACKING, true);
    cortex_m4_exception_advanced_entry_fault(
        NULL, 16u, CORTEX_M4_FAULT_STACKING, true);
    cortex_m4_exception_advanced_vector_fault(NULL);
    TEST_EXPECT(state,
                !cortex_m4_exception_advanced_hardfault_vector(NULL, NULL));
    cortex_m4_exception_advanced_imprecise_fault(NULL);
    cortex_m4_exception_advanced_multiple_complete(NULL);
    cortex_m4_exception_advanced_load_xpsr(NULL, 0u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_multiple_resume(NULL) == 0u);
    TEST_EXPECT(state, cortex_m4_exception_advanced_xpsr(NULL, 0x55u) == 0x55u);
    cortex_m4_destroy(cpu);
}

static void test_faultmask_and_sleep(TestState* state) {
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->system_pending = 1u << 2;
    cortex_m4_exception_advanced_commit_entry(cpu, 2u);
    cpu->faultmask = 1u;
    cpu->xpsr = CORTEX_M4_XPSR_T | 2u;
    cortex_m4_exception_advanced_commit_return(cpu, 2u, true);
    TEST_EXPECT(state, cpu->faultmask == 1u);
    cpu->irq_pending[0] = 1u;
    cortex_m4_exception_advanced_commit_entry(cpu, 16u);
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    cpu->scr = 1u << 1;
    cpu->faultmask = 1u;
    cortex_m4_exception_advanced_commit_return(cpu, 16u, true);
    TEST_EXPECT(state, cpu->faultmask == 0u);
    TEST_EXPECT(state, cpu->sleeping);
    cortex_m4_destroy(cpu);

    memset(&bus, 0, sizeof(bus));
    cpu = create_cpu(state, &bus);
    cpu->msp = 0x800u;
    cpu->faultmask = 1u;
    cpu->system_pending = 1u << 3;
    write_vector(&bus, 2u, 0x301u);
    write_vector(&bus, 3u, 0x301u);
    TEST_EXPECT(state, !cortex_m4_take_pending_exception(cpu));
    cpu->system_pending |= 1u << 2;
    TEST_EXPECT(state, cortex_m4_take_pending_exception(cpu));
    TEST_EXPECT(state, cortex_m4_exception_advanced_active(cpu, 2u));
    cortex_m4_destroy(cpu);
}

int main(void) {
    TestState state = {0};
    test_active_stack(&state);
    test_late_arrival(&state);
    test_tail_chain(&state);
    test_tail_chain_vector_fault(&state);
    test_return_validation(&state);
    test_nested_tail_chain(&state);
    test_entry_vector_fault_reuses_frame(&state);
    test_fault_metadata(&state);
    test_unstack_failure_classification(&state);
    test_ici_state(&state);
    test_ici_execution_address(&state);
    test_faultmask_and_sleep(&state);
    test_boundaries(&state);
    return test_finish(&state);
}
