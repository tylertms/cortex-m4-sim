#include "k22_register_manifest.h"

#include <string.h>

#include "registers/mk22f12810.inc"
#include "registers/mk22f25612.inc"
#include "registers/mk22f51212.inc"
#include "registers/mk22f12.inc"

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

static const K22RegisterManifest manifests[K22_PROFILE_COUNT] = {
    {(K22ProfileId)0, mk22f12810_registers, COUNT(mk22f12810_registers), mk22f12810_peripherals,
     COUNT(mk22f12810_peripherals), UINT64_C(0xf61e2e5f714e55ac), UINT64_C(0x8b036d005f6d0028)},
    {(K22ProfileId)1, mk22f25612_registers, COUNT(mk22f25612_registers), mk22f25612_peripherals,
     COUNT(mk22f25612_peripherals), UINT64_C(0x3c6c83a0b34beb78), UINT64_C(0x79118955ad2407ab)},
    {(K22ProfileId)2, mk22f25612_registers, COUNT(mk22f25612_registers), mk22f25612_peripherals,
     COUNT(mk22f25612_peripherals), UINT64_C(0x3c6c83a0b34beb78), UINT64_C(0x79118955ad2407ab)},
    {(K22ProfileId)3, mk22f51212_registers, COUNT(mk22f51212_registers), mk22f51212_peripherals,
     COUNT(mk22f51212_peripherals), UINT64_C(0x888f32dea7d49bde), UINT64_C(0xcb5f795ba1e30ea8)},
    {(K22ProfileId)4, mk22f12_registers, COUNT(mk22f12_registers), mk22f12_peripherals,
     COUNT(mk22f12_peripherals), UINT64_C(0xfa25597b9f6fc31f), UINT64_C(0x640ec2eadda02f54)},
    {(K22ProfileId)5, mk22f12_registers, COUNT(mk22f12_registers), mk22f12_peripherals,
     COUNT(mk22f12_peripherals), UINT64_C(0xfa25597b9f6fc31f), UINT64_C(0x640ec2eadda02f54)},
};

const K22RegisterManifest* k22_register_manifest_get(K22ProfileId profile) {
    if (profile < 0 || profile >= K22_PROFILE_COUNT)
        return NULL;
    return &manifests[profile];
}

const K22RegisterDescriptor* k22_register_manifest_lookup(K22ProfileId profile, uint32_t address,
                                                          uint8_t width) {
    const K22RegisterManifest* manifest = k22_register_manifest_get(profile);
    if (manifest == NULL || (width != 8u && width != 16u && width != 32u))
        return NULL;
    size_t lower = 0;
    size_t upper = manifest->register_count;
    while (lower < upper) {
        size_t middle = lower + (upper - lower) / 2u;
        const K22RegisterDescriptor* descriptor = &manifest->registers[middle];
        if (descriptor->address < address ||
            (descriptor->address == address && descriptor->width < width))
            lower = middle + 1u;
        else
            upper = middle;
    }
    if (lower == manifest->register_count)
        return NULL;
    const K22RegisterDescriptor* descriptor = &manifest->registers[lower];
    if (descriptor->address != address || descriptor->width != width)
        return NULL;
    return descriptor;
}

bool k22_register_manifest_reset(K22ProfileId profile, uint32_t address, uint8_t width,
                                 uint32_t* value, uint32_t* mask) {
    const K22RegisterDescriptor* descriptor = k22_register_manifest_lookup(profile, address, width);
    if (descriptor == NULL || value == NULL || mask == NULL)
        return false;
    *value = descriptor->reset_value;
    *mask = descriptor->reset_mask;
    return true;
}

bool k22_register_manifest_has_peripheral(K22ProfileId profile, const char* name) {
    const K22RegisterManifest* manifest = k22_register_manifest_get(profile);
    if (manifest == NULL || name == NULL)
        return false;
    for (size_t index = 0; index < manifest->peripheral_count; index++) {
        if (strcmp(manifest->peripheral_names[index], name) == 0)
            return true;
    }
    return false;
}

const char* k22_register_manifest_peripheral_name(const K22RegisterManifest* manifest,
                                                  uint16_t index) {
    if (manifest == NULL || index >= manifest->peripheral_count)
        return NULL;
    return manifest->peripheral_names[index];
}
