#include "cortex_m4_internal.h"

static bool is_sp_or_pc(uint8_t index) { return index == 13u || index == 15u; }

static uint8_t register_count(uint16_t registers) {
    uint8_t count = 0;
    while (registers != 0) {
        count += (uint8_t)(registers & 1u);
        registers >>= 1;
    }
    return count;
}

static bool before_lowest_selected(uint16_t registers, uint8_t index) {
    return index != 0 && (registers & ((1u << index) - 1u)) != 0;
}

static bool nonfinal_it_instruction(const CortexM4* cpu) {
    return cpu->it_state != 0 && (cpu->it_state & 7u) != 0;
}

static bool invalid_it(uint16_t opcode, const CortexM4* cpu) {
    const uint8_t condition = (uint8_t)((opcode >> 4) & 15u);
    return (opcode & 15u) != 0 && (condition >= 14u || cpu->it_state != 0);
}

static bool invalid_cps(uint16_t opcode) {
    return (opcode & 8u) != 0 || (opcode & 7u) == 0 || (opcode & 4u) != 0;
}

static bool writes_pc_thumb16(uint16_t opcode) {
    if ((opcode & 0xfc00u) == 0x4400u) {
        const uint8_t operation = (uint8_t)((opcode >> 8) & 3u);
        const uint8_t destination = (uint8_t)((opcode & 7u) | ((opcode >> 4) & 8u));
        return operation == 3u || ((operation == 0u || operation == 2u) && destination == 15u);
    }
    if ((opcode & 0xfe00u) == 0xbc00u && (opcode & 0x0100u) != 0) {
        return true;
    }
    return (opcode & 0xf000u) == 0xd000u || (opcode & 0xf800u) == 0xe000u;
}

static bool invalid_thumb16_registers(uint16_t opcode) {
    if (((opcode & 0xfe00u) == 0xb400u || (opcode & 0xfe00u) == 0xbc00u) &&
        (opcode & 0x01ffu) == 0) {
        return true;
    }
    if ((opcode & 0xf000u) == 0xc000u) {
        const uint8_t base = (uint8_t)((opcode >> 8) & 7u);
        const uint8_t registers = (uint8_t)opcode;
        const bool load = (opcode & 0x0800u) != 0;
        if (registers == 0) {
            return true;
        }
        if (!load && (registers & (1u << base)) != 0 && before_lowest_selected(registers, base)) {
            return true;
        }
    }
    if ((opcode & 0xff00u) == 0x4700u) {
        return ((opcode >> 3) & 15u) == 15u;
    }
    return false;
}

static CortexM4InstructionDisposition check_thumb16(const CortexM4* cpu, uint16_t opcode) {
    if ((opcode & 0xff00u) == 0xbe00u) {
        return CORTEX_M4_INSTRUCTION_BREAKPOINT;
    }
    if ((opcode & 0xff00u) == 0xbf00u && invalid_it(opcode, cpu)) {
        return CORTEX_M4_INSTRUCTION_UNDEFINED;
    }
    if ((opcode & 0xffe0u) == 0xb660u && invalid_cps(opcode)) {
        return CORTEX_M4_INSTRUCTION_UNDEFINED;
    }
    if (invalid_thumb16_registers(opcode)) {
        return CORTEX_M4_INSTRUCTION_UNDEFINED;
    }
    if (cpu->it_state == 0) {
        return CORTEX_M4_INSTRUCTION_EXECUTE;
    }
    if (((opcode & 0xff00u) == 0xbf00u && (opcode & 15u) != 0) || (opcode & 0xf500u) == 0xb100u ||
        (opcode & 0xffe0u) == 0xb660u || (opcode & 0xff00u) == 0xdf00u) {
        return CORTEX_M4_INSTRUCTION_UNDEFINED;
    }
    if (nonfinal_it_instruction(cpu) && writes_pc_thumb16(opcode)) {
        return CORTEX_M4_INSTRUCTION_UNDEFINED;
    }
    return CORTEX_M4_INSTRUCTION_EXECUTE;
}

static bool invalid_modified_immediate(uint16_t first, uint16_t second) {
    if ((first & 0xfa00u) != 0xf000u || (second & 0x8000u) != 0) {
        return false;
    }
    const uint8_t operation = (uint8_t)((first >> 5) & 15u);
    const bool set_flags = (first & 0x10u) != 0;
    const uint8_t source = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint16_t immediate =
        (uint16_t)((((first >> 10) & 1u) << 11) | (((second >> 12) & 7u) << 8) | (second & 0xffu));
    const bool invalid_replicate =
        (immediate & 0xc00u) == 0 && (immediate & 0x300u) != 0 && (immediate & 0xffu) == 0;
    const bool comparison =
        operation == 0u || operation == 4u || operation == 8u || operation == 13u;
    const bool source_alias = operation == 2u || operation == 3u;
    return invalid_replicate || (destination == 15u && (!set_flags || !comparison)) ||
           (source == 15u && !source_alias) || destination == 13u;
}

static bool invalid_shifted_register(uint16_t first, uint16_t second) {
    if ((first & 0xfe00u) != 0xea00u || (second & 0x8000u) != 0) {
        return false;
    }
    const uint8_t operation = (uint8_t)((first >> 5) & 15u);
    const bool set_flags = (first & 0x10u) != 0;
    const uint8_t source = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t shifted = (uint8_t)(second & 15u);
    const bool comparison =
        operation == 0u || operation == 4u || operation == 8u || operation == 13u;
    const bool source_alias = operation == 2u || operation == 3u;
    return is_sp_or_pc(shifted) || destination == 13u ||
           (destination == 15u && (!set_flags || !comparison)) ||
           (is_sp_or_pc(source) && !(source == 15u && source_alias));
}

static bool invalid_plain_register_operation(uint16_t first, uint16_t second) {
    if ((first & 0xff80u) == 0xfa00u && (second & 0xf0c0u) == 0xf000u) {
        return is_sp_or_pc((uint8_t)(first & 15u)) || is_sp_or_pc((uint8_t)((second >> 8) & 15u)) ||
               is_sp_or_pc((uint8_t)(second & 15u));
    }
    if (((first & 0xfff0u) == 0xfab0u && (second & 0xf0f0u) == 0xf080u) ||
        ((first & 0xfff0u) == 0xfa90u && (second & 0xf0f0u) == 0xf0a0u)) {
        return is_sp_or_pc((uint8_t)(first & 15u)) || is_sp_or_pc((uint8_t)((second >> 8) & 15u));
    }
    if (((first & 0xfbd0u) == 0xf300u || (first & 0xfbd0u) == 0xf380u) && (second & 0x8000u) == 0) {
        return is_sp_or_pc((uint8_t)(first & 15u)) || is_sp_or_pc((uint8_t)((second >> 8) & 15u));
    }
    return false;
}

static bool invalid_bitfield(uint16_t first, uint16_t second) {
    const uint16_t operation = first & 0xfff0u;
    if ((operation != 0xf360u && operation != 0xf340u && operation != 0xf3c0u) ||
        (second & 0x8000u) != 0) {
        return false;
    }
    const uint8_t source = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t least_bit = (uint8_t)((((second >> 12) & 7u) << 2) | ((second >> 6) & 3u));
    if (is_sp_or_pc(destination) || (operation != 0xf360u && is_sp_or_pc(source)) ||
        (operation == 0xf360u && source == 13u)) {
        return true;
    }
    if (operation == 0xf360u) {
        return (second & 31u) < least_bit;
    }
    return least_bit + (second & 31u) + 1u > 32u;
}

static bool invalid_divide_or_multiply(uint16_t first, uint16_t second) {
    const uint16_t operation = first & 0xfff0u;
    if ((operation == 0xfbb0u || operation == 0xfb90u) && (second & 0xf0f0u) == 0xf0f0u) {
        return is_sp_or_pc((uint8_t)(first & 15u)) || is_sp_or_pc((uint8_t)((second >> 8) & 15u)) ||
               is_sp_or_pc((uint8_t)(second & 15u));
    }
    if (operation == 0xfb00u && (second & 0x00f0u) == 0) {
        const uint8_t accumulator = (uint8_t)(second >> 12);
        return is_sp_or_pc((uint8_t)(first & 15u)) || is_sp_or_pc((uint8_t)((second >> 8) & 15u)) ||
               is_sp_or_pc((uint8_t)(second & 15u)) ||
               (accumulator != 15u && is_sp_or_pc(accumulator));
    }
    if ((operation == 0xfba0u || operation == 0xfb80u || operation == 0xfbe0u ||
         operation == 0xfbc0u) &&
        (second & 0x00f0u) == 0) {
        const uint8_t low = (uint8_t)(second >> 12);
        const uint8_t high = (uint8_t)((second >> 8) & 15u);
        return is_sp_or_pc((uint8_t)(first & 15u)) || is_sp_or_pc(low) || is_sp_or_pc(high) ||
               is_sp_or_pc((uint8_t)(second & 15u)) || low == high;
    }
    return false;
}

static bool invalid_doubleword(uint16_t first, uint16_t second) {
    if ((first & 0xfe40u) != 0xe840u || (first & 0x0040u) == 0 ||
        ((first & 0x0100u) == 0 && (first & 0x0020u) == 0)) {
        return false;
    }
    const bool pre_index = (first & 0x0100u) != 0;
    const bool add = (first & 0x0080u) != 0;
    const bool write_back = (first & 0x0020u) != 0;
    const bool load = (first & 0x0010u) != 0;
    const uint8_t base = (uint8_t)(first & 15u);
    const uint8_t first_target = (uint8_t)(second >> 12);
    const uint8_t second_target = (uint8_t)((second >> 8) & 15u);
    const bool literal = load && base == 15u && pre_index && add && !write_back;
    return is_sp_or_pc(first_target) || is_sp_or_pc(second_target) ||
           first_target == second_target || (base == 15u && !literal) ||
           (write_back && (base == first_target || base == second_target));
}

static bool invalid_multiple(uint16_t first, uint16_t second) {
    const bool decrement_before = (first & 0xffc0u) == 0xe900u;
    const bool increment_after = (first & 0xffd0u) == 0xe880u || (first & 0xffd0u) == 0xe890u;
    if (!decrement_before && !increment_after) {
        return false;
    }
    const bool load = (first & 0x0010u) != 0;
    const bool write_back = (first & 0x0020u) != 0;
    const uint8_t base = (uint8_t)(first & 15u);
    if (base == 15u || (second & (1u << 13)) != 0 || register_count(second) < 2u ||
        (!load && (second & (1u << 15)) != 0) || (load && (second & 0xc000u) == 0xc000u)) {
        return true;
    }
    if (!write_back || (second & (1u << base)) == 0) {
        return false;
    }
    return load || before_lowest_selected(second, base);
}

static bool invalid_exclusive(uint16_t first, uint16_t second) {
    const uint16_t operation = first & 0xfff0u;
    if (operation == 0xe850u && (second & 0x0f00u) == 0x0f00u) {
        return is_sp_or_pc((uint8_t)(first & 15u)) || is_sp_or_pc((uint8_t)(second >> 12));
    }
    if (operation == 0xe840u && (second & 0x0800u) == 0) {
        const uint8_t base = (uint8_t)(first & 15u);
        const uint8_t target = (uint8_t)(second >> 12);
        const uint8_t status = (uint8_t)((second >> 8) & 15u);
        return is_sp_or_pc(base) || is_sp_or_pc(target) || is_sp_or_pc(status) || status == base ||
               status == target;
    }
    if (operation == 0xe8d0u && (second & 0x0f0fu) == 0x0f0fu) {
        return is_sp_or_pc((uint8_t)(first & 15u)) || is_sp_or_pc((uint8_t)(second >> 12));
    }
    if (operation == 0xe8c0u && (second & 0x0f00u) == 0x0f00u) {
        const uint8_t base = (uint8_t)(first & 15u);
        const uint8_t target = (uint8_t)(second >> 12);
        const uint8_t status = (uint8_t)(second & 15u);
        return is_sp_or_pc(base) || is_sp_or_pc(target) || is_sp_or_pc(status) || status == base ||
               status == target;
    }
    return false;
}

static bool memory_operation(uint16_t operation) {
    return operation == 0xf800u || operation == 0xf810u || operation == 0xf820u ||
           operation == 0xf830u || operation == 0xf840u || operation == 0xf850u ||
           operation == 0xf880u || operation == 0xf890u || operation == 0xf8a0u ||
           operation == 0xf8b0u || operation == 0xf8c0u || operation == 0xf8d0u ||
           operation == 0xf910u || operation == 0xf930u || operation == 0xf990u ||
           operation == 0xf9b0u;
}

static bool invalid_memory(uint16_t first, uint16_t second) {
    const uint16_t operation = first & 0xfff0u;
    if (!memory_operation(operation)) {
        return false;
    }
    const uint8_t base = (uint8_t)(first & 15u);
    const uint8_t target = (uint8_t)(second >> 12);
    const bool load = (operation & 0x0010u) != 0;
    const bool word = (operation & 0x0040u) != 0;
    const bool indexed_family = operation == 0xf800u || operation == 0xf810u ||
                                operation == 0xf820u || operation == 0xf830u ||
                                operation == 0xf840u || operation == 0xf850u ||
                                operation == 0xf910u || operation == 0xf930u;
    if (target == 13u || (!load && target == 15u)) {
        return true;
    }
    if (!indexed_family) {
        return base == 15u && !load;
    }
    if (base == 15u) {
        return true;
    }
    if ((second & 0x0fc0u) == 0) {
        const uint8_t index = (uint8_t)(second & 15u);
        return is_sp_or_pc(index) || (target == 15u && !word);
    }
    if ((second & 0x0f00u) == 0x0e00u) {
        return is_sp_or_pc(target);
    }
    if ((second & 0x0800u) == 0) {
        return true;
    }
    const bool pre_index = (second & 0x0400u) != 0;
    const bool write_back = (second & 0x0100u) != 0;
    return (!pre_index && !write_back) || (write_back && base == target) ||
           (target == 15u && !word);
}

static bool invalid_system_encoding(uint16_t first, uint16_t second) {
    if (first == 0xf3efu && (second & 0xf000u) == 0x8000u) {
        return is_sp_or_pc((uint8_t)((second >> 8) & 15u));
    }
    if ((first & 0xfff0u) == 0xf380u && (second & 0xff00u) == 0x8800u) {
        return is_sp_or_pc((uint8_t)(first & 15u));
    }
    if (first == 0xf3bfu && (second & 0xff0fu) == 0x8f0fu) {
        const uint8_t operation = (uint8_t)((second >> 4) & 15u);
        return operation != 2u && operation != 4u && operation != 5u && operation != 6u;
    }
    return false;
}

static bool invalid_wide_registers(uint16_t first, uint16_t second) {
    if ((first & 0xfbf0u) == 0xf240u || (first & 0xfbf0u) == 0xf2c0u) {
        return is_sp_or_pc((uint8_t)((second >> 8) & 15u));
    }
    if (((first & 0xfbf0u) == 0xf200u || (first & 0xfbf0u) == 0xf2a0u) && (second & 0x8000u) == 0) {
        return ((second >> 8) & 15u) == 15u;
    }
    if ((first & 0xfff0u) == 0xe8d0u && (second & 0xffe0u) == 0xf000u) {
        return is_sp_or_pc((uint8_t)(second & 15u));
    }
    return false;
}

static bool writes_pc_thumb32(uint16_t first, uint16_t second) {
    if ((first & 0xf800u) == 0xf000u) {
        const uint16_t branch = second & 0xd000u;
        if (branch == 0xd000u || branch == 0x9000u ||
            (branch == 0x8000u && ((first >> 6) & 15u) < 14u)) {
            return true;
        }
    }
    if ((first & 0xfff0u) == 0xe8d0u && (second & 0xffe0u) == 0xf000u) {
        return true;
    }
    if (((first & 0xffd0u) == 0xe890u ||
         ((first & 0xffc0u) == 0xe900u && (first & 0x0010u) != 0)) &&
        (second & (1u << 15)) != 0) {
        return true;
    }
    const uint16_t operation = first & 0xfff0u;
    if (memory_operation(operation) && (operation & 0x0010u) != 0 && (second >> 12) == 15u) {
        return true;
    }
    if ((first & 0xfa00u) == 0xf000u || (first & 0xfe00u) == 0xea00u) {
        const uint8_t data_operation = (uint8_t)((first >> 5) & 15u);
        const bool comparison = data_operation == 0u || data_operation == 4u ||
                                data_operation == 8u || data_operation == 13u;
        return ((second >> 8) & 15u) == 15u && ((first & 0x0010u) == 0u || !comparison);
    }
    if ((first & 0xfbf0u) == 0xf200u || (first & 0xfbf0u) == 0xf2a0u ||
        (first & 0xfbf0u) == 0xf240u || (first & 0xfbf0u) == 0xf2c0u) {
        return ((second >> 8) & 15u) == 15u;
    }
    return false;
}

static CortexM4InstructionDisposition check_thumb32(const CortexM4* cpu, uint16_t first,
                                                    uint16_t second) {
    if (invalid_modified_immediate(first, second) || invalid_shifted_register(first, second) ||
        invalid_plain_register_operation(first, second) || invalid_bitfield(first, second) ||
        invalid_divide_or_multiply(first, second) || invalid_doubleword(first, second) ||
        invalid_multiple(first, second) || invalid_exclusive(first, second) ||
        invalid_memory(first, second) || invalid_system_encoding(first, second) ||
        invalid_wide_registers(first, second)) {
        return CORTEX_M4_INSTRUCTION_UNDEFINED;
    }
    if (nonfinal_it_instruction(cpu) && writes_pc_thumb32(first, second)) {
        return CORTEX_M4_INSTRUCTION_UNDEFINED;
    }
    return CORTEX_M4_INSTRUCTION_EXECUTE;
}

CortexM4InstructionDisposition cortex_m4_check_instruction_constraints(const CortexM4* cpu,
                                                                       uint16_t first,
                                                                       uint16_t second, bool wide) {
    if (cpu == NULL) {
        return CORTEX_M4_INSTRUCTION_UNDEFINED;
    }
    return wide ? check_thumb32(cpu, first, second) : check_thumb16(cpu, first);
}
