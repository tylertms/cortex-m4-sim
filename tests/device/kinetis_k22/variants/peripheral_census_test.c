#include "kinetis_k22.h"

#include <stdint.h>

#include "device/kinetis_k22/internal.h"
#include "test.h"

typedef struct {
    uint64_t reads;
    uint64_t writes;
    uint64_t fingerprint;
} Census;

static uint64_t mix(uint64_t fingerprint, uint32_t value) {
    return (fingerprint ^ value) * UINT64_C(1099511628211);
}

static void record_read(Census* census, bool accepted, uint32_t value) {
    census->reads += accepted;
    census->fingerprint = mix(census->fingerprint, accepted);
    census->fingerprint = mix(census->fingerprint, value);
}

static void record_write(Census* census, bool accepted) {
    census->writes += accepted;
    census->fingerprint = mix(census->fingerprint, accepted);
}

static void exercise_access(Census* census, KinetisK22* device, uint32_t address, uint8_t size,
                            uint32_t value) {
    k22_timing_reset(&device->timing, 0x80u, 0u);
    k22_io_reset(&device->io);
    k22_serial_reset(&device->serial);
    k22_data_reset(device->data);
    uint32_t read_value = UINT32_MAX;
    record_read(census, k22_timing_read(&device->timing, address, size, &read_value), read_value);
    record_write(census, k22_timing_write(&device->timing, address, size, value));
    read_value = UINT32_MAX;
    record_read(census, k22_io_read(&device->io, address, size, &read_value), read_value);
    record_write(census, k22_io_write(&device->io, address, size, value));
    read_value = UINT32_MAX;
    record_read(census, k22_serial_read(&device->serial, address, size, &read_value), read_value);
    record_write(census, k22_serial_write(&device->serial, address, size, value));
    read_value = UINT32_MAX;
    record_read(census, k22_data_read(device->data, address, size, &read_value), read_value);
    record_write(census, k22_data_write(device->data, address, size, value));
}

static Census census_peripherals(KinetisK22* device) {
    static const uint8_t sizes[] = {1u, 2u, 4u};
    static const uint32_t values[] = {0u, 1u, 0x55555555u, 0xaaaaaaaau, UINT32_MAX};
    Census census = {0u, 0u, UINT64_C(14695981039346656037)};
    for (size_t register_index = 0u; register_index < device->manifest->register_count;
         register_index++) {
        const K22RegisterDescriptor* descriptor = &device->manifest->registers[register_index];
        if (descriptor->address < K22_PERIPHERAL_BASE) {
            continue;
        }
        for (size_t size_index = 0u; size_index < sizeof(sizes) / sizeof(sizes[0]); size_index++) {
            for (size_t value_index = 0u; value_index < sizeof(values) / sizeof(values[0]);
                 value_index++) {
                exercise_access(&census, device, descriptor->address, sizes[size_index],
                                values[value_index]);
            }
        }
    }
    return census;
}

int main(void) {
    TestState state = {0};
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.profile = KINETIS_K22_PROFILE_MK22FN1M012;
    configuration.package = KINETIS_K22_PACKAGE_LQ_144_LQFP;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(&state, device != NULL, "device != NULL");
    const Census census = census_peripherals(device);
    expect(&state,
           census.reads == 6610u && census.writes == 6420u &&
               census.fingerprint == UINT64_C(10481145223000664123),
           "peripheral census matches");
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
