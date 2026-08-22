#include "architecture/cortex_m4/internal.h"

#include <string.h>

#include "test.h"

typedef struct {
    uint32_t random;
    uint64_t advanced;
    uint64_t fingerprint;
} TimingCensus;

void cortex_m4_advance(CortexM4* cpu, uint32_t cycles) {
    cpu->cycles += cycles;
    TimingCensus* census = cpu->bus.context;
    census->advanced += cycles;
}

uint32_t cortex_m4_read_register_internal(const CortexM4* cpu, uint8_t index) {
    return cpu->registers[index & 15u];
}

static uint32_t next_random(TimingCensus* census) {
    uint32_t value = census->random;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    census->random = value;
    return value;
}

static void mix(TimingCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static uint32_t wait_states(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                            bool write, bool sequential) {
    TimingCensus* census = context;
    const uint32_t value = address ^ size ^ access ^ write ^ sequential ^ census->random;
    mix(census, value);
    return value & 15u;
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    TimingCensus census = {UINT32_C(0x082efa98), 0u, UINT64_C(14695981039346656037)};
    CortexM4 cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.bus.context = &census;
    cortex_m4_timing_reset(&cpu);
    cortex_m4_set_wait_states(&cpu, wait_states, &census);
    for (uint32_t iteration = 0u; iteration < 1000000u; iteration++) {
        const uint32_t random = next_random(&census);
        const uint16_t first = (uint16_t)random;
        const uint16_t second = (uint16_t)(random >> 16u);
        for (uint8_t index = 0u; index < CORTEX_M4_REGISTER_COUNT; index++)
            cpu.registers[index] = random ^ ((uint32_t)index * UINT32_C(0x9e3779b9));
        const uint32_t sequential_pc = random ^ UINT32_C(0xa5a55a5a);
        if ((random & 1u) != 0u)
            cpu.registers[15] = sequential_pc;
        if ((random & 2u) != 0u)
            cortex_m4_timing_begin_exception(&cpu);
        else
            cortex_m4_timing_begin_instruction(&cpu);
        cortex_m4_timing_prepare_instruction(&cpu, first, second, (random & 4u) != 0u);
        cortex_m4_timing_access(&cpu, random, (uint8_t)((random >> 8u) % 6u),
                                (CortexM4Access)((random >> 12u) % 5u), (random & 8u) != 0u);
        cortex_m4_timing_complete_instruction(&cpu, first, second, (random & 4u) != 0u,
                                              (random & 16u) != 0u, sequential_pc);
        if ((random & 32u) != 0u)
            cortex_m4_timing_barrier(&cpu, (CortexM4Barrier)((random >> 20u) % 3u));
        cortex_m4_timing_reserve(&cpu, random, (uint8_t)((random >> 24u) % 6u));
        mix(&census, cortex_m4_timing_consume_reservation(&cpu, random ^ (random & 64u ? 0u : 4u),
                                                          (uint8_t)((random >> 24u) % 6u)));
        cortex_m4_timing_observe_write(&cpu, random ^ UINT32_C(0x1000), (random >> 25u) & 31u);
        mix(&census, (uint32_t)cpu.cycles);
        mix(&census, cpu.timing_memory_epoch ^ cpu.timing_context_epoch);
    }
    expect(&state,
           census.advanced == UINT64_C(3524931) &&
               census.fingerprint == UINT64_C(1233295475225792319),
           "timing census matches");
    return test_finish(&state);
}
