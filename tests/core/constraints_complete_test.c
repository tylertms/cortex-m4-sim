#include "cortex_m4_internal.h"

#include <stddef.h>

#include "test.h"

typedef struct {
    uint16_t first;
    uint16_t second;
    uint8_t it_state;
    bool wide;
    CortexM4InstructionDisposition expected;
} OpcodeConstraint;

static void test_opcode_matrix(TestState* state, CortexM4* cpu) {
    const OpcodeConstraint cases[] = {
        {0xbe00u, 0, 0, false, CORTEX_M4_INSTRUCTION_BREAKPOINT},
        {0xbeffu, 0, 0x0cu, false, CORTEX_M4_INSTRUCTION_BREAKPOINT},
        {0xbf08u, 0, 0, false, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xbfe8u, 0, 0, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xbff8u, 0, 0, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xbf18u, 0, 0x08u, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xb100u, 0, 0x08u, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xb662u, 0, 0x08u, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xdf00u, 0, 0x08u, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xd000u, 0, 0x0cu, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xd000u, 0, 0x08u, false, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xe000u, 0, 0x0cu, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xbd00u, 0, 0x0cu, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0x4487u, 0, 0x0cu, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xb400u, 0, 0, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xbc00u, 0, 0, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xc000u, 0, 0, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xc001u, 0, 0, false, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xc103u, 0, 0, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xb660u, 0, 0, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xb664u, 0, 0, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0x4778u, 0, 0, false, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf000u, 0xd000u, 0x0cu, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf000u, 0xd000u, 0x08u, true, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xf240u, 0x0f00u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf240u, 0x0d00u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf200u, 0x0f00u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf3efu, 0x8f00u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf38fu, 0x8800u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf04fu, 0x1000u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf041u, 0x0f01u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf00fu, 0x0001u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xea01u, 0x000du, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xea0du, 0x0001u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xfa0du, 0xf001u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xfa01u, 0xf00du, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xfabdu, 0xf08du, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xfab1u, 0xfd81u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf30du, 0x0007u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf301u, 0x0d07u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf361u, 0x2007u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf36du, 0x200fu, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf341u, 0x70ffu, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xfbbdu, 0xf0f1u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xfba1u, 0x2203u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xfb01u, 0xd003u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xfb01u, 0x2003u, 0, true, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xe951u, 0x0001u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xe951u, 0x0102u, 0, true, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xe9f1u, 0x1200u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xe931u, 0x0003u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xe8b1u, 0x2002u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xe8b1u, 0x0005u, 0, true, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xe85du, 0x0f00u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xe851u, 0x0f00u, 0, true, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xe841u, 0x1100u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xe841u, 0x2300u, 0, true, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xe8ddu, 0x0f4fu, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xe8d1u, 0x0f4fu, 0, true, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xe8cdu, 0x0f43u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xe8c1u, 0x0f43u, 0, true, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xe8d1u, 0xf00du, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xe8d1u, 0xf002u, 0x0cu, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xe8b1u, 0x8004u, 0x0cu, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf841u, 0xd000u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf8cfu, 0x0000u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf84fu, 0x0001u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf851u, 0xf00du, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf851u, 0xde04u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf851u, 0xfe04u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf841u, 0x0041u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf851u, 0x1b04u, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf8d1u, 0xf000u, 0x0cu, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf000u, 0x8000u, 0x0cu, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf011u, 0x0f01u, 0x0cu, true, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xf093u, 0x4f7fu, 0x0cu, true, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xf3bfu, 0x8f1fu, 0, true, CORTEX_M4_INSTRUCTION_UNDEFINED},
        {0xf3bfu, 0x8f4fu, 0, true, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xf3bfu, 0x8f4fu, 0x0cu, true, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xf361u, 0x200fu, 0, true, CORTEX_M4_INSTRUCTION_EXECUTE},
        {0xfbb1u, 0xf0f2u, 0, true, CORTEX_M4_INSTRUCTION_EXECUTE},
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        cpu->it_state = cases[index].it_state;
        const CortexM4InstructionDisposition actual =
            cortex_m4_check_instruction_constraints(cpu, cases[index].first,
                                                    cases[index].second, cases[index].wide);
        if (actual != cases[index].expected) {
            fprintf(stderr, "constraint case %zu: %04x %04x expected %u got %u\n", index,
                    cases[index].first, cases[index].second,
                    (unsigned)cases[index].expected, (unsigned)actual);
        }
        TEST_EXPECT(state, actual == cases[index].expected);
    }
}

int main(void) {
    TestState state = {0};
    CortexM4 cpu = {0};
    test_opcode_matrix(&state, &cpu);
    TEST_EXPECT(&state, cortex_m4_check_instruction_constraints(NULL, 0, 0, false) ==
                            CORTEX_M4_INSTRUCTION_UNDEFINED);
    return test_finish(&state);
}
