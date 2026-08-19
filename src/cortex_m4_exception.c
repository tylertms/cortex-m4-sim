#include "cortex_m4_internal.h"

void cortex_m4_raise_fault(CortexM4* cpu, uint8_t exception) {
    if (cpu == NULL || exception < 4 || exception > 6) {
        return;
    }
    cpu->instruction_faulted = true;
    const uint16_t current = (uint16_t)(cpu->xpsr & 0x1ffu);
    if (current == 2 || current == 3) {
        cpu->stop = CORTEX_M4_STOP_LOCKUP;
        return;
    }
    const uint32_t enable = 1u << (exception + 12u);
    if ((cpu->shcsr & enable) != 0 &&
        cortex_m4_system_exception_can_preempt(cpu, exception, current)) {
        cortex_m4_system_set_pending(cpu, exception, true);
    } else {
        cpu->hfsr |= 1u << 30;
        cortex_m4_system_set_pending(cpu, 3, true);
    }
    cpu->event_register = true;
    cpu->sleeping = false;
}

static bool enter_exception(CortexM4* cpu, uint16_t exception) {
    cortex_m4_timing_begin_exception(cpu);
    const bool was_thread = (cpu->xpsr & 0x1ffu) == 0;
    const bool used_psp = was_thread && (cpu->control & CORTEX_M4_CONTROL_SPSEL) != 0;
    uint32_t stack_pointer = used_psp ? cpu->psp : cpu->msp;
    uint32_t exception_return = 0;
    if (!cortex_m4_system_stack_exception_frame(cpu, &stack_pointer, &exception_return)) {
        cortex_m4_timing_abort(cpu);
        cortex_m4_exception_advanced_entry_fault(
            cpu, exception, CORTEX_M4_FAULT_STACKING,
            !cortex_m4_mpu_access_permitted(cpu, stack_pointer - 4u, 4,
                                            CORTEX_M4_ACCESS_DATA, true));
        return false;
    }
    if (used_psp) {
        cpu->psp = stack_pointer;
    } else {
        cpu->msp = stack_pointer;
    }
    exception = cortex_m4_exception_advanced_late_arrival(cpu, exception);
    uint32_t vector = 0;
    if (!cortex_m4_bus_read(cpu, cpu->vtor + exception * 4u, 4, CORTEX_M4_ACCESS_DATA,
                            &vector) ||
        (vector & 1u) == 0) {
        cortex_m4_exception_advanced_vector_fault(cpu);
        if (!cortex_m4_exception_advanced_hardfault_vector(cpu, &vector)) {
            cortex_m4_timing_abort(cpu);
            return false;
        }
        exception = 3u;
    }
    cortex_m4_debug_exception(cpu, exception);
    cpu->registers[14] = exception_return;
    cpu->registers[15] = vector & ~1u;
    cpu->it_state = 0;
    cpu->xpsr = (cpu->xpsr & ~0x1ffu) | exception | CORTEX_M4_XPSR_T;
    cpu->control &= ~CORTEX_M4_CONTROL_FPCA;
    cpu->sleeping = false;
    cpu->exclusive_valid = false;
    cortex_m4_exception_advanced_commit_entry(cpu, exception);
    const bool fp_frame = (exception_return & 0x10u) == 0;
    cortex_m4_debug_exception_cycles(cpu, fp_frame ? 29u : 12u);
    cortex_m4_timing_exception(cpu, fp_frame ? CORTEX_M4_TIMING_EXCEPTION_FP_ENTRY
                                             : CORTEX_M4_TIMING_EXCEPTION_ENTRY);
    return true;
}

bool cortex_m4_take_pending_exception(CortexM4* cpu) {
    const uint16_t current = (uint16_t)(cpu->xpsr & 0x1ffu);
    if ((cpu->system_pending & (1u << 2)) != 0 && current != 2) {
        return enter_exception(cpu, 2);
    }
    if ((cpu->system_pending & (1u << 3)) != 0 &&
        cortex_m4_system_exception_can_preempt(cpu, 3u, current)) {
        return enter_exception(cpu, 3);
    }
    uint16_t selected_exception = 0;
    const uint8_t system_exceptions[] = {4, 5, 6, 11, 12, 14, 15};
    for (uint8_t index = 0;
         index < sizeof(system_exceptions) / sizeof(system_exceptions[0]); index++) {
        const uint8_t exception = system_exceptions[index];
        if ((cpu->system_pending & (1u << exception)) == 0) {
            continue;
        }
        if (cortex_m4_system_exception_can_preempt(cpu, exception, current) &&
            (selected_exception == 0 ||
             cortex_m4_system_exception_before(cpu, exception, selected_exception))) {
            selected_exception = exception;
        }
    }
    for (uint16_t irq = 0; irq < cpu->external_irq_count; irq++) {
        const uint32_t mask = 1u << (irq & 31u);
        if ((cpu->irq_pending[irq / 32] & cpu->irq_enabled[irq / 32] & mask) == 0) {
            continue;
        }
        if (cortex_m4_system_exception_can_preempt(cpu, irq + 16u, current) &&
            (selected_exception == 0 ||
             cortex_m4_system_exception_before(cpu, irq + 16u, selected_exception))) {
            selected_exception = irq + 16;
        }
    }
    if (selected_exception == 0) {
        return false;
    }
    return enter_exception(cpu, selected_exception);
}

bool cortex_m4_exception_return(CortexM4* cpu, uint32_t value) {
    if (!cortex_m4_exception_advanced_valid_return(cpu, value)) {
        cpu->cfsr |= 1u << 18;
        cortex_m4_raise_fault(cpu, 6);
        return false;
    }
    cortex_m4_timing_begin_exception(cpu);
    const uint16_t current_exception = (uint16_t)(cpu->xpsr & 0x1ffu);
    const CortexM4ExceptionChain chain =
        cortex_m4_exception_advanced_tail_chain(cpu, value, current_exception);
    if (chain == CORTEX_M4_EXCEPTION_CHAIN_TAKEN) {
        return true;
    }
    if (chain == CORTEX_M4_EXCEPTION_CHAIN_FAULT) {
        cortex_m4_timing_abort(cpu);
        return true;
    }
    const bool use_psp = (value & 4u) != 0;
    uint32_t stack_pointer = use_psp ? cpu->psp : cpu->msp;
    if (!cortex_m4_system_unstack_exception_frame(cpu, &stack_pointer, value,
                                                  current_exception)) {
        cortex_m4_timing_abort(cpu);
        if (cpu->exception_unstack_memory_fault) {
            cortex_m4_exception_advanced_fault(
                cpu, CORTEX_M4_FAULT_UNSTACKING,
                !cortex_m4_mpu_access_permitted(cpu, stack_pointer, 4,
                                                CORTEX_M4_ACCESS_DATA, false));
        } else {
            cortex_m4_raise_fault(cpu, 6u);
        }
        return true;
    }
    if (use_psp) {
        cpu->psp = stack_pointer;
    } else {
        cpu->msp = stack_pointer;
    }
    cortex_m4_exception_advanced_commit_return(cpu, current_exception,
                                               (value & (1u << 3)) != 0u);
    const bool fp_frame = (value & 0x10u) == 0;
    cortex_m4_debug_exception_cycles(cpu, fp_frame ? 27u : 10u);
    cortex_m4_timing_exception(cpu, fp_frame ? CORTEX_M4_TIMING_EXCEPTION_FP_RETURN
                                             : CORTEX_M4_TIMING_EXCEPTION_RETURN);
    return true;
}

void cortex_m4_set_irq(CortexM4* cpu, uint16_t irq, bool pending) {
    if (cpu == NULL || irq >= cpu->external_irq_count) {
        return;
    }
    if (pending) {
        cortex_m4_system_set_pending(cpu, irq + 16u, true);
    } else {
        cortex_m4_system_set_pending(cpu, irq + 16u, false);
    }
}

void cortex_m4_set_irq_level(CortexM4* cpu, uint16_t irq, bool asserted) {
    if (cpu == NULL || irq >= cpu->external_irq_count) {
        return;
    }
    const uint32_t mask = 1u << (irq & 31u);
    if (asserted) {
        cpu->irq_level[irq / 32] |= mask;
        cortex_m4_set_irq(cpu, irq, true);
    } else {
        cpu->irq_level[irq / 32] &= ~mask;
    }
}

bool cortex_m4_get_irq_pending(const CortexM4* cpu, uint16_t irq) {
    if (cpu == NULL || irq >= cpu->external_irq_count) {
        return false;
    }
    return (cpu->irq_pending[irq / 32] & (1u << (irq & 31u))) != 0;
}

bool cortex_m4_get_irq_active(const CortexM4* cpu, uint16_t irq) {
    if (cpu == NULL || irq >= cpu->external_irq_count) {
        return false;
    }
    return (cpu->irq_active[irq / 32] & (1u << (irq & 31u))) != 0;
}
