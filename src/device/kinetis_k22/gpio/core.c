#include "device/kinetis_k22/gpio/internal.h"
#include "device/kinetis_k22/gpio/io.h"

#include <string.h>

#include "device/kinetis_k22/variants/manifest.h"

uint32_t k22_io_internal_load_bytes(const uint8_t* data, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; index++)
        value |= (uint32_t)data[index] << (index * 8u);
    return value;
}

static uint32_t width_mask(uint8_t size) {
    return size == 4 ? UINT32_MAX : (1u << (size * 8u)) - 1u;
}

static uint32_t merge_value(uint32_t previous, uint32_t offset, uint8_t size, uint32_t value) {
    const uint32_t shift = (offset & 3u) * 8u;
    const uint32_t mask = width_mask(size) << shift;
    return (previous & ~mask) | ((value << shift) & mask);
}

void k22_io_internal_emit(K22Io* io, K22IoEventType type, uint32_t source, uint32_t value,
                          uint32_t auxiliary) {
    if (io->configuration.event_handler == NULL)
        return;
    const K22IoEvent event = {
        .type = type, .source = source, .value = value, .auxiliary = auxiliary};
    io->configuration.event_handler(io->configuration.event_context, &event);
}

void k22_io_internal_emit_can(K22Io* io, uint8_t mailbox, uint32_t identifier, uint32_t control,
                              uint32_t high, uint32_t low) {
    if (io->configuration.event_handler == NULL)
        return;
    K22IoEvent event = {K22_IO_EVENT_CAN_TRANSMIT,
                        mailbox,
                        identifier,
                        (control >> 16) & 15u,
                        {0},
                        0,
                        false,
                        false};
    event.length = (uint8_t)event.auxiliary;
    if (event.length > 8)
        event.length = 8;
    event.extended = (control & (1u << 21)) != 0;
    event.remote = (control & (1u << 20)) != 0;
    for (uint8_t index = 0; index < 4; index++) {
        event.data[index] = (uint8_t)(high >> ((3u - index) * 8u));
        event.data[index + 4u] = (uint8_t)(low >> ((3u - index) * 8u));
    }
    io->configuration.event_handler(io->configuration.event_context, &event);
}

bool k22_io_internal_valid_size(uint8_t size) { return size == 1 || size == 2 || size == 4; }

uint8_t k22_io_internal_first_set_bit(uint32_t value) {
    uint8_t bit = 0;
    while ((value & 1u) == 0) {
        value >>= 1;
        bit++;
    }
    return bit;
}

bool k22_io_internal_pin_exists(const K22Io* io, uint8_t port, uint8_t pin) {
    return port < K22_IO_PORT_COUNT && pin < K22_IO_PIN_COUNT &&
           (io->configuration.package_pin_mask[port] & (1u << pin)) != 0;
}

static uint8_t port_index(K22PeripheralId id) {
    if (id >= K22_PERIPHERAL_PORTA && id <= K22_PERIPHERAL_PORTE)
        return (uint8_t)(id - K22_PERIPHERAL_PORTA);
    return (uint8_t)(id - K22_PERIPHERAL_GPIOA);
}

bool k22_io_internal_is_port(K22PeripheralId id) {
    return id >= K22_PERIPHERAL_PORTA && id <= K22_PERIPHERAL_PORTE;
}

bool k22_io_internal_is_gpio(K22PeripheralId id) {
    return id >= K22_PERIPHERAL_GPIOA && id <= K22_PERIPHERAL_GPIOE;
}

uint32_t k22_io_internal_pin_level_unfiltered(const K22Io* io, uint8_t port) {
    uint32_t value = 0;
    for (uint8_t pin = 0; pin < K22_IO_PIN_COUNT; pin++) {
        const uint32_t bit = 1u << pin;
        if (!k22_io_internal_pin_exists(io, port, pin))
            continue;
        const uint32_t pcr = io->port_pcr[port][pin];
        const bool output = (io->gpio_pddr[port] & bit) != 0 && ((pcr >> 8) & 7u) == 1u;
        const bool externally_driven = (io->gpio_external_drive[port] & bit) != 0;
        bool high = false;
        if (output && ((pcr & K22_PORT_PCR_ODE) == 0 || (io->gpio_pdor[port] & bit) == 0)) {
            high = (io->gpio_pdor[port] & bit) != 0;
        } else if (externally_driven) {
            high = (io->gpio_external[port] & bit) != 0;
        } else if ((pcr & K22_PORT_PCR_PE) != 0) {
            high = (pcr & K22_PORT_PCR_PS) != 0;
        }
        if (high)
            value |= bit;
    }
    return value;
}

uint32_t k22_io_internal_pin_level(const K22Io* io, uint8_t port) {
    uint32_t value = k22_io_internal_pin_level_unfiltered(io, port);
    const uint32_t filtered = io->port_dfer[port] & io->configuration.package_pin_mask[port];
    value = (value & ~filtered) | (io->gpio_filtered[port] & filtered);
    return value;
}

static void update_pin_event(K22Io* io, uint8_t port, uint8_t pin, bool previous, bool current) {
    if (previous == current)
        return;
    const uint32_t irqc = (io->port_pcr[port][pin] >> 16) & 15u;
    bool triggered = false;
    if (irqc == 1u || irqc == 9u)
        triggered = !previous && current;
    else if (irqc == 2u || irqc == 10u)
        triggered = previous && !current;
    else if (irqc == 3u || irqc == 11u)
        triggered = true;
    else if (irqc == 8u)
        triggered = !current;
    else if (irqc == 12u)
        triggered = current;
    if (!triggered)
        return;
    const uint32_t bit = 1u << pin;
    io->port_isfr[port] |= bit;
    io->port_pcr[port][pin] |= K22_PORT_PCR_ISF;
    if (irqc <= 3u)
        k22_io_internal_emit(io, K22_IO_EVENT_DMA, (uint32_t)port * 32u + pin, current, irqc);
    else
        k22_io_internal_emit(io, K22_IO_EVENT_IRQ, 59u + port, bit, irqc);
}

static void update_output_events(K22Io* io, uint8_t port, uint32_t previous) {
    const uint32_t current = k22_io_internal_pin_level(io, port);
    uint32_t changed =
        (previous ^ current) & io->gpio_pddr[port] & io->configuration.package_pin_mask[port];
    while (changed != 0) {
        const uint8_t pin = k22_io_internal_first_set_bit(changed);
        const uint32_t bit = 1u << pin;
        k22_io_internal_emit(io, K22_IO_EVENT_GPIO_OUTPUT, (uint32_t)port * 32u + pin,
                             (current & bit) != 0, (io->port_pcr[port][pin] >> 8) & 7u);
        changed &= ~bit;
    }
}

void k22_io_internal_commit_pin_level(K22Io* io, uint8_t port, uint8_t pin, bool previous,
                                      bool high) {
    const uint32_t bit = 1u << pin;
    if (high)
        io->gpio_filtered[port] |= bit;
    else
        io->gpio_filtered[port] &= ~bit;
    update_pin_event(io, port, pin, previous, high);
}

bool k22_io_internal_module_clocked(const K22Io* io, K22PeripheralId id) {
    if (id == K22_PERIPHERAL_FLASH_CONFIG || id == K22_PERIPHERAL_MCM)
        return true;
    return io->clock_enabled[id];
}

K22IoConfiguration k22_io_default_configuration(const K22Profile* profile) {
    K22IoConfiguration configuration;
    memset(&configuration, 0, sizeof(configuration));
    configuration.profile = profile;
    for (uint8_t port = 0; port < K22_IO_PORT_COUNT; port++)
        configuration.package_pin_mask[port] = UINT32_MAX;
    memset(configuration.flash_configuration, 0xff, sizeof(configuration.flash_configuration));
    configuration.flash_configuration[0x0c] = 0xfeu;
    return configuration;
}

bool k22_io_init(K22Io* io, K22IoConfiguration configuration) {
    if (io == NULL || configuration.profile == NULL)
        return false;
    memset(io, 0, sizeof(*io));
    io->configuration = configuration;
    k22_io_reset(io);
    return true;
}

static void reset_peripheral_registers(const K22IoConfiguration* configuration,
                                       K22PeripheralId peripheral, uint8_t* registers,
                                       size_t capacity) {
    K22PeripheralBlock block;
    const K22RegisterManifest* manifest = k22_register_manifest_get(configuration->profile->id);
    if (manifest == NULL ||
        !k22_profile_peripheral_block(configuration->profile, peripheral, &block)) {
        return;
    }
    for (size_t index = 0u; index < manifest->register_count; index++) {
        const K22RegisterDescriptor* descriptor = &manifest->registers[index];
        const uint8_t size = (uint8_t)(descriptor->width / 8u);
        if (descriptor->address < block.address ||
            descriptor->address - block.address + size > capacity) {
            continue;
        }
        const uint32_t offset = descriptor->address - block.address;
        const uint32_t value = descriptor->reset_value & descriptor->reset_mask;
        for (uint8_t byte = 0u; byte < size; byte++) {
            registers[offset + byte] = (uint8_t)(value >> (byte * 8u));
        }
    }
}

void k22_io_reset(K22Io* io) {
    if (io == NULL)
        return;
    const K22IoConfiguration configuration = io->configuration;
    memset(io, 0, sizeof(*io));
    io->configuration = configuration;
    for (uint8_t port = 0u; port < K22_IO_PORT_COUNT; port++) {
        for (uint8_t pin = 0u; pin < K22_IO_PIN_COUNT; pin++) {
            const uint32_t address = 0x40049000u + (uint32_t)port * 0x1000u + (uint32_t)pin * 4u;
            const K22RegisterDescriptor* descriptor =
                k22_register_manifest_lookup(configuration.profile->id, address, 32u);
            if (descriptor != NULL) {
                io->port_pcr[port][pin] = descriptor->reset_value & descriptor->reset_mask;
            }
        }
    }
    io->clock_enabled[K22_PERIPHERAL_FLASH_CONFIG] = true;
    io->clock_enabled[K22_PERIPHERAL_MCM] = true;
    reset_peripheral_registers(&configuration, K22_PERIPHERAL_USB0, io->usb, sizeof(io->usb));
    reset_peripheral_registers(&configuration, K22_PERIPHERAL_CAN0, (uint8_t*)io->can,
                               sizeof(io->can));
    reset_peripheral_registers(&configuration, K22_PERIPHERAL_I2S0, (uint8_t*)io->i2s,
                               sizeof(io->i2s));
    reset_peripheral_registers(&configuration, K22_PERIPHERAL_FB, (uint8_t*)io->flexbus,
                               sizeof(io->flexbus));
    reset_peripheral_registers(&configuration, K22_PERIPHERAL_SYSMPU, (uint8_t*)io->sysmpu,
                               sizeof(io->sysmpu));
    const bool large_profile = configuration.profile->id == K22_PROFILE_MK22FN1M012 ||
                               configuration.profile->id == K22_PROFILE_MK22FX51212;
    const uint32_t crossbar_ports =
        configuration.profile->id >= K22_PROFILE_MK22FN51212 ? 0x1fu : 0x0fu;
    io->mcm[0] = (large_profile ? 0x00370000u : 0x00170000u) | crossbar_ports;
    if (!large_profile)
        io->mcm[8u / 4u] = 0x00020000u;
    for (uint8_t port = 0; port < K22_IO_PORT_COUNT; port++) {
        io->gpio_filtered[port] = k22_io_internal_pin_level_unfiltered(io, port);
        io->gpio_pending[port] = io->gpio_filtered[port];
    }
}

bool k22_io_copy(K22Io* destination, const K22Io* source) {
    if (destination == NULL || source == NULL || destination == source)
        return destination == source && destination != NULL;
    *destination = *source;
    return true;
}

void k22_io_set_clock(K22Io* io, K22PeripheralId peripheral, bool enabled) {
    if (io == NULL || peripheral < 0 || peripheral >= K22_PERIPHERAL_COUNT ||
        !k22_profile_has_peripheral(io->configuration.profile, peripheral))
        return;
    io->clock_enabled[peripheral] = enabled;
    if (k22_io_internal_is_port(peripheral))
        io->clock_enabled[K22_PERIPHERAL_GPIOA + port_index(peripheral)] = enabled;
    else if (k22_io_internal_is_gpio(peripheral))
        io->clock_enabled[K22_PERIPHERAL_PORTA + port_index(peripheral)] = enabled;
}

bool k22_io_clock_enabled(const K22Io* io, K22PeripheralId peripheral) {
    return io != NULL && peripheral >= 0 && peripheral < K22_PERIPHERAL_COUNT &&
           k22_profile_has_peripheral(io->configuration.profile, peripheral) &&
           k22_io_internal_module_clocked(io, peripheral);
}

bool k22_io_internal_read_port(K22Io* io, K22PeripheralLocation location, uint8_t size,
                               uint32_t* value) {
    const uint8_t port = port_index(location.id);
    if (location.offset < 0x80u) {
        const uint8_t pin = (uint8_t)(location.offset / 4u);
        if (!k22_io_internal_pin_exists(io, port, pin) || location.offset % 4u + size > 4u)
            return false;
        *value = (io->port_pcr[port][pin] >> ((location.offset & 3u) * 8u)) & width_mask(size);
        return true;
    }
    if (location.offset == 0x80u || location.offset == 0x84u) {
        if (size != 4)
            return false;
        *value = 0;
        return true;
    }
    if (location.offset == 0xa0u && size == 4) {
        *value = io->port_isfr[port];
        return true;
    }
    if (location.offset == 0xc0u && size == 4) {
        *value = io->port_dfer[port];
        return true;
    }
    if (location.offset == 0xc4u && size == 1) {
        *value = io->port_dfcr[port];
        return true;
    }
    if (location.offset == 0xc8u && size == 1) {
        *value = io->port_dfwr[port];
        return true;
    }
    return false;
}

static void write_pcr(K22Io* io, uint8_t port, uint8_t pin, uint32_t requested, bool clear_isf) {
    const uint32_t previous = io->port_pcr[port][pin];
    if ((previous & K22_PORT_PCR_LK) != 0) {
        if (clear_isf) {
            io->port_pcr[port][pin] &= ~K22_PORT_PCR_ISF;
            io->port_isfr[port] &= ~(1u << pin);
        }
        return;
    }
    uint32_t value = requested & (K22_PORT_PCR_WRITABLE & ~K22_PORT_PCR_ISF);
    if (!clear_isf)
        value |= previous & K22_PORT_PCR_ISF;
    else
        io->port_isfr[port] &= ~(1u << pin);
    io->port_pcr[port][pin] = value;
}

bool k22_io_internal_write_port(K22Io* io, K22PeripheralLocation location, uint8_t size,
                                uint32_t value) {
    const uint8_t port = port_index(location.id);
    const uint32_t before = k22_io_internal_pin_level(io, port);
    if (location.offset < 0x80u) {
        const uint8_t pin = (uint8_t)(location.offset / 4u);
        if (!k22_io_internal_pin_exists(io, port, pin) || location.offset % 4u + size > 4u)
            return false;
        const uint8_t register_byte = (uint8_t)(location.offset & 3u);
        const bool clear_isf = register_byte <= 3u && register_byte + size > 3u &&
                               (value & (1u << ((3u - register_byte) * 8u))) != 0;
        write_pcr(io, port, pin, merge_value(io->port_pcr[port][pin], location.offset, size, value),
                  clear_isf);
        update_output_events(io, port, before);
        return true;
    }
    if ((location.offset == 0x80u || location.offset == 0x84u) && size == 4) {
        const uint16_t data = (uint16_t)value;
        const uint16_t select = (uint16_t)(value >> 16);
        const uint8_t first = location.offset == 0x80u ? 0 : 16;
        for (uint8_t index = 0; index < 16; index++) {
            const uint8_t pin = first + index;
            if ((select & (1u << index)) != 0 && k22_io_internal_pin_exists(io, port, pin))
                write_pcr(io, port, pin, data, false);
        }
        update_output_events(io, port, before);
        return true;
    }
    if (location.offset == 0xa0u && size == 4) {
        io->port_isfr[port] &= ~value;
        for (uint8_t pin = 0; pin < K22_IO_PIN_COUNT; pin++) {
            if ((value & (1u << pin)) != 0)
                io->port_pcr[port][pin] &= ~K22_PORT_PCR_ISF;
        }
        return true;
    }
    if (location.offset == 0xc0u && size == 4) {
        io->port_dfer[port] = value & io->configuration.package_pin_mask[port];
        io->gpio_filtered[port] = k22_io_internal_pin_level_unfiltered(io, port);
        return true;
    }
    if (location.offset == 0xc4u && size == 1) {
        io->port_dfcr[port] = (uint8_t)value & 1u;
        return true;
    }
    if (location.offset == 0xc8u && size == 1) {
        io->port_dfwr[port] = (uint8_t)value & 0x1fu;
        return true;
    }
    return false;
}

bool k22_io_internal_read_gpio(K22Io* io, K22PeripheralLocation location, uint8_t size,
                               uint32_t* value) {
    const uint32_t register_offset = location.offset & ~3u;
    const uint32_t byte_offset = location.offset & 3u;
    if (byte_offset + size > 4u)
        return false;
    const uint8_t port = port_index(location.id);
    uint32_t register_value = 0;
    if (register_offset == 0)
        register_value = io->gpio_pdor[port];
    else if (register_offset == 0x10u)
        register_value = k22_io_internal_pin_level(io, port);
    else if (register_offset == 0x14u)
        register_value = io->gpio_pddr[port];
    *value = (register_value >> (byte_offset * 8u)) & width_mask(size);
    return true;
}

bool k22_io_internal_write_gpio(K22Io* io, K22PeripheralLocation location, uint8_t size,
                                uint32_t value) {
    const uint32_t register_offset = location.offset & ~3u;
    const uint32_t byte_offset = location.offset & 3u;
    if (byte_offset + size > 4u)
        return false;
    const uint8_t port = port_index(location.id);
    const uint32_t package_mask = io->configuration.package_pin_mask[port];
    const uint32_t before = k22_io_internal_pin_level(io, port);
    const uint32_t shifted = (value & width_mask(size)) << (byte_offset * 8u);
    if (register_offset == 0)
        io->gpio_pdor[port] =
            merge_value(io->gpio_pdor[port], location.offset, size, value) & package_mask;
    else if (register_offset == 4u)
        io->gpio_pdor[port] |= shifted & package_mask;
    else if (register_offset == 8u)
        io->gpio_pdor[port] &= ~(shifted & package_mask);
    else if (register_offset == 0x0cu)
        io->gpio_pdor[port] ^= shifted & package_mask;
    else if (register_offset == 0x14u)
        io->gpio_pddr[port] =
            merge_value(io->gpio_pddr[port], location.offset, size, value) & package_mask;
    else
        return false;
    update_output_events(io, port, before);
    return true;
}
