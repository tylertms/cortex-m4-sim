#ifndef CORTEX_M4_SIM_K22_TEST_H
#define CORTEX_M4_SIM_K22_TEST_H

#include <stdbool.h>
#include <stdint.h>

#include "cortex_m4_sim/kinetis_k22.h"

static inline uint32_t k22_test_core_cycles_for_bus_cycles(const KinetisK22* device,
                                                            uint32_t bus_cycles) {
    const uint64_t core = kinetis_k22_core_clock_hz(device);
    const uint64_t bus = kinetis_k22_bus_clock_hz(device);
    return bus == 0u ? 0u : (uint32_t)((bus_cycles * core + bus - 1u) / bus);
}

static inline bool k22_test_write16(KinetisK22* device, uint32_t address,
                                    uint16_t value) {
    return kinetis_k22_write(device, address, &value, sizeof(value));
}

static inline bool k22_test_disable_watchdog(KinetisK22* device) {
    const uint32_t control = 0x40052000u;
    const uint32_t unlock = 0x4005200eu;
    if (!k22_test_write16(device, unlock, 0xc520u) ||
        !k22_test_write16(device, unlock, 0xd928u) ||
        !k22_test_write16(device, control, 0u))
        return false;
    kinetis_k22_advance(device, k22_test_core_cycles_for_bus_cycles(device, 260u));
    uint16_t value = UINT16_MAX;
    return kinetis_k22_read(device, control, &value, sizeof(value)) && (value & 1u) == 0u;
}

#endif
