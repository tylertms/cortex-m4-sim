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
    uint32_t vectors[17] = {0};
    vectors[0] = 0x20001000u;
    vectors[1] = 0x00000101u;
    vectors[16] = 0x00000201u;
    const uint16_t main_program[] = {0xbf00u, 0xbe00u};
    const uint16_t handler[] = {0x2055u, 0x4770u};
    expect(&state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0, vectors, sizeof(vectors))");
    expect(&state, kinetis_k22_load(device, 0x100, main_program, sizeof(main_program)),
           "kinetis_k22_load(device, 0x100, main_program, sizeof(main_program))");
    expect(&state, kinetis_k22_load(device, 0x200, handler, sizeof(handler)),
           "kinetis_k22_load(device, 0x200, handler, sizeof(handler))");
    expect(&state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    CortexM4* cpu = kinetis_k22_cpu(device);
    expect(&state, cortex_m4_write_memory(cpu, 0xe000e100u, 4, 1),
           "cortex_m4_write_memory(cpu, 0xe000e100u, 4, 1)");
    cortex_m4_set_irq(cpu, 0, true);
    cortex_m4_step(cpu);
    expect(&state, cortex_m4_get_register(cpu, 0) == 0x55u,
           "cortex_m4_get_register(cpu, 0) == 0x55u");
    expect(&state, cortex_m4_get_irq_active(cpu, 0), "cortex_m4_get_irq_active(cpu, 0)");
    cortex_m4_step(cpu);
    expect(&state, cortex_m4_get_register(cpu, 15) == 0x100u,
           "cortex_m4_get_register(cpu, 15) == 0x100u");
    expect(&state, !cortex_m4_get_irq_active(cpu, 0), "!cortex_m4_get_irq_active(cpu, 0)");
    cortex_m4_step(cpu);
    expect(&state, cortex_m4_get_register(cpu, 15) == 0x102u,
           "cortex_m4_get_register(cpu, 15) == 0x102u");

    const uint16_t stack_handler[] = {0xb500u, 0x2055u, 0xbd00u};
    expect(&state, kinetis_k22_load(device, 0x200, stack_handler, sizeof(stack_handler)),
           "kinetis_k22_load(device, 0x200, stack_handler, sizeof(stack_handler))");
    expect(&state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    expect(&state, cortex_m4_write_memory(cpu, 0xe000e100u, 4, 1),
           "cortex_m4_write_memory(cpu, 0xe000e100u, 4, 1)");
    cortex_m4_set_irq(cpu, 0, true);
    cortex_m4_step(cpu);
    cortex_m4_step(cpu);
    cortex_m4_step(cpu);
    expect(&state, cortex_m4_get_register(cpu, 15) == 0x100u,
           "cortex_m4_get_register(cpu, 15) == 0x100u");
    expect(&state, !cortex_m4_get_irq_active(cpu, 0), "!cortex_m4_get_irq_active(cpu, 0)");
    expect(&state, cortex_m4_get_fault_status(cpu) == 0,
           "cortex_m4_get_fault_status(cpu) == 0");
    expect(&state, kinetis_k22_load(device, 0x200, handler, sizeof(handler)),
           "kinetis_k22_load(device, 0x200, handler, sizeof(handler))");

    const uint16_t it_program[] = {0x2000u, 0x2801u, 0xbf08u, 0x3101u, 0xbe00u};
    expect(&state, kinetis_k22_load(device, 0x100, it_program, sizeof(it_program)),
           "kinetis_k22_load(device, 0x100, it_program, sizeof(it_program))");
    expect(&state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    expect(&state, cortex_m4_write_memory(cpu, 0xe000e100u, 4, 1),
           "cortex_m4_write_memory(cpu, 0xe000e100u, 4, 1)");
    test_connect_debugger(&state, cpu);
    cortex_m4_step(cpu);
    cortex_m4_step(cpu);
    cortex_m4_step(cpu);
    expect(&state, (cortex_m4_get_xpsr(cpu) & 0x0600fc00u) == 0x00000800u,
           "(cortex_m4_get_xpsr(cpu) & 0x0600fc00u) == 0x00000800u");
    cortex_m4_set_irq(cpu, 0, true);
    cortex_m4_step(cpu);
    expect(&state, cortex_m4_get_register(cpu, 0) == 0x55u,
           "cortex_m4_get_register(cpu, 0) == 0x55u");
    expect(&state, (cortex_m4_get_xpsr(cpu) & 0x0600fc00u) == 0,
           "(cortex_m4_get_xpsr(cpu) & 0x0600fc00u) == 0");
    uint32_t stacked_xpsr = 0;
    expect(&state, cortex_m4_read_memory(cpu, 0x20000ffcu, 4, &stacked_xpsr),
           "cortex_m4_read_memory(cpu, 0x20000ffcu, 4, &stacked_xpsr)");
    expect(&state, (stacked_xpsr & 0x0600fc00u) == 0x00000800u,
           "(stacked_xpsr & 0x0600fc00u) == 0x00000800u");
    cortex_m4_step(cpu);
    expect(&state, (cortex_m4_get_xpsr(cpu) & 0x0600fc00u) == 0x00000800u,
           "(cortex_m4_get_xpsr(cpu) & 0x0600fc00u) == 0x00000800u");
    cortex_m4_step(cpu);
    expect(&state, cortex_m4_get_register(cpu, 1) == 0,
           "cortex_m4_get_register(cpu, 1) == 0");
    expect(&state, (cortex_m4_get_xpsr(cpu) & 0x0600fc00u) == 0,
           "(cortex_m4_get_xpsr(cpu) & 0x0600fc00u) == 0");
    cortex_m4_step(cpu);
    expect(&state, cortex_m4_get_stop(cpu) == CORTEX_M4_STOP_BREAKPOINT,
           "cortex_m4_get_stop(cpu) == CORTEX_M4_STOP_BREAKPOINT");
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
