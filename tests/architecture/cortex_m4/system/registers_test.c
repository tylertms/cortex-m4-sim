#include "architecture/cortex_m4/internal.h"

#include <string.h>

#include "test.h"

#define SRAM_START 0x20000000u
#define SRAM_SIZE 0x00004000u
#define NVIC_ISER 0xe000e100u
#define NVIC_ICER 0xe000e180u
#define NVIC_ISPR 0xe000e200u
#define NVIC_ICPR 0xe000e280u
#define NVIC_IABR 0xe000e300u
#define NVIC_IPR 0xe000e400u
#define NVIC_STIR 0xe000ef00u
#define ITM_STIMULUS 0xe0000000u
#define ITM_TRACE_ENABLE 0xe0000e00u
#define ITM_TRACE_PRIVILEGE 0xe0000e40u
#define ITM_TRACE_CONTROL 0xe0000e80u
#define ITM_LOCK_ACCESS 0xe0000fb0u
#define DEMCR 0xe000edfcu
#define SCB_ICSR 0xe000ed04u
#define SCB_VTOR 0xe000ed08u
#define SCB_AIRCR 0xe000ed0cu
#define SCB_SCR 0xe000ed10u
#define SCB_CCR 0xe000ed14u
#define SCB_SHCSR 0xe000ed24u
#define SCB_CFSR 0xe000ed28u
#define SCB_HFSR 0xe000ed2cu
#define SCB_DFSR 0xe000ed30u
#define SCB_MMFAR 0xe000ed34u
#define SCB_BFAR 0xe000ed38u
#define SCB_AFSR 0xe000ed3cu
#define SCB_CPACR 0xe000ed88u
#define FPU_FPCCR 0xe000ef34u
#define FPU_FPCAR 0xe000ef38u
#define FPU_FPDSCR 0xe000ef3cu

typedef struct {
    uint8_t memory[SRAM_SIZE];
    uint32_t rejected_read;
    bool reject_read;
} TestBus;

static bool bus_read(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                     uint32_t* value) {
    TestBus* const bus = context;
    (void)access;
    if ((bus->reject_read && address == bus->rejected_read) || address < SRAM_START ||
        address + size > SRAM_START + SRAM_SIZE) {
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
    cpu.external_irq_count = CORTEX_M4_IRQ_COUNT;
    cpu.priority_bits = 4u;
    cpu.mpu_region_count = CORTEX_M4_MPU_REGION_COUNT;
    cortex_m4_system_reset(&cpu);
    return cpu;
}

static uint32_t system_read(TestState* state, CortexM4* cpu, uint32_t address, uint8_t size) {
    uint32_t value = 0;
    const CortexM4SystemAccess result =
        cortex_m4_system_read(cpu, address, size, CORTEX_M4_ACCESS_DATA, &value);
    expect(state, result == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "result == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    return value;
}

static CortexM4SystemAccess system_write(CortexM4* cpu, uint32_t address, uint8_t size,
                                         uint32_t value) {
    return cortex_m4_system_write(cpu, address, size, CORTEX_M4_ACCESS_DATA, value);
}

static void test_registers(TestState* state, CortexM4* cpu) {
    expect(state, cpu->ccr == 0x200u, "cpu->ccr == 0x200u");
    expect(state, cpu->fpccr == 0xc0000000u, "cpu->fpccr == 0xc0000000u");
    expect(state, system_read(state, cpu, 0xe000ed00u, 4) == 0x410fc241u,
           "system_read(state, cpu, 0xe000ed00u, 4) == 0x410fc241u");
    expect(state, system_read(state, cpu, 0xe000ed40u, 4) == 0x30u,
           "system_read(state, cpu, 0xe000ed40u, 4) == 0x30u");
    expect(state, system_read(state, cpu, 0xe000ef40u, 4) == 0x10110021u,
           "system_read(state, cpu, 0xe000ef40u, 4) == 0x10110021u");
    expect(state, system_write(cpu, SCB_SCR, 4, 0xffffffffu) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_SCR, 4, 0xffffffffu) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, system_read(state, cpu, SCB_SCR, 4) == 0x16u,
           "system_read(state, cpu, SCB_SCR, 4) == 0x16u");
    expect(state, system_write(cpu, SCB_CCR, 4, 0xffffffffu) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_CCR, 4, 0xffffffffu) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, system_read(state, cpu, SCB_CCR, 4) == 0x31bu,
           "system_read(state, cpu, SCB_CCR, 4) == 0x31bu");
    expect(state, system_write(cpu, SCB_SHCSR, 4, 0x0007f000u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_SHCSR, 4, 0x0007f000u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, system_read(state, cpu, SCB_SHCSR, 4) == 0x0007f000u,
           "system_read(state, cpu, SCB_SHCSR, 4) == 0x0007f000u");
    cpu->cfsr = 0xffffffffu;
    cpu->hfsr = 0xc0000002u;
    expect(state, system_write(cpu, SCB_CFSR + 1u, 1, 0x55u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_CFSR + 1u, 1, 0x55u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, cpu->cfsr == 0xffffaaffu, "cpu->cfsr == 0xffffaaffu");
    expect(state, system_read(state, cpu, SCB_CFSR + 1u, 1u) == 0xaau,
           "system_read(state, cpu, SCB_CFSR + 1u, 1u) == 0xaau");
    expect(state, system_read(state, cpu, SCB_CFSR + 2u, 2u) == 0xffffu,
           "system_read(state, cpu, SCB_CFSR + 2u, 2u) == 0xffffu");
    expect(state, system_write(cpu, SCB_CFSR + 2u, 2u, 0u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_CFSR + 2u, 2u, 0u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, system_write(cpu, SCB_HFSR, 4, 0x40000002u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_HFSR, 4, 0x40000002u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, cpu->hfsr == 0x80000000u, "cpu->hfsr == 0x80000000u");
    expect(state, system_write(cpu, SCB_MMFAR, 4, 0x12345678u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_MMFAR, 4, 0x12345678u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, system_write(cpu, SCB_BFAR, 4, 0x89abcdefu) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_BFAR, 4, 0x89abcdefu) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, cpu->mmfar == 0x12345678u, "cpu->mmfar == 0x12345678u");
    expect(state, cpu->bfar == 0x89abcdefu, "cpu->bfar == 0x89abcdefu");
    cpu->dfsr = 0x1fu;
    cpu->afsr = 0x13579bdfu;
    expect(state, system_write(cpu, SCB_CPACR, 4, 0xffffffffu) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_CPACR, 4, 0xffffffffu) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, cpu->cpacr == 0x00f00000u, "cpu->cpacr == 0x00f00000u");
    expect(state, system_write(cpu, FPU_FPCCR, 4, 0) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, FPU_FPCCR, 4, 0) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, cpu->fpccr == 0, "cpu->fpccr == 0");
    expect(state, system_write(cpu, FPU_FPCAR, 4, 0x20001237u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, FPU_FPCAR, 4, 0x20001237u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, cpu->fpcar == 0x20001230u, "cpu->fpcar == 0x20001230u");
    expect(state, system_write(cpu, FPU_FPDSCR, 4, 0xffffffffu) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, FPU_FPDSCR, 4, 0xffffffffu) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, cpu->fpdscr == 0x07c00000u, "cpu->fpdscr == 0x07c00000u");
    expect(state, system_read(state, cpu, 0xe000e014u, 4u) == 0u,
           "system_read(state, cpu, 0xe000e014u, 4u) == 0u");
    expect(state, system_read(state, cpu, 0xe000e01cu, 4u) == 0u,
           "system_read(state, cpu, 0xe000e01cu, 4u) == 0u");
    expect(state, system_read(state, cpu, NVIC_ICER, 4u) == 0u,
           "system_read(state, cpu, NVIC_ICER, 4u) == 0u");
    expect(state, system_read(state, cpu, NVIC_ICPR, 4u) == 0u,
           "system_read(state, cpu, NVIC_ICPR, 4u) == 0u");
    expect(state, system_read(state, cpu, NVIC_IABR, 4u) == 0u,
           "system_read(state, cpu, NVIC_IABR, 4u) == 0u");
    expect(state, system_read(state, cpu, SCB_VTOR, 4u) == 0u,
           "system_read(state, cpu, SCB_VTOR, 4u) == 0u");
    expect(state, system_read(state, cpu, SCB_CFSR, 4u) == cpu->cfsr,
           "system_read(state, cpu, SCB_CFSR, 4u) == cpu->cfsr");
    expect(state, system_read(state, cpu, SCB_HFSR, 4u) == cpu->hfsr,
           "system_read(state, cpu, SCB_HFSR, 4u) == cpu->hfsr");
    expect(state, system_read(state, cpu, SCB_DFSR, 4u) == cpu->dfsr,
           "system_read(state, cpu, SCB_DFSR, 4u) == cpu->dfsr");
    expect(state, system_read(state, cpu, SCB_MMFAR, 4u) == cpu->mmfar,
           "system_read(state, cpu, SCB_MMFAR, 4u) == cpu->mmfar");
    expect(state, system_read(state, cpu, SCB_BFAR, 4u) == cpu->bfar,
           "system_read(state, cpu, SCB_BFAR, 4u) == cpu->bfar");
    expect(state, system_read(state, cpu, SCB_AFSR, 4u) == cpu->afsr,
           "system_read(state, cpu, SCB_AFSR, 4u) == cpu->afsr");
    expect(state, system_read(state, cpu, SCB_CPACR, 4u) == cpu->cpacr,
           "system_read(state, cpu, SCB_CPACR, 4u) == cpu->cpacr");
    expect(state, system_read(state, cpu, FPU_FPCCR, 4u) == cpu->fpccr,
           "system_read(state, cpu, FPU_FPCCR, 4u) == cpu->fpccr");
    expect(state, system_read(state, cpu, FPU_FPCAR, 4u) == cpu->fpcar,
           "system_read(state, cpu, FPU_FPCAR, 4u) == cpu->fpcar");
    expect(state, system_read(state, cpu, FPU_FPDSCR, 4u) == cpu->fpdscr,
           "system_read(state, cpu, FPU_FPDSCR, 4u) == cpu->fpdscr");
    expect(state, system_read(state, cpu, 0xe000ef40u, 4u) == 0x10110021u,
           "system_read(state, cpu, 0xe000ef40u, 4u) == 0x10110021u");
    expect(state, system_read(state, cpu, 0xe000ef44u, 4u) == 0x11000011u,
           "system_read(state, cpu, 0xe000ef44u, 4u) == 0x11000011u");
    expect(state, system_read(state, cpu, 0xe000ef48u, 4u) == 0u,
           "system_read(state, cpu, 0xe000ef48u, 4u) == 0u");
    uint32_t value = 0x55u;
    expect(state,
           cortex_m4_system_read(cpu, 0xe000ed8cu, 4, CORTEX_M4_ACCESS_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_system_read(cpu, 0xe000ed8cu, 4, CORTEX_M4_ACCESS_DATA, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state, value == 0x55u, "value == 0x55u");
    expect(state, system_write(cpu, 0xe000ed8cu, 4, 0) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "system_write(cpu, 0xe000ed8cu, 4, 0) == CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_system_read(cpu, 0x40000000u, 4, CORTEX_M4_ACCESS_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_OUTSIDE,
           "cortex_m4_system_read(cpu, 0x40000000u, 4, CORTEX_M4_ACCESS_DATA, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_OUTSIDE");
    expect(state,
           cortex_m4_system_read(cpu, SCB_CCR + 1u, 2, CORTEX_M4_ACCESS_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_system_read(cpu, SCB_CCR + 1u, 2, CORTEX_M4_ACCESS_DATA, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           system_write(cpu, SCB_ICSR, 4, (1u << 31) | (1u << 28) | (1u << 26)) ==
               CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_ICSR, 4, (1u << 31) | (1u << 28) | (1u << 26)) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state,
           (system_read(state, cpu, SCB_ICSR, 4) & ((1u << 31) | (1u << 28) | (1u << 26))) ==
               ((1u << 31) | (1u << 28) | (1u << 26)),
           "(system_read(state, cpu, SCB_ICSR, 4) & ((1u << 31) | (1u << 28) | (1u << 26))) "
           "== ((1u << 31) | (1u << 28) | (1u << 26))");
    expect(state, system_write(cpu, NVIC_ICER, 4u, UINT32_MAX) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, NVIC_ICER, 4u, UINT32_MAX) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, system_write(cpu, SCB_ICSR, 4u, 1u << 27u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_ICSR, 4u, 1u << 27u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, system_write(cpu, SCB_VTOR, 4u, 0x12345678u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_VTOR, 4u, 0x12345678u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, cpu->vtor == 0x12345600u, "cpu->vtor == 0x12345600u");
    expect(state, system_write(cpu, SCB_DFSR, 4u, 0x1fu) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_DFSR, 4u, 0x1fu) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, system_write(cpu, SCB_AFSR, 4u, 0x2468ace0u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_AFSR, 4u, 0x2468ace0u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, cpu->afsr == 0x2468ace0u, "cpu->afsr == 0x2468ace0u");
}

static void test_priorities(TestState* state, CortexM4* cpu) {
    const CortexM4Priority null_priority = cortex_m4_system_priority(NULL, 0xffu);
    expect(state, null_priority.preemption == 0u, "null_priority.preemption == 0u");
    expect(state, null_priority.subpriority == 0u, "null_priority.subpriority == 0u");
    expect(state, system_write(cpu, SCB_AIRCR, 4, 0x05fa0500u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_AIRCR, 4, 0x05fa0500u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    CortexM4Priority priority = cortex_m4_system_priority(cpu, 0xb0u);
    expect(state, priority.preemption == 2, "priority.preemption == 2");
    expect(state, priority.subpriority == 3, "priority.subpriority == 3");
    expect(state, system_write(cpu, NVIC_IPR, 1, 0xb0u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, NVIC_IPR, 1, 0xb0u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, system_write(cpu, NVIC_IPR + 1u, 1, 0x90u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, NVIC_IPR + 1u, 1, 0x90u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, system_read(state, cpu, NVIC_IPR + 1u, 1u) == 0x90u,
           "system_read(state, cpu, NVIC_IPR + 1u, 1u) == 0x90u");
    expect(state, system_read(state, cpu, NVIC_IPR, 2u) == 0x90b0u,
           "system_read(state, cpu, NVIC_IPR, 2u) == 0x90b0u");
    expect(state, system_write(cpu, NVIC_IPR + 2u, 2u, 0x7050u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, NVIC_IPR + 2u, 2u, 0x7050u) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, cortex_m4_system_exception_before(cpu, 17, 16),
           "cortex_m4_system_exception_before(cpu, 17, 16)");
    expect(state, !cortex_m4_system_exception_can_preempt(cpu, 17, 16),
           "!cortex_m4_system_exception_can_preempt(cpu, 17, 16)");
    cpu->irq_priority[1] = 0x70u;
    expect(state, cortex_m4_system_exception_can_preempt(cpu, 17, 16),
           "cortex_m4_system_exception_can_preempt(cpu, 17, 16)");
    cpu->primask = 1;
    expect(state, cortex_m4_system_exception_masked(cpu, 16),
           "cortex_m4_system_exception_masked(cpu, 16)");
    expect(state, !cortex_m4_system_exception_masked(cpu, 3),
           "!cortex_m4_system_exception_masked(cpu, 3)");
    cpu->primask = 0;
    cpu->faultmask = 1;
    expect(state, cortex_m4_system_exception_masked(cpu, 3),
           "cortex_m4_system_exception_masked(cpu, 3)");
    expect(state, !cortex_m4_system_exception_masked(cpu, 2),
           "!cortex_m4_system_exception_masked(cpu, 2)");
    cpu->faultmask = 0;
    cpu->basepri = 0x80u;
    expect(state, cortex_m4_system_exception_masked(cpu, 16),
           "cortex_m4_system_exception_masked(cpu, 16)");
    expect(state, !cortex_m4_system_exception_masked(cpu, 17),
           "!cortex_m4_system_exception_masked(cpu, 17)");
    cpu->basepri = 0u;
    cpu->irq_priority[0] = 0u;
    cpu->irq_priority[1] = 0u;
    expect(state, cortex_m4_system_exception_before(cpu, 16u, 17u),
           "cortex_m4_system_exception_before(cpu, 16u, 17u)");
}

static void test_waiting(TestState* state, CortexM4* cpu) {
    cortex_m4_system_reset(cpu);
    cortex_m4_system_wait_for_interrupt(cpu);
    expect(state, cpu->sleeping, "cpu->sleeping");
    cortex_m4_system_set_pending(cpu, 16, true);
    expect(state, cpu->sleeping, "cpu->sleeping");
    expect(state, system_write(cpu, NVIC_ISER, 4, 1) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, NVIC_ISER, 4, 1) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, !cpu->sleeping, "!cpu->sleeping");
    cortex_m4_system_set_pending(cpu, 16, false);
    expect(state, system_write(cpu, SCB_SCR, 4, 1u << 4) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, SCB_SCR, 4, 1u << 4) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    cpu->event_register = false;
    cortex_m4_system_set_pending(cpu, 17, true);
    expect(state, cpu->event_register, "cpu->event_register");
    expect(state, system_write(cpu, NVIC_ISPR, 4, 4) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "system_write(cpu, NVIC_ISPR, 4, 4) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, (cpu->irq_pending[0] & 6u) == 6u, "(cpu->irq_pending[0] & 6u) == 6u");

    cpu->system_pending = (1u << 3u) | (1u << 15u);
    cpu->system_priority[11] = 0x80u;
    expect(state, ((system_read(state, cpu, SCB_ICSR, 4u) >> 12u) & 0x1ffu) == 3u,
           "((system_read(state, cpu, SCB_ICSR, 4u) >> 12u) & 0x1ffu) == 3u");
    cpu->system_pending = 1u << 15u;
    expect(state, ((system_read(state, cpu, SCB_ICSR, 4u) >> 12u) & 0x1ffu) == 15u,
           "((system_read(state, cpu, SCB_ICSR, 4u) >> 12u) & 0x1ffu) == 15u");
    cpu->system_pending = 1u << 4u;
    cortex_m4_exception_advanced_commit_entry(cpu, 4u);
    expect(state, (system_read(state, cpu, SCB_SHCSR, 4u) & 1u) != 0u,
           "(system_read(state, cpu, SCB_SHCSR, 4u) & 1u) != 0u");
    cortex_m4_exception_advanced_commit_return(cpu, 4u, false);
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
    expect(state, cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value),
           "cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value)");
    expect(state, stack_pointer == 0x20000fe0u, "stack_pointer == 0x20000fe0u");
    expect(state, return_value == 0xfffffff9u, "return_value == 0xfffffff9u");
    expect(state, cpu->exception_frame_depth == 1, "cpu->exception_frame_depth == 1");
    expect(state, cpu->exception_frames[0].it_state == 0x88u,
           "cpu->exception_frames[0].it_state == 0x88u");
    uint32_t stacked_xpsr = 0;
    expect(state, bus_read(cpu->bus.context, 0x20000ffcu, 4, CORTEX_M4_ACCESS_DEBUG, &stacked_xpsr),
           "bus_read(cpu->bus.context, 0x20000ffcu, 4, CORTEX_M4_ACCESS_DEBUG, "
           "&stacked_xpsr)");
    expect(state, (stacked_xpsr & 0x0600fc00u) == 0x00008800u,
           "(stacked_xpsr & 0x0600fc00u) == 0x00008800u");
    expect(state, (stacked_xpsr & (1u << 9)) != 0, "(stacked_xpsr & (1u << 9)) != 0");
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    cpu->exception_depth = 1;
    cpu->active_exceptions[0] = 16u;
    cpu->faultmask = 1;
    memset(cpu->registers, 0, sizeof(cpu->registers));
    expect(state, cortex_m4_system_unstack_exception_frame(cpu, &stack_pointer, return_value, 16),
           "cortex_m4_system_unstack_exception_frame(cpu, &stack_pointer, return_value, 16)");
    cortex_m4_exception_advanced_commit_return(cpu, 16u, true);
    expect(state, stack_pointer == 0x20001004u, "stack_pointer == 0x20001004u");
    expect(state, cpu->registers[0] == 0x10u, "cpu->registers[0] == 0x10u");
    expect(state, cpu->registers[14] == 0x1eu, "cpu->registers[14] == 0x1eu");
    expect(state, cpu->registers[15] == 0x100u, "cpu->registers[15] == 0x100u");
    expect(state, cpu->it_state == 0x88u, "cpu->it_state == 0x88u");
    expect(state, cpu->faultmask == 0, "cpu->faultmask == 0");
    expect(state, cpu->exception_frame_depth == 0, "cpu->exception_frame_depth == 0");
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
    expect(state, cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value),
           "cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &return_value)");
    expect(state, stack_pointer == 0x20001f98u, "stack_pointer == 0x20001f98u");
    expect(state, return_value == 0xffffffedu, "return_value == 0xffffffedu");
    expect(state, cpu->fpcar == stack_pointer, "cpu->fpcar == stack_pointer");
    expect(state, (cpu->fpccr & 1u) != 0, "(cpu->fpccr & 1u) != 0");
    uint32_t word = 0;
    expect(state, bus_read(cpu->bus.context, stack_pointer, 4, CORTEX_M4_ACCESS_DEBUG, &word),
           "bus_read(cpu->bus.context, stack_pointer, 4, CORTEX_M4_ACCESS_DEBUG, &word)");
    expect(state, word == 0, "word == 0");
    expect(state, cortex_m4_system_materialize_lazy_fp(cpu),
           "cortex_m4_system_materialize_lazy_fp(cpu)");
    expect(state, (cpu->fpccr & 1u) == 0, "(cpu->fpccr & 1u) == 0");
    expect(state, bus_read(cpu->bus.context, stack_pointer, 4, CORTEX_M4_ACCESS_DEBUG, &word),
           "bus_read(cpu->bus.context, stack_pointer, 4, CORTEX_M4_ACCESS_DEBUG, &word)");
    expect(state, word == 0x3f800000u, "word == 0x3f800000u");
    cpu->fp_registers[0] = 0;
    cpu->fp_registers[15] = 0;
    cpu->fpscr = 0;
    cpu->xpsr = CORTEX_M4_XPSR_T | 17u;
    cpu->exception_depth = 1;
    cpu->active_exceptions[0] = 17u;
    expect(state, cortex_m4_system_unstack_exception_frame(cpu, &stack_pointer, return_value, 17),
           "cortex_m4_system_unstack_exception_frame(cpu, &stack_pointer, return_value, 17)");
    cortex_m4_exception_advanced_commit_return(cpu, 17u, true);
    expect(state, stack_pointer == 0x20002000u, "stack_pointer == 0x20002000u");
    expect(state, cpu->fp_registers[0] == 0x3f800000u, "cpu->fp_registers[0] == 0x3f800000u");
    expect(state, cpu->fp_registers[15] == 0x40000000u, "cpu->fp_registers[15] == 0x40000000u");
    expect(state, cpu->fpscr == 0x01000000u, "cpu->fpscr == 0x01000000u");
    expect(state, (cpu->control & CORTEX_M4_CONTROL_FPCA) != 0,
           "(cpu->control & CORTEX_M4_CONTROL_FPCA) != 0");
    expect(state, (cpu->control & CORTEX_M4_CONTROL_SPSEL) != 0,
           "(cpu->control & CORTEX_M4_CONTROL_SPSEL) != 0");
}

static void test_extended_unstack_failure(TestState* state, CortexM4* cpu, TestBus* bus) {
    cortex_m4_system_reset(cpu);
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    cpu->exception_depth = 1u;
    cpu->active_exceptions[0] = 16u;
    cpu->exception_frame_depth = 1u;
    cpu->exception_frames[0].address = SRAM_START + 0x100u;
    cpu->exception_frames[0].return_value = 0xffffffe9u;
    const uint32_t core_frame[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0x101u, CORTEX_M4_XPSR_T};
    memcpy(bus->memory + 0x148u, core_frame, sizeof(core_frame));
    bus->reject_read = true;
    bus->rejected_read = SRAM_START + 0x100u;
    uint32_t stack_pointer = SRAM_START + 0x100u;
    expect(state, !cortex_m4_system_unstack_exception_frame(cpu, &stack_pointer, 0xffffffe9u, 16u),
           "!cortex_m4_system_unstack_exception_frame(cpu, &stack_pointer, 0xffffffe9u, 16u)");
    expect(state, cpu->exception_unstack_memory_fault, "cpu->exception_unstack_memory_fault");
    bus->reject_read = false;
}

static void test_invalid_return(TestState* state, CortexM4* cpu) {
    cortex_m4_system_reset(cpu);
    cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
    cpu->exception_depth = 1;
    expect(state, !cortex_m4_system_valid_exception_return(cpu, 0xfffffff1u),
           "!cortex_m4_system_valid_exception_return(cpu, 0xfffffff1u)");
    expect(state, cortex_m4_system_valid_exception_return(cpu, 0xfffffff9u),
           "cortex_m4_system_valid_exception_return(cpu, 0xfffffff9u)");
    uint32_t stack_pointer = 0x20001000u;
    expect(state, !cortex_m4_system_unstack_exception_frame(cpu, &stack_pointer, 0xfffffff5u, 16),
           "!cortex_m4_system_unstack_exception_frame(cpu, &stack_pointer, 0xfffffff5u, 16)");
    expect(state, (cpu->cfsr & (1u << 18)) != 0, "(cpu->cfsr & (1u << 18)) != 0");
    cpu->exception_depth = 2;
    expect(state, cortex_m4_system_valid_exception_return(cpu, 0xfffffff1u),
           "cortex_m4_system_valid_exception_return(cpu, 0xfffffff1u)");
    expect(state, !cortex_m4_system_valid_exception_return(cpu, 0xfffffff9u),
           "!cortex_m4_system_valid_exception_return(cpu, 0xfffffff9u)");
    cpu->ccr |= 1u;
    expect(state, cortex_m4_system_valid_exception_return(cpu, 0xfffffff9u),
           "cortex_m4_system_valid_exception_return(cpu, 0xfffffff9u)");
}

static void test_copy_implementation(TestState* state) {
    TestBus source_bus = {0};
    TestBus destination_bus = {0};
    CortexM4 source = create_cpu(&source_bus);
    CortexM4 destination = create_cpu(&destination_bus);
    source.registers[0] = 0x12345678u;
    expect(state, cortex_m4_copy(&destination, &source), "cortex_m4_copy(&destination, &source)");
    expect(state, destination.registers[0] == 0x12345678u,
           "destination.registers[0] == 0x12345678u");
    expect(state, destination.bus.context == &destination_bus,
           "destination.bus.context == &destination_bus");
    destination.external_irq_count--;
    expect(state, !cortex_m4_copy(&destination, &source), "!cortex_m4_copy(&destination, &source)");
    destination.external_irq_count = source.external_irq_count;
    destination.priority_bits--;
    expect(state, !cortex_m4_copy(&destination, &source), "!cortex_m4_copy(&destination, &source)");
    destination.priority_bits = source.priority_bits;
    destination.mpu_region_count--;
    expect(state, !cortex_m4_copy(&destination, &source), "!cortex_m4_copy(&destination, &source)");
}

static void test_configure_implementation(TestState* state) {
    TestBus bus = {0};
    CortexM4 cpu = create_cpu(&bus);
    memset(cpu.irq_priority, 0xff, sizeof(cpu.irq_priority));
    memset(cpu.irq_enabled, 0xff, sizeof(cpu.irq_enabled));
    memset(cpu.irq_pending, 0xff, sizeof(cpu.irq_pending));
    memset(cpu.irq_active, 0xff, sizeof(cpu.irq_active));
    memset(cpu.irq_level, 0xff, sizeof(cpu.irq_level));
    memset(cpu.mpu_region_base, 0xff, sizeof(cpu.mpu_region_base));
    memset(cpu.mpu_region_attributes, 0xff, sizeof(cpu.mpu_region_attributes));
    cpu.mpu_control = 7u;
    cpu.mpu_region_number = 7u;
    expect(state, !cortex_m4_configure_implementation(NULL, 86u, 4u, 0u),
           "!cortex_m4_configure_implementation(NULL, 86u, 4u, 0u)");
    expect(state, !cortex_m4_configure_implementation(&cpu, 0u, 4u, 0u),
           "!cortex_m4_configure_implementation(&cpu, 0u, 4u, 0u)");
    expect(state, !cortex_m4_configure_implementation(&cpu, CORTEX_M4_IRQ_COUNT + 1u, 4u, 0u),
           "!cortex_m4_configure_implementation(&cpu, CORTEX_M4_IRQ_COUNT + 1u, 4u, 0u)");
    expect(state, !cortex_m4_configure_implementation(&cpu, 86u, 2u, 0u),
           "!cortex_m4_configure_implementation(&cpu, 86u, 2u, 0u)");
    expect(state, !cortex_m4_configure_implementation(&cpu, 86u, 9u, 0u),
           "!cortex_m4_configure_implementation(&cpu, 86u, 9u, 0u)");
    expect(state,
           !cortex_m4_configure_implementation(&cpu, 86u, 4u, CORTEX_M4_MPU_REGION_COUNT + 1u),
           "!cortex_m4_configure_implementation( &cpu, 86u, 4u, CORTEX_M4_MPU_REGION_COUNT + "
           "1u)");
    expect(state, cortex_m4_configure_implementation(&cpu, 86u, 4u, 0u),
           "cortex_m4_configure_implementation(&cpu, 86u, 4u, 0u)");
    expect(state, cpu.external_irq_count == 86u, "cpu.external_irq_count == 86u");
    expect(state, cpu.priority_bits == 4u, "cpu.priority_bits == 4u");
    expect(state, cpu.mpu_region_count == 0u, "cpu.mpu_region_count == 0u");
    expect(state, cpu.irq_priority[0] == 0xf0u, "cpu.irq_priority[0] == 0xf0u");
    expect(state, cpu.irq_priority[85] == 0xf0u, "cpu.irq_priority[85] == 0xf0u");
    expect(state, cpu.irq_priority[86] == 0u, "cpu.irq_priority[86] == 0u");
    expect(state, cpu.irq_enabled[2] == 0x003fffffu, "cpu.irq_enabled[2] == 0x003fffffu");
    expect(state, cpu.irq_pending[2] == 0x003fffffu, "cpu.irq_pending[2] == 0x003fffffu");
    expect(state, cpu.irq_active[2] == 0x003fffffu, "cpu.irq_active[2] == 0x003fffffu");
    expect(state, cpu.irq_level[2] == 0x003fffffu, "cpu.irq_level[2] == 0x003fffffu");
    expect(state, cpu.irq_enabled[3] == 0u, "cpu.irq_enabled[3] == 0u");
    expect(state, cpu.mpu_control == 0u, "cpu.mpu_control == 0u");
    expect(state, cpu.mpu_region_number == 0u, "cpu.mpu_region_number == 0u");
    expect(state, cpu.mpu_region_base[0] == 0u, "cpu.mpu_region_base[0] == 0u");
    expect(state, cpu.mpu_region_attributes[7] == 0u, "cpu.mpu_region_attributes[7] == 0u");
}

static void test_public_ppb_access(TestState* state) {
    TestBus bus = {0};
    CortexM4 cpu = create_cpu(&bus);
    expect(state,
           cortex_m4_bus_write(&cpu, ITM_LOCK_ACCESS, 4u, CORTEX_M4_ACCESS_DATA, 0xc5acce55u),
           "cortex_m4_bus_write(&cpu, ITM_LOCK_ACCESS, 4u, CORTEX_M4_ACCESS_DATA, "
           "0xc5acce55u)");
    expect(state, cortex_m4_bus_write(&cpu, ITM_TRACE_ENABLE, 4u, CORTEX_M4_ACCESS_DATA, 1u),
           "cortex_m4_bus_write(&cpu, ITM_TRACE_ENABLE, 4u, CORTEX_M4_ACCESS_DATA, 1u)");
    expect(state, cortex_m4_bus_write(&cpu, ITM_TRACE_PRIVILEGE, 4u, CORTEX_M4_ACCESS_DATA, 1u),
           "cortex_m4_bus_write(&cpu, ITM_TRACE_PRIVILEGE, 4u, CORTEX_M4_ACCESS_DATA, 1u)");
    expect(state, cortex_m4_bus_write(&cpu, ITM_TRACE_CONTROL, 4u, CORTEX_M4_ACCESS_DATA, 1u),
           "cortex_m4_bus_write(&cpu, ITM_TRACE_CONTROL, 4u, CORTEX_M4_ACCESS_DATA, 1u)");
    expect(state, cortex_m4_bus_write(&cpu, DEMCR, 4u, CORTEX_M4_ACCESS_DATA, 1u << 24u),
           "cortex_m4_bus_write(&cpu, DEMCR, 4u, CORTEX_M4_ACCESS_DATA, 1u << 24u)");
    expect(state, cortex_m4_bus_write(&cpu, ITM_STIMULUS, 1u, CORTEX_M4_ACCESS_DATA, 0x5au),
           "cortex_m4_bus_write(&cpu, ITM_STIMULUS, 1u, CORTEX_M4_ACCESS_DATA, 0x5au)");
    expect(state, cpu.debug.itm_stimulus[0] == 0x5au, "cpu.debug.itm_stimulus[0] == 0x5au");
    expect(
        state,
        cortex_m4_bus_write(&cpu, ITM_STIMULUS + 1u, 1u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, 0xa5u),
        "cortex_m4_bus_write(&cpu, ITM_STIMULUS + 1u, 1u, "
        "CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, 0xa5u)");
    expect(state, cpu.debug.itm_stimulus[0] == 0xa55au, "cpu.debug.itm_stimulus[0] == 0xa55au");
    cpu.control = CORTEX_M4_CONTROL_NPRIV;
    expect(state, cortex_m4_bus_write(&cpu, ITM_STIMULUS + 2u, 1u, CORTEX_M4_ACCESS_DATA, 0x3cu),
           "cortex_m4_bus_write(&cpu, ITM_STIMULUS + 2u, 1u, CORTEX_M4_ACCESS_DATA, 0x3cu)");
    expect(state, cpu.debug.itm_stimulus[0] == 0x3ca55au, "cpu.debug.itm_stimulus[0] == 0x3ca55au");
    cpu.debug.itm_trace_privilege = 0u;
    const uint32_t stimulus = cpu.debug.itm_stimulus[0];
    expect(state, !cortex_m4_bus_write(&cpu, ITM_STIMULUS, 1u, CORTEX_M4_ACCESS_DATA, 0u),
           "!cortex_m4_bus_write(&cpu, ITM_STIMULUS, 1u, CORTEX_M4_ACCESS_DATA, 0u)");
    expect(state, cpu.debug.itm_stimulus[0] == stimulus, "cpu.debug.itm_stimulus[0] == stimulus");
    expect(state, !cortex_m4_bus_write(&cpu, NVIC_STIR, 4u, CORTEX_M4_ACCESS_DATA, 1u),
           "!cortex_m4_bus_write(&cpu, NVIC_STIR, 4u, CORTEX_M4_ACCESS_DATA, 1u)");
    cpu.ccr |= 1u << 1u;
    expect(state, cortex_m4_bus_write(&cpu, NVIC_STIR, 4u, CORTEX_M4_ACCESS_DATA, 1u),
           "cortex_m4_bus_write(&cpu, NVIC_STIR, 4u, CORTEX_M4_ACCESS_DATA, 1u)");
    expect(state, cortex_m4_get_irq_pending(&cpu, 1u), "cortex_m4_get_irq_pending(&cpu, 1u)");
    expect(state, !cortex_m4_get_irq_pending(&cpu, 2u), "!cortex_m4_get_irq_pending(&cpu, 2u)");
    expect(state, cortex_m4_bus_write(&cpu, NVIC_STIR, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, 2u),
           "cortex_m4_bus_write(&cpu, NVIC_STIR, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, 2u)");
    expect(state, cortex_m4_get_irq_pending(&cpu, 2u), "cortex_m4_get_irq_pending(&cpu, 2u)");
    expect(state, !cortex_m4_get_irq_pending(&cpu, 3u), "!cortex_m4_get_irq_pending(&cpu, 3u)");
    expect(state,
           !cortex_m4_bus_write(&cpu, NVIC_STIR + 1u, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, 3u),
           "!cortex_m4_bus_write(&cpu, NVIC_STIR + 1u, 4u, "
           "CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, 3u)");
    expect(state, !cortex_m4_get_irq_pending(&cpu, 3u), "!cortex_m4_get_irq_pending(&cpu, 3u)");
    uint32_t value = 0u;
    cpu.control = 0u;
    expect(state,
           cortex_m4_system_read(&cpu, 0xe000edf0u, 4u, CORTEX_M4_ACCESS_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_system_read(&cpu, 0xe000edf0u, 4u, CORTEX_M4_ACCESS_DATA, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    cpu.control = CORTEX_M4_CONTROL_NPRIV;
    cpu.debug.itm_trace_privilege = 0u;
    expect(state,
           cortex_m4_system_read(&cpu, ITM_STIMULUS, 1u, CORTEX_M4_ACCESS_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_system_read(&cpu, ITM_STIMULUS, 1u, CORTEX_M4_ACCESS_DATA, &value) "
           "== CORTEX_M4_SYSTEM_ACCESS_REJECTED");
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
    test_extended_unstack_failure(&state, &cpu, &bus);
    test_invalid_return(&state, &cpu);
    test_copy_implementation(&state);
    test_configure_implementation(&state);
    test_public_ppb_access(&state);
    return test_finish(&state);
}
