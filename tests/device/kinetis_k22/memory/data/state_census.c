#include "device/kinetis_k22/memory/data/internal.h"

typedef struct {
    uint32_t random;
    uint32_t reads;
    uint32_t writes;
    uint32_t signals;
    uint64_t fingerprint;
} DataStateCensus;

static uint32_t next_random(DataStateCensus* census) {
    uint32_t value = census->random;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    census->random = value;
    return value;
}

static void mix(DataStateCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static uint32_t address_for(uint32_t random) {
    static const uint32_t bases[] = {DMA, DMAMUX, ADC0, ADC1, DAC0, DAC1, RNG, CRC, CMP0, VREF};
    static const uint32_t spans[] = {0x1200u, 16u, 0x70u, 0x70u, 0x24u, 0x24u, 16u, 12u, 24u, 2u};
    const uint8_t region = (uint8_t)(random % (sizeof(bases) / sizeof(bases[0])));
    return bases[region] + ((random >> 8u) % spans[region]);
}

static void randomize_state(K22Data* data, DataStateCensus* census, uint32_t random) {
    const uint32_t second = next_random(census);
    const uint8_t channel = (uint8_t)((random >> 16u) % data->dma_channel_count);
    const uint32_t tcd = 0x1000u + (uint32_t)channel * DMA_TCD_SIZE;
    for (uint8_t index = 0u; index < DMA_TCD_SIZE; index++)
        data->dma[tcd + index] = (uint8_t)next_random(census);
    for (uint8_t index = 0u; index < 0x40u; index++)
        data->dma[index] = (uint8_t)next_random(census);
    data->dma_requests = (uint16_t)random;
    data->dma_hardware_requests = (uint16_t)second;
    data->dma_trigger_waiting = (uint16_t)(random ^ second);
    data->dma_active = 0u;
    data->dma_half = (uint16_t)(random >> 8u);
    data->dma_last_channel = channel;
    data->debug_halted = (random & 1u) != 0u;
    for (uint8_t index = 0u; index < data->dmamux_count; index++) {
        data->dmamux[index] = (uint8_t)next_random(census);
        data->dma_request_source[index] = (uint8_t)next_random(census);
    }
    for (uint8_t instance = 0u; instance < data->adc_count; instance++) {
        for (uint8_t index = 0u; index < ADC_REGISTER_SIZE; index++)
            data->adc[instance].registers[index] = (uint8_t)next_random(census);
        data->adc[instance].remaining_cycles = random & 31u;
        data->adc[instance].active_slot = (uint8_t)(second & 1u);
        data->adc[instance].converting = (random & 2u) != 0u;
    }
    for (uint8_t instance = 0u; instance < data->dac_count; instance++) {
        for (uint8_t index = 0u; index < DAC_REGISTER_SIZE; index++)
            data->dac[instance].registers[index] = (uint8_t)next_random(census);
        data->dac[instance].output = (uint16_t)next_random(census);
    }
    for (uint8_t instance = 0u; instance < data->cmp_count; instance++)
        for (uint8_t index = 0u; index < CMP_REGISTER_SIZE; index++)
            data->cmp[instance].registers[index] = (uint8_t)next_random(census);
    data->vref[0] = (uint8_t)random;
    data->vref[1] = (uint8_t)second;
    data->vref_cycles = random & 0xffu;
    data->rng_control = random;
    data->rng_status = second;
    data->rng_error = next_random(census);
    data->rng_output = next_random(census);
    data->rng_state = next_random(census) | 1u;
    data->rng_cycles = random & 0xffu;
    data->crc_value = random;
    data->crc_polynomial = second | 1u;
    data->crc_control = next_random(census);
}

void k22_data_test_test_state_census(TestState* state) {
    static TestBus bus;
    memset(&bus, 0, sizeof(bus));
    DataStateCensus census = {UINT32_C(0x3c6ef372), 0u, 0u, 0u, UINT64_C(14695981039346656037)};
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    if (data == NULL)
        return;
    for (uint32_t iteration = 0u; iteration < 50000u; iteration++) {
        const uint32_t random = next_random(&census);
        randomize_state(data, &census, random);
        const uint32_t address = address_for(random);
        const uint8_t size = (uint8_t)(random % 6u);
        uint32_t value = UINT32_MAX;
        const bool read = k22_data_read(data, address, size, &value);
        const bool written = k22_data_write(data, address, size, random ^ UINT32_C(0xa5a55a5a));
        const bool requested = k22_data_dma_request(data, (uint8_t)(random >> 24u));
        const bool triggered = k22_data_dma_trigger(data, (uint8_t)((random >> 20u) % 18u));
        const bool adc = k22_data_set_adc_input(data, (uint8_t)((random >> 8u) % 3u),
                                                (uint8_t)((random >> 16u) % 34u), (uint16_t)random);
        const bool cmp = k22_data_set_cmp_input(data, (uint8_t)((random >> 10u) % 4u),
                                                (uint8_t)((random >> 18u) % 10u), (uint8_t)random);
        uint16_t dac_value = 0u;
        const bool dac = k22_data_get_dac_output(data, (uint8_t)((random >> 12u) % 3u), &dac_value);
        k22_data_adc_trigger(data, (uint8_t)((random >> 14u) % 3u));
        k22_data_adc_pretrigger(data, (uint8_t)((random >> 16u) % 3u),
                                (uint8_t)((random >> 22u) % 4u));
        k22_data_dac_trigger(data, (uint8_t)((random >> 18u) % 3u));
        k22_data_advance(data, (random & 31u) + 1u);
        census.reads += read;
        census.writes += written;
        census.signals += requested + triggered + adc + cmp + dac;
        mix(&census, address);
        mix(&census, value ^ dac_value);
        mix(&census, read | (written << 1u) | (requested << 2u) | (triggered << 3u) | (adc << 4u) |
                         (cmp << 5u) | (dac << 6u));
        mix(&census, data->dma_requests ^ data->rng_status ^ data->crc_value);
    }
    expect(state,
           census.reads == 19524u && census.writes == 19053u && census.signals == 86424u &&
               census.fingerprint == UINT64_C(5457665838010798238),
           "data state census matches");
    k22_data_destroy(data);
}
