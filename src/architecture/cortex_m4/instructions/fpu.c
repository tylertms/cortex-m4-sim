#include "architecture/cortex_m4/internal.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>

_Static_assert(FLT_RADIX == 2, "The simulator requires binary floating point");
_Static_assert(sizeof(float) == 4, "The simulator requires 32-bit float");

enum {
    FPSCR_IOC = 1u << 0,
    FPSCR_DZC = 1u << 1,
    FPSCR_OFC = 1u << 2,
    FPSCR_UFC = 1u << 3,
    FPSCR_IXC = 1u << 4,
    FPSCR_IDC = 1u << 7,
    FPSCR_RMODE_SHIFT = 22,
    FPSCR_FZ = 1u << 24,
    FPSCR_DN = 1u << 25,
    FPSCR_AHP = 1u << 26,
    FPSCR_WRITABLE = 0xf7c09f9fu,
    FLOAT_SIGN = 1u << 31,
    FLOAT_EXPONENT = 0x7f800000u,
    FLOAT_FRACTION = 0x007fffffu,
    FLOAT_QUIET = 0x00400000u,
    DEFAULT_NAN = 0x7fc00000u,
};

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

static uint8_t double_destination(uint16_t first, uint16_t second) {
    return (uint8_t)(((second >> 12) & 15u) + ((first >> 6) & 1u) * 16u);
}

static uint8_t double_m(uint16_t second) {
    return (uint8_t)((second & 15u) + ((second >> 5) & 1u) * 16u);
}

static bool fpu_enabled(CortexM4* cpu) {
    if ((cpu->cpacr & 0x00f00000u) == 0x00f00000u) {
        return true;
    }
    cpu->cfsr |= 1u << 19;
    cortex_m4_raise_fault(cpu, 6);
    return false;
}

static bool float_is_nan(uint32_t bits) {
    return (bits & FLOAT_EXPONENT) == FLOAT_EXPONENT && (bits & FLOAT_FRACTION) != 0;
}

static bool float_is_signaling_nan(uint32_t bits) {
    return float_is_nan(bits) && (bits & FLOAT_QUIET) == 0;
}

static bool float_is_infinity(uint32_t bits) { return (bits & ~FLOAT_SIGN) == FLOAT_EXPONENT; }

static bool float_is_zero(uint32_t bits) { return (bits & ~FLOAT_SIGN) == 0; }

static bool float_is_subnormal(uint32_t bits) {
    return (bits & FLOAT_EXPONENT) == 0 && (bits & FLOAT_FRACTION) != 0;
}

static uint32_t quiet_nan(CortexM4* cpu, uint32_t bits) {
    if (float_is_signaling_nan(bits)) {
        cpu->fpscr |= FPSCR_IOC;
    }
    return (cpu->fpscr & FPSCR_DN) != 0 ? DEFAULT_NAN : bits | FLOAT_QUIET;
}

static uint32_t select_nan(CortexM4* cpu, uint32_t first, uint32_t second) {
    if (float_is_signaling_nan(first)) {
        return quiet_nan(cpu, first);
    }
    if (float_is_signaling_nan(second)) {
        return quiet_nan(cpu, second);
    }
    return float_is_nan(first) ? quiet_nan(cpu, first) : quiet_nan(cpu, second);
}

static uint32_t prepare_operand(CortexM4* cpu, uint32_t bits) {
    if ((cpu->fpscr & FPSCR_FZ) != 0 && float_is_subnormal(bits)) {
        cpu->fpscr |= FPSCR_IDC;
        return bits & FLOAT_SIGN;
    }
    return bits;
}

static uint32_t next_float(uint32_t bits, bool upward) {
    if (float_is_nan(bits)) {
        return bits;
    }
    if (bits == (FLOAT_SIGN | FLOAT_EXPONENT)) {
        return upward ? 0xff7fffffu : bits;
    }
    if (bits == FLOAT_EXPONENT) {
        return upward ? bits : 0x7f7fffffu;
    }
    if ((bits & ~FLOAT_SIGN) == 0) {
        return upward ? 1u : FLOAT_SIGN | 1u;
    }
    if ((bits & FLOAT_SIGN) != 0) {
        return upward ? bits - 1u : bits + 1u;
    }
    return upward ? bits + 1u : bits - 1u;
}

static uint32_t rounded_float(CortexM4* cpu, double exact) {
    float nearest = (float)exact;
    uint32_t bits = float_to_bits(nearest);
    const bool overflow = isfinite(exact) && fabs(exact) > FLT_MAX;
    const uint8_t rounding = (uint8_t)(cpu->fpscr >> FPSCR_RMODE_SHIFT) & 3u;
    const double nearest_value = (double)nearest;
    if (rounding == 1 && nearest_value < exact) {
        bits = next_float(bits, true);
    } else if (rounding == 2 && nearest_value > exact) {
        bits = next_float(bits, false);
    } else if (rounding == 3 &&
               ((exact > 0 && nearest_value > exact) || (exact < 0 && nearest_value < exact))) {
        bits = next_float(bits, exact < 0);
    }
    if (overflow) {
        cpu->fpscr |= FPSCR_OFC | FPSCR_IXC;
    } else if (isfinite(exact) && (double)bits_to_float(bits) != exact) {
        cpu->fpscr |= FPSCR_IXC;
    }
    if (exact != 0 && isfinite(exact) && fabs(exact) < FLT_MIN &&
        (double)bits_to_float(bits) != exact) {
        cpu->fpscr |= FPSCR_UFC;
    }
    if (float_is_subnormal(bits)) {
        if ((cpu->fpscr & FPSCR_FZ) != 0) {
            cpu->fpscr |= FPSCR_UFC | FPSCR_IXC;
            return bits & FLOAT_SIGN;
        }
        if ((double)bits_to_float(bits) != exact) {
            cpu->fpscr |= FPSCR_UFC;
        }
    }
    return bits;
}

static uint32_t expand_vfp_immediate(uint8_t immediate) {
    const uint32_t sign = (uint32_t)(immediate >> 7) << 31;
    const uint32_t exponent_head = (uint32_t)(~immediate >> 6 & 1u) << 30;
    const uint32_t exponent_body = (immediate & 0x40u) != 0 ? 0x3e000000u : 0;
    return sign | exponent_head | exponent_body | ((uint32_t)(immediate & 0x3fu) << 19);
}

static void set_compare_flags(CortexM4* cpu, uint32_t left_bits, uint32_t right_bits,
                              bool signaling) {
    left_bits = prepare_operand(cpu, left_bits);
    right_bits = prepare_operand(cpu, right_bits);
    cpu->fpscr &= 0x0fffffffu;
    if (float_is_nan(left_bits) || float_is_nan(right_bits)) {
        cpu->fpscr |= CORTEX_M4_XPSR_C | CORTEX_M4_XPSR_V;
        if (signaling || float_is_signaling_nan(left_bits) || float_is_signaling_nan(right_bits)) {
            cpu->fpscr |= FPSCR_IOC;
        }
        return;
    }
    const float left = bits_to_float(left_bits);
    const float right = bits_to_float(right_bits);
    if (left == right) {
        cpu->fpscr |= CORTEX_M4_XPSR_Z | CORTEX_M4_XPSR_C;
    } else if (left < right) {
        cpu->fpscr |= CORTEX_M4_XPSR_N;
    } else {
        cpu->fpscr |= CORTEX_M4_XPSR_C;
    }
}

static int64_t rounded_integer_value(CortexM4* cpu, double value, bool use_fpscr_rounding) {
    if (!use_fpscr_rounding) {
        return value < 0 ? (int64_t)ceil(value) : (int64_t)floor(value);
    }
    const uint8_t rounding = (uint8_t)(cpu->fpscr >> FPSCR_RMODE_SHIFT) & 3u;
    if (rounding == 1) {
        return (int64_t)ceil(value);
    }
    if (rounding == 2) {
        return (int64_t)floor(value);
    }
    if (rounding == 3) {
        return value < 0 ? (int64_t)ceil(value) : (int64_t)floor(value);
    }
    const double floor_value = floor(value);
    const double fraction = value - floor_value;
    if (fraction < 0.5) {
        return (int64_t)floor_value;
    }
    if (fraction > 0.5) {
        return (int64_t)(floor_value + 1.0);
    }
    const int64_t lower = (int64_t)floor_value;
    return (lower & 1) == 0 ? lower : lower + 1;
}

static uint32_t rounded_half_fraction(CortexM4* cpu, double value, bool negative) {
    const uint8_t rounding = (uint8_t)(cpu->fpscr >> FPSCR_RMODE_SHIFT) & 3u;
    if (rounding == 1) {
        return (uint32_t)(negative ? floor(value) : ceil(value));
    }
    if (rounding == 2) {
        return (uint32_t)(negative ? ceil(value) : floor(value));
    }
    if (rounding == 3) {
        return (uint32_t)floor(value);
    }
    const double lower_value = floor(value);
    const double fraction = value - lower_value;
    const uint32_t lower = (uint32_t)lower_value;
    if (fraction < 0.5) {
        return lower;
    }
    if (fraction > 0.5) {
        return lower + 1u;
    }
    return (lower & 1u) == 0 ? lower : lower + 1u;
}

static uint32_t convert_float_to_integer(CortexM4* cpu, uint32_t source, bool unsigned_result,
                                         bool round_by_fpscr, uint8_t width,
                                         uint8_t fractional_bits) {
    source = prepare_operand(cpu, source);
    if (float_is_nan(source)) {
        cpu->fpscr |= FPSCR_IOC;
        return unsigned_result ? 0 : 1u << (width - 1u);
    }
    const double scale = ldexp(1.0, fractional_bits);
    const double scaled = (double)bits_to_float(source) * scale;
    const double unsigned_max = width == 32 ? 4294967295.0 : 65535.0;
    const double signed_min = width == 32 ? -2147483648.0 : -32768.0;
    const double signed_max = width == 32 ? 2147483647.0 : 32767.0;
    if (!isfinite(scaled) || (unsigned_result && (scaled < 0 || scaled > unsigned_max)) ||
        (!unsigned_result && (scaled < signed_min || scaled > signed_max))) {
        cpu->fpscr |= FPSCR_IOC;
        if (unsigned_result) {
            return scaled < 0 || isnan(scaled) ? 0 : (uint32_t)unsigned_max;
        }
        return scaled < 0 || isnan(scaled) ? 1u << (width - 1u) : (uint32_t)signed_max;
    }
    const int64_t result = rounded_integer_value(cpu, scaled, round_by_fpscr);
    if ((double)result != scaled) {
        cpu->fpscr |= FPSCR_IXC;
    }
    if (width == 16) {
        return unsigned_result ? (uint16_t)result : (uint32_t)(int32_t)(int16_t)result;
    }
    return (uint32_t)result;
}

static uint32_t convert_integer_to_float(CortexM4* cpu, uint32_t source, bool unsigned_source,
                                         uint8_t width, uint8_t fractional_bits) {
    double value = 0;
    if (width == 16) {
        value = unsigned_source ? (double)(uint16_t)source : (double)(int16_t)source;
    } else {
        value = unsigned_source ? (double)source : (double)(int32_t)source;
    }
    return rounded_float(cpu, ldexp(value, -(int)fractional_bits));
}

static uint32_t half_to_float(CortexM4* cpu, uint16_t half) {
    const uint32_t sign = (uint32_t)(half & 0x8000u) << 16;
    const uint32_t exponent = (half >> 10) & 31u;
    uint32_t fraction = half & 0x03ffu;
    if (exponent == 0) {
        if (fraction == 0) {
            return sign;
        }
        if ((cpu->fpscr & FPSCR_FZ) != 0) {
            cpu->fpscr |= FPSCR_IDC;
            return sign;
        }
        int shift = 0;
        while ((fraction & 0x0400u) == 0) {
            fraction <<= 1;
            shift++;
        }
        fraction &= 0x03ffu;
        return sign | (uint32_t)(113 - shift) << 23 | fraction << 13;
    }
    if (exponent == 31 && (cpu->fpscr & FPSCR_AHP) == 0) {
        if (fraction == 0) {
            return sign | FLOAT_EXPONENT;
        }
        const uint32_t nan = sign | FLOAT_EXPONENT | fraction << 13;
        return quiet_nan(cpu, nan);
    }
    return sign | (exponent + 112u) << 23 | fraction << 13;
}

static uint16_t float_to_half(CortexM4* cpu, uint32_t bits) {
    bits = prepare_operand(cpu, bits);
    const uint16_t sign = (uint16_t)(bits >> 16) & 0x8000u;
    if (float_is_nan(bits)) {
        if ((cpu->fpscr & FPSCR_AHP) != 0) {
            cpu->fpscr |= FPSCR_IOC;
            return sign | 0x7fffu;
        }
        const uint32_t nan = quiet_nan(cpu, bits);
        return sign | 0x7c00u | (uint16_t)((nan >> 13) & 0x03ffu);
    }
    if (float_is_infinity(bits)) {
        if ((cpu->fpscr & FPSCR_AHP) != 0) {
            cpu->fpscr |= FPSCR_IOC;
            return sign | 0x7fffu;
        }
        return sign | 0x7c00u;
    }
    const double magnitude = fabs((double)bits_to_float(bits));
    const double maximum = (cpu->fpscr & FPSCR_AHP) != 0 ? 131008.0 : 65504.0;
    if (magnitude > maximum) {
        cpu->fpscr |= FPSCR_OFC | FPSCR_IXC;
        return sign | ((cpu->fpscr & FPSCR_AHP) != 0 ? 0x7fffu : 0x7c00u);
    }
    if (magnitude == 0) {
        return sign;
    }
    int exponent = 0;
    const double normalized = frexp(magnitude, &exponent);
    int half_exponent = exponent + 14;
    double scaled = 0;
    if (half_exponent <= 0) {
        scaled = ldexp(magnitude, 24);
        half_exponent = 0;
    } else {
        scaled = (normalized * 2.0 - 1.0) * 1024.0;
    }
    uint32_t fraction = rounded_half_fraction(cpu, scaled, sign != 0);
    if (fraction >= 1024u && half_exponent != 0) {
        fraction = 0;
        half_exponent++;
    }
    if ((double)fraction != scaled) {
        cpu->fpscr |= FPSCR_IXC;
        if (half_exponent == 0) {
            cpu->fpscr |= FPSCR_UFC;
        }
    }
    if (half_exponent == 0 && (cpu->fpscr & FPSCR_FZ) != 0) {
        cpu->fpscr |= FPSCR_UFC | FPSCR_IXC;
        return sign;
    }
    return sign | (uint16_t)((uint32_t)half_exponent << 10) | (uint16_t)(fraction & 0x03ffu);
}

static uint32_t binary_result(CortexM4* cpu, uint32_t left, uint32_t right, uint8_t operation) {
    left = prepare_operand(cpu, left);
    right = prepare_operand(cpu, right);
    if (float_is_nan(left) || float_is_nan(right)) {
        return select_nan(cpu, left, right);
    }
    const bool multiply = operation == 0 || operation == 1;
    const bool divide = operation == 4;
    const bool subtract = operation == 3;
    if ((multiply && ((float_is_infinity(left) && float_is_zero(right)) ||
                      (float_is_zero(left) && float_is_infinity(right)))) ||
        (divide && ((float_is_zero(left) && float_is_zero(right)) ||
                    (float_is_infinity(left) && float_is_infinity(right)))) ||
        (!multiply && !divide && float_is_infinity(left) && float_is_infinity(right) &&
         ((((left ^ right) & FLOAT_SIGN) != 0) != subtract))) {
        cpu->fpscr |= FPSCR_IOC;
        return DEFAULT_NAN;
    }
    if (divide && float_is_zero(right) && !float_is_zero(left)) {
        cpu->fpscr |= FPSCR_DZC;
        return ((left ^ right) & FLOAT_SIGN) | FLOAT_EXPONENT;
    }
    const double left_value = bits_to_float(left);
    const double right_value = bits_to_float(right);
    if (operation == 0) {
        return rounded_float(cpu, left_value * right_value);
    }
    if (operation == 1) {
        return rounded_float(cpu, -(left_value * right_value));
    }
    if (operation == 2) {
        return rounded_float(cpu, left_value + right_value);
    }
    if (operation == 3) {
        return rounded_float(cpu, left_value - right_value);
    }
    return rounded_float(cpu, left_value / right_value);
}

static uint32_t multiply_accumulate(CortexM4* cpu, uint32_t accumulator, uint32_t left,
                                    uint32_t right, bool fused, bool negate_accumulator,
                                    bool negate_product) {
    accumulator = prepare_operand(cpu, accumulator);
    left = prepare_operand(cpu, left);
    right = prepare_operand(cpu, right);
    if (float_is_nan(accumulator) || float_is_nan(left) || float_is_nan(right)) {
        uint32_t product_nan =
            float_is_nan(left) || float_is_nan(right) ? select_nan(cpu, left, right) : accumulator;
        return float_is_nan(accumulator) ? select_nan(cpu, product_nan, accumulator) : product_nan;
    }
    if ((float_is_infinity(left) && float_is_zero(right)) ||
        (float_is_zero(left) && float_is_infinity(right))) {
        cpu->fpscr |= FPSCR_IOC;
        return DEFAULT_NAN;
    }
    const double accumulator_value = bits_to_float(accumulator);
    const double left_value = bits_to_float(left);
    const double right_value = bits_to_float(right);
    const double signed_accumulator = negate_accumulator ? -accumulator_value : accumulator_value;
    const double signed_left = negate_product ? -left_value : left_value;
    if (isinf(signed_left * right_value) && isinf(signed_accumulator) &&
        signbit(signed_left * right_value) != signbit(signed_accumulator)) {
        cpu->fpscr |= FPSCR_IOC;
        return DEFAULT_NAN;
    }
    if (!fused) {
        const uint32_t product = rounded_float(cpu, signed_left * right_value);
        return binary_result(cpu, float_to_bits((float)signed_accumulator), product, 2);
    }
    return rounded_float(cpu, fma(signed_left, right_value, signed_accumulator));
}

static bool execute_scalar_memory(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 0xff00u) != 0xed00u || (first & 0x0120u) != 0x0100u ||
        (second & 0x0e00u) != 0x0a00u) {
        return false;
    }
    const bool load = (first & 0x0010u) != 0;
    const bool add = (first & 0x0080u) != 0;
    const bool double_register = (second & 0x0100u) != 0;
    const uint8_t base = (uint8_t)(first & 15u);
    const uint8_t target = double_register ? (uint8_t)(double_destination(first, second) * 2u)
                                           : single_destination(first, second);
    const uint8_t words = double_register ? 2 : 1;
    const uint32_t base_value =
        base == 15 ? cpu->registers[15] & ~3u : cortex_m4_read_register_internal(cpu, base);
    const uint32_t offset = (second & 0xffu) * 4u;
    const uint32_t address = add ? base_value + offset : base_value - offset;
    for (uint8_t index = 0; index < words; index++) {
        uint32_t value = 0;
        const uint32_t word_address = address + index * 4u;
        const bool success =
            load ? cortex_m4_data_read(cpu, word_address, 4, CORTEX_M4_ACCESS_DATA, &value)
                 : cortex_m4_data_write(cpu, word_address, 4, CORTEX_M4_ACCESS_DATA,
                                        cpu->fp_registers[target + index]);
        if (!success) {
            return true;
        }
        if (load) {
            cpu->fp_registers[target + index] = value;
        }
    }
    return true;
}

static bool execute_multiple_memory(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 0xfe00u) != 0xec00u || (second & 0x0e00u) != 0x0a00u) {
        return false;
    }
    const bool add = (first & 0x0080u) != 0;
    const bool write_back = (first & 0x0020u) != 0;
    const bool load = (first & 0x0010u) != 0;
    const uint8_t base = (uint8_t)(first & 15u);
    const bool double_registers = (second & 0x0100u) != 0;
    const uint8_t start = double_registers ? (uint8_t)(double_destination(first, second) * 2u)
                                           : single_destination(first, second);
    const uint8_t count = (uint8_t)(second & 0xffu);
    const uint32_t base_value = cortex_m4_read_register_internal(cpu, base);
    uint32_t address = add ? base_value : base_value - (uint32_t)count * 4u;
    for (uint8_t index = 0; index < count; index++, address += 4u) {
        uint32_t value = 0;
        const bool success =
            load ? cortex_m4_data_read(cpu, address, 4, CORTEX_M4_ACCESS_DATA, &value)
                 : cortex_m4_data_write(cpu, address, 4, CORTEX_M4_ACCESS_DATA,
                                        cpu->fp_registers[start + index]);
        if (!success) {
            return true;
        }
        if (load) {
            cpu->fp_registers[start + index] = value;
        }
    }
    if (write_back) {
        cortex_m4_write_register_internal(
            cpu, base, add ? base_value + (uint32_t)count * 4u : base_value - (uint32_t)count * 4u);
    }
    return true;
}

static bool execute_core_transfers(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 0xffe0u) == 0xee00u && (second & 0x0f7fu) == 0x0a10u) {
        const uint8_t core = (uint8_t)(second >> 12);
        const uint8_t single = single_n(first, second);
        if ((first & 0x0010u) != 0) {
            cortex_m4_write_register_internal(cpu, core, cpu->fp_registers[single]);
        } else {
            cpu->fp_registers[single] = cortex_m4_read_register_internal(cpu, core);
        }
        return true;
    }
    if ((first & 0xffe0u) == 0xec40u && (second & 0x0ed0u) == 0x0a10u) {
        const uint8_t first_core = (uint8_t)(second >> 12);
        const uint8_t second_core = (uint8_t)(first & 15u);
        const bool double_register = (second & 0x0100u) != 0;
        const uint8_t single =
            double_register ? (uint8_t)(double_m(second) * 2u) : single_m(second);
        if ((first & 0x0010u) != 0) {
            cortex_m4_write_register_internal(cpu, first_core, cpu->fp_registers[single]);
            cortex_m4_write_register_internal(cpu, second_core, cpu->fp_registers[single + 1u]);
        } else {
            cpu->fp_registers[single] = cortex_m4_read_register_internal(cpu, first_core);
            cpu->fp_registers[single + 1u] = cortex_m4_read_register_internal(cpu, second_core);
        }
        return true;
    }
    if ((first == 0xeef1u || first == 0xeee1u) && (second & 0x0fffu) == 0x0a10u) {
        const uint8_t core = (uint8_t)(second >> 12);
        if (first == 0xeef1u) {
            if (core == 15) {
                cpu->xpsr = (cpu->xpsr & 0x0fffffffu) | (cpu->fpscr & 0xf0000000u);
            } else {
                cortex_m4_write_register_internal(cpu, core, cpu->fpscr);
            }
        } else if (core != 15) {
            cpu->fpscr = cortex_m4_read_register_internal(cpu, core) & FPSCR_WRITABLE;
        }
        return true;
    }
    return false;
}

static bool execute_unary(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 0xffbeu) != 0xeeb0u || (second & 0x0f50u) != 0x0a40u) {
        return false;
    }
    const uint8_t operation = (uint8_t)(((first & 1u) << 1) | ((second >> 7) & 1u));
    const uint8_t destination = single_destination(first, second);
    uint32_t source = cpu->fp_registers[single_m(second)];
    if (operation == 0) {
        cpu->fp_registers[destination] = source;
    } else if (operation == 1) {
        cpu->fp_registers[destination] =
            float_is_nan(source) ? quiet_nan(cpu, source) : source & ~FLOAT_SIGN;
    } else if (operation == 2) {
        cpu->fp_registers[destination] =
            float_is_nan(source) ? quiet_nan(cpu, source) ^ FLOAT_SIGN : source ^ FLOAT_SIGN;
    } else {
        source = prepare_operand(cpu, source);
        if (float_is_nan(source)) {
            cpu->fp_registers[destination] = quiet_nan(cpu, source);
        } else if ((source & FLOAT_SIGN) != 0 && !float_is_zero(source)) {
            cpu->fpscr |= FPSCR_IOC;
            cpu->fp_registers[destination] = DEFAULT_NAN;
        } else {
            cpu->fp_registers[destination] =
                rounded_float(cpu, sqrt((double)bits_to_float(source)));
        }
    }
    return true;
}

static bool execute_arithmetic(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((second & 0x0e10u) != 0x0a00u) {
        return false;
    }
    const uint16_t operation = first & 0xffb0u;
    const uint8_t destination = single_destination(first, second);
    const uint32_t left = cpu->fp_registers[single_n(first, second)];
    const uint32_t right = cpu->fp_registers[single_m(second)];
    const bool alternate = (second & 0x0040u) != 0;
    if (operation == 0xee20u) {
        cpu->fp_registers[destination] = binary_result(cpu, left, right, alternate ? 1 : 0);
    } else if (operation == 0xee30u) {
        cpu->fp_registers[destination] = binary_result(cpu, left, right, alternate ? 3 : 2);
    } else if (operation == 0xee80u) {
        cpu->fp_registers[destination] = binary_result(cpu, left, right, 4);
    } else if (operation == 0xee00u || operation == 0xee10u || operation == 0xee90u ||
               operation == 0xeea0u) {
        const bool fused = operation == 0xee90u || operation == 0xeea0u;
        const bool negate_accumulator = operation == 0xee10u || operation == 0xee90u;
        bool negate_product = alternate;
        if (operation == 0xee10u || operation == 0xee90u) {
            negate_product = !alternate;
        }
        cpu->fp_registers[destination] =
            multiply_accumulate(cpu, cpu->fp_registers[destination], left, right, fused,
                                negate_accumulator, negate_product);
    } else {
        return false;
    }
    return true;
}

static bool execute_conversions(CortexM4* cpu, uint16_t first, uint16_t second) {
    const uint8_t destination = single_destination(first, second);
    const uint8_t source = single_m(second);
    if ((first & 0xffbeu) == 0xeeb2u && (second & 0x0f50u) == 0x0a40u) {
        const bool to_half = (first & 1u) != 0;
        const bool top = (second & 0x0080u) != 0;
        if (to_half) {
            const uint16_t half = float_to_half(cpu, cpu->fp_registers[source]);
            const uint32_t mask = top ? 0x0000ffffu : 0xffff0000u;
            cpu->fp_registers[destination] =
                (cpu->fp_registers[destination] & mask) | (top ? (uint32_t)half << 16 : half);
        } else {
            const uint16_t half = (uint16_t)(cpu->fp_registers[source] >> (top ? 16u : 0u));
            cpu->fp_registers[destination] = half_to_float(cpu, half);
        }
        return true;
    }
    if ((first & 0xffbfu) == 0xeeb8u && (second & 0x0f10u) == 0x0a00u) {
        cpu->fp_registers[destination] = convert_integer_to_float(cpu, cpu->fp_registers[source],
                                                                  (second & 0x0080u) == 0, 32, 0);
        return true;
    }
    if ((first & 0xffbeu) == 0xeebcu && (second & 0x0f10u) == 0x0a00u) {
        cpu->fp_registers[destination] = convert_float_to_integer(
            cpu, cpu->fp_registers[source], (first & 1u) == 0, (second & 0x0080u) == 0, 32, 0);
        return true;
    }
    const uint16_t fixed_operation = first & 0xffbeu;
    if ((fixed_operation == 0xeebau || fixed_operation == 0xeebeu) &&
        (second & 0x0f10u) == 0x0a00u) {
        const bool to_integer = fixed_operation == 0xeebeu;
        const bool unsigned_value = (first & 1u) != 0;
        const uint8_t width = (second & 0x0080u) != 0 ? 32 : 16;
        const uint8_t encoded = (uint8_t)(((second & 15u) << 1) | ((second >> 5) & 1u));
        const uint8_t fractional_bits = (uint8_t)(width - encoded);
        cpu->fp_registers[destination] =
            to_integer ? convert_float_to_integer(cpu, cpu->fp_registers[destination],
                                                  unsigned_value, false, width, fractional_bits)
                       : convert_integer_to_float(cpu, cpu->fp_registers[destination],
                                                  unsigned_value, width, fractional_bits);
        return true;
    }
    return false;
}

static bool valid_fpu_encoding(uint16_t first, uint16_t second) {
    if ((first & 0xffe0u) == 0xee00u && (second & 0x0f7fu) == 0x0a10u) {
        return true;
    }
    if ((first & 0xffe0u) == 0xec40u && (second & 0x0ed0u) == 0x0a10u) {
        return (second & 0x0100u) != 0 ? double_m(second) < 16 : single_m(second) < 31;
    }
    if ((first == 0xeef1u || first == 0xeee1u) && (second & 0x0fffu) == 0x0a10u) {
        return true;
    }
    if ((first & 0xff00u) == 0xed00u && (first & 0x0120u) == 0x0100u &&
        (second & 0x0e00u) == 0x0a00u) {
        return (second & 0x0100u) == 0 || double_destination(first, second) < 16;
    }
    if ((first & 0xfe00u) == 0xec00u && (second & 0x0e00u) == 0x0a00u) {
        const bool double_registers = (second & 0x0100u) != 0;
        const uint8_t start = double_registers ? (uint8_t)(double_destination(first, second) * 2u)
                                               : single_destination(first, second);
        const uint8_t count = (uint8_t)second;
        return count != 0 && (!double_registers || (count & 1u) == 0) &&
               start + count <= CORTEX_M4_FP_REGISTER_COUNT;
    }
    if ((first & 0xffbeu) == 0xeeb0u && (second & 0x0f50u) == 0x0a40u) {
        return true;
    }
    if ((second & 0x0e10u) == 0x0a00u) {
        const uint16_t operation = first & 0xffb0u;
        if (operation == 0xee00u || operation == 0xee10u || operation == 0xee20u ||
            operation == 0xee30u || operation == 0xee80u || operation == 0xee90u ||
            operation == 0xeea0u) {
            return true;
        }
    }
    if ((first & 0xffbeu) == 0xeeb2u && (second & 0x0f50u) == 0x0a40u) {
        return true;
    }
    if ((first & 0xffbfu) == 0xeeb8u && (second & 0x0f10u) == 0x0a00u) {
        return true;
    }
    if ((first & 0xffbeu) == 0xeebcu && (second & 0x0f10u) == 0x0a00u) {
        return true;
    }
    const uint16_t fixed_operation = first & 0xffbeu;
    if ((fixed_operation == 0xeebau || fixed_operation == 0xeebeu) &&
        (second & 0x0f10u) == 0x0a00u) {
        return true;
    }
    if ((first & 0xffbeu) == 0xeeb4u && (((first & 1u) == 0 && (second & 0x0f10u) == 0x0a00u) ||
                                         ((first & 1u) != 0 && (second & 0x0f7fu) == 0x0a40u))) {
        return true;
    }
    return (first & 0xffb0u) == 0xeeb0u && (second & 0x0ff0u) == 0x0a00u;
}

bool cortex_m4_execute_fpu(CortexM4* cpu, uint16_t first, uint16_t second) {
    if (!valid_fpu_encoding(first, second)) {
        return false;
    }
    if (!fpu_enabled(cpu)) {
        return true;
    }
    if (!cortex_m4_system_materialize_lazy_fp(cpu)) {
        cortex_m4_exception_advanced_fault(
            cpu, CORTEX_M4_FAULT_LAZY_FP,
            !cortex_m4_mpu_access_permitted(cpu, cpu->fpcar, 4, CORTEX_M4_ACCESS_DATA, true));
        return true;
    }
    if (execute_core_transfers(cpu, first, second) || execute_scalar_memory(cpu, first, second) ||
        execute_multiple_memory(cpu, first, second) || execute_unary(cpu, first, second) ||
        execute_arithmetic(cpu, first, second) || execute_conversions(cpu, first, second)) {
        return true;
    }
    if ((first & 0xffbeu) == 0xeeb4u && (first & 1u) == 0 && (second & 0x0f10u) == 0x0a00u) {
        set_compare_flags(cpu, cpu->fp_registers[single_destination(first, second)],
                          cpu->fp_registers[single_m(second)], (second & 0x0080u) != 0);
        return true;
    }
    if ((first & 0xffbeu) == 0xeeb4u && (first & 1u) != 0 && (second & 0x0f7fu) == 0x0a40u) {
        set_compare_flags(cpu, cpu->fp_registers[single_destination(first, second)], 0,
                          (second & 0x0080u) != 0);
        return true;
    }
    if ((first & 0xffb0u) == 0xeeb0u && (second & 0x0ff0u) == 0x0a00u) {
        const uint8_t immediate = (uint8_t)((first & 15u) << 4) | (uint8_t)(second & 15u);
        cpu->fp_registers[single_destination(first, second)] = expand_vfp_immediate(immediate);
        return true;
    }
    return false;
}
