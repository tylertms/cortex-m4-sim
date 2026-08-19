#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "test.h"

int main(void) {
  TestState state = {0};
  KinetisK22Configuration configuration = kinetis_k22_default_configuration();
  configuration.flash_size = 4096;
  configuration.sram_size = 65536;
  KinetisK22 *device = kinetis_k22_create(configuration);
  TEST_EXPECT(&state, device != NULL);
  uint32_t vectors[17] = {0};
  vectors[0] = 0x20001000u;
  vectors[1] = 0x00000101u;
  vectors[16] = 0x00000201u;
  const uint16_t main_program[] = {0xbf00u, 0xbe00u};
  const uint16_t handler[] = {0x2055u, 0x4770u};
  TEST_EXPECT(&state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)));
  TEST_EXPECT(&state, kinetis_k22_load(device, 0x100, main_program,
                                       sizeof(main_program)));
  TEST_EXPECT(&state,
              kinetis_k22_load(device, 0x200, handler, sizeof(handler)));
  TEST_EXPECT(&state, kinetis_k22_reset(device));
  CortexM4 *cpu = kinetis_k22_cpu(device);
  TEST_EXPECT(&state, cortex_m4_write_memory(cpu, 0xe000e100u, 4, 1));
  cortex_m4_set_irq(cpu, 0, true);
  cortex_m4_step(cpu);
  TEST_EXPECT(&state, cortex_m4_get_register(cpu, 0) == 0x55u);
  TEST_EXPECT(&state, cortex_m4_get_irq_active(cpu, 0));
  cortex_m4_step(cpu);
  TEST_EXPECT(&state, cortex_m4_get_register(cpu, 15) == 0x100u);
  TEST_EXPECT(&state, !cortex_m4_get_irq_active(cpu, 0));
  cortex_m4_step(cpu);
  TEST_EXPECT(&state, cortex_m4_get_register(cpu, 15) == 0x102u);
  kinetis_k22_destroy(device);
  return test_finish(&state);
}
