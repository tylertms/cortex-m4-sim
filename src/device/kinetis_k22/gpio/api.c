#include "device/kinetis_k22/gpio/internal.h"

bool k22_io_read(K22Io* io, uint32_t address, uint8_t size, uint32_t* value) {
    if (io == NULL || value == NULL || !k22_io_internal_valid_size(size))
        return false;
    if (address >= K22_BIT_BAND_BASE && address < K22_BIT_BAND_LIMIT && size == 4) {
        const uint32_t alias = address - K22_BIT_BAND_BASE;
        const uint32_t byte_address = K22_PERIPHERAL_BASE + alias / 32u;
        const uint8_t bit = (uint8_t)((alias / 4u) & 7u);
        uint32_t byte = 0;
        if (!k22_io_internal_read_direct(io, byte_address, 1, &byte))
            return false;
        *value = (byte >> bit) & 1u;
        return true;
    }
    return k22_io_internal_read_direct(io, address, size, value);
}

bool k22_io_write(K22Io* io, uint32_t address, uint8_t size, uint32_t value) {
    if (io == NULL || !k22_io_internal_valid_size(size))
        return false;
    if (address >= K22_BIT_BAND_BASE && address < K22_BIT_BAND_LIMIT && size == 4) {
        const uint32_t alias = address - K22_BIT_BAND_BASE;
        const uint32_t byte_address = K22_PERIPHERAL_BASE + alias / 32u;
        const uint8_t bit = (uint8_t)((alias / 4u) & 7u);
        uint32_t byte = 0;
        if (!k22_io_internal_read_direct(io, byte_address, 1, &byte))
            return false;
        byte = (value & 1u) != 0 ? byte | (1u << bit) : byte & ~(1u << bit);
        return k22_io_internal_write_direct(io, byte_address, 1, byte);
    }
    return k22_io_internal_write_direct(io, address, size, value);
}

bool k22_io_drive_pin(K22Io* io, uint8_t port, uint8_t pin, bool high) {
    if (io == NULL || !k22_io_internal_pin_exists(io, port, pin))
        return false;
    const uint32_t bit = 1u << pin;
    const bool previous = (k22_io_internal_pin_level(io, port) & bit) != 0;
    io->gpio_external_drive[port] |= bit;
    if (high)
        io->gpio_external[port] |= bit;
    else
        io->gpio_external[port] &= ~bit;
    const bool target = (k22_io_internal_pin_level_unfiltered(io, port) & bit) != 0;
    if ((io->port_dfer[port] & bit) != 0) {
        io->gpio_filter_age[port][pin] = 0;
        if (target)
            io->gpio_pending[port] |= bit;
        else
            io->gpio_pending[port] &= ~bit;
    } else
        k22_io_internal_commit_pin_level(io, port, pin, previous, target);
    return true;
}

bool k22_io_release_pin(K22Io* io, uint8_t port, uint8_t pin) {
    if (io == NULL || !k22_io_internal_pin_exists(io, port, pin))
        return false;
    const uint32_t bit = 1u << pin;
    const bool previous = (k22_io_internal_pin_level(io, port) & bit) != 0;
    io->gpio_external_drive[port] &= ~bit;
    const bool target = (k22_io_internal_pin_level_unfiltered(io, port) & bit) != 0;
    if ((io->port_dfer[port] & bit) != 0) {
        io->gpio_filter_age[port][pin] = 0;
        if (target)
            io->gpio_pending[port] |= bit;
        else
            io->gpio_pending[port] &= ~bit;
    } else
        k22_io_internal_commit_pin_level(io, port, pin, previous, target);
    return true;
}

uint32_t k22_io_pin_input(const K22Io* io, uint8_t port) {
    return io == NULL || port >= K22_IO_PORT_COUNT ? 0 : k22_io_internal_pin_level(io, port);
}

bool k22_io_usb_token(K22Io* io, uint8_t endpoint, uint8_t token, bool transmit) {
    if (io == NULL || endpoint >= 16 || !k22_io_clock_enabled(io, K22_PERIPHERAL_USB0) ||
        (io->usb[K22_USB_CTL] & 1u) == 0)
        return false;
    io->usb[K22_USB_STAT] = (uint8_t)((endpoint << 4) | (transmit ? 8u : 0));
    io->usb[K22_USB_ISTAT] |= 1u << 3;
    k22_io_internal_emit(io, K22_IO_EVENT_USB_TOKEN, endpoint, token, transmit);
    if ((io->usb[K22_USB_INTEN] & (1u << 3)) != 0)
        k22_io_internal_emit(io, K22_IO_EVENT_IRQ, 53u, 1u << 3, endpoint);
    return true;
}

bool k22_io_can_receive(K22Io* io, const K22CanFrame* frame) {
    if (io == NULL || frame == NULL || frame->length > 8 ||
        !k22_io_clock_enabled(io, K22_PERIPHERAL_CAN0) ||
        (io->can[K22_CAN_MCR / 4] & (1u << 31)) != 0)
        return false;
    const uint8_t maximum = (uint8_t)(io->can[K22_CAN_MCR / 4] & 0x7fu);
    for (uint8_t mailbox = 0; mailbox <= maximum && mailbox < 16; mailbox++) {
        const uint32_t offset = K22_CAN_MB_BASE + (uint32_t)mailbox * 16u;
        const uint8_t code = (uint8_t)(io->can[offset / 4u] >> 24) & 15u;
        if (code != 4u)
            continue;
        const uint32_t configured = io->can[(offset + 4u) / 4u];
        const uint32_t mask = mailbox == 14   ? io->can[K22_CAN_RX14MASK / 4]
                              : mailbox == 15 ? io->can[K22_CAN_RX15MASK / 4]
                                              : io->can[K22_CAN_RXMGMASK / 4];
        if (((configured ^ frame->identifier) & mask) != 0)
            continue;
        uint32_t cs = (2u << 24) | ((uint32_t)frame->length << 16);
        if (frame->extended)
            cs |= 1u << 21;
        if (frame->remote)
            cs |= 1u << 20;
        io->can[offset / 4u] = cs | (io->can[K22_CAN_TIMER / 4] & 0xffffu);
        io->can[(offset + 4u) / 4u] = frame->identifier;
        uint32_t high = 0;
        uint32_t low = 0;
        for (uint8_t index = 0; index < 4; index++) {
            high = (high << 8) | frame->data[index];
            low = (low << 8) | frame->data[index + 4u];
        }
        io->can[(offset + 8u) / 4u] = high;
        io->can[(offset + 12u) / 4u] = low;
        io->can[K22_CAN_IFLAG1 / 4] |= 1u << mailbox;
        if ((io->can[K22_CAN_IMASK1 / 4] & (1u << mailbox)) != 0)
            k22_io_internal_emit(io, K22_IO_EVENT_IRQ, 75u, 1u << mailbox, 0);
        return true;
    }
    return false;
}

bool k22_io_i2s_receive(K22Io* io, uint32_t sample) {
    if (io == NULL || !k22_io_clock_enabled(io, K22_PERIPHERAL_I2S0) ||
        (io->i2s[K22_I2S_RCSR / 4] & K22_I2S_ENABLE) == 0 ||
        !k22_io_internal_fifo_push(io->i2s_receive_fifo, &io->i2s_receive_write,
                                   &io->i2s_receive_count, sample))
        return false;
    k22_io_internal_update_i2s_requests(io);
    return true;
}

bool k22_io_i2s_transmit(K22Io* io, uint32_t* sample) {
    if (io == NULL || sample == NULL ||
        !k22_io_internal_fifo_pop(io->i2s_transmit_fifo, &io->i2s_transmit_read,
                                  &io->i2s_transmit_count, sample))
        return false;
    k22_io_internal_update_i2s_requests(io);
    return true;
}

static bool i2s_irq_asserted(uint32_t control) {
    return ((control & (1u << 8)) != 0 && (control & K22_I2S_REQUEST_FLAG) != 0) ||
           ((control & (1u << 10)) != 0 && (control & K22_I2S_FIFO_ERROR) != 0) ||
           ((control & (1u << 11)) != 0 && (control & (1u << 19)) != 0) ||
           ((control & (1u << 12)) != 0 && (control & (1u << 20)) != 0);
}

bool k22_io_irq_asserted(const K22Io* io, uint8_t irq) {
    if (io == NULL)
        return false;
    if (irq >= 59u && irq <= 63u) {
        const uint8_t port = (uint8_t)(irq - 59u);
        uint32_t pending = io->port_isfr[port];
        while (pending != 0) {
            const uint8_t pin = k22_io_internal_first_set_bit(pending);
            const uint32_t irqc = (io->port_pcr[port][pin] >> 16) & 15u;
            if (irqc >= 8u && irqc <= 12u)
                return true;
            pending &= ~(1u << pin);
        }
        return false;
    }
    if (irq == 53u)
        return (io->usb[K22_USB_ISTAT] & io->usb[K22_USB_INTEN]) != 0;
    if (irq == 75u)
        return (io->can[K22_CAN_IFLAG1 / 4u] & io->can[K22_CAN_IMASK1 / 4u]) != 0;
    if (irq == 28u)
        return i2s_irq_asserted(io->i2s[K22_I2S_TCSR / 4u]);
    if (irq == 29u)
        return i2s_irq_asserted(io->i2s[K22_I2S_RCSR / 4u]);
    return false;
}

static bool sysmpu_permission(uint32_t access_control, uint8_t master, bool supervisor,
                              K22SysMpuAccess access) {
    if (master < 4u) {
        const uint8_t shift = master * 6u;
        const uint8_t user = (uint8_t)(access_control >> shift) & 7u;
        if (!supervisor)
            return (user & (1u << (2u - access))) != 0;
        const uint8_t mode = (uint8_t)(access_control >> (shift + 3u)) & 3u;
        if (mode == 0)
            return true;
        if (mode == 1u)
            return access != K22_SYSMPU_WRITE;
        if (mode == 2u)
            return access != K22_SYSMPU_EXECUTE;
        return (user & (1u << (2u - access))) != 0;
    }
    if (access == K22_SYSMPU_EXECUTE)
        return false;
    const uint8_t shift = (uint8_t)(24u + (master - 4u) * 2u);
    const uint8_t bit = access == K22_SYSMPU_WRITE ? 0 : 1;
    return (access_control & (1u << (shift + bit))) != 0;
}

bool k22_io_sysmpu_access(K22Io* io, uint32_t address, uint8_t master, bool supervisor,
                          K22SysMpuAccess access) {
    if (io == NULL || master >= 8u || access > K22_SYSMPU_EXECUTE ||
        !k22_profile_has_peripheral(io->configuration.profile, K22_PERIPHERAL_SYSMPU) ||
        !k22_io_clock_enabled(io, K22_PERIPHERAL_SYSMPU))
        return false;
    if ((io->sysmpu[0] & 1u) == 0)
        return true;
    for (uint8_t region = 0; region < 12; region++) {
        const uint32_t descriptor = 0x400u / 4u + (uint32_t)region * 4u;
        if ((io->sysmpu[descriptor + 3u] & 1u) == 0 || address < io->sysmpu[descriptor] ||
            address > io->sysmpu[descriptor + 1u])
            continue;
        if (sysmpu_permission(io->sysmpu[descriptor + 2u], master, supervisor, access))
            return true;
    }
    io->sysmpu[0] |= 1u << 27;
    io->sysmpu[0x10u / 4u] = address;
    io->sysmpu[0x14u / 4u] = (uint32_t)master << 24 | (uint32_t)access << 20;
    k22_io_internal_emit(io, K22_IO_EVENT_ACCESS_ERROR, address, master, access);
    return false;
}

void k22_io_advance(K22Io* io, uint32_t cycles) {
    if (io == NULL || cycles == 0)
        return;
    for (uint8_t port = 0; port < K22_IO_PORT_COUNT; port++) {
        uint32_t filtered = io->port_dfer[port] & io->configuration.package_pin_mask[port];
        while (filtered != 0) {
            const uint8_t pin = k22_io_internal_first_set_bit(filtered);
            const uint32_t bit = 1u << pin;
            const bool target = (io->gpio_pending[port] & bit) != 0;
            const bool current = (io->gpio_filtered[port] & bit) != 0;
            if (target != current) {
                uint64_t age = (uint64_t)io->gpio_filter_age[port][pin] + cycles;
                const uint32_t threshold = (uint32_t)io->port_dfwr[port] + 1u;
                if (age >= threshold) {
                    k22_io_internal_commit_pin_level(io, port, pin, current, target);
                    age = 0;
                }
                io->gpio_filter_age[port][pin] = age > UINT8_MAX ? UINT8_MAX : (uint8_t)age;
            }
            filtered &= ~bit;
        }
    }
    if (k22_io_clock_enabled(io, K22_PERIPHERAL_USB0) && (io->usb[K22_USB_CTL] & 1u) != 0) {
        const uint64_t elapsed = (uint64_t)io->usb_cycle_remainder + cycles;
        const uint32_t frames = (uint32_t)(elapsed / 1000u);
        io->usb_cycle_remainder = (uint32_t)(elapsed % 1000u);
        if (frames != 0) {
            uint16_t frame =
                (uint16_t)io->usb[K22_USB_FRMNUML] | ((uint16_t)io->usb[K22_USB_FRMNUMH] << 8);
            frame = (uint16_t)((frame + frames) & 0x7ffu);
            io->usb[K22_USB_FRMNUML] = (uint8_t)frame;
            io->usb[K22_USB_FRMNUMH] = (uint8_t)(frame >> 8);
            io->usb[K22_USB_ISTAT] |= 1u << 2;
            if ((io->usb[K22_USB_INTEN] & (1u << 2)) != 0)
                k22_io_internal_emit(io, K22_IO_EVENT_IRQ, 53u, 1u << 2, frame);
        }
    }
    if (k22_io_clock_enabled(io, K22_PERIPHERAL_CAN0) &&
        (io->can[K22_CAN_MCR / 4] & ((1u << 31) | (1u << 28))) == 0)
        io->can[K22_CAN_TIMER / 4] += cycles;
}
