#ifndef CORTEX_M4_FIRMWARE_IMAGE_H
#define CORTEX_M4_FIRMWARE_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "cortex_m4_sim/kinetis_k22.h"

bool firmware_image_load_elf(KinetisK22* device, const char* path, uint32_t* entry_address);

#endif
