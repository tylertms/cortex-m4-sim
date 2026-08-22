#include "internal.h"

static uint8_t dma_transfer_size(uint8_t encoding) {
    if (encoding <= 2)
        return (uint8_t)(1u << encoding);
    return 0;
}

uint32_t k22_data_internal_dma_priority_offset(uint8_t channel) {
    return 0x100u + (channel & 0xfcu) + 3u - (channel & 3u);
}

static uint8_t dma_priority_channel(uint32_t offset) {
    const uint8_t index = (uint8_t)(offset - 0x100u);
    return (uint8_t)((index & 0xfcu) + 3u - (index & 3u));
}

bool k22_data_internal_dma_priorities_valid(const K22Data* data) {
    uint16_t used = 0u;
    for (uint8_t channel = 0u; channel < data->dma_channel_count; channel++) {
        const uint8_t priority = data->dma[k22_data_internal_dma_priority_offset(channel)] & 15u;
        if ((used & (1u << priority)) != 0u)
            return false;
        used |= (uint16_t)(1u << priority);
    }
    return true;
}

uint8_t k22_data_internal_dma_select_channel(const K22Data* data) {
    if ((k22_data_internal_load_bytes(data->dma, 0u, 4u) & 4u) != 0u) {
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
            const uint8_t priority =
                data->dma[k22_data_internal_dma_priority_offset(channel)] & 15u;
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
    uint16_t current = (uint16_t)k22_data_internal_load_bytes(descriptor, offset, 2);
    const uint16_t mask = (current & 0x8000u) != 0 ? 0x01ffu : 0x7fffu;
    current = (uint16_t)((current & ~mask) | (value & mask));
    k22_data_internal_store_bytes(descriptor, offset, 2, current);
}

void k22_data_internal_dma_update_interrupts(K22Data* data) {
    const uint16_t pending = (uint16_t)k22_data_internal_load_bytes(data->dma, 0x24, 2);
    for (uint8_t channel = 0; channel < DMA_CHANNEL_COUNT; channel++)
        k22_data_internal_interrupt(data, (K22DataInterrupt)(K22_DATA_INTERRUPT_DMA0 + channel),
                                    (pending & (1u << channel)) != 0);
    const uint16_t errors = (uint16_t)k22_data_internal_load_bytes(data->dma, 0x2c, 2);
    const uint16_t enabled = (uint16_t)k22_data_internal_load_bytes(data->dma, 0x14, 2);
    k22_data_internal_interrupt(data, K22_DATA_INTERRUPT_DMA_ERROR, (errors & enabled) != 0);
}

void k22_data_internal_dma_error(K22Data* data, uint8_t channel, uint32_t reason) {
    uint32_t status = k22_data_internal_load_bytes(data->dma, 0x04, 4);
    status |= 0x80000000u | ((uint32_t)channel << 8) | reason;
    k22_data_internal_store_bytes(data->dma, 0x04, 4, status);
    uint16_t errors = (uint16_t)k22_data_internal_load_bytes(data->dma, 0x2c, 2);
    errors |= (uint16_t)(1u << channel);
    k22_data_internal_store_bytes(data->dma, 0x2c, 2, errors);
    if ((k22_data_internal_load_bytes(data->dma, 0x14, 2) & (1u << channel)) != 0)
        k22_data_internal_interrupt(data, K22_DATA_INTERRUPT_DMA_ERROR, true);
    if ((k22_data_internal_load_bytes(data->dma, 0u, 4u) & 0x10u) != 0u)
        k22_data_internal_store_bytes(data->dma, 0u, 4u,
                                      k22_data_internal_load_bytes(data->dma, 0u, 4u) | 0x20u);
}

static bool dma_bus_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    return data->bus.read != NULL && data->bus.read(data->bus.context, address, size, value);
}

static bool dma_bus_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    return data->bus.write != NULL && data->bus.write(data->bus.context, address, size, value);
}

static void dma_queue_channel(K22Data* data, uint8_t channel) {
    if (channel < data->dma_channel_count)
        data->dma_requests |= (uint16_t)(1u << channel);
}

bool k22_data_internal_dma_source_always_enabled(const K22Data* data, uint8_t source) {
    const bool large = data->profile->id == K22_PROFILE_MK22FN1M012 ||
                       data->profile->id == K22_PROFILE_MK22FX51212;
    return source >= (large ? 54u : 60u);
}

static uint64_t dma_source_mask(const K22Data* data) {
    if (data->profile->id == K22_PROFILE_MK22FN1M012 ||
        data->profile->id == K22_PROFILE_MK22FX51212)
        return UINT64_C(0xfffffffffffffffc);
    if (data->profile->id == K22_PROFILE_MK22FN51212)
        return UINT64_C(0xfc3f6ffffffdf0fc);
    return UINT64_C(0xfc3f2f00fffdf0fc);
}

bool k22_data_internal_dma_source_valid(const K22Data* data, uint8_t source) {
    return source < 64u && (dma_source_mask(data) & (UINT64_C(1) << source)) != 0u;
}

void k22_data_internal_dma_queue_hardware_channel(K22Data* data, uint8_t channel, uint8_t source) {
    dma_queue_channel(data, channel);
    data->dma_hardware_requests |= (uint16_t)(1u << channel);
    data->dma_request_source[channel] = source;
}

void k22_data_internal_dma_queue_always_enabled(K22Data* data, uint8_t channel) {
    if (channel >= data->dma_channel_count)
        return;
    const uint8_t mux = data->dmamux[channel];
    const uint8_t source = mux & 0x3fu;
    const uint16_t enabled = (uint16_t)k22_data_internal_load_bytes(data->dma, 0x0c, 2);
    if ((mux & 0xc0u) == 0x80u && (enabled & (1u << channel)) != 0u &&
        k22_data_internal_dma_source_always_enabled(data, source))
        k22_data_internal_dma_queue_hardware_channel(data, channel, source);
}

static bool dma_copy_descriptor(K22Data* data, uint8_t* descriptor, uint32_t address) {
    for (uint8_t offset = 0; offset < DMA_TCD_SIZE; offset += 4) {
        uint32_t value = 0;
        if (!dma_bus_read(data, address + offset, 4, &value))
            return false;
        k22_data_internal_store_bytes(descriptor, offset, 4, value);
    }
    return true;
}

static void dma_complete_major(K22Data* data, uint8_t channel, uint8_t* descriptor) {
    uint32_t source = k22_data_internal_load_bytes(descriptor, 0, 4);
    uint32_t destination = k22_data_internal_load_bytes(descriptor, 0x10, 4);
    source =
        (uint32_t)((int64_t)source + (int32_t)k22_data_internal_load_bytes(descriptor, 0x0c, 4));
    destination = (uint32_t)((int64_t)destination +
                             (int32_t)k22_data_internal_load_bytes(descriptor, 0x18, 4));
    k22_data_internal_store_bytes(descriptor, 0, 4, source);
    k22_data_internal_store_bytes(descriptor, 0x10, 4, destination);
    uint16_t control = (uint16_t)k22_data_internal_load_bytes(descriptor, 0x1c, 2);
    const uint16_t beginning =
        dma_iteration_count((uint16_t)k22_data_internal_load_bytes(descriptor, 0x1e, 2));
    dma_set_iteration_count(descriptor, 0x16, beginning);
    if ((control & 0x10u) != 0) {
        const uint32_t next = k22_data_internal_load_bytes(descriptor, 0x18, 4);
        if (!dma_copy_descriptor(data, descriptor, next)) {
            k22_data_internal_dma_error(data, channel, 1u << 2);
            return;
        }
        control = (uint16_t)k22_data_internal_load_bytes(descriptor, 0x1c, 2);
    } else {
        control |= 0x80u;
        k22_data_internal_store_bytes(descriptor, 0x1c, 2, control);
    }
    if ((control & 0x08u) != 0) {
        uint16_t enable = (uint16_t)k22_data_internal_load_bytes(data->dma, 0x0c, 2);
        enable &= (uint16_t)~(1u << channel);
        k22_data_internal_store_bytes(data->dma, 0x0c, 2, enable);
    }
    if ((control & 0x02u) != 0) {
        uint16_t pending = (uint16_t)k22_data_internal_load_bytes(data->dma, 0x24, 2);
        pending |= (uint16_t)(1u << channel);
        k22_data_internal_store_bytes(data->dma, 0x24, 2, pending);
    }
    if ((control & 0x20u) != 0)
        dma_queue_channel(data, (uint8_t)((control >> 8) & 15u));
}

bool k22_data_internal_dma_service_channel(K22Data* data, uint8_t channel) {
    uint8_t* descriptor = data->dma + 0x1000u + (uint32_t)channel * DMA_TCD_SIZE;
    uint16_t current = (uint16_t)k22_data_internal_load_bytes(descriptor, 0x16, 2);
    uint16_t count = dma_iteration_count(current);
    const uint16_t beginning =
        dma_iteration_count((uint16_t)k22_data_internal_load_bytes(descriptor, 0x1e, 2));
    const uint32_t minor = k22_data_internal_load_bytes(descriptor, 8, 4);
    const bool minor_mapping = (k22_data_internal_load_bytes(data->dma, 0, 4) & 0x80u) != 0;
    const uint32_t bytes = minor_mapping ? minor & 0x3ffu : minor & 0x3fffffffu;
    int32_t minor_offset = 0;
    if (minor_mapping) {
        minor_offset = (int32_t)((minor >> 10) & 0xfffffu);
        if ((minor_offset & 0x80000) != 0)
            minor_offset |= (int32_t)0xfff00000u;
    }
    const uint16_t attributes = (uint16_t)k22_data_internal_load_bytes(descriptor, 6, 2);
    const uint8_t source_size = dma_transfer_size((uint8_t)((attributes >> 8) & 7u));
    const uint8_t destination_size = dma_transfer_size((uint8_t)(attributes & 7u));
    if (count == 0 || bytes == 0 || source_size == 0 || destination_size == 0 ||
        bytes % source_size != 0 || bytes % destination_size != 0) {
        k22_data_internal_dma_error(data, channel, 1u << 4);
        return false;
    }
    uint32_t source = k22_data_internal_load_bytes(descriptor, 0, 4);
    uint32_t destination = k22_data_internal_load_bytes(descriptor, 0x10, 4);
    const int16_t source_offset = (int16_t)k22_data_internal_load_bytes(descriptor, 4, 2);
    const int16_t destination_offset = (int16_t)k22_data_internal_load_bytes(descriptor, 0x14, 2);
    const uint8_t source_modulo = (uint8_t)((attributes >> 11) & 31u);
    const uint8_t destination_modulo = (uint8_t)((attributes >> 3) & 31u);
    uint16_t running_control = (uint16_t)k22_data_internal_load_bytes(descriptor, 0x1c, 2);
    k22_data_internal_store_bytes(descriptor, 0x1c, 2, running_control | 0x40u);
    data->dma_active |= (uint16_t)(1u << channel);
    uint64_t transfer_buffer = 0;
    uint8_t buffered_bytes = 0;
    uint32_t source_bytes = 0;
    uint32_t destination_bytes = 0;
    while (destination_bytes < bytes) {
        while (buffered_bytes < destination_size && source_bytes < bytes) {
            uint32_t value = 0;
            if (!dma_bus_read(data, source, source_size, &value)) {
                k22_data_internal_dma_error(data, channel, 1u << 2);
                data->dma_active &= (uint16_t)~(1u << channel);
                k22_data_internal_store_bytes(descriptor, 0x1c, 2, running_control);
                return false;
            }
            transfer_buffer |= (uint64_t)value << (buffered_bytes * 8u);
            buffered_bytes = (uint8_t)(buffered_bytes + source_size);
            source_bytes += source_size;
            source = dma_advance_address(source, source_offset, source_modulo);
        }
        if (buffered_bytes < destination_size ||
            !dma_bus_write(data, destination, destination_size, (uint32_t)transfer_buffer)) {
            k22_data_internal_dma_error(data, channel, 1u << 2);
            data->dma_active &= (uint16_t)~(1u << channel);
            k22_data_internal_store_bytes(descriptor, 0x1c, 2, running_control);
            return false;
        }
        destination = dma_advance_address(destination, destination_offset, destination_modulo);
        destination_bytes += destination_size;
        transfer_buffer >>= destination_size * 8u;
        buffered_bytes = (uint8_t)(buffered_bytes - destination_size);
    }
    if (minor_mapping && (minor & 0x80000000u) != 0)
        source = (uint32_t)((int64_t)source + minor_offset);
    if (minor_mapping && (minor & 0x40000000u) != 0)
        destination = (uint32_t)((int64_t)destination + minor_offset);
    data->dma_active &= (uint16_t)~(1u << channel);
    k22_data_internal_store_bytes(descriptor, 0x1c, 2, running_control);
    k22_data_internal_store_bytes(descriptor, 0, 4, source);
    k22_data_internal_store_bytes(descriptor, 0x10, 4, destination);
    count--;
    dma_set_iteration_count(descriptor, 0x16, count);
    if ((current & 0x8000u) != 0)
        dma_queue_channel(data, dma_link_channel(current));
    const uint16_t control = (uint16_t)k22_data_internal_load_bytes(descriptor, 0x1c, 2);
    if (beginning > 1 && count == beginning / 2u && (control & 0x04u) != 0 &&
        (data->dma_half & (1u << channel)) == 0) {
        data->dma_half |= (uint16_t)(1u << channel);
        uint16_t pending = (uint16_t)k22_data_internal_load_bytes(data->dma, 0x24, 2);
        k22_data_internal_store_bytes(data->dma, 0x24, 2, pending | (1u << channel));
    }
    if (count == 0) {
        data->dma_half &= (uint16_t)~(1u << channel);
        dma_complete_major(data, channel, descriptor);
    }
    k22_data_internal_dma_update_interrupts(data);
    return true;
}

bool k22_data_internal_dma_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    const uint32_t offset = address - DMA_BASE;
    if (!k22_data_internal_valid_access(offset, size, DMA_REGISTER_SIZE))
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
    *value = k22_data_internal_load_bytes(data->dma, offset, size);
    return true;
}

static void dma_command(K22Data* data, uint32_t offset, uint8_t command) {
    const uint8_t channel = command & 15u;
    uint16_t value = 0;
    if (offset == 0x18 || offset == 0x19 || offset == 0x1a || offset == 0x1b || offset == 0x1c ||
        offset == 0x1d || offset == 0x1e || offset == 0x1f) {
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
            uint16_t control = (uint16_t)k22_data_internal_load_bytes(descriptor, 0x1c, 2);
            k22_data_internal_store_bytes(descriptor, 0x1c, 2, control & ~0x80u);
            return;
        }
        value = (uint16_t)k22_data_internal_load_bytes(data->dma, register_offset, 2);
        if ((command & 0x40u) != 0)
            value = set ? 0xffffu : 0;
        else if (set)
            value |= (uint16_t)(1u << channel);
        else
            value &= (uint16_t)~(1u << channel);
        k22_data_internal_store_bytes(data->dma, register_offset, 2, value);
        if (offset == 0x1e && value == 0)
            k22_data_internal_store_bytes(data->dma, 0x04, 4, 0);
    }
}

bool k22_data_internal_dma_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    const uint32_t offset = address - DMA_BASE;
    if (!k22_data_internal_valid_access(offset, size, DMA_REGISTER_SIZE))
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
        for (uint8_t channel = 0u; channel < data->dma_channel_count; channel++)
            k22_data_internal_dma_queue_always_enabled(data, channel);
        k22_data_internal_dma_update_interrupts(data);
        return true;
    }
    if (offset == 0x04 || offset == 0x24 || offset == 0x28 || offset == 0x2c || offset == 0x30 ||
        offset == 0x34)
        return false;
    if (offset >= 0x1000u) {
        const uint32_t tcd_offset = (offset - 0x1000u) % DMA_TCD_SIZE;
        const uint8_t channel = (uint8_t)((offset - 0x1000u) / DMA_TCD_SIZE);
        if (tcd_offset == 0x1c && (size == 1 || size == 2)) {
            const uint16_t previous = (uint16_t)k22_data_internal_load_bytes(data->dma, offset, 2);
            const uint16_t status = previous & 0x00c0u;
            const uint16_t writable =
                size == 1 ? (previous & 0xff00u) | (value & 0x3eu) : value & 0xcf3eu;
            k22_data_internal_store_bytes(data->dma, offset, 2, writable | status);
            if ((value & 1u) != 0)
                dma_queue_channel(data, channel);
            return true;
        }
        if (tcd_offset == 0x1d && size == 1) {
            uint16_t control = (uint16_t)k22_data_internal_load_bytes(data->dma, offset - 1, 2);
            control = (uint16_t)((control & 0x00ffu) | ((value & 0x7fu) << 8));
            k22_data_internal_store_bytes(data->dma, offset - 1, 2, control);
            return true;
        }
    }
    k22_data_internal_store_bytes(data->dma, offset, size, value);
    if (offset <= 0x0du && offset + size > 0x0cu)
        for (uint8_t channel = 0u; channel < data->dma_channel_count; channel++)
            k22_data_internal_dma_queue_always_enabled(data, channel);
    return true;
}

bool k22_data_internal_dmamux_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    const uint32_t offset = address - DMAMUX_BASE;
    if (!k22_data_internal_valid_access(offset, size, data->dmamux_count))
        return false;
    *value = k22_data_internal_load_bytes(data->dmamux, offset, size);
    return true;
}

bool k22_data_internal_dmamux_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    const uint32_t offset = address - DMAMUX_BASE;
    if (!k22_data_internal_valid_access(offset, size, data->dmamux_count))
        return false;
    k22_data_internal_store_bytes(data->dmamux, offset, size, value);
    for (uint8_t channel = (uint8_t)offset; channel < offset + size; channel++) {
        data->dma_trigger_waiting &= (uint16_t)~(1u << channel);
        k22_data_internal_dma_queue_always_enabled(data, channel);
    }
    return true;
}
