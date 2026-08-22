#include "device/kinetis_k22/timing/internal.h"

#include "test.h"

typedef struct {
    uint32_t cases;
    uint32_t signals;
    uint64_t fingerprint;
} FtmStateCensus;

static void mix(FtmStateCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static void configure_case(K22FtmState* ftm, uint32_t selector) {
    static const uint16_t bounds[] = {0u, 1u, 50u, 100u, 500u, 1000u, UINT16_MAX};
    static const uint32_t channel_modes[] = {0u,    4u,    8u,    0x10u, 0x14u,
                                             0x18u, 0x20u, 0x24u, 0x28u, 0x30u};
    memset(ftm, 0, sizeof(*ftm));
    ftm->initial = bounds[(selector >> 1u) % (sizeof(bounds) / sizeof(bounds[0]))];
    ftm->modulo = bounds[(selector >> 4u) % (sizeof(bounds) / sizeof(bounds[0]))];
    ftm->counter = bounds[(selector >> 7u) % (sizeof(bounds) / sizeof(bounds[0]))];
    ftm->sc = ((selector >> 10u) & 3u) << 3u;
    ftm->sc |= ((selector >> 12u) & 1u) << 5u;
    ftm->sc |= selector & 7u;
    ftm->quadrature_capable = (selector & (1u << 13u)) != 0u;
    ftm->counting_down = (selector & (1u << 14u)) != 0u;
    ftm->fault_output_active = (selector & (1u << 15u)) != 0u;
    ftm->fault_release_pending = (selector & (1u << 16u)) != 0u;
    ftm->software_sync_pending = (selector & (1u << 17u)) != 0u;
    ftm->hardware_sync_pending = (selector & (1u << 18u)) != 0u;
    ftm->hardware_trigger_pending_mask = (uint8_t)(selector >> 19u);
    ftm->registers[0] = selector >> 3u;
    ftm->registers[1] = selector >> 5u;
    ftm->registers[3] = selector >> 7u;
    ftm->registers[4] = selector;
    ftm->registers[5] = selector >> 9u;
    ftm->registers[6] = selector >> 11u;
    ftm->registers[7] = selector >> 13u;
    ftm->registers[8] = selector >> 15u;
    ftm->registers[9] = selector;
    ftm->registers[10] = selector >> 2u;
    ftm->registers[11] = selector >> 4u;
    ftm->registers[12] = selector >> 6u;
    ftm->registers[13] = selector >> 8u;
    ftm->registers[14] = selector;
    ftm->registers[15] = selector >> 10u;
    ftm->registers[16] = selector;
    ftm->registers[17] = selector >> 12u;
    ftm->initial_buffer = bounds[(selector >> 15u) % (sizeof(bounds) / sizeof(bounds[0]))];
    ftm->modulo_buffer = bounds[(selector >> 18u) % (sizeof(bounds) / sizeof(bounds[0]))];
    ftm->initial_pending = (selector & (1u << 21u)) != 0u;
    ftm->modulo_pending = (selector & (1u << 22u)) != 0u;
    ftm->outmask_pending = (selector & (1u << 23u)) != 0u;
    ftm->invctrl_pending = (selector & (1u << 24u)) != 0u;
    ftm->swoctrl_pending = (selector & (1u << 25u)) != 0u;
    for (uint8_t channel = 0u; channel < 8u; channel++) {
        const uint32_t shifted = selector >> channel;
        ftm->channel_sc[channel] =
            channel_modes[shifted % (sizeof(channel_modes) / sizeof(channel_modes[0]))];
        ftm->channel_value[channel] =
            bounds[(shifted >> 4u) % (sizeof(bounds) / sizeof(bounds[0]))];
        ftm->channel_value_buffer[channel] =
            bounds[(shifted >> 7u) % (sizeof(bounds) / sizeof(bounds[0]))];
        ftm->channel_value_pending[channel] = (shifted & 1u) != 0u;
        ftm->channel_input[channel] = (shifted & 2u) != 0u;
        ftm->channel_filtered_input[channel] = (shifted & 4u) != 0u;
        ftm->channel_output[channel] = (shifted & 8u) != 0u;
        ftm->channel_deadtime_output[channel] = (shifted & 16u) != 0u;
    }
    for (uint8_t input = 0u; input < 4u; input++) {
        ftm->fault_input[input] = (selector & (1u << input)) != 0u;
        ftm->fault_filtered_input[input] = (selector & (1u << (input + 4u))) != 0u;
        ftm->fault_input_age[input] = selector >> (input + 8u);
    }
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    FtmStateCensus census = {0u, 0u, UINT64_C(14695981039346656037)};
    K22Timing timing;
    expect(&state,
           k22_timing_init(&timing, k22_profile_get(K22_PROFILE_MK22FN1M012), 16000000u, 32768u,
                           (K22TimingSignals){0}),
           "initialize FTM state census");
    timing.sim_scgc3 = UINT32_MAX;
    timing.sim_scgc6 = UINT32_MAX;
    timing.core_clock_hz = 120000000u;
    timing.bus_clock_hz = 60000000u;
    for (uint32_t selector = 0u; selector < 100000u; selector++) {
        const uint8_t instance = (uint8_t)(selector & 3u);
        K22FtmState* ftm = &timing.ftm[instance];
        configure_case(ftm, selector * UINT32_C(2654435761));
        const uint8_t channel =
            (uint8_t)((selector >> 2u) % k22_timing_internal_ftm_channel_count(instance));
        const uint8_t input = (uint8_t)((selector >> 6u) & 3u);
        const bool high = (selector & 1u) != 0u;
        const bool accepted_input = k22_timing_set_ftm_input(&timing, instance, channel, high);
        const bool accepted_fault = k22_timing_set_ftm_fault(&timing, instance, input, high);
        const bool accepted_trigger =
            k22_timing_trigger_ftm_hardware(&timing, instance, (uint8_t)((selector >> 8u) % 3u));
        bool output = false;
        const bool accepted_output = k22_timing_get_ftm_output(&timing, instance, channel, &output);
        k22_timing_internal_ftm_apply_software_sync(ftm);
        k22_timing_internal_ftm_update_fault_status(ftm);
        k22_timing_internal_advance_ftm(&timing, instance, (selector & 0x3ffu) + 1u);
        census.cases++;
        census.signals += accepted_input + accepted_fault + accepted_trigger + accepted_output;
        mix(&census, accepted_input | (accepted_fault << 1u) | (accepted_trigger << 2u) |
                         (accepted_output << 3u) | (output << 4u));
        mix(&census, ftm->sc ^ ftm->counter ^ ftm->registers[8]);
        mix(&census, ftm->channel_value[channel] ^ ((uint32_t)ftm->channel_output[channel] << 24u));
        mix(&census, k22_timing_internal_ftm_active_fault_mask(ftm));
        mix(&census, k22_timing_internal_ftm_output_compare_mode(ftm, channel));
        mix(&census, k22_timing_internal_ftm_input_capture_mode(ftm, channel));
    }
    K22Timing incomplete = timing;
    incomplete.profile = NULL;
    bool output = false;
    mix(&census, k22_timing_set_ftm_input(&incomplete, 0u, 0u, false));
    mix(&census, k22_timing_set_ftm_fault(&incomplete, 0u, 0u, false));
    mix(&census, k22_timing_trigger_ftm_hardware(&incomplete, 0u, 0u));
    mix(&census, k22_timing_get_ftm_output(&incomplete, 0u, 0u, &output));
    expect(&state,
           census.cases == 100000u && census.signals == 400000u &&
               census.fingerprint == UINT64_C(11262719079239042716),
           "FTM state census matches");
    return test_finish(&state);
}
