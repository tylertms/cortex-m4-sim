#include "cortex_m4_internal.h"

#include <string.h>

#define PPB_START 0xe0000000u
#define PPB_END 0xe0100000u
#define ICTR 0xe000e004u
#define SYST_CSR 0xe000e010u
#define SYST_RVR 0xe000e014u
#define SYST_CVR 0xe000e018u
#define SYST_CALIB 0xe000e01cu
#define NVIC_ISER 0xe000e100u
#define NVIC_ICER 0xe000e180u
#define NVIC_ISPR 0xe000e200u
#define NVIC_ICPR 0xe000e280u
#define NVIC_IABR 0xe000e300u
#define NVIC_IPR 0xe000e400u
#define SCB_CPUID 0xe000ed00u
#define SCB_ICSR 0xe000ed04u
#define SCB_VTOR 0xe000ed08u
#define SCB_AIRCR 0xe000ed0cu
#define SCB_SCR 0xe000ed10u
#define SCB_CCR 0xe000ed14u
#define SCB_SHPR 0xe000ed18u
#define SCB_SHCSR 0xe000ed24u
#define SCB_CFSR 0xe000ed28u
#define SCB_HFSR 0xe000ed2cu
#define SCB_DFSR 0xe000ed30u
#define SCB_MMFAR 0xe000ed34u
#define SCB_BFAR 0xe000ed38u
#define SCB_AFSR 0xe000ed3cu
#define SCB_ID_PFR0 0xe000ed40u
#define SCB_ID_ISAR5 0xe000ed74u
#define SCB_CPACR 0xe000ed88u
#define NVIC_STIR 0xe000ef00u
#define FPU_FPCCR 0xe000ef34u
#define FPU_FPCAR 0xe000ef38u
#define FPU_FPDSCR 0xe000ef3cu
#define FPU_MVFR0 0xe000ef40u
#define FPU_MVFR1 0xe000ef44u
#define FPU_MVFR2 0xe000ef48u
#define FPCCR_ASPEN (1u << 31)
#define FPCCR_LSPEN (1u << 30)
#define FPCCR_THREAD (1u << 3)
#define FPCCR_LSPACT 1u
#define SCR_SEVONPEND (1u << 4)

static bool valid_access(uint32_t address, uint8_t size) {
    if ((size != 1 && size != 2 && size != 4) || (address & (size - 1u)) != 0 ||
        (address & 3u) + size > 4u) {
        return false;
    }
    if (size == 4u) {
        return (address & 3u) == 0u;
    }
    return (address >= SCB_SHPR && address + size <= SCB_SHPR + 12u) ||
           (address >= SCB_CFSR && address + size <= SCB_CFSR + 4u) ||
           (address >= NVIC_IPR && address + size <= NVIC_IPR + CORTEX_M4_IRQ_COUNT);
}

static bool privileged_access(const CortexM4* cpu, CortexM4Access access) {
    return access == CORTEX_M4_ACCESS_DEBUG ||
           (access == CORTEX_M4_ACCESS_DATA &&
            ((cpu->xpsr & 0x1ffu) != 0u || (cpu->control & CORTEX_M4_CONTROL_NPRIV) == 0u));
}

static bool debug_access_permitted(const CortexM4* cpu, uint32_t address,
                                   CortexM4Access access) {
    if (privileged_access(cpu, access)) {
        return true;
    }
    if (access != CORTEX_M4_ACCESS_UNPRIVILEGED_DATA || address >= 0xe0000080u) {
        return false;
    }
    const uint8_t port_group = (uint8_t)((address >> 5u) & 3u);
    return (cpu->debug.itm_trace_privilege & (1u << port_group)) != 0u;
}

static uint8_t irq_word_count(const CortexM4* cpu) {
    return (uint8_t)((cpu->external_irq_count + 31u) / 32u);
}

static uint32_t irq_word_mask(const CortexM4* cpu, uint8_t index) {
    const uint16_t first_irq = (uint16_t)index * 32u;
    const uint16_t remaining = cpu->external_irq_count - first_irq;
    return remaining >= 32u ? UINT32_MAX : (1u << remaining) - 1u;
}

static uint32_t read_partial(uint32_t value, uint32_t address, uint8_t size) {
    const uint32_t shift = (address & 3u) * 8u;
    if (size == 1) {
        return (value >> shift) & 0xffu;
    }
    if (size == 2) {
        return (value >> shift) & 0xffffu;
    }
    return value;
}

static uint32_t write_partial(uint32_t previous, uint32_t address, uint8_t size,
                              uint32_t value) {
    const uint32_t shift = (address & 3u) * 8u;
    if (size == 1) {
        return (previous & ~(0xffu << shift)) | ((value & 0xffu) << shift);
    }
    if (size == 2) {
        return (previous & ~(0xffffu << shift)) | ((value & 0xffffu) << shift);
    }
    return value;
}

static uint8_t exception_priority(const CortexM4* cpu, uint16_t exception) {
    if (exception >= 16 && exception < cpu->external_irq_count + 16u) {
        return cpu->irq_priority[exception - 16u];
    }
    if (exception >= 4 && exception <= 15) {
        return cpu->system_priority[exception - 4u];
    }
    return 0;
}

static int16_t exception_preemption(const CortexM4* cpu, uint16_t exception) {
    if (exception == 2) {
        return -2;
    }
    if (exception == 3) {
        return -1;
    }
    return cortex_m4_system_priority(cpu, exception_priority(cpu, exception)).preemption;
}

CortexM4Priority cortex_m4_system_priority(const CortexM4* cpu, uint8_t priority) {
    CortexM4Priority result = {0, 0};
    if (cpu == NULL) {
        return result;
    }
    const uint8_t group = (uint8_t)((cpu->aircr >> 8) & 7u);
    const uint8_t available_preemption = (uint8_t)(7u - group);
    const uint8_t preemption_bits = cpu->priority_bits < available_preemption
                                        ? cpu->priority_bits
                                        : available_preemption;
    const uint8_t subpriority_bits = (uint8_t)(cpu->priority_bits - preemption_bits);
    const uint8_t logical = priority >> (8u - cpu->priority_bits);
    result.preemption = logical >> subpriority_bits;
    result.subpriority = logical & (uint8_t)((1u << subpriority_bits) - 1u);
    return result;
}

bool cortex_m4_system_exception_masked(const CortexM4* cpu, uint16_t exception) {
    if (cpu == NULL || exception == 0 || exception == 2) {
        return false;
    }
    if (cpu->faultmask != 0) {
        return true;
    }
    if (exception == 3) {
        return false;
    }
    if (cpu->primask != 0) {
        return true;
    }
    if (cpu->basepri == 0) {
        return false;
    }
    const CortexM4Priority priority =
        cortex_m4_system_priority(cpu, exception_priority(cpu, exception));
    const CortexM4Priority threshold =
        cortex_m4_system_priority(cpu, (uint8_t)cpu->basepri);
    return priority.preemption >= threshold.preemption;
}

bool cortex_m4_system_exception_can_preempt(const CortexM4* cpu, uint16_t candidate,
                                            uint16_t current) {
    if (cpu == NULL || candidate == 0 ||
        cortex_m4_system_exception_masked(cpu, candidate)) {
        return false;
    }
    if (current == 0) {
        return true;
    }
    return exception_preemption(cpu, candidate) < exception_preemption(cpu, current);
}

bool cortex_m4_system_exception_before(const CortexM4* cpu, uint16_t left, uint16_t right) {
    if (left == 0) {
        return false;
    }
    if (right == 0) {
        return true;
    }
    const int16_t left_preemption = exception_preemption(cpu, left);
    const int16_t right_preemption = exception_preemption(cpu, right);
    if (left_preemption != right_preemption) {
        return left_preemption < right_preemption;
    }
    const CortexM4Priority left_priority =
        cortex_m4_system_priority(cpu, exception_priority(cpu, left));
    const CortexM4Priority right_priority =
        cortex_m4_system_priority(cpu, exception_priority(cpu, right));
    if (left_priority.subpriority != right_priority.subpriority) {
        return left_priority.subpriority < right_priority.subpriority;
    }
    return left < right;
}

static bool external_pending_enabled(const CortexM4* cpu) {
    for (uint8_t index = 0; index < irq_word_count(cpu); index++) {
        if ((cpu->irq_pending[index] & cpu->irq_enabled[index]) != 0) {
            return true;
        }
    }
    return false;
}

static bool wakeup_pending(const CortexM4* cpu) {
    return cpu->system_pending != 0 || external_pending_enabled(cpu);
}

void cortex_m4_system_wait_for_interrupt(CortexM4* cpu) {
    if (cpu != NULL) {
        cpu->sleeping = !wakeup_pending(cpu);
    }
}

void cortex_m4_system_set_pending(CortexM4* cpu, uint16_t exception, bool pending) {
    if (cpu == NULL || exception < 2 || exception >= cpu->external_irq_count + 16u) {
        return;
    }
    bool was_pending = false;
    if (exception < 16) {
        const uint32_t mask = 1u << exception;
        was_pending = (cpu->system_pending & mask) != 0;
        if (pending) {
            cpu->system_pending |= mask;
        } else {
            cpu->system_pending &= ~mask;
        }
    } else {
        const uint16_t irq = exception - 16u;
        const uint32_t mask = 1u << (irq & 31u);
        was_pending = (cpu->irq_pending[irq / 32u] & mask) != 0;
        if (pending) {
            cpu->irq_pending[irq / 32u] |= mask;
        } else {
            cpu->irq_pending[irq / 32u] &= ~mask;
        }
    }
    if (pending && !was_pending && (cpu->scr & SCR_SEVONPEND) != 0) {
        cpu->event_register = true;
    }
    if (pending && wakeup_pending(cpu)) {
        cpu->sleeping = false;
    }
}

static uint16_t pending_vector(const CortexM4* cpu) {
    uint16_t selected = 0;
    if ((cpu->system_pending & (1u << 2)) != 0) {
        selected = 2;
    }
    if ((cpu->system_pending & (1u << 3)) != 0 &&
        cortex_m4_system_exception_before(cpu, 3, selected)) {
        selected = 3;
    }
    const uint8_t system_exceptions[] = {4, 5, 6, 11, 12, 14, 15};
    for (uint8_t index = 0; index < sizeof(system_exceptions); index++) {
        const uint8_t exception = system_exceptions[index];
        if ((cpu->system_pending & (1u << exception)) != 0 &&
            cortex_m4_system_exception_before(cpu, exception, selected)) {
            selected = exception;
        }
    }
    for (uint16_t irq = 0; irq < cpu->external_irq_count; irq++) {
        const uint32_t mask = 1u << (irq & 31u);
        if ((cpu->irq_pending[irq / 32u] & cpu->irq_enabled[irq / 32u] & mask) != 0 &&
            cortex_m4_system_exception_before(cpu, irq + 16u, selected)) {
            selected = irq + 16u;
        }
    }
    return selected;
}

static uint32_t shcsr_value(const CortexM4* cpu) {
    uint32_t value = cpu->shcsr & 0x00070000u;
    const uint8_t active_exceptions[] = {4, 5, 6, 11, 12, 14, 15};
    const uint8_t active_bits[] = {0, 1, 3, 7, 8, 10, 11};
    for (uint8_t index = 0; index < sizeof(active_exceptions); index++) {
        if (cortex_m4_exception_advanced_active(cpu, active_exceptions[index])) {
            value |= 1u << active_bits[index];
        }
    }
    if ((cpu->system_pending & (1u << 6)) != 0) {
        value |= 1u << 12;
    }
    if ((cpu->system_pending & (1u << 4)) != 0) {
        value |= 1u << 13;
    }
    if ((cpu->system_pending & (1u << 5)) != 0) {
        value |= 1u << 14;
    }
    if ((cpu->system_pending & (1u << 11)) != 0) {
        value |= 1u << 15;
    }
    return value;
}

static uint32_t read_priority_bytes(const uint8_t* priorities, uint32_t offset,
                                    uint8_t size) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; index++) {
        value |= (uint32_t)priorities[offset + index] << (index * 8u);
    }
    return value;
}

static void write_priority_bytes(uint8_t* priorities, uint32_t offset, uint8_t size,
                                 uint32_t value, uint8_t priority_bits) {
    const uint8_t mask = (uint8_t)(0xffu << (8u - priority_bits));
    for (uint8_t index = 0; index < size; index++) {
        priorities[offset + index] = (uint8_t)(value >> (index * 8u)) & mask;
    }
}

static bool configurable_system_exception(uint8_t exception) {
    return exception == 4 || exception == 5 || exception == 6 || exception == 11 ||
           exception == 12 || exception == 14 || exception == 15;
}

static uint32_t read_system_priority(const CortexM4* cpu, uint32_t offset, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; index++) {
        const uint8_t exception = (uint8_t)(offset + index + 4u);
        if (configurable_system_exception(exception)) {
            value |= (uint32_t)cpu->system_priority[offset + index] << (index * 8u);
        }
    }
    return value;
}

static void write_system_priority(CortexM4* cpu, uint32_t offset, uint8_t size,
                                  uint32_t value) {
    const uint8_t mask = (uint8_t)(0xffu << (8u - cpu->priority_bits));
    for (uint8_t index = 0; index < size; index++) {
        const uint8_t exception = (uint8_t)(offset + index + 4u);
        if (configurable_system_exception(exception)) {
            cpu->system_priority[offset + index] = (uint8_t)(value >> (index * 8u)) & mask;
        }
    }
}

static CortexM4SystemAccess accepted_read(uint32_t data, uint32_t address, uint8_t size,
                                          uint32_t* value) {
    *value = read_partial(data, address, size);
    return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
}

CortexM4SystemAccess cortex_m4_system_read(CortexM4* cpu, uint32_t address, uint8_t size,
                                           CortexM4Access access, uint32_t* value) {
    if (address >= 0xe0080000u && address < 0xe00a0000u) {
        return CORTEX_M4_SYSTEM_ACCESS_OUTSIDE;
    }
    if (address < PPB_START || address >= PPB_END) {
        return CORTEX_M4_SYSTEM_ACCESS_OUTSIDE;
    }
    if (cpu == NULL || value == NULL) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    if (cortex_m4_debug_address(address)) {
        if (!debug_access_permitted(cpu, address, access)) {
            return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
        }
        return cortex_m4_debug_read(cpu, address, size, value);
    }
    if (!valid_access(address, size) || !privileged_access(cpu, access)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    const uint32_t aligned = address & ~3u;
    if (aligned == ICTR) {
        return accepted_read((cpu->external_irq_count - 1u) / 32u, address, size, value);
    }
    if (aligned == SYST_CSR) {
        const uint32_t data = cpu->systick_control;
        cpu->systick_control &= ~(1u << 16);
        return accepted_read(data, address, size, value);
    }
    if (aligned == SYST_RVR) {
        return accepted_read(cpu->systick_reload, address, size, value);
    }
    if (aligned == SYST_CVR) {
        return accepted_read(cpu->systick_current, address, size, value);
    }
    if (aligned == SYST_CALIB) {
        return accepted_read(cpu->systick_calibration, address, size, value);
    }
    if (aligned >= NVIC_ISER && aligned < NVIC_ISER + irq_word_count(cpu) * 4u) {
        const uint8_t index = (uint8_t)((aligned - NVIC_ISER) / 4u);
        return accepted_read(cpu->irq_enabled[index] & irq_word_mask(cpu, index), address,
                             size, value);
    }
    if (aligned >= NVIC_ICER && aligned < NVIC_ICER + irq_word_count(cpu) * 4u) {
        const uint8_t index = (uint8_t)((aligned - NVIC_ICER) / 4u);
        return accepted_read(cpu->irq_enabled[index] & irq_word_mask(cpu, index), address,
                             size, value);
    }
    if (aligned >= NVIC_ISPR && aligned < NVIC_ISPR + irq_word_count(cpu) * 4u) {
        const uint8_t index = (uint8_t)((aligned - NVIC_ISPR) / 4u);
        return accepted_read(cpu->irq_pending[index] & irq_word_mask(cpu, index), address,
                             size, value);
    }
    if (aligned >= NVIC_ICPR && aligned < NVIC_ICPR + irq_word_count(cpu) * 4u) {
        const uint8_t index = (uint8_t)((aligned - NVIC_ICPR) / 4u);
        return accepted_read(cpu->irq_pending[index] & irq_word_mask(cpu, index), address,
                             size, value);
    }
    if (aligned >= NVIC_IABR && aligned < NVIC_IABR + irq_word_count(cpu) * 4u) {
        const uint8_t index = (uint8_t)((aligned - NVIC_IABR) / 4u);
        return accepted_read(cpu->irq_active[index] & irq_word_mask(cpu, index), address,
                             size, value);
    }
    if (address >= NVIC_IPR && address + size <= NVIC_IPR + cpu->external_irq_count) {
        *value = read_priority_bytes(cpu->irq_priority, address - NVIC_IPR, size);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_CPUID) {
        return accepted_read(0x410fc241u, address, size, value);
    }
    if (aligned == SCB_ICSR) {
        const uint16_t pending = pending_vector(cpu);
        uint32_t data = cpu->xpsr & 0x1ffu;
        data |= (uint32_t)pending << 12;
        if (cpu->exception_depth <= 1) {
            data |= 1u << 11;
        }
        if (external_pending_enabled(cpu)) {
            data |= 1u << 22;
        }
        if ((cpu->system_pending & (1u << 2)) != 0) {
            data |= 1u << 31;
        }
        if ((cpu->system_pending & (1u << 14)) != 0) {
            data |= 1u << 28;
        }
        if ((cpu->system_pending & (1u << 15)) != 0) {
            data |= 1u << 26;
        }
        return accepted_read(data, address, size, value);
    }
    if (aligned == SCB_VTOR) {
        return accepted_read(cpu->vtor, address, size, value);
    }
    if (aligned == SCB_AIRCR) {
        return accepted_read(0xfa050000u | (cpu->aircr & 0x00008700u), address, size,
                             value);
    }
    if (aligned == SCB_SCR) {
        return accepted_read(cpu->scr, address, size, value);
    }
    if (aligned == SCB_CCR) {
        return accepted_read(cpu->ccr, address, size, value);
    }
    if (address >= SCB_SHPR && address + size <= SCB_SHPR + 12u) {
        *value = read_system_priority(cpu, address - SCB_SHPR, size);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_SHCSR) {
        return accepted_read(shcsr_value(cpu), address, size, value);
    }
    if (aligned == SCB_CFSR) {
        return accepted_read(cpu->cfsr, address, size, value);
    }
    if (aligned == SCB_HFSR) {
        return accepted_read(cpu->hfsr, address, size, value);
    }
    if (aligned == SCB_DFSR) {
        return accepted_read(cpu->dfsr, address, size, value);
    }
    if (aligned == SCB_MMFAR) {
        return accepted_read(cpu->mmfar, address, size, value);
    }
    if (aligned == SCB_BFAR) {
        return accepted_read(cpu->bfar, address, size, value);
    }
    if (aligned == SCB_AFSR) {
        return accepted_read(cpu->afsr, address, size, value);
    }
    const uint32_t identification[] = {
        0x00000030u, 0x00000200u, 0x00100000u, 0x00000000u, 0x00000030u,
        0x00000000u, 0x01000000u, 0x00000000u, 0x01141110u, 0x02111000u,
        0x21112231u, 0x01111110u, 0x01310102u, 0x00000000u,
    };
    if (aligned >= SCB_ID_PFR0 && aligned <= SCB_ID_ISAR5) {
        return accepted_read(identification[(aligned - SCB_ID_PFR0) / 4u], address, size,
                             value);
    }
    if (aligned == SCB_CPACR) {
        return accepted_read(cpu->cpacr, address, size, value);
    }
    if (aligned == FPU_FPCCR) {
        return accepted_read(cpu->fpccr, address, size, value);
    }
    if (aligned == FPU_FPCAR) {
        return accepted_read(cpu->fpcar, address, size, value);
    }
    if (aligned == FPU_FPDSCR) {
        return accepted_read(cpu->fpdscr, address, size, value);
    }
    if (aligned == FPU_MVFR0) {
        return accepted_read(0x10110021u, address, size, value);
    }
    if (aligned == FPU_MVFR1) {
        return accepted_read(0x11000011u, address, size, value);
    }
    if (aligned == FPU_MVFR2) {
        return accepted_read(0u, address, size, value);
    }
    return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
}

static uint32_t access_value(uint32_t address, uint8_t size, uint32_t value) {
    return write_partial(0, address, size, value);
}

static void write_shcsr(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t value) {
    const uint32_t written = write_partial(shcsr_value(cpu), address, size, value);
    cpu->shcsr = written & 0x00070000u;
    cortex_m4_system_set_pending(cpu, 6, (written & (1u << 12)) != 0);
    cortex_m4_system_set_pending(cpu, 4, (written & (1u << 13)) != 0);
    cortex_m4_system_set_pending(cpu, 5, (written & (1u << 14)) != 0);
    cortex_m4_system_set_pending(cpu, 11, (written & (1u << 15)) != 0);
}

CortexM4SystemAccess cortex_m4_system_write(CortexM4* cpu, uint32_t address, uint8_t size,
                                            CortexM4Access access, uint32_t value) {
    if (address >= 0xe0080000u && address < 0xe00a0000u) {
        return CORTEX_M4_SYSTEM_ACCESS_OUTSIDE;
    }
    if (address < PPB_START || address >= PPB_END) {
        return CORTEX_M4_SYSTEM_ACCESS_OUTSIDE;
    }
    if (cpu == NULL) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    const uint32_t aligned = address & ~3u;
    if (cortex_m4_debug_address(address)) {
        if (!debug_access_permitted(cpu, address, access)) {
            return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
        }
        return cortex_m4_debug_write(cpu, address, size, value);
    }
    const bool user_stir = address == NVIC_STIR && size == 4u &&
                           access == CORTEX_M4_ACCESS_UNPRIVILEGED_DATA &&
                           (cpu->ccr & (1u << 1u)) != 0u;
    if (!valid_access(address, size) && !user_stir) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    if (!privileged_access(cpu, access) && !user_stir) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    if (aligned == SYST_CSR) {
        cpu->systick_control =
            write_partial(cpu->systick_control, address, size, value) & 0x00010007u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SYST_RVR) {
        cpu->systick_reload =
            write_partial(cpu->systick_reload, address, size, value) & 0x00ffffffu;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SYST_CVR) {
        cpu->systick_current = 0;
        cpu->systick_control &= ~(1u << 16);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned >= NVIC_ISER && aligned < NVIC_ISER + irq_word_count(cpu) * 4u) {
        const uint8_t index = (uint8_t)((aligned - NVIC_ISER) / 4u);
        cpu->irq_enabled[index] |=
            access_value(address, size, value) & irq_word_mask(cpu, index);
        if (wakeup_pending(cpu)) {
            cpu->sleeping = false;
        }
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned >= NVIC_ICER && aligned < NVIC_ICER + irq_word_count(cpu) * 4u) {
        const uint8_t index = (uint8_t)((aligned - NVIC_ICER) / 4u);
        cpu->irq_enabled[index] &=
            ~(access_value(address, size, value) & irq_word_mask(cpu, index));
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned >= NVIC_ISPR && aligned < NVIC_ISPR + irq_word_count(cpu) * 4u) {
        const uint16_t first_irq = (uint16_t)(((aligned - NVIC_ISPR) / 4u) * 32u);
        const uint32_t bits = access_value(address, size, value);
        for (uint8_t bit = 0; bit < 32 && first_irq + bit < cpu->external_irq_count;
             bit++) {
            if ((bits & (1u << bit)) != 0) {
                cortex_m4_system_set_pending(cpu, first_irq + bit + 16u, true);
            }
        }
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned >= NVIC_ICPR && aligned < NVIC_ICPR + irq_word_count(cpu) * 4u) {
        const uint16_t first_irq = (uint16_t)(((aligned - NVIC_ICPR) / 4u) * 32u);
        const uint32_t bits = access_value(address, size, value);
        for (uint8_t bit = 0; bit < 32 && first_irq + bit < cpu->external_irq_count;
             bit++) {
            if ((bits & (1u << bit)) != 0) {
                cortex_m4_system_set_pending(cpu, first_irq + bit + 16u, false);
            }
        }
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (address >= NVIC_IPR && address + size <= NVIC_IPR + cpu->external_irq_count) {
        write_priority_bytes(cpu->irq_priority, address - NVIC_IPR, size, value,
                             cpu->priority_bits);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_ICSR) {
        const uint32_t written = access_value(address, size, value);
        if ((written & (1u << 31)) != 0) {
            cortex_m4_system_set_pending(cpu, 2, true);
        }
        if ((written & (1u << 28)) != 0) {
            cortex_m4_system_set_pending(cpu, 14, true);
        }
        if ((written & (1u << 27)) != 0) {
            cortex_m4_system_set_pending(cpu, 14, false);
        }
        if ((written & (1u << 26)) != 0) {
            cortex_m4_system_set_pending(cpu, 15, true);
        }
        if ((written & (1u << 25)) != 0) {
            cortex_m4_system_set_pending(cpu, 15, false);
        }
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_VTOR) {
        cpu->vtor = write_partial(cpu->vtor, address, size, value) & 0xffffff80u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_AIRCR) {
        if (size == 4 && (value >> 16) == 0x05fau) {
            cpu->aircr = value & 0x00000700u;
            if ((value & (1u << 2)) != 0) {
                cpu->reset_requested = true;
            }
        }
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_SCR) {
        cpu->scr = write_partial(cpu->scr, address, size, value) & 0x16u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_CCR) {
        cpu->ccr = write_partial(cpu->ccr, address, size, value) & 0x0000031bu;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (address >= SCB_SHPR && address + size <= SCB_SHPR + 12u) {
        write_system_priority(cpu, address - SCB_SHPR, size, value);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_SHCSR) {
        write_shcsr(cpu, address, size, value);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_CFSR) {
        cpu->cfsr &= ~access_value(address, size, value);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_HFSR) {
        cpu->hfsr &= ~(access_value(address, size, value) & 0xc0000002u);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_DFSR) {
        cpu->dfsr &= ~(access_value(address, size, value) & 0x1fu);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_MMFAR) {
        cpu->mmfar = write_partial(cpu->mmfar, address, size, value);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_BFAR) {
        cpu->bfar = write_partial(cpu->bfar, address, size, value);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_AFSR) {
        cpu->afsr = write_partial(cpu->afsr, address, size, value);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == SCB_CPACR) {
        cpu->cpacr = write_partial(cpu->cpacr, address, size, value) & 0x00f00000u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (address == NVIC_STIR) {
        const uint16_t irq = (uint16_t)(value & 0x1ffu);
        if (size != 4 || irq >= cpu->external_irq_count) {
            return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
        }
        cortex_m4_system_set_pending(cpu, irq + 16u, true);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == FPU_FPCCR) {
        const uint32_t control = write_partial(cpu->fpccr, address, size, value);
        cpu->fpccr = (cpu->fpccr & ~(FPCCR_ASPEN | FPCCR_LSPEN)) |
                     (control & (FPCCR_ASPEN | FPCCR_LSPEN));
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == FPU_FPCAR) {
        cpu->fpcar = write_partial(cpu->fpcar, address, size, value) & 0xfffffff8u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == FPU_FPDSCR) {
        cpu->fpdscr = write_partial(cpu->fpdscr, address, size, value) & 0x07c00000u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (aligned == ICTR || aligned == SYST_CALIB ||
        (aligned >= NVIC_IABR && aligned < NVIC_IABR + irq_word_count(cpu) * 4u) ||
        aligned == SCB_CPUID || (aligned >= SCB_ID_PFR0 && aligned <= SCB_ID_ISAR5) ||
        aligned == FPU_MVFR0 || aligned == FPU_MVFR1 || aligned == FPU_MVFR2) {
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
}

void cortex_m4_system_reset(CortexM4* cpu) {
    if (cpu == NULL) {
        return;
    }
    cpu->vtor = 0;
    cpu->aircr = 0;
    cpu->scr = 0;
    cpu->ccr = 1u << 9;
    cpu->shcsr = 0;
    cpu->cfsr = 0;
    cpu->hfsr = 0;
    cpu->dfsr = 0;
    cpu->mmfar = 0;
    cpu->bfar = 0;
    cpu->afsr = 0;
    cpu->cpacr = 0;
    cpu->fpccr = FPCCR_ASPEN | FPCCR_LSPEN;
    cpu->fpcar = 0;
    cpu->fpdscr = 0;
    cpu->fpscr = 0;
    cpu->primask = 0;
    cpu->basepri = 0;
    cpu->faultmask = 0;
    cpu->system_pending = 0;
    cpu->systick_control = 0;
    cpu->systick_reload = 0;
    cpu->systick_current = 0;
    cpu->exception_frame_depth = 0;
    cortex_m4_exception_advanced_reset(cpu);
    cortex_m4_mpu_reset(cpu);
    cortex_m4_debug_reset(cpu);
    cortex_m4_timing_reset(cpu);
    memset(cpu->irq_enabled, 0, sizeof(cpu->irq_enabled));
    memset(cpu->irq_pending, 0, sizeof(cpu->irq_pending));
    memset(cpu->irq_active, 0, sizeof(cpu->irq_active));
    memset(cpu->irq_priority, 0, sizeof(cpu->irq_priority));
    memset(cpu->system_priority, 0, sizeof(cpu->system_priority));
    memset(cpu->exception_frames, 0, sizeof(cpu->exception_frames));
}

static bool write_stack_word(CortexM4* cpu, uint32_t address, uint32_t value) {
    return cortex_m4_bus_write(cpu, address, 4, CORTEX_M4_ACCESS_DATA, value);
}

static bool read_stack_word(CortexM4* cpu, uint32_t address, uint32_t* value) {
    return cortex_m4_bus_read(cpu, address, 4, CORTEX_M4_ACCESS_DATA, value);
}

static bool write_fp_frame(CortexM4* cpu, uint32_t address) {
    for (uint8_t index = 0; index < 16; index++) {
        if (!write_stack_word(cpu, address + index * 4u, cpu->fp_registers[index])) {
            return false;
        }
    }
    return write_stack_word(cpu, address + 64u, cpu->fpscr) &&
           write_stack_word(cpu, address + 68u, 0);
}

bool cortex_m4_system_stack_exception_frame(CortexM4* cpu, uint32_t* stack_pointer,
                                            uint32_t* return_value) {
    if (cpu == NULL || stack_pointer == NULL || return_value == NULL ||
        cpu->exception_frame_depth >= CORTEX_M4_EXCEPTION_FRAME_LIMIT) {
        return false;
    }
    const bool was_thread = (cpu->xpsr & 0x1ffu) == 0;
    const bool use_psp = was_thread && (cpu->control & CORTEX_M4_CONTROL_SPSEL) != 0;
    const bool extended =
        (cpu->control & CORTEX_M4_CONTROL_FPCA) != 0 && (cpu->fpccr & FPCCR_ASPEN) != 0;
    const bool lazy = extended && (cpu->fpccr & FPCCR_LSPEN) != 0;
    const bool padding = (cpu->ccr & (1u << 9)) != 0 && (*stack_pointer & 7u) != 0;
    uint32_t frame_address = *stack_pointer - (padding ? 4u : 0u) - (extended ? 104u : 32u);
    const uint32_t core_address = frame_address + (extended ? 72u : 0u);
    uint32_t stacked_xpsr = cortex_m4_xpsr_value(cpu);
    if (padding) {
        stacked_xpsr |= 1u << 9;
    }
    const uint32_t core_frame[8] = {
        cpu->registers[0],  cpu->registers[1],  cpu->registers[2],  cpu->registers[3],
        cpu->registers[12], cpu->registers[14], cpu->registers[15], stacked_xpsr,
    };
    if (extended && !lazy && !write_fp_frame(cpu, frame_address)) {
        return false;
    }
    for (uint8_t index = 0; index < 8; index++) {
        if (!write_stack_word(cpu, core_address + index * 4u, core_frame[index])) {
            return false;
        }
    }
    uint32_t exception_return = !was_thread ? 0xfffffff1u
                                : use_psp   ? 0xfffffffdu
                                            : 0xfffffff9u;
    if (extended) {
        exception_return &= ~(1u << 4);
        cpu->fpcar = frame_address;
        cpu->fpccr = (cpu->fpccr & ~FPCCR_THREAD) | (was_thread ? FPCCR_THREAD : 0u) |
                     (lazy ? FPCCR_LSPACT : 0u);
    }
    CortexM4ExceptionFrame* const metadata =
        &cpu->exception_frames[cpu->exception_frame_depth++];
    metadata->address = frame_address;
    metadata->ici_address = cpu->ici_address;
    metadata->return_value = exception_return;
    metadata->stacked_xpsr = stacked_xpsr;
    metadata->it_state = cpu->it_state;
    metadata->ici_register = cpu->ici_register;
    metadata->extended = extended;
    metadata->lazy = lazy;
    metadata->ici_valid = cpu->ici_valid;
    *stack_pointer = frame_address;
    *return_value = exception_return;
    return true;
}

bool cortex_m4_system_materialize_lazy_fp(CortexM4* cpu) {
    if (cpu == NULL || (cpu->fpccr & FPCCR_LSPACT) == 0 ||
        cpu->exception_frame_depth == 0) {
        return true;
    }
    CortexM4ExceptionFrame* const metadata =
        &cpu->exception_frames[cpu->exception_frame_depth - 1u];
    if (!metadata->extended || !metadata->lazy || !write_fp_frame(cpu, metadata->address)) {
        return false;
    }
    metadata->lazy = false;
    cpu->fpccr &= ~FPCCR_LSPACT;
    return true;
}

bool cortex_m4_system_valid_exception_return(const CortexM4* cpu, uint32_t value) {
    if (cpu == NULL || (cpu->xpsr & 0x1ffu) == 0) {
        return false;
    }
    const bool canonical = value == 0xffffffe1u || value == 0xffffffe9u ||
                           value == 0xffffffedu || value == 0xfffffff1u ||
                           value == 0xfffffff9u || value == 0xfffffffdu;
    if (!canonical) {
        return false;
    }
    const bool return_thread = (value & (1u << 3)) != 0;
    if (return_thread) {
        return cpu->exception_depth == 1 ||
               (cpu->exception_depth > 1 && (cpu->ccr & 1u) != 0);
    }
    return cpu->exception_depth > 1 && (value & (1u << 2)) == 0;
}

static bool read_fp_frame(CortexM4* cpu, uint32_t address, uint32_t* registers,
                          uint32_t* fpscr) {
    for (uint8_t index = 0; index < 16; index++) {
        if (!read_stack_word(cpu, address + index * 4u, &registers[index])) {
            return false;
        }
    }
    return read_stack_word(cpu, address + 64u, fpscr);
}

bool cortex_m4_system_unstack_exception_frame(CortexM4* cpu, uint32_t* stack_pointer,
                                              uint32_t value, uint16_t current_exception) {
    if (cpu != NULL) {
        cpu->exception_unstack_memory_fault = false;
    }
    if (!cortex_m4_exception_advanced_valid_return(cpu, value) || stack_pointer == NULL) {
        if (cpu != NULL) {
            cpu->cfsr |= 1u << 18;
        }
        return false;
    }
    const bool extended = (value & (1u << 4)) == 0;
    const uint32_t core_address = *stack_pointer + (extended ? 72u : 0u);
    uint32_t core_frame[8];
    for (uint8_t index = 0; index < 8; index++) {
        if (!read_stack_word(cpu, core_address + index * 4u, &core_frame[index])) {
            cpu->exception_unstack_memory_fault = true;
            return false;
        }
    }
    const bool return_thread = (value & (1u << 3)) != 0;
    if (!cortex_m4_exception_advanced_valid_stacked_xpsr(cpu, core_frame[7], value)) {
        cpu->cfsr |= (core_frame[7] & CORTEX_M4_XPSR_T) == 0 ? 1u << 17 : 1u << 18;
        return false;
    }
    uint32_t fp_registers[16];
    uint32_t fpscr = 0;
    const bool lazy =
        extended && (cpu->fpccr & FPCCR_LSPACT) != 0 && cpu->fpcar == *stack_pointer;
    if (extended && !lazy && !read_fp_frame(cpu, *stack_pointer, fp_registers, &fpscr)) {
        cpu->exception_unstack_memory_fault = true;
        return false;
    }
    cpu->registers[0] = core_frame[0];
    cpu->registers[1] = core_frame[1];
    cpu->registers[2] = core_frame[2];
    cpu->registers[3] = core_frame[3];
    cpu->registers[12] = core_frame[4];
    cpu->registers[14] = core_frame[5];
    cpu->registers[15] = core_frame[6] & ~1u;
    cortex_m4_load_xpsr(cpu, core_frame[7]);
    if (extended && !lazy) {
        memcpy(cpu->fp_registers, fp_registers, sizeof(fp_registers));
        cpu->fpscr = fpscr;
    }
    if (extended) {
        cpu->fpccr &= ~(FPCCR_LSPACT | FPCCR_THREAD);
    }
    *stack_pointer = core_address + 32u + (((core_frame[7] & (1u << 9)) != 0) ? 4u : 0u);
    if (return_thread) {
        cpu->control =
            (cpu->control & ~(CORTEX_M4_CONTROL_SPSEL | CORTEX_M4_CONTROL_FPCA)) |
            ((value & (1u << 2)) != 0 ? CORTEX_M4_CONTROL_SPSEL : 0u) |
            (extended ? CORTEX_M4_CONTROL_FPCA : 0u);
    }
    if (cpu->exception_frame_depth != 0) {
        cpu->exception_frame_depth--;
    }
    (void)current_exception;
    return true;
}
