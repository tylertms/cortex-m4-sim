#include "cortex_m4_internal.h"

#include <string.h>

#include "test.h"

#define SRAM_START 0x20000000u
#define SRAM_SIZE 0x00004000u
#define NVIC_ISER 0xe000e100u
#define NVIC_ISPR 0xe000e200u
#define NVIC_IPR 0xe000e400u
#define SCB_ICSR 0xe000ed04u
#define SCB_AIRCR 0xe000ed0cu
#define SCB_SCR 0xe000ed10u
#define SCB_CCR 0xe000ed14u
#define SCB_SHCSR 0xe000ed24u
#define SCB_CFSR 0xe000ed28u
#define SCB_HFSR 0xe000ed2cu
#define SCB_MMFAR 0xe000ed34u
#define SCB_BFAR 0xe000ed38u
#define SCB_CPACR 0xe000ed88u
#define FPU_FPCCR 0xe000ef34u
#define FPU_FPCAR 0xe000ef38u
#define FPU_FPDSCR 0xe000ef3cu

typedef struct {
    uint8_t memory[SRAM_SIZE];
} TestBus;

static bool bus_read(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                     uint32_t* value) {
    TestBus* const bus = context;
    (void)access;
    if (address < SRAM_START || address + size > SRAM_START + SRAM_SIZE) {
        return false;
    }
    *value = 0;
    memcpy(value, &bus->memory[address - SRAM_START], size);
    return true;
}

static bool bus_write(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                      uint32_t value) {
    TestBus* const bus = context;
    (void)access;
    if (address < SRAM_START || address + size > SRAM_START + SRAM_SIZE) {
        return false;
    }
    memcpy(&bus->memory[address - SRAM_START], &value, size);
    return true;
}

static CortexM4 create_cpu(TestBus* bus) {
    CortexM4 cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.bus.context = bus;
    cpu.bus.read = bus_read;
    cpu.bus.write = bus_write;
    cpu.xpsr = CORTEX_M4_XPSR_T;
    cortex_m4_system_reset(&cpu);
    return cpu;
}

static uint32_t system_read(TestState* state, CortexM4* cpu, uint32_t address,
                            uint8_t size) {
    uint32_t value = 0;
    TEST_EXPECT(state, cortex_m4_system_read(cpu, address, size, &value) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    return value;
}

static void test_registers(TestState* state, CortexM4* cpu) {
    TEST_EXPECT(state, cpu->ccr == 0x200u);
    TEST_EXPECT(state, cpu->fpccr == 0xc0000000u);
    TEST_EXPECT(state, system_read(state, cpu, 0xe000ed00u, 4) == 0x410fc241u);
    TEST_EXPECT(state, system_read(state, cpu, 0xe000ed40u, 4) == 0x30u);
    TEST_EXPECT(state, system_read(state, cpu, 0xe000ef40u, 4) == 0x10110021u);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, SCB_SCR, 4, 0xffffffffu) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, system_read(state, cpu, SCB_SCR, 4) == 0x16u);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, SCB_CCR, 4, 0xffffffffu) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, system_read(state, cpu, SCB_CCR, 4) == 0x31bu);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, SCB_SHCSR, 4, 0x0007f000u) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, system_read(state, cpu, SCB_SHCSR, 4) == 0x0007f000u);
    cpu->cfsr = 0xffffffffu;
    cpu->hfsr = 0xc0000002u;
    TEST_EXPECT(state, cortex_m4_system_write(cpu, SCB_CFSR + 1u, 1, 0x55u) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, cpu->cfsr == 0xffffaaffu);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, SCB_HFSR, 4, 0x40000002u) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, cpu->hfsr == 0x80000000u);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, SCB_MMFAR, 4, 0x12345678u) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, SCB_BFAR, 4, 0x89abcdefu) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, cpu->mmfar == 0x12345678u);
    TEST_EXPECT(state, cpu->bfar == 0x89abcdefu);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, SCB_CPACR, 4, 0xffffffffu) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, cpu->cpacr == 0x00f00000u);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, FPU_FPCCR, 4, 0) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, cpu->fpccr == 0);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, FPU_FPCAR, 4, 0x20001237u) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, cpu->fpcar == 0x20001230u);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, FPU_FPDSCR, 4, 0xffffffffu) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, cpu->fpdscr == 0x07c00000u);
    uint32_t value = 0x55u;
    TEST_EXPECT(state, cortex_m4_system_read(cpu, 0xe000ed8cu, 4, &value) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
    TEST_EXPECT(state, value == 0x55u);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, 0xe000ed8cu, 4, 0) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
    TEST_EXPECT(state, cortex_m4_system_read(cpu, 0x40000000u, 4, &value) ==
                           CORTEX_M4_SYSTEM_ACCESS_OUTSIDE);
    TEST_EXPECT(state, cortex_m4_system_read(cpu, SCB_CCR + 1u, 2, &value) ==
                           CORTEX_M4_SYSTEM_ACCESS_REJECTED);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, SCB_ICSR, 4,
                                              (1u << 31) | (1u << 28) | (1u << 26)) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, (system_read(state, cpu, SCB_ICSR, 4) &
                        ((1u << 31) | (1u << 28) | (1u << 26))) ==
                           ((1u << 31) | (1u << 28) | (1u << 26)));
}

static void test_priorities(TestState* state, CortexM4* cpu) {
    TEST_EXPECT(state, cortex_m4_system_write(cpu, SCB_AIRCR, 4, 0x05fa0500u) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    CortexM4Priority priority = cortex_m4_system_priority(cpu, 0xb0u);
    TEST_EXPECT(state, priority.preemption == 2);
    TEST_EXPECT(state, priority.subpriority == 3);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, NVIC_IPR, 1, 0xb0u) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, NVIC_IPR + 1u, 1, 0x90u) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, cortex_m4_system_exception_before(cpu, 17, 16));
    TEST_EXPECT(state, !cortex_m4_system_exception_can_preempt(cpu, 17, 16));
    cpu->irq_priority[1] = 0x70u;
    TEST_EXPECT(state, cortex_m4_system_exception_can_preempt(cpu, 17, 16));
    cpu->primask = 1;
    TEST_EXPECT(state, cortex_m4_system_exception_masked(cpu, 16));
    TEST_EXPECT(state, !cortex_m4_system_exception_masked(cpu, 3));
    cpu->primask = 0;
    cpu->faultmask = 1;
    TEST_EXPECT(state, cortex_m4_system_exception_masked(cpu, 3));
    TEST_EXPECT(state, !cortex_m4_system_exception_masked(cpu, 2));
    cpu->faultmask = 0;
    cpu->basepri = 0x80u;
    TEST_EXPECT(state, cortex_m4_system_exception_masked(cpu, 16));
    TEST_EXPECT(state, !cortex_m4_system_exception_masked(cpu, 17));
}

static void test_waiting(TestState* state, CortexM4* cpu) {
    cortex_m4_system_reset(cpu);
    cortex_m4_system_wait_for_interrupt(cpu);
    TEST_EXPECT(state, cpu->sleeping);
    cortex_m4_system_set_pending(cpu, 16, true);
    TEST_EXPECT(state, cpu->sleeping);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, NVIC_ISER, 4, 1) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, !cpu->sleeping);
    cortex_m4_system_set_pending(cpu, 16, false);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, SCB_SCR, 4, 1u << 4) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    cpu->event_register = false;
    cortex_m4_system_set_pending(cpu, 17, true);
    TEST_EXPECT(state, cpu->event_register);
    TEST_EXPECT(state, cortex_m4_system_write(cpu, NVIC_ISPR, 4, 4) ==
                           CORTEX_M4_SYSTEM_ACCESS_ACCEPTED);
    TEST_EXPECT(state, (cpu->irq_pending[0] & 6u) == 6u);
}

static void test_basic_frame(TestState* state, CortexM4* cpu) {
    cortex_m4_system_reset(cpu);
    cpu->xpsr = CORTEX_M4_XPSR_T;
    cpu->it_state = 0x88u;
    cpu->registers[0] = 0x10u;
    cpu->registers[1] = 0x11u;
    cpu->registers[2] = 0x12u;
    cpu->registers[3] = 0x13u;
    cpu->registers[12] = 0x1cu;
    cpu->registers[14] = 0x1eu;
    cpu->registers[15] = 0x100u;
    uint32_t stack_pointer = 0x20001004u;
    uint32_t return_value = 0;
    TEST_EXPECT(state,
                cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value));
    TEST_EXPECT(state, stack_pointer == 0x20000fe0u);
    TEST_EXPECT(state, return_value == 0xfffffff9u);
    TEST_EXPECT(state, cpu->exception_frame_depth == 1);
    TEST_EXPECT(state, cpu->exception_frames[0].it_state == 0x88u);
    uint32_t stacked_xpsr = 0;
    TEST_EXPECT(state, bus_read(cpu->bus.context, 0x20000ffcu, 4, CORTEX_M4_ACCESS_DEBUG,
                                &stacked_xpsr));
    TEST_EXPECT(state, (stacked_xpsr & 0x0600fc00u) == 0x00008800u);
    TEST_EXPECT(state, (stacked_xpsr & (1u << 9)) != 0);
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    cpu->exception_depth = 1;
    cpu->active_exceptions[0] = 16u;
    cpu->faultmask = 1;
    memset(cpu->registers, 0, sizeof(cpu->registers));
    TEST_EXPECT(state, cortex_m4_system_unstack_exception_frame(cpu, &stack_pointer,
                                                                return_value, 16));
    cortex_m4_exception_advanced_commit_return(cpu, 16u, true);
    TEST_EXPECT(state, stack_pointer == 0x20001004u);
    TEST_EXPECT(state, cpu->registers[0] == 0x10u);
    TEST_EXPECT(state, cpu->registers[14] == 0x1eu);
    TEST_EXPECT(state, cpu->registers[15] == 0x100u);
    TEST_EXPECT(state, cpu->it_state == 0x88u);
    TEST_EXPECT(state, cpu->faultmask == 0);
    TEST_EXPECT(state, cpu->exception_frame_depth == 0);
}

static void test_extended_frame(TestState* state, CortexM4* cpu) {
    cortex_m4_system_reset(cpu);
    cpu->xpsr = CORTEX_M4_XPSR_T;
    cpu->control = CORTEX_M4_CONTROL_FPCA | CORTEX_M4_CONTROL_SPSEL;
    cpu->fp_registers[0] = 0x3f800000u;
    cpu->fp_registers[15] = 0x40000000u;
    cpu->fpscr = 0x01000000u;
    cpu->registers[15] = 0x200u;
    uint32_t stack_pointer = 0x20002000u;
    uint32_t return_value = 0;
    TEST_EXPECT(state,
                cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value));
    TEST_EXPECT(state, stack_pointer == 0x20001f98u);
    TEST_EXPECT(state, return_value == 0xffffffedu);
    TEST_EXPECT(state, cpu->fpcar == stack_pointer);
    TEST_EXPECT(state, (cpu->fpccr & 1u) != 0);
    uint32_t word = 0;
    TEST_EXPECT(
        state, bus_read(cpu->bus.context, stack_pointer, 4, CORTEX_M4_ACCESS_DEBUG, &word));
    TEST_EXPECT(state, word == 0);
    TEST_EXPECT(state, cortex_m4_system_materialize_lazy_fp(cpu));
    TEST_EXPECT(state, (cpu->fpccr & 1u) == 0);
    TEST_EXPECT(
        state, bus_read(cpu->bus.context, stack_pointer, 4, CORTEX_M4_ACCESS_DEBUG, &word));
    TEST_EXPECT(state, word == 0x3f800000u);
    cpu->fp_registers[0] = 0;
    cpu->fp_registers[15] = 0;
    cpu->fpscr = 0;
    cpu->xpsr = CORTEX_M4_XPSR_T | 17u;
    cpu->exception_depth = 1;
    cpu->active_exceptions[0] = 17u;
    TEST_EXPECT(state, cortex_m4_system_unstack_exception_frame(cpu, &stack_pointer,
                                                                return_value, 17));
    cortex_m4_exception_advanced_commit_return(cpu, 17u, true);
    TEST_EXPECT(state, stack_pointer == 0x20002000u);
    TEST_EXPECT(state, cpu->fp_registers[0] == 0x3f800000u);
    TEST_EXPECT(state, cpu->fp_registers[15] == 0x40000000u);
    TEST_EXPECT(state, cpu->fpscr == 0x01000000u);
    TEST_EXPECT(state, (cpu->control & CORTEX_M4_CONTROL_FPCA) != 0);
    TEST_EXPECT(state, (cpu->control & CORTEX_M4_CONTROL_SPSEL) != 0);
}

static void test_invalid_return(TestState* state, CortexM4* cpu) {
    cortex_m4_system_reset(cpu);
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    cpu->exception_depth = 1;
    TEST_EXPECT(state, !cortex_m4_system_valid_exception_return(cpu, 0xfffffff1u));
    TEST_EXPECT(state, cortex_m4_system_valid_exception_return(cpu, 0xfffffff9u));
    uint32_t stack_pointer = 0x20001000u;
    TEST_EXPECT(state, !cortex_m4_system_unstack_exception_frame(cpu, &stack_pointer,
                                                                 0xfffffff5u, 16));
    TEST_EXPECT(state, (cpu->cfsr & (1u << 18)) != 0);
    cpu->exception_depth = 2;
    TEST_EXPECT(state, cortex_m4_system_valid_exception_return(cpu, 0xfffffff1u));
    TEST_EXPECT(state, !cortex_m4_system_valid_exception_return(cpu, 0xfffffff9u));
    cpu->ccr |= 1u;
    TEST_EXPECT(state, cortex_m4_system_valid_exception_return(cpu, 0xfffffff9u));
}

int main(void) {
    TestState state = {0};
    TestBus bus = {0};
    CortexM4 cpu = create_cpu(&bus);
    test_registers(&state, &cpu);
    test_priorities(&state, &cpu);
    test_waiting(&state, &cpu);
    test_basic_frame(&state, &cpu);
    test_extended_frame(&state, &cpu);
    test_invalid_return(&state, &cpu);
    return test_finish(&state);
}
