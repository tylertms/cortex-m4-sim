#include "architecture/cortex_m4/internal.h"

#include <string.h>

#include "test.h"

typedef struct {
    uint32_t random;
    uint32_t accepted;
    uint64_t fingerprint;
} FpuCensus;

static bool bus_read(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                     uint32_t* value) {
    (void)context;
    (void)address;
    (void)size;
    (void)access;
    *value = UINT32_C(0x5aa5a55a);
    return true;
}

static bool bus_write(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                      uint32_t value) {
    (void)context;
    (void)address;
    (void)size;
    (void)access;
    (void)value;
    return true;
}

static uint32_t next_random(FpuCensus* census) {
    uint32_t value = census->random;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    census->random = value;
    return value;
}

static void mix(FpuCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static CortexM4 create_cpu(void) {
    CortexM4 cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.bus = (CortexM4Bus){NULL, bus_read, bus_write, NULL, NULL};
    cpu.cpacr = 0x00f00000u;
    cpu.xpsr = CORTEX_M4_XPSR_T;
    cpu.registers[13] = 0x20001000u;
    cpu.registers[15] = 0x100u;
    return cpu;
}

static void encoding_census(CortexM4* cpu, FpuCensus* census) {
    for (uint32_t iteration = 0u; iteration < 1000000u; iteration++) {
        const uint32_t random = next_random(census);
        const uint16_t first = (uint16_t)(0xec00u | (random & 0x03ffu));
        const uint16_t second = (uint16_t)(random >> 16u);
        const bool accepted = cortex_m4_execute_fpu(cpu, first, second);
        census->accepted += accepted;
        mix(census, accepted);
        mix(census, cpu->fpscr);
        mix(census, cpu->fp_registers[random % CORTEX_M4_FP_REGISTER_COUNT]);
        cpu->cfsr = 0u;
        cpu->system_pending = 0u;
    }
}

static void arithmetic_census(CortexM4* cpu, FpuCensus* census) {
    static const uint16_t instructions[][2] = {
        {0xee20u, 0x0a01u}, {0xee20u, 0x0a41u}, {0xee30u, 0x0a01u},
        {0xee30u, 0x0a41u}, {0xee80u, 0x0a01u}, {0xee00u, 0x0a01u},
        {0xee10u, 0x0a01u}, {0xee90u, 0x0a01u}, {0xeea0u, 0x0a01u},
    };
    static const uint32_t values[] = {
        0u,          1u,          0x007fffffu, 0x00800000u, 0x3f000000u,
        0x3f800000u, 0x40000000u, 0x7f7fffffu, 0x7f800000u, 0x7f800001u,
        0x7fc00000u, 0x80000000u, 0xff7fffffu, 0xff800000u, 0xff800001u,
    };
    static const uint32_t modes[] = {0u, 1u << 22u, 2u << 22u, 3u << 22u, 1u << 24u, 1u << 25u};
    for (size_t instruction = 0u; instruction < sizeof(instructions) / sizeof(instructions[0]);
         instruction++) {
        for (size_t left = 0u; left < sizeof(values) / sizeof(values[0]); left++) {
            for (size_t right = 0u; right < sizeof(values) / sizeof(values[0]); right++) {
                for (size_t mode = 0u; mode < sizeof(modes) / sizeof(modes[0]); mode++) {
                    cpu->fpscr = modes[mode];
                    cpu->fp_registers[0] = values[left];
                    cpu->fp_registers[1] = values[right];
                    const bool accepted = cortex_m4_execute_fpu(cpu, instructions[instruction][0],
                                                                instructions[instruction][1]);
                    census->accepted += accepted;
                    mix(census, accepted);
                    mix(census, cpu->fp_registers[0]);
                    mix(census, cpu->fpscr);
                }
            }
        }
    }
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    CortexM4 cpu = create_cpu();
    FpuCensus census = {UINT32_C(0x243f6a88), 0u, UINT64_C(14695981039346656037)};
    encoding_census(&cpu, &census);
    arithmetic_census(&cpu, &census);
    expect(&state,
           census.accepted == 40161u && census.fingerprint == UINT64_C(14537637711255369024),
           "FPU census matches");
    return test_finish(&state);
}
