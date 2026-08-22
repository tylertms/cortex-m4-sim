#include "architecture/cortex_m4/internal.h"

#include <stdint.h>
#include <string.h>

#include "test.h"

enum { XPSR_Q = 1u << 27, XPSR_GE_MASK = 15u << 16 };

static void reset_cpu(CortexM4* cpu) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->xpsr = CORTEX_M4_XPSR_T;
}

static void execute(TestState* state, CortexM4* cpu, uint16_t first, uint16_t second) {
    expect(state, cortex_m4_execute_dsp(cpu, first, second),
           "cortex_m4_execute_dsp(cpu, first, second)");
}

static void test_pack_and_extend(TestState* state, CortexM4* cpu) {
    reset_cpu(cpu);
    cpu->registers[1] = 0xa5a51234u;
    cpu->registers[2] = 0x89abcdefu;
    execute(state, cpu, 0xeac1u, 0x2002u);
    expect(state, cpu->registers[0] == 0xabcd1234u, "cpu->registers[0] == 0xabcd1234u");

    reset_cpu(cpu);
    cpu->registers[1] = 0xa5a51234u;
    cpu->registers[2] = 0x89abcdefu;
    execute(state, cpu, 0xeac1u, 0x2022u);
    expect(state, cpu->registers[0] == 0xa5a5abcdu, "cpu->registers[0] == 0xa5a5abcdu");

    reset_cpu(cpu);
    cpu->registers[1] = 0x100u;
    cpu->registers[2] = 0x00008000u;
    execute(state, cpu, 0xfa41u, 0xf092u);
    expect(state, cpu->registers[0] == 0x80u, "cpu->registers[0] == 0x80u");

    reset_cpu(cpu);
    cpu->registers[1] = 0x00010002u;
    cpu->registers[2] = 0x7f008000u;
    execute(state, cpu, 0xfa21u, 0xf092u);
    expect(state, cpu->registers[0] == 0x0080ff82u, "cpu->registers[0] == 0x0080ff82u");

    reset_cpu(cpu);
    cpu->registers[1] = 3u;
    cpu->registers[2] = 0xfffe0000u;
    execute(state, cpu, 0xfa01u, 0xf092u);
    expect(state, cpu->registers[0] == 0xfffffe03u, "cpu->registers[0] == 0xfffffe03u");

    reset_cpu(cpu);
    cpu->registers[2] = 0x807f01ffu;
    execute(state, cpu, 0xfa2fu, 0xf0b2u);
    expect(state, cpu->registers[0] == 0x0001ff80u, "cpu->registers[0] == 0x0001ff80u");

    reset_cpu(cpu);
    cpu->registers[1] = 0x100u;
    cpu->registers[2] = 0x0000fe00u;
    execute(state, cpu, 0xfa51u, 0xf092u);
    expect(state, cpu->registers[0] == 0x1feu, "cpu->registers[0] == 0x1feu");

    reset_cpu(cpu);
    cpu->registers[1] = 0x10002000u;
    cpu->registers[2] = 0x34001200u;
    execute(state, cpu, 0xfa31u, 0xf092u);
    expect(state, cpu->registers[0] == 0x10342012u, "cpu->registers[0] == 0x10342012u");

    reset_cpu(cpu);
    cpu->registers[2] = 0x89abcdefu;
    execute(state, cpu, 0xfa1fu, 0xf092u);
    expect(state, cpu->registers[0] == 0xabcdu, "cpu->registers[0] == 0xabcdu");
}

typedef struct {
    uint16_t first;
    uint16_t second;
    uint32_t left;
    uint32_t right;
    uint32_t result;
    uint32_t ge;
} ParallelCase;

static void test_parallel_normal(TestState* state, CortexM4* cpu) {
    static const ParallelCase cases[] = {
        {0xfa91u, 0xf002u, 0x7fff8000u, 0x0001ffffu, 0x80007fffu, 0x0cu},
        {0xfaa1u, 0xf002u, 0xffff0001u, 0x00020003u, 0x0002ffffu, 0x0cu},
        {0xfae1u, 0xf002u, 0x00020001u, 0x0003ffffu, 0x00030004u, 0x0fu},
        {0xfad1u, 0xf002u, 0x80000000u, 0x00017fffu, 0x7fff8001u, 0x00u},
        {0xfa81u, 0xf002u, 0x7f80ff01u, 0x01ff017fu, 0x807f0080u, 0x0bu},
        {0xfac1u, 0xf002u, 0x807f00ffu, 0x01ff0101u, 0x7f80fffeu, 0x04u},
        {0xfa91u, 0xf042u, 0xffff0001u, 0x00010002u, 0x00000003u, 0x0cu},
        {0xfaa1u, 0xf042u, 0xffff0001u, 0x00020003u, 0x0002ffffu, 0x0cu},
        {0xfae1u, 0xf042u, 0x00020001u, 0x0003ffffu, 0x00030004u, 0x00u},
        {0xfad1u, 0xf042u, 0x00010000u, 0x00000001u, 0x0001ffffu, 0x0cu},
        {0xfa81u, 0xf042u, 0xff00fe01u, 0x010201ffu, 0x0002ff00u, 0x09u},
        {0xfac1u, 0xf042u, 0xff000102u, 0x01020103u, 0xfefe00ffu, 0x0au},
    };
    for (uint32_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        reset_cpu(cpu);
        cpu->registers[1] = cases[index].left;
        cpu->registers[2] = cases[index].right;
        execute(state, cpu, cases[index].first, cases[index].second);
        expect(state, cpu->registers[0] == cases[index].result,
               "cpu->registers[0] == cases[index].result");
        expect(state, (cpu->xpsr & XPSR_GE_MASK) == cases[index].ge << 16,
               "(cpu->xpsr & XPSR_GE_MASK) == cases[index].ge << 16");
    }
}

static void test_parallel_saturating_and_halving(TestState* state, CortexM4* cpu) {
    static const ParallelCase cases[] = {
        {0xfa91u, 0xf012u, 0x7fff8000u, 0x0001ffffu, 0x7fff8000u, 0},
        {0xfaa1u, 0xf012u, 0x7fff8000u, 0x80007fffu, 0x7fff0000u, 0},
        {0xfa81u, 0xf012u, 0x7f807f80u, 0x017f01ffu, 0x7fff7f80u, 0},
        {0xfa91u, 0xf052u, 0xffff0001u, 0x00020002u, 0xffff0003u, 0},
        {0xfad1u, 0xf052u, 0x0001ffffu, 0x0002ffffu, 0x00000000u, 0},
        {0xfa81u, 0xf052u, 0xff01fe02u, 0x0201ff02u, 0xff02ff04u, 0},
        {0xfa91u, 0xf022u, 0x0001ffffu, 0x0000ffffu, 0x0000ffffu, 0},
        {0xfaa1u, 0xf022u, 0x00030001u, 0x00010003u, 0x00030000u, 0},
        {0xfa81u, 0xf022u, 0x0101ffffu, 0x0001ff01u, 0x0001ff00u, 0},
        {0xfa91u, 0xf062u, 0xffff0001u, 0x00010002u, 0x80000001u, 0},
        {0xfad1u, 0xf062u, 0x00010000u, 0x00000001u, 0x0000ffffu, 0},
        {0xfa81u, 0xf062u, 0xff010002u, 0x01010102u, 0x80010002u, 0},
    };
    for (uint32_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        reset_cpu(cpu);
        cpu->xpsr |= XPSR_Q | XPSR_GE_MASK;
        cpu->registers[1] = cases[index].left;
        cpu->registers[2] = cases[index].right;
        execute(state, cpu, cases[index].first, cases[index].second);
        expect(state, cpu->registers[0] == cases[index].result,
               "cpu->registers[0] == cases[index].result");
        expect(state, (cpu->xpsr & (XPSR_Q | XPSR_GE_MASK)) == (XPSR_Q | XPSR_GE_MASK),
               "(cpu->xpsr & (XPSR_Q | XPSR_GE_MASK)) == (XPSR_Q | XPSR_GE_MASK)");
    }
}

static void test_parallel_family_census(TestState* state, CortexM4* cpu) {
    static const ParallelCase cases[] = {
        {0xfa91u, 0xf012u, 0x00030001u, 0x00020004u, 0x00050005u, 0},
        {0xfaa1u, 0xf012u, 0x00030001u, 0x00020004u, 0x0007ffffu, 0},
        {0xfae1u, 0xf012u, 0x00030001u, 0x00020004u, 0xffff0003u, 0},
        {0xfad1u, 0xf012u, 0x00030001u, 0x00020004u, 0x0001fffdu, 0},
        {0xfa81u, 0xf012u, 0x04030201u, 0x01020304u, 0x05050505u, 0},
        {0xfac1u, 0xf012u, 0x04030201u, 0x01020304u, 0x0301fffdu, 0},
        {0xfa91u, 0xf052u, 0x00030001u, 0x00020004u, 0x00050005u, 0},
        {0xfaa1u, 0xf052u, 0x00030001u, 0x00020004u, 0x00070000u, 0},
        {0xfae1u, 0xf052u, 0x00030001u, 0x00020004u, 0x00000003u, 0},
        {0xfad1u, 0xf052u, 0x00030001u, 0x00020004u, 0x00010000u, 0},
        {0xfa81u, 0xf052u, 0x04030201u, 0x01020304u, 0x05050505u, 0},
        {0xfac1u, 0xf052u, 0x04030201u, 0x01020304u, 0x03010000u, 0},
        {0xfa91u, 0xf022u, 0x00030001u, 0x00020004u, 0x00020002u, 0},
        {0xfaa1u, 0xf022u, 0x00030001u, 0x00020004u, 0x0003ffffu, 0},
        {0xfae1u, 0xf022u, 0x00030001u, 0x00020004u, 0xffff0001u, 0},
        {0xfad1u, 0xf022u, 0x00030001u, 0x00020004u, 0x0000fffeu, 0},
        {0xfa81u, 0xf022u, 0x04030201u, 0x01020304u, 0x02020202u, 0},
        {0xfac1u, 0xf022u, 0x04030201u, 0x01020304u, 0x0100fffeu, 0},
        {0xfa91u, 0xf062u, 0x00030001u, 0x00020004u, 0x00020002u, 0},
        {0xfaa1u, 0xf062u, 0x00030001u, 0x00020004u, 0x0003ffffu, 0},
        {0xfae1u, 0xf062u, 0x00030001u, 0x00020004u, 0xffff0001u, 0},
        {0xfad1u, 0xf062u, 0x00030001u, 0x00020004u, 0x0000fffeu, 0},
        {0xfa81u, 0xf062u, 0x04030201u, 0x01020304u, 0x02020202u, 0},
        {0xfac1u, 0xf062u, 0x04030201u, 0x01020304u, 0x0100fffeu, 0},
    };
    for (uint32_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        reset_cpu(cpu);
        cpu->xpsr |= XPSR_GE_MASK;
        cpu->registers[1] = cases[index].left;
        cpu->registers[2] = cases[index].right;
        execute(state, cpu, cases[index].first, cases[index].second);
        expect(state, cpu->registers[0] == cases[index].result,
               "cpu->registers[0] == cases[index].result");
        expect(state, (cpu->xpsr & (XPSR_Q | XPSR_GE_MASK)) == XPSR_GE_MASK,
               "(cpu->xpsr & (XPSR_Q | XPSR_GE_MASK)) == XPSR_GE_MASK");
    }
}

static void test_saturation_select_and_difference(TestState* state, CortexM4* cpu) {
    reset_cpu(cpu);
    cpu->registers[1] = 0x1000u;
    execute(state, cpu, 0xf301u, 0x0007u);
    expect(state, cpu->registers[0] == 0x7fu, "cpu->registers[0] == 0x7fu");
    expect(state, (cpu->xpsr & XPSR_Q) != 0, "(cpu->xpsr & XPSR_Q) != 0");

    reset_cpu(cpu);
    cpu->registers[1] = 0xffffffffu;
    execute(state, cpu, 0xf381u, 0x0008u);
    expect(state, cpu->registers[0] == 0u, "cpu->registers[0] == 0u");
    expect(state, (cpu->xpsr & XPSR_Q) != 0, "(cpu->xpsr & XPSR_Q) != 0");

    reset_cpu(cpu);
    cpu->registers[1] = 0xffff0000u;
    execute(state, cpu, 0xf321u, 0x100fu);
    expect(state, cpu->registers[0] == 0xfffff000u, "cpu->registers[0] == 0xfffff000u");
    expect(state, (cpu->xpsr & XPSR_Q) == 0, "(cpu->xpsr & XPSR_Q) == 0");

    reset_cpu(cpu);
    cpu->registers[2] = 0x0080ff80u;
    execute(state, cpu, 0xf322u, 0x0007u);
    expect(state, cpu->registers[0] == 0x007fff80u, "cpu->registers[0] == 0x007fff80u");
    expect(state, (cpu->xpsr & XPSR_Q) != 0, "(cpu->xpsr & XPSR_Q) != 0");

    reset_cpu(cpu);
    cpu->registers[2] = 0x0080ff80u;
    execute(state, cpu, 0xf3a2u, 0x0008u);
    expect(state, cpu->registers[0] == 0x00800000u, "cpu->registers[0] == 0x00800000u");
    expect(state, (cpu->xpsr & XPSR_Q) != 0, "(cpu->xpsr & XPSR_Q) != 0");

    reset_cpu(cpu);
    cpu->registers[1] = 0x7fffffffu;
    cpu->registers[2] = 1u;
    execute(state, cpu, 0xfa82u, 0xf081u);
    expect(state, cpu->registers[0] == 0x7fffffffu, "cpu->registers[0] == 0x7fffffffu");
    expect(state, (cpu->xpsr & XPSR_Q) != 0, "(cpu->xpsr & XPSR_Q) != 0");

    reset_cpu(cpu);
    cpu->registers[1] = 0x80000000u;
    cpu->registers[2] = 1u;
    execute(state, cpu, 0xfa82u, 0xf0a1u);
    expect(state, cpu->registers[0] == 0x80000000u, "cpu->registers[0] == 0x80000000u");
    expect(state, (cpu->xpsr & XPSR_Q) != 0, "(cpu->xpsr & XPSR_Q) != 0");

    reset_cpu(cpu);
    cpu->registers[1] = 1u;
    cpu->registers[2] = 0x40000000u;
    execute(state, cpu, 0xfa82u, 0xf091u);
    expect(state, cpu->registers[0] == 0x7fffffffu, "cpu->registers[0] == 0x7fffffffu");
    expect(state, (cpu->xpsr & XPSR_Q) != 0, "(cpu->xpsr & XPSR_Q) != 0");

    reset_cpu(cpu);
    cpu->registers[1] = 4u;
    cpu->registers[2] = 3u;
    execute(state, cpu, 0xfa82u, 0xf0b1u);
    expect(state, cpu->registers[0] == 0xfffffffeu, "cpu->registers[0] == 0xfffffffeu");

    reset_cpu(cpu);
    cpu->xpsr |= 0xau << 16;
    cpu->registers[1] = 0x11223344u;
    cpu->registers[2] = 0xaabbccddu;
    execute(state, cpu, 0xfaa1u, 0xf082u);
    expect(state, cpu->registers[0] == 0x11bb33ddu, "cpu->registers[0] == 0x11bb33ddu");

    reset_cpu(cpu);
    cpu->registers[1] = 0x001020ffu;
    cpu->registers[2] = 0x10200001u;
    execute(state, cpu, 0xfb71u, 0xf002u);
    expect(state, cpu->registers[0] == 0x13eu, "cpu->registers[0] == 0x13eu");

    reset_cpu(cpu);
    cpu->registers[1] = 0x001020ffu;
    cpu->registers[2] = 0x10200001u;
    cpu->registers[3] = 7u;
    execute(state, cpu, 0xfb71u, 0x3002u);
    expect(state, cpu->registers[0] == 0x145u, "cpu->registers[0] == 0x145u");
}

typedef struct {
    uint16_t first;
    uint16_t second;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t result;
    bool q;
} MultiplyCase;

static void test_short_multiply(TestState* state, CortexM4* cpu) {
    static const MultiplyCase cases[] = {
        {0xfb11u, 0x3002u, 0x0000fffdu, 0x00000007u, 100u, 79u, false},
        {0xfb11u, 0x3012u, 0x00000003u, 0xfffe0000u, 100u, 94u, false},
        {0xfb11u, 0x3022u, 0xfffd0000u, 0x00000007u, 100u, 79u, false},
        {0xfb11u, 0x3032u, 0xfffd0000u, 0xfffe0000u, 100u, 106u, false},
        {0xfb31u, 0x3002u, 0x00020000u, 0x0000fffdu, 100u, 94u, false},
        {0xfb31u, 0x3012u, 0x00020000u, 0xfffd0000u, 100u, 94u, false},
        {0xfb31u, 0xf002u, 0x00020000u, 0x0000fffdu, 0, 0xfffffffau, false},
        {0xfb21u, 0x3002u, 0x00020003u, 0x00040005u, 100u, 123u, false},
        {0xfb21u, 0x3012u, 0x00020003u, 0x00040005u, 100u, 122u, false},
        {0xfb21u, 0xf002u, 0x7fff7fffu, 0x7fff7fffu, 0, 0x7ffe0002u, false},
        {0xfb21u, 0xf002u, 0x80008000u, 0x80008000u, 0, 0x80000000u, true},
        {0xfb41u, 0x3002u, 0x00020003u, 0x00040005u, 100u, 107u, false},
        {0xfb41u, 0x3012u, 0x00020003u, 0x00040005u, 100u, 102u, false},
        {0xfb41u, 0xf002u, 0x00020003u, 0x00040005u, 0, 7u, false},
        {0xfb01u, 0x3012u, 7u, 9u, 100u, 37u, false},
    };
    for (uint32_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        reset_cpu(cpu);
        cpu->registers[1] = cases[index].r1;
        cpu->registers[2] = cases[index].r2;
        cpu->registers[3] = cases[index].r3;
        execute(state, cpu, cases[index].first, cases[index].second);
        expect(state, cpu->registers[0] == cases[index].result,
               "cpu->registers[0] == cases[index].result");
        expect(state, ((cpu->xpsr & XPSR_Q) != 0) == cases[index].q,
               "((cpu->xpsr & XPSR_Q) != 0) == cases[index].q");
    }
}

static void test_long_multiply(TestState* state, CortexM4* cpu) {
    reset_cpu(cpu);
    cpu->registers[0] = 10u;
    cpu->registers[3] = 1u;
    cpu->registers[1] = 0x0000fffdu;
    cpu->registers[2] = 7u;
    execute(state, cpu, 0xfbc1u, 0x0382u);
    expect(state, cpu->registers[0] == 0xfffffff5u, "cpu->registers[0] == 0xfffffff5u");
    expect(state, cpu->registers[3] == 0u, "cpu->registers[3] == 0u");

    reset_cpu(cpu);
    cpu->registers[0] = 10u;
    cpu->registers[3] = 1u;
    cpu->registers[1] = 0x00020003u;
    cpu->registers[2] = 0x00040005u;
    execute(state, cpu, 0xfbc1u, 0x03c2u);
    expect(state, cpu->registers[0] == 33u, "cpu->registers[0] == 33u");
    expect(state, cpu->registers[3] == 1u, "cpu->registers[3] == 1u");

    reset_cpu(cpu);
    cpu->registers[0] = 10u;
    cpu->registers[3] = 1u;
    cpu->registers[1] = 0x00020003u;
    cpu->registers[2] = 0x00040005u;
    execute(state, cpu, 0xfbd1u, 0x03d2u);
    expect(state, cpu->registers[0] == 12u, "cpu->registers[0] == 12u");
    expect(state, cpu->registers[3] == 1u, "cpu->registers[3] == 1u");

    reset_cpu(cpu);
    cpu->registers[0] = 0xffffffffu;
    cpu->registers[3] = 0xffffffffu;
    cpu->registers[1] = 2u;
    cpu->registers[2] = 3u;
    execute(state, cpu, 0xfbe1u, 0x0362u);
    expect(state, cpu->registers[0] == 4u, "cpu->registers[0] == 4u");
    expect(state, cpu->registers[3] == 2u, "cpu->registers[3] == 2u");

    reset_cpu(cpu);
    cpu->registers[1] = 0x40000000u;
    cpu->registers[2] = 0x40000000u;
    cpu->registers[3] = 1u;
    execute(state, cpu, 0xfb51u, 0x3002u);
    expect(state, cpu->registers[0] == 0x10000001u, "cpu->registers[0] == 0x10000001u");

    reset_cpu(cpu);
    cpu->registers[1] = 0x40000000u;
    cpu->registers[2] = 0x40000000u;
    execute(state, cpu, 0xfb51u, 0xf012u);
    expect(state, cpu->registers[0] == 0x10000000u, "cpu->registers[0] == 0x10000000u");

    reset_cpu(cpu);
    cpu->registers[1] = 0x40000000u;
    cpu->registers[2] = 0x40000000u;
    cpu->registers[3] = 0x20000000u;
    execute(state, cpu, 0xfb61u, 0x3002u);
    expect(state, cpu->registers[0] == 0x10000000u, "cpu->registers[0] == 0x10000000u");

    static const uint16_t half_long_opcodes[] = {0x0382u, 0x0392u, 0x03a2u, 0x03b2u};
    static const uint32_t half_long_results[] = {15u, 12u, 10u, 8u};
    for (uint32_t index = 0; index < sizeof(half_long_opcodes) / sizeof(half_long_opcodes[0]);
         index++) {
        reset_cpu(cpu);
        cpu->registers[1] = 0x00020003u;
        cpu->registers[2] = 0x00040005u;
        execute(state, cpu, 0xfbc1u, half_long_opcodes[index]);
        expect(state, cpu->registers[0] == half_long_results[index],
               "cpu->registers[0] == half_long_results[index]");
        expect(state, cpu->registers[3] == 0u, "cpu->registers[3] == 0u");
    }

    static const uint16_t dual_long_first[] = {0xfbc1u, 0xfbc1u, 0xfbd1u, 0xfbd1u};
    static const uint16_t dual_long_second[] = {0x03c2u, 0x03d2u, 0x03c2u, 0x03d2u};
    static const uint32_t dual_long_results[] = {23u, 22u, 7u, 2u};
    for (uint32_t index = 0; index < sizeof(dual_long_first) / sizeof(dual_long_first[0]);
         index++) {
        reset_cpu(cpu);
        cpu->registers[1] = 0x00020003u;
        cpu->registers[2] = 0x00040005u;
        execute(state, cpu, dual_long_first[index], dual_long_second[index]);
        expect(state, cpu->registers[0] == dual_long_results[index],
               "cpu->registers[0] == dual_long_results[index]");
        expect(state, cpu->registers[3] == 0u, "cpu->registers[3] == 0u");
    }

    static const uint16_t most_significant_first[] = {0xfb51u, 0xfb51u, 0xfb51u,
                                                      0xfb51u, 0xfb61u, 0xfb61u};
    static const uint16_t most_significant_second[] = {0x3002u, 0x3012u, 0xf002u,
                                                       0xf012u, 0x3002u, 0x3012u};
    static const uint32_t most_significant_results[] = {0u, 1u, 0xffffffffu, 0u, 1u, 2u};
    for (uint32_t index = 0;
         index < sizeof(most_significant_first) / sizeof(most_significant_first[0]); index++) {
        reset_cpu(cpu);
        cpu->registers[1] = 1u;
        cpu->registers[2] = 0x80000000u;
        cpu->registers[3] = 1u;
        execute(state, cpu, most_significant_first[index], most_significant_second[index]);
        expect(state, cpu->registers[0] == most_significant_results[index],
               "cpu->registers[0] == most_significant_results[index]");
    }
}

static void test_invalid_encodings(TestState* state, CortexM4* cpu) {
    static const uint16_t cases[][2] = {
        {0xeacdu, 0x2002u}, {0xeac1u, 0x2d02u}, {0xeac1u, 0x2012u},       {0xfa4du, 0xf082u},
        {0xfa9du, 0xf002u}, {0xfa91u, 0xfd02u}, {0xfa91u, 0xf002u | 13u}, {0xfa71u, 0xf002u},
        {0xf30du, 0x0007u}, {0xf32du, 0x0007u}, {0xfa82u, 0xf08du},       {0xfaadu, 0xf082u},
        {0xfb7du, 0xf002u}, {0xfb1du, 0x3002u}, {0xfb2du, 0x3002u},       {0xfb11u, 0xd002u},
        {0xfb31u, 0xf022u}, {0xfbc1u, 0x00c2u}, {0xfb61u, 0xf002u},       {0xfb01u, 0xf012u},
    };
    for (uint32_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        reset_cpu(cpu);
        expect(state, !cortex_m4_execute_dsp(cpu, cases[index][0], cases[index][1]),
               "!cortex_m4_execute_dsp(cpu, cases[index][0], cases[index][1])");
    }
}

int main(void) {
    TestState state = {0};
    CortexM4 cpu;
    test_pack_and_extend(&state, &cpu);
    test_parallel_normal(&state, &cpu);
    test_parallel_saturating_and_halving(&state, &cpu);
    test_parallel_family_census(&state, &cpu);
    test_saturation_select_and_difference(&state, &cpu);
    test_short_multiply(&state, &cpu);
    test_long_multiply(&state, &cpu);
    test_invalid_encodings(&state, &cpu);
    return test_finish(&state);
}
