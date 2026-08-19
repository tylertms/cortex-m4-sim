#ifndef CORTEX_M4_SIM_KINETIS_K22_H
#define CORTEX_M4_SIM_KINETIS_K22_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cortex_m4_sim/cortex_m4.h"

typedef struct KinetisK22 KinetisK22;

typedef struct {
    size_t flash_size;
    size_t sram_size;
    uint32_t vector_table_address;
} KinetisK22Configuration;

typedef enum {
    KINETIS_K22_I2C_START,
    KINETIS_K22_I2C_REPEATED_START,
    KINETIS_K22_I2C_WRITE,
    KINETIS_K22_I2C_READ,
    KINETIS_K22_I2C_STOP,
} KinetisK22I2cTransferType;

typedef struct {
    KinetisK22I2cTransferType type;
    uint8_t value;
} KinetisK22I2cTransfer;

KinetisK22Configuration kinetis_k22_default_configuration(void);
KinetisK22* kinetis_k22_create(KinetisK22Configuration configuration);
void kinetis_k22_destroy(KinetisK22* device);
CortexM4* kinetis_k22_cpu(KinetisK22* device);
const CortexM4* kinetis_k22_cpu_const(const KinetisK22* device);
bool kinetis_k22_reset(KinetisK22* device);
bool kinetis_k22_load(KinetisK22* device, uint32_t address, const void* data, size_t size);
bool kinetis_k22_read(const KinetisK22* device, uint32_t address, void* data, size_t size);
bool kinetis_k22_write(KinetisK22* device, uint32_t address, const void* data, size_t size);
bool kinetis_k22_copy(KinetisK22* destination, const KinetisK22* source);
void kinetis_k22_advance(KinetisK22* device, uint32_t cycles);
void kinetis_k22_watchdog_advance(KinetisK22* device, uint32_t ticks);
void kinetis_k22_set_adc0_channel(KinetisK22* device, uint8_t channel, uint16_t value);
void kinetis_k22_gpio_drive(KinetisK22* device, uint8_t port, uint8_t pin, bool high);
void kinetis_k22_gpio_release(KinetisK22* device, uint8_t port, uint8_t pin);
bool kinetis_k22_uart1_receive(KinetisK22* device, uint8_t value, uint8_t status);
bool kinetis_k22_uart1_transmit(KinetisK22* device, uint8_t* value);
bool kinetis_k22_spi0_receive(KinetisK22* device, uint16_t value);
bool kinetis_k22_spi0_transmit(KinetisK22* device, uint16_t* value);
bool kinetis_k22_i2c0_transfer(KinetisK22* device, KinetisK22I2cTransfer* transfer);
void kinetis_k22_i2c0_acknowledge(KinetisK22* device, bool acknowledge);
bool kinetis_k22_i2c0_receive(KinetisK22* device, uint8_t value);

#endif
