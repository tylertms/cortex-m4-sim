#include "architecture/cortex_m4/internal.h"

#include <string.h>

#include "test.h"

enum {
    MEMORY_SIZE = 256,
    FPCCR_LSPACT = 1u,
    FPCCR_THREAD = 1u << 3u,
    FPCCR_LSPEN = 1u << 30u,
    FPCCR_ASPEN = 1u << 31u,
};

typedef struct {
    uint8_t memory[MEMORY_SIZE];
    bool reject;
} TestBus;

static bool bus_read(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                     uint32_t* value) {
    TestBus* bus = context;
    (void)access;
    if (bus->reject || address < 0x20000000u ||
        address - 0x20000000u > (uint32_t)MEMORY_SIZE - size) {
        return false;
    }
    memcpy(value, &bus->memory[address - 0x20000000u], size);
    return true;
}

static bool bus_write(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                      uint32_t value) {
    TestBus* bus = context;
    (void)access;
    if (bus->reject || address < 0x20000000u ||
        address - 0x20000000u > (uint32_t)MEMORY_SIZE - size) {
        return false;
    }
    memcpy(&bus->memory[address - 0x20000000u], &value, size);
    return true;
}

static CortexM4* create_cpu(TestState* state, TestBus* bus) {
    const CortexM4Bus interface = {bus, bus_read, bus_write, NULL, NULL};
    CortexM4* cpu = cortex_m4_create(interface);
    expect(state, cpu != NULL, "cpu != NULL");
    return cpu;
}

static void test_system_access_guards(TestState* state, CortexM4* cpu) {
    uint32_t value = 0u;
    expect(state,
           cortex_m4_system_read(cpu, 0xe0080000u, 4u, CORTEX_M4_ACCESS_DEBUG, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_OUTSIDE,
           "system read gap is outside");
    expect(state,
           cortex_m4_system_read(NULL, 0xe000e000u, 4u, CORTEX_M4_ACCESS_DEBUG, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "system read rejects null cpu");
    expect(state,
           cortex_m4_system_read(cpu, 0xe000e000u, 4u, CORTEX_M4_ACCESS_DEBUG, NULL) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "system read rejects null value");
    expect(state,
           cortex_m4_system_write(cpu, 0xe0080000u, 4u, CORTEX_M4_ACCESS_DEBUG, value) ==
               CORTEX_M4_SYSTEM_ACCESS_OUTSIDE,
           "system write gap is outside");
    expect(state,
           cortex_m4_system_write(NULL, 0xe000e000u, 4u, CORTEX_M4_ACCESS_DEBUG, value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "system write rejects null cpu");
    expect(state,
           cortex_m4_system_write(cpu, 0xe000ef00u, 1u, CORTEX_M4_ACCESS_DEBUG, 0u) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "software interrupt requires a word write");

    static const uint32_t read_only[] = {
        0xe000e004u, 0xe000e01cu, 0xe000e300u, 0xe000ed00u,
        0xe000ed40u, 0xe000ef40u, 0xe000ef44u, 0xe000ef48u,
    };
    for (size_t index = 0u; index < sizeof(read_only) / sizeof(read_only[0]); index++)
        expect(state,
               cortex_m4_system_write(cpu, read_only[index], 4u, CORTEX_M4_ACCESS_DEBUG,
                                      UINT32_MAX) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
               "read-only system register ignores a debugger write");

    cpu->xpsr = CORTEX_M4_XPSR_T;
    cpu->control = CORTEX_M4_CONTROL_NPRIV;
    cpu->debug.itm_trace_privilege = 1u;
    expect(state,
           cortex_m4_system_read(cpu, 0xe0000000u, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA,
                                 &value) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "permitted unprivileged trace port is readable");
    expect(state,
           cortex_m4_system_read(cpu, 0xe0000080u, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA,
                                 &value) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "unprivileged trace control is rejected");
}

static void test_exception_masks(TestState* state, CortexM4* cpu) {
    expect(state, cortex_m4_configure_implementation(cpu, 32u, 4u, 8u),
           "configure exception matrix processor");
    expect(state, !cortex_m4_system_exception_masked(NULL, 16u) &&
                      !cortex_m4_system_exception_masked(cpu, 0u) &&
                      !cortex_m4_system_exception_masked(cpu, 2u),
           "unmaskable exception guards are preserved");
    cpu->faultmask = 1u;
    expect(state, cortex_m4_system_exception_masked(cpu, 3u), "fault mask covers hard fault");
    cpu->faultmask = 0u;
    expect(state, !cortex_m4_system_exception_masked(cpu, 3u), "hard fault bypasses other masks");
    cpu->primask = 1u;
    expect(state, cortex_m4_system_exception_masked(cpu, 16u), "primary mask covers interrupts");
    cpu->primask = 0u;
    cpu->basepri = 0u;
    expect(state, !cortex_m4_system_exception_masked(cpu, 16u), "zero base priority is unmasked");
    cpu->basepri = 0x40u;
    cpu->irq_priority[0] = 0x80u;
    expect(state, cortex_m4_system_exception_masked(cpu, 16u),
           "base priority masks lower-priority interrupts");
    cpu->irq_priority[0] = 0u;
    expect(state, !cortex_m4_system_exception_masked(cpu, 16u),
           "base priority permits higher-priority interrupts");
    cpu->basepri = 0u;
    expect(state, !cortex_m4_system_exception_can_preempt(NULL, 16u, 0u) &&
                      !cortex_m4_system_exception_can_preempt(cpu, 0u, 0u) &&
                      cortex_m4_system_exception_can_preempt(cpu, 16u, 0u),
           "exception preemption handles guards and thread mode");
    cpu->irq_priority[0] = 0u;
    cpu->irq_priority[1] = 0x80u;
    expect(state, cortex_m4_system_exception_can_preempt(cpu, 16u, 17u) &&
                      !cortex_m4_system_exception_can_preempt(cpu, 17u, 16u),
           "exception preemption follows configured priorities");
}

static void test_exception_frames(TestState* state, CortexM4* cpu, TestBus* bus) {
    uint32_t stack_pointer = 0x20000100u;
    uint32_t return_value = 0u;
    expect(state, !cortex_m4_system_stack_exception_frame(NULL, &stack_pointer, &return_value) &&
                      !cortex_m4_system_stack_exception_frame(cpu, NULL, &return_value) &&
                      !cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, NULL),
           "exception stacking rejects invalid arguments");
    bus->reject = false;
    cpu->exception_frame_depth = 0u;
    cpu->xpsr = CORTEX_M4_XPSR_T;
    cpu->control = 0u;
    cpu->ccr = 0u;
    expect(state, cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value) &&
                      return_value == 0xfffffff9u,
           "thread exception stacks a basic MSP frame");

    stack_pointer = 0x20000100u;
    cpu->exception_frame_depth = 0u;
    cpu->xpsr = CORTEX_M4_XPSR_T | 3u;
    expect(state, cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value) &&
                      return_value == 0xfffffff1u,
           "handler exception stacks a handler frame");

    stack_pointer = 0x20000100u;
    cpu->exception_frame_depth = 0u;
    cpu->xpsr = CORTEX_M4_XPSR_T;
    cpu->control = CORTEX_M4_CONTROL_SPSEL | CORTEX_M4_CONTROL_FPCA;
    cpu->fpccr = FPCCR_ASPEN | FPCCR_LSPEN;
    expect(state, cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value) &&
                      return_value == 0xffffffedu && (cpu->fpccr & FPCCR_LSPACT) != 0u,
           "thread exception stacks a lazy extended PSP frame");
    expect(state, cortex_m4_system_materialize_lazy_fp(cpu) &&
                      (cpu->fpccr & FPCCR_LSPACT) == 0u,
           "lazy floating-point frame materializes");

    stack_pointer = 0x200000fcu;
    cpu->exception_frame_depth = 0u;
    cpu->xpsr = CORTEX_M4_XPSR_T;
    cpu->control = 0u;
    cpu->fpccr = 0u;
    cpu->ccr = 1u << 9u;
    expect(state, cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value),
           "aligned exception stacking inserts padding");
}

static void test_exception_guards(TestState* state, CortexM4* cpu, TestBus* bus) {
    cortex_m4_system_reset(NULL);
    cortex_m4_system_set_pending(NULL, 2u, true);
    cortex_m4_system_set_pending(cpu, 1u, true);
    expect(state, !cortex_m4_system_exception_before(cpu, 0u, 2u),
           "no exception sorts before an active exception");
    expect(state, !cortex_m4_system_exception_before(cpu, 1u, 2u),
           "reserved exception does not precede nonmaskable interrupt");
    expect(state, !cortex_m4_system_valid_exception_return(NULL, 0xfffffff9u),
           "null cpu has no valid exception return");
    cpu->xpsr = CORTEX_M4_XPSR_T | 3u;
    expect(state, !cortex_m4_system_valid_exception_return(cpu, 0x12345678u),
           "noncanonical exception return is rejected");
    uint32_t stack_pointer = 0x20000100u;
    uint32_t return_value = 0u;
    cpu->exception_frame_depth = CORTEX_M4_EXCEPTION_FRAME_LIMIT;
    expect(state, !cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value),
           "full exception frame stack is rejected");
    cpu->exception_frame_depth = 0u;
    bus->reject = true;
    expect(state, !cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value),
           "failed stack write rejects exception frame");
}

int main(void) {
    TestState state = {0};
    TestBus bus = {0};
    CortexM4* cpu = create_cpu(&state, &bus);
    test_system_access_guards(&state, cpu);
    test_exception_masks(&state, cpu);
    test_exception_frames(&state, cpu, &bus);
    test_exception_guards(&state, cpu, &bus);
    cortex_m4_destroy(cpu);
    return test_finish(&state);
}
