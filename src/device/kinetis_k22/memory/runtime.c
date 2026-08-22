#include "internal.h"

#include <stdlib.h>
#include <string.h>

static bool mapped_memory_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    if (data->profile->flexnvm_size != 0 && address >= data->profile->flexnvm_address &&
        address - data->profile->flexnvm_address <= data->profile->flexnvm_size - size) {
        *value = k22_data_internal_load_bytes(data->flexnvm,
                                              address - data->profile->flexnvm_address, size);
        return true;
    }
    if (data->profile->flexram_size != 0 && address >= data->profile->flexram_address &&
        address - data->profile->flexram_address <= data->profile->flexram_size - size) {
        *value = k22_data_internal_load_bytes(data->flexram,
                                              address - data->profile->flexram_address, size);
        return true;
    }
    return false;
}

static bool mapped_memory_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    if (data->profile->flexnvm_size != 0 && address >= data->profile->flexnvm_address &&
        address - data->profile->flexnvm_address <= data->profile->flexnvm_size - size) {
        uint32_t offset = address - data->profile->flexnvm_address;
        uint32_t previous = k22_data_internal_load_bytes(data->flexnvm, offset, size);
        k22_data_internal_store_bytes(data->flexnvm, offset, size, previous & value);
        return true;
    }
    if (data->profile->flexram_size != 0 && address >= data->profile->flexram_address &&
        address - data->profile->flexram_address <= data->profile->flexram_size - size) {
        k22_data_internal_store_bytes(data->flexram, address - data->profile->flexram_address, size,
                                      value);
        return true;
    }
    return false;
}

K22Data* k22_data_create(const K22Profile* profile, K22DataBus bus) {
    if (profile == NULL)
        return NULL;
    K22Data* data = calloc(1, sizeof(*data));
    if (data == NULL)
        return NULL;
    data->profile = profile;
    data->bus = bus;
    uint32_t size = 0;
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_DMAMUX, NULL, &size)) {
        data->dmamux_count = (uint8_t)(size > DMA_CHANNEL_COUNT ? DMA_CHANNEL_COUNT : size);
        data->dma_channel_count = data->dmamux_count;
    }
    uint32_t base = 0;
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_ADC0, &base, NULL))
        data->adc_base[data->adc_count++] = base;
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_ADC1, &base, NULL))
        data->adc_base[data->adc_count++] = base;
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_DAC0, &base, NULL))
        data->dac_base[data->dac_count++] = base;
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_DAC1, &base, NULL))
        data->dac_base[data->dac_count++] = base;
    for (uint8_t index = 0; index < 3; index++) {
        if (k22_data_internal_profile_block(data, (K22PeripheralId)(K22_PERIPHERAL_CMP0 + index),
                                            NULL, NULL))
            data->cmp_count++;
    }
    if (profile->flexnvm_size != 0)
        data->flexnvm = malloc(profile->flexnvm_size);
    if (profile->flexram_size != 0)
        data->flexram = malloc(profile->flexram_size);
    if ((profile->flexnvm_size != 0 && data->flexnvm == NULL) ||
        (profile->flexram_size != 0 && data->flexram == NULL)) {
        k22_data_destroy(data);
        return NULL;
    }
    if (data->flexnvm != NULL)
        memset(data->flexnvm, 0xff, profile->flexnvm_size);
    if (data->flexram != NULL)
        memset(data->flexram, 0, profile->flexram_size);
    memset(data->flash_config, 0xff, sizeof(data->flash_config));
    data->flash_config[0x0c] = 0xfeu;
    memset(data->flash_program_ifr, 0xff, sizeof(data->flash_program_ifr));
    memset(data->flash_data_ifr, 0xff, sizeof(data->flash_data_ifr));
    k22_data_reset(data);
    return data;
}

void k22_data_destroy(K22Data* data) {
    if (data == NULL)
        return;
    free(data->flexnvm);
    free(data->flexram);
    free(data);
}

void k22_data_reset(K22Data* data) {
    if (data == NULL)
        return;
    uint16_t adc_inputs[2][32];
    uint8_t cmp_inputs[3][8];
    for (uint8_t index = 0; index < 2; index++)
        memcpy(adc_inputs[index], data->adc[index].inputs, sizeof(adc_inputs[index]));
    for (uint8_t index = 0; index < 3; index++)
        memcpy(cmp_inputs[index], data->cmp[index].inputs, sizeof(cmp_inputs[index]));
    memset(data->dma, 0, sizeof(data->dma));
    memset(data->dmamux, 0, sizeof(data->dmamux));
    memset(data->adc, 0, sizeof(data->adc));
    memset(data->dac, 0, sizeof(data->dac));
    memset(data->cmp, 0, sizeof(data->cmp));
    for (uint8_t index = 0; index < 2; index++)
        memcpy(data->adc[index].inputs, adc_inputs[index], sizeof(adc_inputs[index]));
    for (uint8_t index = 0; index < 3; index++)
        memcpy(data->cmp[index].inputs, cmp_inputs[index], sizeof(cmp_inputs[index]));
    memset(data->vref, 0, sizeof(data->vref));
    data->dma_requests = 0;
    data->dma_hardware_requests = 0;
    data->dma_trigger_waiting = 0;
    data->dma_active = 0;
    data->dma_half = 0;
    memset(data->dma_request_source, UINT8_MAX, sizeof(data->dma_request_source));
    data->dma_last_channel = (uint8_t)(data->dma_channel_count - 1u);
    data->debug_halted = false;
    for (uint8_t channel = 0u; channel < data->dma_channel_count; channel++)
        data->dma[k22_data_internal_dma_priority_offset(channel)] = channel;
    data->vref_cycles = 0;
    data->rng_control = 0;
    data->rng_status = 0x00010000u;
    data->rng_error = 0;
    data->rng_output = 0;
    data->rng_state = 0x6d2b79f5u;
    data->rng_cycles = 0;
    data->crc_value = UINT32_MAX;
    data->crc_polynomial = 0x00001021u;
    data->crc_control = 0;
    if (data->flash_swap_mode == 4u) {
        data->flash_swap_current_block = data->flash_swap_next_block;
        data->flash_swap_mode = 1u;
    }
    memset(data->flash, 0, sizeof(data->flash));
    data->flash[0] = 0x80u;
    data->flash[2] = data->flash_config[0x0c];
    data->flash[3] = data->flash_config[0x0d];
    memcpy(data->flash + 0x10, data->flash_config + 8, 4);
    data->flash[0x16] = data->flash_config[0x0e];
    data->flash[0x17] = data->flash_config[0x0f];
    if (data->flexram != NULL) {
        const uint8_t depart = data->flash_data_ifr[0x3fcu];
        const bool eeprom_partitioned =
            depart != 0xffu && depart != 0x00u && depart != 0x0du && depart != 0x0fu;
        data->flash[1] = eeprom_partitioned ? 0x01u : 0x02u;
        data->flexram_eeprom = eeprom_partitioned;
    }
    if (data->flash_swap_current_block != 0u)
        data->flash[1] |= 0x08u;
    data->flash_cycles = 0;
    data->flash_busy_banks = 0u;
    data->flash_busy_start = 0u;
    data->flash_busy_length = 0u;
    data->flash_key_blocked = false;
    data->flash_partitioned = data->flash_data_ifr[0x3fcu] != 0xffu;
    if (data->flexram != NULL)
        memset(data->flexram, 0, data->profile->flexram_size);
    for (uint8_t index = 0; index < data->adc_count; index++)
        k22_data_internal_adc_reset_registers(&data->adc[index]);
    for (uint8_t index = 0; index < data->dac_count; index++) {
        data->dac[index].registers[0x20] = 0x02u;
        data->dac[index].registers[0x23] = 0x0fu;
    }
    for (uint8_t line = 0; line < K22_DATA_INTERRUPT_COUNT; line++)
        k22_data_internal_interrupt(data, (K22DataInterrupt)line, false);
}

bool k22_data_copy(K22Data* destination, const K22Data* source) {
    if (destination == NULL || source == NULL || destination->profile != source->profile)
        return false;
    K22DataBus bus = destination->bus;
    uint8_t* flexnvm = destination->flexnvm;
    uint8_t* flexram = destination->flexram;
    memcpy(destination, source, sizeof(*destination));
    destination->bus = bus;
    destination->flexnvm = flexnvm;
    destination->flexram = flexram;
    if (flexnvm != NULL)
        memcpy(flexnvm, source->flexnvm, source->profile->flexnvm_size);
    if (flexram != NULL)
        memcpy(flexram, source->flexram, source->profile->flexram_size);
    k22_data_internal_dma_update_interrupts(destination);
    return true;
}

bool k22_data_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    if (data == NULL || value == NULL || (size != 1 && size != 2 && size != 4))
        return false;
    if (mapped_memory_read(data, address, size, value))
        return true;
    if (address >= 0x400u && address - 0x400u <= sizeof(data->flash_config) - size &&
        size <= sizeof(data->flash_config)) {
        *value = k22_data_internal_load_bytes(data->flash_config, address - 0x400u, size);
        return true;
    }
    uint32_t base = 0;
    uint32_t block_size = 0;
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_DMA, &base, &block_size) &&
        address >= base && address - base < block_size)
        return k22_data_internal_dma_read(data, address, size, value);
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_DMAMUX, &base, &block_size) &&
        address >= base && address - base < block_size)
        return k22_data_internal_dmamux_read(data, address, size, value);
    for (uint8_t index = 0; index < data->adc_count; index++)
        if (address >= data->adc_base[index] && address - data->adc_base[index] < ADC_REGISTER_SIZE)
            return k22_data_internal_adc_read(data, index, address, size, value);
    for (uint8_t index = 0; index < data->dac_count; index++)
        if (address >= data->dac_base[index] && address - data->dac_base[index] < DAC_REGISTER_SIZE)
            return k22_data_internal_dac_read(data, index, address, size, value);
    for (uint8_t index = 0; index < data->cmp_count; index++)
        if (address >= CMP_BASE + (uint32_t)index * 8u &&
            address - (CMP_BASE + (uint32_t)index * 8u) < CMP_REGISTER_SIZE)
            return k22_data_internal_cmp_read(data, index, address, size, value);
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_VREF, &base, NULL) &&
        address >= VREF_BASE && address - VREF_BASE < 2u) {
        if (!k22_data_internal_valid_access(address - VREF_BASE, size, 2))
            return false;
        *value = k22_data_internal_load_bytes(data->vref, address - VREF_BASE, size);
        return true;
    }
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_RNG, &base, NULL) &&
        address >= RNG_BASE && address - RNG_BASE < 16u)
        return k22_data_internal_rng_read(data, address, size, value);
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_CRC, &base, NULL) &&
        address >= CRC_BASE && address - CRC_BASE < 12u)
        return k22_data_internal_crc_read(data, address, size, value);
    if ((k22_data_internal_profile_block(data, K22_PERIPHERAL_FTFA, &base, NULL) ||
         k22_data_internal_profile_block(data, K22_PERIPHERAL_FTFE, &base, NULL)) &&
        address >= FLASH_BASE && address - FLASH_BASE < sizeof(data->flash))
        return k22_data_internal_flash_read(data, address, size, value);
    return false;
}

bool k22_data_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    if (data == NULL || (size != 1 && size != 2 && size != 4))
        return false;
    if (mapped_memory_write(data, address, size, value))
        return true;
    if (address >= 0x400u && address - 0x400u <= sizeof(data->flash_config) - size &&
        size <= sizeof(data->flash_config))
        return false;
    uint32_t base = 0;
    uint32_t block_size = 0;
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_DMA, &base, &block_size) &&
        address >= base && address - base < block_size)
        return k22_data_internal_dma_write(data, address, size, value);
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_DMAMUX, &base, &block_size) &&
        address >= base && address - base < block_size)
        return k22_data_internal_dmamux_write(data, address, size, value);
    for (uint8_t index = 0; index < data->adc_count; index++)
        if (address >= data->adc_base[index] && address - data->adc_base[index] < ADC_REGISTER_SIZE)
            return k22_data_internal_adc_write(data, index, address, size, value);
    for (uint8_t index = 0; index < data->dac_count; index++)
        if (address >= data->dac_base[index] && address - data->dac_base[index] < DAC_REGISTER_SIZE)
            return k22_data_internal_dac_write(data, index, address, size, value);
    for (uint8_t index = 0; index < data->cmp_count; index++)
        if (address >= CMP_BASE + (uint32_t)index * 8u &&
            address - (CMP_BASE + (uint32_t)index * 8u) < CMP_REGISTER_SIZE)
            return k22_data_internal_cmp_write(data, index, address, size, value);
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_VREF, &base, NULL) &&
        address >= VREF_BASE && address - VREF_BASE < 2u) {
        if (!k22_data_internal_valid_access(address - VREF_BASE, size, 2))
            return false;
        k22_data_internal_store_bytes(data->vref, address - VREF_BASE, size, value);
        data->vref[1] &= 0xfbu;
        data->vref_cycles = (data->vref[1] & 0x80u) != 0 ? 100u : 0;
        return true;
    }
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_RNG, &base, NULL) &&
        address >= RNG_BASE && address - RNG_BASE < 16u)
        return k22_data_internal_rng_write(data, address, size, value);
    if (k22_data_internal_profile_block(data, K22_PERIPHERAL_CRC, &base, NULL) &&
        address >= CRC_BASE && address - CRC_BASE < 12u)
        return k22_data_internal_crc_write(data, address, size, value);
    if ((k22_data_internal_profile_block(data, K22_PERIPHERAL_FTFA, &base, NULL) ||
         k22_data_internal_profile_block(data, K22_PERIPHERAL_FTFE, &base, NULL)) &&
        address >= FLASH_BASE && address - FLASH_BASE < sizeof(data->flash))
        return k22_data_internal_flash_write(data, address, size, value);
    return false;
}

bool k22_data_dma_request(K22Data* data, uint8_t source) {
    if (data == NULL || !k22_data_internal_dma_source_valid(data, source))
        return false;
    bool accepted = false;
    const uint16_t enabled = (uint16_t)k22_data_internal_load_bytes(data->dma, 0x0c, 2);
    for (uint8_t channel = 0; channel < data->dmamux_count; channel++) {
        const uint8_t mux = data->dmamux[channel];
        if ((mux & 0x80u) != 0 && (mux & 0x3fu) == source && (enabled & (1u << channel)) != 0) {
            if (channel < 4u && (mux & 0x40u) != 0u)
                data->dma_trigger_waiting |= (uint16_t)(1u << channel);
            else
                k22_data_internal_dma_queue_hardware_channel(data, channel, source);
            accepted = true;
        }
    }
    return accepted;
}

bool k22_data_dma_trigger(K22Data* data, uint8_t channel) {
    if (data == NULL || channel >= 4u || channel >= data->dma_channel_count)
        return false;
    const uint8_t mux = data->dmamux[channel];
    const uint8_t source = mux & 0x3fu;
    const uint16_t enabled = (uint16_t)k22_data_internal_load_bytes(data->dma, 0x0c, 2);
    if ((mux & 0xc0u) != 0xc0u || (enabled & (1u << channel)) == 0u ||
        (!k22_data_internal_dma_source_always_enabled(data, source) &&
         (data->dma_trigger_waiting & (1u << channel)) == 0u))
        return false;
    data->dma_trigger_waiting &= (uint16_t)~(1u << channel);
    k22_data_internal_dma_queue_hardware_channel(data, channel, source);
    return true;
}

void k22_data_adc_trigger(K22Data* data, uint8_t instance) {
    k22_data_adc_pretrigger(data, instance, 0);
}

void k22_data_adc_pretrigger(K22Data* data, uint8_t instance, uint8_t pretrigger) {
    if (data != NULL && instance < data->adc_count && pretrigger < 2u &&
        (data->adc[instance].registers[0x20] & 0x40u) != 0)
        k22_data_internal_adc_start(&data->adc[instance], pretrigger);
}

bool k22_data_set_adc_input(K22Data* data, uint8_t instance, uint8_t channel, uint16_t value) {
    if (data == NULL || instance >= data->adc_count || channel >= 32)
        return false;
    data->adc[instance].inputs[channel] = value;
    return true;
}

bool k22_data_set_cmp_input(K22Data* data, uint8_t instance, uint8_t input, uint8_t value) {
    if (data == NULL || instance >= data->cmp_count || input >= 8)
        return false;
    data->cmp[instance].inputs[input] = value;
    k22_data_internal_cmp_evaluate(data, instance);
    return true;
}

bool k22_data_get_cmp_output(const K22Data* data, uint8_t instance, bool* high) {
    if (data == NULL || high == NULL || instance >= data->cmp_count)
        return false;
    *high = (data->cmp[instance].registers[3] & 1u) != 0u;
    return true;
}

bool k22_data_get_dac_output(const K22Data* data, uint8_t instance, uint16_t* value) {
    if (data == NULL || value == NULL || instance >= data->dac_count)
        return false;
    *value = data->dac[instance].output;
    return true;
}

void k22_data_dac_trigger(K22Data* data, uint8_t instance) {
    if (data == NULL || instance >= data->dac_count)
        return;
    K22Dac* dac = &data->dac[instance];
    if ((dac->registers[0x21] & 0x80u) == 0 || (dac->registers[0x22] & 0x80u) == 0)
        return;
    uint8_t pointer = (dac->registers[0x23] >> 4) & 15u;
    const uint8_t upper = dac->registers[0x23] & 15u;
    uint8_t flags = 0;
    if ((dac->registers[0x22] & 3u) == 1u) {
        if (pointer == 0) {
            flags |= 2u;
            pointer = upper;
        } else {
            pointer--;
        }
    } else {
        pointer++;
        if (pointer == upper)
            flags |= 4u;
        if (pointer > upper) {
            pointer = 0;
            flags |= 1u;
        }
    }
    dac->registers[0x23] = (uint8_t)((pointer << 4) | upper);
    k22_data_internal_dac_update_output(data, instance);
    k22_data_internal_dac_flags(data, instance, flags);
}

void k22_data_rng_seed(K22Data* data, uint32_t seed) {
    if (data != NULL)
        data->rng_state = seed == 0 ? 0x6d2b79f5u : seed;
}

bool k22_data_set_flash_configuration(K22Data* data, const uint8_t* bytes, size_t size) {
    if (data == NULL || bytes == NULL || (size != 0x0eu && size != 0x10u))
        return false;
    memset(data->flash_config, 0xff, sizeof(data->flash_config));
    memcpy(data->flash_config, bytes, size);
    memcpy(data->flash + 0x10, bytes + 8, 4);
    data->flash[2] = bytes[0x0c];
    data->flash[3] = bytes[0x0d];
    if (size == 0x10u) {
        data->flash[0x16] = bytes[0x0e];
        data->flash[0x17] = bytes[0x0f];
    }
    return true;
}

bool k22_data_flash_read(K22Data* data, bool data_flash, uint32_t offset, uint8_t size) {
    if (data == NULL)
        return false;
    const uint8_t bank = data_flash ? 2u : 1u;
    const bool same_block = data->flash_busy_banks == 3u ||
                            (offset < data->flash_busy_start + data->flash_busy_length &&
                             (uint64_t)offset + size > data->flash_busy_start);
    if (data->flash_cycles == 0u || (data->flash_busy_banks & bank) == 0u || !same_block)
        return true;
    data->flash[0] |= 0x40u;
    k22_data_internal_flash_update_interrupts(data);
    return false;
}

void k22_data_advance(K22Data* data, uint32_t cycles) {
    if (data == NULL || cycles == 0)
        return;
    uint32_t always_enabled_budget = cycles;
    while (data->dma_requests != 0u &&
           (k22_data_internal_load_bytes(data->dma, 0u, 4u) & 0x20u) == 0u &&
           !((k22_data_internal_load_bytes(data->dma, 0u, 4u) & 2u) != 0u && data->debug_halted)) {
        const uint8_t channel = k22_data_internal_dma_select_channel(data);
        if (channel == UINT8_MAX)
            break;
        if ((k22_data_internal_load_bytes(data->dma, 0u, 4u) & 4u) == 0u &&
            !k22_data_internal_dma_priorities_valid(data)) {
            k22_data_internal_dma_error(data, channel, 1u << 14u);
            data->dma_requests &= (uint16_t)~(1u << channel);
            data->dma_hardware_requests &= (uint16_t)~(1u << channel);
            break;
        }
        data->dma_requests &= (uint16_t)~(1u << channel);
        data->dma_hardware_requests &= (uint16_t)~(1u << channel);
        data->dma_last_channel = channel;
        const uint8_t source = data->dma_request_source[channel];
        data->dma_request_source[channel] = UINT8_MAX;
        const bool completed = k22_data_internal_dma_service_channel(data, channel);
        if (completed && source != UINT8_MAX && data->bus.dma_complete != NULL)
            data->bus.dma_complete(data->bus.context, source);
        if (completed && source != UINT8_MAX &&
            k22_data_internal_dma_source_always_enabled(data, source)) {
            k22_data_internal_dma_queue_always_enabled(data, channel);
            if ((data->dma_requests & (1u << channel)) != 0u && --always_enabled_budget == 0u)
                break;
        }
    }
    for (uint8_t index = 0; index < data->adc_count; index++) {
        K22Adc* adc = &data->adc[index];
        if (!adc->converting)
            continue;
        if (cycles >= adc->remaining_cycles) {
            adc->remaining_cycles = 0;
            k22_data_internal_adc_complete(data, index);
        } else {
            adc->remaining_cycles -= cycles;
        }
    }
    if (data->vref_cycles != 0) {
        if (cycles >= data->vref_cycles) {
            data->vref_cycles = 0;
            data->vref[1] |= 4u;
        } else {
            data->vref_cycles -= cycles;
        }
    }
    if ((data->rng_control & 1u) != 0 && data->rng_cycles != 0) {
        if (cycles >= data->rng_cycles) {
            data->rng_cycles = 0;
            data->rng_state = k22_data_internal_rng_next(data->rng_state);
            data->rng_output = data->rng_state;
            data->rng_status |= 1u;
            if ((data->rng_control & 2u) != 0)
                k22_data_internal_interrupt(data, K22_DATA_INTERRUPT_RNG, true);
        } else {
            data->rng_cycles -= cycles;
        }
    }
    if (data->flash_cycles != 0) {
        if (cycles >= data->flash_cycles) {
            data->flash_cycles = 0;
            data->flash_busy_banks = 0u;
            data->flash_busy_start = 0u;
            data->flash_busy_length = 0u;
            data->flash[0] |= 0x80u;
            k22_data_internal_flash_update_interrupts(data);
        } else {
            data->flash_cycles -= cycles;
        }
    }
}

void k22_data_set_debug_halted(K22Data* data, bool halted) {
    if (data != NULL)
        data->debug_halted = halted;
}
