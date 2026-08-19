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
    const uint16_t program[] = {0xbf40u, 0xbf20u, 0x2001u, 0xbf30u, 0x2002u, 0xbe00u};
    TEST_EXPECT(&state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)));
    TEST_EXPECT(&state, kinetis_k22_load(device, 0x100, program, sizeof(program)));
    TEST_EXPECT(&state, kinetis_k22_reset(device));
    CortexM4* cpu = kinetis_k22_cpu(device);

    TEST_EXPECT(&state, cortex_m4_set_breakpoint(cpu, 0, 0x100u, true));
    TEST_EXPECT(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_BREAKPOINT);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 15) == 0x100u);
    TEST_EXPECT(&state, cortex_m4_set_breakpoint(cpu, 0, 0x100u, false));
    CortexM4Result result = cortex_m4_run(cpu, (CortexM4RunLimits){4, 20});
    TEST_EXPECT(&state, result.stop == CORTEX_M4_STOP_LIMIT);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 0) == 1);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 15) == 0x108u);
    result = cortex_m4_step(cpu);
    TEST_EXPECT(&state, result.instructions == 4);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 15) == 0x108u);
    cortex_m4_set_irq(cpu, 5, true);
    result = cortex_m4_run(cpu, (CortexM4RunLimits){6, 20});
    TEST_EXPECT(&state, result.stop == CORTEX_M4_STOP_BREAKPOINT);
    TEST_EXPECT(&state, cortex_m4_get_register(cpu, 0) == 2);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
