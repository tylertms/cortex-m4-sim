#ifndef CORTEX_M4_FIRMWARE_IMAGE_H
#define CORTEX_M4_FIRMWARE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cortex_m4_sim/kinetis_k22.h"

bool firmware_image_load_elf_data(KinetisK22* device, const void* data, size_t size,
                                  uint32_t* entry_address);
bool firmware_image_load_elf(KinetisK22* device, const char* path, uint32_t* entry_address);
bool firmware_image_load_binary(KinetisK22* device, const char* path,
                                uint32_t load_address);

#endif
