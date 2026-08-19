#include "cortex_m4_internal.h"

#include <limits.h>
#include <stdint.h>

enum { XPSR_GE_SHIFT = 16, XPSR_GE_MASK = 15u << XPSR_GE_SHIFT };

static bool valid_register(uint8_t index) { return index != 13u && index != 15u; }

static uint32_t rotate_right(uint32_t value, uint8_t amount) {
    return amount == 0 ? value : (value >> amount) | (value << (32u - amount));
}

static int64_t floor_divide(int64_t value, int64_t divisor) {
    const int64_t quotient = value / divisor;
    return value < 0 && value % divisor != 0 ? quotient - 1 : quotient;
}

static uint32_t signed_saturate(int64_t value, uint8_t width, bool* saturated) {
    const int64_t minimum = -(INT64_C(1) << (width - 1u));
    const int64_t maximum = (INT64_C(1) << (width - 1u)) - 1;
    if (value < minimum) {
        *saturated = true;
        return (uint32_t)minimum;
    }
    if (value > maximum) {
        *saturated = true;
        return (uint32_t)maximum;
    }
    return (uint32_t)value;
}

static uint32_t unsigned_saturate(int64_t value, uint8_t width, bool* saturated) {
    const uint64_t maximum = width == 32 ? UINT32_MAX : (UINT64_C(1) << width) - 1u;
    if (value < 0) {
        *saturated = true;
        return 0;
    }
    if ((uint64_t)value > maximum) {
        *saturated = true;
        return (uint32_t)maximum;
    }
    return (uint32_t)value;
}

static int32_t signed_lane(uint32_t value, uint8_t lane, uint8_t width) {
    return width == 8 ? (int8_t)(value >> (lane * 8u)) : (int16_t)(value >> (lane * 16u));
}

static uint32_t unsigned_lane(uint32_t value, uint8_t lane, uint8_t width) {
    return (value >> (lane * width)) & ((1u << width) - 1u);
}

static uint32_t pack_lane(uint32_t result, uint32_t value, uint8_t lane, uint8_t width) {
    const uint32_t mask = (1u << width) - 1u;
    return result | ((value & mask) << (lane * width));
}

static void set_ge(CortexM4* cpu, uint8_t ge) {
    cpu->xpsr = (cpu->xpsr & ~XPSR_GE_MASK) | ((uint32_t)ge << XPSR_GE_SHIFT);
}

static bool execute_pack(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 0xfff0u) != 0xeac0u || (second & 0x8010u) != 0) {
        return false;
    }
    const uint8_t left_register = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t right_register = (uint8_t)(second & 15u);
    if (!valid_register(left_register) || !valid_register(destination) ||
        !valid_register(right_register)) {
        return false;
    }
    const uint8_t amount = (uint8_t)((((second >> 12) & 7u) << 2) | ((second >> 6) & 3u));
    const uint32_t left = cpu->registers[left_register];
    const uint32_t right = cpu->registers[right_register];
    if ((second & 0x20u) == 0) {
        cpu->registers[destination] = (left & 0xffffu) | ((right << amount) & 0xffff0000u);
    } else {
        const uint32_t shifted = cortex_m4_shift(right, 2, amount, false, NULL);
        cpu->registers[destination] = (left & 0xffff0000u) | (shifted & 0xffffu);
    }
    return true;
}

static bool execute_extend(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 0xff80u) != 0xfa00u || (second & 0xf080u) != 0xf080u) {
        return false;
    }
    const uint8_t operation = (uint8_t)((first >> 4) & 7u);
    const uint8_t accumulator = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t source = (uint8_t)(second & 15u);
    if (operation > 5u || (!valid_register(accumulator) && accumulator != 15u) ||
        !valid_register(destination) || !valid_register(source)) {
        return false;
    }
    const uint32_t rotated =
        rotate_right(cpu->registers[source], (uint8_t)(((second >> 4) & 3u) * 8u));
    const uint32_t base = accumulator == 15u ? 0 : cpu->registers[accumulator];
    uint32_t result = 0;
    if (operation == 0u || operation == 1u) {
        const uint32_t extension =
            operation == 0u ? (uint32_t)(int32_t)(int16_t)rotated : rotated & 0xffffu;
        result = base + extension;
    } else if (operation == 2u || operation == 3u) {
        const uint32_t low =
            operation == 2u ? (uint32_t)(int32_t)(int8_t)rotated : rotated & 0xffu;
        const uint32_t high = operation == 2u ? (uint32_t)(int32_t)(int8_t)(rotated >> 16)
                                              : (rotated >> 16) & 0xffu;
        result = pack_lane(result, (base & 0xffffu) + low, 0, 16);
        result = pack_lane(result, (base >> 16) + high, 1, 16);
    } else {
        const uint32_t extension =
            operation == 4u ? (uint32_t)(int32_t)(int8_t)rotated : rotated & 0xffu;
        result = base + extension;
    }
    cpu->registers[destination] = result;
    return true;
}

static void parallel_sources(uint8_t operation, uint8_t lane, uint8_t lane_count,
                             uint8_t* left_lane, uint8_t* right_lane, bool* subtract) {
    *left_lane = lane;
    *right_lane = lane;
    *subtract = operation == 4u || operation == 5u;
    if (operation == 2u) {
        *right_lane = (uint8_t)(lane_count - 1u - lane);
        *subtract = lane == 0;
    } else if (operation == 6u) {
        *right_lane = (uint8_t)(lane_count - 1u - lane);
        *subtract = lane != 0;
    }
}

static bool execute_parallel(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 0xff80u) != 0xfa80u || (second & 0xf080u) != 0xf000u) {
        return false;
    }
    const uint8_t operation = (uint8_t)((first >> 4) & 7u);
    const uint8_t mode = (uint8_t)((second >> 4) & 7u);
    const bool valid_operation = operation == 0u || operation == 1u || operation == 2u ||
                                 operation == 4u || operation == 5u || operation == 6u;
    const bool valid_mode =
        mode == 0u || mode == 1u || mode == 2u || mode == 4u || mode == 5u || mode == 6u;
    const uint8_t left_register = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t right_register = (uint8_t)(second & 15u);
    if (!valid_operation || !valid_mode || !valid_register(left_register) ||
        !valid_register(destination) || !valid_register(right_register)) {
        return false;
    }
    const bool bytes = operation == 0u || operation == 4u;
    const uint8_t width = bytes ? 8 : 16;
    const uint8_t lane_count = (uint8_t)(32u / width);
    const bool unsigned_mode = mode >= 4u;
    const bool saturating = mode == 1u || mode == 5u;
    const bool halving = mode == 2u || mode == 6u;
    const uint32_t left = cpu->registers[left_register];
    const uint32_t right = cpu->registers[right_register];
    uint32_t result = 0;
    uint8_t ge = 0;
    for (uint8_t lane = 0; lane < lane_count; lane++) {
        uint8_t left_lane = 0;
        uint8_t right_lane = 0;
        bool subtract = false;
        parallel_sources(operation, lane, lane_count, &left_lane, &right_lane, &subtract);
        uint32_t lane_result = 0;
        if (unsigned_mode) {
            const uint32_t a = unsigned_lane(left, left_lane, width);
            const uint32_t b = unsigned_lane(right, right_lane, width);
            const int64_t arithmetic = subtract ? (int64_t)a - b : (int64_t)a + b;
            if (saturating) {
                bool ignored = false;
                lane_result = unsigned_saturate(arithmetic, width, &ignored);
            } else if (halving) {
                const uint64_t mask = (UINT64_C(1) << (width + 1u)) - 1u;
                lane_result = (uint32_t)(((uint64_t)arithmetic & mask) >> 1);
            } else {
                lane_result = (uint32_t)arithmetic;
                const bool passed =
                    subtract ? a >= b : (uint64_t)a + b >= (UINT64_C(1) << width);
                if (passed) {
                    ge |= width == 8 ? (uint8_t)(1u << lane) : (uint8_t)(3u << (lane * 2u));
                }
            }
        } else {
            const int32_t a = signed_lane(left, left_lane, width);
            const int32_t b = signed_lane(right, right_lane, width);
            const int64_t arithmetic = subtract ? (int64_t)a - b : (int64_t)a + b;
            if (saturating) {
                bool ignored = false;
                lane_result = signed_saturate(arithmetic, width, &ignored);
            } else if (halving) {
                lane_result = (uint32_t)floor_divide(arithmetic, 2);
            } else {
                lane_result = (uint32_t)arithmetic;
                if (arithmetic >= 0) {
                    ge |= width == 8 ? (uint8_t)(1u << lane) : (uint8_t)(3u << (lane * 2u));
                }
            }
        }
        result = pack_lane(result, lane_result, lane, width);
    }
    if (!saturating && !halving) {
        set_ge(cpu, ge);
    }
    cpu->registers[destination] = result;
    return true;
}

static bool execute_scalar_saturation(CortexM4* cpu, uint16_t first, uint16_t second) {
    if (((first & 0xfbd0u) != 0xf300u && (first & 0xfbd0u) != 0xf380u) ||
        (second & 0x8000u) != 0) {
        return false;
    }
    const bool unsigned_result = (first & 0x80u) != 0;
    const uint8_t source = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    if (!valid_register(source) || !valid_register(destination)) {
        return false;
    }
    const uint8_t shift_type = (first & 0x20u) != 0 ? 2u : 0u;
    const uint8_t amount = (uint8_t)((((second >> 12) & 7u) << 2) | ((second >> 6) & 3u));
    const uint32_t shifted =
        cortex_m4_shift(cpu->registers[source], shift_type, amount, false, NULL);
    const uint8_t width =
        unsigned_result ? (uint8_t)(second & 31u) : (uint8_t)((second & 31u) + 1u);
    bool saturated = false;
    cpu->registers[destination] =
        unsigned_result ? unsigned_saturate((int32_t)shifted, width, &saturated)
                        : signed_saturate((int32_t)shifted, width, &saturated);
    if (saturated) {
        cpu->xpsr |= CORTEX_M4_XPSR_Q;
    }
    return true;
}

static bool execute_lane_saturation(CortexM4* cpu, uint16_t first, uint16_t second) {
    const uint16_t operation = first & 0xfff0u;
    if ((operation != 0xf320u && operation != 0xf3a0u) || (second & 0xf0f0u) != 0) {
        return false;
    }
    const uint8_t source = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    if (!valid_register(source) || !valid_register(destination)) {
        return false;
    }
    const bool unsigned_result = operation == 0xf3a0u;
    const uint8_t width =
        unsigned_result ? (uint8_t)(second & 15u) : (uint8_t)((second & 15u) + 1u);
    bool saturated = false;
    uint32_t result = 0;
    for (uint8_t lane = 0; lane < 2; lane++) {
        const int32_t value = signed_lane(cpu->registers[source], lane, 16);
        const uint32_t lane_result = unsigned_result
                                         ? unsigned_saturate(value, width, &saturated)
                                         : signed_saturate(value, width, &saturated);
        result = pack_lane(result, lane_result, lane, 16);
    }
    cpu->registers[destination] = result;
    if (saturated) {
        cpu->xpsr |= CORTEX_M4_XPSR_Q;
    }
    return true;
}

static bool execute_scalar_q(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 0xfff0u) != 0xfa80u || (second & 0xf080u) != 0xf080u) {
        return false;
    }
    const uint8_t operation = (uint8_t)((second >> 4) & 3u);
    const uint8_t right_register = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t left_register = (uint8_t)(second & 15u);
    if (!valid_register(left_register) || !valid_register(destination) ||
        !valid_register(right_register)) {
        return false;
    }
    bool saturated = false;
    const int64_t left = (int32_t)cpu->registers[left_register];
    int64_t right = (int32_t)cpu->registers[right_register];
    if (operation == 1u || operation == 3u) {
        right = (int32_t)signed_saturate(right * 2, 32, &saturated);
    }
    const int64_t arithmetic =
        operation == 2u || operation == 3u ? left - right : left + right;
    cpu->registers[destination] = signed_saturate(arithmetic, 32, &saturated);
    if (saturated) {
        cpu->xpsr |= CORTEX_M4_XPSR_Q;
    }
    return true;
}

static bool execute_select(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 0xfff0u) != 0xfaa0u || (second & 0xf0f0u) != 0xf080u) {
        return false;
    }
    const uint8_t left_register = (uint8_t)(first & 15u);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t right_register = (uint8_t)(second & 15u);
    if (!valid_register(left_register) || !valid_register(destination) ||
        !valid_register(right_register)) {
        return false;
    }
    uint32_t result = 0;
    for (uint8_t lane = 0; lane < 4; lane++) {
        const uint32_t source = (cpu->xpsr & (1u << (XPSR_GE_SHIFT + lane))) != 0
                                    ? cpu->registers[left_register]
                                    : cpu->registers[right_register];
        result = pack_lane(result, source >> (lane * 8u), lane, 8);
    }
    cpu->registers[destination] = result;
    return true;
}

static bool execute_absolute_difference(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 0xfff0u) != 0xfb70u || (second & 0x00f0u) != 0) {
        return false;
    }
    const uint8_t left_register = (uint8_t)(first & 15u);
    const uint8_t accumulator = (uint8_t)(second >> 12);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t right_register = (uint8_t)(second & 15u);
    if (!valid_register(left_register) ||
        (!valid_register(accumulator) && accumulator != 15u) ||
        !valid_register(destination) || !valid_register(right_register)) {
        return false;
    }
    uint32_t result = accumulator == 15u ? 0 : cpu->registers[accumulator];
    for (uint8_t lane = 0; lane < 4; lane++) {
        const uint32_t left = unsigned_lane(cpu->registers[left_register], lane, 8);
        const uint32_t right = unsigned_lane(cpu->registers[right_register], lane, 8);
        result += left >= right ? left - right : right - left;
    }
    cpu->registers[destination] = result;
    return true;
}

static int32_t selected_half(uint32_t value, bool top) {
    return top ? (int16_t)(value >> 16) : (int16_t)value;
}

static void set_q_on_i32_overflow(CortexM4* cpu, int64_t value) {
    if (value < INT32_MIN || value > INT32_MAX) {
        cpu->xpsr |= CORTEX_M4_XPSR_Q;
    }
}

static bool execute_half_multiply(CortexM4* cpu, uint16_t first, uint16_t second) {
    const uint16_t operation = first & 0xfff0u;
    if ((operation != 0xfb10u && operation != 0xfb30u) || (second & 0x00c0u) != 0 ||
        (operation == 0xfb30u && (second & 0x20u) != 0)) {
        return false;
    }
    const uint8_t left_register = (uint8_t)(first & 15u);
    const uint8_t accumulator = (uint8_t)(second >> 12);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t right_register = (uint8_t)(second & 15u);
    if (!valid_register(left_register) ||
        (!valid_register(accumulator) && accumulator != 15u) ||
        !valid_register(destination) || !valid_register(right_register)) {
        return false;
    }
    int64_t product = 0;
    if (operation == 0xfb10u) {
        product =
            (int64_t)selected_half(cpu->registers[left_register], (second & 0x20u) != 0) *
            selected_half(cpu->registers[right_register], (second & 0x10u) != 0);
    } else {
        const int64_t wide_product =
            (int64_t)(int32_t)cpu->registers[left_register] *
            selected_half(cpu->registers[right_register], (second & 0x10u) != 0);
        product = floor_divide(wide_product, INT64_C(65536));
    }
    const int64_t result =
        accumulator == 15u ? product : product + (int32_t)cpu->registers[accumulator];
    if (accumulator != 15u) {
        set_q_on_i32_overflow(cpu, result);
    }
    cpu->registers[destination] = (uint32_t)result;
    return true;
}

static bool execute_dual_multiply(CortexM4* cpu, uint16_t first, uint16_t second) {
    const uint16_t operation = first & 0xfff0u;
    if ((operation != 0xfb20u && operation != 0xfb40u) || (second & 0x00e0u) != 0) {
        return false;
    }
    const uint8_t left_register = (uint8_t)(first & 15u);
    const uint8_t accumulator = (uint8_t)(second >> 12);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t right_register = (uint8_t)(second & 15u);
    if (!valid_register(left_register) ||
        (!valid_register(accumulator) && accumulator != 15u) ||
        !valid_register(destination) || !valid_register(right_register)) {
        return false;
    }
    const uint32_t left = cpu->registers[left_register];
    uint32_t right = cpu->registers[right_register];
    if ((second & 0x10u) != 0) {
        right = (right << 16) | (right >> 16);
    }
    const int64_t low_product = (int64_t)(int16_t)left * (int16_t)right;
    const int64_t high_product = (int64_t)(int16_t)(left >> 16) * (int16_t)(right >> 16);
    const int64_t products =
        operation == 0xfb20u ? low_product + high_product : low_product - high_product;
    const int64_t result =
        accumulator == 15u ? products : products + (int32_t)cpu->registers[accumulator];
    set_q_on_i32_overflow(cpu, result);
    cpu->registers[destination] = (uint32_t)result;
    return true;
}

static bool execute_long_dsp_multiply(CortexM4* cpu, uint16_t first, uint16_t second) {
    const uint16_t operation = first & 0xfff0u;
    const uint8_t suboperation = (uint8_t)((second >> 4) & 15u);
    const bool half_product =
        operation == 0xfbc0u && suboperation >= 8u && suboperation <= 11u;
    const bool dual_add =
        operation == 0xfbc0u && (suboperation == 12u || suboperation == 13u);
    const bool dual_subtract =
        operation == 0xfbd0u && (suboperation == 12u || suboperation == 13u);
    const bool umaal = operation == 0xfbe0u && suboperation == 6u;
    if (!half_product && !dual_add && !dual_subtract && !umaal) {
        return false;
    }
    const uint8_t left_register = (uint8_t)(first & 15u);
    const uint8_t low_destination = (uint8_t)(second >> 12);
    const uint8_t high_destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t right_register = (uint8_t)(second & 15u);
    if (!valid_register(left_register) || !valid_register(low_destination) ||
        !valid_register(high_destination) || !valid_register(right_register) ||
        low_destination == high_destination) {
        return false;
    }
    uint64_t result = 0;
    if (umaal) {
        result = (uint64_t)cpu->registers[left_register] * cpu->registers[right_register] +
                 cpu->registers[low_destination] + cpu->registers[high_destination];
    } else {
        int64_t product = 0;
        if (half_product) {
            product =
                (int64_t)selected_half(cpu->registers[left_register],
                                       (suboperation & 2u) != 0) *
                selected_half(cpu->registers[right_register], (suboperation & 1u) != 0);
        } else {
            uint32_t right = cpu->registers[right_register];
            if ((suboperation & 1u) != 0) {
                right = (right << 16) | (right >> 16);
            }
            const int64_t low_product =
                (int64_t)(int16_t)cpu->registers[left_register] * (int16_t)right;
            const int64_t high_product =
                (int64_t)(int16_t)(cpu->registers[left_register] >> 16) *
                (int16_t)(right >> 16);
            product =
                dual_subtract ? low_product - high_product : low_product + high_product;
        }
        const uint64_t accumulator = ((uint64_t)cpu->registers[high_destination] << 32) |
                                     cpu->registers[low_destination];
        result = accumulator + (uint64_t)product;
    }
    cpu->registers[low_destination] = (uint32_t)result;
    cpu->registers[high_destination] = (uint32_t)(result >> 32);
    return true;
}

static bool execute_most_significant_multiply(CortexM4* cpu, uint16_t first,
                                              uint16_t second) {
    const uint16_t operation = first & 0xfff0u;
    if ((operation != 0xfb50u && operation != 0xfb60u) || (second & 0x00e0u) != 0) {
        return false;
    }
    const uint8_t left_register = (uint8_t)(first & 15u);
    const uint8_t accumulator = (uint8_t)(second >> 12);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t right_register = (uint8_t)(second & 15u);
    if (!valid_register(left_register) ||
        (!valid_register(accumulator) && accumulator != 15u) ||
        !valid_register(destination) || !valid_register(right_register) ||
        (operation == 0xfb60u && accumulator == 15u)) {
        return false;
    }
    const uint64_t product = (uint64_t)((int64_t)(int32_t)cpu->registers[left_register] *
                                        (int32_t)cpu->registers[right_register]);
    const uint64_t accumulator_bits =
        accumulator == 15u ? 0 : (uint64_t)cpu->registers[accumulator] << 32;
    uint64_t result =
        operation == 0xfb60u ? accumulator_bits - product : accumulator_bits + product;
    if ((second & 0x10u) != 0) {
        result += UINT64_C(0x80000000);
    }
    cpu->registers[destination] = (uint32_t)(result >> 32);
    return true;
}

static bool execute_multiply_subtract(CortexM4* cpu, uint16_t first, uint16_t second) {
    if ((first & 0xfff0u) != 0xfb00u || (second & 0x00f0u) != 0x0010u) {
        return false;
    }
    const uint8_t left_register = (uint8_t)(first & 15u);
    const uint8_t accumulator = (uint8_t)(second >> 12);
    const uint8_t destination = (uint8_t)((second >> 8) & 15u);
    const uint8_t right_register = (uint8_t)(second & 15u);
    if (!valid_register(left_register) || !valid_register(accumulator) ||
        !valid_register(destination) || !valid_register(right_register)) {
        return false;
    }
    cpu->registers[destination] =
        cpu->registers[accumulator] -
        cpu->registers[left_register] * cpu->registers[right_register];
    return true;
}

bool cortex_m4_execute_dsp(CortexM4* cpu, uint16_t first, uint16_t second) {
    return execute_pack(cpu, first, second) || execute_extend(cpu, first, second) ||
           execute_parallel(cpu, first, second) ||
           execute_lane_saturation(cpu, first, second) ||
           execute_scalar_saturation(cpu, first, second) ||
           execute_scalar_q(cpu, first, second) || execute_select(cpu, first, second) ||
           execute_absolute_difference(cpu, first, second) ||
           execute_half_multiply(cpu, first, second) ||
           execute_dual_multiply(cpu, first, second) ||
           execute_long_dsp_multiply(cpu, first, second) ||
           execute_most_significant_multiply(cpu, first, second) ||
           execute_multiply_subtract(cpu, first, second);
}
