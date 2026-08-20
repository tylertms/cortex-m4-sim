#include "kinetis_k22.h"

#include <stdint.h>

#include "cortex_m4_internal.h"
#include "test.h"

enum {
    FPSCR_IOC = 1u << 0,
    FPSCR_DZC = 1u << 1,
    FPSCR_OFC = 1u << 2,
    FPSCR_UFC = 1u << 3,
    FPSCR_IXC = 1u << 4,
    FPSCR_IDC = 1u << 7,
    FPSCR_ROUND_PLUS_INFINITY = 1u << 22,
    FPSCR_ROUND_ZERO = 3u << 22,
    FPSCR_FZ = 1u << 24,
    FPSCR_DN = 1u << 25,
};

static KinetisK22* create_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096;
    configuration.sram_size = 65536;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(state, device != NULL);
    const uint32_t vectors[2] = {0x20001000u, 0x00000101u};
    TEST_EXPECT(state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)));
    return device;
}

static CortexM4* prepare(TestState* state, KinetisK22* device, uint16_t first,
                         uint16_t second) {
    const uint8_t program[] = {(uint8_t)first,
                               (uint8_t)(first >> 8),
                               (uint8_t)second,
                               (uint8_t)(second >> 8),
                               0x00,
                               0xbe};
    TEST_EXPECT(state, kinetis_k22_load(device, 0x100, program, sizeof(program)));
    TEST_EXPECT(state, kinetis_k22_reset(device));
    CortexM4* cpu = kinetis_k22_cpu(device);
    TEST_CONNECT_DEBUGGER(state, cpu);
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0xe000ed88u, 4, 0x00f00000u));
    return cpu;
}

static void run(TestState* state, CortexM4* cpu) {
    const CortexM4Result result = cortex_m4_run(cpu, (CortexM4RunLimits){2, 10});
    TEST_EXPECT(state, result.stop == CORTEX_M4_STOP_BREAKPOINT);
}

static void test_unary_and_comparison(TestState* state, KinetisK22* device) {
    CortexM4* cpu = prepare(state, device, 0xeeb0u, 0x0ae0u);
    cortex_m4_set_fp_register(cpu, 1, 0xc0600000u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 0) == 0x40600000u);

    cpu = prepare(state, device, 0xeeb1u, 0x1a61u);
    cortex_m4_set_fp_register(cpu, 3, 0x3f800000u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 2) == 0xbf800000u);

    cpu = prepare(state, device, 0xeeb1u, 0x2ae2u);
    cortex_m4_set_fp_register(cpu, 5, 0x41100000u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 4) == 0x40400000u);

    cpu = prepare(state, device, 0xeeb1u, 0x2ae2u);
    cortex_m4_set_fp_register(cpu, 5, 0xbf800000u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 4) == 0x7fc00000u);
    TEST_EXPECT(state, (cortex_m4_get_fpscr(cpu) & FPSCR_IOC) != 0);

    cpu = prepare(state, device, 0xeeb5u, 0x5a40u);
    cortex_m4_set_fp_register(cpu, 0, 0x3f800000u);
    cortex_m4_set_fp_register(cpu, 10, 0x80000000u);
    run(state, cpu);
    TEST_EXPECT(state, (cortex_m4_get_fpscr(cpu) & 0xf0000000u) == 0x60000000u);

    cpu = prepare(state, device, 0xeef5u, 0x5ac0u);
    cortex_m4_set_fp_register(cpu, 11, 0x7fc00001u);
    run(state, cpu);
    TEST_EXPECT(state, (cortex_m4_get_fpscr(cpu) & 0xf0000000u) == 0x30000000u);
    TEST_EXPECT(state, (cortex_m4_get_fpscr(cpu) & FPSCR_IOC) != 0);
}

static void test_arithmetic_and_status(TestState* state, KinetisK22* device) {
    CortexM4* cpu = prepare(state, device, 0xee37u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14, 0x3f800000u);
    cortex_m4_set_fp_register(cpu, 15, 0x33800000u);
    cortex_m4_set_fpscr(cpu, FPSCR_ROUND_PLUS_INFINITY);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 14) == 0x3f800001u);
    TEST_EXPECT(state, (cortex_m4_get_fpscr(cpu) & FPSCR_IXC) != 0);

    cpu = prepare(state, device, 0xee87u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14, 0x3f800000u);
    cortex_m4_set_fp_register(cpu, 15, 0);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 14) == 0x7f800000u);
    TEST_EXPECT(state, (cortex_m4_get_fpscr(cpu) & FPSCR_DZC) != 0);

    cpu = prepare(state, device, 0xee27u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14, 0x7f800000u);
    cortex_m4_set_fp_register(cpu, 15, 0);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 14) == 0x7fc00000u);
    TEST_EXPECT(state, (cortex_m4_get_fpscr(cpu) & FPSCR_IOC) != 0);

    cpu = prepare(state, device, 0xee37u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14, 0x7f800001u);
    cortex_m4_set_fp_register(cpu, 15, 0x3f800000u);
    cortex_m4_set_fpscr(cpu, FPSCR_DN);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 14) == 0x7fc00000u);
    TEST_EXPECT(state, (cortex_m4_get_fpscr(cpu) & FPSCR_IOC) != 0);

    cpu = prepare(state, device, 0xee37u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14, 1u);
    cortex_m4_set_fp_register(cpu, 15, 0x3f800000u);
    cortex_m4_set_fpscr(cpu, FPSCR_FZ);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 14) == 0x3f800000u);
    TEST_EXPECT(state, (cortex_m4_get_fpscr(cpu) & FPSCR_IDC) != 0);

    cpu = prepare(state, device, 0xee27u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14, 0x7f7fffffu);
    cortex_m4_set_fp_register(cpu, 15, 0x7f7fffffu);
    cortex_m4_set_fpscr(cpu, FPSCR_ROUND_ZERO);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 14) == 0x7f7fffffu);
    TEST_EXPECT(state, (cortex_m4_get_fpscr(cpu) & (FPSCR_OFC | FPSCR_IXC)) ==
                           (FPSCR_OFC | FPSCR_IXC));

    cpu = prepare(state, device, 0xee27u, 0x7a27u);
    cortex_m4_set_fp_register(cpu, 14, 0x00800000u);
    cortex_m4_set_fp_register(cpu, 15, 0x3f000000u);
    cortex_m4_set_fpscr(cpu, FPSCR_FZ);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 14) == 0);
    TEST_EXPECT(state, (cortex_m4_get_fpscr(cpu) & (FPSCR_UFC | FPSCR_IXC)) ==
                           (FPSCR_UFC | FPSCR_IXC));
}

static void test_accumulate(TestState* state, KinetisK22* device) {
    CortexM4* cpu = prepare(state, device, 0xee00u, 0x0a81u);
    cortex_m4_set_fp_register(cpu, 0, 0x3f800000u);
    cortex_m4_set_fp_register(cpu, 1, 0x40000000u);
    cortex_m4_set_fp_register(cpu, 2, 0x40400000u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 0) == 0x40e00000u);

    cpu = prepare(state, device, 0xeee8u, 0x7a28u);
    cortex_m4_set_fp_register(cpu, 15, 0x3f800000u);
    cortex_m4_set_fp_register(cpu, 16, 0x40000000u);
    cortex_m4_set_fp_register(cpu, 17, 0x40400000u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 15) == 0x40e00000u);

    cpu = prepare(state, device, 0xeea9u, 0x9acau);
    cortex_m4_set_fp_register(cpu, 18, 0x40a00000u);
    cortex_m4_set_fp_register(cpu, 19, 0x40000000u);
    cortex_m4_set_fp_register(cpu, 20, 0x40400000u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 18) == 0xbf800000u);

    cpu = prepare(state, device, 0xee26u, 0x6ac7u);
    cortex_m4_set_fp_register(cpu, 13, 0x40000000u);
    cortex_m4_set_fp_register(cpu, 14, 0x40400000u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 12) == 0xc0c00000u);
}

static void test_conversions(TestState* state, KinetisK22* device) {
    CortexM4* cpu = prepare(state, device, 0xeebdu, 0x0ae0u);
    cortex_m4_set_fp_register(cpu, 1, 0x3fe00000u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 0) == 1);
    TEST_EXPECT(state, (cortex_m4_get_fpscr(cpu) & FPSCR_IXC) != 0);

    cpu = prepare(state, device, 0xeebdu, 0x2a62u);
    cortex_m4_set_fp_register(cpu, 5, 0x3fe00000u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 4) == 2);

    cpu = prepare(state, device, 0xeeb8u, 0x4ae4u);
    cortex_m4_set_fp_register(cpu, 9, 0xfffffffeu);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 8) == 0xc0000000u);

    cpu = prepare(state, device, 0xeebeu, 0x6a64u);
    cortex_m4_set_fp_register(cpu, 12, 0x3fc00000u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 12) == 192u);

    cpu = prepare(state, device, 0xeebau, 0x8a63u);
    cortex_m4_set_fp_register(cpu, 16, 0xfffffc00u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 16) == 0xc0000000u);

    cpu = prepare(state, device, 0xeeb2u, 0x0a60u);
    cortex_m4_set_fp_register(cpu, 1, 0x00003e00u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 0) == 0x3fc00000u);

    cpu = prepare(state, device, 0xeeb3u, 0x2a62u);
    cortex_m4_set_fp_register(cpu, 5, 0x3fc00000u);
    cortex_m4_set_fp_register(cpu, 4, 0xabcd0000u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 4) == 0xabcd3e00u);
}

static void test_transfers(TestState* state, KinetisK22* device) {
    CortexM4* cpu = prepare(state, device, 0xeee1u, 0x1a10u);
    cortex_m4_set_register(cpu, 1, FPSCR_FZ | FPSCR_DN | 0xffffffffu);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fpscr(cpu) == 0xf7c09f9fu);

    cpu = prepare(state, device, 0xec43u, 0x2a11u);
    cortex_m4_set_register(cpu, 2, 0x11223344u);
    cortex_m4_set_register(cpu, 3, 0x55667788u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 2) == 0x11223344u);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 3) == 0x55667788u);

    cpu = prepare(state, device, 0xec51u, 0x0a10u);
    cortex_m4_set_fp_register(cpu, 0, 0xaabbccddu);
    cortex_m4_set_fp_register(cpu, 1, 0x12345678u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == 0xaabbccddu);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 1) == 0x12345678u);

    cpu = prepare(state, device, 0xec43u, 0x2b11u);
    cortex_m4_set_register(cpu, 2, 0x01020304u);
    cortex_m4_set_register(cpu, 3, 0x50607080u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 2) == 0x01020304u);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 3) == 0x50607080u);

    cpu = prepare(state, device, 0xec51u, 0x0b10u);
    cortex_m4_set_fp_register(cpu, 0, 0x90abcdefu);
    cortex_m4_set_fp_register(cpu, 1, 0x13572468u);
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == 0x90abcdefu);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 1) == 0x13572468u);

    cpu = prepare(state, device, 0xed85u, 0x3b06u);
    cortex_m4_set_register(cpu, 5, 0x20000020u);
    cortex_m4_set_fp_register(cpu, 6, 0xabcdef01u);
    cortex_m4_set_fp_register(cpu, 7, 0x23456789u);
    run(state, cpu);
    uint32_t double_word = 0;
    TEST_EXPECT(state,
                kinetis_k22_read(device, 0x20000038u, &double_word, sizeof(double_word)));
    TEST_EXPECT(state, double_word == 0xabcdef01u);
    TEST_EXPECT(state,
                kinetis_k22_read(device, 0x2000003cu, &double_word, sizeof(double_word)));
    TEST_EXPECT(state, double_word == 0x23456789u);

    cpu = prepare(state, device, 0xed94u, 0x2b04u);
    cortex_m4_set_register(cpu, 4, 0x20000020u);
    const uint32_t double_values[2] = {0x76543210u, 0xfedcba98u};
    TEST_EXPECT(state, kinetis_k22_write(device, 0x20000030u, double_values,
                                         sizeof(double_values)));
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 4) == 0x76543210u);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 5) == 0xfedcba98u);

    cpu = prepare(state, device, 0xeca7u, 0x4a04u);
    cortex_m4_set_register(cpu, 7, 0x20000020u);
    for (uint8_t index = 8; index < 12; index++) {
        cortex_m4_set_fp_register(cpu, index, 0x10000000u + index);
    }
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 7) == 0x20000030u);
    for (uint8_t index = 0; index < 4; index++) {
        uint32_t value = 0;
        TEST_EXPECT(state, kinetis_k22_read(device, 0x20000020u + index * 4u, &value,
                                            sizeof(value)));
        TEST_EXPECT(state, value == 0x10000008u + index);
    }

    cpu = prepare(state, device, 0xecb6u, 0x2a04u);
    cortex_m4_set_register(cpu, 6, 0x20000020u);
    for (uint8_t index = 0; index < 4; index++) {
        const uint32_t value = 0x10000008u + index;
        TEST_EXPECT(state, kinetis_k22_write(device, 0x20000020u + index * 4u, &value,
                                             sizeof(value)));
    }
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 6) == 0x20000030u);
    for (uint8_t index = 0; index < 4; index++) {
        TEST_EXPECT(state,
                    cortex_m4_get_fp_register(cpu, 4u + index) == 0x10000008u + index);
    }

    cpu = prepare(state, device, 0xed2du, 0x6a04u);
    cortex_m4_set_register(cpu, 13, 0x20000100u);
    for (uint8_t index = 0; index < 4; index++) {
        cortex_m4_set_fp_register(cpu, 12u + index, 0x2000000cu + index);
    }
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 13) == 0x200000f0u);
    for (uint8_t index = 0; index < 4; index++) {
        uint32_t value = 0;
        TEST_EXPECT(state, kinetis_k22_read(device, 0x200000f0u + index * 4u, &value,
                                            sizeof(value)));
        TEST_EXPECT(state, value == 0x2000000cu + index);
    }

    cpu = prepare(state, device, 0xecbdu, 0x8a04u);
    cortex_m4_set_register(cpu, 13, 0x200000f0u);
    for (uint8_t index = 0; index < 4; index++) {
        const uint32_t value = 0x30000010u + index;
        TEST_EXPECT(state, kinetis_k22_write(device, 0x200000f0u + index * 4u, &value,
                                             sizeof(value)));
    }
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 13) == 0x20000100u);
    for (uint8_t index = 0; index < 4; index++) {
        TEST_EXPECT(state,
                    cortex_m4_get_fp_register(cpu, 16u + index) == 0x30000010u + index);
    }
}

static void test_lazy_context(TestState* state, KinetisK22* device) {
    CortexM4* cpu = prepare(state, device, 0xeeb0u, 0x0ae0u);
    cortex_m4_set_fp_register(cpu, 0, 0xdeadbeefu);
    cortex_m4_set_fp_register(cpu, 1, 0xc0600000u);
    cortex_m4_set_fpscr(cpu, FPSCR_DN);
    cpu->fpccr |= 1u;
    cpu->fpcar = 0x20000200u;
    cpu->exception_frame_depth = 1;
    cpu->exception_frames[0].address = 0x20000200u;
    cpu->exception_frames[0].extended = true;
    cpu->exception_frames[0].lazy = true;
    run(state, cpu);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 0) == 0x40600000u);
    TEST_EXPECT(state, (cpu->fpccr & 1u) == 0);
    TEST_EXPECT(state, !cpu->exception_frames[0].lazy);
    uint32_t value = 0;
    TEST_EXPECT(state, kinetis_k22_read(device, 0x20000200u, &value, sizeof(value)));
    TEST_EXPECT(state, value == 0xdeadbeefu);
    TEST_EXPECT(state, kinetis_k22_read(device, 0x20000204u, &value, sizeof(value)));
    TEST_EXPECT(state, value == 0xc0600000u);
    TEST_EXPECT(state, kinetis_k22_read(device, 0x20000240u, &value, sizeof(value)));
    TEST_EXPECT(state, value == FPSCR_DN);

    cpu = prepare(state, device, 0xeeb0u, 0x0ae0u);
    cortex_m4_set_fp_register(cpu, 0, 0x11223344u);
    cortex_m4_set_fp_register(cpu, 1, 0xc0600000u);
    cpu->fpccr |= 1u;
    cpu->fpcar = 0x30000000u;
    cpu->exception_frame_depth = 1;
    cpu->exception_frames[0].address = 0x30000000u;
    cpu->exception_frames[0].extended = true;
    cpu->exception_frames[0].lazy = true;
    cortex_m4_step(cpu);
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 13)) != 0);
    TEST_EXPECT(state, cortex_m4_get_fp_register(cpu, 0) == 0x11223344u);
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = create_device(&state);
    test_unary_and_comparison(&state, device);
    test_arithmetic_and_status(&state, device);
    test_accumulate(&state, device);
    test_conversions(&state, device);
    test_transfers(&state, device);
    test_lazy_context(&state, device);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
