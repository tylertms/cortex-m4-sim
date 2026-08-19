#include "cortex_m4_sim/kinetis_k22.h"

#include <stdlib.h>
#include <string.h>

#include "kinetis_k22_internal.h"

static bool valid_range(uint32_t address, uint8_t size, uint32_t base, size_t length) {
    return address >= base && (uint64_t)address + size <= (uint64_t)base + length;
}

static uint32_t load_little_endian(const uint8_t* bytes, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; index++) {
        value |= (uint32_t)bytes[index] << (index * 8u);
    }
    return value;
}

static void store_little_endian(uint8_t* bytes, uint8_t size, uint32_t value) {
    for (uint8_t index = 0; index < size; index++) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

bool kinetis_k22_memory_read(KinetisK22* device, uint32_t address, uint8_t size,
                             uint32_t* value) {
    if (valid_range(address, size, K22_FLASH_BASE, device->configuration.flash_size)) {
        *value = load_little_endian(device->flash + address, size);
        return true;
    }
    if (valid_range(address, size, device->sram_base, device->configuration.sram_size)) {
        *value = load_little_endian(device->sram + address - device->sram_base, size);
        return true;
    }
    if (valid_range(address, size, K22_PERIPHERAL_BASE, K22_PERIPHERAL_SIZE)) {
        return kinetis_k22_peripheral_read(device, address, size, value);
    }
    return false;
}

bool kinetis_k22_memory_write(KinetisK22* device, uint32_t address, uint8_t size,
                              CortexM4Access access, uint32_t value) {
    if (valid_range(address, size, K22_FLASH_BASE, device->configuration.flash_size)) {
        if (access != CORTEX_M4_ACCESS_DEBUG) {
            return false;
        }
        store_little_endian(device->flash + address, size, value);
        return true;
    }
    if (valid_range(address, size, device->sram_base, device->configuration.sram_size)) {
        store_little_endian(device->sram + address - device->sram_base, size, value);
        return true;
    }
    if (valid_range(address, size, K22_PERIPHERAL_BASE, K22_PERIPHERAL_SIZE)) {
        return kinetis_k22_peripheral_write(device, address, size, value);
    }
    return false;
}

static bool k22_read_bus(void* context, uint32_t address, uint8_t size,
                         CortexM4Access access, uint32_t* value) {
    (void)access;
    KinetisK22* device = context;
    if (address >= K22_BIT_BAND_BASE && address < K22_BIT_BAND_BASE + K22_BIT_BAND_SIZE &&
        size == 4) {
        const uint32_t alias = address - K22_BIT_BAND_BASE;
        const uint32_t byte_address = K22_PERIPHERAL_BASE + alias / 32u;
        const uint8_t bit = (uint8_t)((alias / 4u) & 7u);
        uint32_t byte = 0;
        if (!kinetis_k22_memory_read(device, byte_address, 1, &byte)) {
            return false;
        }
        *value = (byte >> bit) & 1u;
        return true;
    }
    return kinetis_k22_memory_read(device, address, size, value);
}

static bool k22_write_bus(void* context, uint32_t address, uint8_t size,
                          CortexM4Access access, uint32_t value) {
    KinetisK22* device = context;
    if (address >= K22_BIT_BAND_BASE && address < K22_BIT_BAND_BASE + K22_BIT_BAND_SIZE &&
        size == 4) {
        const uint32_t alias = address - K22_BIT_BAND_BASE;
        const uint32_t byte_address = K22_PERIPHERAL_BASE + alias / 32u;
        const uint8_t bit = (uint8_t)((alias / 4u) & 7u);
        uint32_t byte = 0;
        if (!kinetis_k22_memory_read(device, byte_address, 1, &byte)) {
            return false;
        }
        if ((value & 1u) != 0) {
            byte |= 1u << bit;
        } else {
            byte &= ~(1u << bit);
        }
        return kinetis_k22_memory_write(device, byte_address, 1, access, byte);
    }
    return kinetis_k22_memory_write(device, address, size, access, value);
}

static void k22_advance_bus(void* context, uint32_t cycles) {
    KinetisK22* device = context;
    device->cycles += cycles;
    kinetis_k22_peripheral_advance(device, cycles);
}

static void k22_reset_bus(void* context) { kinetis_k22_warm_reset(context, 0x04u); }

KinetisK22Configuration kinetis_k22_default_configuration(void) {
    KinetisK22Configuration configuration;
    configuration.flash_size = 512u * 1024u;
    configuration.sram_size = 128u * 1024u;
    configuration.vector_table_address = 0;
    return configuration;
}

KinetisK22* kinetis_k22_create(KinetisK22Configuration configuration) {
    if (configuration.flash_size == 0 || configuration.sram_size == 0 ||
        configuration.sram_size > 0x40000000u) {
        return NULL;
    }
    KinetisK22* device = calloc(1, sizeof(*device));
    if (device == NULL) {
        return NULL;
    }
    device->configuration = configuration;
    device->sram_base = K22_SRAM_CENTER - (uint32_t)(configuration.sram_size / 2u);
    device->flash = malloc(configuration.flash_size);
    device->sram = calloc(1, configuration.sram_size);
    device->peripheral = calloc(1, K22_PERIPHERAL_SIZE);
    if (device->flash == NULL || device->sram == NULL || device->peripheral == NULL) {
        kinetis_k22_destroy(device);
        return NULL;
    }
    memset(device->flash, 0xff, configuration.flash_size);
    CortexM4Bus bus = {device, k22_read_bus, k22_write_bus, k22_advance_bus, k22_reset_bus};
    device->cpu = cortex_m4_create(bus);
    if (device->cpu == NULL) {
        kinetis_k22_destroy(device);
        return NULL;
    }
    return device;
}

void kinetis_k22_destroy(KinetisK22* device) {
    if (device == NULL) {
        return;
    }
    cortex_m4_destroy(device->cpu);
    free(device->flash);
    free(device->sram);
    free(device->peripheral);
    free(device);
}

CortexM4* kinetis_k22_cpu(KinetisK22* device) {
    return device == NULL ? NULL : device->cpu;
}

const CortexM4* kinetis_k22_cpu_const(const KinetisK22* device) {
    return device == NULL ? NULL : device->cpu;
}

bool kinetis_k22_reset(KinetisK22* device) {
    if (device == NULL) {
        return false;
    }
    memset(device->sram, 0, device->configuration.sram_size);
    memset(device->peripheral, 0, K22_PERIPHERAL_SIZE);
    device->cycles = 0;
    kinetis_k22_peripheral_reset(device);
    device->peripheral[0x7f000u] = 0x80u;
    return cortex_m4_reset(device->cpu, device->configuration.vector_table_address);
}

void kinetis_k22_warm_reset(KinetisK22* device, uint8_t cause) {
    if (device == NULL) {
        return;
    }
    kinetis_k22_peripheral_reset(device);
    device->peripheral[0x7f000u] = cause;
    cortex_m4_reset(device->cpu, device->configuration.vector_table_address);
}

bool kinetis_k22_load(KinetisK22* device, uint32_t address, const void* data, size_t size) {
    if (device == NULL || data == NULL) {
        return false;
    }
    if (address < device->configuration.flash_size &&
        (uint64_t)address + size <= device->configuration.flash_size) {
        memcpy(device->flash + address, data, size);
        return true;
    }
    if (address >= device->sram_base &&
        (uint64_t)address + size <=
            (uint64_t)device->sram_base + device->configuration.sram_size) {
        memcpy(device->sram + address - device->sram_base, data, size);
        return true;
    }
    return false;
}

bool kinetis_k22_read(const KinetisK22* device, uint32_t address, void* data, size_t size) {
    if (device == NULL || data == NULL) {
        return false;
    }
    if (size == 1 || size == 2 || size == 4) {
        uint32_t value = 0;
        if (!kinetis_k22_memory_read((KinetisK22*)device, address, (uint8_t)size, &value)) {
            return false;
        }
        memcpy(data, &value, size);
        return true;
    }
    uint8_t* output = data;
    for (size_t index = 0; index < size; index++) {
        uint32_t value = 0;
        if (!kinetis_k22_memory_read((KinetisK22*)device, address + (uint32_t)index, 1,
                                     &value)) {
            return false;
        }
        output[index] = (uint8_t)value;
    }
    return true;
}

bool kinetis_k22_write(KinetisK22* device, uint32_t address, const void* data,
                       size_t size) {
    if (device == NULL || data == NULL) {
        return false;
    }
    if (size == 1 || size == 2 || size == 4) {
        uint32_t value = 0;
        memcpy(&value, data, size);
        return kinetis_k22_memory_write(device, address, (uint8_t)size,
                                        CORTEX_M4_ACCESS_DEBUG, value);
    }
    const uint8_t* input = data;
    for (size_t index = 0; index < size; index++) {
        if (!kinetis_k22_memory_write(device, address + (uint32_t)index, 1,
                                      CORTEX_M4_ACCESS_DEBUG, input[index])) {
            return false;
        }
    }
    return true;
}

bool kinetis_k22_copy(KinetisK22* destination, const KinetisK22* source) {
    if (destination == NULL || source == NULL ||
        destination->configuration.flash_size != source->configuration.flash_size ||
        destination->configuration.sram_size != source->configuration.sram_size) {
        return false;
    }
    memcpy(destination->flash, source->flash, source->configuration.flash_size);
    memcpy(destination->sram, source->sram, source->configuration.sram_size);
    memcpy(destination->peripheral, source->peripheral, K22_PERIPHERAL_SIZE);
    destination->cycles = source->cycles;
    memcpy(destination->gpio_external, source->gpio_external,
           sizeof(destination->gpio_external));
    memcpy(destination->gpio_driven, source->gpio_driven, sizeof(destination->gpio_driven));
    memcpy(destination->adc_channels, source->adc_channels,
           sizeof(destination->adc_channels));
    memcpy(destination->pit_current, source->pit_current, sizeof(destination->pit_current));
    destination->pit_cycle_remainder = source->pit_cycle_remainder;
    destination->dma_enabled = source->dma_enabled;
    destination->dma_interrupts = source->dma_interrupts;
    destination->dma_active = source->dma_active;
    destination->watchdog_unlock_stage = source->watchdog_unlock_stage;
    destination->watchdog_refresh_stage = source->watchdog_refresh_stage;
    destination->watchdog_ticks = source->watchdog_ticks;
    destination->watchdog_cycle_remainder = source->watchdog_cycle_remainder;
    destination->uart1_receive = source->uart1_receive;
    destination->uart1_transmit = source->uart1_transmit;
    destination->spi0_receive = source->spi0_receive;
    destination->spi0_transmit = source->spi0_transmit;
    destination->i2c0_receive = source->i2c0_receive;
    destination->i2c0_transfer = source->i2c0_transfer;
    return cortex_m4_copy(destination->cpu, source->cpu);
}

void kinetis_k22_advance(KinetisK22* device, uint32_t cycles) {
    if (device == NULL || cycles == 0) {
        return;
    }
    k22_advance_bus(device, cycles);
}
