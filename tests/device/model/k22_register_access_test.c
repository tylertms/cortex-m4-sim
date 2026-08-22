#include "kinetis_k22.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "k22_package.h"
#include "k22_profile.h"
#include "k22_register_manifest.h"
#include "kinetis_k22_internal.h"
#include "test.h"

typedef struct {
    K22ProfileId profile;
    K22PackageId package;
} ProfileFixture;

static const ProfileFixture fixtures[] = {
    {K22_PROFILE_MK22F12810, K22_PACKAGE_DC_121_XFBGA},
    {K22_PROFILE_MK22FN12812, K22_PACKAGE_AH_64_WLCSP},
    {K22_PROFILE_MK22FN25612, K22_PACKAGE_DC_121_XFBGA},
    {K22_PROFILE_MK22FN51212, K22_PACKAGE_DC_121_XFBGA},
    {K22_PROFILE_MK22FN1M012, K22_PACKAGE_LQ_144_LQFP},
    {K22_PROFILE_MK22FX51212, K22_PACKAGE_LQ_144_LQFP},
};

static const uint8_t flash_configuration[16] = {
    0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
    0x81u, 0x82u, 0x84u, 0x88u, 0xfeu, 0x5au, 0xc3u, 0x3cu,
};

static uint32_t width_mask(uint8_t width) {
    return width == 32u ? UINT32_MAX : (UINT32_C(1) << width) - 1u;
}

static KinetisK22* create_device(TestState* state, const ProfileFixture* fixture) {
    const K22Profile* profile = k22_profile_get(fixture->profile);
    expect(state, profile != NULL, "profile != NULL");
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.profile = fixture->profile;
    configuration.package = (KinetisK22Package)fixture->package;
    configuration.flash_size = profile->program_flash_size;
    configuration.sram_size = (size_t)profile->sram_lower_size + profile->sram_upper_size;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(state, device != NULL, "device != NULL");
    if (device == NULL) {
        return NULL;
    }
    const uint32_t vectors[] = {
        profile->sram_upper_address + profile->sram_upper_size,
        0x00000101u,
    };
    const uint16_t program = 0xbf00u;
    expect(state, kinetis_k22_load(device, 0u, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0u, vectors, sizeof(vectors))");
    expect(state, kinetis_k22_load(device, 0x100u, &program, sizeof(program)),
           "kinetis_k22_load(device, 0x100u, &program, sizeof(program))");
    expect(state,
           kinetis_k22_load(device, 0x400u, flash_configuration, sizeof(flash_configuration)),
           "kinetis_k22_load(device, 0x400u, flash_configuration, "
           "sizeof(flash_configuration))");
    expect(state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    return device;
}

static void report_mismatch(const K22RegisterDescriptor* descriptor, const char* field,
                            uint32_t actual, uint32_t expected) {
    fprintf(stderr, "register 0x%08lx width %u has %s 0x%08lx, expected 0x%08lx\n",
            (unsigned long)descriptor->address, descriptor->width, field, (unsigned long)actual,
            (unsigned long)expected);
}

static const K22RegisterDescriptor*
widest_covering_descriptor(const K22RegisterManifest* manifest,
                           const K22RegisterDescriptor* descriptor) {
    const K22RegisterDescriptor* widest = descriptor;
    const uint32_t end = descriptor->address + descriptor->width / 8u;
    for (size_t index = 0u; index < manifest->register_count; index++) {
        const K22RegisterDescriptor* candidate = &manifest->registers[index];
        const uint32_t candidate_end = candidate->address + candidate->width / 8u;
        if (candidate->peripheral_index == descriptor->peripheral_index &&
            candidate->address <= descriptor->address && candidate_end >= end &&
            candidate->width > widest->width)
            widest = candidate;
    }
    return widest;
}

static uint32_t expected_reset_value(const K22RegisterManifest* manifest,
                                     const K22RegisterDescriptor* descriptor) {
    const uint32_t offset = descriptor->address - 0x40020000u;
    if (offset == 1u && (manifest->profile == K22_PROFILE_MK22FN1M012 ||
                         manifest->profile == K22_PROFILE_MK22FX51212))
        return 2u;
    if (offset == 2u)
        return flash_configuration[12];
    if (offset == 3u)
        return flash_configuration[13];
    if (offset >= 0x10u && offset <= 0x13u)
        return flash_configuration[8u + offset - 0x10u];
    if (offset == 0x16u)
        return flash_configuration[14];
    if (offset == 0x17u)
        return flash_configuration[15];
    const K22RegisterDescriptor* widest = widest_covering_descriptor(manifest, descriptor);
    return widest->reset_value >> ((descriptor->address - widest->address) * 8u);
}

static bool uses_flash_configuration(const K22RegisterDescriptor* descriptor) {
    const uint32_t offset = descriptor->address - 0x40020000u;
    return offset == 2u || offset == 3u || (offset >= 0x10u && offset <= 0x13u) ||
           offset == 0x16u || offset == 0x17u;
}

static void expect_reset_read(TestState* state, KinetisK22* device,
                              const K22RegisterManifest* manifest,
                              const K22RegisterDescriptor* descriptor) {
    if (descriptor->address < K22_PERIPHERAL_BASE) {
        return;
    }
    expect(state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    for (uint8_t port = 0u; port < 5u; port++) {
        for (uint8_t pin = 0u; pin < 32u; pin++) {
            kinetis_k22_gpio_drive(device, port, pin, false);
        }
    }
    const uint8_t size = (uint8_t)(descriptor->width / 8u);
    uint32_t actual = UINT32_MAX;
    const bool read = kinetis_k22_read(device, descriptor->address, &actual, size);
    const bool readable = (descriptor->access & K22_REGISTER_ACCESS_READ) != 0u;
    if (read != readable) {
        report_mismatch(descriptor, "read access", read, readable);
    }
    expect(state, read == readable, "read == readable");
    if (!readable) {
        return;
    }
    actual &= width_mask(descriptor->width);
    const K22RegisterDescriptor* widest = widest_covering_descriptor(manifest, descriptor);
    const uint32_t reset_mask =
        uses_flash_configuration(descriptor)
            ? width_mask(descriptor->width)
            : widest->reset_mask >> ((descriptor->address - widest->address) * 8u);
    const uint32_t expected =
        expected_reset_value(manifest, descriptor) & reset_mask & width_mask(descriptor->width);
    if (actual != expected) {
        report_mismatch(descriptor, "reset value", actual, expected);
    }
    expect(state, actual == expected, "actual == expected");
}

static void expect_declared_write(TestState* state, KinetisK22* device,
                                  const K22RegisterDescriptor* descriptor) {
    if (descriptor->address < K22_PERIPHERAL_BASE) {
        return;
    }
    const uint8_t size = (uint8_t)(descriptor->width / 8u);
    const uint32_t value = UINT32_MAX;
    const bool wrote = kinetis_k22_write(device, descriptor->address, &value, size);
    expect(state, wrote, "wrote");
}

static bool descriptor_covers(const K22RegisterDescriptor* descriptor, uint32_t address) {
    const uint32_t size = descriptor->width / 8u;
    return address >= descriptor->address && address < descriptor->address + size;
}

static void expect_reserved_gaps(TestState* state, KinetisK22* device,
                                 const K22RegisterManifest* manifest) {
    for (size_t index = 0u; index + 1u < manifest->register_count; index++) {
        const K22RegisterDescriptor* current = &manifest->registers[index];
        const K22RegisterDescriptor* next = &manifest->registers[index + 1u];
        const uint32_t address = current->address + current->width / 8u;
        if (address >= next->address || address < K22_PERIPHERAL_BASE ||
            descriptor_covers(current, address)) {
            continue;
        }
        K22PeripheralLocation location;
        if (!k22_profile_resolve_peripheral(device->profile, address, 1u, &location)) {
            continue;
        }
        uint8_t value = 0u;
        expect(state, !kinetis_k22_read(device, address, &value, sizeof(value)),
               "!kinetis_k22_read(device, address, &value, sizeof(value))");
        expect(state, !kinetis_k22_write(device, address, &value, sizeof(value)),
               "!kinetis_k22_write(device, address, &value, sizeof(value))");
    }
}

static void expect_profile(TestState* state, const ProfileFixture* fixture) {
    KinetisK22* device = create_device(state, fixture);
    if (device == NULL) {
        return;
    }
    const K22RegisterManifest* manifest = k22_register_manifest_get(fixture->profile);
    expect(state, manifest != NULL, "manifest != NULL");
    if (manifest != NULL) {
        for (size_t index = 0u; index < manifest->register_count; index++) {
            expect_reset_read(state, device, manifest, &manifest->registers[index]);
            expect_declared_write(state, device, &manifest->registers[index]);
        }
        expect_reserved_gaps(state, device, manifest);
    }
    kinetis_k22_destroy(device);
}

int main(void) {
    TestState state = {0};
    for (size_t index = 0u; index < sizeof(fixtures) / sizeof(fixtures[0]); index++) {
        expect_profile(&state, &fixtures[index]);
    }
    return test_finish(&state);
}
