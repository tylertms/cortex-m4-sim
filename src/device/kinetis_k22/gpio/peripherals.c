#include "device/kinetis_k22/gpio/internal.h"

static bool usb_offset_valid(uint32_t offset) {
    if (offset <= 0x1cu)
        return (offset & 3u) == 0;
    if (offset >= 0x80u && offset <= 0xbcu)
        return (offset & 3u) == 0;
    if (offset >= 0xc0u && offset <= 0xfcu)
        return (offset & 3u) == 0;
    return offset == 0x100u || offset == 0x104u || offset == 0x108u || offset == 0x10cu ||
           offset == 0x110u || offset == 0x114u || offset == 0x140u || offset == 0x144u ||
           offset == 0x148u || offset == 0x14cu || offset == 0x154u || offset == 0x158u ||
           offset == 0x15cu;
}

static bool read_usb(K22Io* io, K22PeripheralLocation location, uint8_t size, uint32_t* value) {
    if (size != 1 || !usb_offset_valid(location.offset))
        return false;
    *value = io->usb[location.offset];
    return true;
}

static bool write_usb(K22Io* io, K22PeripheralLocation location, uint8_t size, uint32_t value) {
    if (size != 1 || !usb_offset_valid(location.offset) || location.offset <= 0x0cu ||
        location.offset == K22_USB_STAT || location.offset == K22_USB_FRMNUML ||
        location.offset == K22_USB_FRMNUMH)
        return false;
    if (location.offset == 0x10u || location.offset == K22_USB_ISTAT || location.offset == 0x88u)
        io->usb[location.offset] &= (uint8_t)~value;
    else if (location.offset == 0xd0u && (value & 0x80u) != 0) {
        const uint8_t ids[4] = {io->usb[0], io->usb[4], io->usb[8], io->usb[0x0c]};
        memset(io->usb, 0, sizeof(io->usb));
        io->usb[0] = ids[0];
        io->usb[4] = ids[1];
        io->usb[8] = ids[2];
        io->usb[0x0c] = ids[3];
    } else
        io->usb[location.offset] = (uint8_t)value;
    return true;
}

static bool read_words(const uint32_t* words, uint32_t length, uint32_t offset, uint8_t size,
                       uint32_t* value) {
    if (size != 4 || (offset & 3u) != 0 || offset >= length)
        return false;
    *value = words[offset / 4u];
    return true;
}

static bool can_offset_valid(uint32_t offset) {
    switch (offset) {
    case 0:
    case 4:
    case 8:
    case 0x10:
    case 0x14:
    case 0x18:
    case 0x1c:
    case 0x20:
    case 0x24:
    case 0x28:
    case 0x2c:
    case 0x30:
    case 0x34:
    case 0x38:
    case 0x44:
    case 0x48:
    case 0x4c:
        return true;
    default:
        return (offset >= 0x80u && offset < 0x180u) || (offset >= 0x880u && offset < 0x8c0u);
    }
}

static bool write_can(K22Io* io, uint32_t offset, uint8_t size, uint32_t value) {
    if (size != 4 || (offset & 3u) != 0 || !can_offset_valid(offset))
        return false;
    if (offset == K22_CAN_TIMER || offset == 0x1cu || offset == 0x38u || offset == 0x44u ||
        offset == 0x4cu)
        return false;
    if (offset == K22_CAN_IFLAG1) {
        io->can[offset / 4u] &= ~value;
        return true;
    }
    if (offset == K22_CAN_MCR && (value & (1u << 25)) != 0) {
        const bool clock = io->clock_enabled[K22_PERIPHERAL_CAN0];
        memset(io->can, 0, sizeof(io->can));
        io->can[K22_CAN_MCR / 4] = 0xd890000fu;
        io->can[K22_CAN_RXMGMASK / 4] = UINT32_MAX;
        io->can[K22_CAN_RX14MASK / 4] = UINT32_MAX;
        io->can[K22_CAN_RX15MASK / 4] = UINT32_MAX;
        io->clock_enabled[K22_PERIPHERAL_CAN0] = clock;
        return true;
    }
    io->can[offset / 4u] = value;
    if (offset >= K22_CAN_MB_BASE && offset < K22_CAN_MB_BASE + 16u * 16u && (offset & 15u) == 0) {
        const uint8_t mailbox = (uint8_t)((offset - K22_CAN_MB_BASE) / 16u);
        const uint8_t code = (uint8_t)(value >> 24) & 15u;
        if (code >= 0xcu && code <= 0xfu) {
            const uint32_t id = io->can[(offset + 4u) / 4u];
            k22_io_internal_emit_can(io, mailbox, id, value, io->can[(offset + 8u) / 4u],
                                     io->can[(offset + 12u) / 4u]);
            io->can[offset / 4u] = (value & ~(15u << 24)) | (8u << 24);
            io->can[K22_CAN_IFLAG1 / 4] |= 1u << mailbox;
            if ((io->can[K22_CAN_IMASK1 / 4] & (1u << mailbox)) != 0)
                k22_io_internal_emit(io, K22_IO_EVENT_IRQ, 75u, 1u << mailbox, 0);
        }
    }
    return true;
}

bool k22_io_internal_fifo_push(uint32_t* fifo, uint8_t* write, uint8_t* count, uint32_t value) {
    if (*count == K22_IO_FIFO_CAPACITY)
        return false;
    fifo[*write] = value;
    *write = (uint8_t)((*write + 1u) % K22_IO_FIFO_CAPACITY);
    (*count)++;
    return true;
}

bool k22_io_internal_fifo_pop(uint32_t* fifo, uint8_t* read, uint8_t* count, uint32_t* value) {
    if (*count == 0)
        return false;
    *value = fifo[*read];
    *read = (uint8_t)((*read + 1u) % K22_IO_FIFO_CAPACITY);
    (*count)--;
    return true;
}

void k22_io_internal_update_i2s_requests(K22Io* io) {
    uint32_t* transmit = &io->i2s[K22_I2S_TCSR / 4];
    uint32_t* receive = &io->i2s[K22_I2S_RCSR / 4];
    if (io->i2s_transmit_count < K22_IO_FIFO_CAPACITY)
        *transmit |= K22_I2S_REQUEST_FLAG;
    else
        *transmit &= ~K22_I2S_REQUEST_FLAG;
    if (io->i2s_receive_count != 0)
        *receive |= K22_I2S_REQUEST_FLAG;
    else
        *receive &= ~K22_I2S_REQUEST_FLAG;
    if ((*transmit & K22_I2S_REQUEST_FLAG) != 0) {
        if ((*transmit & 1u) != 0)
            k22_io_internal_emit(io, K22_IO_EVENT_DMA, 0, io->i2s_transmit_count, 0);
        if ((*transmit & (1u << 8)) != 0)
            k22_io_internal_emit(io, K22_IO_EVENT_IRQ, 28u, K22_I2S_REQUEST_FLAG, 0);
    }
    if ((*receive & K22_I2S_REQUEST_FLAG) != 0) {
        if ((*receive & 1u) != 0)
            k22_io_internal_emit(io, K22_IO_EVENT_DMA, 1, io->i2s_receive_count, 0);
        if ((*receive & (1u << 8)) != 0)
            k22_io_internal_emit(io, K22_IO_EVENT_IRQ, 29u, K22_I2S_REQUEST_FLAG, 0);
    }
}

static bool i2s_offset_valid(uint32_t offset) {
    return offset <= 0x14u || offset == 0x20u || offset == 0x24u || offset == 0x40u ||
           offset == 0x44u || offset == 0x60u || (offset >= 0x80u && offset <= 0x94u) ||
           offset == 0xa0u || offset == 0xa4u || offset == 0xc0u || offset == 0xc4u ||
           offset == 0xe0u || offset == 0x100u || offset == 0x104u;
}

static bool read_i2s(K22Io* io, uint32_t offset, uint8_t size, uint32_t* value) {
    if (size != 4 || (offset & 3u) != 0 || !i2s_offset_valid(offset))
        return false;
    if (offset >= K22_I2S_RDR0 && offset < K22_I2S_RDR0 + 8u) {
        if (!k22_io_internal_fifo_pop(io->i2s_receive_fifo, &io->i2s_receive_read,
                                      &io->i2s_receive_count, value)) {
            io->i2s[K22_I2S_RCSR / 4] |= K22_I2S_FIFO_ERROR;
            *value = 0;
        }
        k22_io_internal_update_i2s_requests(io);
        return true;
    }
    *value = io->i2s[offset / 4u];
    return true;
}

static bool write_i2s(K22Io* io, uint32_t offset, uint8_t size, uint32_t value) {
    if (size != 4 || (offset & 3u) != 0 || !i2s_offset_valid(offset) || offset == 0x40u ||
        offset == 0x44u || offset == 0xc0u || offset == 0xc4u)
        return false;
    if (offset >= K22_I2S_TDR0 && offset < K22_I2S_TDR0 + 8u) {
        if (!k22_io_internal_fifo_push(io->i2s_transmit_fifo, &io->i2s_transmit_write,
                                       &io->i2s_transmit_count, value)) {
            io->i2s[K22_I2S_TCSR / 4] |= K22_I2S_FIFO_ERROR;
            return true;
        }
        k22_io_internal_emit(io, K22_IO_EVENT_I2S_TRANSMIT, (offset - K22_I2S_TDR0) / 4u, value,
                             io->i2s_transmit_count);
        k22_io_internal_update_i2s_requests(io);
        return true;
    }
    if (offset == K22_I2S_TCSR || offset == K22_I2S_RCSR) {
        const uint32_t flags =
            K22_I2S_REQUEST_FLAG | K22_I2S_FIFO_ERROR | (1u << 17) | (1u << 19) | (1u << 20);
        uint32_t previous = io->i2s[offset / 4u];
        previous &= ~(value & flags);
        io->i2s[offset / 4u] = (value & ~flags) | (previous & flags);
        if ((value & K22_I2S_ENABLE) == 0) {
            if (offset == K22_I2S_TCSR)
                io->i2s_transmit_count = io->i2s_transmit_read = io->i2s_transmit_write = 0;
            else
                io->i2s_receive_count = io->i2s_receive_read = io->i2s_receive_write = 0;
        }
        k22_io_internal_update_i2s_requests(io);
        return true;
    }
    io->i2s[offset / 4u] = value;
    return true;
}

static bool read_flash_configuration(K22Io* io, K22PeripheralLocation location, uint8_t size,
                                     uint32_t* value) {
    if (location.offset + size > sizeof(io->configuration.flash_configuration))
        return false;
    *value =
        k22_io_internal_load_bytes(io->configuration.flash_configuration + location.offset, size);
    return true;
}

static bool flexbus_offset_valid(uint32_t offset) {
    return (offset < 0x48u && (offset % 12u) <= 8u) || offset == 0x60u;
}

bool k22_io_flexbus_transfer(K22Io* io, uint32_t address, uint8_t size, bool write,
                             uint32_t value) {
    if (io == NULL || !k22_io_clock_enabled(io, K22_PERIPHERAL_FB) ||
        (size != 1u && size != 2u && size != 4u))
        return false;
    for (uint8_t chip_select = 0u; chip_select < 6u; chip_select++) {
        const uint32_t base = io->flexbus[chip_select * 3u] & 0xffff0000u;
        const uint32_t block = io->flexbus[chip_select * 3u + 1u];
        if ((block & 1u) == 0u)
            continue;
        const uint32_t comparison = ~(block & 0xffff0000u) & 0xffff0000u;
        if ((address & comparison) != (base & comparison))
            continue;
        k22_io_internal_emit(io, K22_IO_EVENT_FLEXBUS_TRANSFER, chip_select, value,
                             (uint32_t)size | (write ? 0x100u : 0u));
        return true;
    }
    return false;
}
static bool mcm_offset_valid(const K22Io* io, uint32_t offset) {
    if (offset == 0 || offset == 2u || offset == 4u || offset == 8u)
        return true;
    const bool large_profile = io->configuration.profile->id == K22_PROFILE_MK22FN1M012 ||
                               io->configuration.profile->id == K22_PROFILE_MK22FX51212;
    if (large_profile)
        return offset == 0x0cu || offset == 0x10u || offset == 0x14u || offset == 0x28u;
    return offset == 0x38u;
}

static bool sysmpu_offset_valid(uint32_t offset) {
    if (offset == 0)
        return true;
    if (offset >= 0x10u && offset <= 0x54u)
        return (offset & 0x0bu) == 0;
    return (offset >= 0x400u && offset < 0x4c0u) || (offset >= 0x800u && offset < 0x830u);
}

bool k22_io_internal_read_direct(K22Io* io, uint32_t address, uint8_t size, uint32_t* value) {
    K22PeripheralLocation location;
    if (!k22_profile_resolve_peripheral(io->configuration.profile, address, size, &location))
        return false;
    if (!k22_io_internal_module_clocked(io, location.id)) {
        k22_io_internal_emit(io, K22_IO_EVENT_ACCESS_ERROR, address, size, 0);
        return false;
    }
    if (location.id == K22_PERIPHERAL_FLASH_CONFIG)
        return read_flash_configuration(io, location, size, value);
    if (k22_io_internal_is_port(location.id))
        return k22_io_internal_read_port(io, location, size, value);
    if (k22_io_internal_is_gpio(location.id))
        return k22_io_internal_read_gpio(io, location, size, value);
    if (location.id == K22_PERIPHERAL_USB0)
        return read_usb(io, location, size, value);
    if (location.id == K22_PERIPHERAL_CAN0)
        return can_offset_valid(location.offset) &&
               read_words(io->can, sizeof(io->can), location.offset, size, value);
    if (location.id == K22_PERIPHERAL_I2S0)
        return read_i2s(io, location.offset, size, value);
    if (location.id == K22_PERIPHERAL_FB)
        return flexbus_offset_valid(location.offset) &&
               read_words(io->flexbus, sizeof(io->flexbus), location.offset, size, value);
    if (location.id == K22_PERIPHERAL_MCM) {
        if (!mcm_offset_valid(io, location.offset))
            return false;
        if (location.offset == 0 && size == 4) {
            *value = io->mcm[0];
            return true;
        }
        if (location.offset == 0 || location.offset == 2u) {
            if (size != 2)
                return false;
            *value = (io->mcm[0] >> (location.offset * 8u)) & 0xffffu;
            return true;
        }
        return read_words(io->mcm, sizeof(io->mcm), location.offset, size, value);
    }
    if (location.id == K22_PERIPHERAL_SYSMPU) {
        if (!sysmpu_offset_valid(location.offset))
            return false;
        if (size == 4 && location.offset >= 0x800u && location.offset < 0x830u) {
            const uint32_t region = (location.offset - 0x800u) / 4u;
            *value = io->sysmpu[0x400u / 4u + region * 4u + 2u];
            return true;
        }
        return read_words(io->sysmpu, sizeof(io->sysmpu), location.offset, size, value);
    }
    return false;
}

bool k22_io_internal_write_direct(K22Io* io, uint32_t address, uint8_t size, uint32_t value) {
    K22PeripheralLocation location;
    if (!k22_profile_resolve_peripheral(io->configuration.profile, address, size, &location))
        return false;
    if (!k22_io_internal_module_clocked(io, location.id)) {
        k22_io_internal_emit(io, K22_IO_EVENT_ACCESS_ERROR, address, value, size);
        return false;
    }
    if (location.id == K22_PERIPHERAL_FLASH_CONFIG)
        return false;
    if (k22_io_internal_is_port(location.id))
        return k22_io_internal_write_port(io, location, size, value);
    if (k22_io_internal_is_gpio(location.id))
        return k22_io_internal_write_gpio(io, location, size, value);
    if (location.id == K22_PERIPHERAL_USB0)
        return write_usb(io, location, size, value);
    if (location.id == K22_PERIPHERAL_CAN0)
        return write_can(io, location.offset, size, value);
    if (location.id == K22_PERIPHERAL_I2S0)
        return write_i2s(io, location.offset, size, value);
    if (location.id == K22_PERIPHERAL_FB) {
        if (size != 4 || (location.offset & 3u) != 0 || !flexbus_offset_valid(location.offset))
            return false;
        io->flexbus[location.offset / 4u] = value;
        if (location.offset < 0x48u && (location.offset % 12u) == 4u && (value & 1u) != 0)
            k22_io_internal_emit(io, K22_IO_EVENT_FLEXBUS_TRANSFER, location.offset / 12u, value,
                                 0);
        return true;
    }
    if (location.id == K22_PERIPHERAL_MCM) {
        if (size != 4 || (location.offset & 3u) != 0 || location.offset == 0 ||
            !mcm_offset_valid(io, location.offset) || location.offset == 0x14u)
            return false;
        if (location.offset == 0x28u)
            value &= 0xffu;
        io->mcm[location.offset / 4u] = value;
        return true;
    }
    if (location.id == K22_PERIPHERAL_SYSMPU) {
        if (size != 4 || (location.offset & 3u) != 0 || !sysmpu_offset_valid(location.offset))
            return false;
        if (location.offset >= 0x10u && location.offset < 0x70u)
            return false;
        if (location.offset == 0) {
            const uint32_t fixed = io->sysmpu[0] & 0x00ffff00u;
            const uint32_t errors = io->sysmpu[0] & 0xf8000000u & ~value;
            io->sysmpu[0] = fixed | errors | (value & 1u);
        } else if (location.offset >= 0x800u && location.offset < 0x830u) {
            const uint32_t region = (location.offset - 0x800u) / 4u;
            io->sysmpu[0x400u / 4u + region * 4u + 2u] = value;
            io->sysmpu[location.offset / 4u] = value;
        } else {
            io->sysmpu[location.offset / 4u] = value;
            if (location.offset >= 0x408u && location.offset < 0x4c0u &&
                (location.offset - 0x408u) % 16u == 0) {
                const uint32_t region = (location.offset - 0x408u) / 16u;
                io->sysmpu[0x800u / 4u + region] = value;
            }
        }
        return true;
    }
    return false;
}
