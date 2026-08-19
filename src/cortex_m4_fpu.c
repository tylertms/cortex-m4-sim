#include "cortex_m4_internal.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>

_Static_assert(FLT_RADIX == 2, "The simulator requires binary floating point");
_Static_assert(sizeof(float) == 4, "The simulator requires 32-bit float");

static float bits_to_float(uint32_t bits) {
    float value = 0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t float_to_bits(float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint8_t single_destination(uint16_t first, uint16_t second) {
    return (uint8_t)(((second >> 12) & 15u) * 2u + ((first >> 6) & 1u));
}

static uint8_t single_n(uint16_t first, uint16_t second) {
    return (uint8_t)((first & 15u) * 2u + ((second >> 7) & 1u));
}

static uint8_t single_m(uint16_t second) {
    return (uint8_t)((second & 15u) * 2u + ((second >> 5) & 1u));
}

static bool fpu_enabled(CortexM4* cpu) {
    if ((cpu->cpacr & 0x00f00000u) == 0x00f00000u) {
        return true;
    }
    cpu->cfsr |= 1u << 19;
    cpu->stop = CORTEX_M4_STOP_USAGE_FAULT;
    return false;
}

static uint32_t expand_vfp_immediate(uint8_t immediate) {
    const uint32_t sign = (uint32_t)(immediate >> 7) << 31;
    const uint32_t exponent_head = (uint32_t)(~immediate >> 6 & 1u) << 30;
    const uint32_t exponent_body = (immediate & 0x40u) != 0 ? 0x3e000000u : 0;
    return sign | exponent_head | exponent_body | ((uint32_t)(immediate & 0x3fu) << 19);
}

static void set_compare_flags(CortexM4* cpu, float left, float right) {
    cpu->fpscr &= 0x0fffffffu;
    if (isnan(left) || isnan(right)) {
        cpu->fpscr |= CORTEX_M4_XPSR_C | CORTEX_M4_XPSR_V;
    } else if (left == right) {
        cpu->fpscr |= CORTEX_M4_XPSR_Z | CORTEX_M4_XPSR_C;
    } else if (left < right) {
        cpu->fpscr |= CORTEX_M4_XPSR_N;
    } else {
        cpu->fpscr |= CORTEX_M4_XPSR_C;
    }
}

static uint32_t convert_float_to_integer(CortexM4* cpu, float value, bool unsigned_result) {
    if (isnan(value)) {
        cpu->fpscr |= 1u;
        return unsigned_result ? 0 : 0x80000000u;
    }
    if (unsigned_result) {
        if (value <= 0.0f) {
            if (value < 0.0f)
                cpu->fpscr |= 1u;
            return 0;
        }
        if (value >= 4294967295.0f) {
            cpu->fpscr |= 1u;
            return UINT32_MAX;
        }
        return (uint32_t)value;
    }
    if (value <= (float)INT32_MIN) {
        if (value < (float)INT32_MIN)
            cpu->fpscr |= 1u;
        return 0x80000000u;
    }
    if (value >= (float)INT32_MAX) {
        cpu->fpscr |= 1u;
        return INT32_MAX;
    }
    return (uint32_t)(int32_t)value;
}

bool cortex_m4_execute_fpu(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 0xff00u) == 0xed00u && (second & 0x0f00u) == 0x0a00u) {
        if (!fpu_enabled(cpu))
            return true;
        const bool load = (first & 0x0010u) != 0;
        const bool add = (first & 0x0080u) != 0;
        const uint8_t base = (uint8_t)(first & 15u);
        const uint8_t target = single_destination(first, second);
        const uint32_t base_value = base == 15
                                        ? cpu->registers[15] & ~3u
                                        : cortex_m4_read_register_internal(cpu, base);
        const uint32_t offset = (second & 0xffu) * 4u;
        const uint32_t address = add ? base_value + offset : base_value - offset;
        uint32_t value = 0;
        if (load) {
            if (!cortex_m4_bus_read(cpu, address, 4, CORTEX_M4_ACCESS_DATA, &value)) {
                cpu->bfar = address;
                cpu->cfsr |= 1u << 9;
                cpu->stop = CORTEX_M4_STOP_BUS_FAULT;
            } else {
                cpu->fp_registers[target] = value;
            }
        } else if (!cortex_m4_bus_write(cpu, address, 4, CORTEX_M4_ACCESS_DATA,
                                        cpu->fp_registers[target])) {
            cpu->bfar = address;
            cpu->cfsr |= 1u << 9;
            cpu->stop = CORTEX_M4_STOP_BUS_FAULT;
        }
        return true;
    }
    if ((first & 0xffe0u) == 0xee00u && (second & 0x0f7fu) == 0x0a10u) {
        if (!fpu_enabled(cpu))
            return true;
        const uint8_t core_register = (uint8_t)(second >> 12);
        const uint8_t single_register =
            (uint8_t)((first & 15u) * 2u + ((second >> 7) & 1u));
        if ((first & 0x0010u) != 0) {
            cortex_m4_write_register_internal(cpu, core_register,
                                              cpu->fp_registers[single_register]);
        } else {
            cpu->fp_registers[single_register] =
                cortex_m4_read_register_internal(cpu, core_register);
        }
        return true;
    }
    if ((first & 0xffbfu) == 0xeeb0u && (second & 0x0f50u) == 0x0a40u) {
        if (!fpu_enabled(cpu))
            return true;
        cpu->fp_registers[single_destination(first, second)] =
            cpu->fp_registers[single_m(second)];
        return true;
    }
    if ((first & 0xffb0u) == 0xeeb0u && (second & 0x0ff0u) == 0x0a00u) {
        if (!fpu_enabled(cpu))
            return true;
        const uint8_t immediate = (uint8_t)((first & 15u) << 4) | (uint8_t)(second & 15u);
        cpu->fp_registers[single_destination(first, second)] =
            expand_vfp_immediate(immediate);
        return true;
    }
    const uint16_t operation = first & 0xffb0u;
    if ((operation == 0xee20u || operation == 0xee30u || operation == 0xee80u) &&
        (second & 0x0e10u) == 0x0a00u) {
        if (!fpu_enabled(cpu))
            return true;
        const uint8_t destination = single_destination(first, second);
        const float left = bits_to_float(cpu->fp_registers[single_n(first, second)]);
        const float right = bits_to_float(cpu->fp_registers[single_m(second)]);
        volatile float result = 0;
        if (operation == 0xee20u) {
            result = left * right;
        } else if (operation == 0xee80u) {
            result = left / right;
        } else if ((second & 0x0040u) != 0) {
            result = left - right;
        } else {
            result = left + right;
        }
        cpu->fp_registers[destination] = float_to_bits(result);
        return true;
    }
    if ((first & 0xffbfu) == 0xeeb8u && (second & 0x0f10u) == 0x0a00u) {
        if (!fpu_enabled(cpu))
            return true;
        const uint8_t destination = single_destination(first, second);
        const uint8_t source = single_m(second);
        const bool signed_source = (second & 0x0080u) != 0;
        const float result = signed_source ? (float)(int32_t)cpu->fp_registers[source]
                                           : (float)cpu->fp_registers[source];
        cpu->fp_registers[destination] = float_to_bits(result);
        return true;
    }
    if ((first & 0xffbfu) == 0xeebdu && (second & 0x0f10u) == 0x0a00u) {
        if (!fpu_enabled(cpu))
            return true;
        const uint8_t destination = single_destination(first, second);
        const uint8_t source = single_m(second);
        const bool unsigned_result = (second & 0x0080u) == 0;
        cpu->fp_registers[destination] = convert_float_to_integer(
            cpu, bits_to_float(cpu->fp_registers[source]), unsigned_result);
        return true;
    }
    if ((first & 0xffbfu) == 0xeeb4u && (second & 0x0f10u) == 0x0a00u) {
        if (!fpu_enabled(cpu))
            return true;
        set_compare_flags(
            cpu, bits_to_float(cpu->fp_registers[single_destination(first, second)]),
            bits_to_float(cpu->fp_registers[single_m(second)]));
        return true;
    }
    if (first == 0xeef1u && (second & 0x0fffu) == 0x0a10u) {
        if (!fpu_enabled(cpu))
            return true;
        const uint8_t target = (uint8_t)(second >> 12);
        if (target == 15) {
            cpu->xpsr = (cpu->xpsr & 0x0fffffffu) | (cpu->fpscr & 0xf0000000u);
        } else {
            cortex_m4_write_register_internal(cpu, target, cpu->fpscr);
        }
        return true;
    }
    return false;
}
