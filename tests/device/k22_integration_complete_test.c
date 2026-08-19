#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "test.h"

enum {
    FMC_PFB0CR = 0x4001f000u,
    FMC_TAGVDW0S0 = 0x4001f100u,
    DAC1_DAT0L = 0x40028000u,
    SIM_SCGC4 = 0x40048034u,
    I2C0_C1 = 0x40066002u,
    I2C1_C1 = 0x40067002u,
    UART1_C2 = 0x4006b003u,
    UART1_S1 = 0x4006b004u,
    WDOG_STCTRLH = 0x40052000u,
    WDOG_TOVALH = 0x40052004u,
    WDOG_TOVALL = 0x40052006u,
    WDOG_UNLOCK = 0x4005200eu,
    RCM_SRS0 = 0x4007f000u,
    RCM_SSRS0 = 0x4007f008u,
    GPIOA_PDIR = 0x400ff010u,
};

static const uint32_t MCM_PLASC = 0xe0080008u;

typedef struct {
    uint32_t calls;
} WaitFixture;

static uint32_t wait_states(void* context, uint32_t address, uint8_t size,
                            CortexM4Access access, bool write, bool sequential) {
    WaitFixture* fixture = context;
    fixture->calls++;
    return (address & 1u) + size + access + (write ? 1u : 0u) + (sequential ? 1u : 0u);
}

static bool read8(KinetisK22* device, uint32_t address, uint8_t* value) {
    return kinetis_k22_read(device, address, value, sizeof(*value));
}

static bool read32(KinetisK22* device, uint32_t address, uint32_t* value) {
    return kinetis_k22_read(device, address, value, sizeof(*value));
}

static bool write16(KinetisK22* device, uint32_t address, uint16_t value) {
    return kinetis_k22_write(device, address, &value, sizeof(value));
}

static bool write32(KinetisK22* device, uint32_t address, uint32_t value) {
    return kinetis_k22_write(device, address, &value, sizeof(value));
}

static KinetisK22* create_device(TestState* state, KinetisK22Package package) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.package = package;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(state, device != NULL);
    const uint32_t vectors[2] = {0x20001000u, 0x00000101u};
    const uint16_t program = 0xbe00u;
    TEST_EXPECT(state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)));
    TEST_EXPECT(state, kinetis_k22_load(device, 0x100, &program, sizeof(program)));
    return device;
}

static void expect_package_selection(TestState* state) {
    KinetisK22Configuration invalid_profile = kinetis_k22_default_configuration();
    invalid_profile.profile = KINETIS_K22_PROFILE_COUNT;
    TEST_EXPECT(state, kinetis_k22_create(invalid_profile) == NULL);

    KinetisK22Configuration invalid = kinetis_k22_default_configuration();
    invalid.package = KINETIS_K22_PACKAGE_AH_64_WLCSP;
    TEST_EXPECT(state, kinetis_k22_create(invalid) == NULL);

    KinetisK22* small = create_device(state, KINETIS_K22_PACKAGE_LH_64_LQFP);
    uint16_t value = 0;
    TEST_EXPECT(state, !kinetis_k22_read(small, DAC1_DAT0L, &value, sizeof(value)));
    TEST_EXPECT(state, !kinetis_k22_get_dac_output(small, 1, &value));
    kinetis_k22_gpio_drive(small, 4, 31, true);
    uint32_t input = UINT32_MAX;
    TEST_EXPECT(state, read32(small, GPIOA_PDIR + 4u * 0x40u, &input));
    TEST_EXPECT(state, input == 0);
    kinetis_k22_destroy(small);

    KinetisK22* large = create_device(state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    TEST_EXPECT(state, kinetis_k22_read(large, DAC1_DAT0L, &value, sizeof(value)));
    kinetis_k22_destroy(large);
}

static void expect_manifest_fallback(TestState* state, KinetisK22* device) {
    uint32_t value = 0;
    TEST_EXPECT(state, read32(device, FMC_PFB0CR, &value));
    TEST_EXPECT(state, value == 0x00f8003fu);
    TEST_EXPECT(state, write32(device, FMC_PFB0CR, UINT32_MAX));
    TEST_EXPECT(state, read32(device, FMC_PFB0CR, &value));
    TEST_EXPECT(state, value == 0x00ffffffu);
    TEST_EXPECT(state, !read32(device, FMC_PFB0CR + 0x0cu, &value));
    TEST_EXPECT(state, !write32(device, FMC_PFB0CR + 0x0cu, UINT32_MAX));

    const uint32_t alias = 0x42000000u + (FMC_TAGVDW0S0 - 0x40000000u) * 32u + 5u * 4u;
    TEST_EXPECT(state, write32(device, alias, 1));
    TEST_EXPECT(state, read32(device, FMC_TAGVDW0S0, &value));
    TEST_EXPECT(state, value == 0x20u);
    TEST_EXPECT(state, read32(device, alias, &value));
    TEST_EXPECT(state, value == 1);
    TEST_EXPECT(state, !read32(device, 0x43ffffffu, &value));

    TEST_EXPECT(state, read32(device, MCM_PLASC, &value));
    TEST_EXPECT(state, value == 0x0017001fu);
    TEST_EXPECT(state, !write32(device, MCM_PLASC, UINT32_MAX));
    TEST_EXPECT(state, read32(device, MCM_PLASC, &value));
    TEST_EXPECT(state, value == 0x0017001fu);
}

static void expect_clock_gates(TestState* state, KinetisK22* device) {
    uint32_t value = 0;
    uint8_t status = 0;
    CortexM4* cpu = kinetis_k22_cpu(device);
    TEST_EXPECT(state, read32(device, SIM_SCGC4, &value));
    TEST_EXPECT(state, (value & (1u << 11)) == 0);
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SCGC4, 4, 0xf0100830u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, UART1_C2, 1, 0x24u));
    TEST_EXPECT(state,
                kinetis_k22_serial_receive(device, KINETIS_K22_SERIAL_UART1, 0x5au, 0));
    TEST_EXPECT(state, read8(device, UART1_S1, &status));
    TEST_EXPECT(state, (status & 0x20u) != 0);
    TEST_EXPECT(state, cortex_m4_get_irq_pending(cpu, 33));
}

static void expect_endpoint_event_order(TestState* state, KinetisK22* device) {
    CortexM4* cpu = kinetis_k22_cpu(device);
    uint32_t gates = 0;
    TEST_EXPECT(state, read32(device, SIM_SCGC4, &gates));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SCGC4, 4, gates | 0xc0u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, I2C0_C1, 1, 0xf1u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, I2C1_C1, 1, 0xf1u));
    KinetisK22I2cTransfer transfer;
    TEST_EXPECT(state,
                kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C1, &transfer));
    TEST_EXPECT(state, transfer.type == KINETIS_K22_I2C_START);
    TEST_EXPECT(state,
                kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C0, &transfer));
    TEST_EXPECT(state, transfer.type == KINETIS_K22_I2C_START);
}

static void expect_reset_domains(TestState* state, KinetisK22* device) {
    const uint32_t address = 0x20000040u;
    const uint32_t sentinel = 0x5aa53cc3u;
    TEST_EXPECT(state, write32(device, address, sentinel));
    kinetis_k22_gpio_drive(device, 0, 0, true);
    TEST_EXPECT(state, write16(device, WDOG_UNLOCK, 0xc520u));
    TEST_EXPECT(state, write16(device, WDOG_UNLOCK, 0xd928u));
    TEST_EXPECT(state, write16(device, WDOG_TOVALH, 0));
    TEST_EXPECT(state, write16(device, WDOG_TOVALL, 1));
    TEST_EXPECT(state, write16(device, WDOG_STCTRLH, 1));
    kinetis_k22_watchdog_advance(device, 1);

    uint32_t value = 0;
    uint8_t cause = 0;
    TEST_EXPECT(state, read32(device, address, &value));
    TEST_EXPECT(state, value == sentinel);
    TEST_EXPECT(state, read8(device, RCM_SRS0, &cause));
    TEST_EXPECT(state, cause == 0x20u);
    TEST_EXPECT(state, read8(device, RCM_SSRS0, &cause));
    TEST_EXPECT(state, (cause & 0xa2u) == 0xa2u);
    TEST_EXPECT(state, read32(device, GPIOA_PDIR, &value));
    TEST_EXPECT(state, (value & 1u) != 0);
}

static void expect_copy(TestState* state, KinetisK22* source) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.package = KINETIS_K22_PACKAGE_DC_121_XFBGA;
    KinetisK22* destination = kinetis_k22_create(configuration);
    TEST_EXPECT(state, destination != NULL);
    WaitFixture source_wait = {0};
    WaitFixture destination_wait = {0};
    cortex_m4_set_wait_states(kinetis_k22_cpu(source), wait_states, &source_wait);
    cortex_m4_set_wait_states(kinetis_k22_cpu(destination), wait_states, &destination_wait);
    TEST_EXPECT(state, kinetis_k22_copy(destination, source));
    uint32_t value = 0;
    TEST_EXPECT(state, read32(destination, 0x20000040u, &value));
    TEST_EXPECT(state, value == 0x5aa53cc3u);
    TEST_EXPECT(state, kinetis_k22_core_clock_hz(destination) ==
                           kinetis_k22_core_clock_hz(source));
    TEST_EXPECT(state,
                kinetis_k22_bus_clock_hz(destination) == kinetis_k22_bus_clock_hz(source));
    TEST_CONNECT_DEBUGGER(state, kinetis_k22_cpu(destination));
    TEST_EXPECT(state, cortex_m4_step(kinetis_k22_cpu(destination)).stop ==
                           CORTEX_M4_STOP_BREAKPOINT);
    TEST_EXPECT(state, destination_wait.calls != 0u);
    TEST_EXPECT(state, source_wait.calls == 0u);
    kinetis_k22_destroy(destination);
}

int main(void) {
    TestState state = {0};
    expect_package_selection(&state);
    KinetisK22* device = create_device(&state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    TEST_EXPECT(&state, kinetis_k22_reset(device));
    TEST_EXPECT(&state, kinetis_k22_core_clock_hz(device) == 20971520u);
    TEST_EXPECT(&state, kinetis_k22_bus_clock_hz(device) == 20971520u);
    expect_manifest_fallback(&state, device);
    expect_clock_gates(&state, device);
    expect_endpoint_event_order(&state, device);
    expect_reset_domains(&state, device);
    expect_copy(&state, device);
    KinetisK22Event event;
    while (kinetis_k22_next_event(device, &event)) {
    }
    TEST_EXPECT(&state, !kinetis_k22_next_event(device, &event));
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
