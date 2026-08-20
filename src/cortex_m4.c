#include "cortex_m4.h"

#include <stdlib.h>
#include <string.h>

#include "cortex_m4_internal.h"

static CortexM4Result cortex_m4_result(const CortexM4* cpu) {
    CortexM4Result result;
    result.stop = cpu->stop;
    result.instructions = cpu->instructions;
    result.cycles = cpu->cycles;
    result.pc = cpu->registers[15];
    result.opcode = cpu->current_opcode;
    return result;
}

bool cortex_m4_access_is_unprivileged_data(const CortexM4* cpu, CortexM4Access access) {
    return access == CORTEX_M4_ACCESS_UNPRIVILEGED_DATA ||
           (access == CORTEX_M4_ACCESS_DATA && (cpu->xpsr & 0x1ffu) == 0u &&
            (cpu->control & CORTEX_M4_CONTROL_NPRIV) != 0u);
}

CortexM4* cortex_m4_create(CortexM4Bus bus) {
    if (bus.read == NULL || bus.write == NULL) {
        return NULL;
    }
    CortexM4* cpu = calloc(1, sizeof(*cpu));
    if (cpu == NULL) {
        return NULL;
    }
    cpu->bus = bus;
    cpu->systick_calibration = 0;
    cpu->external_irq_count = CORTEX_M4_IRQ_COUNT;
    cpu->priority_bits = 8u;
    cpu->mpu_region_count = CORTEX_M4_MPU_REGION_COUNT;
    return cpu;
}

void cortex_m4_destroy(CortexM4* cpu) { free(cpu); }

bool cortex_m4_copy(CortexM4* destination, const CortexM4* source) {
    if (destination == NULL || source == NULL ||
        destination->external_irq_count != source->external_irq_count ||
        destination->priority_bits != source->priority_bits ||
        destination->mpu_region_count != source->mpu_region_count) {
        return false;
    }
    const CortexM4Bus bus = destination->bus;
    const CortexM4Trace trace = destination->trace;
    void* const trace_context = destination->trace_context;
    const CortexM4WaitStates wait_states = destination->wait_states;
    void* const wait_state_context = destination->wait_state_context;
    const uint16_t external_irq_count = destination->external_irq_count;
    const uint8_t priority_bits = destination->priority_bits;
    const uint8_t mpu_region_count = destination->mpu_region_count;
    *destination = *source;
    destination->bus = bus;
    destination->trace = trace;
    destination->trace_context = trace_context;
    destination->wait_states = wait_states;
    destination->wait_state_context = wait_state_context;
    destination->external_irq_count = external_irq_count;
    destination->priority_bits = priority_bits;
    destination->mpu_region_count = mpu_region_count;
    return true;
}

bool cortex_m4_configure_implementation(CortexM4* cpu, uint16_t external_irq_count,
                                        uint8_t priority_bits, uint8_t mpu_region_count) {
    if (cpu == NULL || external_irq_count == 0 ||
        external_irq_count > CORTEX_M4_IRQ_COUNT || priority_bits < 3u ||
        priority_bits > 8u || mpu_region_count > CORTEX_M4_MPU_REGION_COUNT) {
        return false;
    }
    const uint8_t priority_mask = (uint8_t)(0xffu << (8u - priority_bits));
    for (uint16_t irq = 0u; irq < CORTEX_M4_IRQ_COUNT; irq++) {
        if (irq < external_irq_count) {
            cpu->irq_priority[irq] &= priority_mask;
        } else {
            cpu->irq_priority[irq] = 0u;
        }
    }
    const uint8_t word_count = (uint8_t)((external_irq_count + 31u) / 32u);
    for (uint8_t word = 0u; word < CORTEX_M4_IRQ_WORD_COUNT; word++) {
        if (word >= word_count) {
            cpu->irq_enabled[word] = 0u;
            cpu->irq_pending[word] = 0u;
            cpu->irq_active[word] = 0u;
            cpu->irq_level[word] = 0u;
        }
    }
    const uint8_t remaining = (uint8_t)(external_irq_count & 31u);
    if (remaining != 0u) {
        const uint32_t mask = (1u << remaining) - 1u;
        cpu->irq_enabled[word_count - 1u] &= mask;
        cpu->irq_pending[word_count - 1u] &= mask;
        cpu->irq_active[word_count - 1u] &= mask;
        cpu->irq_level[word_count - 1u] &= mask;
    }
    for (uint8_t region = mpu_region_count; region < CORTEX_M4_MPU_REGION_COUNT; region++) {
        cpu->mpu_region_base[region] = 0u;
        cpu->mpu_region_attributes[region] = 0u;
    }
    if (mpu_region_count == 0u) {
        cpu->mpu_control = 0u;
        cpu->mpu_region_number = 0u;
    } else if (cpu->mpu_region_number >= mpu_region_count) {
        cpu->mpu_region_number = 0u;
    }
    cpu->external_irq_count = external_irq_count;
    cpu->priority_bits = priority_bits;
    cpu->mpu_region_count = mpu_region_count;
    return true;
}

bool cortex_m4_reset(CortexM4* cpu, uint32_t vector_table_address) {
    if (cpu == NULL) {
        return false;
    }
    CortexM4Bus bus = cpu->bus;
    const CortexM4Trace trace = cpu->trace;
    void* const trace_context = cpu->trace_context;
    const CortexM4WaitStates wait_states = cpu->wait_states;
    void* const wait_state_context = cpu->wait_state_context;
    const uint32_t exclusive_granule = cpu->exclusive_granule;
    const uint16_t external_irq_count = cpu->external_irq_count;
    const uint8_t priority_bits = cpu->priority_bits;
    const uint8_t mpu_region_count = cpu->mpu_region_count;
    uint32_t breakpoints[8];
    memcpy(breakpoints, cpu->breakpoints, sizeof(breakpoints));
    const uint8_t breakpoint_enabled = cpu->breakpoint_enabled;
    memset(cpu, 0, sizeof(*cpu));
    cpu->bus = bus;
    cpu->trace = trace;
    cpu->trace_context = trace_context;
    cpu->external_irq_count = external_irq_count;
    cpu->priority_bits = priority_bits;
    cpu->mpu_region_count = mpu_region_count;
    memcpy(cpu->breakpoints, breakpoints, sizeof(breakpoints));
    cpu->breakpoint_enabled = breakpoint_enabled;
    cortex_m4_system_reset(cpu);
    cpu->wait_states = wait_states;
    cpu->wait_state_context = wait_state_context;
    cpu->exclusive_granule = exclusive_granule;
    cpu->vtor = vector_table_address & 0xffffff80u;
    cpu->xpsr = CORTEX_M4_XPSR_T;
    cpu->stop = CORTEX_M4_STOP_RUNNING;
    uint32_t stack_pointer = 0;
    uint32_t reset_vector = 0;
    if (!cortex_m4_bus_read(cpu, vector_table_address, 4, CORTEX_M4_ACCESS_DATA,
                            &stack_pointer) ||
        !cortex_m4_bus_read(cpu, vector_table_address + 4, 4, CORTEX_M4_ACCESS_DATA,
                            &reset_vector)) {
        cpu->stop = CORTEX_M4_STOP_BUS_FAULT;
        return false;
    }
    if ((reset_vector & 1u) == 0) {
        cpu->stop = CORTEX_M4_STOP_USAGE_FAULT;
        return false;
    }
    cpu->msp = stack_pointer & ~3u;
    cpu->registers[13] = cpu->msp;
    cpu->registers[14] = 0xffffffffu;
    cpu->registers[15] = reset_vector & ~1u;
    return true;
}

CortexM4Result cortex_m4_step(CortexM4* cpu) {
    if (cpu == NULL) {
        CortexM4Result result = {CORTEX_M4_STOP_LOCKUP, 0, 0, 0, 0};
        return result;
    }
    if (cpu->stop != CORTEX_M4_STOP_RUNNING) {
        return cortex_m4_result(cpu);
    }
    if (cpu->stop_requested) {
        cpu->stop = CORTEX_M4_STOP_LIMIT;
        return cortex_m4_result(cpu);
    }
    cpu->instruction_faulted = false;
    if (!cortex_m4_debug_execution_allowed(cpu)) {
        cpu->stop = CORTEX_M4_STOP_BREAKPOINT;
        return cortex_m4_result(cpu);
    }
    if (cpu->reset_requested) {
        cpu->reset_requested = false;
        if (cpu->bus.reset != NULL) {
            cpu->bus.reset(cpu->bus.context);
        }
        return cortex_m4_result(cpu);
    }
    if (cortex_m4_take_pending_exception(cpu)) {
    }
    if (cpu->sleeping) {
        cortex_m4_timing_sleep(cpu, 1);
        return cortex_m4_result(cpu);
    }
    const uint32_t previous_xpsr = cpu->xpsr;
    const uint32_t address = cpu->registers[15];
    for (uint8_t index = 0; index < 8; index++) {
        if ((cpu->breakpoint_enabled & (1u << index)) != 0 &&
            cpu->breakpoints[index] == address) {
            cpu->stop = CORTEX_M4_STOP_BREAKPOINT;
            return cortex_m4_result(cpu);
        }
    }
    cortex_m4_timing_begin_instruction(cpu);
    cortex_m4_debug_instruction_access(cpu, address);
    uint32_t first_address = address;
    cortex_m4_debug_remap_instruction(cpu, address, &first_address);
    uint32_t first_value = 0;
    if (!cortex_m4_bus_read(cpu, first_address, 2, CORTEX_M4_ACCESS_INSTRUCTION,
                            &first_value)) {
        cpu->cfsr |= 1u << 8;
        cortex_m4_raise_fault(cpu, 5);
        cortex_m4_timing_abort(cpu);
        return cortex_m4_result(cpu);
    }
    const uint16_t first = (uint16_t)first_value;
    cpu->registers[15] = address + 2;
    const bool in_it_block = cpu->it_state != 0;
    const bool execute = cortex_m4_it_condition_passed(cpu);
    bool supported = !execute;
    uint16_t second = 0;
    const bool wide = (first & 0xf800u) >= 0xe800u;
    if (wide) {
        uint32_t second_address = address + 2u;
        cortex_m4_debug_remap_instruction(cpu, second_address, &second_address);
        uint32_t second_value = 0;
        if (!cortex_m4_bus_read(cpu, second_address, 2, CORTEX_M4_ACCESS_INSTRUCTION,
                                &second_value)) {
            cpu->cfsr |= 1u << 8;
            cortex_m4_raise_fault(cpu, 5);
            cortex_m4_timing_abort(cpu);
            return cortex_m4_result(cpu);
        }
        second = (uint16_t)second_value;
        cpu->registers[15] = address + 4;
        cpu->current_opcode = ((uint32_t)first << 16) | second;
        if (cpu->trace != NULL) {
            cpu->trace(cpu->trace_context, address, cpu->current_opcode, execute);
        }
        cortex_m4_timing_prepare_instruction(cpu, first, second, true);
        const CortexM4InstructionDisposition disposition =
            cortex_m4_check_instruction_constraints(cpu, first, second, true);
        if (execute && disposition == CORTEX_M4_INSTRUCTION_EXECUTE) {
            supported = cortex_m4_execute_thumb32(cpu, first, second);
            cortex_m4_it_preserve_flags(cpu, first, second, true, in_it_block,
                                        previous_xpsr);
        }
    } else {
        cpu->current_opcode = first;
        if (cpu->trace != NULL) {
            cpu->trace(cpu->trace_context, address, cpu->current_opcode, execute);
        }
        cortex_m4_timing_prepare_instruction(cpu, first, 0, false);
        const CortexM4InstructionDisposition disposition =
            cortex_m4_check_instruction_constraints(cpu, first, 0, false);
        if (disposition == CORTEX_M4_INSTRUCTION_BREAKPOINT) {
            cortex_m4_debug_breakpoint(cpu);
            supported = true;
        } else if (execute && disposition == CORTEX_M4_INSTRUCTION_EXECUTE) {
            supported = cortex_m4_execute_thumb16(cpu, first);
            cortex_m4_it_preserve_flags(cpu, first, 0, false, in_it_block, previous_xpsr);
        }
    }
    if (in_it_block) {
        cortex_m4_it_advance(cpu);
    }
    cpu->instructions++;
    cortex_m4_debug_instruction_retired(cpu);
    cortex_m4_timing_complete_instruction(cpu, first, second, wide, execute,
                                          address + (wide ? 4u : 2u));
    if (!cortex_m4_debug_execution_allowed(cpu)) {
        cpu->stop = CORTEX_M4_STOP_BREAKPOINT;
    }
    if (cpu->reset_requested) {
        cpu->reset_requested = false;
        if (cpu->bus.reset != NULL) {
            cpu->bus.reset(cpu->bus.context);
        }
        return cortex_m4_result(cpu);
    }
    if (!supported && !cpu->instruction_faulted && cpu->stop == CORTEX_M4_STOP_RUNNING) {
        cpu->cfsr |= 1u << 16;
        cortex_m4_raise_fault(cpu, 6);
    }
    return cortex_m4_result(cpu);
}

CortexM4Result cortex_m4_run(CortexM4* cpu, CortexM4RunLimits limits) {
    if (cpu == NULL) {
        CortexM4Result result = {CORTEX_M4_STOP_LOCKUP, 0, 0, 0, 0};
        return result;
    }
    const uint64_t start_instructions = cpu->instructions;
    const uint64_t start_cycles = cpu->cycles;
    while (cpu->stop == CORTEX_M4_STOP_RUNNING) {
        if ((limits.instruction_limit != 0 &&
             cpu->instructions - start_instructions >= limits.instruction_limit) ||
            (limits.cycle_limit != 0 && cpu->cycles - start_cycles >= limits.cycle_limit)) {
            CortexM4Result result = cortex_m4_result(cpu);
            result.stop = CORTEX_M4_STOP_LIMIT;
            return result;
        }
        cortex_m4_step(cpu);
    }
    return cortex_m4_result(cpu);
}

void cortex_m4_request_stop(CortexM4* cpu) {
    if (cpu != NULL) {
        cpu->stop_requested = true;
    }
}

bool cortex_m4_set_breakpoint(CortexM4* cpu, uint8_t index, uint32_t address,
                              bool enabled) {
    if (cpu == NULL || index >= 8 || (address & 1u) != 0) {
        return false;
    }
    cpu->breakpoints[index] = address;
    if (enabled) {
        cpu->breakpoint_enabled |= (uint8_t)(1u << index);
    } else {
        cpu->breakpoint_enabled &= (uint8_t)~(1u << index);
    }
    if (cpu->stop == CORTEX_M4_STOP_BREAKPOINT) {
        cpu->stop = CORTEX_M4_STOP_RUNNING;
    }
    return true;
}

void cortex_m4_set_trace(CortexM4* cpu, CortexM4Trace trace, void* context) {
    if (cpu != NULL) {
        cpu->trace = trace;
        cpu->trace_context = context;
    }
}

uint32_t cortex_m4_read_register_internal(const CortexM4* cpu, uint8_t index) {
    if (index == 13) {
        const bool handler = (cpu->xpsr & 0x1ffu) != 0;
        if (!handler && (cpu->control & CORTEX_M4_CONTROL_SPSEL) != 0) {
            return cpu->psp;
        }
        return cpu->msp;
    }
    return cpu->registers[index & 15u];
}

void cortex_m4_write_register_internal(CortexM4* cpu, uint8_t index, uint32_t value) {
    index &= 15u;
    if (index == 13) {
        value &= ~3u;
        const bool handler = (cpu->xpsr & 0x1ffu) != 0;
        if (!handler && (cpu->control & CORTEX_M4_CONTROL_SPSEL) != 0) {
            cpu->psp = value;
        } else {
            cpu->msp = value;
        }
        cpu->registers[13] = value;
        return;
    }
    if (index == 15) {
        value &= ~1u;
    }
    cpu->registers[index] = value;
}

uint32_t cortex_m4_get_register(const CortexM4* cpu, uint8_t index) {
    if (cpu == NULL || index >= CORTEX_M4_REGISTER_COUNT) {
        return 0;
    }
    return cortex_m4_read_register_internal(cpu, index);
}

void cortex_m4_set_register(CortexM4* cpu, uint8_t index, uint32_t value) {
    if (cpu != NULL && index < CORTEX_M4_REGISTER_COUNT) {
        cortex_m4_write_register_internal(cpu, index, value);
    }
}

uint32_t cortex_m4_xpsr_value(const CortexM4* cpu) {
    const uint32_t it_state =
        ((uint32_t)(cpu->it_state & 3u) << 25) | ((uint32_t)(cpu->it_state & 0xfcu) << 8);
    return cortex_m4_exception_advanced_xpsr(cpu, cpu->xpsr | it_state);
}

void cortex_m4_load_xpsr(CortexM4* cpu, uint32_t value) {
    cpu->it_state = (uint8_t)(((value >> 25) & 3u) | ((value >> 8) & 0xfcu));
    cpu->xpsr = (value & ~0x0600fc00u) | CORTEX_M4_XPSR_T;
    cortex_m4_exception_advanced_load_xpsr(cpu, value);
}

uint32_t cortex_m4_get_xpsr(const CortexM4* cpu) {
    return cpu == NULL ? 0 : cortex_m4_xpsr_value(cpu);
}

void cortex_m4_set_xpsr(CortexM4* cpu, uint32_t value) {
    if (cpu != NULL) {
        cortex_m4_load_xpsr(cpu, value);
    }
}

uint32_t cortex_m4_get_control(const CortexM4* cpu) {
    return cpu == NULL ? 0 : cpu->control;
}

void cortex_m4_set_control(CortexM4* cpu, uint32_t value) {
    if (cpu != NULL && (cpu->xpsr & 0x1ffu) == 0) {
        cpu->control = value & 7u;
    }
}

uint32_t cortex_m4_get_fault_status(const CortexM4* cpu) {
    return cpu == NULL ? 0 : cpu->cfsr;
}

uint32_t cortex_m4_get_fault_address(const CortexM4* cpu) {
    return cpu == NULL ? 0 : cpu->bfar;
}

uint64_t cortex_m4_get_instruction_count(const CortexM4* cpu) {
    return cpu == NULL ? 0 : cpu->instructions;
}

uint64_t cortex_m4_get_cycle_count(const CortexM4* cpu) {
    return cpu == NULL ? 0 : cpu->cycles;
}

CortexM4Stop cortex_m4_get_stop(const CortexM4* cpu) {
    return cpu == NULL ? CORTEX_M4_STOP_LOCKUP : cpu->stop;
}

uint32_t cortex_m4_get_fp_register(const CortexM4* cpu, uint8_t index) {
    if (cpu == NULL || index >= CORTEX_M4_FP_REGISTER_COUNT) {
        return 0;
    }
    return cpu->fp_registers[index];
}

void cortex_m4_set_fp_register(CortexM4* cpu, uint8_t index, uint32_t value) {
    if (cpu != NULL && index < CORTEX_M4_FP_REGISTER_COUNT) {
        cpu->fp_registers[index] = value;
    }
}

uint32_t cortex_m4_get_fpscr(const CortexM4* cpu) { return cpu == NULL ? 0 : cpu->fpscr; }

void cortex_m4_set_fpscr(CortexM4* cpu, uint32_t value) {
    if (cpu != NULL) {
        cpu->fpscr = value;
    }
}

bool cortex_m4_read_memory(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t* value) {
    return cpu != NULL && value != NULL &&
           cortex_m4_bus_read(cpu, address, size, CORTEX_M4_ACCESS_DEBUG, value);
}

bool cortex_m4_write_memory(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t value) {
    return cpu != NULL &&
           cortex_m4_bus_write(cpu, address, size, CORTEX_M4_ACCESS_DEBUG, value);
}

void cortex_m4_set_nz(CortexM4* cpu, uint32_t value) {
    cpu->xpsr &= ~(CORTEX_M4_XPSR_N | CORTEX_M4_XPSR_Z);
    if ((value & 0x80000000u) != 0) {
        cpu->xpsr |= CORTEX_M4_XPSR_N;
    }
    if (value == 0) {
        cpu->xpsr |= CORTEX_M4_XPSR_Z;
    }
}

void cortex_m4_set_nzcv(CortexM4* cpu, uint32_t value, bool carry, bool overflow) {
    cortex_m4_set_nz(cpu, value);
    cpu->xpsr &= ~(CORTEX_M4_XPSR_C | CORTEX_M4_XPSR_V);
    if (carry) {
        cpu->xpsr |= CORTEX_M4_XPSR_C;
    }
    if (overflow) {
        cpu->xpsr |= CORTEX_M4_XPSR_V;
    }
}

uint32_t cortex_m4_add_with_carry(uint32_t left, uint32_t right, bool carry,
                                  bool* carry_out, bool* overflow_out) {
    const uint64_t unsigned_sum = (uint64_t)left + right + (carry ? 1u : 0u);
    const uint32_t result = (uint32_t)unsigned_sum;
    if (carry_out != NULL) {
        *carry_out = (unsigned_sum >> 32) != 0;
    }
    if (overflow_out != NULL) {
        *overflow_out = ((~(left ^ right) & (left ^ result)) >> 31) != 0;
    }
    return result;
}

bool cortex_m4_condition_passed(const CortexM4* cpu, uint8_t condition) {
    const bool n = (cpu->xpsr & CORTEX_M4_XPSR_N) != 0;
    const bool z = (cpu->xpsr & CORTEX_M4_XPSR_Z) != 0;
    const bool c = (cpu->xpsr & CORTEX_M4_XPSR_C) != 0;
    const bool v = (cpu->xpsr & CORTEX_M4_XPSR_V) != 0;
    switch (condition & 15u) {
    case 0:
        return z;
    case 1:
        return !z;
    case 2:
        return c;
    case 3:
        return !c;
    case 4:
        return n;
    case 5:
        return !n;
    case 6:
        return v;
    case 7:
        return !v;
    case 8:
        return c && !z;
    case 9:
        return !c || z;
    case 10:
        return n == v;
    case 11:
        return n != v;
    case 12:
        return !z && n == v;
    case 13:
        return z || n != v;
    case 14:
        return true;
    default:
        return false;
    }
}

uint32_t cortex_m4_shift(uint32_t value, uint8_t type, uint32_t amount, bool carry_in,
                         bool* carry_out) {
    bool carry = carry_in;
    uint32_t result = value;
    switch (type & 3u) {
    case 0:
        if (amount != 0) {
            carry = amount <= 32 ? ((value >> (32 - amount)) & 1u) != 0 : false;
            result = amount < 32 ? value << amount : 0;
        }
        break;
    case 1:
        if (amount == 0) {
            amount = 32;
        }
        carry = amount <= 32 ? ((value >> (amount - 1)) & 1u) != 0 : false;
        result = amount < 32 ? value >> amount : 0;
        break;
    case 2:
        if (amount == 0) {
            amount = 32;
        }
        carry =
            amount <= 32 ? ((value >> (amount - 1)) & 1u) != 0 : (value & 0x80000000u) != 0;
        if (amount >= 32) {
            result = (value & 0x80000000u) != 0 ? 0xffffffffu : 0;
        } else {
            result = value >> amount;
            if ((value & 0x80000000u) != 0) {
                result |= UINT32_MAX << (32u - amount);
            }
        }
        break;
    default:
        amount &= 31u;
        if (amount == 0) {
            carry = (value & 1u) != 0;
            result = (carry_in ? 0x80000000u : 0) | (value >> 1);
        } else {
            result = (value >> amount) | (value << (32 - amount));
            carry = (result & 0x80000000u) != 0;
        }
        break;
    }
    if (carry_out != NULL) {
        *carry_out = carry;
    }
    return result;
}

uint32_t cortex_m4_shift_register(uint32_t value, uint8_t type, uint32_t amount,
                                  bool carry_in, bool* carry_out) {
    if ((amount & 0xffu) == 0u) {
        if (carry_out != NULL) {
            *carry_out = carry_in;
        }
        return value;
    }
    return cortex_m4_shift(value, type, amount & 0xffu, carry_in, carry_out);
}
