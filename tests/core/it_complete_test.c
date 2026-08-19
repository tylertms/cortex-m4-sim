#include "cortex_m4_sim/cortex_m4.h"

#include <stdbool.h>
#include <stdint.h>

#include "cortex_m4_internal.h"
#include "test.h"

static const uint32_t xpsr_nzcv = UINT32_C(0xf0000000);

static CortexM4FlagWrite expected_narrow_write(uint16_t opcode) {
    const uint16_t family = opcode & 0xe000u;
    if (family == 0x0000u) {
        return CORTEX_M4_FLAGS_IMPLICIT;
    }
    if (family == 0x2000u) {
        return (opcode & 0x1800u) == 0x0800u ? CORTEX_M4_FLAGS_EXPLICIT
                                             : CORTEX_M4_FLAGS_IMPLICIT;
    }
    if ((opcode & 0xfc00u) == 0x4000u) {
        const uint16_t comparison_operations = (1u << 8) | (1u << 10) | (1u << 11);
        return (comparison_operations & (uint16_t)(1u << ((opcode >> 6) & 15u))) != 0
                   ? CORTEX_M4_FLAGS_EXPLICIT
                   : CORTEX_M4_FLAGS_IMPLICIT;
    }
    return (opcode & 0xff00u) == 0x4500u ? CORTEX_M4_FLAGS_EXPLICIT
                                         : CORTEX_M4_FLAGS_UNCHANGED;
}

static CortexM4FlagWrite expected_wide_write(uint16_t first, uint16_t second) {
    if ((first & 0xfff0u) == 0xf380u && (second & 0xff00u) == 0x8800u &&
        (second & 0xffu) <= 3u) {
        return CORTEX_M4_FLAGS_EXPLICIT;
    }
    if (first == 0xeef1u && second == 0xfa10u) {
        return CORTEX_M4_FLAGS_EXPLICIT;
    }
    const bool data_processing =
        ((first & 0xfa00u) == 0xf000u || (first & 0xfe00u) == 0xea00u) &&
        (second & 0x8000u) == 0;
    return data_processing && (first & 0x10u) != 0 ? CORTEX_M4_FLAGS_EXPLICIT
                                                   : CORTEX_M4_FLAGS_UNCHANGED;
}

static void test_narrow_classification(TestState* state) {
    for (uint32_t opcode = 0; opcode <= UINT16_MAX; opcode++) {
        TEST_EXPECT(state, cortex_m4_it_flag_write((uint16_t)opcode, 0, false) ==
                               expected_narrow_write((uint16_t)opcode));
    }
}

static void test_wide_classification(TestState* state) {
    static const uint16_t seconds[] = {
        0x0000u, 0x7fffu, 0x8000u, 0x8800u, 0x8801u, 0x8802u,
        0x8803u, 0x8804u, 0x8a00u, 0xfa10u, 0xffffu,
    };
    for (uint32_t first = 0; first <= UINT16_MAX; first++) {
        for (size_t index = 0; index < sizeof(seconds) / sizeof(seconds[0]); index++) {
            TEST_EXPECT(state,
                        cortex_m4_it_flag_write((uint16_t)first, seconds[index], true) ==
                            expected_wide_write((uint16_t)first, seconds[index]));
        }
    }
    for (uint32_t selector = 0; selector <= UINT8_MAX; selector++) {
        const uint16_t second = (uint16_t)(0x8800u | selector);
        TEST_EXPECT(state, cortex_m4_it_flag_write(0xf380u, second, true) ==
                               expected_wide_write(0xf380u, second));
    }
    for (uint32_t core = 0; core < 16; core++) {
        const uint16_t second = (uint16_t)((core << 12) | 0x0a10u);
        TEST_EXPECT(state, cortex_m4_it_flag_write(0xeef1u, second, true) ==
                               expected_wide_write(0xeef1u, second));
    }
}

static uint8_t expected_advance(uint8_t state) {
    if (state == 0 || (state & 7u) == 0) {
        return 0;
    }
    return (uint8_t)((state & 0xe0u) | ((state << 1) & 0x1fu));
}

static void test_state_advance(TestState* state) {
    CortexM4 cpu = {0};
    for (uint32_t initial = 0; initial <= UINT8_MAX; initial++) {
        cpu.it_state = (uint8_t)initial;
        uint8_t expected = (uint8_t)initial;
        for (uint8_t step = 0; step < 5; step++) {
            TEST_EXPECT(state, cpu.it_state == expected);
            cortex_m4_it_advance(&cpu);
            expected = expected_advance(expected);
        }
        TEST_EXPECT(state, cpu.it_state == 0);
    }
    cortex_m4_it_advance(NULL);
}

static void test_condition_sequence(TestState* state) {
    CortexM4 cpu = {0};
    for (uint32_t flags = 0; flags < 16; flags++) {
        cpu.xpsr = CORTEX_M4_XPSR_T | (flags << 28);
        for (uint8_t condition = 0; condition < 14; condition++) {
            for (uint8_t mask = 1; mask < 16; mask++) {
                cpu.it_state = (uint8_t)((condition << 4) | mask);
                while (cpu.it_state != 0) {
                    TEST_EXPECT(state, cortex_m4_it_condition_passed(&cpu) ==
                                           cortex_m4_condition_passed(
                                               &cpu, (uint8_t)(cpu.it_state >> 4)));
                    cortex_m4_it_advance(&cpu);
                }
                TEST_EXPECT(state, cortex_m4_it_condition_passed(&cpu));
            }
        }
    }
    TEST_EXPECT(state, !cortex_m4_it_condition_passed(NULL));
}

static void test_flag_preservation(TestState* state) {
    CortexM4 cpu = {0};
    const uint32_t previous = CORTEX_M4_XPSR_T | CORTEX_M4_XPSR_N | CORTEX_M4_XPSR_C;
    cpu.xpsr = CORTEX_M4_XPSR_T | CORTEX_M4_XPSR_Q | 0x000f0000u | CORTEX_M4_XPSR_Z |
               CORTEX_M4_XPSR_V;
    cortex_m4_it_preserve_flags(&cpu, 0x1800u, 0, false, true, previous);
    TEST_EXPECT(state, (cpu.xpsr & xpsr_nzcv) == (previous & xpsr_nzcv));
    TEST_EXPECT(state, (cpu.xpsr & CORTEX_M4_XPSR_Q) != 0);
    TEST_EXPECT(state, (cpu.xpsr & 0x000f0000u) == 0x000f0000u);

    cpu.xpsr = CORTEX_M4_XPSR_Z;
    cortex_m4_it_preserve_flags(&cpu, 0x1800u, 0, false, false, previous);
    TEST_EXPECT(state, cpu.xpsr == CORTEX_M4_XPSR_Z);

    cpu.xpsr = CORTEX_M4_XPSR_V;
    cortex_m4_it_preserve_flags(&cpu, 0x2800u, 0, false, true, previous);
    TEST_EXPECT(state, cpu.xpsr == CORTEX_M4_XPSR_V);

    cpu.xpsr = CORTEX_M4_XPSR_C;
    cortex_m4_it_preserve_flags(&cpu, 0xf110u, 0x0000u, true, true, previous);
    TEST_EXPECT(state, cpu.xpsr == CORTEX_M4_XPSR_C);
    cortex_m4_it_preserve_flags(NULL, 0, 0, false, true, previous);
}

typedef struct {
    uint8_t bytes[4096];
} ExceptionMemory;

static bool exception_memory_read(void* context, uint32_t address, uint8_t size,
                                  CortexM4Access access, uint32_t* value) {
    (void)access;
    ExceptionMemory* memory = context;
    if (address < 0x20000000u || address + size > 0x20000000u + sizeof(memory->bytes) ||
        value == NULL) {
        return false;
    }
    *value = 0;
    for (uint8_t index = 0; index < size; index++) {
        *value |= (uint32_t)memory->bytes[address - 0x20000000u + index] << (index * 8u);
    }
    return true;
}

static bool exception_memory_write(void* context, uint32_t address, uint8_t size,
                                   CortexM4Access access, uint32_t value) {
    (void)access;
    ExceptionMemory* memory = context;
    if (address < 0x20000000u || address + size > 0x20000000u + sizeof(memory->bytes)) {
        return false;
    }
    for (uint8_t index = 0; index < size; index++) {
        memory->bytes[address - 0x20000000u + index] = (uint8_t)(value >> (index * 8u));
    }
    return true;
}

static uint32_t packed_it_state(uint8_t it_state) {
    return (((uint32_t)it_state & 0x3u) << 25) | (((uint32_t)it_state & 0xfcu) << 8);
}

static void test_exception_round_trip(TestState* state) {
    ExceptionMemory memory = {0};
    CortexM4 cpu_value = {0};
    CortexM4* cpu = &cpu_value;
    cpu->bus.context = &memory;
    cpu->bus.read = exception_memory_read;
    cpu->bus.write = exception_memory_write;
    for (uint8_t condition = 0; condition < 14; condition++) {
        for (uint8_t mask = 1; mask < 16; mask++) {
            cpu->msp = 0x20001000u;
            cpu->xpsr = CORTEX_M4_XPSR_T;
            cpu->exception_depth = 0;
            cpu->exception_frame_depth = 0;
            cpu->irq_active[0] = 0;
            const uint8_t it_state = (uint8_t)((condition << 4) | mask);
            cpu->it_state = it_state;
            cpu->xpsr |= CORTEX_M4_XPSR_N | CORTEX_M4_XPSR_C;
            uint32_t stack_pointer = cpu->msp;
            uint32_t return_value = 0;
            TEST_EXPECT(state, cortex_m4_system_stack_exception_frame(cpu, &stack_pointer,
                                                                      &return_value));
            TEST_EXPECT(state, cpu->exception_frame_depth == 1);
            TEST_EXPECT(state, cpu->exception_frames[0].it_state == it_state);
            TEST_EXPECT(state, (cpu->exception_frames[0].stacked_xpsr & 0x0600fc00u) ==
                                   packed_it_state(it_state));
            cpu->it_state = 0;
            cpu->xpsr = CORTEX_M4_XPSR_T | 16u;
            cpu->exception_depth = 1;
            cpu->active_exceptions[0] = 16u;
            cpu->irq_active[0] = 1u;
            TEST_EXPECT(state, cortex_m4_system_unstack_exception_frame(cpu, &stack_pointer,
                                                                        return_value, 16u));
            cortex_m4_exception_advanced_commit_return(cpu, 16u, true);
            TEST_EXPECT(state, cpu->it_state == it_state);
            TEST_EXPECT(state, (cpu->xpsr & (CORTEX_M4_XPSR_N | CORTEX_M4_XPSR_C)) ==
                                   (CORTEX_M4_XPSR_N | CORTEX_M4_XPSR_C));
            TEST_EXPECT(state, cpu->exception_depth == 0);
            TEST_EXPECT(state, cpu->exception_frame_depth == 0);
            TEST_EXPECT(state, cpu->irq_active[0] == 0);
        }
    }
}

static void test_invalid_it_constraints(TestState* state) {
    CortexM4 cpu = {0};
    for (uint8_t condition = 0; condition < 16; condition++) {
        for (uint8_t mask = 1; mask < 16; mask++) {
            const uint16_t opcode = (uint16_t)(0xbf00u | (condition << 4) | mask);
            cpu.it_state = 0;
            const CortexM4InstructionDisposition expected =
                condition < 14 ? CORTEX_M4_INSTRUCTION_EXECUTE
                               : CORTEX_M4_INSTRUCTION_UNDEFINED;
            TEST_EXPECT(state, cortex_m4_check_instruction_constraints(&cpu, opcode, 0,
                                                                       false) == expected);
            cpu.it_state = 0x08u;
            TEST_EXPECT(state,
                        cortex_m4_check_instruction_constraints(&cpu, opcode, 0, false) ==
                            CORTEX_M4_INSTRUCTION_UNDEFINED);
        }
    }
}

int main(void) {
    TestState state = {0};
    test_narrow_classification(&state);
    test_wide_classification(&state);
    test_state_advance(&state);
    test_condition_sequence(&state);
    test_flag_preservation(&state);
    test_exception_round_trip(&state);
    test_invalid_it_constraints(&state);
    return test_finish(&state);
}
