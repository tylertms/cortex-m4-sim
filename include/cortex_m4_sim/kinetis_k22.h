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

KinetisK22Configuration kinetis_k22_default_configuration(void);
KinetisK22 *kinetis_k22_create(KinetisK22Configuration configuration);
void kinetis_k22_destroy(KinetisK22 *device);
CortexM4 *kinetis_k22_cpu(KinetisK22 *device);
const CortexM4 *kinetis_k22_cpu_const(const KinetisK22 *device);
bool kinetis_k22_reset(KinetisK22 *device);
bool kinetis_k22_load(KinetisK22 *device, uint32_t address, const void *data,
                      size_t size);
bool kinetis_k22_read(const KinetisK22 *device, uint32_t address, void *data,
                      size_t size);
bool kinetis_k22_write(KinetisK22 *device, uint32_t address, const void *data,
                       size_t size);

#endif
