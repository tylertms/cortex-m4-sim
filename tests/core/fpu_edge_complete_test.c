#include "kinetis_k22.h"

#include <stdint.h>

#include "cortex_m4_internal.h"
#include "test.h"

enum {
    FPSCR_IOC = 1u << 0,
    FPSCR_OFC = 1u << 2,
    FPSCR_UFC = 1u << 3,
    FPSCR_IXC = 1u << 4,
    FPSCR_IDC = 1u << 7,
    FPSCR_ROUND_PLUS_INFINITY = 1u << 22,
    FPSCR_ROUND_MINUS_INFINITY = 2u << 22,
    FPSCR_ROUND_ZERO = 3u << 22,
    FPSCR_FZ = 1u << 24,
    FPSCR_AHP = 1u << 26,
};

static KinetisK22* create_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096u;
    configuration.sram_size = 65536u;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(state, device != NULL, "device != NULL");
    const uint32_t vectors[2] = {0x20001000u, 0x101u};
    expect(state, kinetis_k22_load(device, 0u, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0u, vectors, sizeof(vectors))");
    return device;
}

static CortexM4* prepare(TestState* state, KinetisK22* device, uint16_t first, uint16_t second) {
    const uint16_t program[] = {first, second, 0xbe00u};
    expect(state, kinetis_k22_load(device, 0x100u, program, sizeof(program)),
           "kinetis_k22_load(device, 0x100u, program, sizeof(program))");
    expect(state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    CortexM4* cpu = kinetis_k22_cpu(device);
    test_connect_debugger(state, cpu);
    expect(state, cortex_m4_write_memory(cpu, 0xe000ed88u, 4u, 0x00f00000u),
           "cortex_m4_write_memory(cpu, 0xe000ed88u, 4u, 0x00f00000u)");
    return cpu;
}

static void run(TestState* state, CortexM4* cpu) {
    const CortexM4Result result = cortex_m4_run(cpu, (CortexM4RunLimits){2u, 16u});
    expect(state, result.stop == CORTEX_M4_STOP_BREAKPOINT,
           "result.stop == CORTEX_M4_STOP_BREAKPOINT");
}

static void test_nan_selection(TestState* state, KinetisK22* device) {
    CortexM4* cpu = prepare(state, device, 0xee37u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14u, 0x7fc00011u);
    cortex_m4_set_fp_register(cpu, 15u, 0x7f800022u);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 14u) == 0x7fc00022u,
           "cortex_m4_get_fp_register(cpu, 14u) == 0x7fc00022u");
    expect(state, (cortex_m4_get_fpscr(cpu) & FPSCR_IOC) != 0u,
           "(cortex_m4_get_fpscr(cpu) & FPSCR_IOC) != 0u");

    cpu = prepare(state, device, 0xee37u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14u, 0x7fc00011u);
    cortex_m4_set_fp_register(cpu, 15u, 0x7fc00022u);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 14u) == 0x7fc00011u,
           "cortex_m4_get_fp_register(cpu, 14u) == 0x7fc00011u");

    cpu = prepare(state, device, 0xee37u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14u, 0x3f800000u);
    cortex_m4_set_fp_register(cpu, 15u, 0x7fc00022u);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 14u) == 0x7fc00022u,
           "cortex_m4_get_fp_register(cpu, 14u) == 0x7fc00022u");
}

static uint32_t convert_rounded(TestState* state, KinetisK22* device, uint32_t source,
                                uint32_t rounding) {
    CortexM4* cpu = prepare(state, device, 0xeebdu, 0x0a60u);
    cortex_m4_set_fp_register(cpu, 1u, source);
    cortex_m4_set_fpscr(cpu, rounding);
    run(state, cpu);
    return cortex_m4_get_fp_register(cpu, 0u);
}

static void test_integer_rounding(TestState* state, KinetisK22* device) {
    expect(state, convert_rounded(state, device, 0x3f99999au, FPSCR_ROUND_PLUS_INFINITY) == 2u,
           "convert_rounded(state, device, 0x3f99999au, FPSCR_ROUND_PLUS_INFINITY) == 2u");
    expect(state, convert_rounded(state, device, 0x3fe66666u, FPSCR_ROUND_MINUS_INFINITY) == 1u,
           "convert_rounded(state, device, 0x3fe66666u, FPSCR_ROUND_MINUS_INFINITY) == 1u");
    expect(state, convert_rounded(state, device, 0xbfe66666u, FPSCR_ROUND_ZERO) == 0xffffffffu,
           "convert_rounded(state, device, 0xbfe66666u, FPSCR_ROUND_ZERO) == 0xffffffffu");
    expect(state, convert_rounded(state, device, 0x40100000u, 0u) == 2u,
           "convert_rounded(state, device, 0x40100000u, 0u) == 2u");
    expect(state, convert_rounded(state, device, 0x40300000u, 0u) == 3u,
           "convert_rounded(state, device, 0x40300000u, 0u) == 3u");
    expect(state, convert_rounded(state, device, 0x40200000u, 0u) == 2u,
           "convert_rounded(state, device, 0x40200000u, 0u) == 2u");
    expect(state, convert_rounded(state, device, 0x40600000u, 0u) == 4u,
           "convert_rounded(state, device, 0x40600000u, 0u) == 4u");
}

static uint32_t half_to_single(TestState* state, KinetisK22* device, uint16_t half,
                               uint32_t fpscr) {
    CortexM4* cpu = prepare(state, device, 0xeeb2u, 0x0a60u);
    cortex_m4_set_fp_register(cpu, 1u, half);
    cortex_m4_set_fpscr(cpu, fpscr);
    run(state, cpu);
    return cortex_m4_get_fp_register(cpu, 0u);
}

static uint16_t single_to_half(TestState* state, KinetisK22* device, uint32_t single,
                               uint32_t fpscr) {
    CortexM4* cpu = prepare(state, device, 0xeeb3u, 0x2a62u);
    cortex_m4_set_fp_register(cpu, 5u, single);
    cortex_m4_set_fp_register(cpu, 4u, 0xa5a50000u);
    cortex_m4_set_fpscr(cpu, fpscr);
    run(state, cpu);
    return (uint16_t)cortex_m4_get_fp_register(cpu, 4u);
}

static void test_half_precision(TestState* state, KinetisK22* device) {
    expect(state, half_to_single(state, device, 0u, 0u) == 0u,
           "half_to_single(state, device, 0u, 0u) == 0u");
    expect(state, half_to_single(state, device, 1u, 0u) == 0x33800000u,
           "half_to_single(state, device, 1u, 0u) == 0x33800000u");
    expect(state, half_to_single(state, device, 1u, FPSCR_FZ) == 0u,
           "half_to_single(state, device, 1u, FPSCR_FZ) == 0u");
    expect(state, half_to_single(state, device, 0x7c00u, 0u) == 0x7f800000u,
           "half_to_single(state, device, 0x7c00u, 0u) == 0x7f800000u");
    expect(state, (half_to_single(state, device, 0x7c01u, 0u) & 0x7fc00000u) == 0x7fc00000u,
           "(half_to_single(state, device, 0x7c01u, 0u) & 0x7fc00000u) == 0x7fc00000u");
    expect(state, half_to_single(state, device, 0x7c00u, FPSCR_AHP) == 0x47800000u,
           "half_to_single(state, device, 0x7c00u, FPSCR_AHP) == 0x47800000u");

    expect(state, (single_to_half(state, device, 0x7fc00011u, 0u) & 0x7c00u) == 0x7c00u,
           "(single_to_half(state, device, 0x7fc00011u, 0u) & 0x7c00u) == 0x7c00u");
    expect(state, single_to_half(state, device, 0x7fc00011u, FPSCR_AHP) == 0x7fffu,
           "single_to_half(state, device, 0x7fc00011u, FPSCR_AHP) == 0x7fffu");
    expect(state, single_to_half(state, device, 0x7f800000u, 0u) == 0x7c00u,
           "single_to_half(state, device, 0x7f800000u, 0u) == 0x7c00u");
    expect(state, single_to_half(state, device, 0x7f800000u, FPSCR_AHP) == 0x7fffu,
           "single_to_half(state, device, 0x7f800000u, FPSCR_AHP) == 0x7fffu");
    expect(state, single_to_half(state, device, 0x4788b800u, 0u) == 0x7c00u,
           "single_to_half(state, device, 0x4788b800u, 0u) == 0x7c00u");
    expect(state, single_to_half(state, device, 0x48000000u, FPSCR_AHP) == 0x7fffu,
           "single_to_half(state, device, 0x48000000u, FPSCR_AHP) == 0x7fffu");
    expect(state, single_to_half(state, device, 0x33800000u, 0u) == 1u,
           "single_to_half(state, device, 0x33800000u, 0u) == 1u");
    expect(state, single_to_half(state, device, 0x33800000u, FPSCR_FZ) == 0u,
           "single_to_half(state, device, 0x33800000u, FPSCR_FZ) == 0u");
    expect(state, single_to_half(state, device, 0x3fffffffu, 0u) == 0x4000u,
           "single_to_half(state, device, 0x3fffffffu, 0u) == 0x4000u");
    expect(state, single_to_half(state, device, 0x3f801000u, 0u) == 0x3c00u,
           "single_to_half(state, device, 0x3f801000u, 0u) == 0x3c00u");
    expect(state, single_to_half(state, device, 0x3f803000u, 0u) == 0x3c02u,
           "single_to_half(state, device, 0x3f803000u, 0u) == 0x3c02u");
    expect(state, single_to_half(state, device, 0x3f80068eu, FPSCR_ROUND_PLUS_INFINITY) == 0x3c01u,
           "single_to_half(state, device, 0x3f80068eu, FPSCR_ROUND_PLUS_INFINITY) == 0x3c01u");
    expect(state, single_to_half(state, device, 0xbf80068eu, FPSCR_ROUND_PLUS_INFINITY) == 0xbc00u,
           "single_to_half(state, device, 0xbf80068eu, FPSCR_ROUND_PLUS_INFINITY) == 0xbc00u");
    expect(state, single_to_half(state, device, 0x3f80068eu, FPSCR_ROUND_MINUS_INFINITY) == 0x3c00u,
           "single_to_half(state, device, 0x3f80068eu, FPSCR_ROUND_MINUS_INFINITY) == "
           "0x3c00u");
    expect(state, single_to_half(state, device, 0xbf80068eu, FPSCR_ROUND_MINUS_INFINITY) == 0xbc01u,
           "single_to_half(state, device, 0xbf80068eu, FPSCR_ROUND_MINUS_INFINITY) == "
           "0xbc01u");
    expect(state, single_to_half(state, device, 0x3f80068eu, FPSCR_ROUND_ZERO) == 0x3c00u,
           "single_to_half(state, device, 0x3f80068eu, FPSCR_ROUND_ZERO) == 0x3c00u");
    expect(state, single_to_half(state, device, 0x33000000u, 0u) == 0u,
           "single_to_half(state, device, 0x33000000u, 0u) == 0u");
}

static void multiply(TestState* state, KinetisK22* device, uint32_t left, uint32_t right,
                     uint32_t fpscr, uint32_t expected) {
    CortexM4* cpu = prepare(state, device, 0xee27u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14u, left);
    cortex_m4_set_fp_register(cpu, 15u, right);
    cortex_m4_set_fpscr(cpu, fpscr);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 14u) == expected,
           "cortex_m4_get_fp_register(cpu, 14u) == expected");
}

static void add(TestState* state, KinetisK22* device, uint32_t left, uint32_t right, uint32_t fpscr,
                uint32_t expected) {
    CortexM4* cpu = prepare(state, device, 0xee37u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14u, left);
    cortex_m4_set_fp_register(cpu, 15u, right);
    cortex_m4_set_fpscr(cpu, fpscr);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 14u) == expected,
           "cortex_m4_get_fp_register(cpu, 14u) == expected");
}

static void test_float_rounding(TestState* state, KinetisK22* device) {
    multiply(state, device, 0xff7fffffu, 0x40000000u, FPSCR_ROUND_PLUS_INFINITY, 0xff7fffffu);
    multiply(state, device, 1u, 0x3f000000u, FPSCR_ROUND_PLUS_INFINITY, 1u);
    multiply(state, device, 0x80000001u, 0x3f000000u, FPSCR_ROUND_MINUS_INFINITY, 0x80000001u);
    multiply(state, device, 1u, 0x3fc00000u, 0u, 2u);
    add(state, device, 0x3f800000u, 0x33c00000u, FPSCR_ROUND_MINUS_INFINITY, 0x3f800000u);
    add(state, device, 0xbf800000u, 0xb3c00000u, FPSCR_ROUND_ZERO, 0xbf800000u);
}

static void test_invalid_arithmetic(TestState* state, KinetisK22* device) {
    CortexM4* cpu = prepare(state, device, 0xee27u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14u, 0x7f800000u);
    cortex_m4_set_fp_register(cpu, 15u, 0u);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 14u) == 0x7fc00000u,
           "cortex_m4_get_fp_register(cpu, 14u) == 0x7fc00000u");

    cpu = prepare(state, device, 0xee37u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14u, 0x7f800000u);
    cortex_m4_set_fp_register(cpu, 15u, 0xff800000u);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 14u) == 0x7fc00000u,
           "cortex_m4_get_fp_register(cpu, 14u) == 0x7fc00000u");

    cpu = prepare(state, device, 0xee00u, 0x0a81u);
    cortex_m4_set_fp_register(cpu, 0u, 0x7fc00011u);
    cortex_m4_set_fp_register(cpu, 1u, 0x3f800000u);
    cortex_m4_set_fp_register(cpu, 2u, 0x40000000u);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 0u) == 0x7fc00011u,
           "cortex_m4_get_fp_register(cpu, 0u) == 0x7fc00011u");

    cpu = prepare(state, device, 0xee00u, 0x0a81u);
    cortex_m4_set_fp_register(cpu, 0u, 0x3f800000u);
    cortex_m4_set_fp_register(cpu, 1u, 0x7fc00011u);
    cortex_m4_set_fp_register(cpu, 2u, 0x40000000u);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 0u) == 0x7fc00011u,
           "cortex_m4_get_fp_register(cpu, 0u) == 0x7fc00011u");

    cpu = prepare(state, device, 0xee00u, 0x0a81u);
    cortex_m4_set_fp_register(cpu, 0u, 0x3f800000u);
    cortex_m4_set_fp_register(cpu, 1u, 0x7f800000u);
    cortex_m4_set_fp_register(cpu, 2u, 0u);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 0u) == 0x7fc00000u,
           "cortex_m4_get_fp_register(cpu, 0u) == 0x7fc00000u");

    cpu = prepare(state, device, 0xeee8u, 0x7a28u);
    cortex_m4_set_fp_register(cpu, 15u, 0xff800000u);
    cortex_m4_set_fp_register(cpu, 16u, 0x7f800000u);
    cortex_m4_set_fp_register(cpu, 17u, 0x3f800000u);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 15u) == 0x7fc00000u,
           "cortex_m4_get_fp_register(cpu, 15u) == 0x7fc00000u");

    cpu = prepare(state, device, 0xee10u, 0x0a81u);
    cortex_m4_set_fp_register(cpu, 0u, 0x40400000u);
    cortex_m4_set_fp_register(cpu, 1u, 0x3f800000u);
    cortex_m4_set_fp_register(cpu, 2u, 0x40000000u);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 0u) == 0xc0a00000u,
           "cortex_m4_get_fp_register(cpu, 0u) == 0xc0a00000u");
}

static void test_unary_nan(TestState* state, KinetisK22* device) {
    CortexM4* cpu = prepare(state, device, 0xeeb1u, 0x1a61u);
    cortex_m4_set_fp_register(cpu, 3u, 0x7f800011u);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 2u) == 0xffc00011u,
           "cortex_m4_get_fp_register(cpu, 2u) == 0xffc00011u");

    cpu = prepare(state, device, 0xeeb1u, 0x2ae2u);
    cortex_m4_set_fp_register(cpu, 5u, 0x7f800011u);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 4u) == 0x7fc00011u,
           "cortex_m4_get_fp_register(cpu, 4u) == 0x7fc00011u");
}

static void test_compare_edges(TestState* state, KinetisK22* device) {
    CortexM4* cpu = prepare(state, device, 0xeeb4u, 0x0a60u);
    cortex_m4_set_fp_register(cpu, 0u, 0x3f800000u);
    cortex_m4_set_fp_register(cpu, 1u, 0x7f800001u);
    run(state, cpu);
    expect(state, (cortex_m4_get_fpscr(cpu) & FPSCR_IOC) != 0u,
           "(cortex_m4_get_fpscr(cpu) & FPSCR_IOC) != 0u");

    cpu = prepare(state, device, 0xeeb4u, 0x0a60u);
    cortex_m4_set_fp_register(cpu, 0u, 0x40000000u);
    cortex_m4_set_fp_register(cpu, 1u, 0x3f800000u);
    run(state, cpu);
    expect(state, (cortex_m4_get_fpscr(cpu) & CORTEX_M4_XPSR_C) != 0u,
           "(cortex_m4_get_fpscr(cpu) & CORTEX_M4_XPSR_C) != 0u");
}

static void test_conversion_limits(TestState* state, KinetisK22* device) {
    CortexM4* cpu = prepare(state, device, 0xeebdu, 0x0ae0u);
    cortex_m4_set_fp_register(cpu, 1u, 0x7f800000u);
    run(state, cpu);
    expect(state, cortex_m4_get_fp_register(cpu, 0u) == 0x7fffffffu,
           "cortex_m4_get_fp_register(cpu, 0u) == 0x7fffffffu");
}

static void test_transfer_edges(TestState* state, KinetisK22* device) {
    CortexM4* cpu = prepare(state, device, 0xeef1u, 0x1a10u);
    cortex_m4_set_fpscr(cpu, 0xa0000000u);
    run(state, cpu);
    expect(state, cortex_m4_get_register(cpu, 1u) == 0xa0000000u,
           "cortex_m4_get_register(cpu, 1u) == 0xa0000000u");

    cpu = prepare(state, device, 0xed94u, 0x2b04u);
    cortex_m4_set_register(cpu, 4u, 0x60000000u);
    expect(state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING,
           "cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING");
    expect(state, (cortex_m4_get_fault_status(cpu) & (1u << 9)) != 0u,
           "(cortex_m4_get_fault_status(cpu) & (1u << 9)) != 0u");

    cpu = prepare(state, device, 0xecb6u, 0x2a04u);
    cortex_m4_set_register(cpu, 6u, 0x60000000u);
    expect(state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING,
           "cortex_m4_step(cpu).stop == CORTEX_M4_STOP_RUNNING");
    expect(state, (cortex_m4_get_fault_status(cpu) & (1u << 9)) != 0u,
           "(cortex_m4_get_fault_status(cpu) & (1u << 9)) != 0u");

    cpu = prepare(state, device, 0xeca4u, 0x0b04u);
    cortex_m4_set_register(cpu, 4u, 0x20000100u);
    for (uint8_t index = 0u; index < 4u; index++) {
        cortex_m4_set_fp_register(cpu, index, 0x12340000u + index);
    }
    run(state, cpu);
    expect(state, cortex_m4_get_register(cpu, 4u) == 0x20000110u,
           "cortex_m4_get_register(cpu, 4u) == 0x20000110u");
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = create_device(&state);
    test_nan_selection(&state, device);
    test_integer_rounding(&state, device);
    test_half_precision(&state, device);
    test_float_rounding(&state, device);
    test_invalid_arithmetic(&state, device);
    test_unary_nan(&state, device);
    test_compare_edges(&state, device);
    test_conversion_limits(&state, device);
    test_transfer_edges(&state, device);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
