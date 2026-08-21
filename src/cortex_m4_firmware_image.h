#ifndef CORTEX_M4_SIM_FIRMWARE_IMAGE_H
#define CORTEX_M4_SIM_FIRMWARE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kinetis_k22.h"

bool cortex_m4_load_elf_data(KinetisK22* device, const void* data, size_t size,
                             uint32_t* entry_address);
bool cortex_m4_load_binary_data(KinetisK22* device, const void* data, size_t size,
                                uint32_t load_address, uint32_t* entry_address);
bool cortex_m4_elf_symbol_data(const void* data, size_t size, const char* name,
                               uint32_t* address);
bool cortex_m4_load_elf(KinetisK22* device, const char* path, uint32_t* entry_address);
bool cortex_m4_load_binary(KinetisK22* device, const char* path, uint32_t load_address);

#endif
