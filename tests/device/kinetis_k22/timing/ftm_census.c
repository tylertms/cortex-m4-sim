#include "device/kinetis_k22/timing/internal.h"

typedef struct {
    uint32_t accepted_reads;
    uint32_t accepted_writes;
    uint32_t asserted_outputs;
    uint64_t fingerprint;
} FtmCensus;

static void mix(FtmCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static void register_census(K22Timing* timing, FtmCensus* census) {
    static const uint8_t sizes[] = {1u, 2u, 4u};
    static const uint32_t values[] = {
        0u,
        1u,
        2u,
        3u,
        0x20u,
        0x40u,
        0x80u,
        0x100u,
        0xffffu,
        UINT32_MAX,
        UINT32_C(0x55555555),
        UINT32_C(0xaaaaaaaa),
    };
    timing->sim_scgc3 |= 1u << 25u;
    timing->sim_scgc6 |= 0x07000040u;
    for (uint8_t instance = 0u; instance < 4u; instance++) {
        const uint32_t base = FTM0_SC + (uint32_t)instance * 0x1000u;
        for (uint32_t offset = 0u; offset <= FTM0_SWOCTRL - FTM0_SC; offset += 4u) {
            for (size_t size_index = 0u; size_index < sizeof(sizes) / sizeof(sizes[0]);
                 size_index++) {
                for (size_t value_index = 0u; value_index < sizeof(values) / sizeof(values[0]);
                     value_index++) {
                    const uint32_t address = base + offset;
                    const uint8_t size = sizes[size_index];
                    const uint32_t value = values[value_index];
                    const bool written = k22_timing_write(timing, address, size, value);
                    uint32_t read_value = 0u;
                    const bool read = k22_timing_read(timing, address, size, &read_value);
                    census->accepted_writes += written;
                    census->accepted_reads += read;
                    mix(census, address);
                    mix(census, size);
                    mix(census, value);
                    mix(census, written);
                    mix(census, read);
                    mix(census, read_value);
                    k22_timing_advance(timing, (uint32_t)(value_index + 1u));
                }
            }
        }
    }
}

static void runtime_census(K22Timing* timing, FtmCensus* census) {
    timing->sim_scgc3 |= 1u << 25u;
    timing->sim_scgc6 |= 0x07000040u;
    for (uint8_t instance = 0u; instance < 4u; instance++) {
        K22FtmState* ftm = &timing->ftm[instance];
        const uint8_t channels = instance == 0u || instance == 3u ? 8u : 2u;
        for (uint8_t alignment = 0u; alignment < 2u; alignment++) {
            for (uint8_t edges = 0u; edges < 4u; edges++) {
                memset(ftm, 0, sizeof(*ftm));
                ftm->quadrature_capable = instance == 1u || instance == 2u;
                ftm->sc = 0x08u | (alignment != 0u ? 0x20u : 0u);
                ftm->initial = 0u;
                ftm->modulo = 4u;
                ftm->counter = alignment != 0u ? 5u : 0u;
                for (uint8_t channel = 0u; channel < channels; channel++) {
                    ftm->channel_sc[channel] = 0x20u | ((uint32_t)edges << 2u);
                    ftm->channel_value[channel] = (uint16_t)(channel % 6u);
                }
                k22_timing_advance(timing, 32u);
                for (uint8_t channel = 0u; channel < channels; channel++) {
                    census->asserted_outputs += ftm->channel_output[channel];
                    mix(census, ftm->channel_output[channel]);
                    mix(census, ftm->channel_sc[channel]);
                }
            }
        }

        memset(ftm, 0, sizeof(*ftm));
        ftm->sc = 0x08u;
        ftm->initial = 0u;
        ftm->modulo = 4u;
        ftm->registers[4] = 1u;
        ftm->channel_sc[0] = 0x20u;
        ftm->channel_value[0] = 1u;
        ftm->channel_value[1] = 3u;
        k22_timing_advance(timing, 16u);
        mix(census, ftm->channel_output[0]);

        if (channels > 4u) {
            ftm->channel_sc[4] = 4u;
            mix(census, k22_timing_set_ftm_input(timing, instance, 4u, true));
            k22_timing_advance(timing, 16u);
            mix(census, ftm->channel_value[4]);
        }
    }
}

static void signal_census(K22Timing* timing, FtmCensus* census) {
    for (uint8_t instance = 0u; instance < 5u; instance++) {
        for (uint8_t channel = 0u; channel < 10u; channel++) {
            for (uint8_t high = 0u; high < 2u; high++) {
                const bool input = k22_timing_set_ftm_input(timing, instance, channel, high != 0u);
                bool output_high = false;
                const bool output =
                    k22_timing_get_ftm_output(timing, instance, channel, &output_high);
                census->asserted_outputs += output && output_high;
                mix(census, input);
                mix(census, output);
                mix(census, output_high);
            }
        }
        for (uint8_t input = 0u; input < 5u; input++) {
            for (uint8_t high = 0u; high < 2u; high++) {
                mix(census, k22_timing_set_ftm_fault(timing, instance, input, high != 0u));
            }
        }
        for (uint8_t trigger = 0u; trigger < 4u; trigger++) {
            mix(census, k22_timing_trigger_ftm_hardware(timing, instance, trigger));
        }
    }
}

void k22_timing_test_test_ftm_census(TestState* state, K22Timing* timing) {
    FtmCensus census = {0u, 0u, 0u, UINT64_C(14695981039346656037)};
    k22_timing_reset(timing, 0x82u, 0u);
    register_census(timing, &census);
    runtime_census(timing, &census);
    signal_census(timing, &census);
    expect(state,
           census.accepted_reads == 1080u && census.accepted_writes == 1080u &&
               census.asserted_outputs == 0u && census.fingerprint == UINT64_C(4855070775294413002),
           "FTM census matches");
}
