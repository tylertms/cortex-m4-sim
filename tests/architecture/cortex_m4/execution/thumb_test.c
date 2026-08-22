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
    const uint16_t program[] = {0x2001u, 0x3002u, 0x0040u, 0x2806u, 0xd100u, 0xbe00u, 0xbe01u};
    expect(&state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0, vectors, sizeof(vectors))");
    expect(&state, kinetis_k22_load(device, 0x100, program, sizeof(program)),
           "kinetis_k22_load(device, 0x100, program, sizeof(program))");
    expect(&state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    test_connect_debugger(&state, kinetis_k22_cpu(device));
    CortexM4Result result = cortex_m4_run(kinetis_k22_cpu(device), (CortexM4RunLimits){20, 100});
    expect(&state, result.stop == CORTEX_M4_STOP_BREAKPOINT,
           "result.stop == CORTEX_M4_STOP_BREAKPOINT");
    expect(&state, cortex_m4_get_register(kinetis_k22_cpu(device), 0) == 6,
           "cortex_m4_get_register(kinetis_k22_cpu(device), 0) == 6");
    expect(&state, result.instructions == 6, "result.instructions == 6");
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
