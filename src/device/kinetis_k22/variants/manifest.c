#include "device/kinetis_k22/variants/manifest.h"

#include <string.h>

#include "device/kinetis_k22/variants/register_data.h"

const K22RegisterManifest* k22_register_manifest_get(K22ProfileId profile) {
    switch (profile) {
    case K22_PROFILE_MK22F12810:
        return k22_mk22f12810_register_manifest();
    case K22_PROFILE_MK22FN12812:
        return k22_mk22fn12812_register_manifest();
    case K22_PROFILE_MK22FN25612:
        return k22_mk22fn25612_register_manifest();
    case K22_PROFILE_MK22FN51212:
        return k22_mk22fn51212_register_manifest();
    case K22_PROFILE_MK22FN1M012:
        return k22_mk22fn1m012_register_manifest();
    case K22_PROFILE_MK22FX51212:
        return k22_mk22fx51212_register_manifest();
    default:
        return NULL;
    }
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
