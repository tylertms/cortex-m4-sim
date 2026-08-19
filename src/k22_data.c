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
    uint16_t dma_hardware_requests;
    uint16_t dma_active;
    uint16_t dma_half;
    uint8_t dma_request_source[DMA_CHANNEL_COUNT];
    uint8_t dma_channel_count;
    uint8_t dma_last_channel;
    bool debug_halted;
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
    uint8_t flash[0x2c];
    uint8_t flash_config[0x10];
    uint8_t flash_program_ifr[1024];
    uint8_t flash_data_ifr[1024];
    uint32_t flash_cycles;
    uint8_t flash_busy_banks;
    uint32_t flash_busy_start;
    uint32_t flash_busy_length;
    uint32_t flash_swap_address;
    uint8_t flash_swap_mode;
    uint8_t flash_swap_current_block;
    uint8_t flash_swap_next_block;
    bool flash_key_blocked;
    bool flash_partitioned;
    bool flexram_eeprom;
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

static void adc_reset_registers(K22Adc* adc) {
    adc->registers[0] = 0x1fu;
    adc->registers[4] = 0x1fu;
    store_bytes(adc->registers, 0x28u, 4u, 0x0004u);
    store_bytes(adc->registers, 0x2cu, 4u, 0x8200u);
    store_bytes(adc->registers, 0x30u, 4u, 0x8200u);
    store_bytes(adc->registers, 0x34u, 4u, 0x000au);
    store_bytes(adc->registers, 0x38u, 4u, 0x0020u);
    store_bytes(adc->registers, 0x3cu, 4u, 0x0200u);
    store_bytes(adc->registers, 0x40u, 4u, 0x0100u);
    store_bytes(adc->registers, 0x44u, 4u, 0x0080u);
    store_bytes(adc->registers, 0x48u, 4u, 0x0040u);
    store_bytes(adc->registers, 0x4cu, 4u, 0x0020u);
    store_bytes(adc->registers, 0x54u, 4u, 0x000au);
    store_bytes(adc->registers, 0x58u, 4u, 0x0020u);
    store_bytes(adc->registers, 0x5cu, 4u, 0x0200u);
    store_bytes(adc->registers, 0x60u, 4u, 0x0100u);
    store_bytes(adc->registers, 0x64u, 4u, 0x0080u);
    store_bytes(adc->registers, 0x68u, 4u, 0x0040u);
    store_bytes(adc->registers, 0x6cu, 4u, 0x0020u);
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

static uint32_t dma_priority_offset(uint8_t channel) {
    return 0x100u + (channel & 0xfcu) + 3u - (channel & 3u);
}

static uint8_t dma_priority_channel(uint32_t offset) {
    const uint8_t index = (uint8_t)(offset - 0x100u);
    return (uint8_t)((index & 0xfcu) + 3u - (index & 3u));
}

static bool dma_priorities_valid(const K22Data* data) {
    uint16_t used = 0u;
    for (uint8_t channel = 0u; channel < data->dma_channel_count; channel++) {
        const uint8_t priority = data->dma[dma_priority_offset(channel)] & 15u;
        if ((used & (1u << priority)) != 0u)
            return false;
        used |= (uint16_t)(1u << priority);
    }
    return true;
}

static uint8_t dma_select_channel(const K22Data* data) {
    if ((load_bytes(data->dma, 0u, 4u) & 4u) != 0u) {
        for (uint8_t step = 1u; step <= data->dma_channel_count; step++) {
            const uint8_t channel =
                (uint8_t)((data->dma_last_channel + step) % data->dma_channel_count);
            if ((data->dma_requests & (1u << channel)) != 0u)
                return channel;
        }
    } else {
        uint8_t selected = UINT8_MAX;
        uint8_t selected_priority = 0u;
        for (uint8_t channel = 0u; channel < data->dma_channel_count; channel++) {
            if ((data->dma_requests & (1u << channel)) == 0u)
                continue;
            const uint8_t priority = data->dma[dma_priority_offset(channel)] & 15u;
            if (selected == UINT8_MAX || priority > selected_priority) {
                selected = channel;
                selected_priority = priority;
            }
        }
        return selected;
    }
    return UINT8_MAX;
}

static uint16_t dma_iteration_count(uint16_t value) {
    return (value & 0x8000u) != 0 ? value & 0x01ffu : value & 0x7fffu;
}

static uint8_t dma_link_channel(uint16_t value) { return (uint8_t)((value >> 9) & 15u); }

static uint32_t dma_advance_address(uint32_t address, int16_t offset, uint8_t modulo) {
    const uint32_t advanced = (uint32_t)((int64_t)address + offset);
    if (modulo == 0 || modulo >= 31)
        return advanced;
    const uint32_t mask = (1u << modulo) - 1u;
    return (address & ~mask) | (advanced & mask);
}

static void dma_set_iteration_count(uint8_t* descriptor, uint32_t offset, uint16_t value) {
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
    if ((load_bytes(data->dma, 0u, 4u) & 0x10u) != 0u)
        store_bytes(data->dma, 0u, 4u, load_bytes(data->dma, 0u, 4u) | 0x20u);
}

static bool dma_bus_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    return data->bus.read != NULL &&
           data->bus.read(data->bus.context, address, size, value);
}

static bool dma_bus_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    return data->bus.write != NULL &&
           data->bus.write(data->bus.context, address, size, value);
}

static void dma_queue_channel(K22Data* data, uint8_t channel) {
    if (channel < data->dma_channel_count)
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

static bool dma_service_channel(K22Data* data, uint8_t channel) {
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
        return false;
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
                return false;
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
            return false;
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
    if (beginning > 1 && count == beginning / 2u && (control & 0x04u) != 0 &&
        (data->dma_half & (1u << channel)) == 0) {
        data->dma_half |= (uint16_t)(1u << channel);
        uint16_t pending = (uint16_t)load_bytes(data->dma, 0x24, 2);
        store_bytes(data->dma, 0x24, 2, pending | (1u << channel));
    }
    if (count == 0) {
        data->dma_half &= (uint16_t)~(1u << channel);
        dma_complete_major(data, channel, descriptor);
    }
    dma_update_interrupts(data);
    return true;
}

static bool dma_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    const uint32_t offset = address - DMA_BASE;
    if (!valid_access(offset, size, DMA_REGISTER_SIZE))
        return false;
    if (offset >= 0x100u && offset < 0x110u) {
        if (size != 1u || dma_priority_channel(offset) >= data->dma_channel_count)
            return false;
    }
    if (offset >= 0x1000u && (offset - 0x1000u) / DMA_TCD_SIZE >= data->dma_channel_count)
        return false;
    if (offset == 0x30 && (size == 2 || size == 4)) {
        *value = data->dma_active;
        return true;
    }
    if (offset == 0x34 && (size == 2 || size == 4)) {
        *value = data->dma_hardware_requests;
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
    if (offset >= 0x100u && offset < 0x110u) {
        if (size != 1u || dma_priority_channel(offset) >= data->dma_channel_count)
            return false;
        data->dma[offset] = (uint8_t)value & 0xcfu;
        return true;
    }
    if (offset >= 0x1000u && (offset - 0x1000u) / DMA_TCD_SIZE >= data->dma_channel_count)
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
            const uint16_t writable =
                size == 1 ? (previous & 0xff00u) | (value & 0x3eu) : value & 0xcf3eu;
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

static void adc_schedule(K22Adc* adc, uint8_t slot, bool clear_complete) {
    if ((adc->registers[slot * 4u] & 0x1fu) == 31u)
        return;
    if (clear_complete)
        adc->registers[slot * 4u] &= 0x7fu;
    adc->active_slot = slot;
    adc->remaining_cycles = adc_conversion_cycles(adc);
    adc->converting = true;
}

static void adc_start(K22Adc* adc, uint8_t slot) { adc_schedule(adc, slot, true); }

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
        adc_schedule(adc, slot, false);
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
    const bool pending =
        ((cmp->registers[3] & 0x08u) != 0 && (cmp->registers[3] & 4u) != 0) ||
        ((cmp->registers[3] & 0x10u) != 0 && (cmp->registers[3] & 2u) != 0);
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
    const uint32_t stored_high = width32 ? 0u : data->crc_value & 0xffff0000u;
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
    return width32 ? value : stored_high | (value & 0xffffu);
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

static uint8_t flash_fccob_offset(uint8_t index) {
    static const uint8_t offsets[12] = {7u, 6u, 5u,  4u,  11u, 10u,
                                        9u, 8u, 15u, 14u, 13u, 12u};
    return offsets[index];
}

static uint8_t flash_fccob(const K22Data* data, uint8_t index) {
    return data->flash[flash_fccob_offset(index)];
}

static void flash_set_fccob(K22Data* data, uint8_t index, uint8_t value) {
    data->flash[flash_fccob_offset(index)] = value;
}

static uint32_t flash_address(const K22Data* data) {
    return ((uint32_t)flash_fccob(data, 1u) << 16u) |
           ((uint32_t)flash_fccob(data, 2u) << 8u) | flash_fccob(data, 3u);
}

uint32_t k22_data_program_flash_address(const K22Data* data, uint32_t address) {
    if (data == NULL || data->flash_swap_current_block == 0u ||
        address >= data->profile->program_flash_size)
        return address;
    return address ^ (data->profile->program_flash_size / 2u);
}

static bool flash_store(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    bool stored = false;
    const uint32_t physical_address = k22_data_program_flash_address(data, address);
    if (data->bus.program != NULL)
        stored = data->bus.program(data->bus.context, physical_address, size, value);
    else if (data->bus.write != NULL)
        stored = data->bus.write(data->bus.context, physical_address, size, value);
    if (!stored)
        return false;
    if (address < 0x410u && address + size > 0x400u) {
        for (uint8_t index = 0; index < size; index++) {
            const uint32_t byte_address = address + index;
            if (byte_address >= 0x400u && byte_address < 0x410u)
                data->flash_config[byte_address - 0x400u] =
                    (uint8_t)(value >> (index * 8u));
        }
    }
    return true;
}

static bool flash_load(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    if (address >= 0x400u && address <= 0x410u - size) {
        *value = load_bytes(data->flash_config, address - 0x400u, size);
        return true;
    }
    return data->bus.read != NULL &&
           data->bus.read(data->bus.context, address, size, value);
}

static uint32_t flash_data_size(const K22Data* data) {
    if (!data->flash_partitioned)
        return data->profile->flexnvm_size;
    switch (data->flash_data_ifr[0x3fcu]) {
    case 0x00u:
    case 0x0du:
    case 0x0fu:
        return 0x20000u;
    case 0x03u:
        return 0x18000u;
    case 0x04u:
    case 0x0cu:
        return 0x10000u;
    case 0x0bu:
        return 0x8000u;
    case 0x05u:
    case 0x08u:
        return 0u;
    default:
        return 0u;
    }
}

static bool flash_memory_range(const K22Data* data, uint32_t address, uint32_t length,
                               bool* data_flash, uint32_t* offset) {
    *data_flash = (address & 0x800000u) != 0u;
    *offset = address & 0x7fffffu;
    const uint32_t available =
        *data_flash ? flash_data_size(data) : data->profile->program_flash_size;
    return length <= available && *offset <= available - length;
}

static bool flash_memory_load(K22Data* data, uint32_t address, uint8_t size,
                              uint32_t* value) {
    bool data_flash = false;
    uint32_t offset = 0;
    if (!flash_memory_range(data, address, size, &data_flash, &offset))
        return false;
    if (!data_flash)
        return flash_load(data, offset, size, value);
    *value = load_bytes(data->flexnvm, offset, size);
    return true;
}

static bool flash_memory_store(K22Data* data, uint32_t address, uint8_t size,
                               uint32_t value) {
    bool data_flash = false;
    uint32_t offset = 0;
    if (!flash_memory_range(data, address, size, &data_flash, &offset))
        return false;
    if (!data_flash)
        return flash_store(data, offset, size, value);
    store_bytes(data->flexnvm, offset, size, value);
    return true;
}

static bool flash_memory_range_protected(const K22Data* data, uint32_t address,
                                         uint32_t length) {
    bool data_flash = false;
    uint32_t offset = 0;
    if (length == 0u || !flash_memory_range(data, address, length, &data_flash, &offset))
        return true;
    const uint32_t region_count = data_flash ? 8u : 32u;
    const uint32_t memory_size =
        data_flash ? flash_data_size(data) : data->profile->program_flash_size;
    const uint32_t first = ((uint64_t)offset * region_count) / memory_size;
    const uint32_t last = ((uint64_t)(offset + length - 1u) * region_count) / memory_size;
    for (uint32_t region = first; region <= last; region++) {
        const uint8_t protection = data->flash[data_flash ? 0x17u : 0x10u + region / 8u];
        if ((protection & (1u << (region & 7u))) == 0u)
            return true;
    }
    return false;
}

static bool flash_program_words(K22Data* data, uint32_t address, uint8_t words,
                                bool* verify_failure) {
    for (uint8_t word = 0; word < words; word++) {
        const uint8_t offset = (uint8_t)(4u + word * 4u);
        const uint32_t value = (uint32_t)flash_fccob(data, offset) |
                               ((uint32_t)flash_fccob(data, offset + 1u) << 8u) |
                               ((uint32_t)flash_fccob(data, offset + 2u) << 16u) |
                               ((uint32_t)flash_fccob(data, offset + 3u) << 24u);
        uint32_t previous = 0;
        if (!flash_memory_load(data, address + (uint32_t)word * 4u, 4u, &previous))
            return false;
        if (previous != UINT32_MAX) {
            *verify_failure = true;
            return true;
        }
        if (!flash_memory_store(data, address + (uint32_t)word * 4u, 4u, value))
            return false;
    }
    return true;
}

static bool flash_program_buffer(K22Data* data, uint32_t address, uint32_t length,
                                 bool* verify_failure) {
    for (uint32_t offset = 0; offset < length; offset += 4u) {
        uint32_t previous = 0;
        if (!flash_memory_load(data, address + offset, 4u, &previous))
            return false;
        if (previous != UINT32_MAX) {
            *verify_failure = true;
            return true;
        }
        const uint32_t value = load_bytes(data->flexram, offset, 4u);
        if (!flash_memory_store(data, address + offset, 4u, value))
            return false;
    }
    return true;
}

static bool flash_erase(K22Data* data, uint32_t start, uint32_t length) {
    for (uint32_t offset = 0; offset < length; offset += 4u) {
        if (!flash_memory_store(data, start + offset, 4u, UINT32_MAX))
            return false;
    }
    return true;
}

static bool flash_range_erased(K22Data* data, uint32_t start, uint32_t length) {
    for (uint32_t offset = 0; offset < length; offset += 4u) {
        uint32_t word = 0;
        if (!flash_memory_load(data, start + offset, 4u, &word) || word != UINT32_MAX)
            return false;
    }
    return true;
}

static uint32_t flash_block_size(const K22Data* data) {
    if (data->profile->program_flash_size >= 0x80000u)
        return data->profile->program_flash_size == 0x100000u ? 0x80000u : 0x40000u;
    return data->profile->program_flash_size;
}

static bool flash_block_range(const K22Data* data, uint32_t address, uint32_t* start,
                              uint32_t* length, bool* data_flash) {
    uint32_t offset = 0u;
    if (!flash_memory_range(data, address, 1u, data_flash, &offset))
        return false;
    *length = *data_flash ? flash_data_size(data) : flash_block_size(data);
    if (*length == 0u)
        return false;
    *start = *data_flash ? 0x800000u : address & ~(*length - 1u);
    return true;
}

static bool flash_read_resource(K22Data* data) {
    const bool ftfe = k22_profile_has_peripheral(data->profile, K22_PERIPHERAL_FTFE);
    const uint32_t address = flash_address(data);
    const uint8_t option = flash_fccob(data, ftfe ? 4u : 8u);
    const uint8_t length = ftfe ? 8u : 4u;
    const uint8_t* source = NULL;
    uint32_t offset = address;
    if (option == 0u && address < (ftfe ? 1024u : 256u))
        source = data->flash_program_ifr;
    else if (option == 0u && ftfe && data->profile->flexnvm_size != 0u &&
             address >= 0x800000u && address - 0x800000u <= 1024u - length) {
        source = data->flash_data_ifr;
        offset = address - 0x800000u;
    } else if (option == 1u && address == (ftfe ? 8u : 0u)) {
        static const uint8_t ftfa_version[8] = {0x01u, 0x00u, 0x46u, 0x54u,
                                                0x46u, 0x41u, 0x00u, 0x00u};
        static const uint8_t ftfe_version[8] = {0x01u, 0x00u, 0x46u, 0x54u,
                                                0x46u, 0x45u, 0x00u, 0x00u};
        source = ftfe ? ftfe_version : ftfa_version;
        offset = 0;
    }
    if (source == NULL || offset > 1024u - length || (address & (length - 1u)) != 0u)
        return false;
    for (uint8_t index = 0; index < length; index++)
        flash_set_fccob(data, (uint8_t)(4u + index), source[offset + index]);
    return true;
}

static bool flash_once_record(const K22Data* data, uint8_t index, uint32_t* offset,
                              uint8_t* length) {
    if (k22_profile_has_peripheral(data->profile, K22_PERIPHERAL_FTFE)) {
        if (index > 7u)
            return false;
        *offset = 0x3c0u + (uint32_t)index * 8u;
        *length = 8u;
        return true;
    }
    if (index <= 7u) {
        *offset = 0xc0u + (uint32_t)index * 4u;
        *length = 4u;
        return true;
    }
    if (index >= 0x10u && index <= 0x13u) {
        *offset = 0xe0u + (uint32_t)(index - 0x10u) * 8u;
        *length = 8u;
        return true;
    }
    return false;
}

static bool flash_read_once(K22Data* data) {
    uint32_t offset = 0;
    uint8_t length = 0;
    if (!flash_once_record(data, flash_fccob(data, 1u), &offset, &length))
        return false;
    for (uint8_t index = 0; index < length; index++)
        flash_set_fccob(data, (uint8_t)(4u + index),
                        data->flash_program_ifr[offset + index]);
    return true;
}

static bool flash_program_once(K22Data* data, bool* verify_failure) {
    uint32_t offset = 0;
    uint8_t length = 0;
    if (!flash_once_record(data, flash_fccob(data, 1u), &offset, &length))
        return false;
    for (uint8_t index = 0; index < length; index++) {
        if (data->flash_program_ifr[offset + index] != 0xffu)
            return false;
    }
    for (uint8_t index = 0; index < length; index++)
        data->flash_program_ifr[offset + index] = flash_fccob(data, (uint8_t)(4u + index));
    for (uint8_t index = 0; index < length; index++)
        *verify_failure |= data->flash_program_ifr[offset + index] !=
                           flash_fccob(data, (uint8_t)(4u + index));
    return true;
}

static bool flash_verify_key(K22Data* data) {
    if (data->flash_key_blocked || (data->flash[2] & 0xc0u) != 0x80u)
        return false;
    uint8_t aggregate_and = 0xffu;
    uint8_t aggregate_or = 0u;
    bool matches = true;
    for (uint8_t index = 0; index < 8u; index++) {
        const uint8_t key = flash_fccob(data, (uint8_t)(4u + index));
        aggregate_and &= key;
        aggregate_or |= key;
        matches &= key == data->flash_config[index];
    }
    if (aggregate_or == 0u || aggregate_and == 0xffu)
        matches = false;
    if (matches)
        data->flash[2] = (uint8_t)((data->flash[2] & 0xfcu) | 0x02u);
    else
        data->flash_key_blocked = true;
    return matches;
}

static bool flash_program_partition(K22Data* data) {
    static const uint8_t valid_depart[] = {0x00u, 0x03u, 0x04u, 0x05u, 0x08u,
                                           0x0bu, 0x0cu, 0x0du, 0x0fu};
    if (data->profile->flexnvm_size == 0u || data->flash_partitioned)
        return false;
    const uint8_t load = flash_fccob(data, 3u);
    const uint8_t eeesize = flash_fccob(data, 4u);
    const uint8_t depart = flash_fccob(data, 5u);
    bool valid_depart_code = false;
    for (size_t index = 0; index < sizeof(valid_depart); index++)
        valid_depart_code |= depart == valid_depart[index];
    const bool no_eeprom = depart == 0x00u || depart == 0x0du || depart == 0x0fu;
    const bool eeprom_disabled = eeesize == 0x0fu;
    if (load > 1u || (eeesize & 0xc0u) != 0u || (depart & 0xf0u) != 0u ||
        !valid_depart_code || (eeesize < 2u && !eeprom_disabled) ||
        (eeesize > 9u && !eeprom_disabled) || no_eeprom != eeprom_disabled)
        return false;
    memset(data->flexnvm, 0xff, data->profile->flexnvm_size);
    data->flash_data_ifr[0x3fcu] = depart;
    data->flash_data_ifr[0x3fdu] = eeesize;
    data->flash_partitioned = true;
    data->flexram_eeprom = !no_eeprom;
    memset(data->flexram, 0xff, data->profile->flexram_size);
    data->flash[1] =
        (uint8_t)((data->flash[1] & 0xfcu) | (data->flexram_eeprom ? 0x01u : 0x02u));
    return true;
}

static bool flash_set_flexram(K22Data* data) {
    if (data->flexram == NULL)
        return false;
    const uint8_t control = flash_fccob(data, 1u);
    if (control != 0u && control != 0xffu)
        return false;
    if (control == 0u && !data->flash_partitioned)
        return false;
    memset(data->flexram, 0xff, data->profile->flexram_size);
    data->flexram_eeprom = control == 0u;
    data->flash[1] =
        (uint8_t)((data->flash[1] & 0xfcu) | (data->flexram_eeprom ? 0x01u : 0x02u));
    return true;
}

static bool flash_swap_address_valid(const K22Data* data, uint32_t address) {
    const uint32_t block_size = data->profile->program_flash_size / 2u;
    return (address & 0x0fu) == 0u && address < block_size &&
           (address < 0x400u || address >= 0x410u);
}

static bool flash_swap_program_indicator(K22Data* data, uint8_t block, uint16_t value) {
    const uint32_t logical_address =
        data->flash_swap_address + ((uint32_t)(block ^ data->flash_swap_current_block) *
                                    (data->profile->program_flash_size / 2u));
    return flash_memory_store(data, logical_address, 2u, value);
}

static bool flash_swap_control(K22Data* data, uint32_t address, bool* verify_failure) {
    const uint8_t control = flash_fccob(data, 4u);
    if (!flash_swap_address_valid(data, address))
        return false;
    if (control == 0x08u) {
        flash_set_fccob(data, 5u, data->flash_swap_mode);
        flash_set_fccob(data, 6u, data->flash_swap_current_block);
        flash_set_fccob(data, 7u, data->flash_swap_next_block);
        return true;
    }
    if (control == 0x01u) {
        if (data->flash_swap_mode != 0u)
            return false;
        data->flash_swap_address = address;
        if (!flash_swap_program_indicator(data, 0u, 0xff00u)) {
            *verify_failure = true;
            return true;
        }
        data->flash_swap_mode = 3u;
        data->flash_swap_current_block = 0u;
        data->flash_swap_next_block = 0u;
        return true;
    }
    if (address != data->flash_swap_address)
        return false;
    if (control == 0x02u) {
        if (data->flash_swap_mode != 1u)
            return false;
        if (!flash_swap_program_indicator(data, data->flash_swap_current_block, 0xff00u)) {
            *verify_failure = true;
            return true;
        }
        data->flash_swap_mode = 2u;
        return true;
    }
    if (control == 0x04u) {
        if (data->flash_swap_mode != 3u)
            return false;
        if (!flash_swap_program_indicator(data, data->flash_swap_current_block, 0u)) {
            *verify_failure = true;
            return true;
        }
        data->flash_swap_mode = 4u;
        data->flash_swap_next_block = data->flash_swap_current_block ^ 1u;
        return true;
    }
    return false;
}

static void flash_swap_erased(K22Data* data, uint32_t start, uint32_t length) {
    if (data->flash_swap_mode != 2u)
        return;
    const uint32_t block_size = data->profile->program_flash_size / 2u;
    const uint32_t nonactive = data->flash_swap_address + block_size;
    if (start <= nonactive && nonactive - start < length)
        data->flash_swap_mode = 3u;
}

static uint8_t flash_busy_banks(uint8_t command, uint32_t address) {
    switch (command) {
    case 0x00u:
    case 0x01u:
    case 0x02u:
    case 0x03u:
    case 0x06u:
    case 0x07u:
    case 0x08u:
    case 0x09u:
    case 0x0bu:
        return (address & 0x800000u) != 0u ? 2u : 1u;
    case 0x40u:
    case 0x44u:
        return 3u;
    case 0x41u:
    case 0x43u:
    case 0x45u:
    case 0x46u:
        return 1u;
    case 0x80u:
    case 0x81u:
        return 2u;
    default:
        return 0u;
    }
}

static void flash_update_interrupts(K22Data* data) {
    interrupt(data, K22_DATA_INTERRUPT_FTFA,
              (data->flash[1] & 0x80u) != 0u && (data->flash[0] & 0x80u) != 0u);
    interrupt(data, K22_DATA_INTERRUPT_FLASH_COLLISION,
              (data->flash[1] & 0x40u) != 0u && (data->flash[0] & 0x40u) != 0u);
}

static void flash_execute(K22Data* data) {
    data->flash[0] &= 0x70u;
    const uint8_t command = flash_fccob(data, 0u);
    const uint32_t address = flash_address(data);
    const bool ftfe = k22_profile_has_peripheral(data->profile, K22_PERIPHERAL_FTFE);
    const uint32_t sector_size = ftfe ? 4096u : 2048u;
    const uint8_t program_command = ftfe ? 0x07u : 0x06u;
    const uint8_t program_words = ftfe ? 2u : 1u;
    bool valid = true;
    bool protection_failure = false;
    bool verify_failure = false;
    if (command == program_command) {
        bool data_flash = false;
        uint32_t offset = 0;
        valid = address % (program_words * 4u) == 0u &&
                flash_memory_range(data, address, program_words * 4u, &data_flash, &offset);
        protection_failure =
            valid && flash_memory_range_protected(data, address, program_words * 4u);
        if (valid && !protection_failure)
            valid = flash_program_words(data, address, program_words, &verify_failure);
    } else if (command == 0x09u) {
        bool data_flash = false;
        uint32_t offset = 0;
        const uint32_t start = address & ~(sector_size - 1u);
        valid = (address & 0x0fu) == 0u &&
                flash_memory_range(data, start, sector_size, &data_flash, &offset);
        protection_failure =
            valid && flash_memory_range_protected(data, start, sector_size);
        if (valid && !protection_failure)
            valid = flash_erase(data, start, sector_size);
        if (valid && !protection_failure)
            flash_swap_erased(data, start, sector_size);
    } else if (command == 0x08u) {
        bool data_flash = false;
        uint32_t block_size = 0u;
        uint32_t start = 0u;
        valid = (address & 0x0fu) == 0u &&
                flash_block_range(data, address, &start, &block_size, &data_flash) &&
                (ftfe || data->profile->program_flash_size == 0x80000u) &&
                !(data_flash && data->flexram_eeprom);
        protection_failure = valid && flash_memory_range_protected(data, start, block_size);
        if (valid && !protection_failure)
            valid = flash_erase(data, start, block_size);
        if (valid && !protection_failure)
            flash_swap_erased(data, start, block_size);
    } else if (command == 0x44u) {
        protection_failure =
            flash_memory_range_protected(data, 0, data->profile->program_flash_size);
        if (!protection_failure && data->profile->flexnvm_size != 0u)
            protection_failure = data->flash[0x17u] != 0xffu;
        if (!protection_failure) {
            valid = flash_erase(data, 0, data->profile->program_flash_size);
            if (valid && data->profile->flexnvm_size != 0u)
                memset(data->flexnvm, 0xff, data->profile->flexnvm_size);
            if (valid && data->flexram != NULL)
                memset(data->flexram, 0xff, data->profile->flexram_size);
            if (valid) {
                memset(data->flash_config, 0xff, sizeof(data->flash_config));
                memset(data->flash_data_ifr, 0xff, sizeof(data->flash_data_ifr));
                data->flash_partitioned = false;
                data->flexram_eeprom = false;
                data->flash[1] = (uint8_t)((data->flash[1] & 0xfcu) |
                                           (data->flexram != NULL ? 0x02u : 0u));
                data->flash[2] = 0xfeu;
                data->flash_swap_address = 0u;
                data->flash_swap_mode = 0u;
                data->flash_swap_current_block = 0u;
                data->flash_swap_next_block = 0u;
            }
        }
    } else if (command == 0x00u) {
        bool data_flash = false;
        const uint8_t margin = flash_fccob(data, 4u);
        uint32_t block_size = 0u;
        uint32_t start = 0u;
        valid = margin <= 2u && (address & 0x0fu) == 0u &&
                flash_block_range(data, address, &start, &block_size, &data_flash) &&
                (ftfe || data->profile->program_flash_size == 0x80000u) &&
                !(data_flash && data->flexram_eeprom);
        verify_failure = valid && !flash_range_erased(data, start, block_size);
    } else if (command == 0x01u) {
        const uint32_t count =
            ((uint32_t)flash_fccob(data, 4u) << 8u) | flash_fccob(data, 5u);
        const uint8_t margin = flash_fccob(data, 6u);
        const uint32_t length = count * 16u;
        bool data_flash = false;
        uint32_t offset = 0;
        valid = margin <= 2u && count != 0u && (address & 0x0fu) == 0u &&
                length <= sector_size - (address & (sector_size - 1u)) &&
                flash_memory_range(data, address, length, &data_flash, &offset);
        verify_failure = valid && !flash_range_erased(data, address, length);
    } else if (command == 0x40u) {
        const uint8_t margin = flash_fccob(data, 1u);
        valid = margin <= 2u;
        verify_failure =
            valid && !flash_range_erased(data, 0, data->profile->program_flash_size);
        if (valid && !verify_failure && data->profile->flexnvm_size != 0u)
            verify_failure = !flash_range_erased(data, 0x800000u, flash_data_size(data));
        if (valid && !verify_failure)
            data->flash[2] = (uint8_t)((data->flash[2] & 0xfcu) | 0x02u);
    } else if (command == 0x02u) {
        uint32_t actual = 0;
        const uint8_t margin = flash_fccob(data, 4u);
        const uint32_t expected = (uint32_t)flash_fccob(data, 8u) |
                                  ((uint32_t)flash_fccob(data, 9u) << 8u) |
                                  ((uint32_t)flash_fccob(data, 10u) << 16u) |
                                  ((uint32_t)flash_fccob(data, 11u) << 24u);
        bool data_flash = false;
        uint32_t offset = 0;
        valid = margin >= 1u && margin <= 2u && (address & 3u) == 0u &&
                flash_memory_range(data, address, 4u, &data_flash, &offset);
        verify_failure =
            valid && (!flash_memory_load(data, address, 4u, &actual) || actual != expected);
    } else if (command == 0x03u) {
        valid = flash_read_resource(data);
    } else if (command == 0x41u) {
        valid = flash_read_once(data);
    } else if (command == 0x43u) {
        valid = flash_program_once(data, &verify_failure);
    } else if (command == 0x0bu) {
        const uint32_t count =
            ((uint32_t)flash_fccob(data, 4u) << 8u) | flash_fccob(data, 5u);
        const uint32_t length = count * 16u;
        bool data_flash = false;
        uint32_t offset = 0;
        valid = ftfe && data->flexram != NULL && (data->flash[1] & 0x02u) != 0u &&
                count != 0u && length <= 1024u && (address & 0x0fu) == 0u &&
                length <= sector_size - (address & (sector_size - 1u)) &&
                flash_memory_range(data, address, length, &data_flash, &offset);
        protection_failure = valid && flash_memory_range_protected(data, address, length);
        if (valid && !protection_failure)
            valid = flash_program_buffer(data, address, length, &verify_failure);
    } else if (command == 0x45u) {
        valid = flash_verify_key(data);
    } else if (command == 0x80u) {
        valid = ftfe && flash_program_partition(data);
    } else if (command == 0x81u) {
        valid = ftfe && flash_set_flexram(data);
    } else if (command == 0x46u) {
        valid = ftfe && data->profile->flexnvm_size == 0u;
        if (valid)
            valid = flash_swap_control(data, address, &verify_failure);
    } else {
        valid = false;
    }
    if (protection_failure)
        data->flash[0] |= 0x10u;
    else if (!valid)
        data->flash[0] |= 0x20u;
    else if (verify_failure)
        data->flash[0] |= 1u;
    data->flash_cycles =
        command == 0x08u || command == 0x09u || command == 0x44u || command == 0x80u ? 2000u
                                                                                     : 40u;
    data->flash_busy_banks =
        valid && !protection_failure ? flash_busy_banks(command, address) : 0u;
    data->flash_busy_start = 0u;
    data->flash_busy_length = 0u;
    if (data->flash_busy_banks == 1u) {
        data->flash_busy_length = flash_block_size(data);
        data->flash_busy_start = address & ~(data->flash_busy_length - 1u);
    } else if (data->flash_busy_banks == 2u) {
        data->flash_busy_length = flash_data_size(data);
    } else if (data->flash_busy_banks == 3u) {
        data->flash_busy_length = UINT32_MAX;
    }
    flash_update_interrupts(data);
}

static bool flash_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    const uint32_t offset = address - FLASH_BASE;
    if (!valid_access(offset, size, sizeof(data->flash)))
        return false;
    const bool ftfe = k22_profile_has_peripheral(data->profile, K22_PERIPHERAL_FTFE);
    for (uint8_t index = 0; index < size; index++) {
        const uint32_t current = offset + index;
        const bool common = current <= 0x13u;
        const bool extension =
            ftfe ? current >= 0x16u && current <= 0x17u
                 : (current >= 0x18u && current <= 0x28u) || current == 0x2bu;
        if (!common && !extension)
            return false;
    }
    *value = load_bytes(data->flash, offset, size);
    return true;
}

static bool flash_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    const uint32_t offset = address - FLASH_BASE;
    if (!valid_access(offset, size, sizeof(data->flash)))
        return false;
    if (offset == 0 && size == 1) {
        data->flash[0] &= (uint8_t)~(value & 0x70u);
        if ((value & 0x80u) != 0 && (data->flash[0] & 0xb0u) == 0x80u)
            flash_execute(data);
        else
            flash_update_interrupts(data);
        return true;
    }
    if (offset == 1u && size == 1u) {
        data->flash[1] = (uint8_t)((data->flash[1] & 0x2fu) | (value & 0xd0u));
        flash_update_interrupts(data);
        return true;
    }
    if (offset >= 4u && offset <= 0x10u - size) {
        if ((data->flash[0] & 0x80u) != 0u)
            store_bytes(data->flash, offset, size, value);
        return true;
    }
    if (offset >= 0x10u && offset <= 0x14u - size) {
        if ((data->flash[0] & 0x80u) == 0u)
            return true;
        for (uint8_t index = 0; index < size; index++)
            data->flash[offset + index] &= (uint8_t)(value >> (index * 8u));
        return true;
    }
    if (k22_profile_has_peripheral(data->profile, K22_PERIPHERAL_FTFE) &&
        (offset == 0x16u || offset == 0x17u) && size == 1u) {
        if ((data->flash[0] & 0x80u) != 0u)
            data->flash[offset] &= (uint8_t)value;
        return true;
    }
    return false;
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
    if (profile_block(data, K22_PERIPHERAL_DMAMUX, NULL, &size)) {
        data->dmamux_count = (uint8_t)(size > DMA_CHANNEL_COUNT ? DMA_CHANNEL_COUNT : size);
        data->dma_channel_count = data->dmamux_count;
    }
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
        if (profile_block(data, (K22PeripheralId)(K22_PERIPHERAL_CMP0 + index), NULL, NULL))
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
    data->dma_active = 0;
    data->dma_half = 0;
    memset(data->dma_request_source, UINT8_MAX, sizeof(data->dma_request_source));
    data->dma_last_channel = (uint8_t)(data->dma_channel_count - 1u);
    data->debug_halted = false;
    for (uint8_t channel = 0u; channel < data->dma_channel_count; channel++)
        data->dma[dma_priority_offset(channel)] = channel;
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
        adc_reset_registers(&data->adc[index]);
    for (uint8_t index = 0; index < data->dac_count; index++) {
        data->dac[index].registers[0x20] = 0x02u;
        data->dac[index].registers[0x23] = 0x0fu;
    }
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

bool k22_data_dma_request(K22Data* data, uint8_t source) {
    if (data == NULL)
        return false;
    bool accepted = false;
    const uint16_t enabled = (uint16_t)load_bytes(data->dma, 0x0c, 2);
    for (uint8_t channel = 0; channel < data->dmamux_count; channel++) {
        const uint8_t mux = data->dmamux[channel];
        if ((mux & 0x80u) != 0 && (mux & 0x3fu) == source &&
            (enabled & (1u << channel)) != 0) {
            dma_queue_channel(data, channel);
            data->dma_hardware_requests |= (uint16_t)(1u << channel);
            data->dma_request_source[channel] = source;
            accepted = true;
        }
    }
    return accepted;
}

void k22_data_adc_trigger(K22Data* data, uint8_t instance) {
    k22_data_adc_pretrigger(data, instance, 0);
}

void k22_data_adc_pretrigger(K22Data* data, uint8_t instance, uint8_t pretrigger) {
    if (data != NULL && instance < data->adc_count && pretrigger < 2u &&
        (data->adc[instance].registers[0x20] & 0x40u) != 0)
        adc_start(&data->adc[instance], pretrigger);
}

bool k22_data_set_adc_input(K22Data* data, uint8_t instance, uint8_t channel,
                            uint16_t value) {
    if (data == NULL || instance >= data->adc_count || channel >= 32)
        return false;
    data->adc[instance].inputs[channel] = value;
    return true;
}

bool k22_data_set_cmp_input(K22Data* data, uint8_t instance, uint8_t input, uint8_t value) {
    if (data == NULL || instance >= data->cmp_count || input >= 8)
        return false;
    data->cmp[instance].inputs[input] = value;
    cmp_evaluate(data, instance);
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
    flash_update_interrupts(data);
    return false;
}

void k22_data_advance(K22Data* data, uint32_t cycles) {
    if (data == NULL || cycles == 0)
        return;
    while (data->dma_requests != 0u && (load_bytes(data->dma, 0u, 4u) & 0x20u) == 0u &&
           !((load_bytes(data->dma, 0u, 4u) & 2u) != 0u && data->debug_halted)) {
        const uint8_t channel = dma_select_channel(data);
        if (channel == UINT8_MAX)
            break;
        if ((load_bytes(data->dma, 0u, 4u) & 4u) == 0u && !dma_priorities_valid(data)) {
            dma_error(data, channel, 1u << 14u);
            data->dma_requests &= (uint16_t)~(1u << channel);
            data->dma_hardware_requests &= (uint16_t)~(1u << channel);
            break;
        }
        data->dma_requests &= (uint16_t)~(1u << channel);
        data->dma_hardware_requests &= (uint16_t)~(1u << channel);
        data->dma_last_channel = channel;
        const uint8_t source = data->dma_request_source[channel];
        data->dma_request_source[channel] = UINT8_MAX;
        if (dma_service_channel(data, channel) && source != UINT8_MAX &&
            data->bus.dma_complete != NULL)
            data->bus.dma_complete(data->bus.context, source);
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
            data->flash_busy_banks = 0u;
            data->flash_busy_start = 0u;
            data->flash_busy_length = 0u;
            data->flash[0] |= 0x80u;
            flash_update_interrupts(data);
        } else {
            data->flash_cycles -= cycles;
        }
    }
}

void k22_data_set_debug_halted(K22Data* data, bool halted) {
    if (data != NULL)
        data->debug_halted = halted;
}
