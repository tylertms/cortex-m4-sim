#include "cortex_m4_internal.h"

#include <limits.h>

static uint32_t visible_pc16(const CortexM4 *cpu) {
  return cpu->registers[15] + 2u;
}

static uint32_t visible_pc32(const CortexM4 *cpu) { return cpu->registers[15]; }

static int32_t sign_extend(uint32_t value, uint8_t width) {
  const uint32_t sign = 1u << (width - 1u);
  return (int32_t)((value ^ sign) - sign);
}

static bool read_data(CortexM4 *cpu, uint32_t address, uint8_t size,
                      uint32_t *value) {
  if (!cortex_m4_bus_read(cpu, address, size, CORTEX_M4_ACCESS_DATA, value)) {
    cpu->bfar = address;
    cpu->cfsr |= 1u << 9;
    cpu->stop = CORTEX_M4_STOP_BUS_FAULT;
    return false;
  }
  return true;
}

static bool write_data(CortexM4 *cpu, uint32_t address, uint8_t size,
                       uint32_t value) {
  if (!cortex_m4_bus_write(cpu, address, size, CORTEX_M4_ACCESS_DATA, value)) {
    cpu->bfar = address;
    cpu->cfsr |= 1u << 9;
    cpu->stop = CORTEX_M4_STOP_BUS_FAULT;
    return false;
  }
  return true;
}

static void write_pc(CortexM4 *cpu, uint32_t value) {
  if ((value & 1u) == 0) {
    cpu->cfsr |= 1u << 16;
    cpu->stop = CORTEX_M4_STOP_USAGE_FAULT;
    return;
  }
  cpu->registers[15] = value & ~1u;
}

static void set_logic_flags(CortexM4 *cpu, uint32_t value, bool carry) {
  cortex_m4_set_nz(cpu, value);
  cpu->xpsr &= ~CORTEX_M4_XPSR_C;
  if (carry) {
    cpu->xpsr |= CORTEX_M4_XPSR_C;
  }
}

static uint32_t add_flags(CortexM4 *cpu, uint32_t left, uint32_t right,
                          bool carry) {
  bool carry_out = false;
  bool overflow = false;
  const uint32_t result =
      cortex_m4_add_with_carry(left, right, carry, &carry_out, &overflow);
  cortex_m4_set_nzcv(cpu, result, carry_out, overflow);
  return result;
}

static bool execute_shift_immediate(CortexM4 *cpu, uint16_t opcode) {
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

static bool execute_add_subtract(CortexM4 *cpu, uint16_t opcode) {
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

static bool execute_immediate(CortexM4 *cpu, uint16_t opcode) {
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

static bool execute_data_processing(CortexM4 *cpu, uint16_t opcode) {
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
    result = cortex_m4_shift(left, 0, right & 0xffu, carry, &carry);
    cpu->registers[destination] = result;
    set_logic_flags(cpu, result, carry);
    break;
  case 3:
    result = cortex_m4_shift(left, 1, right & 0xffu, carry, &carry);
    cpu->registers[destination] = result;
    set_logic_flags(cpu, result, carry);
    break;
  case 4:
    result = cortex_m4_shift(left, 2, right & 0xffu, carry, &carry);
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
    result = cortex_m4_shift(left, 3, right & 0xffu, carry, &carry);
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

static bool execute_high_register(CortexM4 *cpu, uint16_t opcode) {
  const uint8_t operation = (uint8_t)((opcode >> 8) & 3u);
  const uint8_t source = (uint8_t)(((opcode >> 3) & 15u));
  const uint8_t destination = (uint8_t)((opcode & 7u) | ((opcode >> 4) & 8u));
  const uint32_t source_value =
      source == 15 ? visible_pc16(cpu)
                   : cortex_m4_read_register_internal(cpu, source);
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

static bool execute_register_offset_memory(CortexM4 *cpu, uint16_t opcode) {
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

static bool execute_immediate_memory(CortexM4 *cpu, uint16_t opcode) {
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

static bool execute_halfword_memory(CortexM4 *cpu, uint16_t opcode) {
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

static bool execute_stack_memory(CortexM4 *cpu, uint16_t opcode) {
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

static bool execute_push_pop(CortexM4 *cpu, uint16_t opcode) {
  const bool pop = (opcode & (1u << 11)) != 0;
  const bool extra = (opcode & (1u << 8)) != 0;
  const uint8_t list = (uint8_t)opcode;
  uint32_t stack_pointer = cortex_m4_read_register_internal(cpu, 13);
  if (!pop) {
    uint32_t count = extra ? 1u : 0u;
    for (uint8_t index = 0; index < 8; index++) {
      if ((list & (1u << index)) != 0)
        count++;
    }
    uint32_t address = stack_pointer - count * 4u;
    const uint32_t new_stack = address;
    for (uint8_t index = 0; index < 8; index++) {
      if ((list & (1u << index)) != 0) {
        if (!write_data(cpu, address, 4, cpu->registers[index]))
          return true;
        address += 4;
      }
    }
    if (extra && !write_data(cpu, address, 4, cpu->registers[14]))
      return true;
    cortex_m4_write_register_internal(cpu, 13, new_stack);
    return true;
  }
  for (uint8_t index = 0; index < 8; index++) {
    if ((list & (1u << index)) != 0) {
      uint32_t value = 0;
      if (!read_data(cpu, stack_pointer, 4, &value))
        return true;
      cpu->registers[index] = value;
      stack_pointer += 4;
    }
  }
  if (extra) {
    uint32_t value = 0;
    if (!read_data(cpu, stack_pointer, 4, &value))
      return true;
    stack_pointer += 4;
    write_pc(cpu, value);
  }
  cortex_m4_write_register_internal(cpu, 13, stack_pointer);
  return true;
}

static bool execute_multiple(CortexM4 *cpu, uint16_t opcode) {
  const bool load = (opcode & (1u << 11)) != 0;
  const uint8_t base = (uint8_t)((opcode >> 8) & 7u);
  const uint8_t list = (uint8_t)opcode;
  uint32_t address = cpu->registers[base];
  for (uint8_t index = 0; index < 8; index++) {
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
  }
  if (!load || (list & (1u << base)) == 0) {
    cpu->registers[base] = address;
  }
  return true;
}

static bool execute_miscellaneous(CortexM4 *cpu, uint16_t opcode) {
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
      cpu->registers[15] += immediate;
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
  if ((opcode & 0xfff0u) == 0xb660u) {
    cpu->primask = (opcode & 1u) != 0 ? 1u : 0u;
    return true;
  }
  return false;
}

bool cortex_m4_execute_thumb16(CortexM4 *cpu, uint16_t opcode) {
  if ((opcode & 0xff00u) == 0xbe00u) {
    cpu->stop = CORTEX_M4_STOP_BREAKPOINT;
    return true;
  }
  if ((opcode & 0xff00u) == 0xbf00u) {
    const uint8_t immediate = (uint8_t)opcode;
    if ((immediate & 15u) != 0) {
      cpu->it_state = immediate;
    } else if (immediate == 0x20u) {
      cpu->sleeping = true;
    } else if (immediate == 0x40u) {
      if (!cpu->event_register)
        cpu->sleeping = true;
      cpu->event_register = false;
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
                           ((first & 0x03ffu) << 12) |
                           ((second & 0x07ffu) << 1);
  return (uint32_t)sign_extend(encoded, 25);
}

bool cortex_m4_execute_thumb32(CortexM4 *cpu, uint16_t first, uint16_t second) {
  if ((first & 0xf800u) == 0xf000u && (second & 0xd000u) == 0xd000u) {
    const uint32_t base = visible_pc32(cpu);
    cpu->registers[14] = base | 1u;
    cpu->registers[15] = base + decode_branch_offset(first, second);
    return true;
  }
  if ((first & 0xfbf0u) == 0xf240u || (first & 0xfbf0u) == 0xf2c0u) {
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint32_t immediate = ((first & 15u) << 12) |
                               (((first >> 10) & 1u) << 11) |
                               (((second >> 12) & 7u) << 8) | (second & 0xffu);
    if ((first & 0x0080u) == 0) {
      cpu->registers[destination] = immediate;
    } else {
      cpu->registers[destination] =
          (cpu->registers[destination] & 0xffffu) | (immediate << 16);
    }
    return true;
  }
  return false;
}
