#include "device/kinetis_k22/timing/api.h"

#include "test.h"

enum {
    CENSUS_SIM = 0x40047000u,
    CENSUS_MCG = 0x40064000u,
    CENSUS_OSC = 0x40065000u,
    CENSUS_LLWU = 0x4007c000u,
    CENSUS_PMC = 0x4007d000u,
    CENSUS_SMC = 0x4007e000u,
    CENSUS_RCM = 0x4007f000u,
    CENSUS_PIT = 0x40037000u,
    CENSUS_LPTMR = 0x40040000u,
    CENSUS_RTC = 0x4003d000u,
    CENSUS_PDB = 0x40036000u,
    CENSUS_WDOG = 0x40052000u,
    CENSUS_EWM = 0x40061000u,
};

typedef struct {
    uint32_t random;
    uint32_t reads;
    uint32_t writes;
    uint64_t fingerprint;
} RegisterStateCensus;

static uint32_t next_random(RegisterStateCensus* census) {
    uint32_t value = census->random;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    census->random = value;
    return value;
}

static void mix(RegisterStateCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static uint32_t address_for(uint32_t random) {
    static const uint32_t bases[] = {CENSUS_SIM, CENSUS_MCG,  CENSUS_OSC, CENSUS_LLWU,  CENSUS_PMC,
                                     CENSUS_SMC, CENSUS_RCM,  CENSUS_PIT, CENSUS_LPTMR, CENSUS_RTC,
                                     CENSUS_PDB, CENSUS_WDOG, CENSUS_EWM};
    static const uint32_t spans[] = {0x2000u, 14u,   4u,     16u,    4u,    4u, 16u,
                                     0x140u,  0x10u, 0x805u, 0x1a0u, 0x18u, 6u};
    const uint8_t region = (uint8_t)(random % (sizeof(bases) / sizeof(bases[0])));
    return bases[region] + ((random >> 8u) % spans[region]);
}

static void randomize_state(K22Timing* timing, RegisterStateCensus* census, uint32_t random) {
    const uint32_t second = next_random(census);
    timing->debug_halted = (random & 1u) != 0u;
    timing->sim_sopt1 = random;
    timing->sim_sopt2 = second;
    timing->sim_sopt4 = next_random(census);
    timing->sim_sopt7 = next_random(census);
    timing->sim_scgc3 = random;
    timing->sim_scgc4 = second;
    timing->sim_scgc5 = random ^ second;
    timing->sim_scgc6 = ~random;
    timing->sim_scgc7 = ~second;
    timing->sim_clkdiv1 = next_random(census);
    for (uint8_t index = 0u; index < sizeof(timing->mcg); index++)
        timing->mcg[index] = (uint8_t)next_random(census);
    timing->osc_cr = (uint8_t)random;
    timing->osc_div = (uint8_t)second;
    for (uint8_t index = 0u; index < sizeof(timing->llwu); index++)
        timing->llwu[index] = (uint8_t)next_random(census);
    for (uint8_t index = 0u; index < sizeof(timing->pmc); index++)
        timing->pmc[index] = (uint8_t)next_random(census);
    for (uint8_t index = 0u; index < sizeof(timing->smc); index++)
        timing->smc[index] = (uint8_t)next_random(census);
    for (uint8_t index = 0u; index < sizeof(timing->rcm); index++)
        timing->rcm[index] = (uint8_t)next_random(census);
    timing->cpu_sleeping = (random & 2u) != 0u;
    timing->deep_sleeping = (random & 4u) != 0u;
    timing->pit_mcr = second;
    for (uint8_t channel = 0u; channel < 4u; channel++) {
        const uint32_t value = next_random(census);
        timing->pit[channel].load = value;
        timing->pit[channel].current = value >> 8u;
        timing->pit[channel].control = value >> 16u;
        timing->pit[channel].flag = (value & 1u) != 0u;
    }
    timing->pit_remainder = random & 0xffffu;
    timing->lptmr_csr = random;
    timing->lptmr_psr = second;
    timing->lptmr_cmr = next_random(census);
    timing->lptmr_counter = (uint16_t)next_random(census);
    timing->lptmr_latched_counter = (uint16_t)next_random(census);
    timing->lptmr_remainder = random & 0xffffu;
    timing->lptmr_filter_remainder = second & 0xffffu;
    timing->lptmr_filter_ticks = next_random(census) & 0xffffu;
    timing->lptmr_observed_active = (random & 8u) != 0u;
    timing->rtc_tsr = random;
    timing->rtc_tpr = (uint16_t)second;
    timing->rtc_tar = next_random(census);
    timing->rtc_tcr = next_random(census);
    timing->rtc_cr = next_random(census);
    timing->rtc_sr = next_random(census);
    timing->rtc_lr = next_random(census);
    timing->rtc_ier = next_random(census);
    timing->rtc_war = next_random(census);
    timing->rtc_rar = next_random(census);
    timing->rtc_remainder = random & 0xffffu;
    timing->rtc_subsecond_ticks = second & 0xffffu;
    timing->pdb_sc = random;
    timing->pdb_mod = (uint16_t)second;
    timing->pdb_counter = (uint16_t)next_random(census);
    timing->pdb_idly = (uint16_t)next_random(census);
    timing->pdb_registers[(random >> 16u) % 104u] = second;
    timing->pdb_remainder = random & 0xffffu;
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    RegisterStateCensus census = {UINT32_C(0x9e3779b9), 0u, 0u, UINT64_C(14695981039346656037)};
    K22Timing timing;
    expect(&state,
           k22_timing_init(&timing, k22_profile_get(K22_PROFILE_MK22FN1M012), 16000000u, 32768u,
                           (K22TimingSignals){0}),
           "initialize timing register state census");
    for (uint32_t iteration = 0u; iteration < 100000u; iteration++) {
        const uint32_t random = next_random(&census);
        randomize_state(&timing, &census, random);
        const uint32_t address = address_for(random);
        const uint8_t size = (uint8_t)(random % 6u);
        uint32_t value = UINT32_MAX;
        const bool read = k22_timing_read(&timing, address, size, &value);
        const bool written =
            k22_timing_write(&timing, address, size, random ^ UINT32_C(0xa5a55a5a));
        k22_timing_advance(&timing, (random & 31u) + 1u);
        census.reads += read;
        census.writes += written;
        mix(&census, address);
        mix(&census, value);
        mix(&census, read | (written << 1u));
        mix(&census,
            timing.pit[0].current ^ timing.lptmr_counter ^ timing.rtc_tsr ^ timing.pdb_counter);
    }
    expect(&state,
           census.reads == 9044u && census.writes == 12102u &&
               census.fingerprint == UINT64_C(17480889887381082607),
           "timing register state census matches");
    return test_finish(&state);
}
