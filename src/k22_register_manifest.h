#ifndef CORTEX_M4_K22_REGISTER_MANIFEST_H
#define CORTEX_M4_K22_REGISTER_MANIFEST_H

#include "k22_profile.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    K22_REGISTER_ACCESS_NONE = 0,
    K22_REGISTER_ACCESS_READ = 1,
    K22_REGISTER_ACCESS_WRITE = 2,
    K22_REGISTER_ACCESS_READ_WRITE = 3,
} K22RegisterAccess;

typedef struct {
    uint32_t address;
    uint32_t reset_value;
    uint32_t reset_mask;
    uint32_t implemented_mask;
    uint32_t read_mask;
    uint32_t write_mask;
    uint32_t w1c_mask;
    uint16_t peripheral_index;
    uint8_t width;
    K22RegisterAccess access;
} K22RegisterDescriptor;

typedef struct {
    K22ProfileId profile;
    const K22RegisterDescriptor* registers;
    size_t register_count;
    const char* const* peripheral_names;
    size_t peripheral_count;
    uint64_t register_digest;
    uint64_t peripheral_digest;
} K22RegisterManifest;

const K22RegisterManifest* k22_register_manifest_get(K22ProfileId profile);
const K22RegisterDescriptor* k22_register_manifest_lookup(K22ProfileId profile, uint32_t address,
                                                          uint8_t width);
bool k22_register_manifest_reset(K22ProfileId profile, uint32_t address, uint8_t width,
                                 uint32_t* value, uint32_t* mask);
bool k22_register_manifest_has_peripheral(K22ProfileId profile, const char* name);
const char* k22_register_manifest_peripheral_name(const K22RegisterManifest* manifest,
                                                  uint16_t index);

#endif
