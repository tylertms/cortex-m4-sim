#include "device/kinetis_k22/communication/serial/internal.h"

bool k22_serial_read(K22Serial* serial, uint32_t address, uint8_t size, uint32_t* value) {
    if (serial == NULL || value == NULL)
        return false;
    bool lpuart = false;
    uint32_t offset = 0;
    K22SerialUart* uart = k22_serial_internal_uart_at(serial, address, &lpuart, &offset);
    if (uart != NULL)
        return k22_serial_internal_read_uart(uart, lpuart, offset, size, value);
    K22SerialSpi* spi = k22_serial_internal_spi_at(serial, address, &offset);
    if (spi != NULL)
        return k22_serial_internal_read_spi(spi, offset, size, value);
    K22SerialI2c* i2c = k22_serial_internal_i2c_at(serial, address, &offset);
    if (i2c != NULL)
        return k22_serial_internal_read_i2c(serial, i2c, offset, size, value);
    return false;
}

bool k22_serial_write(K22Serial* serial, uint32_t address, uint8_t size, uint32_t value) {
    if (serial == NULL)
        return false;
    bool lpuart = false;
    uint32_t offset = 0;
    K22SerialUart* uart = k22_serial_internal_uart_at(serial, address, &lpuart, &offset);
    if (uart != NULL)
        return k22_serial_internal_write_uart(uart, lpuart, offset, size, value);
    K22SerialSpi* spi = k22_serial_internal_spi_at(serial, address, &offset);
    if (spi != NULL)
        return k22_serial_internal_write_spi(spi, offset, size, value);
    K22SerialI2c* i2c = k22_serial_internal_i2c_at(serial, address, &offset);
    if (i2c != NULL)
        return k22_serial_internal_write_i2c(serial, i2c, offset, size, value);
    return false;
}

static uint32_t uart_frame_cycles(const K22SerialUart* uart, bool lpuart) {
    if (lpuart) {
        uint32_t baud = k22_serial_internal_load32(&uart->registers[LPUART_BAUD]);
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
    uint32_t control = lpuart ? k22_serial_internal_load32(&uart->registers[LPUART_CTRL])
                              : uart->registers[UART_C2];
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
                k22_serial_internal_fifo_pop(&uart->wire_receive, &value, &errors);
                if (!k22_serial_internal_fifo_push(
                        &uart->receive, lpuart ? 1 : k22_serial_internal_uart_capacity(uart), value,
                        errors)) {
                    if (lpuart) {
                        uint32_t status = k22_serial_internal_load32(&uart->registers[LPUART_STAT]);
                        k22_serial_internal_store32(&uart->registers[LPUART_STAT],
                                                    status | (1u << 19));
                    } else {
                        uart->registers[UART_SFIFO] |= 0x04u;
                        uart->registers[UART_S1] |= 0x08u;
                    }
                } else if (lpuart) {
                    uint32_t status = k22_serial_internal_load32(&uart->registers[LPUART_STAT]);
                    k22_serial_internal_store32(&uart->registers[LPUART_STAT],
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
                k22_serial_internal_fifo_pop(&uart->transmit, &value, NULL);
                k22_serial_internal_fifo_push(&uart->wire_transmit, K22_SERIAL_FIFO_CAPACITY, value,
                                              0);
            }
        }
    }
    k22_serial_internal_refresh_uart(uart, lpuart);
}

static uint32_t spi_frame_cycles(const K22SerialSpi* spi) {
    static const uint16_t baud_scalers[16] = {2,   4,   6,    8,    16,   32,   64,    128,
                                              256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
    static const uint8_t prescalers[4] = {2, 3, 5, 7};
    const uint16_t command = spi->transmit.metadata[spi->transmit.read_index];
    const uint32_t ctar = spi->registers[(((command >> 12) & 7u) != 0 ? SPI_CTAR1 : SPI_CTAR0) / 4];
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
            k22_serial_internal_fifo_pop(&spi->transmit, &transmitted, &command);
            k22_serial_internal_fifo_pop(&spi->wire_receive, &received, NULL);
            k22_serial_internal_fifo_push(&spi->wire_transmit, K22_SERIAL_FIFO_CAPACITY,
                                          transmitted, command);
            spi->registers[SPI_POPR / 4] = received;
            if (!k22_serial_internal_fifo_push(&spi->receive, spi->fifo_depth, received, 0))
                spi->registers[SPI_SR / 4] |= 1u << 19;
            spi->registers[SPI_TCR / 4] += 1u << 16;
            spi->registers[SPI_SR / 4] |= 1u << 31;
            if ((command & (1u << 11)) != 0)
                spi->registers[SPI_SR / 4] |= 1u << 28;
        }
    }
    k22_serial_internal_refresh_spi(spi);
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
        k22_serial_internal_fifo_pop(&i2c->receive, &received, NULL);
        i2c->registers[I2C_D] = (uint8_t)received;
    }
    i2c->registers[I2C_S] |= 0x82u;
    if (i2c->acknowledge)
        i2c->registers[I2C_S] &= 0xfeu;
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

static K22SerialUart* endpoint_uart(K22Serial* serial, K22SerialEndpoint endpoint, bool* lpuart) {
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

void k22_serial_advance_endpoint(K22Serial* serial, K22SerialEndpoint endpoint) {
    if (serial == NULL)
        return;
    bool lpuart = false;
    K22SerialUart* uart = endpoint_uart(serial, endpoint, &lpuart);
    if (uart != NULL) {
        advance_uart(uart, lpuart, uart_frame_cycles(uart, lpuart));
        return;
    }
    K22SerialSpi* spi = endpoint_spi(serial, endpoint);
    if (spi != NULL) {
        advance_spi(spi, spi_frame_cycles(spi));
        return;
    }
    K22SerialI2c* i2c = endpoint_i2c(serial, endpoint);
    if (i2c != NULL)
        advance_i2c(i2c,
                    i2c->transfer_cycles == 0u ? i2c_transfer_cycles(i2c) : i2c->transfer_cycles);
}

bool k22_serial_push_receive(K22Serial* serial, K22SerialEndpoint endpoint, uint16_t value,
                             uint8_t errors) {
    if (serial == NULL)
        return false;
    bool lpuart;
    K22SerialUart* uart = endpoint_uart(serial, endpoint, &lpuart);
    if (uart != NULL)
        return uart->present &&
               k22_serial_internal_fifo_push(&uart->wire_receive, K22_SERIAL_FIFO_CAPACITY, value,
                                             errors & 0x0fu);
    K22SerialSpi* spi = endpoint_spi(serial, endpoint);
    if (spi != NULL)
        return spi->present && k22_serial_internal_fifo_push(&spi->wire_receive,
                                                             K22_SERIAL_FIFO_CAPACITY, value, 0);
    K22SerialI2c* i2c = endpoint_i2c(serial, endpoint);
    if (i2c == NULL || !i2c->present)
        return false;
    if ((i2c->registers[I2C_S] & 0x40u) != 0 && !i2c->slave_transmit) {
        if (!k22_serial_internal_fifo_push(&i2c->slave_receive, K22_SERIAL_FIFO_CAPACITY, value, 0))
            return false;
        i2c->registers[I2C_D] = (uint8_t)value;
        i2c->registers[I2C_S] |= 0x82u;
        return true;
    }
    return k22_serial_internal_fifo_push(&i2c->receive, K22_SERIAL_FIFO_CAPACITY, value, 0);
}

bool k22_serial_pop_transmit(K22Serial* serial, K22SerialEndpoint endpoint, uint16_t* value) {
    if (serial == NULL || value == NULL)
        return false;
    bool lpuart;
    K22SerialUart* uart = endpoint_uart(serial, endpoint, &lpuart);
    if (uart != NULL)
        return uart->present && k22_serial_internal_fifo_pop(&uart->wire_transmit, value, NULL);
    K22SerialSpi* spi = endpoint_spi(serial, endpoint);
    if (spi != NULL)
        return spi->present && k22_serial_internal_fifo_pop(&spi->wire_transmit, value, NULL);
    K22SerialI2c* i2c = endpoint_i2c(serial, endpoint);
    return i2c != NULL && i2c->present &&
           k22_serial_internal_fifo_pop(&i2c->slave_transmit_fifo, value, NULL);
}

bool k22_serial_pop_spi_transfer(K22Serial* serial, K22SerialEndpoint endpoint,
                                 K22SerialSpiTransfer* transfer) {
    if (serial == NULL || transfer == NULL)
        return false;
    K22SerialSpi* spi = endpoint_spi(serial, endpoint);
    if (spi == NULL || !spi->present)
        return false;
    uint16_t command = 0;
    if (!k22_serial_internal_fifo_pop(&spi->wire_transmit, &transfer->data, &command))
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
    i2c->registers[I2C_S] &= 0xdfu;
    i2c->registers[I2C_C1] &= 0xdfu;
    i2c->transfer_pending = false;
    i2c->transfer_cycles = 0;
    return true;
}

bool k22_serial_i2c_slave_address(K22Serial* serial, K22SerialEndpoint endpoint, uint16_t address,
                                  bool read) {
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
        i2c->registers[I2C_S] &= 0xfbu;
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
    k22_serial_internal_refresh_uart(uart, lpuart);
    if (lpuart) {
        uint32_t status = k22_serial_internal_load32(&uart->registers[LPUART_STAT]);
        uint32_t control = k22_serial_internal_load32(&uart->registers[LPUART_CTRL]);
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
    k22_serial_internal_refresh_spi(spi);
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
    k22_serial_internal_refresh_uart(uart, false);
    if (receive)
        return (uart->registers[UART_C5] & 0x20u) != 0 && (uart->registers[UART_S1] & 0x20u) != 0;
    return (uart->registers[UART_C5] & 0x80u) != 0 && (uart->registers[UART_S1] & 0x80u) != 0;
}

bool k22_serial_dma_request(const K22Serial* serial, K22SerialDmaRequest request) {
    if (serial == NULL || request >= K22_SERIAL_DMA_COUNT)
        return false;
    if (request <= K22_SERIAL_DMA_LPUART0_TRANSMIT) {
        K22SerialUart* uart = (K22SerialUart*)&serial->lpuart0;
        if (!uart->present || !uart->clock_enabled)
            return false;
        k22_serial_internal_refresh_uart(uart, true);
        uint32_t baud = k22_serial_internal_load32(&uart->registers[LPUART_BAUD]);
        uint32_t status = k22_serial_internal_load32(&uart->registers[LPUART_STAT]);
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
        k22_serial_internal_refresh_spi(spi);
        uint32_t status = spi->registers[SPI_SR / 4];
        uint32_t enable = spi->registers[SPI_RSER / 4];
        return receive ? (status & (1u << 17)) != 0 && (enable & (3u << 16)) == (3u << 16)
                       : (status & (1u << 25)) != 0 && (enable & (3u << 24)) == (3u << 24);
    }
    if (request >= K22_SERIAL_DMA_I2C0 && request <= K22_SERIAL_DMA_I2C2) {
        const K22SerialI2c* i2c = &serial->i2c[request - K22_SERIAL_DMA_I2C0];
        return i2c->present && i2c->clock_enabled && (i2c->registers[I2C_C1] & 0x01u) != 0 &&
               (i2c->registers[I2C_S] & 0x02u) != 0;
    }
    size_t index = (request - K22_SERIAL_DMA_UART0_RECEIVE) / 2;
    bool receive = ((request - K22_SERIAL_DMA_UART0_RECEIVE) & 1u) == 0;
    return legacy_uart_dma(&serial->uart[index], receive);
}
