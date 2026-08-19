#include "k22_io.h"

#include <string.h>

#include "k22_register_manifest.h"

enum {
    K22_BIT_BAND_BASE = 0x42000000u,
    K22_BIT_BAND_LIMIT = 0x44000000u,
    K22_PERIPHERAL_BASE = 0x40000000u,
    K22_PORT_PCR_ISF = 1u << 24,
    K22_PORT_PCR_LK = 1u << 15,
    K22_PORT_PCR_ODE = 1u << 5,
    K22_PORT_PCR_PE = 1u << 1,
    K22_PORT_PCR_PS = 1u,
    K22_PORT_PCR_WRITABLE = 0x010f87f7u,
    K22_USB_ISTAT = 0x80,
    K22_USB_INTEN = 0x84,
    K22_USB_STAT = 0x90,
    K22_USB_CTL = 0x94,
    K22_USB_FRMNUML = 0xa0,
    K22_USB_FRMNUMH = 0xa4,
    K22_CAN_MCR = 0,
    K22_CAN_TIMER = 0x08,
    K22_CAN_RXMGMASK = 0x10,
    K22_CAN_RX14MASK = 0x14,
    K22_CAN_RX15MASK = 0x18,
    K22_CAN_IMASK1 = 0x28,
    K22_CAN_IFLAG1 = 0x30,
    K22_CAN_MB_BASE = 0x80,
    K22_I2S_TCSR = 0,
    K22_I2S_TDR0 = 0x20,
    K22_I2S_RCSR = 0x80,
    K22_I2S_RDR0 = 0xa0,
    K22_I2S_REQUEST_FLAG = 1u << 16,
    K22_I2S_FIFO_ERROR = 1u << 18,
};

#define K22_I2S_ENABLE UINT32_C(0x80000000)

static uint32_t load_bytes(const uint8_t* data, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; index++)
        value |= (uint32_t)data[index] << (index * 8u);
    return value;
}

static uint32_t width_mask(uint8_t size) {
    return size == 4 ? UINT32_MAX : (1u << (size * 8u)) - 1u;
}

static uint32_t merge_value(uint32_t previous, uint32_t offset, uint8_t size,
                            uint32_t value) {
    const uint32_t shift = (offset & 3u) * 8u;
    const uint32_t mask = width_mask(size) << shift;
    return (previous & ~mask) | ((value << shift) & mask);
}

static void emit(K22Io* io, K22IoEventType type, uint32_t source, uint32_t value,
                 uint32_t auxiliary) {
    if (io->configuration.event_handler == NULL)
        return;
    const K22IoEvent event = {
        .type = type, .source = source, .value = value, .auxiliary = auxiliary};
    io->configuration.event_handler(io->configuration.event_context, &event);
}

static void emit_can(K22Io* io, uint8_t mailbox, uint32_t identifier, uint32_t control,
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

static bool valid_size(uint8_t size) { return size == 1 || size == 2 || size == 4; }

static uint8_t first_set_bit(uint32_t value) {
    uint8_t bit = 0;
    while ((value & 1u) == 0) {
        value >>= 1;
        bit++;
    }
    return bit;
}

static bool pin_exists(const K22Io* io, uint8_t port, uint8_t pin) {
    return port < K22_IO_PORT_COUNT && pin < K22_IO_PIN_COUNT &&
           (io->configuration.package_pin_mask[port] & (1u << pin)) != 0;
}

static uint8_t port_index(K22PeripheralId id) {
    if (id >= K22_PERIPHERAL_PORTA && id <= K22_PERIPHERAL_PORTE)
        return (uint8_t)(id - K22_PERIPHERAL_PORTA);
    return (uint8_t)(id - K22_PERIPHERAL_GPIOA);
}

static bool is_port(K22PeripheralId id) {
    return id >= K22_PERIPHERAL_PORTA && id <= K22_PERIPHERAL_PORTE;
}

static bool is_gpio(K22PeripheralId id) {
    return id >= K22_PERIPHERAL_GPIOA && id <= K22_PERIPHERAL_GPIOE;
}

static uint32_t pin_level_unfiltered(const K22Io* io, uint8_t port) {
    uint32_t value = 0;
    for (uint8_t pin = 0; pin < K22_IO_PIN_COUNT; pin++) {
        const uint32_t bit = 1u << pin;
        if (!pin_exists(io, port, pin))
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

static uint32_t pin_level(const K22Io* io, uint8_t port) {
    uint32_t value = pin_level_unfiltered(io, port);
    const uint32_t filtered =
        io->port_dfer[port] & io->configuration.package_pin_mask[port];
    value = (value & ~filtered) | (io->gpio_filtered[port] & filtered);
    return value;
}

static void update_pin_event(K22Io* io, uint8_t port, uint8_t pin, bool previous,
                             bool current) {
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
        emit(io, K22_IO_EVENT_DMA, (uint32_t)port * 32u + pin, current, irqc);
    else
        emit(io, K22_IO_EVENT_IRQ, 59u + port, bit, irqc);
}

static void update_output_events(K22Io* io, uint8_t port, uint32_t previous) {
    const uint32_t current = pin_level(io, port);
    uint32_t changed = (previous ^ current) & io->gpio_pddr[port] &
                       io->configuration.package_pin_mask[port];
    while (changed != 0) {
        const uint8_t pin = first_set_bit(changed);
        const uint32_t bit = 1u << pin;
        emit(io, K22_IO_EVENT_GPIO_OUTPUT, (uint32_t)port * 32u + pin, (current & bit) != 0,
             (io->port_pcr[port][pin] >> 8) & 7u);
        changed &= ~bit;
    }
}

static void commit_pin_level(K22Io* io, uint8_t port, uint8_t pin, bool previous,
                             bool high) {
    const uint32_t bit = 1u << pin;
    if (high)
        io->gpio_filtered[port] |= bit;
    else
        io->gpio_filtered[port] &= ~bit;
    update_pin_event(io, port, pin, previous, high);
}

static bool module_clocked(const K22Io* io, K22PeripheralId id) {
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
    memset(configuration.flash_configuration, 0xff,
           sizeof(configuration.flash_configuration));
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
    const K22RegisterManifest* manifest =
        k22_register_manifest_get(configuration->profile->id);
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
            const uint32_t address =
                0x40049000u + (uint32_t)port * 0x1000u + (uint32_t)pin * 4u;
            const K22RegisterDescriptor* descriptor =
                k22_register_manifest_lookup(configuration.profile->id, address, 32u);
            if (descriptor != NULL) {
                io->port_pcr[port][pin] = descriptor->reset_value & descriptor->reset_mask;
            }
        }
    }
    io->clock_enabled[K22_PERIPHERAL_FLASH_CONFIG] = true;
    io->clock_enabled[K22_PERIPHERAL_MCM] = true;
    reset_peripheral_registers(&configuration, K22_PERIPHERAL_USB0, io->usb,
                               sizeof(io->usb));
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
        io->gpio_filtered[port] = pin_level_unfiltered(io, port);
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
    if (is_port(peripheral))
        io->clock_enabled[K22_PERIPHERAL_GPIOA + port_index(peripheral)] = enabled;
    else if (is_gpio(peripheral))
        io->clock_enabled[K22_PERIPHERAL_PORTA + port_index(peripheral)] = enabled;
}

bool k22_io_clock_enabled(const K22Io* io, K22PeripheralId peripheral) {
    return io != NULL && peripheral >= 0 && peripheral < K22_PERIPHERAL_COUNT &&
           k22_profile_has_peripheral(io->configuration.profile, peripheral) &&
           module_clocked(io, peripheral);
}

static bool read_port(K22Io* io, K22PeripheralLocation location, uint8_t size,
                      uint32_t* value) {
    const uint8_t port = port_index(location.id);
    if (location.offset < 0x80u) {
        const uint8_t pin = (uint8_t)(location.offset / 4u);
        if (!pin_exists(io, port, pin) || location.offset % 4u + size > 4u)
            return false;
        *value =
            (io->port_pcr[port][pin] >> ((location.offset & 3u) * 8u)) & width_mask(size);
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

static void write_pcr(K22Io* io, uint8_t port, uint8_t pin, uint32_t requested,
                      bool clear_isf) {
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

static bool write_port(K22Io* io, K22PeripheralLocation location, uint8_t size,
                       uint32_t value) {
    const uint8_t port = port_index(location.id);
    const uint32_t before = pin_level(io, port);
    if (location.offset < 0x80u) {
        const uint8_t pin = (uint8_t)(location.offset / 4u);
        if (!pin_exists(io, port, pin) || location.offset % 4u + size > 4u)
            return false;
        const uint8_t register_byte = (uint8_t)(location.offset & 3u);
        const bool clear_isf = register_byte <= 3u && register_byte + size > 3u &&
                               (value & (1u << ((3u - register_byte) * 8u))) != 0;
        write_pcr(io, port, pin,
                  merge_value(io->port_pcr[port][pin], location.offset, size, value),
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
            if ((select & (1u << index)) != 0 && pin_exists(io, port, pin))
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
        io->gpio_filtered[port] = pin_level_unfiltered(io, port);
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

static bool read_gpio(K22Io* io, K22PeripheralLocation location, uint8_t size,
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
        register_value = pin_level(io, port);
    else if (register_offset == 0x14u)
        register_value = io->gpio_pddr[port];
    else if (register_offset != 4u && register_offset != 8u && register_offset != 0x0cu)
        return false;
    *value = (register_value >> (byte_offset * 8u)) & width_mask(size);
    return true;
}

static bool write_gpio(K22Io* io, K22PeripheralLocation location, uint8_t size,
                       uint32_t value) {
    const uint32_t register_offset = location.offset & ~3u;
    const uint32_t byte_offset = location.offset & 3u;
    if (byte_offset + size > 4u)
        return false;
    const uint8_t port = port_index(location.id);
    const uint32_t package_mask = io->configuration.package_pin_mask[port];
    const uint32_t before = pin_level(io, port);
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

static bool read_usb(K22Io* io, K22PeripheralLocation location, uint8_t size,
                     uint32_t* value) {
    if (size != 1 || !usb_offset_valid(location.offset))
        return false;
    *value = io->usb[location.offset];
    return true;
}

static bool write_usb(K22Io* io, K22PeripheralLocation location, uint8_t size,
                      uint32_t value) {
    if (size != 1 || !usb_offset_valid(location.offset) || location.offset <= 0x0cu ||
        location.offset == K22_USB_STAT || location.offset == K22_USB_FRMNUML ||
        location.offset == K22_USB_FRMNUMH)
        return false;
    if (location.offset == 0x10u || location.offset == K22_USB_ISTAT ||
        location.offset == 0x88u)
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

static bool read_words(const uint32_t* words, uint32_t length, uint32_t offset,
                       uint8_t size, uint32_t* value) {
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
        return (offset >= 0x80u && offset < 0x180u) ||
               (offset >= 0x880u && offset < 0x8c0u);
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
    if (offset >= K22_CAN_MB_BASE && offset < K22_CAN_MB_BASE + 16u * 16u &&
        (offset & 15u) == 0) {
        const uint8_t mailbox = (uint8_t)((offset - K22_CAN_MB_BASE) / 16u);
        const uint8_t code = (uint8_t)(value >> 24) & 15u;
        if (code >= 0xcu && code <= 0xfu) {
            const uint32_t id = io->can[(offset + 4u) / 4u];
            emit_can(io, mailbox, id, value, io->can[(offset + 8u) / 4u],
                     io->can[(offset + 12u) / 4u]);
            io->can[offset / 4u] = (value & ~(15u << 24)) | (8u << 24);
            io->can[K22_CAN_IFLAG1 / 4] |= 1u << mailbox;
            if ((io->can[K22_CAN_IMASK1 / 4] & (1u << mailbox)) != 0)
                emit(io, K22_IO_EVENT_IRQ, 75u, 1u << mailbox, 0);
        }
    }
    return true;
}

static bool fifo_push(uint32_t* fifo, uint8_t* write, uint8_t* count, uint32_t value) {
    if (*count == K22_IO_FIFO_CAPACITY)
        return false;
    fifo[*write] = value;
    *write = (uint8_t)((*write + 1u) % K22_IO_FIFO_CAPACITY);
    (*count)++;
    return true;
}

static bool fifo_pop(uint32_t* fifo, uint8_t* read, uint8_t* count, uint32_t* value) {
    if (*count == 0)
        return false;
    *value = fifo[*read];
    *read = (uint8_t)((*read + 1u) % K22_IO_FIFO_CAPACITY);
    (*count)--;
    return true;
}

static void update_i2s_requests(K22Io* io) {
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
            emit(io, K22_IO_EVENT_DMA, 0, io->i2s_transmit_count, 0);
        if ((*transmit & (1u << 8)) != 0)
            emit(io, K22_IO_EVENT_IRQ, 28u, K22_I2S_REQUEST_FLAG, 0);
    }
    if ((*receive & K22_I2S_REQUEST_FLAG) != 0) {
        if ((*receive & 1u) != 0)
            emit(io, K22_IO_EVENT_DMA, 1, io->i2s_receive_count, 0);
        if ((*receive & (1u << 8)) != 0)
            emit(io, K22_IO_EVENT_IRQ, 29u, K22_I2S_REQUEST_FLAG, 0);
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
        if (!fifo_pop(io->i2s_receive_fifo, &io->i2s_receive_read, &io->i2s_receive_count,
                      value)) {
            io->i2s[K22_I2S_RCSR / 4] |= K22_I2S_FIFO_ERROR;
            *value = 0;
        }
        update_i2s_requests(io);
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
        if (!fifo_push(io->i2s_transmit_fifo, &io->i2s_transmit_write,
                       &io->i2s_transmit_count, value)) {
            io->i2s[K22_I2S_TCSR / 4] |= K22_I2S_FIFO_ERROR;
            return true;
        }
        emit(io, K22_IO_EVENT_I2S_TRANSMIT, (offset - K22_I2S_TDR0) / 4u, value,
             io->i2s_transmit_count);
        update_i2s_requests(io);
        return true;
    }
    if (offset == K22_I2S_TCSR || offset == K22_I2S_RCSR) {
        const uint32_t flags = K22_I2S_REQUEST_FLAG | K22_I2S_FIFO_ERROR | (1u << 17) |
                               (1u << 19) | (1u << 20);
        uint32_t previous = io->i2s[offset / 4u];
        previous &= ~(value & flags);
        io->i2s[offset / 4u] = (value & ~flags) | (previous & flags);
        if ((value & K22_I2S_ENABLE) == 0) {
            if (offset == K22_I2S_TCSR)
                io->i2s_transmit_count = io->i2s_transmit_read = io->i2s_transmit_write = 0;
            else
                io->i2s_receive_count = io->i2s_receive_read = io->i2s_receive_write = 0;
        }
        update_i2s_requests(io);
        return true;
    }
    io->i2s[offset / 4u] = value;
    return true;
}

static bool read_flash_configuration(K22Io* io, K22PeripheralLocation location,
                                     uint8_t size, uint32_t* value) {
    if (location.offset + size > sizeof(io->configuration.flash_configuration))
        return false;
    *value = load_bytes(io->configuration.flash_configuration + location.offset, size);
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
        emit(io, K22_IO_EVENT_FLEXBUS_TRANSFER, chip_select, value,
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

static bool read_direct(K22Io* io, uint32_t address, uint8_t size, uint32_t* value) {
    K22PeripheralLocation location;
    if (!k22_profile_resolve_peripheral(io->configuration.profile, address, size,
                                        &location))
        return false;
    if (!module_clocked(io, location.id)) {
        emit(io, K22_IO_EVENT_ACCESS_ERROR, address, size, 0);
        return false;
    }
    if (location.id == K22_PERIPHERAL_FLASH_CONFIG)
        return read_flash_configuration(io, location, size, value);
    if (is_port(location.id))
        return read_port(io, location, size, value);
    if (is_gpio(location.id))
        return read_gpio(io, location, size, value);
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

static bool write_direct(K22Io* io, uint32_t address, uint8_t size, uint32_t value) {
    K22PeripheralLocation location;
    if (!k22_profile_resolve_peripheral(io->configuration.profile, address, size,
                                        &location))
        return false;
    if (!module_clocked(io, location.id)) {
        emit(io, K22_IO_EVENT_ACCESS_ERROR, address, value, size);
        return false;
    }
    if (location.id == K22_PERIPHERAL_FLASH_CONFIG)
        return false;
    if (is_port(location.id))
        return write_port(io, location, size, value);
    if (is_gpio(location.id))
        return write_gpio(io, location, size, value);
    if (location.id == K22_PERIPHERAL_USB0)
        return write_usb(io, location, size, value);
    if (location.id == K22_PERIPHERAL_CAN0)
        return write_can(io, location.offset, size, value);
    if (location.id == K22_PERIPHERAL_I2S0)
        return write_i2s(io, location.offset, size, value);
    if (location.id == K22_PERIPHERAL_FB) {
        if (size != 4 || (location.offset & 3u) != 0 ||
            !flexbus_offset_valid(location.offset))
            return false;
        io->flexbus[location.offset / 4u] = value;
        if (location.offset < 0x48u && (location.offset % 12u) == 4u && (value & 1u) != 0)
            emit(io, K22_IO_EVENT_FLEXBUS_TRANSFER, location.offset / 12u, value, 0);
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
        if (size != 4 || (location.offset & 3u) != 0 ||
            !sysmpu_offset_valid(location.offset))
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

bool k22_io_read(K22Io* io, uint32_t address, uint8_t size, uint32_t* value) {
    if (io == NULL || value == NULL || !valid_size(size))
        return false;
    if (address >= K22_BIT_BAND_BASE && address < K22_BIT_BAND_LIMIT && size == 4) {
        const uint32_t alias = address - K22_BIT_BAND_BASE;
        const uint32_t byte_address = K22_PERIPHERAL_BASE + alias / 32u;
        const uint8_t bit = (uint8_t)((alias / 4u) & 7u);
        uint32_t byte = 0;
        if (!read_direct(io, byte_address, 1, &byte))
            return false;
        *value = (byte >> bit) & 1u;
        return true;
    }
    return read_direct(io, address, size, value);
}

bool k22_io_write(K22Io* io, uint32_t address, uint8_t size, uint32_t value) {
    if (io == NULL || !valid_size(size))
        return false;
    if (address >= K22_BIT_BAND_BASE && address < K22_BIT_BAND_LIMIT && size == 4) {
        const uint32_t alias = address - K22_BIT_BAND_BASE;
        const uint32_t byte_address = K22_PERIPHERAL_BASE + alias / 32u;
        const uint8_t bit = (uint8_t)((alias / 4u) & 7u);
        uint32_t byte = 0;
        if (!read_direct(io, byte_address, 1, &byte))
            return false;
        byte = (value & 1u) != 0 ? byte | (1u << bit) : byte & ~(1u << bit);
        return write_direct(io, byte_address, 1, byte);
    }
    return write_direct(io, address, size, value);
}

bool k22_io_drive_pin(K22Io* io, uint8_t port, uint8_t pin, bool high) {
    if (io == NULL || !pin_exists(io, port, pin))
        return false;
    const uint32_t bit = 1u << pin;
    const bool previous = (pin_level(io, port) & bit) != 0;
    io->gpio_external_drive[port] |= bit;
    if (high)
        io->gpio_external[port] |= bit;
    else
        io->gpio_external[port] &= ~bit;
    const bool target = (pin_level_unfiltered(io, port) & bit) != 0;
    if ((io->port_dfer[port] & bit) != 0) {
        io->gpio_filter_age[port][pin] = 0;
        if (target)
            io->gpio_pending[port] |= bit;
        else
            io->gpio_pending[port] &= ~bit;
    } else
        commit_pin_level(io, port, pin, previous, target);
    return true;
}

bool k22_io_release_pin(K22Io* io, uint8_t port, uint8_t pin) {
    if (io == NULL || !pin_exists(io, port, pin))
        return false;
    const uint32_t bit = 1u << pin;
    const bool previous = (pin_level(io, port) & bit) != 0;
    io->gpio_external_drive[port] &= ~bit;
    const bool target = (pin_level_unfiltered(io, port) & bit) != 0;
    if ((io->port_dfer[port] & bit) != 0) {
        io->gpio_filter_age[port][pin] = 0;
        if (target)
            io->gpio_pending[port] |= bit;
        else
            io->gpio_pending[port] &= ~bit;
    } else
        commit_pin_level(io, port, pin, previous, target);
    return true;
}

uint32_t k22_io_pin_input(const K22Io* io, uint8_t port) {
    return io == NULL || port >= K22_IO_PORT_COUNT ? 0 : pin_level(io, port);
}

bool k22_io_usb_token(K22Io* io, uint8_t endpoint, uint8_t token, bool transmit) {
    if (io == NULL || endpoint >= 16 || !k22_io_clock_enabled(io, K22_PERIPHERAL_USB0) ||
        (io->usb[K22_USB_CTL] & 1u) == 0)
        return false;
    io->usb[K22_USB_STAT] = (uint8_t)((endpoint << 4) | (transmit ? 8u : 0));
    io->usb[K22_USB_ISTAT] |= 1u << 3;
    emit(io, K22_IO_EVENT_USB_TOKEN, endpoint, token, transmit);
    if ((io->usb[K22_USB_INTEN] & (1u << 3)) != 0)
        emit(io, K22_IO_EVENT_IRQ, 53u, 1u << 3, endpoint);
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
            emit(io, K22_IO_EVENT_IRQ, 75u, 1u << mailbox, 0);
        return true;
    }
    return false;
}

bool k22_io_i2s_receive(K22Io* io, uint32_t sample) {
    if (io == NULL || !k22_io_clock_enabled(io, K22_PERIPHERAL_I2S0) ||
        (io->i2s[K22_I2S_RCSR / 4] & K22_I2S_ENABLE) == 0 ||
        !fifo_push(io->i2s_receive_fifo, &io->i2s_receive_write, &io->i2s_receive_count,
                   sample))
        return false;
    update_i2s_requests(io);
    return true;
}

bool k22_io_i2s_transmit(K22Io* io, uint32_t* sample) {
    if (io == NULL || sample == NULL ||
        !fifo_pop(io->i2s_transmit_fifo, &io->i2s_transmit_read, &io->i2s_transmit_count,
                  sample))
        return false;
    update_i2s_requests(io);
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
            const uint8_t pin = first_set_bit(pending);
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
    emit(io, K22_IO_EVENT_ACCESS_ERROR, address, master, access);
    return false;
}

void k22_io_advance(K22Io* io, uint32_t cycles) {
    if (io == NULL || cycles == 0)
        return;
    for (uint8_t port = 0; port < K22_IO_PORT_COUNT; port++) {
        uint32_t filtered = io->port_dfer[port] & io->configuration.package_pin_mask[port];
        while (filtered != 0) {
            const uint8_t pin = first_set_bit(filtered);
            const uint32_t bit = 1u << pin;
            const bool target = (io->gpio_pending[port] & bit) != 0;
            const bool current = (io->gpio_filtered[port] & bit) != 0;
            if (target != current) {
                uint64_t age = (uint64_t)io->gpio_filter_age[port][pin] + cycles;
                const uint32_t threshold = (uint32_t)io->port_dfwr[port] + 1u;
                if (age >= threshold) {
                    commit_pin_level(io, port, pin, current, target);
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
            uint16_t frame = (uint16_t)io->usb[K22_USB_FRMNUML] |
                             ((uint16_t)io->usb[K22_USB_FRMNUMH] << 8);
            frame = (uint16_t)((frame + frames) & 0x7ffu);
            io->usb[K22_USB_FRMNUML] = (uint8_t)frame;
            io->usb[K22_USB_FRMNUMH] = (uint8_t)(frame >> 8);
            io->usb[K22_USB_ISTAT] |= 1u << 2;
            if ((io->usb[K22_USB_INTEN] & (1u << 2)) != 0)
                emit(io, K22_IO_EVENT_IRQ, 53u, 1u << 2, frame);
        }
    }
    if (k22_io_clock_enabled(io, K22_PERIPHERAL_CAN0) &&
        (io->can[K22_CAN_MCR / 4] & ((1u << 31) | (1u << 28))) == 0)
        io->can[K22_CAN_TIMER / 4] += cycles;
}
