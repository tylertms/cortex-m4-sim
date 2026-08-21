#include "cortex_m4_internal.h"

#include <string.h>

#include "test.h"

typedef struct {
    uint8_t memory[4096];
    bool reject_reads;
    bool reject_writes;
} ExceptionBus;

static bool bus_read(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                     uint32_t* value) {
    ExceptionBus* bus = context;
    (void)access;
    if (bus->reject_reads || (uint64_t)address + size > sizeof(bus->memory)) {
        return false;
    }
    *value = 0u;
    memcpy(value, bus->memory + address, size);
    return true;
}

static bool bus_write(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                      uint32_t value) {
    ExceptionBus* bus = context;
    (void)access;
    if (bus->reject_writes || (uint64_t)address + size > sizeof(bus->memory)) {
        return false;
    }
    memcpy(bus->memory + address, &value, size);
    return true;
}

static CortexM4* create_cpu(TestState* state, ExceptionBus* bus) {
    CortexM4* cpu = cortex_m4_create((CortexM4Bus){bus, bus_read, bus_write, NULL, NULL});
    expect(state, cpu != NULL, "cpu != NULL");
    cortex_m4_system_reset(cpu);
    cpu->xpsr = CORTEX_M4_XPSR_T;
    cpu->msp = 0x800u;
    cpu->psp = 0x900u;
    cpu->registers[15] = 0x100u;
    return cpu;
}

static void write_vector(ExceptionBus* bus, uint16_t exception, uint32_t vector) {
    memcpy(bus->memory + exception * 4u, &vector, sizeof(vector));
}

static void prepare_active(CortexM4* cpu, uint16_t exception, uint32_t return_value,
                           uint32_t stack_pointer) {
    cpu->irq_pending[(exception - 16u) / 32u] |= 1u << ((exception - 16u) & 31u);
    cortex_m4_exception_advanced_commit_entry(cpu, exception);
    cpu->xpsr = CORTEX_M4_XPSR_T | exception;
    cpu->msp = stack_pointer;
    CortexM4ExceptionFrame* frame = &cpu->exception_frames[cpu->exception_frame_depth++];
    memset(frame, 0, sizeof(*frame));
    frame->address = stack_pointer;
    frame->return_value = return_value;
}

static void test_fault_lockup(TestState* state) {
    ExceptionBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->xpsr = CORTEX_M4_XPSR_T | 3u;
    cortex_m4_raise_fault(cpu, 4u);
    expect(state, cpu->stop == CORTEX_M4_STOP_LOCKUP, "cpu->stop == CORTEX_M4_STOP_LOCKUP");
    cortex_m4_destroy(cpu);
}

static void test_psp_lifecycle(TestState* state) {
    ExceptionBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->control = CORTEX_M4_CONTROL_SPSEL;
    cpu->irq_enabled[0] = 1u;
    cpu->irq_pending[0] = 1u;
    write_vector(&bus, 16u, 0x301u);
    expect(state, cortex_m4_take_pending_exception(cpu),
           "cortex_m4_take_pending_exception(cpu)");
    expect(state, cpu->psp == 0x8e0u, "cpu->psp == 0x8e0u");
    expect(state, cpu->registers[14] == 0xfffffffdu, "cpu->registers[14] == 0xfffffffdu");
    expect(state, cortex_m4_exception_return(cpu, cpu->registers[14]),
           "cortex_m4_exception_return(cpu, cpu->registers[14])");
    expect(state, cpu->psp == 0x900u, "cpu->psp == 0x900u");
    expect(state, (cpu->xpsr & 0x1ffu) == 0u, "(cpu->xpsr & 0x1ffu) == 0u");
    cortex_m4_destroy(cpu);
}

static void test_entry_failures(TestState* state) {
    ExceptionBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->irq_enabled[0] = 1u;
    cpu->irq_pending[0] = 1u;
    write_vector(&bus, 16u, 0x301u);
    bus.reject_writes = true;
    expect(state, !cortex_m4_take_pending_exception(cpu),
           "!cortex_m4_take_pending_exception(cpu)");
    expect(state, (cpu->cfsr & (1u << 12)) != 0u, "(cpu->cfsr & (1u << 12)) != 0u");
    cortex_m4_destroy(cpu);

    memset(&bus, 0, sizeof(bus));
    cpu = create_cpu(state, &bus);
    cpu->irq_enabled[0] = 1u;
    cpu->irq_pending[0] = 1u;
    bus.reject_reads = true;
    expect(state, !cortex_m4_take_pending_exception(cpu),
           "!cortex_m4_take_pending_exception(cpu)");
    expect(state, (cpu->hfsr & (1u << 1)) != 0u, "(cpu->hfsr & (1u << 1)) != 0u");
    cortex_m4_destroy(cpu);
}

static void test_exception_selection(TestState* state) {
    ExceptionBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    cpu->system_pending = (1u << 14) | (1u << 15);
    cpu->system_priority[10] = 0x80u;
    cpu->system_priority[11] = 0x10u;
    write_vector(&bus, 14u, 0x301u);
    write_vector(&bus, 15u, 0x321u);
    expect(state, cortex_m4_take_pending_exception(cpu),
           "cortex_m4_take_pending_exception(cpu)");
    expect(state, (cpu->xpsr & 0x1ffu) == 15u, "(cpu->xpsr & 0x1ffu) == 15u");
    cortex_m4_destroy(cpu);

    memset(&bus, 0, sizeof(bus));
    cpu = create_cpu(state, &bus);
    cpu->irq_enabled[0] = 3u;
    cpu->irq_pending[0] = 3u;
    cpu->irq_priority[0] = 0x80u;
    cpu->irq_priority[1] = 0x10u;
    write_vector(&bus, 16u, 0x301u);
    write_vector(&bus, 17u, 0x321u);
    expect(state, cortex_m4_take_pending_exception(cpu),
           "cortex_m4_take_pending_exception(cpu)");
    expect(state, (cpu->xpsr & 0x1ffu) == 17u, "(cpu->xpsr & 0x1ffu) == 17u");
    cortex_m4_destroy(cpu);
}

static void test_invalid_return(TestState* state) {
    ExceptionBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    prepare_active(cpu, 16u, 0xfffffff9u, 0x400u);
    expect(state, !cortex_m4_exception_return(cpu, 0xfffffff8u),
           "!cortex_m4_exception_return(cpu, 0xfffffff8u)");
    expect(state, (cpu->cfsr & (1u << 18)) != 0u, "(cpu->cfsr & (1u << 18)) != 0u");
    cortex_m4_destroy(cpu);
}

static void test_return_failures(TestState* state) {
    ExceptionBus bus = {0};
    CortexM4* cpu = create_cpu(state, &bus);
    prepare_active(cpu, 16u, 0xfffffff9u, 0x400u);
    cpu->irq_enabled[0] = 3u;
    cpu->irq_pending[0] |= 2u;
    bus.reject_reads = true;
    expect(state, cortex_m4_exception_return(cpu, 0xfffffff9u),
           "cortex_m4_exception_return(cpu, 0xfffffff9u)");
    expect(state, (cpu->hfsr & (1u << 1)) != 0u, "(cpu->hfsr & (1u << 1)) != 0u");
    cortex_m4_destroy(cpu);

    memset(&bus, 0, sizeof(bus));
    cpu = create_cpu(state, &bus);
    prepare_active(cpu, 16u, 0xfffffff9u, 0x400u);
    bus.reject_reads = true;
    expect(state, cortex_m4_exception_return(cpu, 0xfffffff9u),
           "cortex_m4_exception_return(cpu, 0xfffffff9u)");
    expect(state, cpu->exception_unstack_memory_fault,
           "cpu->exception_unstack_memory_fault");
    expect(state, (cpu->cfsr & (1u << 11)) != 0u, "(cpu->cfsr & (1u << 11)) != 0u");
    cortex_m4_destroy(cpu);

    memset(&bus, 0, sizeof(bus));
    cpu = create_cpu(state, &bus);
    prepare_active(cpu, 16u, 0xfffffff9u, 0x400u);
    expect(state, cortex_m4_exception_return(cpu, 0xfffffff9u),
           "cortex_m4_exception_return(cpu, 0xfffffff9u)");
    expect(state, !cpu->exception_unstack_memory_fault,
           "!cpu->exception_unstack_memory_fault");
    expect(state, (cpu->cfsr & (1u << 17)) != 0u, "(cpu->cfsr & (1u << 17)) != 0u");
    cortex_m4_destroy(cpu);
}

int main(void) {
    TestState state = {0};
    test_fault_lockup(&state);
    test_psp_lifecycle(&state);
    test_entry_failures(&state);
    test_exception_selection(&state);
    test_invalid_return(&state);
    test_return_failures(&state);
    return test_finish(&state);
}
