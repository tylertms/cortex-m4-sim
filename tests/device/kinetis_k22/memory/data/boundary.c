#include "device/kinetis_k22/memory/data/internal.h"

#include <stdlib.h>

void k22_data_test_test_api_boundaries(TestState* state) {
    static TestBus first_bus;
    static TestBus second_bus;
    K22DataBus empty_bus = {0};
    K22Data* first = k22_data_test_create(state, &first_bus, K22_PROFILE_MK22FX51212);
    K22Data* second = k22_data_test_create(state, &second_bus, K22_PROFILE_MK22F12810);
    uint32_t word = 0u;
    uint16_t dac = 0u;
    uint8_t configuration[16] = {0};

    expect(state, k22_data_create(NULL, empty_bus) == NULL, "null profile is rejected");
    k22_data_destroy(NULL);
    k22_data_reset(NULL);
    expect(state, !k22_data_copy(NULL, first) && !k22_data_copy(first, NULL) &&
                      !k22_data_copy(first, second),
           "incompatible data copies are rejected");
    expect(state, !k22_data_read(NULL, 0u, 1u, &word) &&
                      !k22_data_read(first, 0u, 3u, &word) &&
                      !k22_data_read(first, 0u, 1u, NULL),
           "invalid data reads are rejected");
    expect(state, !k22_data_write(NULL, 0u, 1u, 0u) &&
                      !k22_data_write(first, 0u, 3u, 0u) &&
                      !k22_data_write(first, 0x400u, 1u, 0u),
           "invalid and protected data writes are rejected");
    expect(state, !k22_data_get_dac_output(NULL, 0u, &dac) &&
                      !k22_data_get_dac_output(first, UINT8_MAX, &dac) &&
                      !k22_data_get_dac_output(first, 0u, NULL),
           "invalid DAC output requests are rejected");
    k22_data_dac_trigger(NULL, 0u);
    k22_data_dac_trigger(first, UINT8_MAX);
    k22_data_dac_trigger(first, 0u);
    expect(state, !k22_data_set_flash_configuration(NULL, configuration, sizeof(configuration)) &&
                      !k22_data_set_flash_configuration(first, NULL, sizeof(configuration)) &&
                      !k22_data_set_flash_configuration(first, configuration, 15u),
           "invalid flash configurations are rejected");
    expect(state, !k22_data_flash_read(NULL, false, 0u, 1u),
           "null flash collision request is rejected");
    k22_data_advance(NULL, 1u);
    k22_data_advance(first, 0u);

    first->flash_data_ifr[0x3fcu] = 0xaau;
    k22_data_test_clear_flash_status(state, first);
    k22_data_test_flash_command(state, first, 0x08u, 0x800000u, 2000u);
    free(first->flexram);
    first->flexram = NULL;
    k22_data_test_clear_flash_status(state, first);
    k22_data_test_flash_command_without_address(state, first, 0x81u, 40u);

    k22_data_destroy(second);
    k22_data_destroy(first);
}
