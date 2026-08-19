#include "kinetis_k22_internal.h"

#include <stdint.h>

#include "k22_test.h"
#include "test.h"

enum {
    AIPS0_PACRI = 0x40000050u,
    DMA_ERQ = 0x4000800cu,
    DMA_SERQ = 0x4000801bu,
    DMA_TCD0 = 0x40009000u,
    AXBS_PRS0 = 0x40004000u,
    AXBS_CRS0 = 0x40004010u,
    FMC_PFAPR = 0x4001f000u,
    FMC_PFB0CR = 0x4001f004u,
    FMC_TAGVDW0S0 = 0x4001f100u,
    FMC_TAGVDW1S0 = 0x4001f110u,
    FMC_TAGVDW2S0 = 0x4001f120u,
    FMC_TAGVDW3S0 = 0x4001f130u,
    DMAMUX_CHCFG0 = 0x40021000u,
    USBDCD_CONTROL = 0x40035000u,
    USBDCD_CLOCK = 0x40035004u,
    USBDCD_STATUS = 0x40035008u,
    USBDCD_TIMER0 = 0x40035010u,
    USBDCD_TIMER1 = 0x40035014u,
    USBDCD_TIMER2 = 0x40035018u,
    RFVBAT_REG0 = 0x4003e000u,
    RFSYS_REG0 = 0x40041000u,
    SIM_SCGC4 = 0x40048034u,
    SIM_SCGC6 = 0x4004803cu,
    CMT_MSC = 0x40062005u,
    CMT_CMD1 = 0x40062006u,
    CMT_CMD2 = 0x40062007u,
    CMT_CMD3 = 0x40062008u,
    CMT_CMD4 = 0x40062009u,
    CMT_DMA = 0x4006200bu,
    FTM0_SC = 0x40038000u,
    FTM0_MOD = 0x40038008u,
    FTM0_C0SC = 0x4003800cu,
    FTM0_C0V = 0x40038010u,
};

static KinetisK22* create_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.profile = KINETIS_K22_PROFILE_MK22FN1M012;
    configuration.package = KINETIS_K22_PACKAGE_LQ_144_LQFP;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(state, device != NULL);
    const uint32_t vectors[2] = {0x20001000u, 0x00000101u};
    const uint16_t program = 0xbf00u;
    TEST_EXPECT(state, kinetis_k22_load(device, 0u, vectors, sizeof(vectors)));
    TEST_EXPECT(state, kinetis_k22_load(device, 0x100u, &program, sizeof(program)));
    TEST_EXPECT(state, kinetis_k22_reset(device));
    TEST_EXPECT(state, k22_test_disable_watchdog(device));
    return device;
}

static uint32_t read32(TestState* state, KinetisK22* device, uint32_t address) {
    uint32_t value = UINT32_MAX;
    TEST_EXPECT(state, kinetis_k22_read(device, address, &value, sizeof(value)));
    return value;
}

static void write32(TestState* state, KinetisK22* device, uint32_t address,
                    uint32_t value) {
    TEST_EXPECT(state, kinetis_k22_write(device, address, &value, sizeof(value)));
}

static uint8_t read8(TestState* state, KinetisK22* device, uint32_t address) {
    uint8_t value = UINT8_MAX;
    TEST_EXPECT(state, kinetis_k22_read(device, address, &value, sizeof(value)));
    return value;
}

static uint16_t read16(TestState* state, KinetisK22* device, uint32_t address) {
    uint16_t value = UINT16_MAX;
    TEST_EXPECT(state, kinetis_k22_read(device, address, &value, sizeof(value)));
    return value;
}

static void write8(TestState* state, KinetisK22* device, uint32_t address, uint8_t value) {
    TEST_EXPECT(state, kinetis_k22_write(device, address, &value, sizeof(value)));
}

static void write16(TestState* state, KinetisK22* device, uint32_t address,
                    uint16_t value) {
    TEST_EXPECT(state, kinetis_k22_write(device, address, &value, sizeof(value)));
}

static void configure_cmt_dma(TestState* state, KinetisK22* device) {
    write32(state, device, DMA_TCD0, CMT_MSC);
    write16(state, device, DMA_TCD0 + 4u, 0u);
    write16(state, device, DMA_TCD0 + 6u, 0u);
    write32(state, device, DMA_TCD0 + 8u, 1u);
    write32(state, device, DMA_TCD0 + 0x10u, 0x20000080u);
    write16(state, device, DMA_TCD0 + 0x14u, 0u);
    write16(state, device, DMA_TCD0 + 0x16u, 1u);
    write16(state, device, DMA_TCD0 + 0x1cu, 1u << 3u);
    write16(state, device, DMA_TCD0 + 0x1eu, 1u);
    write8(state, device, DMAMUX_CHCFG0, 0x80u | 47u);
    write8(state, device, DMA_SERQ, 0u);
    write8(state, device, CMT_DMA, 1u);
}

static void test_cmt(TestState* state, KinetisK22* device) {
    write32(state, device, SIM_SCGC4, read32(state, device, SIM_SCGC4) | 4u);
    write8(state, device, CMT_CMD1, 0u);
    write8(state, device, CMT_CMD2, 1u);
    write8(state, device, CMT_CMD3, 0u);
    write8(state, device, CMT_CMD4, 0u);
    write8(state, device, CMT_MSC, 1u);
    TEST_EXPECT(state, (read8(state, device, CMT_MSC) & 0x80u) != 0u);
    write8(state, device, CMT_CMD2, 1u);
    TEST_EXPECT(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u);
    configure_cmt_dma(state, device);
    write8(state, device, CMT_MSC, 3u);
    TEST_EXPECT(state, !cortex_m4_get_irq_pending(kinetis_k22_cpu(device), 45u));
    kinetis_k22_advance(device, 7u);
    TEST_EXPECT(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u);
    kinetis_k22_advance(device, 8u);
    TEST_EXPECT(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u);
    kinetis_k22_advance(device, 1u);
    TEST_EXPECT(state, (read8(state, device, CMT_MSC) & 0x80u) != 0u);
    TEST_EXPECT(state, !cortex_m4_get_irq_pending(kinetis_k22_cpu(device), 45u));
    kinetis_k22_advance(device, 1u);
    TEST_EXPECT(state, read8(state, device, 0x20000080u) == 0x83u);
    TEST_EXPECT(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u);
    TEST_EXPECT(state, read16(state, device, DMA_TCD0 + 0x16u) == 1u);
    TEST_EXPECT(state, read16(state, device, DMA_ERQ) == 0u);
    write8(state, device, CMT_CMD2, 1u);
    TEST_EXPECT(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u);
    write8(state, device, CMT_MSC, 1u);
    kinetis_k22_advance(device, 1u);
    TEST_EXPECT(state, device->cmt_cycles != 0u);
    write8(state, device, CMT_MSC, 0u);
    TEST_EXPECT(state, device->cmt_stop_pending);
    TEST_EXPECT(state, device->cmt_cycles != 0u);
    kinetis_k22_advance(device, 14u);
    TEST_EXPECT(state, device->cmt_cycles == 0u);
    TEST_EXPECT(state, !device->cmt_running);
}

static void test_event_capacity(TestState* state, KinetisK22* device) {
    KinetisK22Event event;
    while (kinetis_k22_next_event(device, &event)) {
    }
    k22_io_set_clock(&device->io, K22_PERIPHERAL_PORTD, true);
    TEST_EXPECT(state, k22_io_write(&device->io, 0x4004c004u, 4u, 1u << 16u));
    for (uint32_t index = 0u; index < K22_EVENT_CAPACITY + 1u; index++) {
        TEST_EXPECT(state, k22_io_drive_pin(&device->io, 3u, 1u, false));
        TEST_EXPECT(state, k22_io_drive_pin(&device->io, 3u, 1u, true));
    }
    TEST_EXPECT(state, device->event_count == K22_EVENT_CAPACITY);
    TEST_EXPECT(state, device->event_read_index != 0u);
}

static void test_timing_dma(TestState* state, KinetisK22* device) {
    write32(state, device, SIM_SCGC6, read32(state, device, SIM_SCGC6) | (1u << 24u));
    write32(state, device, FTM0_MOD, 7u);
    write32(state, device, FTM0_C0V, 1u);
    write32(state, device, FTM0_C0SC, 0x11u);
    write32(state, device, FTM0_SC, 8u);
    kinetis_k22_advance(device, 2u);
    TEST_EXPECT(state, (read32(state, device, FTM0_C0SC) & (1u << 7u)) != 0u);
}

static void test_usbdcd(TestState* state, KinetisK22* device) {
    write32(state, device, SIM_SCGC6, read32(state, device, SIM_SCGC6) | (1u << 21u));
    TEST_EXPECT(state,
                kinetis_k22_set_usb_charger(device, KINETIS_K22_USB_CHARGER_STANDARD_HOST));
    write32(state, device, USBDCD_CLOCK, 1u << 2u);
    write32(state, device, USBDCD_TIMER0, 0u);
    write32(state, device, USBDCD_TIMER1, 0u);
    write32(state, device, USBDCD_TIMER2, 0u);
    write32(state, device, USBDCD_CONTROL, (1u << 16u) | (1u << 24u));
    kinetis_k22_advance(device, 3u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_STATUS) & (1u << 22u)) == 0u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_STATUS) & 0x000f0000u) == 0x00090000u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_CONTROL) & (1u << 8u)) != 0u);
    TEST_EXPECT(state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), 54u));
    write32(state, device, USBDCD_CONTROL, 1u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_CONTROL) & (1u << 8u)) == 0u);
    write32(state, device, USBDCD_CONTROL, (1u << 16u) | (1u << 25u));
    TEST_EXPECT(state,
                kinetis_k22_set_usb_charger(device, KINETIS_K22_USB_CHARGER_CHARGING_PORT));
    write32(state, device, USBDCD_CONTROL, (1u << 17u) | (1u << 16u) | (1u << 24u));
    kinetis_k22_advance(device, 5u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_STATUS) & 0x000f0000u) == 0x000e0000u);
    write32(state, device, USBDCD_CONTROL, (1u << 17u) | (1u << 16u) | (1u << 25u));
    TEST_EXPECT(state,
                kinetis_k22_set_usb_charger(device, KINETIS_K22_USB_CHARGER_DEDICATED));
    write32(state, device, USBDCD_CONTROL, (1u << 17u) | (1u << 16u) | (1u << 24u));
    kinetis_k22_advance(device, 5u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_STATUS) & 0x000f0000u) == 0x000f0000u);
    write32(state, device, USBDCD_CONTROL, (1u << 17u) | (1u << 16u) | (1u << 25u));
    TEST_EXPECT(state, kinetis_k22_set_usb_charger(device, KINETIS_K22_USB_CHARGER_NONE));
    write32(state, device, USBDCD_CONTROL, (1u << 17u) | (1u << 16u) | (1u << 24u));
    kinetis_k22_advance(device, 1000u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_STATUS) & 0x00700000u) == 0x00700000u);
    write32(state, device, USBDCD_CONTROL, 1u << 25u);
    TEST_EXPECT(state, read32(state, device, USBDCD_CONTROL) == 0x01030000u);
    TEST_EXPECT(state, read32(state, device, USBDCD_CLOCK) == 0x00000004u);
    TEST_EXPECT(state, read32(state, device, USBDCD_STATUS) == 0u);
    TEST_EXPECT(state, read32(state, device, USBDCD_TIMER0) == 0u);
    TEST_EXPECT(state, read32(state, device, USBDCD_TIMER1) == 0u);
    TEST_EXPECT(state, read32(state, device, USBDCD_TIMER2) == 0u);
    TEST_EXPECT(state,
                !kinetis_k22_set_usb_charger(
                    device, (KinetisK22UsbCharger)(KINETIS_K22_USB_CHARGER_ERROR + 1)));
}

static void test_access_controls(TestState* state, KinetisK22* device) {
    write32(state, device, AIPS0_PACRI, 1u << 20u);
    uint32_t value = 0u;
    TEST_EXPECT(state,
                kinetis_k22_peripheral_read(device, CMT_MSC, 1u,
                                            CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, &value));
    write32(state, device, AIPS0_PACRI, 6u << 20u);
    TEST_EXPECT(state,
                !kinetis_k22_peripheral_read(device, CMT_MSC, 1u,
                                             CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, &value));
    cortex_m4_set_control(kinetis_k22_cpu(device), CORTEX_M4_CONTROL_NPRIV);
    TEST_EXPECT(state, !kinetis_k22_peripheral_read(device, CMT_MSC, 1u,
                                                    CORTEX_M4_ACCESS_DATA, &value));
    cortex_m4_set_control(kinetis_k22_cpu(device), 0u);
    TEST_EXPECT(state, kinetis_k22_peripheral_read(device, CMT_MSC, 1u,
                                                   CORTEX_M4_ACCESS_DATA, &value));
    TEST_EXPECT(state, !kinetis_k22_peripheral_write(device, CMT_MSC, 1u,
                                                     CORTEX_M4_ACCESS_DATA, 0u));
    write32(state, device, AIPS0_PACRI, 0u);

    write32(state, device, AXBS_CRS0, 0x80000000u);
    uint32_t priority = 0x12345678u;
    TEST_EXPECT(state, !kinetis_k22_write(device, AXBS_PRS0, &priority, sizeof(priority)));

    write32(state, device, FMC_PFAPR, 0u);
    TEST_EXPECT(state, !kinetis_k22_memory_read(device, 0x100u, 2u,
                                                CORTEX_M4_ACCESS_INSTRUCTION, &value));
    TEST_EXPECT(
        state, kinetis_k22_memory_read(device, 0x100u, 2u, CORTEX_M4_ACCESS_DEBUG, &value));
}

static void test_fmc_geometry(TestState* state, KinetisK22* device) {
    write32(state, device, FMC_TAGVDW0S0, 0x41u);
    write32(state, device, FMC_TAGVDW1S0, 0x81u);
    write32(state, device, FMC_TAGVDW2S0, 0xc1u);
    write32(state, device, FMC_TAGVDW3S0, 0x101u);
    write32(state, device, FMC_PFB0CR, 1u << 20u);
    TEST_EXPECT(state, read32(state, device, FMC_TAGVDW0S0) == 0u);
    TEST_EXPECT(state, read32(state, device, FMC_TAGVDW1S0) == 0x81u);
    TEST_EXPECT(state, read32(state, device, FMC_TAGVDW2S0) == 0xc1u);
    TEST_EXPECT(state, read32(state, device, FMC_TAGVDW3S0) == 0x101u);
    write32(state, device, FMC_PFB0CR, 1u << 21u);
    TEST_EXPECT(state, read32(state, device, FMC_TAGVDW1S0) == 0u);
    TEST_EXPECT(state, read32(state, device, FMC_TAGVDW2S0) == 0xc1u);
}

static void test_retention(TestState* state, KinetisK22* device) {
    write32(state, device, RFVBAT_REG0, 0x12345678u);
    write32(state, device, RFSYS_REG0, 0xa5a55a5au);
    kinetis_k22_warm_reset(device, 0u, 4u);
    TEST_EXPECT(state, read32(state, device, RFVBAT_REG0) == 0x12345678u);
    TEST_EXPECT(state, read32(state, device, RFSYS_REG0) == 0u);
    TEST_EXPECT(state, kinetis_k22_reset(device));
    TEST_EXPECT(state, read32(state, device, RFVBAT_REG0) == 0u);
    TEST_EXPECT(state, k22_test_disable_watchdog(device));
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = create_device(&state);
    test_cmt(&state, device);
    test_usbdcd(&state, device);
    test_access_controls(&state, device);
    test_fmc_geometry(&state, device);
    test_retention(&state, device);
    test_event_capacity(&state, device);
    test_timing_dma(&state, device);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
