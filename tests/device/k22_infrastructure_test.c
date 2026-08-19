#include "kinetis_k22_internal.h"

#include <stdint.h>

#include "test.h"

enum {
    AIPS0_PACRI = 0x40000050u,
    DMA_ERQ = 0x4000800cu,
    DMA_SERQ = 0x4000801bu,
    DMA_TCD0 = 0x40009000u,
    AXBS_PRS0 = 0x40004000u,
    AXBS_CRS0 = 0x40004010u,
    FMC_PFAPR = 0x4001f000u,
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
    configure_cmt_dma(state, device);
    write8(state, device, CMT_MSC, 3u);
    TEST_EXPECT(state, (read8(state, device, CMT_MSC) & 0x80u) != 0u);
    TEST_EXPECT(state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), 45u));
    kinetis_k22_advance(device, 1u);
    TEST_EXPECT(state, read8(state, device, 0x20000080u) == 0x83u);
    TEST_EXPECT(state, read16(state, device, DMA_TCD0 + 0x16u) == 1u);
    TEST_EXPECT(state, read16(state, device, DMA_ERQ) == 0u);
    write8(state, device, CMT_CMD1, 0u);
    TEST_EXPECT(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u);
    kinetis_k22_advance(device, 1u);
    TEST_EXPECT(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u);
    kinetis_k22_advance(device, 1u);
    TEST_EXPECT(state, (read8(state, device, CMT_MSC) & 0x80u) != 0u);
    TEST_EXPECT(state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), 45u));
    write8(state, device, CMT_CMD1, 0u);
    TEST_EXPECT(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u);
}

static void test_usbdcd(TestState* state, KinetisK22* device) {
    write32(state, device, SIM_SCGC6, read32(state, device, SIM_SCGC6) | (1u << 21u));
    TEST_EXPECT(state,
                kinetis_k22_set_usb_charger(device, KINETIS_K22_USB_CHARGER_STANDARD_HOST));
    write32(state, device, USBDCD_CLOCK, 1u | (1u << 2u));
    write32(state, device, USBDCD_TIMER0, 0u);
    write32(state, device, USBDCD_TIMER1, 0u);
    write32(state, device, USBDCD_TIMER2, 0u);
    write32(state, device, USBDCD_CONTROL, (1u << 16u) | (1u << 24u));
    kinetis_k22_advance(device, 1024u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_STATUS) & (1u << 22u)) == 0u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_STATUS) & 0x000f0000u) == 0x000d0000u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_CONTROL) & (1u << 8u)) != 0u);
    TEST_EXPECT(state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), 54u));
    write32(state, device, USBDCD_CONTROL, 1u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_CONTROL) & (1u << 8u)) == 0u);
    TEST_EXPECT(state,
                kinetis_k22_set_usb_charger(device, KINETIS_K22_USB_CHARGER_CHARGING_PORT));
    write32(state, device, USBDCD_CONTROL, 1u << 24u);
    write32(state, device, USBDCD_CONTROL, 1u << 24u);
    kinetis_k22_advance(device, 1024u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_STATUS) & 0x000f0000u) == 0x000e0000u);
    TEST_EXPECT(state,
                kinetis_k22_set_usb_charger(device, KINETIS_K22_USB_CHARGER_DEDICATED));
    write32(state, device, USBDCD_CONTROL, 1u << 24u);
    kinetis_k22_advance(device, 1024u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_STATUS) & 0x000f0000u) == 0x000f0000u);
    TEST_EXPECT(state, kinetis_k22_set_usb_charger(device, KINETIS_K22_USB_CHARGER_NONE));
    write32(state, device, USBDCD_CONTROL, 1u << 24u);
    kinetis_k22_advance(device, 1024u);
    TEST_EXPECT(state, (read32(state, device, USBDCD_STATUS) & 0x003f0000u) == 0x00300000u);
    write32(state, device, USBDCD_CONTROL, 1u << 25u);
    TEST_EXPECT(state, read32(state, device, USBDCD_CONTROL) == 0u);
    TEST_EXPECT(state, read32(state, device, USBDCD_STATUS) == 0u);
    TEST_EXPECT(state,
                !kinetis_k22_set_usb_charger(
                    device, (KinetisK22UsbCharger)(KINETIS_K22_USB_CHARGER_DEDICATED + 1)));
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

static void test_retention(TestState* state, KinetisK22* device) {
    write32(state, device, RFVBAT_REG0, 0x12345678u);
    write32(state, device, RFSYS_REG0, 0xa5a55a5au);
    kinetis_k22_warm_reset(device, 0u, 4u);
    TEST_EXPECT(state, read32(state, device, RFVBAT_REG0) == 0x12345678u);
    TEST_EXPECT(state, read32(state, device, RFSYS_REG0) == 0u);
    TEST_EXPECT(state, kinetis_k22_reset(device));
    TEST_EXPECT(state, read32(state, device, RFVBAT_REG0) == 0u);
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = create_device(&state);
    test_cmt(&state, device);
    test_usbdcd(&state, device);
    test_access_controls(&state, device);
    test_retention(&state, device);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
