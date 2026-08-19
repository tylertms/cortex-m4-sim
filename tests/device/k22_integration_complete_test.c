#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "k22_test.h"
#include "kinetis_k22_internal.h"
#include "test.h"

enum {
    FMC_PFAPR = 0x4001f000u,
    FMC_PFB0CR = 0x4001f004u,
    FMC_TAGVDW0S0 = 0x4001f100u,
    FMC_TAGVDW1S0 = 0x4001f110u,
    FMC_DATAW0S0UM = 0x4001f200u,
    FMC_DATAW0S0LM = 0x4001f20cu,
    FMC_DATAW1S0UM = 0x4001f240u,
    FTFA_FSTAT = 0x40020000u,
    FTFA_FCCOB3 = 0x40020004u,
    PDB_SC = 0x40036000u,
    PDB_MOD = 0x40036004u,
    PDB_CH0C1 = 0x40036010u,
    PDB_CH0DLY0 = 0x40036018u,
    PDB_DACINT0 = 0x40036150u,
    PDB_DACINTC0 = 0x40036154u,
    PIT_MCR = 0x40037000u,
    PIT_LDVAL0 = 0x40037100u,
    PIT_TCTRL0 = 0x40037108u,
    PIT_TFLG0 = 0x4003710cu,
    FTM0_SC = 0x40038000u,
    FTM0_MOD = 0x40038008u,
    FTM0_C0SC = 0x4003800cu,
    FTM0_C0V = 0x40038010u,
    FTM0_EXTTRIG = 0x4003806cu,
    LPTMR_CSR = 0x40040000u,
    LPTMR_PSR = 0x40040004u,
    LPTMR_CMR = 0x40040008u,
    LPTMR_CNR = 0x4004000cu,
    ADC0_SC1A = 0x4003b000u,
    ADC0_SC1B = 0x4003b004u,
    ADC0_CFG1 = 0x4003b008u,
    ADC0_RA = 0x4003b010u,
    ADC0_RB = 0x4003b014u,
    ADC0_SC2 = 0x4003b020u,
    DAC0_DAT0L = 0x4003f000u,
    DAC0_DAT1L = 0x4003f002u,
    DAC0_C0 = 0x4003f021u,
    DAC0_C1 = 0x4003f022u,
    DAC0_C2 = 0x4003f023u,
    DAC1_DAT0L = 0x40028000u,
    CMP0_CR1 = 0x40073001u,
    CMP0_MUXCR = 0x40073005u,
    SIM_SCGC4 = 0x40048034u,
    SIM_SCGC5 = 0x40048038u,
    SIM_SCGC6 = 0x4004803cu,
    SIM_SCGC7 = 0x40048040u,
    SIM_SCGC1 = 0x40048028u,
    SIM_SCGC3 = 0x40048030u,
    SIM_SOPT7 = 0x40048018u,
    PORTD_PCR0 = 0x4004c000u,
    PORTD_ISFR = 0x4004c0a0u,
    USB0_ISTAT = 0x40072080u,
    USB0_INTEN = 0x40072084u,
    USB0_ENDPT3 = 0x40072094u,
    CAN0_MCR = 0x40024000u,
    CAN0_CTRL1 = 0x40024010u,
    CAN0_IMASK1 = 0x40024028u,
    CAN0_IFLAG1 = 0x40024030u,
    CAN0_MB0_CS = 0x40024080u,
    I2S0_TCSR = 0x4002f000u,
    I2S0_TDR0 = 0x4002f020u,
    I2S0_RCSR = 0x4002f080u,
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
    DHCSR = 0xe000edf0u,
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

static bool write8(KinetisK22* device, uint32_t address, uint8_t value) {
    return kinetis_k22_write(device, address, &value, sizeof(value));
}

static bool read16(KinetisK22* device, uint32_t address, uint16_t* value) {
    return kinetis_k22_read(device, address, value, sizeof(*value));
}

static bool write32(KinetisK22* device, uint32_t address, uint32_t value) {
    return kinetis_k22_write(device, address, &value, sizeof(value));
}

static bool cpu_write8(KinetisK22* device, uint32_t address, uint8_t value) {
    return cortex_m4_write_memory(kinetis_k22_cpu(device), address, 1u, value);
}

static bool irq_level(const KinetisK22* device, uint8_t irq) {
    const CortexM4* cpu = kinetis_k22_cpu_const(device);
    return (cpu->irq_level[irq / 32u] & (1u << (irq & 31u))) != 0u;
}

static uint32_t flash_fccob_address(uint8_t index) {
    static const uint8_t offsets[12] = {7u, 6u, 5u,  4u,  11u, 10u,
                                        9u, 8u, 15u, 14u, 13u, 12u};
    return FTFA_FCCOB3 - 4u + offsets[index];
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

static KinetisK22* create_f12_device(TestState* state, KinetisK22Package package) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.profile = KINETIS_K22_PROFILE_MK22FN1M012;
    configuration.package = package;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(state, device != NULL);
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
    uint8_t register_value = 0;
    TEST_EXPECT(state, kinetis_k22_read(small, DAC1_DAT0L, &register_value,
                                        sizeof(register_value)));
    TEST_EXPECT(state, kinetis_k22_get_dac_output(small, 1, &value));
    kinetis_k22_gpio_drive(small, 4, 31, true);
    uint32_t input = UINT32_MAX;
    TEST_EXPECT(state, read32(small, GPIOA_PDIR + 4u * 0x40u, &input));
    TEST_EXPECT(state, input == 0);
    kinetis_k22_destroy(small);

    KinetisK22* large = create_device(state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    TEST_EXPECT(state, kinetis_k22_read(large, DAC1_DAT0L, &register_value,
                                        sizeof(register_value)));
    kinetis_k22_destroy(large);

    KinetisK22* limited = create_device(state, KINETIS_K22_PACKAGE_FX_88_HVQFN);
    TEST_EXPECT(state, !kinetis_k22_read(limited, DAC1_DAT0L, &register_value,
                                         sizeof(register_value)));
    TEST_EXPECT(state, !kinetis_k22_get_dac_output(limited, 1, &value));
    kinetis_k22_destroy(limited);
}

static void expect_integrated_flash_command(TestState* state, KinetisK22* device) {
    const uint32_t target = 0x00001000u;
    uint32_t value = 0u;
    TEST_EXPECT(state, read32(device, target, &value));
    TEST_EXPECT(state, value == UINT32_MAX);
    const uint8_t command[8] = {0x06u, 0x00u, 0x10u, 0x00u, 0x78u, 0x56u, 0x34u, 0x12u};
    for (uint8_t index = 0u; index < sizeof(command); index++)
        TEST_EXPECT(state, cpu_write8(device, flash_fccob_address(index), command[index]));
    TEST_EXPECT(state, cpu_write8(device, FTFA_FSTAT, 0x80u));
    TEST_EXPECT(state, read32(device, target, &value));
    TEST_EXPECT(state, value == 0x12345678u);
    const uint32_t fstat_bit_band =
        0x42000000u + (FTFA_FSTAT - 0x40000000u) * 32u + 4u * 4u;
    TEST_EXPECT(state, write32(device, fstat_bit_band, 1u));
}

static void expect_io_irq_levels(TestState* state, KinetisK22* device) {
    TEST_EXPECT(state, write32(device, SIM_SCGC5, 1u << 12u));
    TEST_EXPECT(state, write32(device, PORTD_PCR0, 9u << 16u));
    kinetis_k22_gpio_drive(device, 3u, 0u, false);
    kinetis_k22_gpio_drive(device, 3u, 0u, true);
    TEST_EXPECT(state, irq_level(device, 62u));
    TEST_EXPECT(state, write32(device, PORTD_ISFR, 1u));
    TEST_EXPECT(state, !irq_level(device, 62u));

    TEST_EXPECT(state, write32(device, SIM_SCGC4, 1u << 18u));
    TEST_EXPECT(state, write8(device, USB0_INTEN, 1u << 3u));
    TEST_EXPECT(state, write8(device, USB0_ENDPT3, 1u));
    TEST_EXPECT(state, kinetis_k22_usb_token(device, 3u, 0x69u, false));
    TEST_EXPECT(state, irq_level(device, 53u));
    TEST_EXPECT(state, write8(device, USB0_ISTAT, 1u << 3u));
    TEST_EXPECT(state, !irq_level(device, 53u));

    TEST_EXPECT(state, write32(device, SIM_SCGC6, 1u << 15u));
    TEST_EXPECT(state, write32(device, I2S0_TCSR, UINT32_C(0x80000100)));
    TEST_EXPECT(state, write32(device, I2S0_TDR0, 0x12345678u));
    TEST_EXPECT(state, irq_level(device, 28u));
    TEST_EXPECT(state, write32(device, I2S0_TCSR, UINT32_C(0x80000000)));
    TEST_EXPECT(state, !irq_level(device, 28u));
    TEST_EXPECT(state, write32(device, I2S0_RCSR, UINT32_C(0x80000100)));
    TEST_EXPECT(state, kinetis_k22_i2s_receive(device, 0x87654321u));
    TEST_EXPECT(state, irq_level(device, 29u));
    TEST_EXPECT(state, write32(device, I2S0_RCSR, UINT32_C(0x80000000)));
    TEST_EXPECT(state, !irq_level(device, 29u));
}

static void expect_can_irq_level(TestState* state) {
    KinetisK22* device = create_f12_device(state, KINETIS_K22_PACKAGE_MD_144_MAPBGA);
    TEST_EXPECT(state, write32(device, SIM_SCGC6, 1u << 4u));
    TEST_EXPECT(state, write32(device, CAN0_MCR, 0x0fu));
    TEST_EXPECT(state, write32(device, CAN0_CTRL1, 0u));
    TEST_EXPECT(state, write32(device, CAN0_IMASK1, 1u));
    TEST_EXPECT(state, write32(device, CAN0_MB0_CS, 4u << 24u));
    const KinetisK22CanFrame frame = {
        .identifier = 0x123u,
        .length = 8u,
        .data = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u},
    };
    TEST_EXPECT(state, kinetis_k22_can_receive(device, &frame));
    TEST_EXPECT(state, irq_level(device, 75u));
    TEST_EXPECT(state, write32(device, CAN0_IFLAG1, 1u));
    TEST_EXPECT(state, !irq_level(device, 75u));
    kinetis_k22_destroy(device);
}

static void expect_memory_domains(TestState* state) {
    KinetisK22* device = create_f12_device(state, KINETIS_K22_PACKAGE_LQ_144_LQFP);
    CortexM4* cpu = kinetis_k22_cpu(device);
    const CortexM4* constant_cpu = kinetis_k22_cpu_const(device);
    TEST_EXPECT(state, constant_cpu == cpu);
    TEST_EXPECT(state, kinetis_k22_cpu_const(NULL) == NULL);

    const uint32_t flexram = 0x14000000u;
    uint32_t value = 0x12345678u;
    TEST_EXPECT(state, kinetis_k22_write(device, flexram, &value, sizeof(value)));
    value = 0u;
    TEST_EXPECT(state, cortex_m4_read_memory(cpu, flexram, 4u, &value));
    TEST_EXPECT(state, value == 0x12345678u);
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, flexram + 4u, 4u, 0xa5a55a5au));
    TEST_EXPECT(state, read32(device, flexram + 4u, &value));
    TEST_EXPECT(state, value == 0xa5a55a5au);

    const uint32_t flash = 0x100u;
    TEST_EXPECT(state,
                !kinetis_k22_memory_write(device, flash, 1u, CORTEX_M4_ACCESS_DATA, 0x5au));
    uint8_t byte = 0x5au;
    TEST_EXPECT(state, kinetis_k22_write(device, flash, &byte, sizeof(byte)));
    byte = 0u;
    TEST_EXPECT(state, read8(device, flash, &byte));
    TEST_EXPECT(state, byte == 0x5au);
    kinetis_k22_destroy(device);

    KinetisK22Configuration small = kinetis_k22_default_configuration();
    small.flash_size = 8u;
    KinetisK22* short_flash = kinetis_k22_create(small);
    TEST_EXPECT(state, short_flash != NULL);
    TEST_EXPECT(state, kinetis_k22_reset(short_flash));
    kinetis_k22_warm_reset(NULL, 0u, 0u);
    kinetis_k22_destroy(short_flash);
}

static void expect_manifest_fallback(TestState* state, KinetisK22* device) {
    uint32_t value = 0;
    TEST_EXPECT(state, read32(device, FMC_PFAPR, &value));
    TEST_EXPECT(state, value == 0x00f8003fu);
    TEST_EXPECT(state, write32(device, FMC_PFAPR, UINT32_MAX));
    TEST_EXPECT(state, read32(device, FMC_PFAPR, &value));
    TEST_EXPECT(state, value == 0x00ffffffu);
    TEST_EXPECT(state, !read32(device, FMC_PFAPR + 0x0cu, &value));
    TEST_EXPECT(state, !write32(device, FMC_PFAPR + 0x0cu, UINT32_MAX));
    TEST_EXPECT(state, !kinetis_k22_peripheral_write(
                           device, FMC_PFAPR, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, 0u));

    uint8_t flash = 0x5au;
    TEST_EXPECT(state, kinetis_k22_write(device, 0x100u, &flash, sizeof(flash)));
    TEST_EXPECT(state, write32(device, FMC_PFAPR, 0x10u));
    TEST_EXPECT(
        state, !kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value));
    TEST_EXPECT(state, kinetis_k22_dma_read(device, 0x100u, 1u, &value));
    TEST_EXPECT(state, value == 0x5au);
    TEST_EXPECT(state, write32(device, FMC_PFAPR, 4u));
    TEST_EXPECT(state, !kinetis_k22_dma_read(device, 0x100u, 1u, &value));
    TEST_EXPECT(state,
                kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value));
    TEST_EXPECT(state, value == 0x5au);
    TEST_EXPECT(state, write32(device, FMC_PFAPR, UINT32_MAX));
    TEST_EXPECT(state, read32(device, FMC_PFB0CR, &value));
    TEST_EXPECT(state, write32(device, FMC_PFB0CR, (value & ~0x18u) | 0x00f00000u));

    const uint32_t alias = 0x42000000u + (FMC_TAGVDW0S0 - 0x40000000u) * 32u + 5u * 4u;
    TEST_EXPECT(state, write32(device, alias, 1));
    TEST_EXPECT(state, read32(device, FMC_TAGVDW0S0, &value));
    TEST_EXPECT(state, value == 0x20u);
    TEST_EXPECT(state, read32(device, alias, &value));
    TEST_EXPECT(state, value == 1);
    TEST_EXPECT(state, write32(device, FMC_TAGVDW0S0, 0x21u));
    TEST_EXPECT(state, write32(device, FMC_TAGVDW1S0, 0x41u));
    TEST_EXPECT(state, write32(device, FMC_DATAW0S0UM, 0x12345678u));
    TEST_EXPECT(state, write32(device, FMC_DATAW1S0UM, 0x89abcdefu));
    TEST_EXPECT(state, write32(device, FMC_PFB0CR, (1u << 19u) | (1u << 20u)));
    TEST_EXPECT(state, read32(device, FMC_TAGVDW0S0, &value));
    TEST_EXPECT(state, value == 0u);
    TEST_EXPECT(state, read32(device, FMC_TAGVDW1S0, &value));
    TEST_EXPECT(state, value == 0x41u);
    TEST_EXPECT(state, read32(device, FMC_DATAW0S0UM, &value));
    TEST_EXPECT(state, value == 0u);
    TEST_EXPECT(state, read32(device, FMC_DATAW1S0UM, &value));
    TEST_EXPECT(state, value == 0x89abcdefu);
    TEST_EXPECT(state, read32(device, FMC_PFB0CR, &value));
    TEST_EXPECT(state, (value & 0x00f80000u) == 0u);
    TEST_EXPECT(state, write32(device, FMC_PFB0CR, 1u << 21u));
    TEST_EXPECT(state, read32(device, FMC_TAGVDW1S0, &value));
    TEST_EXPECT(state, value == 0u);
    TEST_EXPECT(state, read32(device, FMC_DATAW1S0UM, &value));
    TEST_EXPECT(state, value == 0u);
    TEST_EXPECT(state, !read32(device, 0x43ffffffu, &value));

    TEST_EXPECT(state, read32(device, MCM_PLASC, &value));
    TEST_EXPECT(state, value == 0x0017001fu);
    TEST_EXPECT(state, !write32(device, MCM_PLASC, UINT32_MAX));
    TEST_EXPECT(state, read32(device, MCM_PLASC, &value));
    TEST_EXPECT(state, value == 0x0017001fu);
}

static void expect_fmc_cache(TestState* state) {
    KinetisK22* device = create_device(state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    TEST_EXPECT(state, kinetis_k22_reset(device));
    uint32_t value = 0u;
    TEST_EXPECT(state, read32(device, FMC_PFB0CR, &value));
    TEST_EXPECT(state, write32(device, FMC_PFB0CR, value | 0x0ef00000u));
    uint8_t byte = 0x5au;
    TEST_EXPECT(state, kinetis_k22_write(device, 0x100u, &byte, sizeof(byte)));
    TEST_EXPECT(state,
                kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value));
    TEST_EXPECT(state, value == 0x5au);
    TEST_EXPECT(state, read32(device, FMC_TAGVDW0S0, &value));
    TEST_EXPECT(state, (value & 1u) != 0u);
    TEST_EXPECT(state, read32(device, FMC_DATAW0S0LM, &value));
    TEST_EXPECT(state, (value & 0xffu) == 0x5au);
    TEST_EXPECT(state, kinetis_k22_flash_controller_write(device, 0x100u, 1u, 0xa5u));
    TEST_EXPECT(state,
                kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value));
    TEST_EXPECT(state, value == 0x5au);
    TEST_EXPECT(state, read32(device, FMC_PFB0CR, &value));
    TEST_EXPECT(state, write32(device, FMC_PFB0CR, value | (1u << 20u)));
    TEST_EXPECT(state,
                kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value));
    TEST_EXPECT(state, value == 0xa5u);
    kinetis_k22_destroy(device);
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

static void expect_package_serial_extensions(TestState* state) {
    KinetisK22* small = create_f12_device(state, KINETIS_K22_PACKAGE_LH_64_LQFP);
    TEST_EXPECT(state,
                !kinetis_k22_serial_receive(small, KINETIS_K22_SERIAL_UART3, 0x11u, 0u));
    TEST_EXPECT(state,
                !kinetis_k22_serial_receive(small, KINETIS_K22_SERIAL_SPI1, 0x22u, 0u));
    kinetis_k22_destroy(small);

    KinetisK22* large = create_f12_device(state, KINETIS_K22_PACKAGE_MD_144_MAPBGA);
    CortexM4* cpu = kinetis_k22_cpu(large);
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SCGC1, 4u, 3u << 10u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SCGC3, 4u, 1u << 12u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SCGC4, 4u, 1u << 13u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x4006d003u, 1u, 0x24u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x400ea003u, 1u, 0x24u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x400eb003u, 1u, 0x24u));
    TEST_EXPECT(state,
                kinetis_k22_serial_receive(large, KINETIS_K22_SERIAL_UART3, 0x33u, 0u));
    TEST_EXPECT(state,
                kinetis_k22_serial_receive(large, KINETIS_K22_SERIAL_UART4, 0x44u, 0u));
    TEST_EXPECT(state,
                kinetis_k22_serial_receive(large, KINETIS_K22_SERIAL_UART5, 0x55u, 0u));
    TEST_EXPECT(state,
                kinetis_k22_serial_receive(large, KINETIS_K22_SERIAL_SPI2, 0x66u, 0u));
    uint8_t value = 0u;
    TEST_EXPECT(state, read8(large, 0x4006d007u, &value));
    TEST_EXPECT(state, value == 0x33u);
    TEST_EXPECT(state, read8(large, 0x400ea007u, &value));
    TEST_EXPECT(state, value == 0x44u);
    TEST_EXPECT(state, read8(large, 0x400eb007u, &value));
    TEST_EXPECT(state, value == 0x55u);
    kinetis_k22_destroy(large);
}

static void expect_sdhc_integration(TestState* state) {
    uint8_t card[512];
    for (size_t index = 0u; index < sizeof(card); index++)
        card[index] = (uint8_t)index;
    KinetisK22* small = create_f12_device(state, KINETIS_K22_PACKAGE_LH_64_LQFP);
    TEST_EXPECT(state, !kinetis_k22_sdhc_insert(small, card, sizeof(card), false));
    kinetis_k22_destroy(small);

    KinetisK22* large = create_f12_device(state, KINETIS_K22_PACKAGE_LK_80_LQFP);
    TEST_EXPECT(state, kinetis_k22_sdhc_insert(large, card, sizeof(card), false));
    CortexM4* cpu = kinetis_k22_cpu(large);
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SCGC3, 4u, 1u << 17u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x400b1034u, 4u, UINT32_MAX));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x400b1038u, 4u, UINT32_MAX));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x400b1008u, 4u, 0u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x400b100cu, 4u, 3u << 24u));
    uint32_t response = 0u;
    TEST_EXPECT(state, read32(large, 0x400b1010u, &response));
    TEST_EXPECT(state, response == 0x00010000u);
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x400b1008u, 4u, 0x00010000u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x400b100cu, 4u, 7u << 24u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x400b1004u, 4u, 4u | (1u << 16u)));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x400b1008u, 4u, 0u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x400b100cu, 4u, 17u << 24u));
    TEST_EXPECT(state, read32(large, 0x400b1020u, &response));
    TEST_EXPECT(state, response == 0x03020100u);
    kinetis_k22_sdhc_eject(large);
    TEST_EXPECT(state, !kinetis_k22_sdhc_read_card(large, 0u, &response, 1u));
    kinetis_k22_destroy(large);
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

static void expect_pdb_data_triggers(TestState* state, KinetisK22* device) {
    CortexM4* cpu = kinetis_k22_cpu(device);
    uint32_t gates = 0;
    TEST_EXPECT(state, read32(device, SIM_SCGC6, &gates));
    TEST_EXPECT(state,
                cortex_m4_write_memory(cpu, SIM_SCGC6, 4,
                                       gates | (1u << 22u) | (1u << 27u) | (1u << 31u)));
    TEST_EXPECT(state, kinetis_k22_set_adc_channel(device, 0, 5, 0x345u));
    TEST_EXPECT(state, cpu_write8(device, ADC0_CFG1, 0x04u));
    TEST_EXPECT(state, cpu_write8(device, ADC0_SC1A, 5u));
    TEST_EXPECT(state, cpu_write8(device, ADC0_SC2, 0x40u));

    TEST_EXPECT(state, cpu_write8(device, DAC0_DAT0L, 0x11u));
    TEST_EXPECT(state, cpu_write8(device, DAC0_DAT0L + 1u, 1u));
    TEST_EXPECT(state, cpu_write8(device, DAC0_DAT1L, 0x22u));
    TEST_EXPECT(state, cpu_write8(device, DAC0_DAT1L + 1u, 2u));
    TEST_EXPECT(state, cpu_write8(device, DAC0_C0, 0x80u));
    TEST_EXPECT(state, cpu_write8(device, DAC0_C1, 0x80u));
    TEST_EXPECT(state, cpu_write8(device, DAC0_C2, 1u));

    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PDB_MOD, 4, 31u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PDB_CH0C1, 4, 1u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PDB_CH0DLY0, 4, 1u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PDB_DACINT0, 4, 1u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PDB_DACINTC0, 4, 1u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PDB_SC, 4, 3u));
    kinetis_k22_advance(device, 20u);

    uint16_t adc = 0;
    uint16_t dac = 0;
    TEST_EXPECT(state, read16(device, ADC0_RA, &adc));
    TEST_EXPECT(state, adc == 0x345u);
    TEST_EXPECT(state, kinetis_k22_get_dac_output(device, 0, &dac));
    TEST_EXPECT(state, dac == 0x222u);
}

static void expect_adc_alternate_triggers(TestState* state) {
    KinetisK22* device = create_device(state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    CortexM4* cpu = kinetis_k22_cpu(device);
    uint32_t gates = 0u;
    TEST_EXPECT(state, read32(device, SIM_SCGC6, &gates));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SCGC6, 4u,
                                              gates | (1u << 23u) | (1u << 27u)));
    TEST_EXPECT(state, kinetis_k22_set_adc_channel(device, 0u, 5u, 0x345u));
    TEST_EXPECT(state, kinetis_k22_set_adc_channel(device, 0u, 6u, 0x456u));
    TEST_EXPECT(state, cpu_write8(device, ADC0_CFG1, 0x04u));
    TEST_EXPECT(state, cpu_write8(device, ADC0_SC2, 0x40u));
    TEST_EXPECT(state, cpu_write8(device, ADC0_SC1A, 5u));
    TEST_EXPECT(state, cpu_write8(device, ADC0_SC1B, 6u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PIT_MCR, 4u, 0u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PIT_LDVAL0, 4u, 0u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x85u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 1u));
    kinetis_k22_advance(device, 20u);
    uint8_t status = 0u;
    TEST_EXPECT(state, read8(device, ADC0_SC1A, &status));
    TEST_EXPECT(state, (status & 0x80u) == 0u);

    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x84u));
    kinetis_k22_advance(device, 20u);
    TEST_EXPECT(state, read8(device, ADC0_SC1A, &status));
    TEST_EXPECT(state, (status & 0x80u) != 0u);
    uint16_t result = 0u;
    TEST_EXPECT(state, read16(device, ADC0_RA, &result));
    TEST_EXPECT(state, result == 0x345u);

    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x94u));
    kinetis_k22_advance(device, 20u);
    TEST_EXPECT(state, read8(device, ADC0_SC1B, &status));
    TEST_EXPECT(state, (status & 0x80u) != 0u);
    TEST_EXPECT(state, read16(device, ADC0_RB, &result));
    TEST_EXPECT(state, result == 0x456u);
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 0u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PIT_TFLG0, 4u, 1u));

    uint32_t data_gates = 0u;
    TEST_EXPECT(state, read32(device, SIM_SCGC4, &data_gates));
    TEST_EXPECT(state,
                cortex_m4_write_memory(cpu, SIM_SCGC4, 4u, data_gates | (1u << 19u)));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x81u));
    TEST_EXPECT(state, cpu_write8(device, ADC0_SC1A, 5u));
    TEST_EXPECT(state, kinetis_k22_set_cmp_input(device, 0u, 1u, 10u));
    TEST_EXPECT(state, kinetis_k22_set_cmp_input(device, 0u, 2u, 20u));
    TEST_EXPECT(state, cpu_write8(device, CMP0_MUXCR, 0x0au));
    TEST_EXPECT(state, cpu_write8(device, CMP0_CR1, 1u));
    TEST_EXPECT(state, kinetis_k22_set_cmp_input(device, 0u, 1u, 30u));
    kinetis_k22_advance(device, 20u);
    TEST_EXPECT(state, read16(device, ADC0_RA, &result));
    TEST_EXPECT(state, result == 0x345u);

    TEST_EXPECT(state, read32(device, SIM_SCGC5, &gates));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SCGC5, 4u, gates | 1u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x8eu));
    TEST_EXPECT(state, cpu_write8(device, ADC0_SC1A, 6u));
    TEST_EXPECT(state, kinetis_k22_set_lptmr_input(device, 0u, false));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, LPTMR_PSR, 4u, 4u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, LPTMR_CMR, 4u, 0u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, LPTMR_CSR, 4u, 3u));
    TEST_EXPECT(state, kinetis_k22_set_lptmr_input(device, 0u, true));
    kinetis_k22_advance(device, 20u);
    TEST_EXPECT(state, read16(device, ADC0_RA, &result));
    TEST_EXPECT(state, result == 0x456u);

    TEST_EXPECT(state, read32(device, SIM_SCGC6, &gates));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SCGC6, 4u, gates | (1u << 24u)));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x88u));
    TEST_EXPECT(state, cpu_write8(device, ADC0_SC1A, 5u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, FTM0_MOD, 4u, 3u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, FTM0_C0V, 4u, 2u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, FTM0_C0SC, 4u, 0x10u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, FTM0_EXTTRIG, 4u, 0x10u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, FTM0_SC, 4u, 0x08u));
    kinetis_k22_advance(device, 20u);
    TEST_EXPECT(state, read16(device, ADC0_RA, &result));
    TEST_EXPECT(state, result == 0x345u);

    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 0u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PIT_LDVAL0, 4u, 5u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PIT_MCR, 4u, 1u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 1u));
    kinetis_k22_advance(device, 2u);
    uint32_t counter = 0u;
    TEST_EXPECT(state, read32(device, PIT_LDVAL0 + 4u, &counter));
    TEST_EXPECT(state, counter == 3u);
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, DHCSR, 4u, 0xa05f0003u));
    kinetis_k22_advance(device, 10u);
    TEST_EXPECT(state, read32(device, PIT_LDVAL0 + 4u, &counter));
    TEST_EXPECT(state, counter == 3u);
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, DHCSR, 4u, 0xa05f0001u));
    kinetis_k22_advance(device, 1u);
    TEST_EXPECT(state, read32(device, PIT_LDVAL0 + 4u, &counter));
    TEST_EXPECT(state, counter == 2u);
    kinetis_k22_destroy(device);
}

static void expect_lptmr_pulse_input(TestState* state, KinetisK22* device) {
    CortexM4* cpu = kinetis_k22_cpu(device);
    uint32_t gates = 0u;
    TEST_EXPECT(state, read32(device, SIM_SCGC5, &gates));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SCGC5, 4u, gates | 1u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, LPTMR_PSR, 4u, 4u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, LPTMR_CMR, 4u, 0u));
    TEST_EXPECT(state, kinetis_k22_set_lptmr_input(device, 1u, false));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, LPTMR_CSR, 4u, 0x53u));
    TEST_EXPECT(state, kinetis_k22_set_lptmr_input(device, 0u, true));
    TEST_EXPECT(state, kinetis_k22_set_lptmr_input(device, 1u, false));
    TEST_EXPECT(state, kinetis_k22_set_lptmr_input(device, 1u, true));
    uint32_t value = 0u;
    TEST_EXPECT(state, read32(device, LPTMR_CSR, &value));
    TEST_EXPECT(state, value == 0xd3u);
    TEST_EXPECT(state, cortex_m4_get_irq_pending(cpu, 58u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, LPTMR_CNR, 4u, 0u));
    TEST_EXPECT(state, read32(device, LPTMR_CNR, &value));
    TEST_EXPECT(state, value == 0u);
    TEST_EXPECT(state, !kinetis_k22_set_lptmr_input(device, 3u, false));

    TEST_EXPECT(state, cortex_m4_write_memory(cpu, LPTMR_CSR, 4u, 0u));
    uint32_t data_gates = 0u;
    TEST_EXPECT(state, read32(device, SIM_SCGC4, &data_gates));
    TEST_EXPECT(state,
                cortex_m4_write_memory(cpu, SIM_SCGC4, 4u, data_gates | (1u << 19u)));
    TEST_EXPECT(state, kinetis_k22_set_cmp_input(device, 0u, 1u, 10u));
    TEST_EXPECT(state, kinetis_k22_set_cmp_input(device, 0u, 2u, 20u));
    TEST_EXPECT(state, cpu_write8(device, CMP0_MUXCR, 0x0au));
    TEST_EXPECT(state, cpu_write8(device, CMP0_CR1, 1u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, LPTMR_PSR, 4u, 4u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, LPTMR_CMR, 4u, 0u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, LPTMR_CSR, 4u, 0x43u));
    TEST_EXPECT(state, kinetis_k22_set_cmp_input(device, 0u, 2u, 20u));
    TEST_EXPECT(state, kinetis_k22_set_cmp_input(device, 0u, 1u, 30u));
    TEST_EXPECT(state, read32(device, LPTMR_CSR, &value));
    TEST_EXPECT(state, value == 0xc3u);
}

static void expect_flexbus_integration(TestState* state, KinetisK22* device) {
    uint8_t memory[256];
    for (size_t index = 0u; index < sizeof(memory); index++)
        memory[index] = (uint8_t)(0x80u + index);
    TEST_EXPECT(state, kinetis_k22_flexbus_attach(device, 0x60000000u, memory,
                                                  sizeof(memory), false));
    CortexM4* cpu = kinetis_k22_cpu(device);
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, SIM_SCGC7, 4u, 1u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x4000c000u, 4u, 0x60000000u));
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x4000c004u, 4u, 1u));
    uint32_t value = 0u;
    TEST_EXPECT(state, cortex_m4_read_memory(cpu, 0x60000004u, 4u, &value));
    TEST_EXPECT(state, value == 0x87868584u);
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, 0x60000008u, 4u, 0x12345678u));
    TEST_EXPECT(state, kinetis_k22_flexbus_read(device, 8u, &value, sizeof(value)));
    TEST_EXPECT(state, value == 0x12345678u);
    KinetisK22Event event;
    bool saw_read = false;
    bool saw_write = false;
    while (kinetis_k22_next_event(device, &event)) {
        if (event.type == KINETIS_K22_EVENT_FLEXBUS_TRANSFER) {
            saw_read |= event.auxiliary == 4u;
            saw_write |= event.auxiliary == 0x104u && event.value == 0x12345678u;
        }
    }
    TEST_EXPECT(state, saw_read);
    TEST_EXPECT(state, saw_write);
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
    kinetis_k22_advance(device, k22_test_core_cycles_for_bus_cycles(device, 260u));
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

    TEST_EXPECT(state, write16(device, WDOG_UNLOCK, 0xc520u));
    TEST_EXPECT(state, write16(device, WDOG_UNLOCK, 0xd928u));
    TEST_EXPECT(state, write16(device, WDOG_TOVALH, 0u));
    TEST_EXPECT(state, write16(device, WDOG_TOVALL, 1u));
    TEST_EXPECT(state, write16(device, WDOG_STCTRLH, 5u));
    kinetis_k22_advance(device, k22_test_core_cycles_for_bus_cycles(device, 260u));
    kinetis_k22_watchdog_advance(device, 1u);
    TEST_EXPECT(state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), 22u));
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
    expect_package_serial_extensions(&state);
    expect_can_irq_level(&state);
    expect_sdhc_integration(&state);
    expect_fmc_cache(&state);
    KinetisK22* device = create_device(&state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    TEST_EXPECT(&state, kinetis_k22_reset(device));
    TEST_EXPECT(&state, kinetis_k22_core_clock_hz(device) == 20971520u);
    TEST_EXPECT(&state, kinetis_k22_bus_clock_hz(device) == 20971520u);
    expect_manifest_fallback(&state, device);
    expect_integrated_flash_command(&state, device);
    expect_io_irq_levels(&state, device);
    expect_clock_gates(&state, device);
    expect_endpoint_event_order(&state, device);
    expect_pdb_data_triggers(&state, device);
    expect_adc_alternate_triggers(&state);
    expect_lptmr_pulse_input(&state, device);
    expect_flexbus_integration(&state, device);
    expect_reset_domains(&state, device);
    expect_copy(&state, device);
    KinetisK22Event event;
    while (kinetis_k22_next_event(device, &event)) {
    }
    TEST_EXPECT(&state, !kinetis_k22_next_event(device, &event));
    kinetis_k22_destroy(device);
    expect_memory_domains(&state);
    return test_finish(&state);
}
