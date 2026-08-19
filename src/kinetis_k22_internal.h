#ifndef CORTEX_M4_KINETIS_K22_INTERNAL_H
#define CORTEX_M4_KINETIS_K22_INTERNAL_H

#include "cortex_m4_sim/kinetis_k22.h"

enum {
    K22_FLASH_BASE = 0x00000000u,
    K22_SRAM_CENTER = 0x20000000u,
    K22_PERIPHERAL_BASE = 0x40000000u,
    K22_PERIPHERAL_SIZE = 0x00100000u,
    K22_BIT_BAND_BASE = 0x42000000u,
    K22_BIT_BAND_SIZE = 0x02000000u,
    K22_FIFO_CAPACITY = 256,
};

typedef struct {
    uint16_t values[K22_FIFO_CAPACITY];
    uint16_t read_index;
    uint16_t write_index;
    uint16_t count;
} KinetisK22Fifo;

struct KinetisK22 {
    KinetisK22Configuration configuration;
    CortexM4* cpu;
    uint8_t* flash;
    uint8_t* sram;
    uint8_t* peripheral;
    uint32_t sram_base;
    uint64_t cycles;
    uint32_t gpio_external[5];
    uint32_t gpio_driven[5];
    uint16_t adc_channels[32];
    uint32_t pit_current[4];
    uint8_t pit_cycle_remainder;
    uint16_t dma_enabled;
    uint16_t dma_interrupts;
    uint16_t dma_active;
    KinetisK22Fifo uart1_receive;
    KinetisK22Fifo uart1_transmit;
    KinetisK22Fifo spi0_receive;
    KinetisK22Fifo spi0_transmit;
    KinetisK22Fifo i2c0_receive;
    KinetisK22Fifo i2c0_transfer;
};

bool kinetis_k22_memory_read(KinetisK22* device, uint32_t address, uint8_t size,
                             uint32_t* value);
bool kinetis_k22_memory_write(KinetisK22* device, uint32_t address, uint8_t size,
                              CortexM4Access access, uint32_t value);
bool kinetis_k22_peripheral_read(KinetisK22* device, uint32_t address, uint8_t size,
                                 uint32_t* value);
bool kinetis_k22_peripheral_write(KinetisK22* device, uint32_t address, uint8_t size,
                                  uint32_t value);
void kinetis_k22_peripheral_advance(KinetisK22* device, uint32_t cycles);
void kinetis_k22_peripheral_reset(KinetisK22* device);

#endif
