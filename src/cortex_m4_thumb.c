#include "cortex_m4_internal.h"

#include <limits.h>

static uint32_t visible_pc16(const CortexM4* cpu) { return cpu->registers[15] + 2u; }

static uint32_t visible_pc32(const CortexM4* cpu) { return cpu->registers[15]; }

static int32_t sign_extend(uint32_t value, uint8_t width) {
    const uint32_t sign = 1u << (width - 1u);
    return (int32_t)((value ^ sign) - sign);
}

static bool read_data(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t* value) {
    return cortex_m4_data_read(cpu, address, size, CORTEX_M4_ACCESS_DATA, value);
}

static bool write_data(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t value) {
    return cortex_m4_data_write(cpu, address, size, CORTEX_M4_ACCESS_DATA, value);
}

static void write_pc(CortexM4* cpu, uint32_t value) {
    if ((value & 1u) == 0) {
        cpu->cfsr |= 1u << 17;
        cortex_m4_raise_fault(cpu, 6);
        return;
    }
    cpu->registers[15] = value & ~1u;
}

static void load_write_pc(CortexM4* cpu, uint32_t value) {
    if ((value & 0xfffffff0u) == 0xfffffff0u) {
        cortex_m4_exception_return(cpu, value);
        return;
    }
    write_pc(cpu, value);
}

static void set_logic_flags(CortexM4* cpu, uint32_t value, bool carry) {
    cortex_m4_set_nz(cpu, value);
    cpu->xpsr &= ~CORTEX_M4_XPSR_C;
    if (carry) {
        cpu->xpsr |= CORTEX_M4_XPSR_C;
    }
}

static uint32_t add_flags(CortexM4* cpu, uint32_t left, uint32_t right, bool carry) {
    bool carry_out = false;
    bool overflow = false;
    const uint32_t result =
        cortex_m4_add_with_carry(left, right, carry, &carry_out, &overflow);
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
    const uint32_t result =
        cortex_m4_shift(cpu->registers[source], type, amount, carry, &carry);
    cpu->registers[destination] = result;
    set_logic_flags(cpu, result, carry);
    return true;
}

static bool execute_add_subtract(CortexM4* cpu, uint16_t opcode) {
    const bool immediate = (opcode & (1u << 10)) != 0;
    const bool subtract = (opcode & (1u << 9)) != 0;
    const uint8_t operand = (uint8_t)((opcode >> 6) & 7u);
    const uint8_t source = (uint8_t)((opcode >> 3) & 7u);
    const uint8_t destination = (uint8_t)(opcode & 7u);
    const uint32_t right = immediate ? operand : cpu->registers[operand];
    cpu->registers[destination] =
        subtract ? add_flags(cpu, cpu->registers[source], ~right, true)
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
        cpu->registers[destination] =
            add_flags(cpu, cpu->registers[destination], immediate, false);
    } else {
        cpu->registers[destination] =
            add_flags(cpu, cpu->registers[destination], ~immediate, true);
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
        set_logic_flags(cpu, result, carry);
        break;
    case 3:
        result = cortex_m4_shift_register(left, 1, right, carry, &carry);
        cpu->registers[destination] = result;
        set_logic_flags(cpu, result, carry);
        break;
    case 4:
        result = cortex_m4_shift_register(left, 2, right, carry, &carry);
        cpu->registers[destination] = result;
        set_logic_flags(cpu, result, carry);
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
        set_logic_flags(cpu, result, carry);
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
        destination == 15 ? visible_pc16(cpu)
                          : cortex_m4_read_register_internal(cpu, destination);
    if (operation == 0) {
        const uint32_t result = destination_value + source_value;
        if (destination == 15) {
            write_pc(cpu, result);
        } else {
            cortex_m4_write_register_internal(cpu, destination, result);
        }
    } else if (operation == 1) {
        add_flags(cpu, destination_value, ~source_value, true);
    } else if (operation == 2) {
        if (destination == 15) {
            write_pc(cpu, source_value);
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
        write_pc(cpu, source_value);
    }
    return true;
}

static bool execute_register_offset_memory(CortexM4* cpu, uint16_t opcode) {
    const uint8_t operation = (uint8_t)((opcode >> 9) & 7u);
    const uint8_t offset_register = (uint8_t)((opcode >> 6) & 7u);
    const uint8_t base_register = (uint8_t)((opcode >> 3) & 7u);
    const uint8_t target_register = (uint8_t)(opcode & 7u);
    const uint32_t address =
        cpu->registers[base_register] + cpu->registers[offset_register];
    uint32_t value = 0;
    switch (operation) {
    case 0:
        return write_data(cpu, address, 4, cpu->registers[target_register]);
    case 1:
        return write_data(cpu, address, 2, cpu->registers[target_register]);
    case 2:
        return write_data(cpu, address, 1, cpu->registers[target_register]);
    case 3:
        if (!read_data(cpu, address, 1, &value))
            return true;
        cpu->registers[target_register] = (uint32_t)sign_extend(value, 8);
        return true;
    case 4:
        if (!read_data(cpu, address, 4, &value))
            return true;
        cpu->registers[target_register] = value;
        return true;
    case 5:
        if (!read_data(cpu, address, 2, &value))
            return true;
        cpu->registers[target_register] = value;
        return true;
    case 6:
        if (!read_data(cpu, address, 1, &value))
            return true;
        cpu->registers[target_register] = value;
        return true;
    default:
        if (!read_data(cpu, address, 2, &value))
            return true;
        cpu->registers[target_register] = (uint32_t)sign_extend(value, 16);
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
        return write_data(cpu, address, size, cpu->registers[target_register]);
    }
    uint32_t value = 0;
    if (read_data(cpu, address, size, &value)) {
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
        return write_data(cpu, address, 2, cpu->registers[target_register]);
    }
    uint32_t value = 0;
    if (read_data(cpu, address, 2, &value)) {
        cpu->registers[target_register] = value;
    }
    return true;
}

static bool execute_stack_memory(CortexM4* cpu, uint16_t opcode) {
    const bool load = (opcode & (1u << 11)) != 0;
    const uint8_t target_register = (uint8_t)((opcode >> 8) & 7u);
    const uint32_t address =
        cortex_m4_read_register_internal(cpu, 13) + (opcode & 0xffu) * 4u;
    if (!load) {
        return write_data(cpu, address, 4, cpu->registers[target_register]);
    }
    uint32_t value = 0;
    if (read_data(cpu, address, 4, &value)) {
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
                if (!write_data(cpu, address, 4, cpu->registers[index]))
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
            if (!write_data(cpu, address, 4, cpu->registers[14]))
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
            if (!read_data(cpu, stack_pointer, 4, &value))
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
        if (!read_data(cpu, stack_pointer, 4, &value))
            return true;
        stack_pointer += 4;
        cortex_m4_write_register_internal(cpu, 13, stack_pointer);
        load_write_pc(cpu, value);
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
    uint32_t address =
        cortex_m4_exception_advanced_multiple_address(cpu, cpu->registers[base]);
    const uint8_t resume = cortex_m4_exception_advanced_multiple_resume(cpu);
    for (uint8_t index = 0; index < 8; index++) {
        if (index < resume)
            continue;
        if ((list & (1u << index)) == 0)
            continue;
        if (load) {
            uint32_t value = 0;
            if (!read_data(cpu, address, 4, &value))
                return true;
            cpu->registers[index] = value;
        } else if (!write_data(cpu, address, 4, cpu->registers[index])) {
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
        cortex_m4_write_register_internal(
            cpu, 13, (opcode & 0x80u) != 0 ? stack - amount : stack + amount);
        return true;
    }
    if ((opcode & 0xf500u) == 0xb100u) {
        const bool nonzero = (opcode & (1u << 11)) != 0;
        const uint8_t source = (uint8_t)(opcode & 7u);
        const uint32_t immediate =
            ((opcode >> 3) & 0x1fu) * 2u + ((opcode >> 9) & 1u) * 64u;
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
            cpu->registers[destination] = (uint32_t)sign_extend(value, 16);
        if (operation == 1)
            cpu->registers[destination] = (uint32_t)sign_extend(value, 8);
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
            cpu->registers[destination] = (uint32_t)sign_extend(half, 16);
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
        if (read_data(cpu, address, 4, &value))
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
        const uint32_t base = (opcode & (1u << 11)) != 0
                                  ? cortex_m4_read_register_internal(cpu, 13)
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
            cpu->registers[15] =
                visible_pc16(cpu) + (uint32_t)(sign_extend(opcode & 0xffu, 8) * 2);
        }
        return true;
    }
    if ((opcode & 0xf800u) == 0xe000u) {
        cpu->registers[15] =
            visible_pc16(cpu) + (uint32_t)(sign_extend(opcode & 0x7ffu, 11) * 2);
        return true;
    }
    return false;
}

static uint32_t decode_branch_offset(uint16_t first, uint16_t second) {
    const uint32_t sign = (first >> 10) & 1u;
    const uint32_t j1 = (second >> 13) & 1u;
    const uint32_t j2 = (second >> 11) & 1u;
    const uint32_t i1 = ~(j1 ^ sign) & 1u;
    const uint32_t i2 = ~(j2 ^ sign) & 1u;
    const uint32_t encoded = (sign << 24) | (i1 << 23) | (i2 << 22) |
                             ((first & 0x03ffu) << 12) | ((second & 0x07ffu) << 1);
    return (uint32_t)sign_extend(encoded, 25);
}

static uint32_t read_special_register(const CortexM4* cpu, uint8_t selector) {
    const uint32_t xpsr = cortex_m4_xpsr_value(cpu);
    switch (selector) {
    case 0:
        return xpsr & 0xf80f0000u;
    case 1:
        return xpsr & 0xf80f01ffu;
    case 2:
        return xpsr & 0x0700fc00u;
    case 3:
        return xpsr;
    case 5:
        return cpu->xpsr & 0x1ffu;
    case 6:
        return xpsr & 0x0700fc00u;
    case 7:
        return xpsr & 0x0700fdffu;
    case 8:
        return cpu->msp;
    case 9:
        return cpu->psp;
    case 16:
        return cpu->primask;
    case 17:
    case 18:
        return cpu->basepri;
    case 19:
        return cpu->faultmask;
    case 20:
        return cpu->control;
    default:
        return 0;
    }
}

static void write_special_register(CortexM4* cpu, uint8_t selector, uint32_t value) {
    const bool privileged =
        (cpu->xpsr & 0x1ffu) != 0 || (cpu->control & CORTEX_M4_CONTROL_NPRIV) == 0;
    if (selector <= 3) {
        cpu->xpsr = (cpu->xpsr & ~0xf80f0000u) | (value & 0xf80f0000u) | CORTEX_M4_XPSR_T;
    } else if (selector == 8 && privileged) {
        cpu->msp = value & ~3u;
    } else if (selector == 9 && privileged) {
        cpu->psp = value & ~3u;
    } else if (selector == 16 && privileged) {
        cpu->primask = value & 1u;
    } else if (selector == 17 && privileged) {
        cpu->basepri = value & 0xf0u;
    } else if (selector == 18 && privileged) {
        const uint32_t priority = value & 0xf0u;
        if (priority != 0 && (cpu->basepri == 0 || priority < cpu->basepri)) {
            cpu->basepri = priority;
        }
    } else if (selector == 19 && privileged) {
        cpu->faultmask = value & 1u;
    } else if (selector == 20 && privileged) {
        cpu->control = value & 7u;
    }
}

static uint32_t rotate_right(uint32_t value, uint8_t amount) {
    amount &= 31u;
    return amount == 0 ? value : (value >> amount) | (value << (32u - amount));
}

static uint8_t count_leading_zeros(uint32_t value) {
    uint8_t count = 0;
    while (count < 32 && (value & 0x80000000u) == 0) {
        value <<= 1;
        count++;
    }
    return count;
}

static uint32_t reverse_bits(uint32_t value) {
    value = ((value & 0x55555555u) << 1) | ((value >> 1) & 0x55555555u);
    value = ((value & 0x33333333u) << 2) | ((value >> 2) & 0x33333333u);
    value = ((value & 0x0f0f0f0fu) << 4) | ((value >> 4) & 0x0f0f0f0fu);
    value = ((value & 0x00ff00ffu) << 8) | ((value >> 8) & 0x00ff00ffu);
    return (value << 16) | (value >> 16);
}

static uint32_t expand_immediate(uint16_t first, uint16_t second, bool carry_in,
                                 bool* carry_out) {
    const uint16_t immediate = (uint16_t)(((first >> 10) & 1u) << 11) |
                               (uint16_t)(((second >> 12) & 7u) << 8) | (second & 0xffu);
    uint32_t value = immediate & 0xffu;
    bool carry = carry_in;
    if ((immediate & 0xc00u) == 0) {
        switch ((immediate >> 8) & 3u) {
        case 0:
            break;
        case 1:
            value |= value << 16;
            break;
        case 2:
            value = (value << 24) | (value << 8);
            break;
        default:
            value *= 0x01010101u;
            break;
        }
    } else {
        const uint8_t rotation = (uint8_t)((immediate >> 7) & 31u);
        value = rotate_right(0x80u | (immediate & 0x7fu), rotation);
        carry = (value & 0x80000000u) != 0;
    }
    if (carry_out != NULL) {
        *carry_out = carry;
    }
    return value;
}

static bool execute_data_operation(CortexM4* cpu, uint8_t operation, uint8_t destination,
                                   uint32_t left, uint32_t right, bool set_flags,
                                   bool logical_carry) {
    uint32_t result = 0;
    bool carry = false;
    bool overflow = false;
    bool arithmetic = false;
    switch (operation) {
    case 0:
        result = left & right;
        break;
    case 1:
        result = left & ~right;
        break;
    case 2:
        result = left | right;
        break;
    case 3:
        result = left | ~right;
        break;
    case 4:
        result = left ^ right;
        break;
    case 8:
        arithmetic = true;
        result = cortex_m4_add_with_carry(left, right, false, &carry, &overflow);
        break;
    case 10:
        arithmetic = true;
        result = cortex_m4_add_with_carry(left, right, (cpu->xpsr & CORTEX_M4_XPSR_C) != 0,
                                          &carry, &overflow);
        break;
    case 11:
        arithmetic = true;
        result = cortex_m4_add_with_carry(left, ~right, (cpu->xpsr & CORTEX_M4_XPSR_C) != 0,
                                          &carry, &overflow);
        break;
    case 13:
        arithmetic = true;
        result = cortex_m4_add_with_carry(left, ~right, true, &carry, &overflow);
        break;
    case 14:
        arithmetic = true;
        result = cortex_m4_add_with_carry(~left, right, true, &carry, &overflow);
        break;
    default:
        return false;
    }
    if (set_flags) {
        if (arithmetic) {
            cortex_m4_set_nzcv(cpu, result, carry, overflow);
        } else {
            set_logic_flags(cpu, result, logical_carry);
        }
    }
    if (destination != 15) {
        cortex_m4_write_register_internal(cpu, destination, result);
    } else if (!set_flags) {
        write_pc(cpu, result);
    }
    return true;
}

static bool execute_modified_immediate(CortexM4* cpu, uint16_t first, uint16_t second) {
    const uint8_t operation = (uint8_t)((first >> 5) & 15u);
    const bool set_flags = (first & 0x10u) != 0;
    const uint8_t source = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    bool carry = (cpu->xpsr & CORTEX_M4_XPSR_C) != 0;
    const uint32_t immediate = expand_immediate(first, second, carry, &carry);
    const uint32_t left = source == 15 ? 0 : cortex_m4_read_register_internal(cpu, source);
    return execute_data_operation(cpu, operation, destination, left, immediate, set_flags,
                                  carry);
}

static bool execute_shifted_register(CortexM4* cpu, uint16_t first, uint16_t second) {
    const uint8_t operation = (uint8_t)((first >> 5) & 15u);
    const bool set_flags = (first & 0x10u) != 0;
    const uint8_t source = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t right_register = (uint8_t)(second & 15u);
    const uint8_t shift_type = (uint8_t)((second >> 4) & 3u);
    const uint8_t shift_amount =
        (uint8_t)(((second >> 12) & 7u) << 2) | (uint8_t)((second >> 6) & 3u);
    bool carry = (cpu->xpsr & CORTEX_M4_XPSR_C) != 0;
    const uint32_t right =
        cortex_m4_shift(cortex_m4_read_register_internal(cpu, right_register), shift_type,
                        shift_amount, carry, &carry);
    const uint32_t left = source == 15 ? 0 : cortex_m4_read_register_internal(cpu, source);
    return execute_data_operation(cpu, operation, destination, left, right, set_flags,
                                  carry);
}

bool cortex_m4_execute_thumb32(CortexM4* cpu, uint16_t first, uint16_t second) {
    if (cortex_m4_execute_remaining(cpu, first, second)) {
        return true;
    }
    if (cortex_m4_execute_thumb32_extra(cpu, first, second)) {
        return true;
    }
    if (cortex_m4_execute_dsp(cpu, first, second)) {
        return true;
    }
    if ((first & 0xf800u) == 0xf000u && (second & 0xd000u) == 0xd000u) {
        const uint32_t base = visible_pc32(cpu);
        cpu->registers[14] = base | 1u;
        cpu->registers[15] = base + decode_branch_offset(first, second);
        return true;
    }
    if ((first & 0xf800u) == 0xf000u && (second & 0xd000u) == 0x9000u) {
        cpu->registers[15] = visible_pc32(cpu) + decode_branch_offset(first, second);
        return true;
    }
    if ((first & 0xf800u) == 0xf000u && (second & 0xd000u) == 0x8000u) {
        const uint8_t condition = (uint8_t)((first >> 6) & 15u);
        if (condition < 14 && cortex_m4_condition_passed(cpu, condition)) {
            const uint32_t encoded = ((uint32_t)((first >> 10) & 1u) << 20) |
                                     ((uint32_t)((second >> 11) & 1u) << 19) |
                                     ((uint32_t)((second >> 13) & 1u) << 18) |
                                     ((uint32_t)(first & 0x3fu) << 12) |
                                     ((uint32_t)(second & 0x7ffu) << 1);
            cpu->registers[15] = visible_pc32(cpu) + (uint32_t)sign_extend(encoded, 21);
        }
        if (condition < 14) {
            return true;
        }
    }
    if (first == 0xf3efu && (second & 0xf000u) == 0x8000u) {
        const uint8_t destination = (uint8_t)((second >> 8) & 15u);
        cortex_m4_write_register_internal(cpu, destination,
                                          read_special_register(cpu, (uint8_t)second));
        return true;
    }
    if ((first & 0xfff0u) == 0xf380u && (second & 0xff00u) == 0x8800u) {
        const uint8_t source = (uint8_t)(first & 15u);
        write_special_register(cpu, (uint8_t)second,
                               cortex_m4_read_register_internal(cpu, source));
        return true;
    }
    if ((first & 0xfbf0u) == 0xf240u || (first & 0xfbf0u) == 0xf2c0u) {
        const uint8_t destination = (uint8_t)((second >> 8) & 15u);
        const uint32_t immediate = ((first & 15u) << 12) | (((first >> 10) & 1u) << 11) |
                                   (((second >> 12) & 7u) << 8) | (second & 0xffu);
        if ((first & 0x0080u) == 0) {
            cpu->registers[destination] = immediate;
        } else {
            cpu->registers[destination] =
                (cpu->registers[destination] & 0xffffu) | (immediate << 16);
        }
        return true;
    }
    if ((first & 0xfa00u) == 0xf000u && (second & 0x8000u) == 0) {
        return execute_modified_immediate(cpu, first, second);
    }
    if ((first & 0xfe00u) == 0xea00u && (second & 0x8000u) == 0) {
        return execute_shifted_register(cpu, first, second);
    }
    if ((first & 0xff80u) == 0xfa00u && (second & 0xf0c0u) == 0xf000u) {
        const uint8_t source = (uint8_t)(first & 15u);
        const uint8_t destination = (uint8_t)((second >> 8) & 15u);
        const uint8_t amount_register = (uint8_t)(second & 15u);
        const uint8_t shift_type = (uint8_t)((first >> 5) & 3u);
        const bool carry_in = (cpu->xpsr & CORTEX_M4_XPSR_C) != 0;
        const uint32_t result = cortex_m4_shift_register(
            cortex_m4_read_register_internal(cpu, source), shift_type,
            cortex_m4_read_register_internal(cpu, amount_register), carry_in, NULL);
        cortex_m4_write_register_internal(cpu, destination, result);
        return true;
    }
    if ((first & 0xfff0u) == 0xfab0u && (second & 0xf0f0u) == 0xf080u) {
        const uint8_t source = (uint8_t)(first & 15u);
        const uint8_t destination = (uint8_t)((second >> 8) & 15u);
        cortex_m4_write_register_internal(
            cpu, destination,
            count_leading_zeros(cortex_m4_read_register_internal(cpu, source)));
        return true;
    }
    if ((first & 0xfff0u) == 0xfa90u && (second & 0xf0f0u) == 0xf0a0u) {
        const uint8_t source = (uint8_t)(first & 15u);
        const uint8_t destination = (uint8_t)((second >> 8) & 15u);
        cortex_m4_write_register_internal(
            cpu, destination, reverse_bits(cortex_m4_read_register_internal(cpu, source)));
        return true;
    }
    if ((first & 0xfff0u) == 0xf360u && (second & 0x8000u) == 0) {
        const uint8_t source = (uint8_t)(first & 15u);
        const uint8_t destination = (uint8_t)((second >> 8) & 15u);
        const uint8_t least_bit =
            (uint8_t)(((second >> 12) & 7u) << 2) | (uint8_t)((second >> 6) & 3u);
        const uint8_t most_bit = (uint8_t)(second & 31u);
        if (most_bit >= least_bit) {
            const uint8_t width = (uint8_t)(most_bit - least_bit + 1u);
            const uint32_t mask =
                width == 32 ? UINT32_MAX : ((1u << width) - 1u) << least_bit;
            const uint32_t source_value =
                source == 15 ? 0 : cortex_m4_read_register_internal(cpu, source);
            const uint32_t destination_value =
                cortex_m4_read_register_internal(cpu, destination);
            cortex_m4_write_register_internal(cpu, destination,
                                              (destination_value & ~mask) |
                                                  ((source_value << least_bit) & mask));
        }
        return true;
    }
    if (((first & 0xfff0u) == 0xfbb0u || (first & 0xfff0u) == 0xfb90u) &&
        (second & 0xf0f0u) == 0xf0f0u) {
        const uint8_t numerator_register = (uint8_t)(first & 15u);
        const uint8_t destination = (uint8_t)((second >> 8) & 15u);
        const uint8_t denominator_register = (uint8_t)(second & 15u);
        const uint32_t numerator =
            cortex_m4_read_register_internal(cpu, numerator_register);
        const uint32_t denominator =
            cortex_m4_read_register_internal(cpu, denominator_register);
        if (denominator == 0) {
            if ((cpu->ccr & (1u << 4)) != 0) {
                cpu->cfsr |= 1u << 25;
                cortex_m4_raise_fault(cpu, 6);
            } else {
                cortex_m4_write_register_internal(cpu, destination, 0);
            }
            return true;
        }
        uint32_t result = 0;
        if ((first & 0x0020u) != 0) {
            result = numerator / denominator;
        } else if (numerator == 0x80000000u && denominator == 0xffffffffu) {
            result = 0x80000000u;
        } else {
            result = (uint32_t)((int32_t)numerator / (int32_t)denominator);
        }
        cortex_m4_write_register_internal(cpu, destination, result);
        return true;
    }
    if ((first & 0xfff0u) == 0xfb00u && (second & 0x00f0u) == 0) {
        const uint8_t left_register = (uint8_t)(first & 15u);
        const uint8_t destination = (uint8_t)((second >> 8) & 15u);
        const uint8_t accumulator = (uint8_t)(second >> 12);
        const uint8_t right_register = (uint8_t)(second & 15u);
        uint32_t result = cortex_m4_read_register_internal(cpu, left_register) *
                          cortex_m4_read_register_internal(cpu, right_register);
        if (accumulator != 15) {
            result += cortex_m4_read_register_internal(cpu, accumulator);
        }
        cortex_m4_write_register_internal(cpu, destination, result);
        return true;
    }
    if (((first & 0xfff0u) == 0xfba0u || (first & 0xfff0u) == 0xfb80u) &&
        (second & 0x00f0u) == 0) {
        const uint8_t left_register = (uint8_t)(first & 15u);
        const uint8_t low_destination = (uint8_t)(second >> 12);
        const uint8_t high_destination = (uint8_t)((second >> 8) & 15u);
        const uint8_t right_register = (uint8_t)(second & 15u);
        uint64_t result = 0;
        if ((first & 0x0020u) != 0) {
            result = (uint64_t)cortex_m4_read_register_internal(cpu, left_register) *
                     cortex_m4_read_register_internal(cpu, right_register);
        } else {
            result =
                (uint64_t)((int64_t)(int32_t)cortex_m4_read_register_internal(
                               cpu, left_register) *
                           (int32_t)cortex_m4_read_register_internal(cpu, right_register));
        }
        cortex_m4_write_register_internal(cpu, low_destination, (uint32_t)result);
        cortex_m4_write_register_internal(cpu, high_destination, (uint32_t)(result >> 32));
        return true;
    }
    if (((first & 0xfff0u) == 0xfbe0u || (first & 0xfff0u) == 0xfbc0u) &&
        (second & 0x00f0u) == 0) {
        const uint8_t left_register = (uint8_t)(first & 15u);
        const uint8_t low_destination = (uint8_t)(second >> 12);
        const uint8_t high_destination = (uint8_t)((second >> 8) & 15u);
        const uint8_t right_register = (uint8_t)(second & 15u);
        uint64_t product = 0;
        if ((first & 0x0020u) != 0) {
            product = (uint64_t)cortex_m4_read_register_internal(cpu, left_register) *
                      cortex_m4_read_register_internal(cpu, right_register);
        } else {
            product =
                (uint64_t)((int64_t)(int32_t)cortex_m4_read_register_internal(
                               cpu, left_register) *
                           (int32_t)cortex_m4_read_register_internal(cpu, right_register));
        }
        const uint64_t accumulator =
            ((uint64_t)cortex_m4_read_register_internal(cpu, high_destination) << 32) |
            cortex_m4_read_register_internal(cpu, low_destination);
        const uint64_t result = accumulator + product;
        cortex_m4_write_register_internal(cpu, low_destination, (uint32_t)result);
        cortex_m4_write_register_internal(cpu, high_destination, (uint32_t)(result >> 32));
        return true;
    }
    if (((first & 0xfff0u) == 0xf3c0u || (first & 0xfff0u) == 0xf340u) &&
        (second & 0x8000u) == 0) {
        const uint8_t source = (uint8_t)(first & 15u);
        const uint8_t destination = (uint8_t)((second >> 8) & 15u);
        const uint8_t least_bit =
            (uint8_t)(((second >> 12) & 7u) << 2) | (uint8_t)((second >> 6) & 3u);
        const uint8_t width = (uint8_t)((second & 31u) + 1u);
        uint32_t value = cortex_m4_read_register_internal(cpu, source);
        if (width == 32) {
            value >>= least_bit;
        } else {
            value = (value >> least_bit) & ((1u << width) - 1u);
        }
        if ((first & 0x0080u) == 0 && width < 32 && (value & (1u << (width - 1u))) != 0) {
            value |= ~((1u << width) - 1u);
        }
        cortex_m4_write_register_internal(cpu, destination, value);
        return true;
    }
    if ((first & 0xfff0u) == 0xe850u && (second & 0x0f00u) == 0x0f00u) {
        const uint8_t base = (uint8_t)(first & 15u);
        const uint8_t target = (uint8_t)(second >> 12);
        const uint32_t address =
            cortex_m4_read_register_internal(cpu, base) + (uint32_t)(second & 0xffu) * 4u;
        if (!cortex_m4_require_alignment(cpu, address, 4)) {
            return true;
        }
        uint32_t value = 0;
        if (read_data(cpu, address, 4, &value)) {
            cortex_m4_write_register_internal(cpu, target, value);
            cortex_m4_timing_reserve(cpu, address, 4);
        }
        return true;
    }
    if ((first & 0xfff0u) == 0xe840u && (second & 0x0800u) == 0) {
        const uint8_t base = (uint8_t)(first & 15u);
        const uint8_t target = (uint8_t)(second >> 12);
        const uint8_t status = (uint8_t)((second >> 8) & 15u);
        const uint32_t address =
            cortex_m4_read_register_internal(cpu, base) + (uint32_t)(second & 0xffu) * 4u;
        if (!cortex_m4_require_alignment(cpu, address, 4)) {
            return true;
        }
        const bool matched = cortex_m4_timing_consume_reservation(cpu, address, 4);
        if (matched &&
            !write_data(cpu, address, 4, cortex_m4_read_register_internal(cpu, target))) {
            return true;
        }
        cortex_m4_write_register_internal(cpu, status, matched ? 0 : 1);
        return true;
    }
    if ((first & 0xfff0u) == 0xe8d0u && (second & 0x0f0fu) == 0x0f0fu &&
        ((second >> 4) & 15u) >= 4u && ((second >> 4) & 15u) <= 5u) {
        const uint8_t base = (uint8_t)(first & 15u);
        const uint8_t target = (uint8_t)(second >> 12);
        const uint8_t size = ((second >> 4) & 15u) == 4u ? 1 : 2;
        const uint32_t address = cortex_m4_read_register_internal(cpu, base);
        if (!cortex_m4_require_alignment(cpu, address, size)) {
            return true;
        }
        uint32_t value = 0;
        if (read_data(cpu, address, size, &value)) {
            cortex_m4_write_register_internal(cpu, target, value);
            cortex_m4_timing_reserve(cpu, address, size);
        }
        return true;
    }
    if ((first & 0xfff0u) == 0xe8c0u && (second & 0x0f00u) == 0x0f00u &&
        ((second >> 4) & 15u) >= 4u && ((second >> 4) & 15u) <= 5u) {
        const uint8_t base = (uint8_t)(first & 15u);
        const uint8_t target = (uint8_t)(second >> 12);
        const uint8_t status = (uint8_t)(second & 15u);
        const uint8_t size = ((second >> 4) & 15u) == 4u ? 1 : 2;
        const uint32_t address = cortex_m4_read_register_internal(cpu, base);
        if (!cortex_m4_require_alignment(cpu, address, size)) {
            return true;
        }
        const bool matched = cortex_m4_timing_consume_reservation(cpu, address, size);
        if (matched && !write_data(cpu, address, size,
                                   cortex_m4_read_register_internal(cpu, target))) {
            return true;
        }
        cortex_m4_write_register_internal(cpu, status, matched ? 0 : 1);
        return true;
    }
    if ((first & 0xfff0u) == 0xe8d0u && (second & 0xffe0u) == 0xf000u) {
        const uint8_t base = (uint8_t)(first & 15u);
        const uint8_t index = (uint8_t)(second & 15u);
        const bool halfword = (second & 0x10u) != 0;
        const uint32_t address =
            cortex_m4_read_register_internal(cpu, base) +
            cortex_m4_read_register_internal(cpu, index) * (halfword ? 2u : 1u);
        uint32_t value = 0;
        if (read_data(cpu, address, halfword ? 2 : 1, &value)) {
            cpu->registers[15] = visible_pc32(cpu) + value * 2u;
        }
        return true;
    }
    if (((first & 0xffd0u) == 0xe880u || (first & 0xffd0u) == 0xe890u) && second != 0) {
        const bool load = (first & 0x0010u) != 0;
        const bool write_back = (first & 0x0020u) != 0;
        const uint8_t base = (uint8_t)(first & 15u);
        uint32_t address = cortex_m4_exception_advanced_multiple_address(
            cpu, cortex_m4_read_register_internal(cpu, base));
        const uint8_t resume = cortex_m4_exception_advanced_multiple_resume(cpu);
        if (!cortex_m4_require_alignment(cpu, address, 4)) {
            return true;
        }
        for (uint8_t index = 0; index < 16; index++) {
            if (index < resume) {
                continue;
            }
            if ((second & (1u << index)) == 0) {
                continue;
            }
            if (load) {
                uint32_t value = 0;
                if (!read_data(cpu, address, 4, &value)) {
                    return true;
                }
                cortex_m4_write_register_internal(cpu, index, value);
            } else if (!write_data(cpu, address, 4,
                                   cortex_m4_read_register_internal(cpu, index))) {
                return true;
            }
            address += 4;
            uint8_t next = 0;
            for (uint8_t candidate = index + 1u; candidate < 16u; candidate++) {
                if ((second & (1u << candidate)) != 0) {
                    next = candidate;
                    break;
                }
            }
            if (cortex_m4_exception_advanced_multiple_suspend(cpu, next, 4u, address)) {
                return true;
            }
        }
        if (write_back) {
            cortex_m4_write_register_internal(cpu, base, address);
        }
        cortex_m4_exception_advanced_multiple_complete(cpu);
        return true;
    }
    if (first == 0xf3bfu && second == 0x8f2fu) {
        cpu->exclusive_valid = false;
        return true;
    }
    if (first == 0xf3bfu && (second & 0xff0fu) == 0x8f0fu) {
        return true;
    }
    const uint16_t memory_operation = first & 0xfff0u;
    if ((memory_operation == 0xf890u || memory_operation == 0xf8b0u ||
         memory_operation == 0xf990u || memory_operation == 0xf9b0u) &&
        (second >> 12) == 15u) {
        return true;
    }
    if (memory_operation == 0xf8c0u || memory_operation == 0xf8d0u ||
        memory_operation == 0xf880u || memory_operation == 0xf890u ||
        memory_operation == 0xf8a0u || memory_operation == 0xf8b0u ||
        memory_operation == 0xf990u || memory_operation == 0xf9b0u) {
        const uint8_t base = (uint8_t)(first & 15u);
        const uint8_t target = (uint8_t)(second >> 12);
        const uint32_t base_value = base == 15
                                        ? visible_pc32(cpu) & ~3u
                                        : cortex_m4_read_register_internal(cpu, base);
        const uint32_t address = base_value + (second & 0x0fffu);
        const bool load = (memory_operation & 0x0010u) != 0;
        const uint8_t size = (memory_operation & 0x0040u) != 0   ? 4
                             : (memory_operation & 0x0020u) != 0 ? 2
                                                                 : 1;
        if (!load) {
            return write_data(cpu, address, size,
                              cortex_m4_read_register_internal(cpu, target));
        }
        uint32_t value = 0;
        if (!read_data(cpu, address, size, &value)) {
            return true;
        }
        if ((memory_operation & 0x0100u) != 0) {
            value = (uint32_t)sign_extend(value, (uint8_t)(size * 8u));
        }
        if (target == 15) {
            load_write_pc(cpu, value);
        } else {
            cortex_m4_write_register_internal(cpu, target, value);
        }
        return true;
    }
    if (memory_operation == 0xf840u || memory_operation == 0xf850u ||
        memory_operation == 0xf800u || memory_operation == 0xf810u ||
        memory_operation == 0xf820u || memory_operation == 0xf830u ||
        memory_operation == 0xf910u || memory_operation == 0xf930u) {
        const uint8_t base = (uint8_t)(first & 15u);
        const uint8_t target = (uint8_t)(second >> 12);
        const bool pre_index = (second & 0x0400u) != 0;
        const bool add = (second & 0x0200u) != 0;
        const bool write_back = (second & 0x0100u) != 0;
        const uint32_t base_value = cortex_m4_read_register_internal(cpu, base);
        const uint32_t offset = second & 0xffu;
        const uint32_t offset_address = add ? base_value + offset : base_value - offset;
        const uint32_t address = pre_index ? offset_address : base_value;
        const bool load = (memory_operation & 0x0010u) != 0;
        const uint8_t size = (memory_operation & 0x0040u) != 0   ? 4
                             : (memory_operation & 0x0020u) != 0 ? 2
                                                                 : 1;
        if (load) {
            uint32_t value = 0;
            if (!read_data(cpu, address, size, &value)) {
                return true;
            }
            if ((memory_operation & 0x0100u) != 0) {
                value = (uint32_t)sign_extend(value, (uint8_t)(size * 8u));
            }
            if (target == 15) {
                load_write_pc(cpu, value);
            } else {
                cortex_m4_write_register_internal(cpu, target, value);
            }
        } else if (!write_data(cpu, address, size,
                               cortex_m4_read_register_internal(cpu, target))) {
            return true;
        }
        if (write_back) {
            cortex_m4_write_register_internal(cpu, base, offset_address);
        }
        return true;
    }
    if (cortex_m4_execute_fpu(cpu, first, second)) {
        return true;
    }
    return false;
}
