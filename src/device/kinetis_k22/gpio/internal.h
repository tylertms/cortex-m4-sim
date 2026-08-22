#ifndef KINETIS_K22_SIM_GPIO_INTERNAL_H
#define KINETIS_K22_SIM_GPIO_INTERNAL_H

#include "device/kinetis_k22/gpio/io.h"

#include <string.h>

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

bool k22_io_internal_fifo_pop(uint32_t* fifo, uint8_t* read, uint8_t* count, uint32_t* value);
bool k22_io_internal_fifo_push(uint32_t* fifo, uint8_t* write, uint8_t* count, uint32_t value);
bool k22_io_internal_is_gpio(K22PeripheralId id);
bool k22_io_internal_is_port(K22PeripheralId id);
bool k22_io_internal_module_clocked(const K22Io* io, K22PeripheralId id);
bool k22_io_internal_pin_exists(const K22Io* io, uint8_t port, uint8_t pin);
bool k22_io_internal_read_direct(K22Io* io, uint32_t address, uint8_t size, uint32_t* value);
bool k22_io_internal_read_gpio(K22Io* io, K22PeripheralLocation location, uint8_t size,
                               uint32_t* value);
bool k22_io_internal_read_port(K22Io* io, K22PeripheralLocation location, uint8_t size,
                               uint32_t* value);
bool k22_io_internal_valid_size(uint8_t size);
bool k22_io_internal_write_direct(K22Io* io, uint32_t address, uint8_t size, uint32_t value);
bool k22_io_internal_write_gpio(K22Io* io, K22PeripheralLocation location, uint8_t size,
                                uint32_t value);
bool k22_io_internal_write_port(K22Io* io, K22PeripheralLocation location, uint8_t size,
                                uint32_t value);
uint32_t k22_io_internal_load_bytes(const uint8_t* data, uint8_t size);
uint32_t k22_io_internal_pin_level_unfiltered(const K22Io* io, uint8_t port);
uint32_t k22_io_internal_pin_level(const K22Io* io, uint8_t port);
uint8_t k22_io_internal_first_set_bit(uint32_t value);
void k22_io_internal_commit_pin_level(K22Io* io, uint8_t port, uint8_t pin, bool previous,
                                      bool high);
void k22_io_internal_emit_can(K22Io* io, uint8_t mailbox, uint32_t identifier, uint32_t control,
                              uint32_t high, uint32_t low);
void k22_io_internal_emit(K22Io* io, K22IoEventType type, uint32_t source, uint32_t value,
                          uint32_t auxiliary);
void k22_io_internal_update_i2s_requests(K22Io* io);

#endif
