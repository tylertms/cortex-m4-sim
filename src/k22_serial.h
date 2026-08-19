#ifndef CORTEX_M4_K22_SERIAL_H
#define CORTEX_M4_K22_SERIAL_H

#include "k22_profile.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    K22_SERIAL_ENDPOINT_COUNT = 9,
    K22_SERIAL_FIFO_CAPACITY = 256,
    K22_SERIAL_EVENT_CAPACITY = 64,
};

typedef enum {
    K22_SERIAL_LPUART0,
    K22_SERIAL_SPI0,
    K22_SERIAL_SPI1,
    K22_SERIAL_I2C0,
    K22_SERIAL_I2C1,
    K22_SERIAL_I2C2,
    K22_SERIAL_UART0,
    K22_SERIAL_UART1,
    K22_SERIAL_UART2,
    K22_SERIAL_ENDPOINT_INVALID = K22_SERIAL_ENDPOINT_COUNT,
} K22SerialEndpoint;

typedef enum {
    K22_SERIAL_IRQ_LPUART0,
    K22_SERIAL_IRQ_SPI0,
    K22_SERIAL_IRQ_SPI1,
    K22_SERIAL_IRQ_I2C0,
    K22_SERIAL_IRQ_I2C1,
    K22_SERIAL_IRQ_I2C2,
    K22_SERIAL_IRQ_UART0,
    K22_SERIAL_IRQ_UART0_ERROR,
    K22_SERIAL_IRQ_UART1,
    K22_SERIAL_IRQ_UART1_ERROR,
    K22_SERIAL_IRQ_UART2,
    K22_SERIAL_IRQ_UART2_ERROR,
    K22_SERIAL_IRQ_COUNT,
} K22SerialIrq;

typedef enum {
    K22_SERIAL_DMA_LPUART0_RECEIVE,
    K22_SERIAL_DMA_LPUART0_TRANSMIT,
    K22_SERIAL_DMA_SPI0_RECEIVE,
    K22_SERIAL_DMA_SPI0_TRANSMIT,
    K22_SERIAL_DMA_SPI1_RECEIVE,
    K22_SERIAL_DMA_SPI1_TRANSMIT,
    K22_SERIAL_DMA_I2C0,
    K22_SERIAL_DMA_I2C1,
    K22_SERIAL_DMA_I2C2,
    K22_SERIAL_DMA_UART0_RECEIVE,
    K22_SERIAL_DMA_UART0_TRANSMIT,
    K22_SERIAL_DMA_UART1_RECEIVE,
    K22_SERIAL_DMA_UART1_TRANSMIT,
    K22_SERIAL_DMA_UART2_RECEIVE,
    K22_SERIAL_DMA_UART2_TRANSMIT,
    K22_SERIAL_DMA_COUNT,
} K22SerialDmaRequest;

typedef enum {
    K22_SERIAL_EVENT_I2C_START,
    K22_SERIAL_EVENT_I2C_REPEATED_START,
    K22_SERIAL_EVENT_I2C_STOP,
    K22_SERIAL_EVENT_I2C_WRITE,
    K22_SERIAL_EVENT_I2C_READ,
} K22SerialEventType;

typedef struct {
    K22SerialEndpoint endpoint;
    K22SerialEventType type;
    uint16_t value;
} K22SerialEvent;

typedef struct {
    uint16_t data;
    uint8_t chip_selects;
    uint8_t clock_and_transfer_attributes;
    bool continuous_chip_select;
    bool end_of_queue;
} K22SerialSpiTransfer;

typedef struct {
    uint16_t values[K22_SERIAL_FIFO_CAPACITY];
    uint16_t metadata[K22_SERIAL_FIFO_CAPACITY];
    uint16_t read_index;
    uint16_t write_index;
    uint16_t count;
} K22SerialFifo;

typedef struct {
    K22PeripheralId peripheral;
    uint32_t base;
    uint32_t block_size;
    uint8_t registers[0x40];
    K22SerialFifo receive;
    K22SerialFifo transmit;
    K22SerialFifo wire_receive;
    K22SerialFifo wire_transmit;
    uint32_t receive_cycles;
    uint32_t transmit_cycles;
    uint8_t fifo_depth;
    bool present;
    bool clock_enabled;
    bool status_read;
} K22SerialUart;

typedef struct {
    K22PeripheralId peripheral;
    uint32_t base;
    uint32_t block_size;
    uint32_t registers[0x8c / 4];
    K22SerialFifo receive;
    K22SerialFifo transmit;
    K22SerialFifo wire_receive;
    K22SerialFifo wire_transmit;
    uint32_t transfer_cycles;
    uint8_t fifo_depth;
    bool present;
    bool clock_enabled;
} K22SerialSpi;

typedef struct {
    K22PeripheralId peripheral;
    uint32_t base;
    uint32_t block_size;
    uint8_t registers[0x0c];
    K22SerialFifo receive;
    K22SerialFifo slave_receive;
    K22SerialFifo slave_transmit_fifo;
    uint32_t transfer_cycles;
    uint8_t pending_value;
    bool present;
    bool clock_enabled;
    bool acknowledge;
    bool transfer_pending;
    bool read_pending;
    bool slave_transmit;
} K22SerialI2c;

typedef struct {
    const K22Profile* profile;
    uint32_t core_clock_hz;
    uint32_t bus_clock_hz;
    K22SerialUart lpuart0;
    K22SerialUart uart[3];
    K22SerialSpi spi[2];
    K22SerialI2c i2c[3];
    K22SerialEvent events[K22_SERIAL_EVENT_CAPACITY];
    uint8_t event_read_index;
    uint8_t event_write_index;
    uint8_t event_count;
} K22Serial;

bool k22_serial_init(K22Serial* serial, const K22Profile* profile);
void k22_serial_reset(K22Serial* serial);
bool k22_serial_copy(K22Serial* destination, const K22Serial* source);
void k22_serial_set_clocks(K22Serial* serial, uint32_t core_clock_hz,
                           uint32_t bus_clock_hz);
bool k22_serial_set_clock_gate(K22Serial* serial, K22PeripheralId peripheral, bool enabled);
bool k22_serial_read(K22Serial* serial, uint32_t address, uint8_t size, uint32_t* value);
bool k22_serial_write(K22Serial* serial, uint32_t address, uint8_t size, uint32_t value);
void k22_serial_advance(K22Serial* serial, uint32_t bus_cycles);
bool k22_serial_push_receive(K22Serial* serial, K22SerialEndpoint endpoint, uint16_t value,
                             uint8_t errors);
bool k22_serial_pop_transmit(K22Serial* serial, K22SerialEndpoint endpoint,
                             uint16_t* value);
bool k22_serial_pop_spi_transfer(K22Serial* serial, K22SerialEndpoint endpoint,
                                 K22SerialSpiTransfer* transfer);
bool k22_serial_i2c_set_acknowledge(K22Serial* serial, K22SerialEndpoint endpoint,
                                    bool acknowledge);
bool k22_serial_i2c_lose_arbitration(K22Serial* serial, K22SerialEndpoint endpoint);
bool k22_serial_i2c_slave_address(K22Serial* serial, K22SerialEndpoint endpoint,
                                  uint16_t address, bool read);
bool k22_serial_pop_event(K22Serial* serial, K22SerialEvent* event);
bool k22_serial_irq(const K22Serial* serial, K22SerialIrq irq);
bool k22_serial_dma_request(const K22Serial* serial, K22SerialDmaRequest request);

#endif
