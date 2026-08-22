#include "architecture/cortex_m4/internal.h"

#include <limits.h>

static uint32_t visible_pc16(const CortexM4* cpu) { return cpu->registers[15] + 2u; }

uint32_t cortex_m4_internal_visible_pc32(const CortexM4* cpu) { return cpu->registers[15]; }

int32_t cortex_m4_internal_sign_extend(uint32_t value, uint8_t width) {
    const uint32_t sign = 1u << (width - 1u);
    return (int32_t)((value ^ sign) - sign);
}

bool cortex_m4_internal_read_data(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t* value) {
    return cortex_m4_data_read(cpu, address, size, CORTEX_M4_ACCESS_DATA, value);
}

bool cortex_m4_internal_write_data(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t value) {
    return cortex_m4_data_write(cpu, address, size, CORTEX_M4_ACCESS_DATA, value);
}

void cortex_m4_internal_write_pc(CortexM4* cpu, uint32_t value) {
    if ((value & 1u) == 0) {
        cpu->cfsr |= 1u << 17;
        cortex_m4_raise_fault(cpu, 6);
        return;
    }
    cpu->registers[15] = value & ~1u;
}

void cortex_m4_internal_load_write_pc(CortexM4* cpu, uint32_t value) {
    if ((value & 0xfffffff0u) == 0xfffffff0u) {
        cortex_m4_exception_return(cpu, value);
        return;
    }
    cortex_m4_internal_write_pc(cpu, value);
}

void cortex_m4_internal_set_logic_flags(CortexM4* cpu, uint32_t value, bool carry) {
    cortex_m4_set_nz(cpu, value);
    cpu->xpsr &= ~CORTEX_M4_XPSR_C;
    if (carry) {
        cpu->xpsr |= CORTEX_M4_XPSR_C;
    }
}

static uint32_t add_flags(CortexM4* cpu, uint32_t left, uint32_t right, bool carry) {
    bool carry_out = false;
    bool overflow = false;
    const uint32_t result = cortex_m4_add_with_carry(left, right, carry, &carry_out, &overflow);
    cortex_m4_set_nzcv(cpu, result, carry_out, overflow);
    return result;
}

static bool execute_shift_immediate(CortexM4* cpu, uint16_t opcode) {
    const uint8_t type = (uint8_t)((opcode >> 11) & 3u);
    if (type == 3) {
        return false;
    }
    const uint8_t amount = (uint8_t)((opcode >> 6) & 31u);
    const uint8_t source = (uint8_t)((opcode >> 3) & 7u);
    const uint8_t destination = (uint8_t)(opcode & 7u);
    bool carry = (cpu->xpsr & CORTEX_M4_XPSR_C) != 0;
    const uint32_t result = cortex_m4_shift(cpu->registers[source], type, amount, carry, &carry);
    cpu->registers[destination] = result;
    cortex_m4_internal_set_logic_flags(cpu, result, carry);
    return true;
}

static bool execute_add_subtract(CortexM4* cpu, uint16_t opcode) {
    const bool immediate = (opcode & (1u << 10)) != 0;
    const bool subtract = (opcode & (1u << 9)) != 0;
    const uint8_t operand = (uint8_t)((opcode >> 6) & 7u);
    const uint8_t source = (uint8_t)((opcode >> 3) & 7u);
    const uint8_t destination = (uint8_t)(opcode & 7u);
    const uint32_t right = immediate ? operand : cpu->registers[operand];
    cpu->registers[destination] = subtract ? add_flags(cpu, cpu->registers[source], ~right, true)
                                           : add_flags(cpu, cpu->registers[source], right, false);
    return true;
}

static bool execute_immediate(CortexM4* cpu, uint16_t opcode) {
    const uint8_t operation = (uint8_t)((opcode >> 11) & 3u);
    const uint8_t destination = (uint8_t)((opcode >> 8) & 7u);
    const uint32_t immediate = opcode & 0xffu;
    if (operation == 0) {
        cpu->registers[destination] = immediate;
        cortex_m4_set_nz(cpu, immediate);
    } else if (operation == 1) {
        add_flags(cpu, cpu->registers[destination], ~immediate, true);
    } else if (operation == 2) {
        cpu->registers[destination] = add_flags(cpu, cpu->registers[destination], immediate, false);
    } else {
        cpu->registers[destination] = add_flags(cpu, cpu->registers[destination], ~immediate, true);
    }
    return true;
}

static bool execute_data_processing(CortexM4* cpu, uint16_t opcode) {
    const uint8_t operation = (uint8_t)((opcode >> 6) & 15u);
    const uint8_t source = (uint8_t)((opcode >> 3) & 7u);
    const uint8_t destination = (uint8_t)(opcode & 7u);
    const uint32_t left = cpu->registers[destination];
    const uint32_t right = cpu->registers[source];
    uint32_t result = left;
    bool carry = (cpu->xpsr & CORTEX_M4_XPSR_C) != 0;
    switch (operation) {
    case 0:
        result = left & right;
        cpu->registers[destination] = result;
        cortex_m4_set_nz(cpu, result);
        break;
    case 1:
        result = left ^ right;
        cpu->registers[destination] = result;
        cortex_m4_set_nz(cpu, result);
        break;
    case 2:
        result = cortex_m4_shift_register(left, 0, right, carry, &carry);
        cpu->registers[destination] = result;
        cortex_m4_internal_set_logic_flags(cpu, result, carry);
        break;
    case 3:
        result = cortex_m4_shift_register(left, 1, right, carry, &carry);
        cpu->registers[destination] = result;
        cortex_m4_internal_set_logic_flags(cpu, result, carry);
        break;
    case 4:
        result = cortex_m4_shift_register(left, 2, right, carry, &carry);
        cpu->registers[destination] = result;
        cortex_m4_internal_set_logic_flags(cpu, result, carry);
        break;
    case 5:
        cpu->registers[destination] = add_flags(cpu, left, right, carry);
        break;
    case 6:
        cpu->registers[destination] = add_flags(cpu, left, ~right, carry);
        break;
    case 7:
        result = cortex_m4_shift_register(left, 3, right, carry, &carry);
        cpu->registers[destination] = result;
        cortex_m4_internal_set_logic_flags(cpu, result, carry);
        break;
    case 8:
        cortex_m4_set_nz(cpu, left & right);
        break;
    case 9:
        cpu->registers[destination] = add_flags(cpu, 0, ~right, true);
        break;
    case 10:
        add_flags(cpu, left, ~right, true);
        break;
    case 11:
        add_flags(cpu, left, right, false);
        break;
    case 12:
        result = left | right;
        cpu->registers[destination] = result;
        cortex_m4_set_nz(cpu, result);
        break;
    case 13:
        result = left * right;
        cpu->registers[destination] = result;
        cortex_m4_set_nz(cpu, result);
        break;
    case 14:
        result = left & ~right;
        cpu->registers[destination] = result;
        cortex_m4_set_nz(cpu, result);
        break;
    default:
        result = ~right;
        cpu->registers[destination] = result;
        cortex_m4_set_nz(cpu, result);
        break;
    }
    return true;
}

static bool execute_high_register(CortexM4* cpu, uint16_t opcode) {
    const uint8_t operation = (uint8_t)((opcode >> 8) & 3u);
    const uint8_t source = (uint8_t)(((opcode >> 3) & 15u));
    const uint8_t destination = (uint8_t)((opcode & 7u) | ((opcode >> 4) & 8u));
    const uint32_t source_value =
        source == 15 ? visible_pc16(cpu) : cortex_m4_read_register_internal(cpu, source);
    const uint32_t destination_value =
        destination == 15 ? visible_pc16(cpu) : cortex_m4_read_register_internal(cpu, destination);
    if (operation == 0) {
        const uint32_t result = destination_value + source_value;
        if (destination == 15) {
            cortex_m4_internal_write_pc(cpu, result);
        } else {
            cortex_m4_write_register_internal(cpu, destination, result);
        }
    } else if (operation == 1) {
        add_flags(cpu, destination_value, ~source_value, true);
    } else if (operation == 2) {
        if (destination == 15) {
            cortex_m4_internal_write_pc(cpu, source_value);
        } else {
            cortex_m4_write_register_internal(cpu, destination, source_value);
        }
    } else {
        if ((source_value & 0xfffffff0u) == 0xfffffff0u &&
            cortex_m4_exception_return(cpu, source_value)) {
            return true;
        }
        if ((opcode & 0x0080u) != 0) {
            cpu->registers[14] = cpu->registers[15] | 1u;
        }
        cortex_m4_internal_write_pc(cpu, source_value);
    }
    return true;
}

static bool execute_register_offset_memory(CortexM4* cpu, uint16_t opcode) {
    const uint8_t operation = (uint8_t)((opcode >> 9) & 7u);
    const uint8_t offset_register = (uint8_t)((opcode >> 6) & 7u);
    const uint8_t base_register = (uint8_t)((opcode >> 3) & 7u);
    const uint8_t target_register = (uint8_t)(opcode & 7u);
    const uint32_t address = cpu->registers[base_register] + cpu->registers[offset_register];
    uint32_t value = 0;
    switch (operation) {
    case 0:
        return cortex_m4_internal_write_data(cpu, address, 4, cpu->registers[target_register]);
    case 1:
        return cortex_m4_internal_write_data(cpu, address, 2, cpu->registers[target_register]);
    case 2:
        return cortex_m4_internal_write_data(cpu, address, 1, cpu->registers[target_register]);
    case 3:
        if (!cortex_m4_internal_read_data(cpu, address, 1, &value))
            return true;
        cpu->registers[target_register] = (uint32_t)cortex_m4_internal_sign_extend(value, 8);
        return true;
    case 4:
        if (!cortex_m4_internal_read_data(cpu, address, 4, &value))
            return true;
        cpu->registers[target_register] = value;
        return true;
    case 5:
        if (!cortex_m4_internal_read_data(cpu, address, 2, &value))
            return true;
        cpu->registers[target_register] = value;
        return true;
    case 6:
        if (!cortex_m4_internal_read_data(cpu, address, 1, &value))
            return true;
        cpu->registers[target_register] = value;
        return true;
    default:
        if (!cortex_m4_internal_read_data(cpu, address, 2, &value))
            return true;
        cpu->registers[target_register] = (uint32_t)cortex_m4_internal_sign_extend(value, 16);
        return true;
    }
}

static bool execute_immediate_memory(CortexM4* cpu, uint16_t opcode) {
    const uint8_t operation = (uint8_t)((opcode >> 11) & 3u);
    const uint8_t immediate = (uint8_t)((opcode >> 6) & 31u);
    const uint8_t base_register = (uint8_t)((opcode >> 3) & 7u);
    const uint8_t target_register = (uint8_t)(opcode & 7u);
    const uint8_t size = operation < 2 ? 4 : 1;
    const uint32_t address =
        cpu->registers[base_register] + (size == 4 ? immediate * 4u : immediate);
    if ((operation & 1u) == 0) {
        return cortex_m4_internal_write_data(cpu, address, size, cpu->registers[target_register]);
    }
    uint32_t value = 0;
    if (cortex_m4_internal_read_data(cpu, address, size, &value)) {
        cpu->registers[target_register] = value;
    }
    return true;
}

static bool execute_halfword_memory(CortexM4* cpu, uint16_t opcode) {
    const bool load = (opcode & (1u << 11)) != 0;
    const uint8_t immediate = (uint8_t)((opcode >> 6) & 31u);
    const uint8_t base_register = (uint8_t)((opcode >> 3) & 7u);
    const uint8_t target_register = (uint8_t)(opcode & 7u);
    const uint32_t address = cpu->registers[base_register] + immediate * 2u;
    if (!load) {
        return cortex_m4_internal_write_data(cpu, address, 2, cpu->registers[target_register]);
    }
    uint32_t value = 0;
    if (cortex_m4_internal_read_data(cpu, address, 2, &value)) {
        cpu->registers[target_register] = value;
    }
    return true;
}

static bool execute_stack_memory(CortexM4* cpu, uint16_t opcode) {
    const bool load = (opcode & (1u << 11)) != 0;
    const uint8_t target_register = (uint8_t)((opcode >> 8) & 7u);
    const uint32_t address = cortex_m4_read_register_internal(cpu, 13) + (opcode & 0xffu) * 4u;
    if (!load) {
        return cortex_m4_internal_write_data(cpu, address, 4, cpu->registers[target_register]);
    }
    uint32_t value = 0;
    if (cortex_m4_internal_read_data(cpu, address, 4, &value)) {
        cpu->registers[target_register] = value;
    }
    return true;
}

static bool execute_push_pop(CortexM4* cpu, uint16_t opcode) {
    const bool pop = (opcode & (1u << 11)) != 0;
    const bool extra = (opcode & (1u << 8)) != 0;
    const uint8_t list = (uint8_t)opcode;
    uint32_t stack_pointer = cortex_m4_read_register_internal(cpu, 13);
    const uint8_t resume = cortex_m4_exception_advanced_multiple_resume(cpu);
    if (!pop) {
        uint32_t count = extra ? 1u : 0u;
        for (uint8_t index = 0; index < 8; index++) {
            if ((list & (1u << index)) != 0)
                count++;
        }
        uint32_t address = stack_pointer - count * 4u;
        const uint32_t new_stack = address;
        address = cortex_m4_exception_advanced_multiple_address(cpu, address);
        for (uint8_t index = 0; index < 8; index++) {
            if (index < resume)
                continue;
            if ((list & (1u << index)) != 0) {
                if (!cortex_m4_internal_write_data(cpu, address, 4, cpu->registers[index]))
                    return true;
                address += 4;
                uint8_t next = 0;
                for (uint8_t candidate = index + 1u; candidate < 8u; candidate++) {
                    if ((list & (1u << candidate)) != 0) {
                        next = candidate;
                        break;
                    }
                }
                if (next == 0u && extra) {
                    next = 14u;
                }
                if (cortex_m4_exception_advanced_multiple_suspend(cpu, next, 2u, address))
                    return true;
            }
        }
        if (extra && resume <= 14u) {
            if (!cortex_m4_internal_write_data(cpu, address, 4, cpu->registers[14]))
                return true;
        }
        cortex_m4_write_register_internal(cpu, 13, new_stack);
        cortex_m4_exception_advanced_multiple_complete(cpu);
        return true;
    }
    stack_pointer = cortex_m4_exception_advanced_multiple_address(cpu, stack_pointer);
    for (uint8_t index = 0; index < 8; index++) {
        if (index < resume)
            continue;
        if ((list & (1u << index)) != 0) {
            uint32_t value = 0;
            if (!cortex_m4_internal_read_data(cpu, stack_pointer, 4, &value))
                return true;
            cpu->registers[index] = value;
            stack_pointer += 4;
            uint8_t next = 0;
            for (uint8_t candidate = index + 1u; candidate < 8u; candidate++) {
                if ((list & (1u << candidate)) != 0) {
                    next = candidate;
                    break;
                }
            }
            if (next == 0u && extra) {
                next = 15u;
            }
            if (cortex_m4_exception_advanced_multiple_suspend(cpu, next, 2u, stack_pointer))
                return true;
        }
    }
    if (extra && resume <= 15u) {
        uint32_t value = 0;
        if (!cortex_m4_internal_read_data(cpu, stack_pointer, 4, &value))
            return true;
        stack_pointer += 4;
        cortex_m4_write_register_internal(cpu, 13, stack_pointer);
        cortex_m4_internal_load_write_pc(cpu, value);
    } else {
        cortex_m4_write_register_internal(cpu, 13, stack_pointer);
    }
    cortex_m4_exception_advanced_multiple_complete(cpu);
    return true;
}

static bool execute_multiple(CortexM4* cpu, uint16_t opcode) {
    const bool load = (opcode & (1u << 11)) != 0;
    const uint8_t base = (uint8_t)((opcode >> 8) & 7u);
    const uint8_t list = (uint8_t)opcode;
    uint32_t address = cortex_m4_exception_advanced_multiple_address(cpu, cpu->registers[base]);
    const uint8_t resume = cortex_m4_exception_advanced_multiple_resume(cpu);
    for (uint8_t index = 0; index < 8; index++) {
        if (index < resume)
            continue;
        if ((list & (1u << index)) == 0)
            continue;
        if (load) {
            uint32_t value = 0;
            if (!cortex_m4_internal_read_data(cpu, address, 4, &value))
                return true;
            cpu->registers[index] = value;
        } else if (!cortex_m4_internal_write_data(cpu, address, 4, cpu->registers[index])) {
            return true;
        }
        address += 4;
        uint8_t next = 0;
        for (uint8_t candidate = index + 1u; candidate < 8u; candidate++) {
            if ((list & (1u << candidate)) != 0) {
                next = candidate;
                break;
            }
        }
        if (cortex_m4_exception_advanced_multiple_suspend(cpu, next, 2u, address))
            return true;
    }
    if (!load || (list & (1u << base)) == 0) {
        cpu->registers[base] = address;
    }
    cortex_m4_exception_advanced_multiple_complete(cpu);
    return true;
}

static bool execute_miscellaneous(CortexM4* cpu, uint16_t opcode) {
    if ((opcode & 0xff00u) == 0xb000u) {
        const uint32_t amount = (opcode & 0x7fu) * 4u;
        const uint32_t stack = cortex_m4_read_register_internal(cpu, 13);
        cortex_m4_write_register_internal(cpu, 13,
                                          (opcode & 0x80u) != 0 ? stack - amount : stack + amount);
        return true;
    }
    if ((opcode & 0xf500u) == 0xb100u) {
        const bool nonzero = (opcode & (1u << 11)) != 0;
        const uint8_t source = (uint8_t)(opcode & 7u);
        const uint32_t immediate = ((opcode >> 3) & 0x1fu) * 2u + ((opcode >> 9) & 1u) * 64u;
        if ((cpu->registers[source] != 0) == nonzero) {
            cpu->registers[15] = visible_pc16(cpu) + immediate;
        }
        return true;
    }
    if ((opcode & 0xfe00u) == 0xb400u || (opcode & 0xfe00u) == 0xbc00u) {
        return execute_push_pop(cpu, opcode);
    }
    if ((opcode & 0xff00u) == 0xb200u) {
        const uint8_t operation = (uint8_t)((opcode >> 6) & 3u);
        const uint8_t source = (uint8_t)((opcode >> 3) & 7u);
        const uint8_t destination = (uint8_t)(opcode & 7u);
        const uint32_t value = cpu->registers[source];
        if (operation == 0)
            cpu->registers[destination] = (uint32_t)cortex_m4_internal_sign_extend(value, 16);
        if (operation == 1)
            cpu->registers[destination] = (uint32_t)cortex_m4_internal_sign_extend(value, 8);
        if (operation == 2)
            cpu->registers[destination] = value & 0xffffu;
        if (operation == 3)
            cpu->registers[destination] = value & 0xffu;
        return true;
    }
    if ((opcode & 0xff00u) == 0xba00u) {
        const uint8_t operation = (uint8_t)((opcode >> 6) & 3u);
        const uint8_t source = (uint8_t)((opcode >> 3) & 7u);
        const uint8_t destination = (uint8_t)(opcode & 7u);
        const uint32_t value = cpu->registers[source];
        if (operation == 0) {
            cpu->registers[destination] =
                ((value & 0x000000ffu) << 24) | ((value & 0x0000ff00u) << 8) |
                ((value & 0x00ff0000u) >> 8) | ((value & 0xff000000u) >> 24);
        } else if (operation == 1) {
            cpu->registers[destination] =
                ((value & 0x00ff00ffu) << 8) | ((value & 0xff00ff00u) >> 8);
        } else if (operation == 3) {
            const uint32_t half = ((value & 0xffu) << 8) | ((value >> 8) & 0xffu);
            cpu->registers[destination] = (uint32_t)cortex_m4_internal_sign_extend(half, 16);
        } else {
            return false;
        }
        return true;
    }
    if ((opcode & 0xffe0u) == 0xb660u) {
        const bool privileged =
            (cpu->xpsr & 0x1ffu) != 0 || (cpu->control & CORTEX_M4_CONTROL_NPRIV) == 0;
        if (!privileged) {
            return true;
        }
        const uint32_t value = (opcode & 0x10u) != 0 ? 1u : 0u;
        if ((opcode & 2u) != 0) {
            cpu->primask = value;
        }
        if ((opcode & 1u) != 0) {
            cpu->faultmask = value;
        }
        return true;
    }
    return false;
}
bool cortex_m4_execute_thumb16(CortexM4* cpu, uint16_t opcode) {
    if ((opcode & 0xff00u) == 0xbf00u) {
        const uint8_t immediate = (uint8_t)opcode;
        if ((immediate & 15u) != 0) {
            cpu->it_state = immediate;
        } else if (immediate == 0x20u) {
            if (!cpu->event_register)
                cpu->sleeping = true;
            cpu->event_register = false;
        } else if (immediate == 0x30u) {
            cortex_m4_system_wait_for_interrupt(cpu);
        } else if (immediate == 0x40u) {
            cpu->event_register = true;
        }
        return true;
    }
    if ((opcode & 0xe000u) == 0x0000u) {
        if ((opcode & 0x1800u) == 0x1800u)
            return execute_add_subtract(cpu, opcode);
        return execute_shift_immediate(cpu, opcode);
    }
    if ((opcode & 0xe000u) == 0x2000u)
        return execute_immediate(cpu, opcode);
    if ((opcode & 0xfc00u) == 0x4000u)
        return execute_data_processing(cpu, opcode);
    if ((opcode & 0xfc00u) == 0x4400u)
        return execute_high_register(cpu, opcode);
    if ((opcode & 0xf800u) == 0x4800u) {
        const uint8_t target = (uint8_t)((opcode >> 8) & 7u);
        const uint32_t address = (visible_pc16(cpu) & ~3u) + (opcode & 0xffu) * 4u;
        uint32_t value = 0;
        if (cortex_m4_internal_read_data(cpu, address, 4, &value))
            cpu->registers[target] = value;
        return true;
    }
    if ((opcode & 0xf000u) == 0x5000u)
        return execute_register_offset_memory(cpu, opcode);
    if ((opcode & 0xe000u) == 0x6000u)
        return execute_immediate_memory(cpu, opcode);
    if ((opcode & 0xf000u) == 0x8000u)
        return execute_halfword_memory(cpu, opcode);
    if ((opcode & 0xf000u) == 0x9000u)
        return execute_stack_memory(cpu, opcode);
    if ((opcode & 0xf000u) == 0xa000u) {
        const uint8_t target = (uint8_t)((opcode >> 8) & 7u);
        const uint32_t base = (opcode & (1u << 11)) != 0 ? cortex_m4_read_register_internal(cpu, 13)
                                                         : visible_pc16(cpu) & ~3u;
        cpu->registers[target] = base + (opcode & 0xffu) * 4u;
        return true;
    }
    if ((opcode & 0xf000u) == 0xb000u)
        return execute_miscellaneous(cpu, opcode);
    if ((opcode & 0xf000u) == 0xc000u)
        return execute_multiple(cpu, opcode);
    if ((opcode & 0xf000u) == 0xd000u) {
        const uint8_t condition = (uint8_t)((opcode >> 8) & 15u);
        if (condition == 15) {
            cpu->system_pending |= 1u << 11;
            return true;
        }
        if (condition == 14)
            return false;
        if (cortex_m4_condition_passed(cpu, condition)) {
            cpu->registers[15] = visible_pc16(cpu) +
                                 (uint32_t)(cortex_m4_internal_sign_extend(opcode & 0xffu, 8) * 2);
        }
        return true;
    }
    if ((opcode & 0xf800u) == 0xe000u) {
        cpu->registers[15] =
            visible_pc16(cpu) + (uint32_t)(cortex_m4_internal_sign_extend(opcode & 0x7ffu, 11) * 2);
        return true;
    }
    return false;
}
