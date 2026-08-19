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
    RCM_SRS1 = 0x4007f001u,
    SIM_REGISTER = 0x40047000u,
    PORTA_PCR0 = 0x40049000u,
    GPIOA_PDIR = 0x400ff010u,
    PIT0_LDVAL = 0x40037100u,
    ADC0_CFG1 = 0x4003b008u,
    DMA_TCD0_SADDR = 0x40009000u,
    UART1_C2 = 0x4006b003u,
    SPI0_MCR = 0x4002c000u,
    I2C0_C1 = 0x40066002u,
    SCB_AIRCR = 0xe000ed0cu,
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

static uint32_t read32(TestState* state, KinetisK22* device, uint32_t address) {
    uint32_t value = 0;
    TEST_EXPECT(state, kinetis_k22_read(device, address, &value, sizeof(value)));
    return value;
}

static void write32(TestState* state, KinetisK22* device, uint32_t address,
                    uint32_t value) {
    TEST_EXPECT(state, kinetis_k22_write(device, address, &value, sizeof(value)));
}

static void dirty_peripherals(TestState* state, KinetisK22* device) {
    write32(state, device, SIM_REGISTER, 0x11223344u);
    write32(state, device, PORTA_PCR0, 0x01090300u);
    write32(state, device, PIT0_LDVAL, 0x55667788u);
    write32(state, device, ADC0_CFG1, 0x99aabbccu);
    write32(state, device, DMA_TCD0_SADDR, 0xddeeff00u);
    const uint8_t byte = 0xffu;
    TEST_EXPECT(state, kinetis_k22_write(device, UART1_C2, &byte, sizeof(byte)));
    TEST_EXPECT(state, kinetis_k22_write(device, I2C0_C1, &byte, sizeof(byte)));
    write32(state, device, SPI0_MCR, 0);
}

static void expect_reset_peripherals(TestState* state, KinetisK22* device) {
    TEST_EXPECT(state, read32(state, device, SIM_REGISTER) == 0x80000000u);
    TEST_EXPECT(state, read32(state, device, PORTA_PCR0) == 0);
    TEST_EXPECT(state, read32(state, device, PIT0_LDVAL) == 0);
    TEST_EXPECT(state, read32(state, device, ADC0_CFG1) == 0);
    TEST_EXPECT(state, read32(state, device, DMA_TCD0_SADDR) == 0);
    TEST_EXPECT(state, read8(state, device, UART1_C2) == 0);
    TEST_EXPECT(state, read8(state, device, I2C0_C1) == 0);
    TEST_EXPECT(state, read32(state, device, SPI0_MCR) == 0x00004001u);
    TEST_EXPECT(state, (read32(state, device, GPIOA_PDIR) & 1u) != 0);
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
    const uint16_t nop = 0xbf00u;
    TEST_EXPECT(&state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)));
    TEST_EXPECT(&state, kinetis_k22_load(device, 0x100, &nop, sizeof(nop)));
    TEST_EXPECT(&state, kinetis_k22_reset(device));
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS0) == 0x82u);
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS1) == 0);
    TEST_EXPECT(&state, read16(&state, device, WDOG_STCTRLH) == 0x01d3u);
    TEST_EXPECT(&state, read16(&state, device, WDOG_TOVALH) == 0x004cu);
    TEST_EXPECT(&state, read16(&state, device, WDOG_TOVALL) == 0x4b4cu);
    const uint32_t address = 0x20000040u;
    const uint32_t sentinel = 0xa55ac33cu;
    TEST_EXPECT(&state, kinetis_k22_write(device, address, &sentinel, sizeof(sentinel)));
    kinetis_k22_gpio_drive(device, 0, 0, true);
    dirty_peripherals(&state, device);

    configure(&state, device, 3, true);
    kinetis_k22_watchdog_advance(device, 2);
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS0) == 0x82u);
    write16(&state, device, WDOG_REFRESH, 0xa602u);
    write16(&state, device, WDOG_REFRESH, 0xb480u);
    kinetis_k22_watchdog_advance(device, 2);
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS0) == 0x82u);
    kinetis_k22_watchdog_advance(device, 1);
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS0) == 0x20u);
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS1) == 0);
    uint32_t retained = 0;
    TEST_EXPECT(&state, kinetis_k22_read(device, address, &retained, sizeof(retained)));
    TEST_EXPECT(&state, retained == sentinel);
    TEST_EXPECT(&state, cortex_m4_get_register(kinetis_k22_cpu(device), 15) == 0x100u);
    expect_reset_peripherals(&state, device);

    dirty_peripherals(&state, device);
    TEST_EXPECT(&state,
                cortex_m4_write_memory(kinetis_k22_cpu(device), SCB_AIRCR, 4, 0x05fa0004u));
    cortex_m4_step(kinetis_k22_cpu(device));
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS0) == 0);
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS1) == 0x04u);
    expect_reset_peripherals(&state, device);
    TEST_EXPECT(&state, kinetis_k22_read(device, address, &retained, sizeof(retained)));
    TEST_EXPECT(&state, retained == sentinel);

    configure(&state, device, 1, false);
    kinetis_k22_watchdog_advance(device, UINT32_MAX);
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS0) == 0);
    TEST_EXPECT(&state, read8(&state, device, RCM_SRS1) == 0x04u);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
