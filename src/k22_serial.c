#include "k22_serial.h"

#include <string.h>

#include "k22_register_manifest.h"

enum {
    UART_C2 = 0x03,
    UART_S1 = 0x04,
    UART_S2 = 0x05,
    UART_C3 = 0x06,
    UART_D = 0x07,
    UART_C4 = 0x0a,
    UART_C5 = 0x0b,
    UART_ED = 0x0c,
    UART_PFIFO = 0x10,
    UART_CFIFO = 0x11,
    UART_SFIFO = 0x12,
    UART_TCFIFO = 0x14,
    UART_RCFIFO = 0x16,
    LPUART_BAUD = 0x00,
    LPUART_STAT = 0x04,
    LPUART_CTRL = 0x08,
    LPUART_DATA = 0x0c,
    SPI_MCR = 0x00,
    SPI_TCR = 0x08,
    SPI_CTAR0 = 0x0c,
    SPI_CTAR1 = 0x10,
    SPI_SR = 0x2c,
    SPI_RSER = 0x30,
    SPI_PUSHR = 0x34,
    SPI_POPR = 0x38,
    I2C_A1 = 0x00,
    I2C_F = 0x01,
    I2C_C1 = 0x02,
    I2C_S = 0x03,
    I2C_D = 0x04,
    I2C_C2 = 0x05,
    I2C_FLT = 0x06,
    I2C_A2 = 0x09,
};

static uint32_t load32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static void store32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void reset_register_block(const K22Profile* profile, uint32_t base,
                                 uint32_t block_size, uint8_t* registers, size_t capacity) {
    memset(registers, 0, capacity);
    const K22RegisterManifest* manifest = k22_register_manifest_get(profile->id);
    if (manifest == NULL) {
        return;
    }
    for (size_t index = 0u; index < manifest->register_count; index++) {
        const K22RegisterDescriptor* descriptor = &manifest->registers[index];
        const uint8_t size = (uint8_t)(descriptor->width / 8u);
        if (descriptor->address < base || descriptor->address - base >= block_size ||
            descriptor->address - base + size > capacity) {
            continue;
        }
        const uint32_t offset = descriptor->address - base;
        const uint32_t value = descriptor->reset_value & descriptor->reset_mask;
        for (uint8_t byte = 0u; byte < size; byte++) {
            registers[offset + byte] = (uint8_t)(value >> (byte * 8u));
        }
    }
}

static void fifo_clear(K22SerialFifo* fifo) { memset(fifo, 0, sizeof(*fifo)); }

static bool fifo_push(K22SerialFifo* fifo, uint16_t capacity, uint16_t value,
                      uint16_t metadata) {
    if (fifo->count >= capacity || capacity > K22_SERIAL_FIFO_CAPACITY)
        return false;
    fifo->values[fifo->write_index] = value;
    fifo->metadata[fifo->write_index] = metadata;
    fifo->write_index = (uint16_t)((fifo->write_index + 1u) % K22_SERIAL_FIFO_CAPACITY);
    fifo->count++;
    return true;
}

static bool fifo_pop(K22SerialFifo* fifo, uint16_t* value, uint16_t* metadata) {
    if (fifo->count == 0)
        return false;
    if (value != NULL)
        *value = fifo->values[fifo->read_index];
    if (metadata != NULL)
        *metadata = fifo->metadata[fifo->read_index];
    fifo->read_index = (uint16_t)((fifo->read_index + 1u) % K22_SERIAL_FIFO_CAPACITY);
    fifo->count--;
    return true;
}

static uint8_t fifo_error(const K22SerialFifo* fifo) {
    return fifo->count == 0 ? 0 : (uint8_t)fifo->metadata[fifo->read_index];
}

static void push_event(K22Serial* serial, K22SerialEndpoint endpoint,
                       K22SerialEventType type, uint16_t value) {
    if (serial->event_count == K22_SERIAL_EVENT_CAPACITY) {
        serial->event_read_index =
            (uint8_t)((serial->event_read_index + 1u) % K22_SERIAL_EVENT_CAPACITY);
        serial->event_count--;
    }
    K22SerialEvent* event = &serial->events[serial->event_write_index];
    event->endpoint = endpoint;
    event->type = type;
    event->value = value;
    serial->event_write_index =
        (uint8_t)((serial->event_write_index + 1u) % K22_SERIAL_EVENT_CAPACITY);
    serial->event_count++;
}

static bool configure_block(const K22Profile* profile, K22PeripheralId peripheral,
                            uint32_t* base, uint32_t* size) {
    K22PeripheralBlock block;
    if (!k22_profile_peripheral_block(profile, peripheral, &block))
        return false;
    *base = block.address;
    *size = block.size;
    return true;
}

static void configure_uart(K22SerialUart* uart, const K22Profile* profile,
                           K22PeripheralId peripheral, uint8_t depth) {
    memset(uart, 0, sizeof(*uart));
    uart->peripheral = peripheral;
    uart->present = configure_block(profile, peripheral, &uart->base, &uart->block_size);
    uart->fifo_depth = depth;
}

static void configure_spi(K22SerialSpi* spi, const K22Profile* profile,
                          K22PeripheralId peripheral, uint8_t depth) {
    memset(spi, 0, sizeof(*spi));
    spi->peripheral = peripheral;
    spi->present = configure_block(profile, peripheral, &spi->base, &spi->block_size);
    spi->fifo_depth = depth;
}

static void configure_i2c(K22SerialI2c* i2c, const K22Profile* profile,
                          K22PeripheralId peripheral) {
    memset(i2c, 0, sizeof(*i2c));
    i2c->peripheral = peripheral;
    i2c->present = configure_block(profile, peripheral, &i2c->base, &i2c->block_size);
    i2c->acknowledge = true;
}

static void reset_uart(K22SerialUart* uart, const K22Profile* profile) {
    reset_register_block(profile, uart->base, uart->block_size, uart->registers,
                         sizeof(uart->registers));
    fifo_clear(&uart->receive);
    fifo_clear(&uart->transmit);
    fifo_clear(&uart->wire_receive);
    fifo_clear(&uart->wire_transmit);
    uart->receive_cycles = 0;
    uart->transmit_cycles = 0;
    uart->clock_enabled = false;
    uart->status_read = false;
}

static void reset_spi(K22SerialSpi* spi, const K22Profile* profile) {
    reset_register_block(profile, spi->base, spi->block_size, (uint8_t*)spi->registers,
                         sizeof(spi->registers));
    fifo_clear(&spi->receive);
    fifo_clear(&spi->transmit);
    fifo_clear(&spi->wire_receive);
    fifo_clear(&spi->wire_transmit);
    spi->transfer_cycles = 0;
    spi->clock_enabled = false;
}

static void reset_i2c(K22SerialI2c* i2c, const K22Profile* profile) {
    reset_register_block(profile, i2c->base, i2c->block_size, i2c->registers,
                         sizeof(i2c->registers));
    fifo_clear(&i2c->receive);
    fifo_clear(&i2c->slave_receive);
    fifo_clear(&i2c->slave_transmit_fifo);
    i2c->transfer_cycles = 0;
    i2c->pending_value = 0;
    i2c->clock_enabled = false;
    i2c->acknowledge = true;
    i2c->transfer_pending = false;
    i2c->read_pending = false;
    i2c->slave_transmit = false;
}

bool k22_serial_init(K22Serial* serial, const K22Profile* profile) {
    if (serial == NULL || profile == NULL)
        return false;
    memset(serial, 0, sizeof(*serial));
    serial->profile = profile;
    serial->core_clock_hz = profile->cpu.maximum_core_clock_hz;
    serial->bus_clock_hz = profile->cpu.maximum_core_clock_hz / 2u;
    configure_uart(&serial->lpuart0, profile, K22_PERIPHERAL_LPUART0, 1);
    configure_uart(&serial->uart[0], profile, K22_PERIPHERAL_UART0, 8);
    configure_uart(&serial->uart[1], profile, K22_PERIPHERAL_UART1, 1);
    configure_uart(&serial->uart[2], profile, K22_PERIPHERAL_UART2, 1);
    configure_uart(&serial->uart[3], profile, K22_PERIPHERAL_UART3, 1);
    configure_uart(&serial->uart[4], profile, K22_PERIPHERAL_UART4, 1);
    configure_uart(&serial->uart[5], profile, K22_PERIPHERAL_UART5, 1);
    configure_spi(&serial->spi[0], profile, K22_PERIPHERAL_SPI0, 4);
    configure_spi(&serial->spi[1], profile, K22_PERIPHERAL_SPI1, 4);
    configure_spi(&serial->spi[2], profile, K22_PERIPHERAL_SPI2, 4);
    configure_i2c(&serial->i2c[0], profile, K22_PERIPHERAL_I2C0);
    configure_i2c(&serial->i2c[1], profile, K22_PERIPHERAL_I2C1);
    configure_i2c(&serial->i2c[2], profile, K22_PERIPHERAL_I2C2);
    k22_serial_reset(serial);
    return true;
}

void k22_serial_reset(K22Serial* serial) {
    if (serial == NULL)
        return;
    reset_uart(&serial->lpuart0, serial->profile);
    for (size_t index = 0; index < 6; index++)
        reset_uart(&serial->uart[index], serial->profile);
    for (size_t index = 0; index < 3; index++)
        reset_spi(&serial->spi[index], serial->profile);
    for (size_t index = 0; index < 3; index++)
        reset_i2c(&serial->i2c[index], serial->profile);
    serial->event_read_index = 0;
    serial->event_write_index = 0;
    serial->event_count = 0;
}

bool k22_serial_copy(K22Serial* destination, const K22Serial* source) {
    if (destination == NULL || source == NULL)
        return false;
    *destination = *source;
    return true;
}

void k22_serial_set_clocks(K22Serial* serial, uint32_t core_clock_hz,
                           uint32_t bus_clock_hz) {
    if (serial == NULL)
        return;
    serial->core_clock_hz = core_clock_hz;
    serial->bus_clock_hz = bus_clock_hz;
}

static K22SerialUart* find_uart_by_peripheral(K22Serial* serial, K22PeripheralId peripheral,
                                              bool* lpuart) {
    if (peripheral == K22_PERIPHERAL_LPUART0) {
        *lpuart = true;
        return &serial->lpuart0;
    }
    *lpuart = false;
    if (peripheral >= K22_PERIPHERAL_UART0 && peripheral <= K22_PERIPHERAL_UART5)
        return &serial->uart[peripheral - K22_PERIPHERAL_UART0];
    return NULL;
}

static K22SerialSpi* find_spi_by_peripheral(K22Serial* serial, K22PeripheralId peripheral) {
    if (peripheral >= K22_PERIPHERAL_SPI0 && peripheral <= K22_PERIPHERAL_SPI2)
        return &serial->spi[peripheral - K22_PERIPHERAL_SPI0];
    return NULL;
}

static K22SerialI2c* find_i2c_by_peripheral(K22Serial* serial, K22PeripheralId peripheral) {
    if (peripheral >= K22_PERIPHERAL_I2C0 && peripheral <= K22_PERIPHERAL_I2C2)
        return &serial->i2c[peripheral - K22_PERIPHERAL_I2C0];
    return NULL;
}

bool k22_serial_set_clock_gate(K22Serial* serial, K22PeripheralId peripheral,
                               bool enabled) {
    if (serial == NULL)
        return false;
    bool lpuart;
    K22SerialUart* uart = find_uart_by_peripheral(serial, peripheral, &lpuart);
    if (uart != NULL)
        return uart->present ? (uart->clock_enabled = enabled, true) : false;
    K22SerialSpi* spi = find_spi_by_peripheral(serial, peripheral);
    if (spi != NULL)
        return spi->present ? (spi->clock_enabled = enabled, true) : false;
    K22SerialI2c* i2c = find_i2c_by_peripheral(serial, peripheral);
    if (i2c != NULL)
        return i2c->present ? (i2c->clock_enabled = enabled, true) : false;
    return false;
}

static K22SerialUart* uart_at(K22Serial* serial, uint32_t address, bool* lpuart,
                              uint32_t* offset) {
    K22SerialUart* candidates[] = {
        &serial->lpuart0, &serial->uart[0], &serial->uart[1], &serial->uart[2],
        &serial->uart[3], &serial->uart[4], &serial->uart[5],
    };
    for (size_t index = 0; index < 7; index++) {
        K22SerialUart* uart = candidates[index];
        if (uart->present && address >= uart->base &&
            address - uart->base < uart->block_size) {
            *lpuart = index == 0;
            *offset = address - uart->base;
            return uart;
        }
    }
    return NULL;
}

static K22SerialSpi* spi_at(K22Serial* serial, uint32_t address, uint32_t* offset) {
    for (size_t index = 0; index < 3; index++) {
        K22SerialSpi* spi = &serial->spi[index];
        if (spi->present && address >= spi->base && address - spi->base < spi->block_size) {
            *offset = address - spi->base;
            return spi;
        }
    }
    return NULL;
}

static K22SerialI2c* i2c_at(K22Serial* serial, uint32_t address, uint32_t* offset) {
    for (size_t index = 0; index < 3; index++) {
        K22SerialI2c* i2c = &serial->i2c[index];
        if (i2c->present && address >= i2c->base && address - i2c->base < i2c->block_size) {
            *offset = address - i2c->base;
            return i2c;
        }
    }
    return NULL;
}

static uint8_t uart_capacity(const K22SerialUart* uart) {
    bool receive_fifo = (uart->registers[UART_PFIFO] & 0x08u) != 0;
    return receive_fifo ? uart->fifo_depth : 1;
}

static uint8_t uart_transmit_capacity(const K22SerialUart* uart) {
    bool transmit_fifo = (uart->registers[UART_PFIFO] & 0x80u) != 0;
    return transmit_fifo ? uart->fifo_depth : 1;
}

static void refresh_uart(K22SerialUart* uart, bool lpuart) {
    if (lpuart) {
        uint32_t status = load32(&uart->registers[LPUART_STAT]);
        status &= ~0x00e00000u;
        if (uart->receive.count != 0)
            status |= 1u << 21;
        if (uart->transmit.count < 1)
            status |= 1u << 23;
        if (uart->transmit.count == 0 && uart->transmit_cycles == 0)
            status |= 1u << 22;
        status |= (uint32_t)(fifo_error(&uart->receive) & 0x0fu) << 16;
        store32(&uart->registers[LPUART_STAT], status);
        return;
    }
    uint8_t status = uart->registers[UART_S1] & 0x1fu;
    if (uart->receive.count != 0)
        status |= 0x20u;
    if (uart->transmit.count < uart_transmit_capacity(uart))
        status |= 0x80u;
    if (uart->transmit.count == 0 && uart->transmit_cycles == 0)
        status |= 0x40u;
    status |= fifo_error(&uart->receive) & 0x0fu;
    uart->registers[UART_S1] = status;
    uart->registers[UART_RCFIFO] = uart->receive.count;
    uart->registers[UART_TCFIFO] = uart->transmit.count;
}

static void refresh_spi(K22SerialSpi* spi) {
    uint32_t status = spi->registers[SPI_SR / 4];
    status &= ~0x0000f0f0u;
    status |= (uint32_t)(spi->transmit.count & 0x0fu) << 12;
    status |= (uint32_t)(spi->receive.count & 0x0fu) << 4;
    if (spi->transmit.count < spi->fifo_depth)
        status |= 1u << 25;
    else
        status &= ~(1u << 25);
    if (spi->receive.count != 0)
        status |= 1u << 17;
    else
        status &= ~(1u << 17);
    if (spi->transfer_cycles != 0)
        status |= 1u << 30;
    else
        status &= ~(1u << 30);
    spi->registers[SPI_SR / 4] = status;
}

static bool read_uart(K22SerialUart* uart, bool lpuart, uint32_t offset, uint8_t size,
                      uint32_t* value) {
    if (!uart->clock_enabled || (lpuart ? size != 4 || (offset & 3u) != 0 : size != 1))
        return false;
    refresh_uart(uart, lpuart);
    if (lpuart) {
        if (offset == LPUART_DATA) {
            uint16_t received = 0;
            uint16_t errors = 0;
            if (fifo_pop(&uart->receive, &received, &errors))
                *value = received | (uint32_t)errors << 16;
            else
                *value = load32(&uart->registers[offset]);
            refresh_uart(uart, true);
            return true;
        }
        *value = load32(&uart->registers[offset]);
        return true;
    }
    if (offset == UART_S1)
        uart->status_read = true;
    if (offset == UART_D) {
        uint16_t received = 0;
        uint16_t errors = 0;
        if (fifo_pop(&uart->receive, &received, &errors)) {
            *value = received & 0xffu;
            uart->registers[UART_ED] = (uint8_t)errors;
        } else {
            *value = uart->registers[UART_D];
        }
        if (uart->status_read)
            uart->registers[UART_S1] &= 0xf0u;
        uart->status_read = false;
        refresh_uart(uart, false);
        return true;
    }
    *value = uart->registers[offset];
    return true;
}

static bool write_lpuart(K22SerialUart* uart, uint32_t offset, uint32_t value) {
    if (offset == LPUART_STAT) {
        uint32_t current = load32(&uart->registers[offset]);
        current &= ~(value & 0xc01f0000u);
        store32(&uart->registers[offset], current);
        return true;
    }
    if (offset == LPUART_DATA) {
        if (!fifo_push(&uart->transmit, 1, (uint16_t)(value & 0x3ffu), 0)) {
            uint32_t status = load32(&uart->registers[LPUART_STAT]);
            store32(&uart->registers[LPUART_STAT], status | (1u << 19));
        }
        return true;
    }
    if (offset == LPUART_BAUD)
        value &= 0xffe73fffu;
    if (offset == LPUART_CTRL)
        value &= 0xfffdfefdu;
    store32(&uart->registers[offset], value);
    return true;
}

static bool write_uart(K22SerialUart* uart, bool lpuart, uint32_t offset, uint8_t size,
                       uint32_t value) {
    if (!uart->clock_enabled || (lpuart ? size != 4 || (offset & 3u) != 0 : size != 1))
        return false;
    if (lpuart) {
        bool result = write_lpuart(uart, offset, value);
        refresh_uart(uart, true);
        return result;
    }
    uint8_t byte = (uint8_t)value;
    if (offset == UART_S1) {
        uart->registers[UART_S1] &= (uint8_t)~(byte & 0x1fu);
    } else if (offset == UART_S2) {
        uart->registers[UART_S2] &= (uint8_t)~(byte & 0xc0u);
        uart->registers[UART_S2] = (uart->registers[UART_S2] & 0xc0u) | (byte & 0x3fu);
    } else if (offset == UART_D) {
        if (!fifo_push(&uart->transmit, uart_transmit_capacity(uart), byte, 0))
            uart->registers[UART_SFIFO] |= 0x02u;
    } else if (offset == UART_CFIFO) {
        if ((byte & 0x40u) != 0)
            fifo_clear(&uart->receive);
        if ((byte & 0x80u) != 0)
            fifo_clear(&uart->transmit);
        uart->registers[UART_CFIFO] = byte & 0x0cu;
    } else if (offset == UART_SFIFO) {
        uart->registers[UART_SFIFO] &= (uint8_t)~(byte & 0xc3u);
    } else if (offset == UART_PFIFO) {
        uart->registers[UART_PFIFO] =
            (uart->registers[UART_PFIFO] & 0x77u) | (byte & 0x88u);
    } else if (offset == UART_ED || offset == UART_TCFIFO || offset == UART_RCFIFO) {
    } else {
        uart->registers[offset] = byte;
    }
    refresh_uart(uart, false);
    return true;
}

static bool read_spi(K22SerialSpi* spi, uint32_t offset, uint8_t size, uint32_t* value) {
    if (!spi->clock_enabled || size != 4 || (offset & 3u) != 0)
        return false;
    refresh_spi(spi);
    if (offset == SPI_POPR) {
        uint16_t received = 0;
        if (fifo_pop(&spi->receive, &received, NULL))
            *value = received;
        else {
            spi->registers[SPI_SR / 4] |= 1u << 19;
            *value = spi->registers[SPI_POPR / 4];
        }
        refresh_spi(spi);
        return true;
    }
    *value = spi->registers[offset / 4];
    return true;
}

static bool write_spi(K22SerialSpi* spi, uint32_t offset, uint8_t size, uint32_t value) {
    if (!spi->clock_enabled || size != 4 || (offset & 3u) != 0)
        return false;
    if (offset == SPI_MCR) {
        if ((value & (1u << 10)) != 0)
            fifo_clear(&spi->receive);
        if ((value & (1u << 11)) != 0)
            fifo_clear(&spi->transmit);
        spi->registers[offset / 4] = value & ~0x00000c00u;
    } else if (offset == SPI_SR) {
        spi->registers[offset / 4] &= ~(value & 0xba0a0000u);
    } else if (offset == SPI_PUSHR) {
        uint16_t command = (uint16_t)(value >> 16);
        if (!fifo_push(&spi->transmit, spi->fifo_depth, (uint16_t)value, command))
            spi->registers[SPI_SR / 4] |= 1u << 27;
    } else if (offset == SPI_POPR) {
    } else {
        spi->registers[offset / 4] = value;
    }
    refresh_spi(spi);
    return true;
}

static K22SerialEndpoint i2c_endpoint(const K22Serial* serial, const K22SerialI2c* i2c) {
    return (K22SerialEndpoint)(K22_SERIAL_I2C0 + (i2c - serial->i2c));
}

static bool read_i2c(K22Serial* serial, K22SerialI2c* i2c, uint32_t offset, uint8_t size,
                     uint32_t* value) {
    if (!i2c->clock_enabled || size != 1)
        return false;
    if (offset == I2C_D) {
        *value = i2c->registers[I2C_D];
        if ((i2c->registers[I2C_C1] & 0x30u) == 0) {
            if (i2c->slave_receive.count != 0)
                fifo_pop(&i2c->slave_receive, NULL, NULL);
        } else if ((i2c->registers[I2C_C1] & 0x10u) == 0) {
            i2c->read_pending = true;
            i2c->transfer_pending = true;
            i2c->transfer_cycles = 0;
            push_event(serial, i2c_endpoint(serial, i2c), K22_SERIAL_EVENT_I2C_READ, 0);
        }
        return true;
    }
    *value = i2c->registers[offset];
    return true;
}

static void i2c_stop(K22Serial* serial, K22SerialI2c* i2c) {
    i2c->registers[I2C_S] &= (uint8_t)~0x20u;
    i2c->transfer_pending = false;
    i2c->transfer_cycles = 0;
    push_event(serial, i2c_endpoint(serial, i2c), K22_SERIAL_EVENT_I2C_STOP, 0);
}

static bool write_i2c(K22Serial* serial, K22SerialI2c* i2c, uint32_t offset, uint8_t size,
                      uint32_t value) {
    if (!i2c->clock_enabled || size != 1)
        return false;
    uint8_t byte = (uint8_t)value;
    if (offset == I2C_C1) {
        uint8_t previous = i2c->registers[I2C_C1];
        i2c->registers[I2C_C1] = byte & (uint8_t)~0x04u;
        if ((byte & 0x80u) == 0) {
            if ((previous & 0x20u) != 0 || (i2c->registers[I2C_S] & 0x20u) != 0)
                i2c_stop(serial, i2c);
            else {
                i2c->transfer_pending = false;
                i2c->transfer_cycles = 0;
            }
            i2c->registers[I2C_S] = 0;
        } else if ((byte & 0x20u) != 0 && (previous & 0x20u) == 0) {
            i2c->registers[I2C_S] |= 0x20u;
            push_event(serial, i2c_endpoint(serial, i2c), K22_SERIAL_EVENT_I2C_START, 0);
        } else if ((byte & 0x04u) != 0 && (previous & 0x04u) == 0 && (byte & 0x20u) != 0) {
            push_event(serial, i2c_endpoint(serial, i2c),
                       K22_SERIAL_EVENT_I2C_REPEATED_START, 0);
        } else if ((byte & 0x20u) == 0 && (previous & 0x20u) != 0) {
            i2c_stop(serial, i2c);
        }
    } else if (offset == I2C_S) {
        i2c->registers[I2C_S] &= (uint8_t)~(byte & 0x12u);
    } else if (offset == I2C_FLT) {
        i2c->registers[I2C_FLT] =
            (byte & 0x1fu) | (i2c->registers[I2C_FLT] & (uint8_t)~(byte & 0xe0u));
    } else if (offset == I2C_D) {
        i2c->registers[I2C_D] = byte;
        if ((i2c->registers[I2C_C1] & 0x30u) == 0x30u) {
            i2c->pending_value = byte;
            i2c->read_pending = false;
            i2c->transfer_pending = true;
            i2c->transfer_cycles = 0;
            push_event(serial, i2c_endpoint(serial, i2c), K22_SERIAL_EVENT_I2C_WRITE, byte);
        } else if (i2c->slave_transmit) {
            i2c->pending_value = byte;
            fifo_push(&i2c->slave_transmit_fifo, K22_SERIAL_FIFO_CAPACITY, byte, 0);
        }
    } else {
        i2c->registers[offset] = byte;
    }
    return true;
}

bool k22_serial_read(K22Serial* serial, uint32_t address, uint8_t size, uint32_t* value) {
    if (serial == NULL || value == NULL)
        return false;
    bool lpuart = false;
    uint32_t offset = 0;
    K22SerialUart* uart = uart_at(serial, address, &lpuart, &offset);
    if (uart != NULL)
        return read_uart(uart, lpuart, offset, size, value);
    K22SerialSpi* spi = spi_at(serial, address, &offset);
    if (spi != NULL)
        return read_spi(spi, offset, size, value);
    K22SerialI2c* i2c = i2c_at(serial, address, &offset);
    if (i2c != NULL)
        return read_i2c(serial, i2c, offset, size, value);
    return false;
}

bool k22_serial_write(K22Serial* serial, uint32_t address, uint8_t size, uint32_t value) {
    if (serial == NULL)
        return false;
    bool lpuart = false;
    uint32_t offset = 0;
    K22SerialUart* uart = uart_at(serial, address, &lpuart, &offset);
    if (uart != NULL)
        return write_uart(uart, lpuart, offset, size, value);
    K22SerialSpi* spi = spi_at(serial, address, &offset);
    if (spi != NULL)
        return write_spi(spi, offset, size, value);
    K22SerialI2c* i2c = i2c_at(serial, address, &offset);
    if (i2c != NULL)
        return write_i2c(serial, i2c, offset, size, value);
    return false;
}

static uint32_t uart_frame_cycles(const K22SerialUart* uart, bool lpuart) {
    if (lpuart) {
        uint32_t baud = load32(&uart->registers[LPUART_BAUD]);
        uint32_t sbr = baud & 0x1fffu;
        uint32_t osr = ((baud >> 24) & 0x1fu) + 1u;
        return (sbr == 0 ? 1u : sbr * osr) * 10u;
    }
    uint32_t sbr = ((uint32_t)(uart->registers[0] & 0x1fu) << 8) | uart->registers[1];
    uint32_t brfa = uart->registers[UART_C4] & 0x1fu;
    uint32_t thirty_second_cycles = sbr * 512u + brfa * 16u;
    return thirty_second_cycles == 0 ? 1u : (thirty_second_cycles * 10u + 31u) / 32u;
}

static void advance_uart(K22SerialUart* uart, bool lpuart, uint32_t cycles) {
    uint32_t control =
        lpuart ? load32(&uart->registers[LPUART_CTRL]) : uart->registers[UART_C2];
    uint32_t receive_enable = lpuart ? 1u << 18 : 0x04u;
    uint32_t transmit_enable = lpuart ? 1u << 19 : 0x08u;
    while (cycles != 0 && uart->clock_enabled) {
        uint32_t elapsed = cycles;
        if ((control & receive_enable) != 0 && uart->wire_receive.count != 0) {
            if (uart->receive_cycles == 0)
                uart->receive_cycles = uart_frame_cycles(uart, lpuart);
            if (elapsed > uart->receive_cycles)
                elapsed = uart->receive_cycles;
        }
        if ((control & transmit_enable) != 0 && uart->transmit.count != 0) {
            if (uart->transmit_cycles == 0)
                uart->transmit_cycles = uart_frame_cycles(uart, lpuart);
            if (elapsed > uart->transmit_cycles)
                elapsed = uart->transmit_cycles;
        }
        if (elapsed == cycles && uart->receive_cycles == 0 && uart->transmit_cycles == 0)
            break;
        cycles -= elapsed;
        if (uart->receive_cycles != 0) {
            uart->receive_cycles -= elapsed;
            if (uart->receive_cycles == 0) {
                uint16_t value = 0;
                uint16_t errors = 0;
                fifo_pop(&uart->wire_receive, &value, &errors);
                if (!fifo_push(&uart->receive, lpuart ? 1 : uart_capacity(uart), value,
                               errors)) {
                    if (lpuart) {
                        uint32_t status = load32(&uart->registers[LPUART_STAT]);
                        store32(&uart->registers[LPUART_STAT], status | (1u << 19));
                    } else {
                        uart->registers[UART_SFIFO] |= 0x04u;
                        uart->registers[UART_S1] |= 0x08u;
                    }
                } else if (lpuart) {
                    uint32_t status = load32(&uart->registers[LPUART_STAT]);
                    store32(&uart->registers[LPUART_STAT],
                            status | (uint32_t)(errors & 0x0fu) << 16);
                } else {
                    uart->registers[UART_S1] |= errors & 0x0fu;
                }
            }
        }
        if (uart->transmit_cycles != 0) {
            uart->transmit_cycles -= elapsed;
            if (uart->transmit_cycles == 0) {
                uint16_t value = 0;
                fifo_pop(&uart->transmit, &value, NULL);
                fifo_push(&uart->wire_transmit, K22_SERIAL_FIFO_CAPACITY, value, 0);
            }
        }
    }
    refresh_uart(uart, lpuart);
}

static uint32_t spi_frame_cycles(const K22SerialSpi* spi) {
    static const uint16_t baud_scalers[16] = {
        2, 4, 6, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
    static const uint8_t prescalers[4] = {2, 3, 5, 7};
    const uint16_t command = spi->transmit.metadata[spi->transmit.read_index];
    const uint32_t ctar =
        spi->registers[(((command >> 12) & 7u) != 0 ? SPI_CTAR1 : SPI_CTAR0) / 4];
    uint32_t frame_bits = ((ctar >> 27) & 0x0fu) + 1u;
    uint32_t cycles = prescalers[(ctar >> 16) & 3u] * baud_scalers[ctar & 0x0fu];
    if ((ctar & (1u << 31)) != 0 && cycles > 1)
        cycles /= 2u;
    return cycles * frame_bits;
}

static void advance_spi(K22SerialSpi* spi, uint32_t cycles) {
    while (cycles != 0 && spi->clock_enabled && spi->transmit.count != 0 &&
           (spi->registers[SPI_MCR / 4] & 0x00004001u) == 0) {
        if (spi->transfer_cycles == 0)
            spi->transfer_cycles = spi_frame_cycles(spi);
        uint32_t elapsed = cycles < spi->transfer_cycles ? cycles : spi->transfer_cycles;
        cycles -= elapsed;
        spi->transfer_cycles -= elapsed;
        if (spi->transfer_cycles == 0) {
            uint16_t transmitted;
            uint16_t received = 0xffffu;
            uint16_t command = 0;
            fifo_pop(&spi->transmit, &transmitted, &command);
            fifo_pop(&spi->wire_receive, &received, NULL);
            fifo_push(&spi->wire_transmit, K22_SERIAL_FIFO_CAPACITY, transmitted, command);
            spi->registers[SPI_POPR / 4] = received;
            if (!fifo_push(&spi->receive, spi->fifo_depth, received, 0))
                spi->registers[SPI_SR / 4] |= 1u << 19;
            spi->registers[SPI_TCR / 4] += 1u << 16;
            spi->registers[SPI_SR / 4] |= 1u << 31;
            if ((command & (1u << 11)) != 0)
                spi->registers[SPI_SR / 4] |= 1u << 28;
        }
    }
    refresh_spi(spi);
}

static uint32_t i2c_transfer_cycles(const K22SerialI2c* i2c) {
    static const uint16_t dividers[64] = {
        20,   22,   24,   26,   28,   30,   34,   40,   28,   32,   36,   40,  44,
        48,   56,   68,   48,   56,   64,   72,   80,   88,   104,  128,  80,  96,
        112,  128,  144,  160,  192,  240,  160,  192,  224,  256,  288,  320, 384,
        480,  320,  384,  448,  512,  576,  640,  768,  960,  640,  768,  896, 1024,
        1152, 1280, 1536, 1920, 1280, 1536, 1792, 2048, 2304, 2560, 3072, 3840};
    uint8_t multiplier = (uint8_t)(1u << ((i2c->registers[I2C_F] >> 6) & 3u));
    return (uint32_t)dividers[i2c->registers[I2C_F] & 0x3fu] * multiplier * 9u;
}

static void advance_i2c(K22SerialI2c* i2c, uint32_t cycles) {
    if (!i2c->clock_enabled || !i2c->transfer_pending)
        return;
    if (i2c->transfer_cycles == 0)
        i2c->transfer_cycles = i2c_transfer_cycles(i2c);
    if (cycles < i2c->transfer_cycles) {
        i2c->transfer_cycles -= cycles;
        return;
    }
    i2c->transfer_cycles = 0;
    i2c->transfer_pending = false;
    if (i2c->read_pending) {
        uint16_t received = 0xffu;
        fifo_pop(&i2c->receive, &received, NULL);
        i2c->registers[I2C_D] = (uint8_t)received;
    }
    i2c->registers[I2C_S] |= 0x82u;
    if (i2c->acknowledge)
        i2c->registers[I2C_S] &= (uint8_t)~0x01u;
    else
        i2c->registers[I2C_S] |= 0x01u;
}

void k22_serial_advance(K22Serial* serial, uint32_t bus_cycles) {
    if (serial == NULL)
        return;
    advance_uart(&serial->lpuart0, true, bus_cycles);
    for (size_t index = 0; index < 6; index++)
        advance_uart(&serial->uart[index], false, bus_cycles);
    for (size_t index = 0; index < 3; index++)
        advance_spi(&serial->spi[index], bus_cycles);
    for (size_t index = 0; index < 3; index++)
        advance_i2c(&serial->i2c[index], bus_cycles);
}

static K22SerialUart* endpoint_uart(K22Serial* serial, K22SerialEndpoint endpoint,
                                    bool* lpuart) {
    if (endpoint == K22_SERIAL_LPUART0) {
        *lpuart = true;
        return &serial->lpuart0;
    }
    *lpuart = false;
    if (endpoint >= K22_SERIAL_UART0 && endpoint <= K22_SERIAL_UART5)
        return &serial->uart[endpoint - K22_SERIAL_UART0];
    return NULL;
}

static K22SerialSpi* endpoint_spi(K22Serial* serial, K22SerialEndpoint endpoint) {
    if (endpoint >= K22_SERIAL_SPI0 && endpoint <= K22_SERIAL_SPI2)
        return &serial->spi[endpoint - K22_SERIAL_SPI0];
    return NULL;
}

static K22SerialI2c* endpoint_i2c(K22Serial* serial, K22SerialEndpoint endpoint) {
    if (endpoint >= K22_SERIAL_I2C0 && endpoint <= K22_SERIAL_I2C2)
        return &serial->i2c[endpoint - K22_SERIAL_I2C0];
    return NULL;
}

bool k22_serial_push_receive(K22Serial* serial, K22SerialEndpoint endpoint, uint16_t value,
                             uint8_t errors) {
    if (serial == NULL)
        return false;
    bool lpuart;
    K22SerialUart* uart = endpoint_uart(serial, endpoint, &lpuart);
    if (uart != NULL)
        return uart->present && fifo_push(&uart->wire_receive, K22_SERIAL_FIFO_CAPACITY,
                                          value, errors & 0x0fu);
    K22SerialSpi* spi = endpoint_spi(serial, endpoint);
    if (spi != NULL)
        return spi->present &&
               fifo_push(&spi->wire_receive, K22_SERIAL_FIFO_CAPACITY, value, 0);
    K22SerialI2c* i2c = endpoint_i2c(serial, endpoint);
    if (i2c == NULL || !i2c->present)
        return false;
    if ((i2c->registers[I2C_S] & 0x40u) != 0 && !i2c->slave_transmit) {
        if (!fifo_push(&i2c->slave_receive, K22_SERIAL_FIFO_CAPACITY, value, 0))
            return false;
        i2c->registers[I2C_D] = (uint8_t)value;
        i2c->registers[I2C_S] |= 0x82u;
        return true;
    }
    return fifo_push(&i2c->receive, K22_SERIAL_FIFO_CAPACITY, value, 0);
}

bool k22_serial_pop_transmit(K22Serial* serial, K22SerialEndpoint endpoint,
                             uint16_t* value) {
    if (serial == NULL || value == NULL)
        return false;
    bool lpuart;
    K22SerialUart* uart = endpoint_uart(serial, endpoint, &lpuart);
    if (uart != NULL)
        return uart->present && fifo_pop(&uart->wire_transmit, value, NULL);
    K22SerialSpi* spi = endpoint_spi(serial, endpoint);
    if (spi != NULL)
        return spi->present && fifo_pop(&spi->wire_transmit, value, NULL);
    K22SerialI2c* i2c = endpoint_i2c(serial, endpoint);
    return i2c != NULL && i2c->present && fifo_pop(&i2c->slave_transmit_fifo, value, NULL);
}

bool k22_serial_pop_spi_transfer(K22Serial* serial, K22SerialEndpoint endpoint,
                                 K22SerialSpiTransfer* transfer) {
    if (serial == NULL || transfer == NULL)
        return false;
    K22SerialSpi* spi = endpoint_spi(serial, endpoint);
    if (spi == NULL || !spi->present)
        return false;
    uint16_t command = 0;
    if (!fifo_pop(&spi->wire_transmit, &transfer->data, &command))
        return false;
    transfer->chip_selects = (uint8_t)(command & 0x3fu);
    transfer->clock_and_transfer_attributes = (uint8_t)((command >> 12) & 7u);
    transfer->continuous_chip_select = (command & (1u << 15)) != 0;
    transfer->end_of_queue = (command & (1u << 11)) != 0;
    return true;
}

bool k22_serial_i2c_set_acknowledge(K22Serial* serial, K22SerialEndpoint endpoint,
                                    bool acknowledge) {
    if (serial == NULL)
        return false;
    K22SerialI2c* i2c = endpoint_i2c(serial, endpoint);
    if (i2c == NULL || !i2c->present)
        return false;
    i2c->acknowledge = acknowledge;
    return true;
}

bool k22_serial_i2c_lose_arbitration(K22Serial* serial, K22SerialEndpoint endpoint) {
    if (serial == NULL)
        return false;
    K22SerialI2c* i2c = endpoint_i2c(serial, endpoint);
    if (i2c == NULL || !i2c->present || !i2c->clock_enabled)
        return false;
    i2c->registers[I2C_S] |= 0x12u;
    i2c->registers[I2C_S] &= (uint8_t)~0x20u;
    i2c->registers[I2C_C1] &= (uint8_t)~0x20u;
    i2c->transfer_pending = false;
    i2c->transfer_cycles = 0;
    return true;
}

bool k22_serial_i2c_slave_address(K22Serial* serial, K22SerialEndpoint endpoint,
                                  uint16_t address, bool read) {
    if (serial == NULL)
        return false;
    K22SerialI2c* i2c = endpoint_i2c(serial, endpoint);
    if (i2c == NULL || !i2c->present || !i2c->clock_enabled ||
        (i2c->registers[I2C_C1] & 0x80u) == 0)
        return false;
    uint16_t own_address = i2c->registers[I2C_A1] >> 1;
    own_address |= (uint16_t)(i2c->registers[I2C_C2] & 0x07u) << 7;
    uint16_t second_address = i2c->registers[I2C_A2] >> 1;
    if (address != own_address && address != second_address)
        return false;
    i2c->registers[I2C_S] |= 0x42u;
    if (read)
        i2c->registers[I2C_S] |= 0x04u;
    else
        i2c->registers[I2C_S] &= (uint8_t)~0x04u;
    i2c->slave_transmit = read;
    return true;
}

bool k22_serial_pop_event(K22Serial* serial, K22SerialEvent* event) {
    if (serial == NULL || event == NULL || serial->event_count == 0)
        return false;
    *event = serial->events[serial->event_read_index];
    serial->event_read_index =
        (uint8_t)((serial->event_read_index + 1u) % K22_SERIAL_EVENT_CAPACITY);
    serial->event_count--;
    return true;
}

static bool uart_irq(const K22SerialUart* source, bool lpuart, bool error) {
    K22SerialUart* uart = (K22SerialUart*)source;
    if (!uart->present || !uart->clock_enabled)
        return false;
    refresh_uart(uart, lpuart);
    if (lpuart) {
        uint32_t status = load32(&uart->registers[LPUART_STAT]);
        uint32_t control = load32(&uart->registers[LPUART_CTRL]);
        return ((status & control & 0x00e00000u) != 0) ||
               ((((status & 0x000f0000u) << 8) & control) != 0);
    }
    uint8_t status = uart->registers[UART_S1];
    if (error)
        return (status & uart->registers[UART_C3] & 0x0fu) != 0;
    uint8_t control = uart->registers[UART_C2];
    return (((status & 0x20u) != 0 && (control & 0x20u) != 0) ||
            ((status & 0x40u) != 0 && (control & 0x40u) != 0) ||
            ((status & 0x80u) != 0 && (control & 0x80u) != 0) ||
            ((status & 0x10u) != 0 && (control & 0x10u) != 0));
}

static bool spi_irq(const K22SerialSpi* source) {
    K22SerialSpi* spi = (K22SerialSpi*)source;
    if (!spi->present || !spi->clock_enabled)
        return false;
    refresh_spi(spi);
    uint32_t status = spi->registers[SPI_SR / 4];
    uint32_t enable = spi->registers[SPI_RSER / 4];
    return ((status & enable & 0xb8080000u) != 0) ||
           ((status & (1u << 25)) != 0 && (enable & (3u << 24)) == (1u << 25)) ||
           ((status & (1u << 17)) != 0 && (enable & (3u << 16)) == (1u << 17));
}

bool k22_serial_irq(const K22Serial* serial, K22SerialIrq irq) {
    if (serial == NULL || irq >= K22_SERIAL_IRQ_COUNT)
        return false;
    switch (irq) {
    case K22_SERIAL_IRQ_LPUART0:
        return uart_irq(&serial->lpuart0, true, false);
    case K22_SERIAL_IRQ_SPI0:
        return spi_irq(&serial->spi[0]);
    case K22_SERIAL_IRQ_SPI1:
        return spi_irq(&serial->spi[1]);
    case K22_SERIAL_IRQ_SPI2:
        return spi_irq(&serial->spi[2]);
    case K22_SERIAL_IRQ_I2C0:
    case K22_SERIAL_IRQ_I2C1:
    case K22_SERIAL_IRQ_I2C2: {
        const K22SerialI2c* i2c = &serial->i2c[irq - K22_SERIAL_IRQ_I2C0];
        return i2c->present && (i2c->registers[I2C_C1] & 0x40u) != 0 &&
               (i2c->registers[I2C_S] & 0x12u) != 0;
    }
    case K22_SERIAL_IRQ_UART0:
    case K22_SERIAL_IRQ_UART1:
    case K22_SERIAL_IRQ_UART2:
    case K22_SERIAL_IRQ_UART3:
    case K22_SERIAL_IRQ_UART4:
    case K22_SERIAL_IRQ_UART5:
        return uart_irq(&serial->uart[(irq - K22_SERIAL_IRQ_UART0) / 2], false, false);
    case K22_SERIAL_IRQ_UART0_ERROR:
    case K22_SERIAL_IRQ_UART1_ERROR:
    case K22_SERIAL_IRQ_UART2_ERROR:
    case K22_SERIAL_IRQ_UART3_ERROR:
    case K22_SERIAL_IRQ_UART4_ERROR:
    case K22_SERIAL_IRQ_UART5_ERROR:
        return uart_irq(&serial->uart[(irq - K22_SERIAL_IRQ_UART0_ERROR) / 2], false, true);
    default:
        return false;
    }
}

static bool legacy_uart_dma(const K22SerialUart* source, bool receive) {
    K22SerialUart* uart = (K22SerialUart*)source;
    if (!uart->present || !uart->clock_enabled)
        return false;
    refresh_uart(uart, false);
    if (receive)
        return (uart->registers[UART_C5] & 0x20u) != 0 &&
               (uart->registers[UART_S1] & 0x20u) != 0;
    return (uart->registers[UART_C5] & 0x80u) != 0 &&
           (uart->registers[UART_S1] & 0x80u) != 0;
}

bool k22_serial_dma_request(const K22Serial* serial, K22SerialDmaRequest request) {
    if (serial == NULL || request >= K22_SERIAL_DMA_COUNT)
        return false;
    if (request <= K22_SERIAL_DMA_LPUART0_TRANSMIT) {
        K22SerialUart* uart = (K22SerialUart*)&serial->lpuart0;
        if (!uart->present || !uart->clock_enabled)
            return false;
        refresh_uart(uart, true);
        uint32_t baud = load32(&uart->registers[LPUART_BAUD]);
        uint32_t status = load32(&uart->registers[LPUART_STAT]);
        return request == K22_SERIAL_DMA_LPUART0_RECEIVE
                   ? (baud & (1u << 21)) != 0 && (status & (1u << 21)) != 0
                   : (baud & (1u << 23)) != 0 && (status & (1u << 23)) != 0;
    }
    if (request >= K22_SERIAL_DMA_SPI0_RECEIVE && request <= K22_SERIAL_DMA_SPI2_TRANSMIT) {
        size_t index = (request - K22_SERIAL_DMA_SPI0_RECEIVE) / 2;
        bool receive = ((request - K22_SERIAL_DMA_SPI0_RECEIVE) & 1u) == 0;
        K22SerialSpi* spi = (K22SerialSpi*)&serial->spi[index];
        if (!spi->present || !spi->clock_enabled)
            return false;
        refresh_spi(spi);
        uint32_t status = spi->registers[SPI_SR / 4];
        uint32_t enable = spi->registers[SPI_RSER / 4];
        return receive ? (status & (1u << 17)) != 0 && (enable & (3u << 16)) == (3u << 16)
                       : (status & (1u << 25)) != 0 && (enable & (3u << 24)) == (3u << 24);
    }
    if (request >= K22_SERIAL_DMA_I2C0 && request <= K22_SERIAL_DMA_I2C2) {
        const K22SerialI2c* i2c = &serial->i2c[request - K22_SERIAL_DMA_I2C0];
        return i2c->present && i2c->clock_enabled &&
               (i2c->registers[I2C_C1] & 0x01u) != 0 &&
               (i2c->registers[I2C_S] & 0x02u) != 0;
    }
    size_t index = (request - K22_SERIAL_DMA_UART0_RECEIVE) / 2;
    bool receive = ((request - K22_SERIAL_DMA_UART0_RECEIVE) & 1u) == 0;
    return legacy_uart_dma(&serial->uart[index], receive);
}
