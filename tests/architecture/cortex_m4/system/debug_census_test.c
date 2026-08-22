#include "architecture/cortex_m4/internal.h"

#include <string.h>

#include "test.h"

typedef struct {
    uint32_t random;
    uint32_t accepted;
    uint32_t rejected;
    uint32_t outside;
    uint64_t fingerprint;
} DebugCensus;

static uint32_t next_random(DebugCensus* census) {
    uint32_t value = census->random;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    census->random = value;
    return value;
}

static void mix(DebugCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static void record(DebugCensus* census, CortexM4SystemAccess result, uint32_t value) {
    census->accepted += result == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    census->rejected += result == CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    census->outside += result == CORTEX_M4_SYSTEM_ACCESS_OUTSIDE;
    mix(census, result);
    mix(census, value);
}

static void register_census(CortexM4* cpu, DebugCensus* census) {
    static const uint32_t bases[] = {
        0xdffffffcu, 0xe0000000u, 0xe0001000u, 0xe0002000u,
        0xe000edf0u, 0xe000fff0u, 0xe0040000u, 0xe0100000u,
    };
    for (uint8_t state = 0u; state < 8u; state++) {
        cpu->xpsr = CORTEX_M4_XPSR_T | (state & 1u ? 0u : 3u);
        cpu->control = state & 2u ? CORTEX_M4_CONTROL_SPSEL : 0u;
        cpu->debug.demcr = state & 4u ? 1u << 24u : 0u;
        cpu->debug.itm_locked = (state & 1u) != 0u;
        cpu->debug.dwt_locked = (state & 2u) != 0u;
        cpu->debug.fpb_locked = (state & 4u) != 0u;
        cpu->debug.tpiu_locked = (state & 1u) == 0u;
        cpu->debug.itm_trace_control = UINT32_MAX;
        cpu->debug.itm_trace_enable = state & 2u ? UINT32_MAX : 0u;
        for (size_t base_index = 0u; base_index < sizeof(bases) / sizeof(bases[0]); base_index++) {
            const uint32_t limit =
                base_index == 1u || base_index == 2u || base_index == 3u || base_index == 6u
                    ? 0x1000u
                    : 0x20u;
            for (uint32_t offset = 0u; offset < limit; offset += 4u) {
                for (uint8_t byte = 0u; byte < 4u; byte++) {
                    for (uint8_t size = 0u; size <= 5u; size++) {
                        const uint32_t address = bases[base_index] + offset + byte;
                        uint32_t value = UINT32_C(0xa5a55a5a);
                        record(census, cortex_m4_debug_read(cpu, address, size, &value), value);
                        record(census,
                               cortex_m4_debug_write(cpu, address, size,
                                                     UINT32_C(0x5aa5a55a) ^ address ^ state),
                               0u);
                    }
                }
            }
        }
    }
}

static void event_census(CortexM4* cpu, DebugCensus* census) {
    cpu->debug.demcr = UINT32_MAX;
    cpu->debug.dwt_control = UINT32_MAX;
    cpu->debug.fpb_control = 1u;
    for (uint32_t iteration = 0u; iteration < 20000u; iteration++) {
        const uint32_t random = next_random(census);
        CortexM4DwtComparator* comparator = &cpu->debug.dwt_comparators[random % 4u];
        comparator->comparator = random ^ UINT32_C(0x20000000);
        comparator->mask = (random >> 8u) % 34u;
        comparator->function = random >> 16u;
        cpu->debug.dwt_cycle_count = random;
        cpu->debug.halted = (random & 1u) != 0u;
        cpu->debug.step_armed = (random & 2u) != 0u;
        cortex_m4_debug_advance(cpu, (random >> 3u) & 31u, (random & 4u) != 0u);
        cortex_m4_debug_instruction_access(cpu, random);
        cortex_m4_debug_memory_access(cpu, random ^ UINT32_C(0x20000000),
                                      (uint8_t)((random >> 12u) % 6u), (random & 8u) != 0u,
                                      random ^ UINT32_C(0x55aa55aa));
        uint32_t remapped = 0u;
        mix(census, cortex_m4_debug_remap_instruction(cpu, random, &remapped));
        mix(census, remapped);
        mix(census, cortex_m4_debug_remap_literal(cpu, random, &remapped));
        mix(census, remapped);
        mix(census, cortex_m4_debug_execution_allowed(cpu));
    }
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    CortexM4 cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.xpsr = CORTEX_M4_XPSR_T;
    cortex_m4_debug_reset(&cpu);
    DebugCensus census = {UINT32_C(0x9e3779b9), 0u, 0u, 0u, UINT64_C(14695981039346656037)};
    register_census(&cpu, &census);
    event_census(&cpu, &census);
    expect(&state,
           census.accepted == 44580u && census.rejected == 1532508u && census.outside == 8064u &&
               census.fingerprint == UINT64_C(10725639757196505340),
           "debug census matches");
    return test_finish(&state);
}
