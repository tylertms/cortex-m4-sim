#include "kinetis_k22.h"

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
    DMA_ERQ = 0x4000800cu,
    DMA_HRS = 0x40008034u,
    DMA_TCD0 = 0x40009000u,
    DMAMUX_CHCFG0 = 0x40021000u,
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
    expect(state, device != NULL, "device != NULL");
    const uint32_t vectors[2] = {0x20001000u, 0x00000101u};
    const uint16_t program = 0xbe00u;
    expect(state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0, vectors, sizeof(vectors))");
    expect(state, kinetis_k22_load(device, 0x100, &program, sizeof(program)),
           "kinetis_k22_load(device, 0x100, &program, sizeof(program))");
    return device;
}

static KinetisK22* create_f12_device(TestState* state, KinetisK22Package package) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.profile = KINETIS_K22_PROFILE_MK22FN1M012;
    configuration.package = package;
    configuration.flash_size = 1024u * 1024u;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(state, device != NULL, "device != NULL");
    return device;
}

static void expect_package_selection(TestState* state) {
    KinetisK22Configuration invalid_profile = kinetis_k22_default_configuration();
    invalid_profile.profile = KINETIS_K22_PROFILE_COUNT;
    expect(state, kinetis_k22_create(invalid_profile) == NULL,
           "kinetis_k22_create(invalid_profile) == NULL");

    KinetisK22Configuration invalid = kinetis_k22_default_configuration();
    invalid.package = KINETIS_K22_PACKAGE_AH_64_WLCSP;
    expect(state, kinetis_k22_create(invalid) == NULL,
           "kinetis_k22_create(invalid) == NULL");

    KinetisK22* small = create_device(state, KINETIS_K22_PACKAGE_LH_64_LQFP);
    uint16_t value = 0;
    uint8_t register_value = 0;
    expect(state,
           kinetis_k22_read(small, DAC1_DAT0L, &register_value, sizeof(register_value)),
           "kinetis_k22_read(small, DAC1_DAT0L, &register_value, sizeof(register_value))");
    expect(state, kinetis_k22_get_dac_output(small, 1, &value),
           "kinetis_k22_get_dac_output(small, 1, &value)");
    kinetis_k22_gpio_drive(small, 4, 31, true);
    uint32_t input = UINT32_MAX;
    expect(state, read32(small, GPIOA_PDIR + 4u * 0x40u, &input),
           "read32(small, GPIOA_PDIR + 4u * 0x40u, &input)");
    expect(state, input == 0, "input == 0");
    kinetis_k22_destroy(small);

    KinetisK22* large = create_device(state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    expect(state,
           kinetis_k22_read(large, DAC1_DAT0L, &register_value, sizeof(register_value)),
           "kinetis_k22_read(large, DAC1_DAT0L, &register_value, sizeof(register_value))");
    kinetis_k22_destroy(large);

    KinetisK22* limited = create_device(state, KINETIS_K22_PACKAGE_FX_88_HVQFN);
    expect(
        state,
        !kinetis_k22_read(limited, DAC1_DAT0L, &register_value, sizeof(register_value)),
        "!kinetis_k22_read(limited, DAC1_DAT0L, &register_value, sizeof(register_value))");
    expect(state, !kinetis_k22_get_dac_output(limited, 1, &value),
           "!kinetis_k22_get_dac_output(limited, 1, &value)");
    kinetis_k22_destroy(limited);
}

static void expect_integrated_flash_command(TestState* state, KinetisK22* device) {
    const uint32_t target = 0x00001000u;
    uint32_t value = 0u;
    expect(state, read32(device, target, &value), "read32(device, target, &value)");
    expect(state, value == UINT32_MAX, "value == UINT32_MAX");
    const uint8_t command[8] = {0x06u, 0x00u, 0x10u, 0x00u, 0x78u, 0x56u, 0x34u, 0x12u};
    for (uint8_t index = 0u; index < sizeof(command); index++)
        expect(state, cpu_write8(device, flash_fccob_address(index), command[index]),
               "cpu_write8(device, flash_fccob_address(index), command[index])");
    expect(state, cpu_write8(device, FTFA_FSTAT, 0x80u),
           "cpu_write8(device, FTFA_FSTAT, 0x80u)");
    expect(state, read32(device, target, &value), "read32(device, target, &value)");
    expect(state, value == UINT32_MAX, "value == UINT32_MAX");
    const uint32_t fstat_bit_band =
        0x42000000u + (FTFA_FSTAT - 0x40000000u) * 32u + 6u * 4u;
    expect(state, write32(device, fstat_bit_band, 1u),
           "write32(device, fstat_bit_band, 1u)");
    kinetis_k22_advance(device, 40u);
    expect(state, read32(device, target, &value), "read32(device, target, &value)");
    expect(state, value == 0x12345678u, "value == 0x12345678u");
}

static void launch_swap_command(TestState* state, KinetisK22* device, uint32_t address,
                                uint8_t control) {
    const uint8_t command[5] = {0x46u, (uint8_t)(address >> 16u), (uint8_t)(address >> 8u),
                                (uint8_t)address, control};
    for (uint8_t index = 0u; index < sizeof(command); index++)
        expect(state, cpu_write8(device, flash_fccob_address(index), command[index]),
               "cpu_write8(device, flash_fccob_address(index), command[index])");
    expect(state, cpu_write8(device, FTFA_FSTAT, 0x80u),
           "cpu_write8(device, FTFA_FSTAT, 0x80u)");
    kinetis_k22_advance(device, 40u);
}

static void expect_integrated_flash_swap(TestState* state) {
    KinetisK22* device = create_f12_device(state, KINETIS_K22_PACKAGE_MC_121_MAPBGA);
    uint8_t lower = 0x11u;
    uint8_t upper = 0x22u;
    expect(state, kinetis_k22_write(device, 0x20u, &lower, sizeof(lower)),
           "kinetis_k22_write(device, 0x20u, &lower, sizeof(lower))");
    expect(state, kinetis_k22_write(device, 0x80020u, &upper, sizeof(upper)),
           "kinetis_k22_write(device, 0x80020u, &upper, sizeof(upper))");
    launch_swap_command(state, device, 0x1000u, 1u);
    launch_swap_command(state, device, 0x1000u, 4u);
    expect(state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    uint8_t value = 0u;
    expect(state, read8(device, 0x20u, &value), "read8(device, 0x20u, &value)");
    expect(state, value == upper, "value == upper");
    expect(state, read8(device, 0x80020u, &value), "read8(device, 0x80020u, &value)");
    expect(state, value == lower, "value == lower");
    expect(state, read8(device, FTFA_FSTAT + 1u, &value),
           "read8(device, FTFA_FSTAT + 1u, &value)");
    expect(state, (value & 8u) != 0u, "(value & 8u) != 0u");
    kinetis_k22_destroy(device);
}

static void expect_io_irq_levels(TestState* state, KinetisK22* device) {
    expect(state, write32(device, SIM_SCGC5, 1u << 12u),
           "write32(device, SIM_SCGC5, 1u << 12u)");
    expect(state, write32(device, PORTD_PCR0, 9u << 16u),
           "write32(device, PORTD_PCR0, 9u << 16u)");
    kinetis_k22_gpio_drive(device, 3u, 0u, false);
    kinetis_k22_gpio_drive(device, 3u, 0u, true);
    expect(state, irq_level(device, 62u), "irq_level(device, 62u)");
    expect(state, write32(device, PORTD_ISFR, 1u), "write32(device, PORTD_ISFR, 1u)");
    expect(state, !irq_level(device, 62u), "!irq_level(device, 62u)");

    expect(state, write32(device, SIM_SCGC4, 1u << 18u),
           "write32(device, SIM_SCGC4, 1u << 18u)");
    expect(state, write8(device, USB0_INTEN, 1u << 3u),
           "write8(device, USB0_INTEN, 1u << 3u)");
    expect(state, write8(device, USB0_ENDPT3, 1u), "write8(device, USB0_ENDPT3, 1u)");
    expect(state, kinetis_k22_usb_token(device, 3u, 0x69u, false),
           "kinetis_k22_usb_token(device, 3u, 0x69u, false)");
    expect(state, irq_level(device, 53u), "irq_level(device, 53u)");
    expect(state, write8(device, USB0_ISTAT, 1u << 3u),
           "write8(device, USB0_ISTAT, 1u << 3u)");
    expect(state, !irq_level(device, 53u), "!irq_level(device, 53u)");

    expect(state, write32(device, SIM_SCGC6, 1u << 15u),
           "write32(device, SIM_SCGC6, 1u << 15u)");
    expect(state, write32(device, I2S0_TCSR, UINT32_C(0x80000100)),
           "write32(device, I2S0_TCSR, UINT32_C(0x80000100))");
    expect(state, write32(device, I2S0_TDR0, 0x12345678u),
           "write32(device, I2S0_TDR0, 0x12345678u)");
    expect(state, irq_level(device, 28u), "irq_level(device, 28u)");
    expect(state, write32(device, I2S0_TCSR, UINT32_C(0x80000000)),
           "write32(device, I2S0_TCSR, UINT32_C(0x80000000))");
    expect(state, !irq_level(device, 28u), "!irq_level(device, 28u)");
    expect(state, write32(device, I2S0_RCSR, UINT32_C(0x80000100)),
           "write32(device, I2S0_RCSR, UINT32_C(0x80000100))");
    expect(state, kinetis_k22_i2s_receive(device, 0x87654321u),
           "kinetis_k22_i2s_receive(device, 0x87654321u)");
    expect(state, irq_level(device, 29u), "irq_level(device, 29u)");
    expect(state, write32(device, I2S0_RCSR, UINT32_C(0x80000000)),
           "write32(device, I2S0_RCSR, UINT32_C(0x80000000))");
    expect(state, !irq_level(device, 29u), "!irq_level(device, 29u)");
}

static void expect_can_irq_level(TestState* state) {
    KinetisK22* device = create_f12_device(state, KINETIS_K22_PACKAGE_MD_144_MAPBGA);
    expect(state, write32(device, SIM_SCGC6, 1u << 4u),
           "write32(device, SIM_SCGC6, 1u << 4u)");
    expect(state, write32(device, CAN0_MCR, 0x0fu), "write32(device, CAN0_MCR, 0x0fu)");
    expect(state, write32(device, CAN0_CTRL1, 0u), "write32(device, CAN0_CTRL1, 0u)");
    expect(state, write32(device, CAN0_IMASK1, 1u), "write32(device, CAN0_IMASK1, 1u)");
    expect(state, write32(device, CAN0_MB0_CS, 4u << 24u),
           "write32(device, CAN0_MB0_CS, 4u << 24u)");
    const KinetisK22CanFrame frame = {
        .identifier = 0x123u,
        .length = 8u,
        .data = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u},
    };
    expect(state, kinetis_k22_can_receive(device, &frame),
           "kinetis_k22_can_receive(device, &frame)");
    expect(state, irq_level(device, 75u), "irq_level(device, 75u)");
    expect(state, write32(device, CAN0_IFLAG1, 1u), "write32(device, CAN0_IFLAG1, 1u)");
    expect(state, !irq_level(device, 75u), "!irq_level(device, 75u)");
    kinetis_k22_destroy(device);
}

static void expect_memory_domains(TestState* state) {
    KinetisK22* device = create_f12_device(state, KINETIS_K22_PACKAGE_LQ_144_LQFP);
    CortexM4* cpu = kinetis_k22_cpu(device);
    const CortexM4* constant_cpu = kinetis_k22_cpu_const(device);
    expect(state, constant_cpu == cpu, "constant_cpu == cpu");
    expect(state, kinetis_k22_cpu_const(NULL) == NULL,
           "kinetis_k22_cpu_const(NULL) == NULL");

    const uint32_t flexram = 0x14000000u;
    uint32_t value = 0x12345678u;
    expect(state, kinetis_k22_write(device, flexram, &value, sizeof(value)),
           "kinetis_k22_write(device, flexram, &value, sizeof(value))");
    value = 0u;
    expect(state, cortex_m4_read_memory(cpu, flexram, 4u, &value),
           "cortex_m4_read_memory(cpu, flexram, 4u, &value)");
    expect(state, value == 0x12345678u, "value == 0x12345678u");
    expect(state, cortex_m4_write_memory(cpu, flexram + 4u, 4u, 0xa5a55a5au),
           "cortex_m4_write_memory(cpu, flexram + 4u, 4u, 0xa5a55a5au)");
    expect(state, read32(device, flexram + 4u, &value),
           "read32(device, flexram + 4u, &value)");
    expect(state, value == 0xa5a55a5au, "value == 0xa5a55a5au");

    const uint32_t flash = 0x100u;
    expect(state,
           !kinetis_k22_memory_write(device, flash, 1u, CORTEX_M4_ACCESS_DATA, 0x5au),
           "!kinetis_k22_memory_write(device, flash, 1u, CORTEX_M4_ACCESS_DATA, 0x5au)");
    uint8_t byte = 0x5au;
    expect(state, kinetis_k22_write(device, flash, &byte, sizeof(byte)),
           "kinetis_k22_write(device, flash, &byte, sizeof(byte))");
    byte = 0u;
    expect(state, read8(device, flash, &byte), "read8(device, flash, &byte)");
    expect(state, byte == 0x5au, "byte == 0x5au");
    kinetis_k22_destroy(device);

    KinetisK22Configuration small = kinetis_k22_default_configuration();
    small.flash_size = 8u;
    KinetisK22* short_flash = kinetis_k22_create(small);
    expect(state, short_flash != NULL, "short_flash != NULL");
    expect(state, kinetis_k22_reset(short_flash), "kinetis_k22_reset(short_flash)");
    kinetis_k22_warm_reset(NULL, 0u, 0u);
    kinetis_k22_destroy(short_flash);
}

static void expect_manifest_fallback(TestState* state, KinetisK22* device) {
    uint32_t value = 0;
    expect(state, read32(device, FMC_PFAPR, &value), "read32(device, FMC_PFAPR, &value)");
    expect(state, value == 0x00f8003fu, "value == 0x00f8003fu");
    expect(state, write32(device, FMC_PFAPR, UINT32_MAX),
           "write32(device, FMC_PFAPR, UINT32_MAX)");
    expect(state, read32(device, FMC_PFAPR, &value), "read32(device, FMC_PFAPR, &value)");
    expect(state, value == 0x00ffffffu, "value == 0x00ffffffu");
    expect(state, !read32(device, FMC_PFAPR + 0x0cu, &value),
           "!read32(device, FMC_PFAPR + 0x0cu, &value)");
    expect(state, !write32(device, FMC_PFAPR + 0x0cu, UINT32_MAX),
           "!write32(device, FMC_PFAPR + 0x0cu, UINT32_MAX)");
    expect(state,
           !kinetis_k22_peripheral_write(device, FMC_PFAPR, 4u,
                                         CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, 0u),
           "!kinetis_k22_peripheral_write( device, FMC_PFAPR, 4u, "
           "CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, 0u)");

    uint8_t flash = 0x5au;
    expect(state, kinetis_k22_write(device, 0x100u, &flash, sizeof(flash)),
           "kinetis_k22_write(device, 0x100u, &flash, sizeof(flash))");
    expect(state, write32(device, FMC_PFAPR, 0x10u), "write32(device, FMC_PFAPR, 0x10u)");
    expect(state,
           !kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "!kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, kinetis_k22_dma_read(device, 0x100u, 1u, &value),
           "kinetis_k22_dma_read(device, 0x100u, 1u, &value)");
    expect(state, value == 0x5au, "value == 0x5au");
    expect(state, write32(device, FMC_PFAPR, 4u), "write32(device, FMC_PFAPR, 4u)");
    expect(state, !kinetis_k22_dma_read(device, 0x100u, 1u, &value),
           "!kinetis_k22_dma_read(device, 0x100u, 1u, &value)");
    expect(state,
           kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, value == 0x5au, "value == 0x5au");
    expect(state, write32(device, FMC_PFAPR, UINT32_MAX),
           "write32(device, FMC_PFAPR, UINT32_MAX)");
    expect(state, read32(device, FMC_PFB0CR, &value), "read32(device, FMC_PFB0CR, &value)");
    expect(state, write32(device, FMC_PFB0CR, (value & ~0x18u) | 0x00f00000u),
           "write32(device, FMC_PFB0CR, (value & ~0x18u) | 0x00f00000u)");

    const uint32_t alias = 0x42000000u + (FMC_TAGVDW0S0 - 0x40000000u) * 32u + 5u * 4u;
    expect(state, write32(device, alias, 1), "write32(device, alias, 1)");
    expect(state, read32(device, FMC_TAGVDW0S0, &value),
           "read32(device, FMC_TAGVDW0S0, &value)");
    expect(state, value == 0x20u, "value == 0x20u");
    expect(state, read32(device, alias, &value), "read32(device, alias, &value)");
    expect(state, value == 1, "value == 1");
    expect(state, write32(device, FMC_TAGVDW0S0, 0x21u),
           "write32(device, FMC_TAGVDW0S0, 0x21u)");
    expect(state, write32(device, FMC_TAGVDW1S0, 0x41u),
           "write32(device, FMC_TAGVDW1S0, 0x41u)");
    expect(state, write32(device, FMC_DATAW0S0UM, 0x12345678u),
           "write32(device, FMC_DATAW0S0UM, 0x12345678u)");
    expect(state, write32(device, FMC_DATAW1S0UM, 0x89abcdefu),
           "write32(device, FMC_DATAW1S0UM, 0x89abcdefu)");
    expect(state, write32(device, FMC_PFB0CR, (1u << 19u) | (1u << 20u)),
           "write32(device, FMC_PFB0CR, (1u << 19u) | (1u << 20u))");
    expect(state, read32(device, FMC_TAGVDW0S0, &value),
           "read32(device, FMC_TAGVDW0S0, &value)");
    expect(state, value == 0u, "value == 0u");
    expect(state, read32(device, FMC_TAGVDW1S0, &value),
           "read32(device, FMC_TAGVDW1S0, &value)");
    expect(state, value == 0x41u, "value == 0x41u");
    expect(state, read32(device, FMC_DATAW0S0UM, &value),
           "read32(device, FMC_DATAW0S0UM, &value)");
    expect(state, value == 0u, "value == 0u");
    expect(state, read32(device, FMC_DATAW1S0UM, &value),
           "read32(device, FMC_DATAW1S0UM, &value)");
    expect(state, value == 0x89abcdefu, "value == 0x89abcdefu");
    expect(state, read32(device, FMC_PFB0CR, &value), "read32(device, FMC_PFB0CR, &value)");
    expect(state, (value & 0x00f80000u) == 0u, "(value & 0x00f80000u) == 0u");
    expect(state, write32(device, FMC_PFB0CR, 1u << 21u),
           "write32(device, FMC_PFB0CR, 1u << 21u)");
    expect(state, read32(device, FMC_TAGVDW1S0, &value),
           "read32(device, FMC_TAGVDW1S0, &value)");
    expect(state, value == 0u, "value == 0u");
    expect(state, read32(device, FMC_DATAW1S0UM, &value),
           "read32(device, FMC_DATAW1S0UM, &value)");
    expect(state, value == 0u, "value == 0u");
    expect(state, !read32(device, 0x43ffffffu, &value),
           "!read32(device, 0x43ffffffu, &value)");

    expect(state, read32(device, MCM_PLASC, &value), "read32(device, MCM_PLASC, &value)");
    expect(state, value == 0x0017001fu, "value == 0x0017001fu");
    expect(state, !write32(device, MCM_PLASC, UINT32_MAX),
           "!write32(device, MCM_PLASC, UINT32_MAX)");
    expect(state, read32(device, MCM_PLASC, &value), "read32(device, MCM_PLASC, &value)");
    expect(state, value == 0x0017001fu, "value == 0x0017001fu");
}

static void expect_fmc_cache(TestState* state) {
    KinetisK22* device = create_device(state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    expect(state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    uint32_t value = 0u;
    expect(state, read32(device, FMC_PFB0CR, &value), "read32(device, FMC_PFB0CR, &value)");
    expect(state, write32(device, FMC_PFB0CR, value | 0x0ef00000u),
           "write32(device, FMC_PFB0CR, value | 0x0ef00000u)");
    uint8_t byte = 0x5au;
    expect(state, kinetis_k22_write(device, 0x100u, &byte, sizeof(byte)),
           "kinetis_k22_write(device, 0x100u, &byte, sizeof(byte))");
    expect(state,
           kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, value == 0x5au, "value == 0x5au");
    expect(state, read32(device, FMC_TAGVDW0S0, &value),
           "read32(device, FMC_TAGVDW0S0, &value)");
    expect(state, (value & 1u) != 0u, "(value & 1u) != 0u");
    expect(state, read32(device, FMC_DATAW0S0LM, &value),
           "read32(device, FMC_DATAW0S0LM, &value)");
    expect(state, (value & 0xffu) == 0x5au, "(value & 0xffu) == 0x5au");
    expect(state, kinetis_k22_flash_controller_write(device, 0x100u, 1u, 0xa5u),
           "kinetis_k22_flash_controller_write(device, 0x100u, 1u, 0xa5u)");
    expect(state,
           kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, value == 0x5au, "value == 0x5au");
    expect(state, read32(device, FMC_PFB0CR, &value), "read32(device, FMC_PFB0CR, &value)");
    expect(state, write32(device, FMC_PFB0CR, value | (1u << 20u)),
           "write32(device, FMC_PFB0CR, value | (1u << 20u))");
    expect(state,
           kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, value == 0xa5u, "value == 0xa5u");
    kinetis_k22_destroy(device);
}

static void expect_fmc_invalidation_and_locking(TestState* state) {
    KinetisK22* device = create_device(state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    expect(state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    uint8_t byte = 0x5au;
    uint32_t control = 0u;
    uint32_t tag = 0u;
    uint32_t value = 0u;
    expect(state, kinetis_k22_write(device, 0x100u, &byte, sizeof(byte)),
           "kinetis_k22_write(device, 0x100u, &byte, sizeof(byte))");
    expect(state, read32(device, FMC_PFB0CR, &control),
           "read32(device, FMC_PFB0CR, &control)");
    expect(state, write32(device, FMC_PFB0CR, control | 0x0ef00000u),
           "write32(device, FMC_PFB0CR, control | 0x0ef00000u)");
    expect(state,
           kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, read32(device, FMC_TAGVDW0S0, &tag),
           "read32(device, FMC_TAGVDW0S0, &tag)");
    expect(state, write32(device, FMC_TAGVDW1S0, tag),
           "write32(device, FMC_TAGVDW1S0, tag)");
    device->fmc_bank[1][0] = 0u;
    expect(state,
           kinetis_k22_memory_write(device, 0x100u, 1u, CORTEX_M4_ACCESS_DEBUG, 0xa5u),
           "kinetis_k22_memory_write(device, 0x100u, 1u, CORTEX_M4_ACCESS_DEBUG, 0xa5u)");
    expect(state, read32(device, FMC_TAGVDW0S0, &value),
           "read32(device, FMC_TAGVDW0S0, &value)");
    expect(state, value == 0u, "value == 0u");
    expect(state, read32(device, FMC_TAGVDW1S0, &value),
           "read32(device, FMC_TAGVDW1S0, &value)");
    expect(state, value == 0u, "value == 0u");

    expect(state, write32(device, FMC_PFB0CR, control | 0x0ff00000u),
           "write32(device, FMC_PFB0CR, control | 0x0ff00000u)");
    expect(state,
           kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, value == 0xa5u, "value == 0xa5u");
    expect(state, read32(device, FMC_TAGVDW0S0, &value),
           "read32(device, FMC_TAGVDW0S0, &value)");
    expect(state, value == 0u, "value == 0u");
    kinetis_k22_destroy(device);
}

static void expect_clock_gates(TestState* state, KinetisK22* device) {
    uint32_t value = 0;
    uint8_t status = 0;
    CortexM4* cpu = kinetis_k22_cpu(device);
    expect(state, read32(device, SIM_SCGC4, &value), "read32(device, SIM_SCGC4, &value)");
    expect(state, (value & (1u << 11)) == 0, "(value & (1u << 11)) == 0");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC4, 4, 0xf0100830u),
           "cortex_m4_write_memory(cpu, SIM_SCGC4, 4, 0xf0100830u)");
    expect(state, cortex_m4_write_memory(cpu, UART1_C2, 1, 0x24u),
           "cortex_m4_write_memory(cpu, UART1_C2, 1, 0x24u)");
    expect(state, kinetis_k22_serial_receive(device, KINETIS_K22_SERIAL_UART1, 0x5au, 0),
           "kinetis_k22_serial_receive(device, KINETIS_K22_SERIAL_UART1, 0x5au, 0)");
    expect(state, read8(device, UART1_S1, &status), "read8(device, UART1_S1, &status)");
    expect(state, (status & 0x20u) != 0, "(status & 0x20u) != 0");
    expect(state, cortex_m4_get_irq_pending(cpu, 33), "cortex_m4_get_irq_pending(cpu, 33)");
}

static void expect_package_serial_extensions(TestState* state) {
    KinetisK22* small = create_f12_device(state, KINETIS_K22_PACKAGE_LH_64_LQFP);
    expect(state, !kinetis_k22_serial_receive(small, KINETIS_K22_SERIAL_UART3, 0x11u, 0u),
           "!kinetis_k22_serial_receive(small, KINETIS_K22_SERIAL_UART3, 0x11u, 0u)");
    expect(state, !kinetis_k22_serial_receive(small, KINETIS_K22_SERIAL_SPI1, 0x22u, 0u),
           "!kinetis_k22_serial_receive(small, KINETIS_K22_SERIAL_SPI1, 0x22u, 0u)");
    kinetis_k22_destroy(small);

    KinetisK22* large = create_f12_device(state, KINETIS_K22_PACKAGE_MD_144_MAPBGA);
    CortexM4* cpu = kinetis_k22_cpu(large);
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC1, 4u, 3u << 10u),
           "cortex_m4_write_memory(cpu, SIM_SCGC1, 4u, 3u << 10u)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC3, 4u, 1u << 12u),
           "cortex_m4_write_memory(cpu, SIM_SCGC3, 4u, 1u << 12u)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC4, 4u, 1u << 13u),
           "cortex_m4_write_memory(cpu, SIM_SCGC4, 4u, 1u << 13u)");
    expect(state, cortex_m4_write_memory(cpu, 0x4006d003u, 1u, 0x24u),
           "cortex_m4_write_memory(cpu, 0x4006d003u, 1u, 0x24u)");
    expect(state, cortex_m4_write_memory(cpu, 0x400ea003u, 1u, 0x24u),
           "cortex_m4_write_memory(cpu, 0x400ea003u, 1u, 0x24u)");
    expect(state, cortex_m4_write_memory(cpu, 0x400eb003u, 1u, 0x24u),
           "cortex_m4_write_memory(cpu, 0x400eb003u, 1u, 0x24u)");
    expect(state, kinetis_k22_serial_receive(large, KINETIS_K22_SERIAL_UART3, 0x33u, 0u),
           "kinetis_k22_serial_receive(large, KINETIS_K22_SERIAL_UART3, 0x33u, 0u)");
    expect(state, kinetis_k22_serial_receive(large, KINETIS_K22_SERIAL_UART4, 0x44u, 0u),
           "kinetis_k22_serial_receive(large, KINETIS_K22_SERIAL_UART4, 0x44u, 0u)");
    expect(state, kinetis_k22_serial_receive(large, KINETIS_K22_SERIAL_UART5, 0x55u, 0u),
           "kinetis_k22_serial_receive(large, KINETIS_K22_SERIAL_UART5, 0x55u, 0u)");
    expect(state, kinetis_k22_serial_receive(large, KINETIS_K22_SERIAL_SPI2, 0x66u, 0u),
           "kinetis_k22_serial_receive(large, KINETIS_K22_SERIAL_SPI2, 0x66u, 0u)");
    uint8_t value = 0u;
    expect(state, read8(large, 0x4006d007u, &value), "read8(large, 0x4006d007u, &value)");
    expect(state, value == 0x33u, "value == 0x33u");
    expect(state, read8(large, 0x400ea007u, &value), "read8(large, 0x400ea007u, &value)");
    expect(state, value == 0x44u, "value == 0x44u");
    expect(state, read8(large, 0x400eb007u, &value), "read8(large, 0x400eb007u, &value)");
    expect(state, value == 0x55u, "value == 0x55u");
    kinetis_k22_destroy(large);
}

static void expect_serial_dma_sources(TestState* state) {
    KinetisK22* device = create_f12_device(state, KINETIS_K22_PACKAGE_MD_144_MAPBGA);
    CortexM4* cpu = kinetis_k22_cpu(device);
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC7, 4u, 1u << 1u),
           "cortex_m4_write_memory(cpu, SIM_SCGC7, 4u, 1u << 1u)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC6, 4u, 1u << 1u),
           "cortex_m4_write_memory(cpu, SIM_SCGC6, 4u, 1u << 1u)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC1, 4u, 3u << 10u),
           "cortex_m4_write_memory(cpu, SIM_SCGC1, 4u, 3u << 10u)");
    expect(state, cortex_m4_write_memory(cpu, DMA_ERQ, 2u, 3u),
           "cortex_m4_write_memory(cpu, DMA_ERQ, 2u, 3u)");
    expect(state, cortex_m4_write_memory(cpu, DMAMUX_CHCFG0, 1u, 0x80u | 10u),
           "cortex_m4_write_memory(cpu, DMAMUX_CHCFG0, 1u, 0x80u | 10u)");
    expect(state, cortex_m4_write_memory(cpu, DMAMUX_CHCFG0 + 1u, 1u, 0x80u | 11u),
           "cortex_m4_write_memory(cpu, DMAMUX_CHCFG0 + 1u, 1u, 0x80u | 11u)");
    expect(state, cortex_m4_write_memory(cpu, 0x400ea003u, 1u, 0x04u),
           "cortex_m4_write_memory(cpu, 0x400ea003u, 1u, 0x04u)");
    expect(state, cortex_m4_write_memory(cpu, 0x400ea00bu, 1u, 0x20u),
           "cortex_m4_write_memory(cpu, 0x400ea00bu, 1u, 0x20u)");
    expect(state, cortex_m4_write_memory(cpu, 0x400eb003u, 1u, 0x04u),
           "cortex_m4_write_memory(cpu, 0x400eb003u, 1u, 0x04u)");
    expect(state, cortex_m4_write_memory(cpu, 0x400eb00bu, 1u, 0x20u),
           "cortex_m4_write_memory(cpu, 0x400eb00bu, 1u, 0x20u)");
    expect(state, kinetis_k22_serial_receive(device, KINETIS_K22_SERIAL_UART4, 0x44u, 0u),
           "kinetis_k22_serial_receive(device, KINETIS_K22_SERIAL_UART4, 0x44u, 0u)");
    uint16_t requests = 0u;
    expect(state, read16(device, DMA_HRS, &requests), "read16(device, DMA_HRS, &requests)");
    expect(state, requests == 1u, "requests == 1u");
    expect(state, kinetis_k22_serial_receive(device, KINETIS_K22_SERIAL_UART5, 0x55u, 0u),
           "kinetis_k22_serial_receive(device, KINETIS_K22_SERIAL_UART5, 0x55u, 0u)");
    expect(state, read16(device, DMA_HRS, &requests), "read16(device, DMA_HRS, &requests)");
    expect(state, requests == 1u, "requests == 1u");
    kinetis_k22_destroy(device);
}

static void expect_periodic_dma_trigger(TestState* state) {
    KinetisK22* device = create_device(state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    CortexM4* cpu = kinetis_k22_cpu(device);
    const uint32_t source = 0x20000080u;
    const uint32_t destination = 0x20000081u;
    expect(state, cortex_m4_write_memory(cpu, source, 1u, 0x5au),
           "cortex_m4_write_memory(cpu, source, 1u, 0x5au)");
    expect(state, cortex_m4_write_memory(cpu, destination, 1u, 0u),
           "cortex_m4_write_memory(cpu, destination, 1u, 0u)");
    uint32_t gates = 0u;
    expect(state, read32(device, SIM_SCGC7, &gates), "read32(device, SIM_SCGC7, &gates)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC7, 4u, gates | 2u),
           "cortex_m4_write_memory(cpu, SIM_SCGC7, 4u, gates | 2u)");
    expect(state, read32(device, SIM_SCGC6, &gates), "read32(device, SIM_SCGC6, &gates)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC6, 4u, gates | 2u | (1u << 23u)),
           "cortex_m4_write_memory(cpu, SIM_SCGC6, 4u, gates | 2u | (1u << 23u))");
    expect(state, cortex_m4_write_memory(cpu, DMA_TCD0, 4u, source),
           "cortex_m4_write_memory(cpu, DMA_TCD0, 4u, source)");
    expect(state, cortex_m4_write_memory(cpu, DMA_TCD0 + 4u, 2u, 0u),
           "cortex_m4_write_memory(cpu, DMA_TCD0 + 4u, 2u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, DMA_TCD0 + 6u, 2u, 0u),
           "cortex_m4_write_memory(cpu, DMA_TCD0 + 6u, 2u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, DMA_TCD0 + 8u, 4u, 1u),
           "cortex_m4_write_memory(cpu, DMA_TCD0 + 8u, 4u, 1u)");
    expect(state, cortex_m4_write_memory(cpu, DMA_TCD0 + 0x0cu, 4u, 0u),
           "cortex_m4_write_memory(cpu, DMA_TCD0 + 0x0cu, 4u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, DMA_TCD0 + 0x10u, 4u, destination),
           "cortex_m4_write_memory(cpu, DMA_TCD0 + 0x10u, 4u, destination)");
    expect(state, cortex_m4_write_memory(cpu, DMA_TCD0 + 0x14u, 2u, 0u),
           "cortex_m4_write_memory(cpu, DMA_TCD0 + 0x14u, 2u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, DMA_TCD0 + 0x16u, 2u, 1u),
           "cortex_m4_write_memory(cpu, DMA_TCD0 + 0x16u, 2u, 1u)");
    expect(state, cortex_m4_write_memory(cpu, DMA_TCD0 + 0x18u, 4u, 0u),
           "cortex_m4_write_memory(cpu, DMA_TCD0 + 0x18u, 4u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, DMA_TCD0 + 0x1cu, 2u, 8u),
           "cortex_m4_write_memory(cpu, DMA_TCD0 + 0x1cu, 2u, 8u)");
    expect(state, cortex_m4_write_memory(cpu, DMA_TCD0 + 0x1eu, 2u, 1u),
           "cortex_m4_write_memory(cpu, DMA_TCD0 + 0x1eu, 2u, 1u)");
    expect(state, cortex_m4_write_memory(cpu, DMAMUX_CHCFG0, 1u, 0xc0u | 60u),
           "cortex_m4_write_memory(cpu, DMAMUX_CHCFG0, 1u, 0xc0u | 60u)");
    expect(state, cortex_m4_write_memory(cpu, DMA_ERQ, 2u, 1u),
           "cortex_m4_write_memory(cpu, DMA_ERQ, 2u, 1u)");
    uint16_t requests = UINT16_MAX;
    expect(state, read16(device, DMA_HRS, &requests), "read16(device, DMA_HRS, &requests)");
    expect(state, (requests & 1u) == 0u, "(requests & 1u) == 0u");
    expect(state, cortex_m4_write_memory(cpu, PIT_MCR, 4u, 0u),
           "cortex_m4_write_memory(cpu, PIT_MCR, 4u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, PIT_LDVAL0, 4u, 0u),
           "cortex_m4_write_memory(cpu, PIT_LDVAL0, 4u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 1u),
           "cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 1u)");
    kinetis_k22_advance(device, k22_test_core_cycles_for_bus_cycles(device, 1u));
    uint32_t value = 0u;
    expect(state, cortex_m4_read_memory(cpu, destination, 1u, &value),
           "cortex_m4_read_memory(cpu, destination, 1u, &value)");
    expect(state, value == 0x5au, "value == 0x5au");
    expect(state, read16(device, DMA_ERQ, &requests), "read16(device, DMA_ERQ, &requests)");
    expect(state, (requests & 1u) == 0u, "(requests & 1u) == 0u");
    kinetis_k22_destroy(device);
}

static void expect_sdhc_integration(TestState* state) {
    uint8_t card[512];
    for (size_t index = 0u; index < sizeof(card); index++)
        card[index] = (uint8_t)index;
    KinetisK22* small = create_f12_device(state, KINETIS_K22_PACKAGE_LH_64_LQFP);
    expect(state, !kinetis_k22_sdhc_insert(small, card, sizeof(card), false),
           "!kinetis_k22_sdhc_insert(small, card, sizeof(card), false)");
    kinetis_k22_destroy(small);

    KinetisK22* large = create_f12_device(state, KINETIS_K22_PACKAGE_LK_80_LQFP);
    expect(state, kinetis_k22_sdhc_insert(large, card, sizeof(card), false),
           "kinetis_k22_sdhc_insert(large, card, sizeof(card), false)");
    CortexM4* cpu = kinetis_k22_cpu(large);
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC3, 4u, 1u << 17u),
           "cortex_m4_write_memory(cpu, SIM_SCGC3, 4u, 1u << 17u)");
    expect(state, cortex_m4_write_memory(cpu, 0x400b1034u, 4u, UINT32_MAX),
           "cortex_m4_write_memory(cpu, 0x400b1034u, 4u, UINT32_MAX)");
    expect(state, cortex_m4_write_memory(cpu, 0x400b1038u, 4u, UINT32_MAX),
           "cortex_m4_write_memory(cpu, 0x400b1038u, 4u, UINT32_MAX)");
    expect(state, cortex_m4_write_memory(cpu, 0x400b1008u, 4u, 0u),
           "cortex_m4_write_memory(cpu, 0x400b1008u, 4u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, 0x400b100cu, 4u, 3u << 24u),
           "cortex_m4_write_memory(cpu, 0x400b100cu, 4u, 3u << 24u)");
    uint32_t response = 0u;
    expect(state, read32(large, 0x400b1010u, &response),
           "read32(large, 0x400b1010u, &response)");
    expect(state, response == 0x00010000u, "response == 0x00010000u");
    expect(state, cortex_m4_write_memory(cpu, 0x400b1008u, 4u, 0x00010000u),
           "cortex_m4_write_memory(cpu, 0x400b1008u, 4u, 0x00010000u)");
    expect(state, cortex_m4_write_memory(cpu, 0x400b100cu, 4u, 7u << 24u),
           "cortex_m4_write_memory(cpu, 0x400b100cu, 4u, 7u << 24u)");
    expect(state, cortex_m4_write_memory(cpu, 0x400b1004u, 4u, 4u | (1u << 16u)),
           "cortex_m4_write_memory(cpu, 0x400b1004u, 4u, 4u | (1u << 16u))");
    expect(state, cortex_m4_write_memory(cpu, 0x400b1008u, 4u, 0u),
           "cortex_m4_write_memory(cpu, 0x400b1008u, 4u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, 0x400b100cu, 4u, 17u << 24u),
           "cortex_m4_write_memory(cpu, 0x400b100cu, 4u, 17u << 24u)");
    expect(state, read32(large, 0x400b1020u, &response),
           "read32(large, 0x400b1020u, &response)");
    expect(state, response == 0x03020100u, "response == 0x03020100u");
    kinetis_k22_sdhc_eject(large);
    expect(state, !kinetis_k22_sdhc_read_card(large, 0u, &response, 1u),
           "!kinetis_k22_sdhc_read_card(large, 0u, &response, 1u)");
    kinetis_k22_destroy(large);
}

static void expect_endpoint_event_order(TestState* state, KinetisK22* device) {
    CortexM4* cpu = kinetis_k22_cpu(device);
    uint32_t gates = 0;
    expect(state, read32(device, SIM_SCGC4, &gates), "read32(device, SIM_SCGC4, &gates)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC4, 4, gates | 0xc0u),
           "cortex_m4_write_memory(cpu, SIM_SCGC4, 4, gates | 0xc0u)");
    expect(state, cortex_m4_write_memory(cpu, I2C0_C1, 1, 0xf1u),
           "cortex_m4_write_memory(cpu, I2C0_C1, 1, 0xf1u)");
    expect(state, cortex_m4_write_memory(cpu, I2C1_C1, 1, 0xf1u),
           "cortex_m4_write_memory(cpu, I2C1_C1, 1, 0xf1u)");
    KinetisK22I2cTransfer transfer;
    expect(state, kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C1, &transfer),
           "kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C1, &transfer)");
    expect(state, transfer.type == KINETIS_K22_I2C_START,
           "transfer.type == KINETIS_K22_I2C_START");
    expect(state, kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C0, &transfer),
           "kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C0, &transfer)");
    expect(state, transfer.type == KINETIS_K22_I2C_START,
           "transfer.type == KINETIS_K22_I2C_START");
}

static void expect_pdb_data_triggers(TestState* state, KinetisK22* device) {
    CortexM4* cpu = kinetis_k22_cpu(device);
    uint32_t gates = 0;
    expect(state, read32(device, SIM_SCGC6, &gates), "read32(device, SIM_SCGC6, &gates)");
    expect(state,
           cortex_m4_write_memory(cpu, SIM_SCGC6, 4,
                                  gates | (1u << 22u) | (1u << 27u) | (1u << 31u)),
           "cortex_m4_write_memory(cpu, SIM_SCGC6, 4, gates | (1u << 22u) | (1u << 27u) | "
           "(1u << 31u))");
    expect(state, kinetis_k22_set_adc_channel(device, 0, 5, 0x345u),
           "kinetis_k22_set_adc_channel(device, 0, 5, 0x345u)");
    expect(state, cpu_write8(device, ADC0_CFG1, 0x04u),
           "cpu_write8(device, ADC0_CFG1, 0x04u)");
    expect(state, cpu_write8(device, ADC0_SC1A, 5u), "cpu_write8(device, ADC0_SC1A, 5u)");
    expect(state, cpu_write8(device, ADC0_SC2, 0x40u),
           "cpu_write8(device, ADC0_SC2, 0x40u)");

    expect(state, cpu_write8(device, DAC0_DAT0L, 0x11u),
           "cpu_write8(device, DAC0_DAT0L, 0x11u)");
    expect(state, cpu_write8(device, DAC0_DAT0L + 1u, 1u),
           "cpu_write8(device, DAC0_DAT0L + 1u, 1u)");
    expect(state, cpu_write8(device, DAC0_DAT1L, 0x22u),
           "cpu_write8(device, DAC0_DAT1L, 0x22u)");
    expect(state, cpu_write8(device, DAC0_DAT1L + 1u, 2u),
           "cpu_write8(device, DAC0_DAT1L + 1u, 2u)");
    expect(state, cpu_write8(device, DAC0_C0, 0x80u), "cpu_write8(device, DAC0_C0, 0x80u)");
    expect(state, cpu_write8(device, DAC0_C1, 0x80u), "cpu_write8(device, DAC0_C1, 0x80u)");
    expect(state, cpu_write8(device, DAC0_C2, 1u), "cpu_write8(device, DAC0_C2, 1u)");

    expect(state, cortex_m4_write_memory(cpu, PDB_MOD, 4, 31u),
           "cortex_m4_write_memory(cpu, PDB_MOD, 4, 31u)");
    expect(state, cortex_m4_write_memory(cpu, PDB_CH0C1, 4, 1u),
           "cortex_m4_write_memory(cpu, PDB_CH0C1, 4, 1u)");
    expect(state, cortex_m4_write_memory(cpu, PDB_CH0DLY0, 4, 1u),
           "cortex_m4_write_memory(cpu, PDB_CH0DLY0, 4, 1u)");
    expect(state, cortex_m4_write_memory(cpu, PDB_DACINT0, 4, 1u),
           "cortex_m4_write_memory(cpu, PDB_DACINT0, 4, 1u)");
    expect(state, cortex_m4_write_memory(cpu, PDB_DACINTC0, 4, 1u),
           "cortex_m4_write_memory(cpu, PDB_DACINTC0, 4, 1u)");
    expect(state, cortex_m4_write_memory(cpu, PDB_SC, 4, 3u),
           "cortex_m4_write_memory(cpu, PDB_SC, 4, 3u)");
    kinetis_k22_advance(device, 20u);

    uint16_t adc = 0;
    uint16_t dac = 0;
    expect(state, read16(device, ADC0_RA, &adc), "read16(device, ADC0_RA, &adc)");
    expect(state, adc == 0x345u, "adc == 0x345u");
    expect(state, kinetis_k22_get_dac_output(device, 0, &dac),
           "kinetis_k22_get_dac_output(device, 0, &dac)");
    expect(state, dac == 0x222u, "dac == 0x222u");
}

static void expect_adc_alternate_triggers(TestState* state) {
    KinetisK22* device = create_device(state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    CortexM4* cpu = kinetis_k22_cpu(device);
    uint32_t gates = 0u;
    expect(state, read32(device, SIM_SCGC6, &gates), "read32(device, SIM_SCGC6, &gates)");
    expect(state,
           cortex_m4_write_memory(cpu, SIM_SCGC6, 4u, gates | (1u << 23u) | (1u << 27u)),
           "cortex_m4_write_memory(cpu, SIM_SCGC6, 4u, gates | (1u << 23u) | (1u << 27u))");
    expect(state, kinetis_k22_set_adc_channel(device, 0u, 5u, 0x345u),
           "kinetis_k22_set_adc_channel(device, 0u, 5u, 0x345u)");
    expect(state, kinetis_k22_set_adc_channel(device, 0u, 6u, 0x456u),
           "kinetis_k22_set_adc_channel(device, 0u, 6u, 0x456u)");
    expect(state, cpu_write8(device, ADC0_CFG1, 0x04u),
           "cpu_write8(device, ADC0_CFG1, 0x04u)");
    expect(state, cpu_write8(device, ADC0_SC2, 0x40u),
           "cpu_write8(device, ADC0_SC2, 0x40u)");
    expect(state, cpu_write8(device, ADC0_SC1A, 5u), "cpu_write8(device, ADC0_SC1A, 5u)");
    expect(state, cpu_write8(device, ADC0_SC1B, 6u), "cpu_write8(device, ADC0_SC1B, 6u)");
    expect(state, cortex_m4_write_memory(cpu, PIT_MCR, 4u, 0u),
           "cortex_m4_write_memory(cpu, PIT_MCR, 4u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, PIT_LDVAL0, 4u, 0u),
           "cortex_m4_write_memory(cpu, PIT_LDVAL0, 4u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x85u),
           "cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x85u)");
    expect(state, cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 1u),
           "cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 1u)");
    kinetis_k22_advance(device, 20u);
    uint8_t status = 0u;
    expect(state, read8(device, ADC0_SC1A, &status), "read8(device, ADC0_SC1A, &status)");
    expect(state, (status & 0x80u) == 0u, "(status & 0x80u) == 0u");

    expect(state, cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x84u),
           "cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x84u)");
    kinetis_k22_advance(device, 20u);
    expect(state, read8(device, ADC0_SC1A, &status), "read8(device, ADC0_SC1A, &status)");
    expect(state, (status & 0x80u) != 0u, "(status & 0x80u) != 0u");
    uint16_t result = 0u;
    expect(state, read16(device, ADC0_RA, &result), "read16(device, ADC0_RA, &result)");
    expect(state, result == 0x345u, "result == 0x345u");

    expect(state, cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x94u),
           "cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x94u)");
    kinetis_k22_advance(device, 20u);
    expect(state, read8(device, ADC0_SC1B, &status), "read8(device, ADC0_SC1B, &status)");
    expect(state, (status & 0x80u) != 0u, "(status & 0x80u) != 0u");
    expect(state, read16(device, ADC0_RB, &result), "read16(device, ADC0_RB, &result)");
    expect(state, result == 0x456u, "result == 0x456u");
    expect(state, cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 0u),
           "cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, PIT_TFLG0, 4u, 1u),
           "cortex_m4_write_memory(cpu, PIT_TFLG0, 4u, 1u)");

    uint32_t data_gates = 0u;
    expect(state, read32(device, SIM_SCGC4, &data_gates),
           "read32(device, SIM_SCGC4, &data_gates)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC4, 4u, data_gates | (1u << 19u)),
           "cortex_m4_write_memory(cpu, SIM_SCGC4, 4u, data_gates | (1u << 19u))");
    expect(state, cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x81u),
           "cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x81u)");
    expect(state, cpu_write8(device, ADC0_SC1A, 5u), "cpu_write8(device, ADC0_SC1A, 5u)");
    expect(state, kinetis_k22_set_cmp_input(device, 0u, 1u, 10u),
           "kinetis_k22_set_cmp_input(device, 0u, 1u, 10u)");
    expect(state, kinetis_k22_set_cmp_input(device, 0u, 2u, 20u),
           "kinetis_k22_set_cmp_input(device, 0u, 2u, 20u)");
    expect(state, cpu_write8(device, CMP0_MUXCR, 0x0au),
           "cpu_write8(device, CMP0_MUXCR, 0x0au)");
    expect(state, cpu_write8(device, CMP0_CR1, 1u), "cpu_write8(device, CMP0_CR1, 1u)");
    expect(state, kinetis_k22_set_cmp_input(device, 0u, 1u, 30u),
           "kinetis_k22_set_cmp_input(device, 0u, 1u, 30u)");
    kinetis_k22_advance(device, 20u);
    expect(state, read16(device, ADC0_RA, &result), "read16(device, ADC0_RA, &result)");
    expect(state, result == 0x345u, "result == 0x345u");

    expect(state, read32(device, SIM_SCGC5, &gates), "read32(device, SIM_SCGC5, &gates)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC5, 4u, gates | 1u),
           "cortex_m4_write_memory(cpu, SIM_SCGC5, 4u, gates | 1u)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x8eu),
           "cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x8eu)");
    expect(state, cpu_write8(device, ADC0_SC1A, 6u), "cpu_write8(device, ADC0_SC1A, 6u)");
    expect(state, kinetis_k22_set_lptmr_input(device, 0u, false),
           "kinetis_k22_set_lptmr_input(device, 0u, false)");
    expect(state, cortex_m4_write_memory(cpu, LPTMR_PSR, 4u, 4u),
           "cortex_m4_write_memory(cpu, LPTMR_PSR, 4u, 4u)");
    expect(state, cortex_m4_write_memory(cpu, LPTMR_CMR, 4u, 0u),
           "cortex_m4_write_memory(cpu, LPTMR_CMR, 4u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, LPTMR_CSR, 4u, 3u),
           "cortex_m4_write_memory(cpu, LPTMR_CSR, 4u, 3u)");
    expect(state, kinetis_k22_set_lptmr_input(device, 0u, true),
           "kinetis_k22_set_lptmr_input(device, 0u, true)");
    kinetis_k22_advance(device, 20u);
    expect(state, read16(device, ADC0_RA, &result), "read16(device, ADC0_RA, &result)");
    expect(state, result == 0x456u, "result == 0x456u");

    expect(state, read32(device, SIM_SCGC6, &gates), "read32(device, SIM_SCGC6, &gates)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC6, 4u, gates | (1u << 24u)),
           "cortex_m4_write_memory(cpu, SIM_SCGC6, 4u, gates | (1u << 24u))");
    expect(state, cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x88u),
           "cortex_m4_write_memory(cpu, SIM_SOPT7, 4u, 0x88u)");
    expect(state, cpu_write8(device, ADC0_SC1A, 5u), "cpu_write8(device, ADC0_SC1A, 5u)");
    expect(state, cortex_m4_write_memory(cpu, FTM0_MOD, 4u, 3u),
           "cortex_m4_write_memory(cpu, FTM0_MOD, 4u, 3u)");
    expect(state, cortex_m4_write_memory(cpu, FTM0_C0V, 4u, 2u),
           "cortex_m4_write_memory(cpu, FTM0_C0V, 4u, 2u)");
    expect(state, cortex_m4_write_memory(cpu, FTM0_C0SC, 4u, 0x10u),
           "cortex_m4_write_memory(cpu, FTM0_C0SC, 4u, 0x10u)");
    expect(state, cortex_m4_write_memory(cpu, FTM0_EXTTRIG, 4u, 0x10u),
           "cortex_m4_write_memory(cpu, FTM0_EXTTRIG, 4u, 0x10u)");
    expect(state, cortex_m4_write_memory(cpu, FTM0_SC, 4u, 0x08u),
           "cortex_m4_write_memory(cpu, FTM0_SC, 4u, 0x08u)");
    kinetis_k22_advance(device, 20u);
    expect(state, read16(device, ADC0_RA, &result), "read16(device, ADC0_RA, &result)");
    expect(state, result == 0x345u, "result == 0x345u");

    expect(state, cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 0u),
           "cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, PIT_LDVAL0, 4u, 5u),
           "cortex_m4_write_memory(cpu, PIT_LDVAL0, 4u, 5u)");
    expect(state, cortex_m4_write_memory(cpu, PIT_MCR, 4u, 1u),
           "cortex_m4_write_memory(cpu, PIT_MCR, 4u, 1u)");
    expect(state, cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 1u),
           "cortex_m4_write_memory(cpu, PIT_TCTRL0, 4u, 1u)");
    kinetis_k22_advance(device, 2u);
    uint32_t counter = 0u;
    expect(state, read32(device, PIT_LDVAL0 + 4u, &counter),
           "read32(device, PIT_LDVAL0 + 4u, &counter)");
    expect(state, counter == 3u, "counter == 3u");
    expect(state, cortex_m4_write_memory(cpu, DHCSR, 4u, 0xa05f0003u),
           "cortex_m4_write_memory(cpu, DHCSR, 4u, 0xa05f0003u)");
    kinetis_k22_advance(device, 10u);
    expect(state, read32(device, PIT_LDVAL0 + 4u, &counter),
           "read32(device, PIT_LDVAL0 + 4u, &counter)");
    expect(state, counter == 3u, "counter == 3u");
    expect(state, cortex_m4_write_memory(cpu, DHCSR, 4u, 0xa05f0001u),
           "cortex_m4_write_memory(cpu, DHCSR, 4u, 0xa05f0001u)");
    kinetis_k22_advance(device, 1u);
    expect(state, read32(device, PIT_LDVAL0 + 4u, &counter),
           "read32(device, PIT_LDVAL0 + 4u, &counter)");
    expect(state, counter == 2u, "counter == 2u");
    kinetis_k22_destroy(device);
}

static void expect_lptmr_pulse_input(TestState* state, KinetisK22* device) {
    CortexM4* cpu = kinetis_k22_cpu(device);
    uint32_t gates = 0u;
    expect(state, read32(device, SIM_SCGC5, &gates), "read32(device, SIM_SCGC5, &gates)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC5, 4u, gates | 1u),
           "cortex_m4_write_memory(cpu, SIM_SCGC5, 4u, gates | 1u)");
    expect(state, cortex_m4_write_memory(cpu, LPTMR_PSR, 4u, 4u),
           "cortex_m4_write_memory(cpu, LPTMR_PSR, 4u, 4u)");
    expect(state, cortex_m4_write_memory(cpu, LPTMR_CMR, 4u, 0u),
           "cortex_m4_write_memory(cpu, LPTMR_CMR, 4u, 0u)");
    expect(state, kinetis_k22_set_lptmr_input(device, 1u, false),
           "kinetis_k22_set_lptmr_input(device, 1u, false)");
    expect(state, cortex_m4_write_memory(cpu, LPTMR_CSR, 4u, 0x53u),
           "cortex_m4_write_memory(cpu, LPTMR_CSR, 4u, 0x53u)");
    expect(state, kinetis_k22_set_lptmr_input(device, 0u, true),
           "kinetis_k22_set_lptmr_input(device, 0u, true)");
    expect(state, kinetis_k22_set_lptmr_input(device, 1u, false),
           "kinetis_k22_set_lptmr_input(device, 1u, false)");
    expect(state, kinetis_k22_set_lptmr_input(device, 1u, true),
           "kinetis_k22_set_lptmr_input(device, 1u, true)");
    uint32_t value = 0u;
    expect(state, read32(device, LPTMR_CSR, &value), "read32(device, LPTMR_CSR, &value)");
    expect(state, value == 0xd3u, "value == 0xd3u");
    expect(state, cortex_m4_get_irq_pending(cpu, 58u),
           "cortex_m4_get_irq_pending(cpu, 58u)");
    expect(state, cortex_m4_write_memory(cpu, LPTMR_CNR, 4u, 0u),
           "cortex_m4_write_memory(cpu, LPTMR_CNR, 4u, 0u)");
    expect(state, read32(device, LPTMR_CNR, &value), "read32(device, LPTMR_CNR, &value)");
    expect(state, value == 0u, "value == 0u");
    expect(state, !kinetis_k22_set_lptmr_input(device, 3u, false),
           "!kinetis_k22_set_lptmr_input(device, 3u, false)");

    expect(state, cortex_m4_write_memory(cpu, LPTMR_CSR, 4u, 0u),
           "cortex_m4_write_memory(cpu, LPTMR_CSR, 4u, 0u)");
    uint32_t data_gates = 0u;
    expect(state, read32(device, SIM_SCGC4, &data_gates),
           "read32(device, SIM_SCGC4, &data_gates)");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC4, 4u, data_gates | (1u << 19u)),
           "cortex_m4_write_memory(cpu, SIM_SCGC4, 4u, data_gates | (1u << 19u))");
    expect(state, kinetis_k22_set_cmp_input(device, 0u, 1u, 10u),
           "kinetis_k22_set_cmp_input(device, 0u, 1u, 10u)");
    expect(state, kinetis_k22_set_cmp_input(device, 0u, 2u, 20u),
           "kinetis_k22_set_cmp_input(device, 0u, 2u, 20u)");
    expect(state, cpu_write8(device, CMP0_MUXCR, 0x0au),
           "cpu_write8(device, CMP0_MUXCR, 0x0au)");
    expect(state, cpu_write8(device, CMP0_CR1, 1u), "cpu_write8(device, CMP0_CR1, 1u)");
    expect(state, cortex_m4_write_memory(cpu, LPTMR_PSR, 4u, 4u),
           "cortex_m4_write_memory(cpu, LPTMR_PSR, 4u, 4u)");
    expect(state, cortex_m4_write_memory(cpu, LPTMR_CMR, 4u, 0u),
           "cortex_m4_write_memory(cpu, LPTMR_CMR, 4u, 0u)");
    expect(state, cortex_m4_write_memory(cpu, LPTMR_CSR, 4u, 0x43u),
           "cortex_m4_write_memory(cpu, LPTMR_CSR, 4u, 0x43u)");
    expect(state, kinetis_k22_set_cmp_input(device, 0u, 2u, 20u),
           "kinetis_k22_set_cmp_input(device, 0u, 2u, 20u)");
    expect(state, kinetis_k22_set_cmp_input(device, 0u, 1u, 30u),
           "kinetis_k22_set_cmp_input(device, 0u, 1u, 30u)");
    expect(state, read32(device, LPTMR_CSR, &value), "read32(device, LPTMR_CSR, &value)");
    expect(state, value == 0xc3u, "value == 0xc3u");
}

static void expect_flexbus_integration(TestState* state, KinetisK22* device) {
    uint8_t memory[256];
    for (size_t index = 0u; index < sizeof(memory); index++)
        memory[index] = (uint8_t)(0x80u + index);
    expect(
        state,
        kinetis_k22_flexbus_attach(device, 0x60000000u, memory, sizeof(memory), false),
        "kinetis_k22_flexbus_attach(device, 0x60000000u, memory, sizeof(memory), false)");
    CortexM4* cpu = kinetis_k22_cpu(device);
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC7, 4u, 1u),
           "cortex_m4_write_memory(cpu, SIM_SCGC7, 4u, 1u)");
    expect(state, cortex_m4_write_memory(cpu, 0x4000c000u, 4u, 0x60000000u),
           "cortex_m4_write_memory(cpu, 0x4000c000u, 4u, 0x60000000u)");
    expect(state, cortex_m4_write_memory(cpu, 0x4000c004u, 4u, 1u),
           "cortex_m4_write_memory(cpu, 0x4000c004u, 4u, 1u)");
    uint32_t value = 0u;
    expect(state, cortex_m4_read_memory(cpu, 0x60000004u, 4u, &value),
           "cortex_m4_read_memory(cpu, 0x60000004u, 4u, &value)");
    expect(state, value == 0x87868584u, "value == 0x87868584u");
    expect(state, cortex_m4_write_memory(cpu, 0x60000008u, 4u, 0x12345678u),
           "cortex_m4_write_memory(cpu, 0x60000008u, 4u, 0x12345678u)");
    expect(state, kinetis_k22_flexbus_read(device, 8u, &value, sizeof(value)),
           "kinetis_k22_flexbus_read(device, 8u, &value, sizeof(value))");
    expect(state, value == 0x12345678u, "value == 0x12345678u");
    KinetisK22Event event;
    bool saw_read = false;
    bool saw_write = false;
    while (kinetis_k22_next_event(device, &event)) {
        if (event.type == KINETIS_K22_EVENT_FLEXBUS_TRANSFER) {
            saw_read |= event.auxiliary == 4u;
            saw_write |= event.auxiliary == 0x104u && event.value == 0x12345678u;
        }
    }
    expect(state, saw_read, "saw_read");
    expect(state, saw_write, "saw_write");
}

static void expect_reset_domains(TestState* state, KinetisK22* device) {
    const uint32_t address = 0x20000040u;
    const uint32_t sentinel = 0x5aa53cc3u;
    expect(state, write32(device, address, sentinel), "write32(device, address, sentinel)");
    kinetis_k22_gpio_drive(device, 0, 0, true);
    expect(state, write16(device, WDOG_UNLOCK, 0xc520u),
           "write16(device, WDOG_UNLOCK, 0xc520u)");
    expect(state, write16(device, WDOG_UNLOCK, 0xd928u),
           "write16(device, WDOG_UNLOCK, 0xd928u)");
    expect(state, write16(device, WDOG_TOVALH, 0), "write16(device, WDOG_TOVALH, 0)");
    expect(state, write16(device, WDOG_TOVALL, 1), "write16(device, WDOG_TOVALL, 1)");
    expect(state, write16(device, WDOG_STCTRLH, 1), "write16(device, WDOG_STCTRLH, 1)");
    kinetis_k22_advance(device, k22_test_core_cycles_for_bus_cycles(device, 260u));
    kinetis_k22_watchdog_advance(device, 1);

    uint32_t value = 0;
    uint8_t cause = 0;
    expect(state, read32(device, address, &value), "read32(device, address, &value)");
    expect(state, value == sentinel, "value == sentinel");
    expect(state, read8(device, RCM_SRS0, &cause), "read8(device, RCM_SRS0, &cause)");
    expect(state, cause == 0x20u, "cause == 0x20u");
    expect(state, read8(device, RCM_SSRS0, &cause), "read8(device, RCM_SSRS0, &cause)");
    expect(state, (cause & 0xa2u) == 0xa2u, "(cause & 0xa2u) == 0xa2u");
    expect(state, read32(device, GPIOA_PDIR, &value), "read32(device, GPIOA_PDIR, &value)");
    expect(state, (value & 1u) != 0, "(value & 1u) != 0");

    expect(state, write16(device, WDOG_UNLOCK, 0xc520u),
           "write16(device, WDOG_UNLOCK, 0xc520u)");
    expect(state, write16(device, WDOG_UNLOCK, 0xd928u),
           "write16(device, WDOG_UNLOCK, 0xd928u)");
    expect(state, write16(device, WDOG_TOVALH, 0u), "write16(device, WDOG_TOVALH, 0u)");
    expect(state, write16(device, WDOG_TOVALL, 1u), "write16(device, WDOG_TOVALL, 1u)");
    expect(state, write16(device, WDOG_STCTRLH, 5u), "write16(device, WDOG_STCTRLH, 5u)");
    kinetis_k22_advance(device, k22_test_core_cycles_for_bus_cycles(device, 260u));
    kinetis_k22_watchdog_advance(device, 1u);
    expect(state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), 22u),
           "cortex_m4_get_irq_pending(kinetis_k22_cpu(device), 22u)");
}

static void expect_copy(TestState* state, KinetisK22* source) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.package = KINETIS_K22_PACKAGE_DC_121_XFBGA;
    KinetisK22* destination = kinetis_k22_create(configuration);
    expect(state, destination != NULL, "destination != NULL");
    WaitFixture source_wait = {0};
    WaitFixture destination_wait = {0};
    cortex_m4_set_wait_states(kinetis_k22_cpu(source), wait_states, &source_wait);
    cortex_m4_set_wait_states(kinetis_k22_cpu(destination), wait_states, &destination_wait);
    expect(state, kinetis_k22_copy(destination, source),
           "kinetis_k22_copy(destination, source)");
    uint32_t value = 0;
    expect(state, read32(destination, 0x20000040u, &value),
           "read32(destination, 0x20000040u, &value)");
    expect(state, value == 0x5aa53cc3u, "value == 0x5aa53cc3u");
    expect(state,
           kinetis_k22_core_clock_hz(destination) == kinetis_k22_core_clock_hz(source),
           "kinetis_k22_core_clock_hz(destination) == kinetis_k22_core_clock_hz(source)");
    expect(state, kinetis_k22_bus_clock_hz(destination) == kinetis_k22_bus_clock_hz(source),
           "kinetis_k22_bus_clock_hz(destination) == kinetis_k22_bus_clock_hz(source)");
    test_connect_debugger(state, kinetis_k22_cpu(destination));
    expect(
        state,
        cortex_m4_step(kinetis_k22_cpu(destination)).stop == CORTEX_M4_STOP_BREAKPOINT,
        "cortex_m4_step(kinetis_k22_cpu(destination)).stop == CORTEX_M4_STOP_BREAKPOINT");
    expect(state, destination_wait.calls != 0u, "destination_wait.calls != 0u");
    expect(state, source_wait.calls == 0u, "source_wait.calls == 0u");
    kinetis_k22_destroy(destination);
}

int main(void) {
    TestState state = {0};
    expect_package_selection(&state);
    expect_package_serial_extensions(&state);
    expect_serial_dma_sources(&state);
    expect_periodic_dma_trigger(&state);
    expect_can_irq_level(&state);
    expect_sdhc_integration(&state);
    expect_fmc_cache(&state);
    expect_fmc_invalidation_and_locking(&state);
    expect_integrated_flash_swap(&state);
    KinetisK22* device = create_device(&state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    expect(&state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    expect(&state, kinetis_k22_core_clock_hz(device) == 20971520u,
           "kinetis_k22_core_clock_hz(device) == 20971520u");
    expect(&state, kinetis_k22_bus_clock_hz(device) == 20971520u,
           "kinetis_k22_bus_clock_hz(device) == 20971520u");
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
    expect(&state, !kinetis_k22_next_event(device, &event),
           "!kinetis_k22_next_event(device, &event)");
    kinetis_k22_destroy(device);
    expect_memory_domains(&state);
    return test_finish(&state);
}
