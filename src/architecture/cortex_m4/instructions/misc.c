#include "architecture/cortex_m4/internal.h"

static bool valid_core_register(uint8_t index) { return index != 13u && index != 15u; }

static uint32_t sign_extend(uint32_t value, uint8_t width) {
    const uint32_t sign = 1u << (width - 1u);
    return (value ^ sign) - sign;
}

static void branch_to(CortexM4* cpu, uint32_t address) {
    if ((address & 1u) == 0) {
        cpu->cfsr |= 1u << 17;
        cortex_m4_raise_fault(cpu, 6);
        return;
    }
    cpu->registers[15] = address & ~1u;
}

static bool execute_reverse(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 0xfff0u) != 0xfa90u || (second & 0xf0c0u) != 0xf080u) {
        return false;
    }
    const uint8_t operation = (uint8_t)((second >> 4) & 15u);
    if (operation != 8u && operation != 9u && operation != 11u) {
        return false;
    }
    const uint8_t source = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    if (!valid_core_register(source) || !valid_core_register(destination)) {
        return false;
    }
    const uint32_t value = cpu->registers[source];
    uint32_t result = 0;
    if (operation == 8u) {
        result = ((value & 0x000000ffu) << 24) | ((value & 0x0000ff00u) << 8) |
                 ((value & 0x00ff0000u) >> 8) | ((value & 0xff000000u) >> 24);
    } else if (operation == 9u) {
        result = ((value & 0x00ff00ffu) << 8) | ((value & 0xff00ff00u) >> 8);
    } else {
        const uint32_t halfword = ((value & 0xffu) << 8) | ((value >> 8) & 0xffu);
        result = sign_extend(halfword, 16);
    }
    cpu->registers[destination] = result;
    return true;
}

static bool execute_hint(CortexM4* cpu, uint16_t first, uint16_t second) {
    if (first != 0xf3afu || (second & 0xff00u) != 0x8000u) {
        return false;
    }
    const uint8_t hint = (uint8_t)second;
    if (hint == 0u || hint == 1u || (hint & 0xf0u) == 0xf0u) {
        return true;
    }
    if (hint == 2u) {
        if (!cpu->event_register) {
            cpu->sleeping = true;
        }
        cpu->event_register = false;
        return true;
    }
    if (hint == 3u) {
        cortex_m4_system_wait_for_interrupt(cpu);
        return true;
    }
    if (hint == 4u) {
        cpu->event_register = true;
        return true;
    }
    return false;
}

static bool execute_preload_hint(uint16_t first, uint16_t second) {
    if ((second & 0xf000u) != 0xf000u) {
        return false;
    }
    const uint16_t operation = first & 0xfff0u;
    if (operation == 0xf890u || operation == 0xf990u) {
        return true;
    }
    if (operation != 0xf810u && operation != 0xf910u) {
        return false;
    }
    return (second & 0x0fc0u) == 0 || (second & 0x0f00u) == 0x0c00u;
}

static bool execute_negative_literal(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 15u) != 15u) {
        return false;
    }
    const uint16_t operation = first & 0xfff0u;
    if (operation != 0xf850u && operation != 0xf810u && operation != 0xf830u &&
        operation != 0xf910u && operation != 0xf930u) {
        return false;
    }
    const uint8_t target = (uint8_t)(second >> 12);
    if (target == 15u && operation != 0xf850u) {
        return false;
    }
    const uint8_t size = operation == 0xf850u                           ? 4u
                         : operation == 0xf830u || operation == 0xf930u ? 2u
                                                                        : 1u;
    const uint32_t address = (cpu->registers[15] & ~3u) - (second & 0x0fffu);
    uint32_t value = 0;
    if (!cortex_m4_data_read(cpu, address, size, CORTEX_M4_ACCESS_DATA, &value)) {
        return true;
    }
    if (operation == 0xf910u || operation == 0xf930u) {
        value = sign_extend(value, (uint8_t)(size * 8u));
    }
    if (target == 15u) {
        branch_to(cpu, value);
    } else {
        cpu->registers[target] = value;
    }
    return true;
}

bool cortex_m4_execute_remaining(CortexM4* cpu, uint16_t first, uint16_t second) {
    return execute_reverse(cpu, first, second) || execute_hint(cpu, first, second) ||
           execute_preload_hint(first, second) || execute_negative_literal(cpu, first, second);
}
