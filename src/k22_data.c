#include "k22_data.h"

#include <stdlib.h>
#include <string.h>

enum {
    DMA_BASE = 0x40008000u,
    DMA_TCD_BASE = 0x40009000u,
    DMAMUX_BASE = 0x40021000u,
    ADC0_BASE = 0x4003b000u,
    ADC1_BASE_STANDARD = 0x40027000u,
    ADC1_BASE_LARGE = 0x400bb000u,
    DAC0_BASE_STANDARD = 0x4003f000u,
    DAC0_BASE_LARGE = 0x400cc000u,
    DAC1_BASE = 0x40028000u,
    RNG_BASE = 0x40029000u,
    CRC_BASE = 0x40032000u,
    FLASH_BASE = 0x40020000u,
    CMP_BASE = 0x40073000u,
    VREF_BASE = 0x40074000u,
    DMA_CHANNEL_COUNT = 16,
    DMA_TCD_SIZE = 32,
    DMA_REGISTER_SIZE = 0x1200,
    ADC_REGISTER_SIZE = 0x70,
    DAC_REGISTER_SIZE = 0x24,
    CMP_REGISTER_SIZE = 6,
};

typedef struct {
    uint8_t registers[ADC_REGISTER_SIZE];
    uint16_t inputs[32];
    uint32_t remaining_cycles;
    uint8_t active_slot;
    bool converting;
} K22Adc;

typedef struct {
    uint8_t registers[DAC_REGISTER_SIZE];
    uint16_t output;
} K22Dac;

typedef struct {
    uint8_t registers[CMP_REGISTER_SIZE];
    uint8_t inputs[8];
} K22Cmp;

struct K22Data {
    const K22Profile* profile;
    K22DataBus bus;
    uint8_t dma[DMA_REGISTER_SIZE];
    uint16_t dma_requests;
    uint16_t dma_active;
    uint16_t dma_half;
    uint8_t dmamux[DMA_CHANNEL_COUNT];
    uint8_t dmamux_count;
    K22Adc adc[2];
    uint8_t adc_count;
    uint32_t adc_base[2];
    K22Dac dac[2];
    uint8_t dac_count;
    uint32_t dac_base[2];
    K22Cmp cmp[3];
    uint8_t cmp_count;
    uint8_t vref[2];
    uint32_t vref_cycles;
    uint32_t rng_control;
    uint32_t rng_status;
    uint32_t rng_error;
    uint32_t rng_output;
    uint32_t rng_state;
    uint32_t rng_cycles;
    uint32_t crc_value;
    uint32_t crc_polynomial;
    uint32_t crc_control;
    uint8_t flash[0x20];
    uint8_t flash_config[0x10];
    uint32_t flash_cycles;
    uint8_t* flexnvm;
    uint8_t* flexram;
};

static uint32_t load_bytes(const uint8_t* bytes, uint32_t offset, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; index++)
        value |= (uint32_t)bytes[offset + index] << (8u * index);
    return value;
}

static void store_bytes(uint8_t* bytes, uint32_t offset, uint8_t size, uint32_t value) {
    for (uint8_t index = 0; index < size; index++)
        bytes[offset + index] = (uint8_t)(value >> (8u * index));
}

static bool valid_access(uint32_t offset, uint8_t size, uint32_t length) {
    return (size == 1 || size == 2 || size == 4) && offset < length &&
           size <= length - offset;
}

static void interrupt(K22Data* data, K22DataInterrupt line, bool asserted) {
    if (data->bus.interrupt != NULL)
        data->bus.interrupt(data->bus.context, line, asserted);
}

static bool profile_block(const K22Data* data, K22PeripheralId id, uint32_t* base,
                          uint32_t* size) {
    K22PeripheralBlock block;
    if (!k22_profile_peripheral_block(data->profile, id, &block))
        return false;
    if (base != NULL)
        *base = block.address;
    if (size != NULL)
        *size = block.size;
    return true;
}

static uint8_t dma_transfer_size(uint8_t encoding) {
    if (encoding <= 2)
        return (uint8_t)(1u << encoding);
    return 0;
}

static uint16_t dma_iteration_count(uint16_t value) {
    return (value & 0x8000u) != 0 ? value & 0x01ffu : value & 0x7fffu;
}

static uint8_t dma_link_channel(uint16_t value) {
    return (uint8_t)((value >> 9) & 15u);
}

static uint32_t dma_advance_address(uint32_t address, int16_t offset, uint8_t modulo) {
    const uint32_t advanced = (uint32_t)((int64_t)address + offset);
    if (modulo == 0 || modulo >= 31)
        return advanced;
    const uint32_t mask = (1u << modulo) - 1u;
    return (address & ~mask) | (advanced & mask);
}

static void dma_set_iteration_count(uint8_t* descriptor, uint32_t offset,
                                    uint16_t value) {
    uint16_t current = (uint16_t)load_bytes(descriptor, offset, 2);
    const uint16_t mask = (current & 0x8000u) != 0 ? 0x01ffu : 0x7fffu;
    current = (uint16_t)((current & ~mask) | (value & mask));
    store_bytes(descriptor, offset, 2, current);
}

static void dma_update_interrupts(K22Data* data) {
    const uint16_t pending = (uint16_t)load_bytes(data->dma, 0x24, 2);
    for (uint8_t channel = 0; channel < DMA_CHANNEL_COUNT; channel++)
        interrupt(data, (K22DataInterrupt)(K22_DATA_INTERRUPT_DMA0 + channel),
                  (pending & (1u << channel)) != 0);
    const uint16_t errors = (uint16_t)load_bytes(data->dma, 0x2c, 2);
    const uint16_t enabled = (uint16_t)load_bytes(data->dma, 0x14, 2);
    interrupt(data, K22_DATA_INTERRUPT_DMA_ERROR, (errors & enabled) != 0);
}

static void dma_error(K22Data* data, uint8_t channel, uint32_t reason) {
    uint32_t status = load_bytes(data->dma, 0x04, 4);
    status |= 0x80000000u | ((uint32_t)channel << 8) | reason;
    store_bytes(data->dma, 0x04, 4, status);
    uint16_t errors = (uint16_t)load_bytes(data->dma, 0x2c, 2);
    errors |= (uint16_t)(1u << channel);
    store_bytes(data->dma, 0x2c, 2, errors);
    if ((load_bytes(data->dma, 0x14, 2) & (1u << channel)) != 0)
        interrupt(data, K22_DATA_INTERRUPT_DMA_ERROR, true);
}

static bool dma_bus_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    return data->bus.read != NULL && data->bus.read(data->bus.context, address, size, value);
}

static bool dma_bus_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    return data->bus.write != NULL && data->bus.write(data->bus.context, address, size, value);
}

static void dma_queue_channel(K22Data* data, uint8_t channel) {
    if (channel < DMA_CHANNEL_COUNT)
        data->dma_requests |= (uint16_t)(1u << channel);
}

static bool dma_copy_descriptor(K22Data* data, uint8_t* descriptor, uint32_t address) {
    for (uint8_t offset = 0; offset < DMA_TCD_SIZE; offset += 4) {
        uint32_t value = 0;
        if (!dma_bus_read(data, address + offset, 4, &value))
            return false;
        store_bytes(descriptor, offset, 4, value);
    }
    return true;
}

static void dma_complete_major(K22Data* data, uint8_t channel, uint8_t* descriptor) {
    uint32_t source = load_bytes(descriptor, 0, 4);
    uint32_t destination = load_bytes(descriptor, 0x10, 4);
    source = (uint32_t)((int64_t)source + (int32_t)load_bytes(descriptor, 0x0c, 4));
    destination =
        (uint32_t)((int64_t)destination + (int32_t)load_bytes(descriptor, 0x18, 4));
    store_bytes(descriptor, 0, 4, source);
    store_bytes(descriptor, 0x10, 4, destination);
    uint16_t control = (uint16_t)load_bytes(descriptor, 0x1c, 2);
    const uint16_t beginning =
        dma_iteration_count((uint16_t)load_bytes(descriptor, 0x1e, 2));
    dma_set_iteration_count(descriptor, 0x16, beginning);
    if ((control & 0x10u) != 0) {
        const uint32_t next = load_bytes(descriptor, 0x18, 4);
        if (!dma_copy_descriptor(data, descriptor, next)) {
            dma_error(data, channel, 1u << 2);
            return;
        }
        control = (uint16_t)load_bytes(descriptor, 0x1c, 2);
    } else {
        control |= 0x80u;
        store_bytes(descriptor, 0x1c, 2, control);
    }
    if ((control & 0x08u) != 0) {
        uint16_t enable = (uint16_t)load_bytes(data->dma, 0x0c, 2);
        enable &= (uint16_t)~(1u << channel);
        store_bytes(data->dma, 0x0c, 2, enable);
    }
    if ((control & 0x02u) != 0) {
        uint16_t pending = (uint16_t)load_bytes(data->dma, 0x24, 2);
        pending |= (uint16_t)(1u << channel);
        store_bytes(data->dma, 0x24, 2, pending);
    }
    if ((control & 0x20u) != 0)
        dma_queue_channel(data, (uint8_t)((control >> 8) & 15u));
}

static void dma_service_channel(K22Data* data, uint8_t channel) {
    uint8_t* descriptor = data->dma + 0x1000u + (uint32_t)channel * DMA_TCD_SIZE;
    uint16_t current = (uint16_t)load_bytes(descriptor, 0x16, 2);
    uint16_t count = dma_iteration_count(current);
    const uint16_t beginning =
        dma_iteration_count((uint16_t)load_bytes(descriptor, 0x1e, 2));
    const uint32_t minor = load_bytes(descriptor, 8, 4);
    const bool minor_mapping = (load_bytes(data->dma, 0, 4) & 0x80u) != 0;
    const uint32_t bytes = minor_mapping ? minor & 0x3ffu : minor & 0x3fffffffu;
    int32_t minor_offset = 0;
    if (minor_mapping) {
        minor_offset = (int32_t)((minor >> 10) & 0xfffffu);
        if ((minor_offset & 0x80000) != 0)
            minor_offset |= (int32_t)0xfff00000u;
    }
    const uint16_t attributes = (uint16_t)load_bytes(descriptor, 6, 2);
    const uint8_t source_size = dma_transfer_size((uint8_t)((attributes >> 8) & 7u));
    const uint8_t destination_size = dma_transfer_size((uint8_t)(attributes & 7u));
    if (count == 0 || bytes == 0 || source_size == 0 || destination_size == 0 ||
        bytes % source_size != 0 || bytes % destination_size != 0) {
        dma_error(data, channel, 1u << 4);
        return;
    }
    uint32_t source = load_bytes(descriptor, 0, 4);
    uint32_t destination = load_bytes(descriptor, 0x10, 4);
    const int16_t source_offset = (int16_t)load_bytes(descriptor, 4, 2);
    const int16_t destination_offset = (int16_t)load_bytes(descriptor, 0x14, 2);
    const uint8_t source_modulo = (uint8_t)((attributes >> 11) & 31u);
    const uint8_t destination_modulo = (uint8_t)((attributes >> 3) & 31u);
    uint16_t running_control = (uint16_t)load_bytes(descriptor, 0x1c, 2);
    store_bytes(descriptor, 0x1c, 2, running_control | 0x40u);
    data->dma_active |= (uint16_t)(1u << channel);
    uint64_t transfer_buffer = 0;
    uint8_t buffered_bytes = 0;
    uint32_t source_bytes = 0;
    uint32_t destination_bytes = 0;
    while (destination_bytes < bytes) {
        while (buffered_bytes < destination_size && source_bytes < bytes) {
            uint32_t value = 0;
            if (!dma_bus_read(data, source, source_size, &value)) {
                dma_error(data, channel, 1u << 2);
                data->dma_active &= (uint16_t)~(1u << channel);
                store_bytes(descriptor, 0x1c, 2, running_control);
                return;
            }
            transfer_buffer |= (uint64_t)value << (buffered_bytes * 8u);
            buffered_bytes = (uint8_t)(buffered_bytes + source_size);
            source_bytes += source_size;
            source = dma_advance_address(source, source_offset, source_modulo);
        }
        if (buffered_bytes < destination_size ||
            !dma_bus_write(data, destination, destination_size,
                           (uint32_t)transfer_buffer)) {
            dma_error(data, channel, 1u << 2);
            data->dma_active &= (uint16_t)~(1u << channel);
            store_bytes(descriptor, 0x1c, 2, running_control);
            return;
        }
        destination =
            dma_advance_address(destination, destination_offset, destination_modulo);
        destination_bytes += destination_size;
        transfer_buffer >>= destination_size * 8u;
        buffered_bytes = (uint8_t)(buffered_bytes - destination_size);
    }
    if (minor_mapping && (minor & 0x80000000u) != 0)
        source = (uint32_t)((int64_t)source + minor_offset);
    if (minor_mapping && (minor & 0x40000000u) != 0)
        destination = (uint32_t)((int64_t)destination + minor_offset);
    data->dma_active &= (uint16_t)~(1u << channel);
    store_bytes(descriptor, 0x1c, 2, running_control);
    store_bytes(descriptor, 0, 4, source);
    store_bytes(descriptor, 0x10, 4, destination);
    count--;
    dma_set_iteration_count(descriptor, 0x16, count);
    if ((current & 0x8000u) != 0)
        dma_queue_channel(data, dma_link_channel(current));
    const uint16_t control = (uint16_t)load_bytes(descriptor, 0x1c, 2);
    if (beginning > 1 && count == beginning / 2u &&
        (control & 0x04u) != 0 && (data->dma_half & (1u << channel)) == 0) {
        data->dma_half |= (uint16_t)(1u << channel);
        uint16_t pending = (uint16_t)load_bytes(data->dma, 0x24, 2);
        store_bytes(data->dma, 0x24, 2, pending | (1u << channel));
    }
    if (count == 0) {
        data->dma_half &= (uint16_t)~(1u << channel);
        dma_complete_major(data, channel, descriptor);
    }
    dma_update_interrupts(data);
}

static bool dma_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    const uint32_t offset = address - DMA_BASE;
    if (!valid_access(offset, size, DMA_REGISTER_SIZE))
        return false;
    if (offset == 0x30 && (size == 2 || size == 4)) {
        *value = data->dma_active;
        return true;
    }
    if (offset == 0x34 && (size == 2 || size == 4)) {
        *value = data->dma_requests;
        return true;
    }
    *value = load_bytes(data->dma, offset, size);
    return true;
}

static void dma_command(K22Data* data, uint32_t offset, uint8_t command) {
    const uint8_t channel = command & 15u;
    uint16_t value = 0;
    if (offset == 0x18 || offset == 0x19 || offset == 0x1a || offset == 0x1b ||
        offset == 0x1c || offset == 0x1d || offset == 0x1e || offset == 0x1f) {
        uint32_t register_offset = 0;
        bool set = false;
        if (offset == 0x18 || offset == 0x19)
            register_offset = 0x14;
        else if (offset == 0x1a || offset == 0x1b)
            register_offset = 0x0c;
        else if (offset == 0x1f)
            register_offset = 0x24;
        else if (offset == 0x1e)
            register_offset = 0x2c;
        if (offset == 0x19 || offset == 0x1b)
            set = true;
        if (offset == 0x1d) {
            dma_queue_channel(data, channel);
            return;
        }
        if (offset == 0x1c) {
            uint8_t* descriptor = data->dma + 0x1000u + (uint32_t)channel * DMA_TCD_SIZE;
            uint16_t control = (uint16_t)load_bytes(descriptor, 0x1c, 2);
            store_bytes(descriptor, 0x1c, 2, control & ~0x80u);
            return;
        }
        value = (uint16_t)load_bytes(data->dma, register_offset, 2);
        if ((command & 0x40u) != 0)
            value = set ? 0xffffu : 0;
        else if (set)
            value |= (uint16_t)(1u << channel);
        else
            value &= (uint16_t)~(1u << channel);
        store_bytes(data->dma, register_offset, 2, value);
        if (offset == 0x1e && value == 0)
            store_bytes(data->dma, 0x04, 4, 0);
    }
}

static bool dma_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    const uint32_t offset = address - DMA_BASE;
    if (!valid_access(offset, size, DMA_REGISTER_SIZE))
        return false;
    if (offset >= 0x18 && offset <= 0x1f && size == 1) {
        dma_command(data, offset, (uint8_t)value);
        dma_update_interrupts(data);
        return true;
    }
    if (offset == 0x04 || offset == 0x24 || offset == 0x28 || offset == 0x2c ||
        offset == 0x30 || offset == 0x34)
        return false;
    if (offset >= 0x1000u) {
        const uint32_t tcd_offset = (offset - 0x1000u) % DMA_TCD_SIZE;
        const uint8_t channel = (uint8_t)((offset - 0x1000u) / DMA_TCD_SIZE);
        if (tcd_offset == 0x1c && (size == 1 || size == 2)) {
            const uint16_t previous = (uint16_t)load_bytes(data->dma, offset, 2);
            const uint16_t status = previous & 0x00c0u;
            const uint16_t writable = size == 1 ? (previous & 0xff00u) | (value & 0x3eu)
                                                : value & 0xcf3eu;
            store_bytes(data->dma, offset, 2, writable | status);
            if ((value & 1u) != 0)
                dma_queue_channel(data, channel);
            return true;
        }
        if (tcd_offset == 0x1d && size == 1) {
            uint16_t control = (uint16_t)load_bytes(data->dma, offset - 1, 2);
            control = (uint16_t)((control & 0x00ffu) | ((value & 0x7fu) << 8));
            store_bytes(data->dma, offset - 1, 2, control);
            return true;
        }
    }
    store_bytes(data->dma, offset, size, value);
    return true;
}

static bool dmamux_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    const uint32_t offset = address - DMAMUX_BASE;
    if (!valid_access(offset, size, data->dmamux_count))
        return false;
    *value = load_bytes(data->dmamux, offset, size);
    return true;
}

static bool dmamux_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    const uint32_t offset = address - DMAMUX_BASE;
    if (!valid_access(offset, size, data->dmamux_count))
        return false;
    store_bytes(data->dmamux, offset, size, value);
    return true;
}

static uint32_t adc_conversion_cycles(const K22Adc* adc) {
    const uint8_t cfg1 = adc->registers[8];
    const uint8_t mode = (cfg1 >> 2) & 3u;
    uint32_t cycles = mode == 0 ? 10u : mode == 1 ? 14u : mode == 2 ? 12u : 18u;
    if ((cfg1 & 0x10u) != 0)
        cycles += (adc->registers[9] & 3u) == 0 ? 12u : 20u;
    if ((adc->registers[0x24] & 0x04u) != 0)
        cycles *= 1u << ((adc->registers[0x24] & 3u) + 2u);
    return cycles;
}

static void adc_start(K22Adc* adc, uint8_t slot) {
    if ((adc->registers[slot * 4u] & 0x1fu) == 31u)
        return;
    adc->registers[slot * 4u] &= 0x7fu;
    adc->active_slot = slot;
    adc->remaining_cycles = adc_conversion_cycles(adc);
    adc->converting = true;
}

static bool adc_compare(const K22Adc* adc, uint16_t result) {
    const uint8_t sc2 = adc->registers[0x20];
    if ((sc2 & 0x20u) == 0)
        return true;
    const uint16_t cv1 = (uint16_t)load_bytes(adc->registers, 0x18, 2);
    const uint16_t cv2 = (uint16_t)load_bytes(adc->registers, 0x1c, 2);
    if ((sc2 & 0x08u) != 0) {
        const bool in_range = result >= cv1 && result <= cv2;
        return (sc2 & 0x10u) != 0 ? !in_range : in_range;
    }
    return (sc2 & 0x10u) != 0 ? result > cv1 : result < cv1;
}

static uint16_t adc_result(const K22Adc* adc, uint8_t channel) {
    uint16_t result = adc->inputs[channel];
    const uint8_t mode = (adc->registers[8] >> 2) & 3u;
    const uint8_t bits = mode == 0 ? 8u : mode == 1 ? 12u : mode == 2 ? 10u : 16u;
    if (bits < 16)
        result &= (uint16_t)((1u << bits) - 1u);
    return result;
}

static void adc_complete(K22Data* data, uint8_t instance) {
    K22Adc* adc = &data->adc[instance];
    const uint8_t slot = adc->active_slot;
    const uint8_t channel = adc->registers[slot * 4u] & 0x1fu;
    const uint16_t result = adc_result(adc, channel);
    adc->converting = false;
    if (adc_compare(adc, result)) {
        store_bytes(adc->registers, 0x10u + slot * 4u, 2, result);
        adc->registers[slot * 4u] |= 0x80u;
        if ((adc->registers[slot * 4u] & 0x40u) != 0)
            interrupt(data, (K22DataInterrupt)(K22_DATA_INTERRUPT_ADC0 + instance), true);
        if ((adc->registers[0x20] & 0x04u) != 0)
            k22_data_dma_request(data, (uint8_t)(40u + instance));
    }
    if ((adc->registers[0x24] & 0x08u) != 0)
        adc_start(adc, slot);
}

static bool adc_read(K22Data* data, uint8_t instance, uint32_t address, uint8_t size,
                     uint32_t* value) {
    K22Adc* adc = &data->adc[instance];
    const uint32_t offset = address - data->adc_base[instance];
    if (!valid_access(offset, size, ADC_REGISTER_SIZE))
        return false;
    *value = load_bytes(adc->registers, offset, size);
    if ((offset == 0x10 || offset == 0x14) && size <= 4) {
        const uint8_t slot = (uint8_t)((offset - 0x10) / 4u);
        adc->registers[slot * 4u] &= 0x7fu;
        interrupt(data, (K22DataInterrupt)(K22_DATA_INTERRUPT_ADC0 + instance), false);
    }
    return true;
}

static bool adc_write(K22Data* data, uint8_t instance, uint32_t address, uint8_t size,
                      uint32_t value) {
    K22Adc* adc = &data->adc[instance];
    const uint32_t offset = address - data->adc_base[instance];
    if (!valid_access(offset, size, ADC_REGISTER_SIZE))
        return false;
    if (offset == 0x10 || offset == 0x14 || offset == 0x0c || offset == 0x28)
        return false;
    if (offset == 0 || offset == 4) {
        const uint8_t slot = (uint8_t)(offset / 4u);
        store_bytes(adc->registers, offset, size, value & 0x7fu);
        interrupt(data, (K22DataInterrupt)(K22_DATA_INTERRUPT_ADC0 + instance), false);
        if ((adc->registers[0x20] & 0x40u) == 0)
            adc_start(adc, slot);
        return true;
    }
    if (offset == 0x24) {
        const uint8_t command = (uint8_t)value;
        adc->registers[0x24] = command & 0xcfu;
        if ((command & 0x80u) != 0) {
            adc->registers[0x24] &= 0x3fu;
            adc->registers[0] |= 0x80u;
            store_bytes(adc->registers, 0x30, 2, 0x8200u);
            store_bytes(adc->registers, 0x34, 2, 0x8200u);
            store_bytes(adc->registers, 0x38, 2, 0x4000u);
        }
        return true;
    }
    store_bytes(adc->registers, offset, size, value);
    return true;
}

static void dac_update_output(K22Data* data, uint8_t instance) {
    K22Dac* dac = &data->dac[instance];
    if ((dac->registers[0x21] & 0x80u) == 0) {
        dac->output = 0;
        return;
    }
    uint8_t pointer = (dac->registers[0x23] >> 4) & 15u;
    dac->output = (uint16_t)load_bytes(dac->registers, (uint32_t)pointer * 2u, 2) & 0x0fffu;
}

static void dac_flags(K22Data* data, uint8_t instance, uint8_t flags) {
    K22Dac* dac = &data->dac[instance];
    dac->registers[0x20] |= flags;
    const uint8_t enables = dac->registers[0x21] & 7u;
    interrupt(data, (K22DataInterrupt)(K22_DATA_INTERRUPT_DAC0 + instance),
              (enables & flags) != 0);
    if ((dac->registers[0x22] & 0x80u) != 0 && flags != 0)
        k22_data_dma_request(data, (uint8_t)(45u + instance));
}

static bool dac_read(K22Data* data, uint8_t instance, uint32_t address, uint8_t size,
                     uint32_t* value) {
    const uint32_t offset = address - data->dac_base[instance];
    if (!valid_access(offset, size, DAC_REGISTER_SIZE))
        return false;
    *value = load_bytes(data->dac[instance].registers, offset, size);
    return true;
}

static bool dac_write(K22Data* data, uint8_t instance, uint32_t address, uint8_t size,
                      uint32_t value) {
    K22Dac* dac = &data->dac[instance];
    const uint32_t offset = address - data->dac_base[instance];
    if (!valid_access(offset, size, DAC_REGISTER_SIZE))
        return false;
    if (offset == 0x20) {
        dac->registers[0x20] &= (uint8_t)~value;
        interrupt(data, (K22DataInterrupt)(K22_DATA_INTERRUPT_DAC0 + instance), false);
        return true;
    }
    store_bytes(dac->registers, offset, size, value);
    dac_update_output(data, instance);
    if (offset <= 0x1f && (dac->registers[0x22] & 0x80u) == 0)
        dac_update_output(data, instance);
    return true;
}

static uint8_t cmp_level(const K22Cmp* cmp, uint8_t selection) {
    if (selection == 7u && (cmp->registers[4] & 0x80u) != 0)
        return cmp->registers[4] & 0x3fu;
    return cmp->inputs[selection & 7u];
}

static void cmp_evaluate(K22Data* data, uint8_t instance) {
    K22Cmp* cmp = &data->cmp[instance];
    const bool enabled = (cmp->registers[1] & 1u) != 0;
    const uint8_t positive = (cmp->registers[5] >> 3) & 7u;
    const uint8_t negative = cmp->registers[5] & 7u;
    bool output = enabled && cmp_level(cmp, positive) > cmp_level(cmp, negative);
    if ((cmp->registers[1] & 0x08u) != 0)
        output = !output;
    const bool previous = (cmp->registers[3] & 1u) != 0;
    if (output)
        cmp->registers[3] |= 1u;
    else
        cmp->registers[3] &= 0xfeu;
    if (!previous && output)
        cmp->registers[3] |= 4u;
    if (previous && !output)
        cmp->registers[3] |= 2u;
    const bool pending = ((cmp->registers[3] & 0x08u) != 0 &&
                          (cmp->registers[3] & 4u) != 0) ||
                         ((cmp->registers[3] & 0x10u) != 0 &&
                          (cmp->registers[3] & 2u) != 0);
    interrupt(data, (K22DataInterrupt)(K22_DATA_INTERRUPT_CMP0 + instance), pending);
    if (pending && (cmp->registers[3] & 0x40u) != 0)
        k22_data_dma_request(data, (uint8_t)(42u + instance));
}

static bool cmp_read(K22Data* data, uint8_t instance, uint32_t address, uint8_t size,
                     uint32_t* value) {
    const uint32_t offset = address - (CMP_BASE + (uint32_t)instance * 8u);
    if (!valid_access(offset, size, CMP_REGISTER_SIZE))
        return false;
    *value = load_bytes(data->cmp[instance].registers, offset, size);
    return true;
}

static bool cmp_write(K22Data* data, uint8_t instance, uint32_t address, uint8_t size,
                      uint32_t value) {
    K22Cmp* cmp = &data->cmp[instance];
    const uint32_t offset = address - (CMP_BASE + (uint32_t)instance * 8u);
    if (!valid_access(offset, size, CMP_REGISTER_SIZE))
        return false;
    if (offset == 3) {
        const uint8_t previous = cmp->registers[3];
        cmp->registers[3] = (uint8_t)((value & 0x78u) | (previous & 1u) |
                                      ((previous & 6u) & ~(value & 6u)));
    } else {
        store_bytes(cmp->registers, offset, size, value);
    }
    cmp_evaluate(data, instance);
    return true;
}

static uint32_t reverse_bits(uint32_t value, uint8_t count) {
    uint32_t result = 0;
    for (uint8_t bit = 0; bit < count; bit++) {
        result <<= 1;
        result |= (value >> bit) & 1u;
    }
    return result;
}

static void crc_accumulate(K22Data* data, uint8_t byte) {
    const bool width32 = (data->crc_control & 0x01000000u) != 0;
    const uint8_t bits = width32 ? 32u : 16u;
    const uint32_t mask = width32 ? UINT32_MAX : 0xffffu;
    uint32_t input = byte;
    data->crc_value &= mask;
    data->crc_value ^= input << (bits - 8u);
    for (uint8_t bit = 0; bit < 8; bit++) {
        const bool top = (data->crc_value & (1u << (bits - 1u))) != 0;
        data->crc_value = (data->crc_value << 1) & mask;
        if (top)
            data->crc_value ^= data->crc_polynomial & mask;
    }
}

static uint32_t crc_result(const K22Data* data) {
    const bool width32 = (data->crc_control & 0x01000000u) != 0;
    const uint8_t bits = width32 ? 32u : 16u;
    uint32_t value = data->crc_value;
    const uint8_t transpose = (uint8_t)((data->crc_control >> 28) & 3u);
    uint32_t transformed = 0;
    const uint8_t byte_count = (uint8_t)(bits / 8u);
    for (uint8_t index = 0; index < byte_count; index++) {
        uint8_t byte = (uint8_t)(value >> (index * 8u));
        if (transpose == 1 || transpose == 2)
            byte = (uint8_t)reverse_bits(byte, 8);
        const uint8_t target = transpose >= 2 ? (uint8_t)(byte_count - 1u - index) : index;
        transformed |= (uint32_t)byte << (target * 8u);
    }
    value = transformed;
    if ((data->crc_control & 0x04000000u) != 0)
        value = ~value;
    return width32 ? value : value & 0xffffu;
}

static bool crc_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    const uint32_t offset = address - CRC_BASE;
    if (!valid_access(offset, size, 12))
        return false;
    if (offset < 4) {
        *value = crc_result(data) >> (offset * 8u);
        if (size < 4)
            *value &= (1u << (size * 8u)) - 1u;
    } else if (offset < 8)
        *value = data->crc_polynomial >> ((offset - 4u) * 8u);
    else
        *value = data->crc_control >> ((offset - 8u) * 8u);
    return true;
}

static bool crc_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    const uint32_t offset = address - CRC_BASE;
    if (!valid_access(offset, size, 12))
        return false;
    if (offset < 4) {
        if ((data->crc_control & 0x02000000u) != 0) {
            uint32_t mask = size == 4 ? UINT32_MAX : (1u << (size * 8u)) - 1u;
            data->crc_value = (data->crc_value & ~(mask << (offset * 8u))) |
                              ((value & mask) << (offset * 8u));
            return true;
        }
        const uint8_t transpose = (uint8_t)((data->crc_control >> 30) & 3u);
        for (uint8_t index = 0; index < size; index++) {
            const uint8_t source = transpose >= 2 ? (uint8_t)(size - 1u - index) : index;
            uint8_t byte = (uint8_t)(value >> (source * 8u));
            if (transpose == 1 || transpose == 2)
                byte = (uint8_t)reverse_bits(byte, 8);
            crc_accumulate(data, byte);
        }
        return true;
    }
    if (offset < 8) {
        uint8_t bytes[4];
        store_bytes(bytes, 0, 4, data->crc_polynomial);
        store_bytes(bytes, offset - 4u, size, value);
        data->crc_polynomial = load_bytes(bytes, 0, 4);
    } else {
        uint8_t bytes[4];
        store_bytes(bytes, 0, 4, data->crc_control);
        store_bytes(bytes, offset - 8u, size, value);
        data->crc_control = load_bytes(bytes, 0, 4) & 0xf7000000u;
    }
    return true;
}

static uint32_t rng_next(uint32_t value) {
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value == 0 ? 0x6d2b79f5u : value;
}

static bool rng_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    const uint32_t offset = address - RNG_BASE;
    if (!valid_access(offset, size, 16) || size != 4)
        return false;
    if (offset == 0)
        *value = data->rng_control;
    else if (offset == 4)
        *value = data->rng_status;
    else if (offset == 8)
        *value = data->rng_error;
    else if (offset == 12) {
        *value = data->rng_output;
        data->rng_status &= ~1u;
        interrupt(data, K22_DATA_INTERRUPT_RNG, false);
    } else
        return false;
    return true;
}

static bool rng_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    const uint32_t offset = address - RNG_BASE;
    if (size != 4 || !valid_access(offset, size, 16))
        return false;
    if (offset != 0)
        return false;
    data->rng_control = value & 0x1fu;
    if ((value & 0x10u) != 0) {
        data->rng_status = 0;
        data->rng_error = 0;
        interrupt(data, K22_DATA_INTERRUPT_RNG, false);
    }
    if ((value & 1u) != 0)
        data->rng_cycles = 64;
    return true;
}

static uint32_t flash_address(const uint8_t* flash) {
    return ((uint32_t)flash[5] << 16) | ((uint32_t)flash[6] << 8) | flash[7];
}

static bool flash_store(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    return data->bus.write != NULL && data->bus.write(data->bus.context, address, size, value);
}

static bool flash_load(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    return data->bus.read != NULL && data->bus.read(data->bus.context, address, size, value);
}

static bool flash_protected(const K22Data* data, uint32_t address) {
    const uint32_t segment =
        ((uint64_t)address * 32u) / data->profile->program_flash_size;
    const uint8_t protection = data->flash[0x10u + segment / 8u];
    return (protection & (1u << (segment & 7u))) == 0;
}

static void flash_execute(K22Data* data) {
    data->flash[0] &= 0x30u;
    const uint8_t command = data->flash[4];
    const uint32_t address = flash_address(data->flash);
    bool valid = address < data->profile->program_flash_size;
    bool protection_failure = valid && flash_protected(data, address);
    if (command == 0x06u && valid && !protection_failure && (address & 3u) == 0) {
        const uint32_t value = ((uint32_t)data->flash[8] << 24) |
                               ((uint32_t)data->flash[9] << 16) |
                               ((uint32_t)data->flash[10] << 8) | data->flash[11];
        uint32_t previous = 0;
        valid = flash_load(data, address, 4, &previous) &&
                flash_store(data, address, 4, previous & value);
    } else if ((command == 0x09u || command == 0x08u) && valid &&
               !protection_failure) {
        const uint32_t sector = command == 0x08u ? data->profile->program_flash_size : 2048u;
        const uint32_t start = command == 0x08u ? 0 : address & ~(sector - 1u);
        if (command == 0x08u) {
            for (uint8_t index = 0x10; index <= 0x13; index++)
                protection_failure = protection_failure || data->flash[index] != 0xffu;
            valid = !protection_failure;
        }
        for (uint32_t offset = 0; valid && offset < sector; offset += 4)
            valid = flash_store(data, start + offset, 4, UINT32_MAX);
    } else if (command == 0x01u || command == 0x02u || command == 0x03u ||
               command == 0x41u || command == 0x44u) {
        valid = true;
    } else {
        valid = false;
    }
    if (!valid)
        data->flash[0] |= protection_failure ? 0x10u : 0x20u;
    data->flash_cycles = command == 0x09u || command == 0x08u ? 2000u : 40u;
}

static bool flash_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    const uint32_t offset = address - FLASH_BASE;
    if (!valid_access(offset, size, sizeof(data->flash)))
        return false;
    *value = load_bytes(data->flash, offset, size);
    return true;
}

static bool flash_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    const uint32_t offset = address - FLASH_BASE;
    if (!valid_access(offset, size, sizeof(data->flash)))
        return false;
    if (offset == 0 && size == 1) {
        data->flash[0] &= (uint8_t)~(value & 0x30u);
        if ((value & 0x80u) != 0 && (data->flash[0] & 0x80u) != 0)
            flash_execute(data);
        return true;
    }
    if (offset >= 0x10 && offset < 0x18) {
        for (uint8_t index = 0; index < size; index++)
            data->flash[offset + index] &= (uint8_t)(value >> (index * 8u));
        return true;
    }
    if (offset < 4)
        return false;
    store_bytes(data->flash, offset, size, value);
    return true;
}

static bool mapped_memory_read(K22Data* data, uint32_t address, uint8_t size,
                               uint32_t* value) {
    if (data->profile->flexnvm_size != 0 && address >= data->profile->flexnvm_address &&
        address - data->profile->flexnvm_address <= data->profile->flexnvm_size - size) {
        *value = load_bytes(data->flexnvm, address - data->profile->flexnvm_address, size);
        return true;
    }
    if (data->profile->flexram_size != 0 && address >= data->profile->flexram_address &&
        address - data->profile->flexram_address <= data->profile->flexram_size - size) {
        *value = load_bytes(data->flexram, address - data->profile->flexram_address, size);
        return true;
    }
    return false;
}

static bool mapped_memory_write(K22Data* data, uint32_t address, uint8_t size,
                                uint32_t value) {
    if (data->profile->flexnvm_size != 0 && address >= data->profile->flexnvm_address &&
        address - data->profile->flexnvm_address <= data->profile->flexnvm_size - size) {
        uint32_t offset = address - data->profile->flexnvm_address;
        uint32_t previous = load_bytes(data->flexnvm, offset, size);
        store_bytes(data->flexnvm, offset, size, previous & value);
        return true;
    }
    if (data->profile->flexram_size != 0 && address >= data->profile->flexram_address &&
        address - data->profile->flexram_address <= data->profile->flexram_size - size) {
        store_bytes(data->flexram, address - data->profile->flexram_address, size, value);
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
    if (profile_block(data, K22_PERIPHERAL_DMAMUX, NULL, &size))
        data->dmamux_count = (uint8_t)(size > DMA_CHANNEL_COUNT ? DMA_CHANNEL_COUNT : size);
    uint32_t base = 0;
    if (profile_block(data, K22_PERIPHERAL_ADC0, &base, NULL))
        data->adc_base[data->adc_count++] = base;
    if (profile_block(data, K22_PERIPHERAL_ADC1, &base, NULL))
        data->adc_base[data->adc_count++] = base;
    if (profile_block(data, K22_PERIPHERAL_DAC0, &base, NULL))
        data->dac_base[data->dac_count++] = base;
    if (profile_block(data, K22_PERIPHERAL_DAC1, &base, NULL))
        data->dac_base[data->dac_count++] = base;
    for (uint8_t index = 0; index < 3; index++) {
        if (profile_block(data, (K22PeripheralId)(K22_PERIPHERAL_CMP0 + index), NULL,
                          NULL))
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
    data->dma_active = 0;
    data->dma_half = 0;
    data->vref_cycles = 0;
    data->rng_control = 0;
    data->rng_status = 0;
    data->rng_error = 0;
    data->rng_output = 0;
    data->rng_state = 0x6d2b79f5u;
    data->rng_cycles = 0;
    data->crc_value = 0;
    data->crc_polynomial = 0x00001021u;
    data->crc_control = 0;
    memset(data->flash, 0, sizeof(data->flash));
    data->flash[0] = 0x80u;
    data->flash[2] = data->flash_config[0x0c];
    data->flash[3] = data->flash_config[0x0d];
    memcpy(data->flash + 0x10, data->flash_config + 8, 4);
    data->flash[0x16] = data->flash_config[0x0e];
    data->flash[0x17] = data->flash_config[0x0f];
    data->flash_cycles = 0;
    if (data->flexram != NULL)
        memset(data->flexram, 0, data->profile->flexram_size);
    for (uint8_t index = 0; index < data->adc_count; index++) {
        data->adc[index].registers[0] = 0x1fu;
        data->adc[index].registers[4] = 0x1fu;
    }
    for (uint8_t index = 0; index < data->dac_count; index++)
        data->dac[index].registers[0x23] = 0x0fu;
    for (uint8_t line = 0; line < K22_DATA_INTERRUPT_COUNT; line++)
        interrupt(data, (K22DataInterrupt)line, false);
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
    dma_update_interrupts(destination);
    return true;
}

bool k22_data_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    if (data == NULL || value == NULL || (size != 1 && size != 2 && size != 4))
        return false;
    if (mapped_memory_read(data, address, size, value))
        return true;
    if (address >= 0x400u && address - 0x400u <= sizeof(data->flash_config) - size &&
        size <= sizeof(data->flash_config)) {
        *value = load_bytes(data->flash_config, address - 0x400u, size);
        return true;
    }
    uint32_t base = 0;
    uint32_t block_size = 0;
    if (profile_block(data, K22_PERIPHERAL_DMA, &base, &block_size) && address >= base &&
        address - base < block_size)
        return dma_read(data, address, size, value);
    if (profile_block(data, K22_PERIPHERAL_DMAMUX, &base, &block_size) && address >= base &&
        address - base < block_size)
        return dmamux_read(data, address, size, value);
    for (uint8_t index = 0; index < data->adc_count; index++)
        if (address >= data->adc_base[index] &&
            address - data->adc_base[index] < ADC_REGISTER_SIZE)
            return adc_read(data, index, address, size, value);
    for (uint8_t index = 0; index < data->dac_count; index++)
        if (address >= data->dac_base[index] &&
            address - data->dac_base[index] < DAC_REGISTER_SIZE)
            return dac_read(data, index, address, size, value);
    for (uint8_t index = 0; index < data->cmp_count; index++)
        if (address >= CMP_BASE + (uint32_t)index * 8u &&
            address - (CMP_BASE + (uint32_t)index * 8u) < CMP_REGISTER_SIZE)
            return cmp_read(data, index, address, size, value);
    if (profile_block(data, K22_PERIPHERAL_VREF, &base, NULL) && address >= VREF_BASE &&
        address - VREF_BASE < 2u) {
        if (!valid_access(address - VREF_BASE, size, 2))
            return false;
        *value = load_bytes(data->vref, address - VREF_BASE, size);
        return true;
    }
    if (profile_block(data, K22_PERIPHERAL_RNG, &base, NULL) && address >= RNG_BASE &&
        address - RNG_BASE < 16u)
        return rng_read(data, address, size, value);
    if (profile_block(data, K22_PERIPHERAL_CRC, &base, NULL) && address >= CRC_BASE &&
        address - CRC_BASE < 12u)
        return crc_read(data, address, size, value);
    if ((profile_block(data, K22_PERIPHERAL_FTFA, &base, NULL) ||
         profile_block(data, K22_PERIPHERAL_FTFE, &base, NULL)) &&
        address >= FLASH_BASE && address - FLASH_BASE < sizeof(data->flash))
        return flash_read(data, address, size, value);
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
    if (profile_block(data, K22_PERIPHERAL_DMA, &base, &block_size) && address >= base &&
        address - base < block_size)
        return dma_write(data, address, size, value);
    if (profile_block(data, K22_PERIPHERAL_DMAMUX, &base, &block_size) && address >= base &&
        address - base < block_size)
        return dmamux_write(data, address, size, value);
    for (uint8_t index = 0; index < data->adc_count; index++)
        if (address >= data->adc_base[index] &&
            address - data->adc_base[index] < ADC_REGISTER_SIZE)
            return adc_write(data, index, address, size, value);
    for (uint8_t index = 0; index < data->dac_count; index++)
        if (address >= data->dac_base[index] &&
            address - data->dac_base[index] < DAC_REGISTER_SIZE)
            return dac_write(data, index, address, size, value);
    for (uint8_t index = 0; index < data->cmp_count; index++)
        if (address >= CMP_BASE + (uint32_t)index * 8u &&
            address - (CMP_BASE + (uint32_t)index * 8u) < CMP_REGISTER_SIZE)
            return cmp_write(data, index, address, size, value);
    if (profile_block(data, K22_PERIPHERAL_VREF, &base, NULL) && address >= VREF_BASE &&
        address - VREF_BASE < 2u) {
        if (!valid_access(address - VREF_BASE, size, 2))
            return false;
        store_bytes(data->vref, address - VREF_BASE, size, value);
        data->vref[1] &= 0xfbu;
        data->vref_cycles = (data->vref[1] & 0x80u) != 0 ? 100u : 0;
        return true;
    }
    if (profile_block(data, K22_PERIPHERAL_RNG, &base, NULL) && address >= RNG_BASE &&
        address - RNG_BASE < 16u)
        return rng_write(data, address, size, value);
    if (profile_block(data, K22_PERIPHERAL_CRC, &base, NULL) && address >= CRC_BASE &&
        address - CRC_BASE < 12u)
        return crc_write(data, address, size, value);
    if ((profile_block(data, K22_PERIPHERAL_FTFA, &base, NULL) ||
         profile_block(data, K22_PERIPHERAL_FTFE, &base, NULL)) &&
        address >= FLASH_BASE && address - FLASH_BASE < sizeof(data->flash))
        return flash_write(data, address, size, value);
    return false;
}

void k22_data_dma_request(K22Data* data, uint8_t source) {
    if (data == NULL)
        return;
    const uint16_t enabled = (uint16_t)load_bytes(data->dma, 0x0c, 2);
    for (uint8_t channel = 0; channel < data->dmamux_count; channel++) {
        const uint8_t mux = data->dmamux[channel];
        if ((mux & 0x80u) != 0 && (mux & 0x3fu) == source &&
            (enabled & (1u << channel)) != 0)
            dma_queue_channel(data, channel);
    }
}

void k22_data_adc_trigger(K22Data* data, uint8_t instance) {
    if (data != NULL && instance < data->adc_count &&
        (data->adc[instance].registers[0x20] & 0x40u) != 0)
        adc_start(&data->adc[instance], 0);
}

bool k22_data_set_adc_input(K22Data* data, uint8_t instance, uint8_t channel,
                            uint16_t value) {
    if (data == NULL || instance >= data->adc_count || channel >= 32)
        return false;
    data->adc[instance].inputs[channel] = value;
    return true;
}

bool k22_data_set_cmp_input(K22Data* data, uint8_t instance, uint8_t input,
                            uint8_t value) {
    if (data == NULL || instance >= data->cmp_count || input >= 8)
        return false;
    data->cmp[instance].inputs[input] = value;
    cmp_evaluate(data, instance);
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
        if (pointer == 0)
            flags |= 2u;
        pointer = pointer == 0 ? upper : (uint8_t)(pointer - 1u);
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
    dac_update_output(data, instance);
    dac_flags(data, instance, flags);
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

void k22_data_advance(K22Data* data, uint32_t cycles) {
    if (data == NULL || cycles == 0)
        return;
    for (uint8_t channel = 0; channel < DMA_CHANNEL_COUNT; channel++) {
        if ((data->dma_requests & (1u << channel)) != 0) {
            data->dma_requests &= (uint16_t)~(1u << channel);
            dma_service_channel(data, channel);
        }
    }
    for (uint8_t index = 0; index < data->adc_count; index++) {
        K22Adc* adc = &data->adc[index];
        if (!adc->converting)
            continue;
        if (cycles >= adc->remaining_cycles) {
            adc->remaining_cycles = 0;
            adc_complete(data, index);
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
            data->rng_state = rng_next(data->rng_state);
            data->rng_output = data->rng_state;
            data->rng_status |= 1u;
            if ((data->rng_control & 2u) != 0)
                interrupt(data, K22_DATA_INTERRUPT_RNG, true);
        } else {
            data->rng_cycles -= cycles;
        }
    }
    if (data->flash_cycles != 0) {
        if (cycles >= data->flash_cycles) {
            data->flash_cycles = 0;
            data->flash[0] |= 0x80u;
            if ((data->flash[1] & 0x80u) != 0)
                interrupt(data, K22_DATA_INTERRUPT_FTFA, true);
        } else {
            data->flash_cycles -= cycles;
        }
    }
}
