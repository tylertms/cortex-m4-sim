#ifndef KINETIS_K22_SIM_SERIAL_INTERNAL_H
#define KINETIS_K22_SIM_SERIAL_INTERNAL_H

#include "device/kinetis_k22/communication/serial/api.h"

#include <string.h>

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

bool k22_serial_internal_fifo_pop(K22SerialFifo* fifo, uint16_t* value, uint16_t* metadata);
bool k22_serial_internal_fifo_push(K22SerialFifo* fifo, uint16_t capacity, uint16_t value,
                                   uint16_t metadata);
bool k22_serial_internal_read_i2c(K22Serial* serial, K22SerialI2c* i2c, uint32_t offset,
                                  uint8_t size, uint32_t* value);
bool k22_serial_internal_read_spi(K22SerialSpi* spi, uint32_t offset, uint8_t size,
                                  uint32_t* value);
bool k22_serial_internal_read_uart(K22SerialUart* uart, bool lpuart, uint32_t offset, uint8_t size,
                                   uint32_t* value);
bool k22_serial_internal_write_i2c(K22Serial* serial, K22SerialI2c* i2c, uint32_t offset,
                                   uint8_t size, uint32_t value);
bool k22_serial_internal_write_spi(K22SerialSpi* spi, uint32_t offset, uint8_t size,
                                   uint32_t value);
bool k22_serial_internal_write_uart(K22SerialUart* uart, bool lpuart, uint32_t offset, uint8_t size,
                                    uint32_t value);
K22SerialI2c* k22_serial_internal_i2c_at(K22Serial* serial, uint32_t address, uint32_t* offset);
K22SerialSpi* k22_serial_internal_spi_at(K22Serial* serial, uint32_t address, uint32_t* offset);
K22SerialUart* k22_serial_internal_uart_at(K22Serial* serial, uint32_t address, bool* lpuart,
                                           uint32_t* offset);
uint32_t k22_serial_internal_load32(const uint8_t* bytes);
uint8_t k22_serial_internal_uart_capacity(const K22SerialUart* uart);
void k22_serial_internal_refresh_spi(K22SerialSpi* spi);
void k22_serial_internal_refresh_uart(K22SerialUart* uart, bool lpuart);
void k22_serial_internal_store32(uint8_t* bytes, uint32_t value);

#endif
