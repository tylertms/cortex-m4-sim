#include "cortex_m4_sim/kinetis_k22.h"

#include <stdlib.h>
#include <string.h>

enum {
  K22_FLASH_BASE = 0x00000000u,
  K22_SRAM_CENTER = 0x20000000u,
  K22_PERIPHERAL_BASE = 0x40000000u,
  K22_PERIPHERAL_SIZE = 0x00100000u,
  K22_BIT_BAND_BASE = 0x42000000u,
  K22_BIT_BAND_SIZE = 0x02000000u,
  K22_SMC_PMCTRL = 0x4007e001u,
  K22_SMC_PMSTAT = 0x4007e003u,
  K22_MCG_C1 = 0x40064000u,
  K22_MCG_C6 = 0x40064005u,
  K22_MCG_S = 0x40064006u,
  K22_LPTMR0_CSR = 0x40040000u,
  K22_UART1_S1 = 0x4006b004u,
};

struct KinetisK22 {
  KinetisK22Configuration configuration;
  CortexM4 *cpu;
  uint8_t *flash;
  uint8_t *sram;
  uint8_t *peripheral;
  uint32_t sram_base;
  uint64_t cycles;
};

static bool valid_range(uint32_t address, uint8_t size, uint32_t base,
                        size_t length) {
  return address >= base && (uint64_t)address + size <= (uint64_t)base + length;
}

static uint32_t load_little_endian(const uint8_t *bytes, uint8_t size) {
  uint32_t value = 0;
  for (uint8_t index = 0; index < size; index++) {
    value |= (uint32_t)bytes[index] << (index * 8u);
  }
  return value;
}

static void store_little_endian(uint8_t *bytes, uint8_t size, uint32_t value) {
  for (uint8_t index = 0; index < size; index++) {
    bytes[index] = (uint8_t)(value >> (index * 8u));
  }
}

static bool k22_read_direct(const KinetisK22 *device, uint32_t address,
                            uint8_t size, uint32_t *value) {
  if (valid_range(address, size, K22_FLASH_BASE,
                  device->configuration.flash_size)) {
    *value = load_little_endian(device->flash + address, size);
    return true;
  }
  if (valid_range(address, size, device->sram_base,
                  device->configuration.sram_size)) {
    *value =
        load_little_endian(device->sram + address - device->sram_base, size);
    return true;
  }
  if (valid_range(address, size, K22_PERIPHERAL_BASE, K22_PERIPHERAL_SIZE)) {
    *value = load_little_endian(
        device->peripheral + address - K22_PERIPHERAL_BASE, size);
    return true;
  }
  return false;
}

static bool k22_write_direct(KinetisK22 *device, uint32_t address, uint8_t size,
                             CortexM4Access access, uint32_t value) {
  if (valid_range(address, size, K22_FLASH_BASE,
                  device->configuration.flash_size)) {
    if (access != CORTEX_M4_ACCESS_DEBUG) {
      return false;
    }
    store_little_endian(device->flash + address, size, value);
    return true;
  }
  if (valid_range(address, size, device->sram_base,
                  device->configuration.sram_size)) {
    store_little_endian(device->sram + address - device->sram_base, size,
                        value);
    return true;
  }
  if (valid_range(address, size, K22_PERIPHERAL_BASE, K22_PERIPHERAL_SIZE)) {
    store_little_endian(device->peripheral + address - K22_PERIPHERAL_BASE,
                        size, value);
    if (address == K22_SMC_PMCTRL) {
      device->peripheral[K22_SMC_PMSTAT - K22_PERIPHERAL_BASE] =
          (value & 0x60u) == 0x60u ? 0x80u : 1u;
    }
    if (address == K22_MCG_C1 || address == K22_MCG_C6) {
      device->peripheral[K22_MCG_S - K22_PERIPHERAL_BASE] &= (uint8_t)~0x1cu;
    }
    if (address == K22_LPTMR0_CSR && (value & 1u) != 0) {
      device->peripheral[K22_LPTMR0_CSR - K22_PERIPHERAL_BASE] =
          (uint8_t)(value | 0x80u);
    }
    return true;
  }
  return false;
}

static bool k22_read_bus(void *context, uint32_t address, uint8_t size,
                         CortexM4Access access, uint32_t *value) {
  (void)access;
  KinetisK22 *device = context;
  if (address >= K22_BIT_BAND_BASE &&
      address < K22_BIT_BAND_BASE + K22_BIT_BAND_SIZE && size == 4) {
    const uint32_t alias = address - K22_BIT_BAND_BASE;
    const uint32_t byte_address = K22_PERIPHERAL_BASE + alias / 32u;
    const uint8_t bit = (uint8_t)((alias / 4u) & 7u);
    uint32_t byte = 0;
    if (!k22_read_direct(device, byte_address, 1, &byte)) {
      return false;
    }
    *value = (byte >> bit) & 1u;
    return true;
  }
  return k22_read_direct(device, address, size, value);
}

static bool k22_write_bus(void *context, uint32_t address, uint8_t size,
                          CortexM4Access access, uint32_t value) {
  KinetisK22 *device = context;
  if (address >= K22_BIT_BAND_BASE &&
      address < K22_BIT_BAND_BASE + K22_BIT_BAND_SIZE && size == 4) {
    const uint32_t alias = address - K22_BIT_BAND_BASE;
    const uint32_t byte_address = K22_PERIPHERAL_BASE + alias / 32u;
    const uint8_t bit = (uint8_t)((alias / 4u) & 7u);
    uint32_t byte = 0;
    if (!k22_read_direct(device, byte_address, 1, &byte)) {
      return false;
    }
    if ((value & 1u) != 0) {
      byte |= 1u << bit;
    } else {
      byte &= ~(1u << bit);
    }
    return k22_write_direct(device, byte_address, 1, access, byte);
  }
  return k22_write_direct(device, address, size, access, value);
}

static void k22_advance_bus(void *context, uint32_t cycles) {
  KinetisK22 *device = context;
  device->cycles += cycles;
}

KinetisK22Configuration kinetis_k22_default_configuration(void) {
  KinetisK22Configuration configuration;
  configuration.flash_size = 512u * 1024u;
  configuration.sram_size = 128u * 1024u;
  configuration.vector_table_address = 0;
  return configuration;
}

KinetisK22 *kinetis_k22_create(KinetisK22Configuration configuration) {
  if (configuration.flash_size == 0 || configuration.sram_size == 0 ||
      configuration.sram_size > 0x40000000u) {
    return NULL;
  }
  KinetisK22 *device = calloc(1, sizeof(*device));
  if (device == NULL) {
    return NULL;
  }
  device->configuration = configuration;
  device->sram_base =
      K22_SRAM_CENTER - (uint32_t)(configuration.sram_size / 2u);
  device->flash = malloc(configuration.flash_size);
  device->sram = calloc(1, configuration.sram_size);
  device->peripheral = calloc(1, K22_PERIPHERAL_SIZE);
  if (device->flash == NULL || device->sram == NULL ||
      device->peripheral == NULL) {
    kinetis_k22_destroy(device);
    return NULL;
  }
  memset(device->flash, 0xff, configuration.flash_size);
  CortexM4Bus bus = {device, k22_read_bus, k22_write_bus, k22_advance_bus};
  device->cpu = cortex_m4_create(bus);
  if (device->cpu == NULL) {
    kinetis_k22_destroy(device);
    return NULL;
  }
  return device;
}

void kinetis_k22_destroy(KinetisK22 *device) {
  if (device == NULL) {
    return;
  }
  cortex_m4_destroy(device->cpu);
  free(device->flash);
  free(device->sram);
  free(device->peripheral);
  free(device);
}

CortexM4 *kinetis_k22_cpu(KinetisK22 *device) {
  return device == NULL ? NULL : device->cpu;
}

const CortexM4 *kinetis_k22_cpu_const(const KinetisK22 *device) {
  return device == NULL ? NULL : device->cpu;
}

bool kinetis_k22_reset(KinetisK22 *device) {
  if (device == NULL) {
    return false;
  }
  memset(device->sram, 0, device->configuration.sram_size);
  memset(device->peripheral, 0, K22_PERIPHERAL_SIZE);
  device->cycles = 0;
  device->peripheral[K22_SMC_PMSTAT - K22_PERIPHERAL_BASE] = 1;
  device->peripheral[K22_UART1_S1 - K22_PERIPHERAL_BASE] = 0xc0u;
  return cortex_m4_reset(device->cpu,
                         device->configuration.vector_table_address);
}

bool kinetis_k22_load(KinetisK22 *device, uint32_t address, const void *data,
                      size_t size) {
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

bool kinetis_k22_read(const KinetisK22 *device, uint32_t address, void *data,
                      size_t size) {
  if (device == NULL || data == NULL) {
    return false;
  }
  uint8_t *output = data;
  for (size_t index = 0; index < size; index++) {
    uint32_t value = 0;
    if (!k22_read_direct(device, address + (uint32_t)index, 1, &value)) {
      return false;
    }
    output[index] = (uint8_t)value;
  }
  return true;
}

bool kinetis_k22_write(KinetisK22 *device, uint32_t address, const void *data,
                       size_t size) {
  if (device == NULL || data == NULL) {
    return false;
  }
  const uint8_t *input = data;
  for (size_t index = 0; index < size; index++) {
    if (!k22_write_direct(device, address + (uint32_t)index, 1,
                          CORTEX_M4_ACCESS_DEBUG, input[index])) {
      return false;
    }
  }
  return true;
}
