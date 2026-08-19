#include "cortex_m4_internal.h"

static uint32_t immediate12(uint16_t first, uint16_t second) {
    return ((uint32_t)((first >> 10) & 1u) << 11) | ((uint32_t)((second >> 12) & 7u) << 8) |
           (second & 0xffu);
}

static bool execute_wide_add_subtract(CortexM4* cpu, uint16_t first, uint16_t second) {
    const uint16_t operation = first & 0xfbf0u;
    if (operation != 0xf200u && operation != 0xf2a0u) {
        return false;
    }
    const uint8_t source = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint32_t left = source == 15u ? cpu->registers[15] & ~3u
                                        : cortex_m4_read_register_internal(cpu, source);
    const uint32_t immediate = immediate12(first, second);
    cortex_m4_write_register_internal(
        cpu, destination, operation == 0xf200u ? left + immediate : left - immediate);
    return true;
}

static bool execute_decrement_before_multiple(CortexM4* cpu, uint16_t first,
                                              uint16_t second) {
    if ((first & 0xffc0u) != 0xe900u || second == 0) {
        return false;
    }
    const bool load = (first & 0x0010u) != 0;
    const bool write_back = (first & 0x0020u) != 0;
    const uint8_t base = (uint8_t)(first & 15u);
    uint32_t count = 0;
    for (uint8_t index = 0; index < 16; index++) {
        count += (second >> index) & 1u;
    }
    const uint32_t write_back_value =
        cortex_m4_read_register_internal(cpu, base) - count * 4u;
    uint32_t address = cortex_m4_exception_advanced_multiple_address(cpu, write_back_value);
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
            if (!cortex_m4_data_read(cpu, address, 4, CORTEX_M4_ACCESS_DATA, &value)) {
                return true;
            }
            cortex_m4_write_register_internal(cpu, index, value);
        } else if (!cortex_m4_data_write(cpu, address, 4, CORTEX_M4_ACCESS_DATA,
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
        cortex_m4_write_register_internal(cpu, base, write_back_value);
    }
    cortex_m4_exception_advanced_multiple_complete(cpu);
    return true;
}

static bool execute_doubleword(CortexM4* cpu, uint16_t first, uint16_t second) {
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
    const uint32_t base_value = cortex_m4_read_register_internal(cpu, base);
    const uint32_t offset = (uint32_t)(second & 0xffu) * 4u;
    const uint32_t offset_address = add ? base_value + offset : base_value - offset;
    const uint32_t address = pre_index ? offset_address : base_value;
    if (!cortex_m4_require_alignment(cpu, address, 4)) {
        return true;
    }
    if (load) {
        uint32_t first_value = 0;
        uint32_t second_value = 0;
        if (!cortex_m4_data_read(cpu, address, 4, CORTEX_M4_ACCESS_DATA, &first_value) ||
            !cortex_m4_data_read(cpu, address + 4u, 4, CORTEX_M4_ACCESS_DATA,
                                 &second_value)) {
            return true;
        }
        cortex_m4_write_register_internal(cpu, first_target, first_value);
        cortex_m4_write_register_internal(cpu, second_target, second_value);
    } else if (!cortex_m4_data_write(cpu, address, 4, CORTEX_M4_ACCESS_DATA,
                                     cortex_m4_read_register_internal(cpu, first_target)) ||
               !cortex_m4_data_write(
                   cpu, address + 4u, 4, CORTEX_M4_ACCESS_DATA,
                   cortex_m4_read_register_internal(cpu, second_target))) {
        return true;
    }
    if (write_back) {
        cortex_m4_write_register_internal(cpu, base, offset_address);
    }
    return true;
}

static bool execute_register_offset(CortexM4* cpu, uint16_t first, uint16_t second) {
    const uint16_t operation = first & 0xfff0u;
    if ((second & 0x0fc0u) != 0 ||
        (operation != 0xf800u && operation != 0xf810u && operation != 0xf820u &&
         operation != 0xf830u && operation != 0xf840u && operation != 0xf850u &&
         operation != 0xf910u && operation != 0xf930u)) {
        return false;
    }
    const uint8_t base = (uint8_t)(first & 15u);
    const uint8_t target = (uint8_t)(second >> 12);
    const uint8_t index = (uint8_t)(second & 15u);
    const uint8_t shift = (uint8_t)((second >> 4) & 3u);
    const uint8_t size = (operation & 0x0040u) != 0   ? 4
                         : (operation & 0x0020u) != 0 ? 2
                                                      : 1;
    const uint32_t address = cortex_m4_read_register_internal(cpu, base) +
                             (cortex_m4_read_register_internal(cpu, index) << shift);
    const bool load = (operation & 0x0010u) != 0;
    if (!load) {
        return cortex_m4_data_write(cpu, address, size, CORTEX_M4_ACCESS_DATA,
                                    cortex_m4_read_register_internal(cpu, target));
    }
    uint32_t value = 0;
    if (!cortex_m4_data_read(cpu, address, size, CORTEX_M4_ACCESS_DATA, &value)) {
        return true;
    }
    if ((operation & 0x0100u) != 0) {
        const uint32_t sign = 1u << (size * 8u - 1u);
        value = (value ^ sign) - sign;
    }
    cortex_m4_write_register_internal(cpu, target, value);
    return true;
}

static bool execute_unprivileged(CortexM4* cpu, uint16_t first, uint16_t second) {
    const uint16_t operation = first & 0xfff0u;
    if ((second & 0x0f00u) != 0x0e00u ||
        (operation != 0xf800u && operation != 0xf810u && operation != 0xf820u &&
         operation != 0xf830u && operation != 0xf840u && operation != 0xf850u &&
         operation != 0xf910u && operation != 0xf930u)) {
        return false;
    }
    const uint8_t base = (uint8_t)(first & 15u);
    const uint8_t target = (uint8_t)(second >> 12);
    const uint8_t size = (operation & 0x0040u) != 0   ? 4
                         : (operation & 0x0020u) != 0 ? 2
                                                      : 1;
    const uint32_t address =
        cortex_m4_read_register_internal(cpu, base) + (uint32_t)(second & 0xffu);
    const bool load = (operation & 0x0010u) != 0;
    if (!load) {
        return cortex_m4_data_write(cpu, address, size, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA,
                                    cortex_m4_read_register_internal(cpu, target));
    }
    uint32_t value = 0;
    if (!cortex_m4_data_read(cpu, address, size, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA,
                             &value)) {
        return true;
    }
    if ((operation & 0x0100u) != 0) {
        const uint32_t sign = 1u << (size * 8u - 1u);
        value = (value ^ sign) - sign;
    }
    cortex_m4_write_register_internal(cpu, target, value);
    return true;
}

bool cortex_m4_execute_thumb32_extra(CortexM4* cpu, uint16_t first, uint16_t second) {
    return execute_wide_add_subtract(cpu, first, second) ||
           execute_decrement_before_multiple(cpu, first, second) ||
           execute_doubleword(cpu, first, second) ||
           execute_register_offset(cpu, first, second) ||
           execute_unprivileged(cpu, first, second);
}
