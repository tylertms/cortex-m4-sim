#include "kinetis_k22.h"

#include <stdint.h>

#include "test.h"

int main(void) {
    TestState state = {0};
    KinetisK22* device = kinetis_k22_create(kinetis_k22_default_configuration());
    TEST_EXPECT(&state, device != NULL);
    const uint32_t flash_value = 0x12345678u;
    TEST_EXPECT(&state, kinetis_k22_load(device, 0x100, &flash_value, 4));
    uint32_t value = 0;
    TEST_EXPECT(&state, kinetis_k22_read(device, 0x100, &value, 4));
    TEST_EXPECT(&state, value == flash_value);
    const uint32_t ram_value = 0xa55ac33cu;
    TEST_EXPECT(&state, kinetis_k22_write(device, 0x20000000u, &ram_value, 4));
    TEST_EXPECT(&state, kinetis_k22_read(device, 0x20000000u, &value, 4));
    TEST_EXPECT(&state, value == ram_value);
    const uint8_t zero = 0;
    TEST_EXPECT(&state, !kinetis_k22_write(device, 0x40000001u, &zero, 1));
    const uint32_t bit_alias = 0x42000000u + 0x000ff000u * 32u + 3u * 4u;
    TEST_EXPECT(&state, cortex_m4_write_memory(kinetis_k22_cpu(device), bit_alias, 4, 1));
    uint8_t byte = 0;
    TEST_EXPECT(&state, kinetis_k22_read(device, 0x400ff000u, &byte, 1));
    TEST_EXPECT(&state, byte == 8u);
    TEST_EXPECT(&state,
                cortex_m4_read_memory(kinetis_k22_cpu(device), bit_alias, 4, &value));
    TEST_EXPECT(&state, value == 1u);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
