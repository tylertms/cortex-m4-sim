#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "test.h"

enum {
    WDOG_STCTRLH = 0x40052000u,
    WDOG_TOVALH = 0x40052004u,
    WDOG_TOVALL = 0x40052006u,
    WDOG_REFRESH = 0x4005200cu,
    WDOG_UNLOCK = 0x4005200eu,
    RCM_SRS0 = 0x4007f000u,
};

static void write16(TestState* state, KinetisK22* device, uint32_t address,
                    uint16_t value) {
    TEST_EXPECT(state, kinetis_k22_write(device, address, &value, sizeof(value)));
}

static uint8_t read8(TestState* state, KinetisK22* device, uint32_t address) {
    uint8_t value = 0;
    TEST_EXPECT(state, kinetis_k22_read(device, address, &value, sizeof(value)));
    return value;
}

static uint16_t read16(TestState* state, KinetisK22* device, uint32_t address) {
    uint16_t value = 0;
    TEST_EXPECT(state, kinetis_k22_read(device, address, &value, sizeof(value)));
    return value;
}

static void configure(TestState* state, KinetisK22* device, uint16_t timeout,
                      bool enabled) {
    write16(state, device, WDOG_UNLOCK, 0xc520u);
    write16(state, device, WDOG_UNLOCK, 0xd928u);
    write16(state, device, WDOG_TOVALH, 0);
    write16(state, device, WDOG_TOVALL, timeout);
    write16(state, device, WDOG_STCTRLH, enabled ? 1u : 0u);
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = kinetis_k22_create(kinetis_k22_default_configuration());
    TEST_EXPECT(&state, device != NULL);
    const uint32_t vectors[2] = {0x20001000u, 0x00000101u};
    TEST_EXPECT(&state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)));
    TEST_EXPECT(&state, kinetis_k22_reset(device));
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS0) == 0x80u);
    TEST_EXPECT(&state, read16(&state, device, WDOG_STCTRLH) == 0x01d3u);
    TEST_EXPECT(&state, read16(&state, device, WDOG_TOVALH) == 0x004cu);
    TEST_EXPECT(&state, read16(&state, device, WDOG_TOVALL) == 0x4b4cu);
    const uint32_t address = 0x20000040u;
    const uint32_t sentinel = 0xa55ac33cu;
    TEST_EXPECT(&state, kinetis_k22_write(device, address, &sentinel, sizeof(sentinel)));

    configure(&state, device, 3, true);
    kinetis_k22_watchdog_advance(device, 2);
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS0) == 0x80u);
    write16(&state, device, WDOG_REFRESH, 0xa602u);
    write16(&state, device, WDOG_REFRESH, 0xb480u);
    kinetis_k22_watchdog_advance(device, 2);
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS0) == 0x80u);
    kinetis_k22_watchdog_advance(device, 1);
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS0) == 0x20u);
    uint32_t retained = 0;
    TEST_EXPECT(&state, kinetis_k22_read(device, address, &retained, sizeof(retained)));
    TEST_EXPECT(&state, retained == sentinel);
    TEST_EXPECT(&state, cortex_m4_get_register(kinetis_k22_cpu(device), 15) == 0x100u);

    configure(&state, device, 1, false);
    kinetis_k22_watchdog_advance(device, UINT32_MAX);
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS0) == 0x20u);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
