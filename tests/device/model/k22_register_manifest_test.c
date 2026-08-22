#include "k22_register_manifest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#define K22_EXPECTED_PERIPHERAL(name) name,
static const char* const mk22f12810_peripherals[] = {
#include "expected/mk22f12810_peripherals.def"
};
static const char* const mk22f25612_peripherals[] = {
#include "expected/mk22f25612_peripherals.def"
};
static const char* const mk22f51212_peripherals[] = {
#include "expected/mk22f51212_peripherals.def"
};
static const char* const mk22f12_peripherals[] = {
#include "expected/mk22f12_peripherals.def"
};
#undef K22_EXPECTED_PERIPHERAL

#define K22_EXPECTED_REGISTER(address, reset_value, reset_mask, implemented_mask, read_mask,       \
                              write_mask, w1c_mask, peripheral_index, width, access)               \
    {address,  reset_value,      reset_mask, implemented_mask,         read_mask, write_mask,      \
     w1c_mask, peripheral_index, width,      (K22RegisterAccess)access},
static const K22RegisterDescriptor mk22f12810_registers[] = {
#include "expected/mk22f12810_registers.def"
};
static const K22RegisterDescriptor mk22f25612_registers[] = {
#include "expected/mk22f25612_registers.def"
};
static const K22RegisterDescriptor mk22f51212_registers[] = {
#include "expected/mk22f51212_registers.def"
};
static const K22RegisterDescriptor mk22f12_registers[] = {
#include "expected/mk22f12_registers.def"
};
#undef K22_EXPECTED_REGISTER

typedef struct {
    const K22RegisterDescriptor* registers;
    size_t register_count;
    const char* const* peripherals;
    size_t peripheral_count;
    uint64_t register_digest;
    uint64_t peripheral_digest;
} ExpectedManifest;

#define K22_EXPECTED_MANIFEST(family, register_count, peripheral_count, register_digest,           \
                              peripheral_digest)                                                   \
    {family##_registers, register_count,  family##_peripherals,                                    \
     peripheral_count,   register_digest, peripheral_digest},
static const ExpectedManifest expected_manifests[] = {
#include "expected/k22_register_manifest_constants.def"
};
#undef K22_EXPECTED_MANIFEST

static uint64_t hash_byte(uint64_t hash, uint8_t value) {
    return (hash ^ value) * UINT64_C(0x100000001b3);
}

static uint64_t hash_integer(uint64_t hash, uint32_t value, uint8_t size) {
    for (uint8_t byte = 0; byte < size; byte++)
        hash = hash_byte(hash, (uint8_t)(value >> (byte * 8u)));
    return hash;
}

static uint64_t calculate_register_digest(const K22RegisterManifest* manifest) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (size_t index = 0; index < manifest->register_count; index++) {
        const K22RegisterDescriptor* descriptor = &manifest->registers[index];
        hash = hash_integer(hash, descriptor->address, 4u);
        hash = hash_integer(hash, descriptor->reset_value, 4u);
        hash = hash_integer(hash, descriptor->reset_mask, 4u);
        hash = hash_integer(hash, descriptor->implemented_mask, 4u);
        hash = hash_integer(hash, descriptor->read_mask, 4u);
        hash = hash_integer(hash, descriptor->write_mask, 4u);
        hash = hash_integer(hash, descriptor->w1c_mask, 4u);
        hash = hash_integer(hash, descriptor->peripheral_index, 2u);
        hash = hash_integer(hash, descriptor->width, 1u);
        hash = hash_integer(hash, descriptor->access, 1u);
    }
    return hash;
}

static uint64_t calculate_peripheral_digest(const K22RegisterManifest* manifest) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (size_t index = 0; index < manifest->peripheral_count; index++) {
        const char* name = manifest->peripheral_names[index];
        do {
            hash = hash_byte(hash, (uint8_t)*name);
        } while (*name++ != '\0');
    }
    return hash;
}

static uint32_t width_mask(uint8_t width) {
    return width == 32u ? UINT32_MAX : (UINT32_C(1) << width) - 1u;
}

static void expect_descriptor(TestState* state, const K22RegisterManifest* manifest,
                              const K22RegisterDescriptor* expected) {
    const K22RegisterDescriptor* actual =
        k22_register_manifest_lookup(manifest->profile, expected->address, expected->width);
    expect(state, actual != NULL, "actual != NULL");
    expect(state, actual->address == expected->address, "actual->address == expected->address");
    expect(state, actual->reset_value == expected->reset_value,
           "actual->reset_value == expected->reset_value");
    expect(state, actual->reset_mask == expected->reset_mask,
           "actual->reset_mask == expected->reset_mask");
    expect(state, actual->implemented_mask == expected->implemented_mask,
           "actual->implemented_mask == expected->implemented_mask");
    expect(state, actual->read_mask == expected->read_mask,
           "actual->read_mask == expected->read_mask");
    expect(state, actual->write_mask == expected->write_mask,
           "actual->write_mask == expected->write_mask");
    expect(state, actual->w1c_mask == expected->w1c_mask, "actual->w1c_mask == expected->w1c_mask");
    expect(state, actual->peripheral_index == expected->peripheral_index,
           "actual->peripheral_index == expected->peripheral_index");
    expect(state, actual->width == expected->width, "actual->width == expected->width");
    expect(state, actual->access == expected->access, "actual->access == expected->access");
}

static void expect_manifest(TestState* state, K22ProfileId profile,
                            const ExpectedManifest* expected) {
    const K22RegisterManifest* manifest = k22_register_manifest_get(profile);
    expect(state, manifest != NULL, "manifest != NULL");
    expect(state, manifest->profile == profile, "manifest->profile == profile");
    expect(state, manifest->register_count == expected->register_count,
           "manifest->register_count == expected->register_count");
    expect(state, manifest->peripheral_count == expected->peripheral_count,
           "manifest->peripheral_count == expected->peripheral_count");
    expect(state, manifest->register_digest == expected->register_digest,
           "manifest->register_digest == expected->register_digest");
    expect(state, manifest->peripheral_digest == expected->peripheral_digest,
           "manifest->peripheral_digest == expected->peripheral_digest");
    const uint64_t calculated_register_digest = calculate_register_digest(manifest);
    if (calculated_register_digest != expected->register_digest) {
        fprintf(stderr, "profile %u register digest 0x%016llx, expected 0x%016llx\n",
                (unsigned)profile, (unsigned long long)calculated_register_digest,
                (unsigned long long)expected->register_digest);
    }
    expect(state, calculated_register_digest == expected->register_digest,
           "calculated_register_digest == expected->register_digest");
    expect(state, calculate_peripheral_digest(manifest) == expected->peripheral_digest,
           "calculate_peripheral_digest(manifest) == expected->peripheral_digest");

    for (size_t index = 0; index < expected->peripheral_count; index++) {
        expect(state, strcmp(manifest->peripheral_names[index], expected->peripherals[index]) == 0,
               "strcmp(manifest->peripheral_names[index], expected->peripherals[index]) == 0");
        expect(state, k22_register_manifest_has_peripheral(profile, expected->peripherals[index]),
               "k22_register_manifest_has_peripheral( profile, expected->peripherals[index])");
        expect(state,
               strcmp(k22_register_manifest_peripheral_name(manifest, index),
                      expected->peripherals[index]) == 0,
               "strcmp(k22_register_manifest_peripheral_name(manifest, index), "
               "expected->peripherals[index]) == 0");
    }

    for (size_t index = 0; index < expected->register_count; index++) {
        const K22RegisterDescriptor* descriptor = &expected->registers[index];
        expect_descriptor(state, manifest, descriptor);
        uint32_t mask = width_mask(descriptor->width);
        expect(state, (descriptor->reset_value & ~descriptor->reset_mask) == 0u,
               "(descriptor->reset_value & ~descriptor->reset_mask) == 0u");
        expect(state, (descriptor->reset_mask & ~mask) == 0u,
               "(descriptor->reset_mask & ~mask) == 0u");
        expect(state, (descriptor->implemented_mask & ~mask) == 0u,
               "(descriptor->implemented_mask & ~mask) == 0u");
        expect(state, (descriptor->read_mask & ~descriptor->implemented_mask) == 0u,
               "(descriptor->read_mask & ~descriptor->implemented_mask) == 0u");
        expect(state, (descriptor->write_mask & ~descriptor->implemented_mask) == 0u,
               "(descriptor->write_mask & ~descriptor->implemented_mask) == 0u");
        expect(state, (descriptor->w1c_mask & ~descriptor->write_mask) == 0u,
               "(descriptor->w1c_mask & ~descriptor->write_mask) == 0u");
        K22RegisterAccess access = (descriptor->read_mask != 0u ? K22_REGISTER_ACCESS_READ : 0) |
                                   (descriptor->write_mask != 0u ? K22_REGISTER_ACCESS_WRITE : 0);
        expect(state, descriptor->access == access, "descriptor->access == access");
        expect(state, descriptor->peripheral_index < expected->peripheral_count,
               "descriptor->peripheral_index < expected->peripheral_count");
        if (index != 0u) {
            const K22RegisterDescriptor* previous = &expected->registers[index - 1u];
            expect(state,
                   previous->address < descriptor->address ||
                       (previous->address == descriptor->address &&
                        previous->width < descriptor->width),
                   "previous->address < descriptor->address || (previous->address == "
                   "descriptor->address && previous->width < descriptor->width)");
        }
    }

    uint32_t value = UINT32_C(0x55555555);
    uint32_t mask = UINT32_C(0xaaaaaaaa);
    const K22RegisterDescriptor* first = &expected->registers[0];
    expect(state, k22_register_manifest_reset(profile, first->address, first->width, &value, &mask),
           "k22_register_manifest_reset(profile, first->address, first->width, &value, "
           "&mask)");
    expect(state, value == first->reset_value, "value == first->reset_value");
    expect(state, mask == first->reset_mask, "mask == first->reset_mask");
    expect(state, k22_register_manifest_lookup(profile, first->address, 0u) == NULL,
           "k22_register_manifest_lookup(profile, first->address, 0u) == NULL");
    expect(state, k22_register_manifest_lookup(profile, first->address, 64u) == NULL,
           "k22_register_manifest_lookup(profile, first->address, 64u) == NULL");
    expect(state, !k22_register_manifest_reset(profile, first->address, first->width, NULL, &mask),
           "!k22_register_manifest_reset(profile, first->address, first->width, NULL, &mask)");
    expect(state, !k22_register_manifest_reset(profile, first->address, first->width, &value, NULL),
           "!k22_register_manifest_reset(profile, first->address, first->width, &value, "
           "NULL)");
    expect(state,
           k22_register_manifest_peripheral_name(manifest, manifest->peripheral_count) == NULL,
           "k22_register_manifest_peripheral_name( manifest, manifest->peripheral_count) "
           "== NULL");
    expect(state, !k22_register_manifest_has_peripheral(profile, "UNKNOWN"),
           "!k22_register_manifest_has_peripheral(profile, \"UNKNOWN\")");
}

int main(void) {
    TestState state = {0};
    expect_manifest(&state, K22_PROFILE_MK22F12810, &expected_manifests[0]);
    expect_manifest(&state, K22_PROFILE_MK22FN12812, &expected_manifests[1]);
    expect_manifest(&state, K22_PROFILE_MK22FN25612, &expected_manifests[1]);
    expect_manifest(&state, K22_PROFILE_MK22FN51212, &expected_manifests[2]);
    expect_manifest(&state, K22_PROFILE_MK22FN1M012, &expected_manifests[3]);
    expect_manifest(&state, K22_PROFILE_MK22FX51212, &expected_manifests[3]);
    expect(&state, k22_register_manifest_get((K22ProfileId)-1) == NULL,
           "k22_register_manifest_get((K22ProfileId)-1) == NULL");
    expect(&state, k22_register_manifest_get(K22_PROFILE_COUNT) == NULL,
           "k22_register_manifest_get(K22_PROFILE_COUNT) == NULL");
    expect(&state, k22_register_manifest_lookup(K22_PROFILE_COUNT, 0x40000000u, 32u) == NULL,
           "k22_register_manifest_lookup(K22_PROFILE_COUNT, 0x40000000u, 32u) == NULL");
    expect(&state, !k22_register_manifest_has_peripheral(K22_PROFILE_COUNT, "SIM"),
           "!k22_register_manifest_has_peripheral(K22_PROFILE_COUNT, \"SIM\")");
    expect(&state, !k22_register_manifest_has_peripheral(K22_PROFILE_MK22F12810, NULL),
           "!k22_register_manifest_has_peripheral(K22_PROFILE_MK22F12810, NULL)");
    expect(&state, k22_register_manifest_peripheral_name(NULL, 0u) == NULL,
           "k22_register_manifest_peripheral_name(NULL, 0u) == NULL");
    return test_finish(&state);
}
