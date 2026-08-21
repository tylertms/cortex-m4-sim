#include "kinetis_k22.h"

#include <stdint.h>

#include "test.h"

int main(void) {
    TestState state = {0};
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096;
    configuration.sram_size = 65536;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(&state, device != NULL, "device != NULL");
    const uint32_t vectors[2] = {0x20001000u, 0x00000101u};
    expect(&state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0, vectors, sizeof(vectors))");
    expect(&state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    CortexM4* cpu = kinetis_k22_cpu(device);
    expect(&state, cortex_m4_get_register(cpu, 13) == 0x20001000u,
           "cortex_m4_get_register(cpu, 13) == 0x20001000u");
    expect(&state, cortex_m4_get_register(cpu, 14) == 0xffffffffu,
           "cortex_m4_get_register(cpu, 14) == 0xffffffffu");
    expect(&state, cortex_m4_get_register(cpu, 15) == 0x00000100u,
           "cortex_m4_get_register(cpu, 15) == 0x00000100u");
    expect(&state, cortex_m4_get_xpsr(cpu) == 0x01000000u,
           "cortex_m4_get_xpsr(cpu) == 0x01000000u");
    uint32_t ccr = 0;
    expect(&state, cortex_m4_read_memory(cpu, 0xe000ed14u, 4, &ccr),
           "cortex_m4_read_memory(cpu, 0xe000ed14u, 4, &ccr)");
    expect(&state, ccr == 0x00000200u, "ccr == 0x00000200u");
    expect(&state, cortex_m4_get_stop(cpu) == CORTEX_M4_STOP_RUNNING,
           "cortex_m4_get_stop(cpu) == CORTEX_M4_STOP_RUNNING");
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
