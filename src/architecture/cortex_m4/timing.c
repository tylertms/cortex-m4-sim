#include "architecture/cortex_m4/internal.h"

enum {
    CORTEX_M4_MAXIMUM_EXCLUSIVE_GRANULE = 2048,
};

static uint32_t population_count(uint32_t value) {
    uint32_t count = 0;
    while (value != 0) {
        count += value & 1u;
        value >>= 1;
    }
    return count;
}

static uint32_t leading_zero_count(uint32_t value) {
    uint32_t count = 0;
    while ((value & 0x80000000u) == 0) {
        value <<= 1;
        count++;
    }
    return count;
}

static uint32_t signed_magnitude(uint32_t value) {
    if ((value & 0x80000000u) == 0) {
        return value;
    }
    return ~value + 1u;
}

static uint32_t saturating_add(uint32_t left, uint32_t right) {
    return UINT32_MAX - left < right ? UINT32_MAX : left + right;
}

uint32_t cortex_m4_timing_divide_cycles(uint32_t dividend, uint32_t divisor, bool signed_divide) {
    if (signed_divide) {
        dividend = signed_magnitude(dividend);
        divisor = signed_magnitude(divisor);
    }
    if (divisor == 0 || dividend < divisor) {
        return 2;
    }
    const uint32_t leading_difference = leading_zero_count(divisor) - leading_zero_count(dividend);
    const uint32_t cycles = 2u + leading_difference / 3u;
    return cycles > 12u ? 12u : cycles;
}

static bool overlaps(uint32_t left_address, uint32_t left_size, uint32_t right_address,
                     uint32_t right_size) {
    const uint64_t left_end = (uint64_t)left_address + left_size;
    const uint64_t right_end = (uint64_t)right_address + right_size;
    return left_address < right_end && right_address < left_end;
}

static void progress_pending_store(CortexM4* cpu, uint32_t cycles) {
    if (cycles >= cpu->timing_pending_store_cycles) {
        cpu->timing_pending_store_cycles = 0;
    } else {
        cpu->timing_pending_store_cycles -= cycles;
    }
}

static void advance_timed(CortexM4* cpu, uint32_t cycles) {
    progress_pending_store(cpu, cycles);
    cortex_m4_advance(cpu, cycles);
}

static CortexM4TimingBus access_bus(uint32_t address, CortexM4Access access) {
    if (address >= 0xe0000000u) {
        return CORTEX_M4_TIMING_BUS_PPB;
    }
    if (access == CORTEX_M4_ACCESS_INSTRUCTION) {
        return address < 0x20000000u ? CORTEX_M4_TIMING_BUS_ICODE : CORTEX_M4_TIMING_BUS_SYSTEM;
    }
    return address < 0x20000000u ? CORTEX_M4_TIMING_BUS_DCODE : CORTEX_M4_TIMING_BUS_SYSTEM;
}

static bool branch16(uint16_t opcode) {
    const bool conditional = (opcode & 0xf000u) == 0xd000u && ((opcode >> 8) & 15u) < 14u;
    return conditional || (opcode & 0xf800u) == 0xe000u || (opcode & 0xff00u) == 0x4700u ||
           (opcode & 0xf500u) == 0xb100u;
}

static bool branch32(uint16_t first, uint16_t second) {
    if ((first & 0xf800u) == 0xf000u) {
        const uint16_t form = second & 0xd000u;
        if (form == 0x9000u || form == 0xd000u || form == 0xc000u) {
            return true;
        }
        if (form == 0x8000u && ((first >> 6) & 15u) < 14u) {
            return true;
        }
    }
    return (first & 0xfff0u) == 0xe8d0u && (second & 0xffe0u) == 0xf000u;
}

static bool table_branch32(uint16_t first, uint16_t second) {
    return (first & 0xfff0u) == 0xe8d0u && (second & 0xffe0u) == 0xf000u;
}

static bool memory16(uint16_t opcode, bool* load) {
    if ((opcode & 0xf800u) == 0x4800u || (opcode & 0xf000u) == 0x5000u ||
        (opcode & 0xe000u) == 0x6000u || (opcode & 0xf000u) == 0x8000u ||
        (opcode & 0xf000u) == 0x9000u) {
        *load = (opcode & 0x0800u) != 0;
        if ((opcode & 0xf800u) == 0x4800u) {
            *load = true;
        }
        if ((opcode & 0xf000u) == 0x5000u) {
            const uint8_t operation = (uint8_t)((opcode >> 9) & 7u);
            *load = operation >= 3u;
        }
        return true;
    }
    return false;
}

static bool multiple16(uint16_t opcode, uint32_t* transfers, bool* loads_pc) {
    if ((opcode & 0xf000u) == 0xc000u) {
        *transfers = population_count(opcode & 0xffu);
        *loads_pc = false;
        return true;
    }
    if ((opcode & 0xfe00u) == 0xb400u) {
        *transfers = population_count(opcode & 0xffu) + ((opcode >> 8) & 1u);
        *loads_pc = false;
        return true;
    }
    if ((opcode & 0xfe00u) == 0xbc00u) {
        *transfers = population_count(opcode & 0xffu) + ((opcode >> 8) & 1u);
        *loads_pc = (opcode & 0x0100u) != 0;
        return true;
    }
    return false;
}

static bool multiple32(uint16_t first, uint16_t second, uint32_t* transfers, bool* loads_pc) {
    if ((((first & 0xffd0u) == 0xe880u || (first & 0xffd0u) == 0xe890u) ||
         (first & 0xffc0u) == 0xe900u) &&
        second != 0) {
        *transfers = population_count(second);
        *loads_pc = (first & 0x0010u) != 0 && (second & 0x8000u) != 0;
        return true;
    }
    if ((first & 0xfe40u) == 0xe840u && (first & 0x0040u) != 0 &&
        ((first & 0x0100u) != 0 || (first & 0x0020u) != 0)) {
        *transfers = 2;
        *loads_pc = false;
        return true;
    }
    return false;
}

static bool divide32(uint16_t first, uint16_t second) {
    return ((first & 0xfff0u) == 0xfbb0u || (first & 0xfff0u) == 0xfb90u) &&
           (second & 0x00f0u) == 0x00f0u;
}

static bool exclusive32(uint16_t first, uint16_t second) {
    if ((first & 0xfff0u) == 0xe850u && (second & 0x0f00u) == 0x0f00u) {
        return true;
    }
    if ((first & 0xfff0u) == 0xe840u && (second & 0x0800u) == 0) {
        return true;
    }
    return ((first & 0xfff0u) == 0xe8d0u || (first & 0xfff0u) == 0xe8c0u) &&
           (second & 0x0f00u) == 0x0f00u;
}

static bool multiply32(uint16_t first) { return (first & 0xff00u) == 0xfb00u; }

static bool memory32(uint16_t first) {
    const uint16_t operation = first & 0xfff0u;
    return operation == 0xf8c0u || operation == 0xf8d0u || operation == 0xf880u ||
           operation == 0xf890u || operation == 0xf8a0u || operation == 0xf8b0u ||
           operation == 0xf990u || operation == 0xf9b0u || (first & 0xff00u) == 0xf800u ||
           (first & 0xff00u) == 0xf900u || (first & 0xff00u) == 0xe800u;
}

static bool load32(uint16_t first) {
    const uint16_t operation = first & 0xfff0u;
    if (operation == 0xf8d0u || operation == 0xf890u || operation == 0xf8b0u ||
        operation == 0xf990u || operation == 0xf9b0u) {
        return true;
    }
    return (first & 0x0010u) != 0;
}

static uint32_t instruction_cycles(const CortexM4* cpu, uint16_t first, uint16_t second, bool wide,
                                   bool executed, uint32_t sequential_pc) {
    if (!executed) {
        return 1;
    }
    const bool taken = cpu->registers[15] != sequential_pc;
    if (wide && table_branch32(first, second)) {
        return 4;
    }
    if ((!wide && branch16(first)) || (wide && branch32(first, second))) {
        return taken ? 3u : 1u;
    }
    uint32_t transfers = 0;
    bool loads_pc = false;
    if ((!wide && multiple16(first, &transfers, &loads_pc)) ||
        (wide && multiple32(first, second, &transfers, &loads_pc))) {
        const uint32_t base = 1u + transfers;
        return loads_pc ? base + 2u : base;
    }
    if (wide && divide32(first, second)) {
        if (cpu->timing_prepared) {
            return cpu->timing_prepared_cycles;
        }
        const uint8_t dividend_register = (uint8_t)(first & 15u);
        const uint8_t divisor_register = (uint8_t)(second & 15u);
        return cortex_m4_timing_divide_cycles(
            cortex_m4_read_register_internal(cpu, dividend_register),
            cortex_m4_read_register_internal(cpu, divisor_register), (first & 0x0020u) == 0);
    }
    if (wide && exclusive32(first, second)) {
        return 2;
    }
    if ((!wide && (first & 0xffc0u) == 0x4340u) || (wide && multiply32(first))) {
        return 1;
    }
    bool load = false;
    if ((!wide && memory16(first, &load)) || (wide && memory32(first))) {
        if (wide) {
            load = load32(first);
        }
        if (wide && load && (second >> 12) == 15u) {
            return 4;
        }
        return load ? 2u : 1u;
    }
    return 1;
}

void cortex_m4_set_wait_states(CortexM4* cpu, CortexM4WaitStates wait_states, void* context) {
    if (cpu == NULL) {
        return;
    }
    cpu->wait_states = wait_states;
    cpu->wait_state_context = context;
}

bool cortex_m4_set_exclusive_granule(CortexM4* cpu, uint32_t bytes) {
    if (cpu == NULL || (bytes != 0 && (bytes < 4 || bytes > CORTEX_M4_MAXIMUM_EXCLUSIVE_GRANULE ||
                                       (bytes & (bytes - 1u)) != 0))) {
        return false;
    }
    cpu->exclusive_granule = bytes;
    cpu->exclusive_valid = false;
    return true;
}

void cortex_m4_timing_reset(CortexM4* cpu) {
    if (cpu == NULL) {
        return;
    }
    cpu->exclusive_granule = 0;
    cpu->exclusive_valid = false;
    cpu->timing_instruction_active = false;
    cpu->timing_exception_active = false;
    cpu->timing_instruction_wait_cycles = 0;
    cpu->timing_instruction_store_cycles = 0;
    cpu->timing_pending_store_cycles = 0;
    cpu->timing_last_access_valid = false;
    cpu->timing_memory_epoch = 0;
    cpu->timing_context_epoch = 0;
    cpu->timing_prepared = false;
}

void cortex_m4_timing_begin_instruction(CortexM4* cpu) {
    if (cpu == NULL) {
        return;
    }
    cpu->timing_instruction_active = true;
    cpu->timing_exception_active = false;
    cpu->timing_instruction_wait_cycles = 0;
    cpu->timing_instruction_store_cycles = 0;
    cpu->timing_prepared = false;
}

void cortex_m4_timing_begin_exception(CortexM4* cpu) {
    if (cpu == NULL) {
        return;
    }
    cortex_m4_timing_begin_instruction(cpu);
    cpu->timing_exception_active = true;
}

void cortex_m4_timing_abort(CortexM4* cpu) {
    if (cpu == NULL) {
        return;
    }
    cpu->timing_instruction_active = false;
    cpu->timing_exception_active = false;
    cpu->timing_instruction_wait_cycles = 0;
    cpu->timing_instruction_store_cycles = 0;
    cpu->timing_prepared = false;
}

void cortex_m4_timing_prepare_instruction(CortexM4* cpu, uint16_t first, uint16_t second,
                                          bool wide) {
    if (cpu == NULL || !cpu->timing_instruction_active) {
        return;
    }
    cpu->timing_prepared = false;
    if (!wide || !divide32(first, second)) {
        return;
    }
    const uint8_t dividend_register = (uint8_t)(first & 15u);
    const uint8_t divisor_register = (uint8_t)(second & 15u);
    cpu->timing_prepared_cycles = cortex_m4_timing_divide_cycles(
        cortex_m4_read_register_internal(cpu, dividend_register),
        cortex_m4_read_register_internal(cpu, divisor_register), (first & 0x0020u) == 0);
    cpu->timing_prepared = true;
}

void cortex_m4_timing_access(CortexM4* cpu, uint32_t address, uint8_t size, CortexM4Access access,
                             bool write) {
    if (cpu == NULL || !cpu->timing_instruction_active || size == 0) {
        return;
    }
    const bool sequential =
        cpu->timing_last_access_valid && cpu->timing_last_access_write == write &&
        cpu->timing_last_access_type == access && cpu->timing_last_access_address == address;
    uint32_t wait_cycles = 0;
    const CortexM4TimingBus bus = access_bus(address, access);
    if (cpu->wait_states != NULL) {
        wait_cycles =
            cpu->wait_states(cpu->wait_state_context, address, size, access, write, sequential);
    }
    if (write && (cpu->timing_exception_active || bus == CORTEX_M4_TIMING_BUS_PPB)) {
        cpu->timing_instruction_wait_cycles =
            saturating_add(cpu->timing_instruction_wait_cycles, wait_cycles);
    } else if (write) {
        if (cpu->timing_pending_store_cycles != 0) {
            cpu->timing_instruction_wait_cycles = saturating_add(
                cpu->timing_instruction_wait_cycles, cpu->timing_pending_store_cycles);
            cpu->timing_pending_store_cycles = 0;
        }
        if (cpu->timing_instruction_store_cycles != 0) {
            cpu->timing_instruction_wait_cycles = saturating_add(
                cpu->timing_instruction_wait_cycles, cpu->timing_instruction_store_cycles);
        }
        cpu->timing_instruction_store_cycles = wait_cycles;
        cpu->timing_instruction_store_bus = bus;
    } else {
        const bool conflicts_with_store =
            access != CORTEX_M4_ACCESS_INSTRUCTION || bus == cpu->timing_pending_store_bus;
        if (cpu->timing_pending_store_cycles != 0 && conflicts_with_store) {
            cpu->timing_instruction_wait_cycles = saturating_add(
                cpu->timing_instruction_wait_cycles, cpu->timing_pending_store_cycles);
            cpu->timing_pending_store_cycles = 0;
        }
        cpu->timing_instruction_wait_cycles =
            saturating_add(cpu->timing_instruction_wait_cycles, wait_cycles);
    }
    cpu->timing_last_access_address = address + size;
    cpu->timing_last_access_type = access;
    cpu->timing_last_access_write = write;
    cpu->timing_last_access_valid = true;
}

void cortex_m4_timing_barrier(CortexM4* cpu, CortexM4Barrier barrier) {
    if (cpu == NULL) {
        return;
    }
    if (barrier == CORTEX_M4_TIMING_BARRIER_DMB || barrier == CORTEX_M4_TIMING_BARRIER_DSB) {
        cpu->timing_instruction_wait_cycles =
            saturating_add(cpu->timing_instruction_wait_cycles, cpu->timing_pending_store_cycles);
        cpu->timing_pending_store_cycles = 0;
        cpu->timing_memory_epoch++;
        return;
    }
    cpu->timing_last_access_valid = false;
    cpu->timing_instruction_wait_cycles = saturating_add(cpu->timing_instruction_wait_cycles, 2);
    cpu->timing_context_epoch++;
}

void cortex_m4_timing_complete_instruction(CortexM4* cpu, uint16_t first, uint16_t second,
                                           bool wide, bool executed, uint32_t sequential_pc) {
    if (cpu == NULL) {
        return;
    }
    if (!cpu->timing_instruction_active) {
        return;
    }
    if (wide && first == 0xf3bfu && (second & 0xff0fu) == 0x8f0fu) {
        const uint8_t operation = (uint8_t)((second >> 4) & 15u);
        if (operation == 4) {
            cortex_m4_timing_barrier(cpu, CORTEX_M4_TIMING_BARRIER_DSB);
        } else if (operation == 5) {
            cortex_m4_timing_barrier(cpu, CORTEX_M4_TIMING_BARRIER_DMB);
        } else if (operation == 6) {
            cortex_m4_timing_barrier(cpu, CORTEX_M4_TIMING_BARRIER_ISB);
        }
    }
    const uint32_t base_cycles =
        instruction_cycles(cpu, first, second, wide, executed, sequential_pc);
    const uint32_t elapsed = saturating_add(base_cycles, cpu->timing_instruction_wait_cycles);
    advance_timed(cpu, elapsed);
    if (cpu->timing_instruction_store_cycles > cpu->timing_pending_store_cycles) {
        cpu->timing_pending_store_cycles = cpu->timing_instruction_store_cycles;
        cpu->timing_pending_store_bus = cpu->timing_instruction_store_bus;
    }
    cpu->timing_instruction_active = false;
    cpu->timing_exception_active = false;
    cpu->timing_instruction_wait_cycles = 0;
    cpu->timing_instruction_store_cycles = 0;
    cpu->timing_prepared = false;
}

void cortex_m4_timing_exception(CortexM4* cpu, CortexM4ExceptionTiming transition) {
    if (cpu == NULL) {
        return;
    }
    uint32_t cycles = 12;
    if (transition == CORTEX_M4_TIMING_EXCEPTION_FP_ENTRY) {
        cycles = 29;
    }
    if (transition == CORTEX_M4_TIMING_EXCEPTION_RETURN) {
        cycles = 10;
    }
    if (transition == CORTEX_M4_TIMING_EXCEPTION_FP_RETURN) {
        cycles = 27;
    }
    if (transition == CORTEX_M4_TIMING_EXCEPTION_TAIL_CHAIN) {
        cycles = 6;
    }
    cpu->timing_instruction_wait_cycles =
        saturating_add(cpu->timing_instruction_wait_cycles, cpu->timing_pending_store_cycles);
    cpu->timing_pending_store_cycles = 0;
    cpu->timing_memory_epoch++;
    cpu->exclusive_valid = false;
    cpu->timing_last_access_valid = false;
    cycles = saturating_add(cycles, cpu->timing_instruction_wait_cycles);
    advance_timed(cpu, cycles);
    cortex_m4_timing_abort(cpu);
}

void cortex_m4_timing_sleep(CortexM4* cpu, uint32_t cycles) {
    if (cpu == NULL || cycles == 0) {
        return;
    }
    advance_timed(cpu, cycles);
}

void cortex_m4_timing_reserve(CortexM4* cpu, uint32_t address, uint8_t size) {
    if (cpu == NULL || (size != 1 && size != 2 && size != 4)) {
        return;
    }
    cpu->exclusive_address = address;
    cpu->exclusive_reservation_base =
        cpu->exclusive_granule == 0 ? 0 : address & ~(cpu->exclusive_granule - 1u);
    cpu->exclusive_size = size;
    cpu->exclusive_valid = true;
}

bool cortex_m4_timing_consume_reservation(CortexM4* cpu, uint32_t address, uint8_t size) {
    if (cpu == NULL) {
        return false;
    }
    const bool address_matched = cpu->exclusive_granule == 0 || cpu->exclusive_address == address;
    const bool matched = cpu->exclusive_valid && address_matched && cpu->exclusive_size == size;
    cpu->exclusive_valid = false;
    return matched;
}

void cortex_m4_timing_observe_write(CortexM4* cpu, uint32_t address, uint32_t size) {
    if (cpu == NULL || !cpu->exclusive_valid || size == 0) {
        return;
    }
    if (cpu->exclusive_granule == 0 ||
        overlaps(cpu->exclusive_reservation_base, cpu->exclusive_granule, address, size)) {
        cpu->exclusive_valid = false;
    }
}

void cortex_m4_notify_external_write(CortexM4* cpu, uint32_t address, uint32_t size) {
    cortex_m4_timing_observe_write(cpu, address, size);
}
