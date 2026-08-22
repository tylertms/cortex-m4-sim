#include "device/kinetis_k22/internal.h"

#include <stdint.h>

#include "test.h"

typedef struct {
    uint64_t accepted;
    uint64_t fingerprint;
} Census;

static void record(Census* census, bool accepted, uint32_t value) {
    census->accepted += accepted;
    census->fingerprint = (census->fingerprint ^ accepted) * UINT64_C(1099511628211);
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static Census census_bit_band(KinetisK22* device) {
    Census census = {0u, UINT64_C(14695981039346656037)};
    CortexM4Bus* bus = &device->cpu->bus;
    for (size_t index = 0u; index < device->manifest->register_count; index++) {
        const K22RegisterDescriptor* descriptor = &device->manifest->registers[index];
        for (uint8_t bit = 0u; bit < descriptor->width; bit++) {
            const uint32_t byte_address = descriptor->address + bit / 8u;
            const uint32_t alias = K22_BIT_BAND_BASE + (byte_address - K22_PERIPHERAL_BASE) * 32u +
                                   (uint32_t)(bit & 7u) * 4u;
            uint32_t value = UINT32_MAX;
            const bool read = bus->read(bus->context, alias, 4u, CORTEX_M4_ACCESS_DEBUG, &value);
            record(&census, read, value);
            record(&census,
                   bus->write(bus->context, alias, 4u, CORTEX_M4_ACCESS_DEBUG,
                              (uint32_t)(index + bit) & 1u),
                   descriptor->address);
        }
    }
    uint32_t value = 0u;
    record(&census,
           bus->read(bus->context, K22_BIT_BAND_BASE + 1u, 4u, CORTEX_M4_ACCESS_DEBUG, &value),
           value);
    record(&census,
           bus->write(bus->context, K22_BIT_BAND_BASE + 1u, 4u, CORTEX_M4_ACCESS_DEBUG, 0u), value);
    record(&census,
           bus->read(bus->context, K22_BIT_BAND_BASE + K22_BIT_BAND_SIZE - 4u, 4u,
                     CORTEX_M4_ACCESS_DEBUG, &value),
           value);
    return census;
}

static void test_boundaries(TestState* state, KinetisK22* device) {
    uint32_t value = 0u;
    expect(state, !kinetis_k22_memory_read(NULL, 0u, 4u, CORTEX_M4_ACCESS_DEBUG, &value),
           "null memory read rejected");
    expect(state, !kinetis_k22_memory_read(device, 0u, 4u, CORTEX_M4_ACCESS_DEBUG, NULL),
           "null memory read value rejected");
    expect(state, !kinetis_k22_memory_read(device, 0u, 3u, CORTEX_M4_ACCESS_DEBUG, &value),
           "invalid memory read size rejected");
    expect(state, !kinetis_k22_memory_write(NULL, 0u, 4u, CORTEX_M4_ACCESS_DEBUG, value),
           "null memory write rejected");
    expect(state, !kinetis_k22_memory_write(device, 0u, 3u, CORTEX_M4_ACCESS_DEBUG, value),
           "invalid memory write size rejected");
    expect(state, !kinetis_k22_flash_controller_write(NULL, 0u, 4u, value),
           "null flash controller write rejected");
    expect(state, !kinetis_k22_flash_controller_write(device, 0u, 3u, value),
           "invalid flash controller write rejected");
    expect(state, !kinetis_k22_flexbus_attach(NULL, 0u, &value, sizeof(value), false),
           "null flexbus attach rejected");
    expect(state, !kinetis_k22_flexbus_attach(device, 0u, NULL, sizeof(value), false),
           "null flexbus data rejected");
    expect(state, !kinetis_k22_flexbus_attach(device, 0u, &value, 0u, false),
           "empty flexbus data rejected");
    expect(state, !kinetis_k22_flexbus_attach(device, UINT32_MAX, &value, sizeof(value), false),
           "overflowing flexbus range rejected");
    expect(state, !kinetis_k22_flexbus_read(NULL, 0u, &value, sizeof(value)),
           "null flexbus read rejected");
    expect(state, !kinetis_k22_flexbus_read(device, 0u, NULL, sizeof(value)),
           "null flexbus output rejected");
    kinetis_k22_flexbus_detach(NULL);
    kinetis_k22_advance(NULL, 1u);
    kinetis_k22_advance(device, 0u);
    kinetis_k22_watchdog_advance(NULL, 1u);
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = kinetis_k22_create(kinetis_k22_default_configuration());
    expect(&state, device != NULL, "device != NULL");
    const uint32_t flash_value = 0x12345678u;
    expect(&state, kinetis_k22_load(device, 0x100, &flash_value, 4),
           "kinetis_k22_load(device, 0x100, &flash_value, 4)");
    uint32_t value = 0;
    expect(&state, kinetis_k22_read(device, 0x100, &value, 4),
           "kinetis_k22_read(device, 0x100, &value, 4)");
    expect(&state, value == flash_value, "value == flash_value");
    const uint32_t ram_value = 0xa55ac33cu;
    expect(&state, kinetis_k22_write(device, 0x20000000u, &ram_value, 4),
           "kinetis_k22_write(device, 0x20000000u, &ram_value, 4)");
    expect(&state, kinetis_k22_read(device, 0x20000000u, &value, 4),
           "kinetis_k22_read(device, 0x20000000u, &value, 4)");
    expect(&state, value == ram_value, "value == ram_value");
    const uint8_t zero = 0;
    expect(&state, !kinetis_k22_write(device, 0x40000001u, &zero, 1),
           "!kinetis_k22_write(device, 0x40000001u, &zero, 1)");
    const uint32_t bit_alias = 0x42000000u + 0x000ff000u * 32u + 3u * 4u;
    expect(&state, cortex_m4_write_memory(kinetis_k22_cpu(device), bit_alias, 4, 1),
           "cortex_m4_write_memory(kinetis_k22_cpu(device), bit_alias, 4, 1)");
    uint8_t byte = 0;
    expect(&state, kinetis_k22_read(device, 0x400ff000u, &byte, 1),
           "kinetis_k22_read(device, 0x400ff000u, &byte, 1)");
    expect(&state, byte == 8u, "byte == 8u");
    expect(&state, cortex_m4_read_memory(kinetis_k22_cpu(device), bit_alias, 4, &value),
           "cortex_m4_read_memory(kinetis_k22_cpu(device), bit_alias, 4, &value)");
    expect(&state, value == 1u, "value == 1u");
    const Census census = census_bit_band(device);
    expect(&state, census.accepted == 33430u && census.fingerprint == UINT64_C(5430620924546023010),
           "bit-band census matches");
    test_boundaries(&state, device);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
