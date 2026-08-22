#include "device/kinetis_k22/timing/support.h"

typedef struct {
    uint32_t random;
    uint32_t reads;
    uint32_t writes;
    uint32_t signals;
    uint64_t fingerprint;
} TimingStateCensus;

static uint32_t next_random(TimingStateCensus* census) {
    uint32_t value = census->random;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    census->random = value;
    return value;
}

static void mix(TimingStateCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static void randomize_ftm(K22FtmState* ftm, TimingStateCensus* census) {
    ftm->sc = next_random(census);
    ftm->counter = (uint16_t)next_random(census);
    ftm->modulo = (uint16_t)next_random(census);
    ftm->initial = (uint16_t)next_random(census);
    ftm->modulo_buffer = (uint16_t)next_random(census);
    ftm->initial_buffer = (uint16_t)next_random(census);
    ftm->outmask_buffer = next_random(census);
    ftm->invctrl_buffer = next_random(census);
    ftm->swoctrl_buffer = next_random(census);
    for (uint8_t channel = 0u; channel < 8u; channel++) {
        const uint32_t random = next_random(census);
        ftm->channel_sc[channel] = random;
        ftm->channel_value[channel] = (uint16_t)(random >> 8u);
        ftm->channel_value_buffer[channel] = (uint16_t)(random >> 16u);
        ftm->channel_input[channel] = (random & 1u) != 0u;
        ftm->channel_filtered_input[channel] = (random & 2u) != 0u;
        ftm->channel_output[channel] = (random & 4u) != 0u;
        ftm->channel_deadtime_output[channel] = (random & 8u) != 0u;
        ftm->channel_value_pending[channel] = (random & 16u) != 0u;
        ftm->channel_input_age[channel] = random >> 5u;
        ftm->channel_deadtime_remaining[channel] = random >> 9u;
    }
    for (uint8_t index = 0u; index < 20u; index++)
        ftm->registers[index] = next_random(census);
    for (uint8_t input = 0u; input < 4u; input++) {
        const uint32_t random = next_random(census);
        ftm->fault_input[input] = (random & 1u) != 0u;
        ftm->fault_filtered_input[input] = (random & 2u) != 0u;
        ftm->fault_input_age[input] = random >> 2u;
    }
    const uint32_t flags = next_random(census);
    ftm->outmask_pending = (flags & 1u) != 0u;
    ftm->invctrl_pending = (flags & 2u) != 0u;
    ftm->swoctrl_pending = (flags & 4u) != 0u;
    ftm->modulo_pending = (flags & 8u) != 0u;
    ftm->initial_pending = (flags & 16u) != 0u;
    ftm->software_sync_pending = (flags & 32u) != 0u;
    ftm->hardware_sync_pending = (flags & 64u) != 0u;
    ftm->write_protection_read = (flags & 128u) != 0u;
    ftm->fault_aggregate_read = (flags & 256u) != 0u;
    ftm->fault_output_active = (flags & 512u) != 0u;
    ftm->fault_release_pending = (flags & 1024u) != 0u;
    ftm->quadrature_capable = (flags & 2048u) != 0u;
    ftm->counting_down = (flags & 4096u) != 0u;
    ftm->hardware_trigger_pending_mask = (uint8_t)(flags >> 16u);
    ftm->fault_flags_read_mask = (uint8_t)(flags >> 24u);
}

void k22_timing_test_test_state_census(TestState* state, K22Timing* timing) {
    TimingStateCensus census = {UINT32_C(0xb7e15162), 0u, 0u, 0u, UINT64_C(14695981039346656037)};
    timing->sim_scgc3 = UINT32_MAX;
    timing->sim_scgc6 = UINT32_MAX;
    for (uint32_t iteration = 0u; iteration < 20000u; iteration++) {
        const uint8_t instance = (uint8_t)(iteration & 3u);
        K22FtmState* ftm = &timing->ftm[instance];
        randomize_ftm(ftm, &census);
        const uint32_t random = next_random(&census);
        const uint32_t address =
            FTM0_SC + (uint32_t)instance * 0x1000u + ((random >> 8u) % 41u) * 4u;
        const uint8_t size = (uint8_t[]){1u, 2u, 4u}[random % 3u];
        const bool written = k22_timing_write(timing, address, size, random);
        uint32_t value = UINT32_MAX;
        const bool read = k22_timing_read(timing, address, size, &value);
        const bool input = k22_timing_set_ftm_input(timing, instance, (uint8_t)(random >> 16u) % 9u,
                                                    (random & 1u) != 0u);
        const bool fault = k22_timing_set_ftm_fault(timing, instance, (uint8_t)(random >> 20u) % 5u,
                                                    (random & 2u) != 0u);
        const bool trigger =
            k22_timing_trigger_ftm_hardware(timing, instance, (uint8_t)(random >> 24u) % 4u);
        bool high = false;
        const bool output =
            k22_timing_get_ftm_output(timing, instance, (uint8_t)(random >> 12u) % 9u, &high);
        k22_timing_advance(timing, (random & 31u) + 1u);
        census.writes += written;
        census.reads += read;
        census.signals += input + fault + trigger + output;
        mix(&census, written);
        mix(&census, read);
        mix(&census, value);
        mix(&census, input | (fault << 1u) | (trigger << 2u) | (output << 3u) | (high << 4u));
        mix(&census, ftm->sc ^ ftm->counter ^ ftm->registers[0]);
    }
    expect(state,
           census.reads == 3752u && census.writes == 3752u && census.signals == 53454u &&
               census.fingerprint == UINT64_C(16015189613287118939),
           "timing state census matches");
}
