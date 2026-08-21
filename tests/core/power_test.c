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
    uint32_t vectors[22] = {0};
    vectors[0] = 0x20001000u;
    vectors[1] = 0x00000101u;
    vectors[21] = 0x00000201u;
    const uint16_t program[] = {0xbf40u, 0xbf20u, 0x2001u, 0xbf30u, 0x2002u, 0xbe00u};
    const uint16_t handler[] = {0x4770u};
    expect(&state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0, vectors, sizeof(vectors))");
    expect(&state, kinetis_k22_load(device, 0x100, program, sizeof(program)),
           "kinetis_k22_load(device, 0x100, program, sizeof(program))");
    expect(&state, kinetis_k22_load(device, 0x200, handler, sizeof(handler)),
           "kinetis_k22_load(device, 0x200, handler, sizeof(handler))");
    expect(&state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    CortexM4* cpu = kinetis_k22_cpu(device);
    test_connect_debugger(&state, cpu);

    expect(&state, cortex_m4_set_breakpoint(cpu, 0, 0x100u, true),
           "cortex_m4_set_breakpoint(cpu, 0, 0x100u, true)");
    expect(&state, cortex_m4_step(cpu).stop == CORTEX_M4_STOP_BREAKPOINT,
           "cortex_m4_step(cpu).stop == CORTEX_M4_STOP_BREAKPOINT");
    expect(&state, cortex_m4_get_register(cpu, 15) == 0x100u,
           "cortex_m4_get_register(cpu, 15) == 0x100u");
    expect(&state, cortex_m4_set_breakpoint(cpu, 0, 0x100u, false),
           "cortex_m4_set_breakpoint(cpu, 0, 0x100u, false)");
    CortexM4Result result = cortex_m4_run(cpu, (CortexM4RunLimits){4, 20});
    expect(&state, result.stop == CORTEX_M4_STOP_LIMIT,
           "result.stop == CORTEX_M4_STOP_LIMIT");
    expect(&state, cortex_m4_get_register(cpu, 0) == 1,
           "cortex_m4_get_register(cpu, 0) == 1");
    expect(&state, cortex_m4_get_register(cpu, 15) == 0x108u,
           "cortex_m4_get_register(cpu, 15) == 0x108u");
    result = cortex_m4_step(cpu);
    expect(&state, result.instructions == 4, "result.instructions == 4");
    expect(&state, cortex_m4_get_register(cpu, 15) == 0x108u,
           "cortex_m4_get_register(cpu, 15) == 0x108u");
    expect(&state, cortex_m4_write_memory(cpu, 0xe000e100u, 4, 1u << 5),
           "cortex_m4_write_memory(cpu, 0xe000e100u, 4, 1u << 5)");
    cortex_m4_set_irq(cpu, 5, true);
    result = cortex_m4_run(cpu, (CortexM4RunLimits){8, 40});
    expect(&state, result.stop == CORTEX_M4_STOP_BREAKPOINT,
           "result.stop == CORTEX_M4_STOP_BREAKPOINT");
    expect(&state, cortex_m4_get_register(cpu, 0) == 2,
           "cortex_m4_get_register(cpu, 0) == 2");
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
