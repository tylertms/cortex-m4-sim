#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "test.h"

int main(void) {
    TestState state = {0};
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096;
    configuration.sram_size = 65536;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(&state, device != NULL);
    const uint32_t vectors[2] = {0x20001000u, 0x00000101u};
    TEST_EXPECT(&state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)));
    TEST_EXPECT(&state, kinetis_k22_reset(device));
    CortexM4* cpu = kinetis_k22_cpu(device);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 13) == 0x20001000u);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 14) == 0xffffffffu);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 15) == 0x00000100u);
    TEST_EXPECT(&state, cortex_m4_get_xpsr(cpu) == 0x01000000u);
    TEST_EXPECT(&state, cortex_m4_get_stop(cpu) == CORTEX_M4_STOP_RUNNING);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
