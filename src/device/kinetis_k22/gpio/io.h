#ifndef KINETIS_K22_SIM_K22_IO_H
#define KINETIS_K22_SIM_K22_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device/kinetis_k22/variants/profile.h"

enum {
    K22_IO_PORT_COUNT = 5,
    K22_IO_PIN_COUNT = 32,
    K22_IO_FIFO_CAPACITY = 16,
};

typedef enum {
    K22_IO_EVENT_IRQ,
    K22_IO_EVENT_DMA,
    K22_IO_EVENT_GPIO_OUTPUT,
    K22_IO_EVENT_USB_TOKEN,
    K22_IO_EVENT_CAN_TRANSMIT,
    K22_IO_EVENT_I2S_TRANSMIT,
    K22_IO_EVENT_FLEXBUS_TRANSFER,
    K22_IO_EVENT_ACCESS_ERROR,
} K22IoEventType;

typedef struct {
    K22IoEventType type;
    uint32_t source;
    uint32_t value;
    uint32_t auxiliary;
    uint8_t data[8];
    uint8_t length;
    bool extended;
    bool remote;
} K22IoEvent;

typedef void (*K22IoEventHandler)(void* context, const K22IoEvent* event);

typedef struct {
    const K22Profile* profile;
    uint32_t package_pin_mask[K22_IO_PORT_COUNT];
    uint8_t flash_configuration[16];
    K22IoEventHandler event_handler;
    void* event_context;
} K22IoConfiguration;

typedef struct {
    uint32_t identifier;
    uint8_t length;
    uint8_t data[8];
    bool extended;
    bool remote;
} K22CanFrame;

typedef enum {
    K22_SYSMPU_READ,
    K22_SYSMPU_WRITE,
    K22_SYSMPU_EXECUTE,
} K22SysMpuAccess;

typedef struct {
    K22IoConfiguration configuration;
    bool clock_enabled[K22_PERIPHERAL_COUNT];
    uint32_t port_pcr[K22_IO_PORT_COUNT][K22_IO_PIN_COUNT];
    uint32_t port_isfr[K22_IO_PORT_COUNT];
    uint32_t port_dfer[K22_IO_PORT_COUNT];
    uint8_t port_dfcr[K22_IO_PORT_COUNT];
    uint8_t port_dfwr[K22_IO_PORT_COUNT];
    uint32_t gpio_pdor[K22_IO_PORT_COUNT];
    uint32_t gpio_pddr[K22_IO_PORT_COUNT];
    uint32_t gpio_external[K22_IO_PORT_COUNT];
    uint32_t gpio_external_drive[K22_IO_PORT_COUNT];
    uint32_t gpio_filtered[K22_IO_PORT_COUNT];
    uint32_t gpio_pending[K22_IO_PORT_COUNT];
    uint8_t gpio_filter_age[K22_IO_PORT_COUNT][K22_IO_PIN_COUNT];
    uint8_t usb[0x160];
    uint32_t usb_cycle_remainder;
    uint32_t can[0x8c0 / 4];
    uint32_t i2s[0x108 / 4];
    uint32_t flexbus[0x64 / 4];
    uint32_t mcm[0x3c / 4];
    uint32_t sysmpu[0x830 / 4];
    uint32_t i2s_receive_fifo[K22_IO_FIFO_CAPACITY];
    uint32_t i2s_transmit_fifo[K22_IO_FIFO_CAPACITY];
    uint8_t i2s_receive_read;
    uint8_t i2s_receive_write;
    uint8_t i2s_receive_count;
    uint8_t i2s_transmit_read;
    uint8_t i2s_transmit_write;
    uint8_t i2s_transmit_count;
} K22Io;

K22IoConfiguration k22_io_default_configuration(const K22Profile* profile);
bool k22_io_init(K22Io* io, K22IoConfiguration configuration);
void k22_io_reset(K22Io* io);
bool k22_io_copy(K22Io* destination, const K22Io* source);
bool k22_io_read(K22Io* io, uint32_t address, uint8_t size, uint32_t* value);
bool k22_io_write(K22Io* io, uint32_t address, uint8_t size, uint32_t value);
void k22_io_advance(K22Io* io, uint32_t cycles);
void k22_io_set_clock(K22Io* io, K22PeripheralId peripheral, bool enabled);
bool k22_io_clock_enabled(const K22Io* io, K22PeripheralId peripheral);
bool k22_io_drive_pin(K22Io* io, uint8_t port, uint8_t pin, bool high);
bool k22_io_release_pin(K22Io* io, uint8_t port, uint8_t pin);
uint32_t k22_io_pin_input(const K22Io* io, uint8_t port);
bool k22_io_usb_token(K22Io* io, uint8_t endpoint, uint8_t token, bool transmit);
bool k22_io_can_receive(K22Io* io, const K22CanFrame* frame);
bool k22_io_i2s_receive(K22Io* io, uint32_t sample);
bool k22_io_i2s_transmit(K22Io* io, uint32_t* sample);
bool k22_io_irq_asserted(const K22Io* io, uint8_t irq);
bool k22_io_flexbus_transfer(K22Io* io, uint32_t address, uint8_t size, bool write, uint32_t value);
bool k22_io_sysmpu_access(K22Io* io, uint32_t address, uint8_t master, bool supervisor,
                          K22SysMpuAccess access);

#endif
