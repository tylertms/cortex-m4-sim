#include "device/kinetis_k22/communication/serial/api.h"
#include "device/kinetis_k22/communication/serial/internal.h"

#include <string.h>

#include "device/kinetis_k22/variants/manifest.h"

uint32_t k22_serial_internal_load32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

void k22_serial_internal_store32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void reset_register_block(const K22Profile* profile, uint32_t base, uint32_t block_size,
                                 uint8_t* registers, size_t capacity) {
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

bool k22_serial_internal_fifo_push(K22SerialFifo* fifo, uint16_t capacity, uint16_t value,
                                   uint16_t metadata) {
    if (fifo->count >= capacity || capacity > K22_SERIAL_FIFO_CAPACITY)
        return false;
    fifo->values[fifo->write_index] = value;
    fifo->metadata[fifo->write_index] = metadata;
    fifo->write_index = (uint16_t)((fifo->write_index + 1u) % K22_SERIAL_FIFO_CAPACITY);
    fifo->count++;
    return true;
}

bool k22_serial_internal_fifo_pop(K22SerialFifo* fifo, uint16_t* value, uint16_t* metadata) {
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

static void push_event(K22Serial* serial, K22SerialEndpoint endpoint, K22SerialEventType type,
                       uint16_t value) {
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

static bool configure_block(const K22Profile* profile, K22PeripheralId peripheral, uint32_t* base,
                            uint32_t* size) {
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

static void configure_spi(K22SerialSpi* spi, const K22Profile* profile, K22PeripheralId peripheral,
                          uint8_t depth) {
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

void k22_serial_set_clocks(K22Serial* serial, uint32_t core_clock_hz, uint32_t bus_clock_hz) {
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

bool k22_serial_set_clock_gate(K22Serial* serial, K22PeripheralId peripheral, bool enabled) {
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

K22SerialUart* k22_serial_internal_uart_at(K22Serial* serial, uint32_t address, bool* lpuart,
                                           uint32_t* offset) {
    K22SerialUart* candidates[] = {
        &serial->lpuart0, &serial->uart[0], &serial->uart[1], &serial->uart[2],
        &serial->uart[3], &serial->uart[4], &serial->uart[5],
    };
    for (size_t index = 0; index < 7; index++) {
        K22SerialUart* uart = candidates[index];
        if (uart->present && address >= uart->base && address - uart->base < uart->block_size) {
            *lpuart = index == 0;
            *offset = address - uart->base;
            return uart;
        }
    }
    return NULL;
}

K22SerialSpi* k22_serial_internal_spi_at(K22Serial* serial, uint32_t address, uint32_t* offset) {
    for (size_t index = 0; index < 3; index++) {
        K22SerialSpi* spi = &serial->spi[index];
        if (spi->present && address >= spi->base && address - spi->base < spi->block_size) {
            *offset = address - spi->base;
            return spi;
        }
    }
    return NULL;
}

K22SerialI2c* k22_serial_internal_i2c_at(K22Serial* serial, uint32_t address, uint32_t* offset) {
    for (size_t index = 0; index < 3; index++) {
        K22SerialI2c* i2c = &serial->i2c[index];
        if (i2c->present && address >= i2c->base && address - i2c->base < i2c->block_size) {
            *offset = address - i2c->base;
            return i2c;
        }
    }
    return NULL;
}

uint8_t k22_serial_internal_uart_capacity(const K22SerialUart* uart) {
    bool receive_fifo = (uart->registers[UART_PFIFO] & 0x08u) != 0;
    return receive_fifo ? uart->fifo_depth : 1;
}

static uint8_t uart_transmit_capacity(const K22SerialUart* uart) {
    bool transmit_fifo = (uart->registers[UART_PFIFO] & 0x80u) != 0;
    return transmit_fifo ? uart->fifo_depth : 1;
}

void k22_serial_internal_refresh_uart(K22SerialUart* uart, bool lpuart) {
    if (lpuart) {
        uint32_t status = k22_serial_internal_load32(&uart->registers[LPUART_STAT]);
        status &= ~0x00e00000u;
        if (uart->receive.count != 0)
            status |= 1u << 21;
        if (uart->transmit.count < 1)
            status |= 1u << 23;
        if (uart->transmit.count == 0 && uart->transmit_cycles == 0)
            status |= 1u << 22;
        status |= (uint32_t)(fifo_error(&uart->receive) & 0x0fu) << 16;
        k22_serial_internal_store32(&uart->registers[LPUART_STAT], status);
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
    uart->registers[UART_RCFIFO] = (uint8_t)uart->receive.count;
    uart->registers[UART_TCFIFO] = (uint8_t)uart->transmit.count;
}

void k22_serial_internal_refresh_spi(K22SerialSpi* spi) {
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

bool k22_serial_internal_read_uart(K22SerialUart* uart, bool lpuart, uint32_t offset, uint8_t size,
                                   uint32_t* value) {
    if (!uart->clock_enabled || (lpuart ? size != 4 || (offset & 3u) != 0 : size != 1))
        return false;
    k22_serial_internal_refresh_uart(uart, lpuart);
    if (lpuart) {
        if (offset == LPUART_DATA) {
            uint16_t received = 0;
            uint16_t errors = 0;
            if (k22_serial_internal_fifo_pop(&uart->receive, &received, &errors))
                *value = received | (uint32_t)errors << 16;
            else
                *value = k22_serial_internal_load32(&uart->registers[offset]);
            k22_serial_internal_refresh_uart(uart, true);
            return true;
        }
        *value = k22_serial_internal_load32(&uart->registers[offset]);
        return true;
    }
    if (offset == UART_S1)
        uart->status_read = true;
    if (offset == UART_D) {
        uint16_t received = 0;
        uint16_t errors = 0;
        if (k22_serial_internal_fifo_pop(&uart->receive, &received, &errors)) {
            *value = received & 0xffu;
            uart->registers[UART_ED] = (uint8_t)errors;
        } else {
            *value = uart->registers[UART_D];
        }
        if (uart->status_read)
            uart->registers[UART_S1] &= 0xf0u;
        uart->status_read = false;
        k22_serial_internal_refresh_uart(uart, false);
        return true;
    }
    *value = uart->registers[offset];
    return true;
}

static bool write_lpuart(K22SerialUart* uart, uint32_t offset, uint32_t value) {
    if (offset == LPUART_STAT) {
        uint32_t current = k22_serial_internal_load32(&uart->registers[offset]);
        current &= ~(value & 0xc01f0000u);
        k22_serial_internal_store32(&uart->registers[offset], current);
        return true;
    }
    if (offset == LPUART_DATA) {
        if (!k22_serial_internal_fifo_push(&uart->transmit, 1, (uint16_t)(value & 0x3ffu), 0)) {
            uint32_t status = k22_serial_internal_load32(&uart->registers[LPUART_STAT]);
            k22_serial_internal_store32(&uart->registers[LPUART_STAT], status | (1u << 19));
        }
        return true;
    }
    if (offset == LPUART_BAUD)
        value &= 0xffe73fffu;
    if (offset == LPUART_CTRL)
        value &= 0xfffdfefdu;
    k22_serial_internal_store32(&uart->registers[offset], value);
    return true;
}

bool k22_serial_internal_write_uart(K22SerialUart* uart, bool lpuart, uint32_t offset, uint8_t size,
                                    uint32_t value) {
    if (!uart->clock_enabled || (lpuart ? size != 4 || (offset & 3u) != 0 : size != 1))
        return false;
    if (lpuart) {
        bool result = write_lpuart(uart, offset, value);
        k22_serial_internal_refresh_uart(uart, true);
        return result;
    }
    uint8_t byte = (uint8_t)value;
    if (offset == UART_S1) {
        uart->registers[UART_S1] &= (uint8_t)~(byte & 0x1fu);
    } else if (offset == UART_S2) {
        uart->registers[UART_S2] &= (uint8_t)~(byte & 0xc0u);
        uart->registers[UART_S2] = (uart->registers[UART_S2] & 0xc0u) | (byte & 0x3fu);
    } else if (offset == UART_D) {
        if (!k22_serial_internal_fifo_push(&uart->transmit, uart_transmit_capacity(uart), byte, 0))
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
        uart->registers[UART_PFIFO] = (uart->registers[UART_PFIFO] & 0x77u) | (byte & 0x88u);
    } else if (offset == UART_ED || offset == UART_TCFIFO || offset == UART_RCFIFO) {
    } else {
        uart->registers[offset] = byte;
    }
    k22_serial_internal_refresh_uart(uart, false);
    return true;
}

bool k22_serial_internal_read_spi(K22SerialSpi* spi, uint32_t offset, uint8_t size,
                                  uint32_t* value) {
    if (!spi->clock_enabled || size != 4 || (offset & 3u) != 0)
        return false;
    k22_serial_internal_refresh_spi(spi);
    if (offset == SPI_POPR) {
        uint16_t received = 0;
        if (k22_serial_internal_fifo_pop(&spi->receive, &received, NULL))
            *value = received;
        else {
            spi->registers[SPI_SR / 4] |= 1u << 19;
            *value = spi->registers[SPI_POPR / 4];
        }
        k22_serial_internal_refresh_spi(spi);
        return true;
    }
    *value = spi->registers[offset / 4];
    return true;
}

bool k22_serial_internal_write_spi(K22SerialSpi* spi, uint32_t offset, uint8_t size,
                                   uint32_t value) {
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
        if (!k22_serial_internal_fifo_push(&spi->transmit, spi->fifo_depth, (uint16_t)value,
                                           command))
            spi->registers[SPI_SR / 4] |= 1u << 27;
    } else if (offset == SPI_POPR) {
    } else {
        spi->registers[offset / 4] = value;
    }
    k22_serial_internal_refresh_spi(spi);
    return true;
}

static K22SerialEndpoint i2c_endpoint(const K22Serial* serial, const K22SerialI2c* i2c) {
    return (K22SerialEndpoint)(K22_SERIAL_I2C0 + (i2c - serial->i2c));
}

bool k22_serial_internal_read_i2c(K22Serial* serial, K22SerialI2c* i2c, uint32_t offset,
                                  uint8_t size, uint32_t* value) {
    if (!i2c->clock_enabled || size != 1)
        return false;
    if (offset == I2C_D) {
        *value = i2c->registers[I2C_D];
        if ((i2c->registers[I2C_C1] & 0x30u) == 0) {
            if (i2c->slave_receive.count != 0)
                k22_serial_internal_fifo_pop(&i2c->slave_receive, NULL, NULL);
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
    i2c->registers[I2C_S] &= 0xdfu;
    i2c->transfer_pending = false;
    i2c->transfer_cycles = 0;
    push_event(serial, i2c_endpoint(serial, i2c), K22_SERIAL_EVENT_I2C_STOP, 0);
}

bool k22_serial_internal_write_i2c(K22Serial* serial, K22SerialI2c* i2c, uint32_t offset,
                                   uint8_t size, uint32_t value) {
    if (!i2c->clock_enabled || size != 1)
        return false;
    uint8_t byte = (uint8_t)value;
    if (offset == I2C_C1) {
        uint8_t previous = i2c->registers[I2C_C1];
        i2c->registers[I2C_C1] = byte & 0xfbu;
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
            push_event(serial, i2c_endpoint(serial, i2c), K22_SERIAL_EVENT_I2C_REPEATED_START, 0);
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
            k22_serial_internal_fifo_push(&i2c->slave_transmit_fifo, K22_SERIAL_FIFO_CAPACITY, byte,
                                          0);
        }
    } else {
        i2c->registers[offset] = byte;
    }
    return true;
}
