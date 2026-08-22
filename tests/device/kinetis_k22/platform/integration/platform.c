#include "device/kinetis_k22/platform/integration/internal.h"

bool k22_integration_test_read8(KinetisK22* device, uint32_t address, uint8_t* value) {
    return kinetis_k22_read(device, address, value, sizeof(*value));
}

bool k22_integration_test_read32(KinetisK22* device, uint32_t address, uint32_t* value) {
    return kinetis_k22_read(device, address, value, sizeof(*value));
}

bool k22_integration_test_write16(KinetisK22* device, uint32_t address, uint16_t value) {
    return kinetis_k22_write(device, address, &value, sizeof(value));
}

static bool write8(KinetisK22* device, uint32_t address, uint8_t value) {
    return kinetis_k22_write(device, address, &value, sizeof(value));
}

bool k22_integration_test_read16(KinetisK22* device, uint32_t address, uint16_t* value) {
    return kinetis_k22_read(device, address, value, sizeof(*value));
}

bool k22_integration_test_write32(KinetisK22* device, uint32_t address, uint32_t value) {
    return kinetis_k22_write(device, address, &value, sizeof(value));
}

bool k22_integration_test_cpu_write8(KinetisK22* device, uint32_t address, uint8_t value) {
    return cortex_m4_write_memory(kinetis_k22_cpu(device), address, 1u, value);
}

static bool irq_level(const KinetisK22* device, uint8_t irq) {
    const CortexM4* cpu = kinetis_k22_cpu_const(device);
    return (cpu->irq_level[irq / 32u] & (1u << (irq & 31u))) != 0u;
}

static uint32_t flash_fccob_address(uint8_t index) {
    static const uint8_t offsets[12] = {7u, 6u, 5u, 4u, 11u, 10u, 9u, 8u, 15u, 14u, 13u, 12u};
    return FTFA_FCCOB3 - 4u + offsets[index];
}

KinetisK22* k22_integration_test_create_device(TestState* state, KinetisK22Package package) {
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

KinetisK22* k22_integration_test_create_f12_device(TestState* state, KinetisK22Package package) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.profile = KINETIS_K22_PROFILE_MK22FN1M012;
    configuration.package = package;
    configuration.flash_size = 1024u * 1024u;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(state, device != NULL, "device != NULL");
    return device;
}

void k22_integration_test_expect_package_selection(TestState* state) {
    KinetisK22Configuration invalid_profile = kinetis_k22_default_configuration();
    invalid_profile.profile = KINETIS_K22_PROFILE_COUNT;
    expect(state, kinetis_k22_create(invalid_profile) == NULL,
           "kinetis_k22_create(invalid_profile) == NULL");

    KinetisK22Configuration invalid = kinetis_k22_default_configuration();
    invalid.package = KINETIS_K22_PACKAGE_AH_64_WLCSP;
    expect(state, kinetis_k22_create(invalid) == NULL, "kinetis_k22_create(invalid) == NULL");

    KinetisK22* small = k22_integration_test_create_device(state, KINETIS_K22_PACKAGE_LH_64_LQFP);
    uint16_t value = 0;
    uint8_t register_value = 0;
    expect(state, kinetis_k22_read(small, DAC1_DAT0L, &register_value, sizeof(register_value)),
           "kinetis_k22_read(small, DAC1_DAT0L, &register_value, sizeof(register_value))");
    expect(state, kinetis_k22_get_dac_output(small, 1, &value),
           "kinetis_k22_get_dac_output(small, 1, &value)");
    kinetis_k22_gpio_drive(small, 4, 31, true);
    uint32_t input = UINT32_MAX;
    expect(state, k22_integration_test_read32(small, GPIOA_PDIR + 4u * 0x40u, &input),
           "k22_integration_test_read32(small, GPIOA_PDIR + 4u * 0x40u, &input)");
    expect(state, input == 0, "input == 0");
    kinetis_k22_destroy(small);

    KinetisK22* large = k22_integration_test_create_device(state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    expect(state, kinetis_k22_read(large, DAC1_DAT0L, &register_value, sizeof(register_value)),
           "kinetis_k22_read(large, DAC1_DAT0L, &register_value, sizeof(register_value))");
    kinetis_k22_destroy(large);

    KinetisK22* limited =
        k22_integration_test_create_device(state, KINETIS_K22_PACKAGE_FX_88_HVQFN);
    expect(state, !kinetis_k22_read(limited, DAC1_DAT0L, &register_value, sizeof(register_value)),
           "!kinetis_k22_read(limited, DAC1_DAT0L, &register_value, sizeof(register_value))");
    expect(state, !kinetis_k22_get_dac_output(limited, 1, &value),
           "!kinetis_k22_get_dac_output(limited, 1, &value)");
    kinetis_k22_destroy(limited);
}

void k22_integration_test_expect_integrated_flash_command(TestState* state, KinetisK22* device) {
    const uint32_t target = 0x00001000u;
    uint32_t value = 0u;
    expect(state, k22_integration_test_read32(device, target, &value),
           "k22_integration_test_read32(device, target, &value)");
    expect(state, value == UINT32_MAX, "value == UINT32_MAX");
    const uint8_t command[8] = {0x06u, 0x00u, 0x10u, 0x00u, 0x78u, 0x56u, 0x34u, 0x12u};
    for (uint8_t index = 0u; index < sizeof(command); index++)
        expect(
            state,
            k22_integration_test_cpu_write8(device, flash_fccob_address(index), command[index]),
            "k22_integration_test_cpu_write8(device, flash_fccob_address(index), command[index])");
    expect(state, k22_integration_test_cpu_write8(device, FTFA_FSTAT, 0x80u),
           "k22_integration_test_cpu_write8(device, FTFA_FSTAT, 0x80u)");
    expect(state, k22_integration_test_read32(device, target, &value),
           "k22_integration_test_read32(device, target, &value)");
    expect(state, value == UINT32_MAX, "value == UINT32_MAX");
    const uint32_t fstat_bit_band = 0x42000000u + (FTFA_FSTAT - 0x40000000u) * 32u + 6u * 4u;
    expect(state, k22_integration_test_write32(device, fstat_bit_band, 1u),
           "k22_integration_test_write32(device, fstat_bit_band, 1u)");
    kinetis_k22_advance(device, 40u);
    expect(state, k22_integration_test_read32(device, target, &value),
           "k22_integration_test_read32(device, target, &value)");
    expect(state, value == 0x12345678u, "value == 0x12345678u");
}

static void launch_swap_command(TestState* state, KinetisK22* device, uint32_t address,
                                uint8_t control) {
    const uint8_t command[5] = {0x46u, (uint8_t)(address >> 16u), (uint8_t)(address >> 8u),
                                (uint8_t)address, control};
    for (uint8_t index = 0u; index < sizeof(command); index++)
        expect(
            state,
            k22_integration_test_cpu_write8(device, flash_fccob_address(index), command[index]),
            "k22_integration_test_cpu_write8(device, flash_fccob_address(index), command[index])");
    expect(state, k22_integration_test_cpu_write8(device, FTFA_FSTAT, 0x80u),
           "k22_integration_test_cpu_write8(device, FTFA_FSTAT, 0x80u)");
    kinetis_k22_advance(device, 40u);
}

void k22_integration_test_expect_integrated_flash_swap(TestState* state) {
    KinetisK22* device =
        k22_integration_test_create_f12_device(state, KINETIS_K22_PACKAGE_MC_121_MAPBGA);
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
    expect(state, k22_integration_test_read8(device, 0x20u, &value),
           "k22_integration_test_read8(device, 0x20u, &value)");
    expect(state, value == upper, "value == upper");
    expect(state, k22_integration_test_read8(device, 0x80020u, &value),
           "k22_integration_test_read8(device, 0x80020u, &value)");
    expect(state, value == lower, "value == lower");
    expect(state, k22_integration_test_read8(device, FTFA_FSTAT + 1u, &value),
           "k22_integration_test_read8(device, FTFA_FSTAT + 1u, &value)");
    expect(state, (value & 8u) != 0u, "(value & 8u) != 0u");
    kinetis_k22_destroy(device);
}

void k22_integration_test_expect_io_irq_levels(TestState* state, KinetisK22* device) {
    expect(state, k22_integration_test_write32(device, SIM_SCGC5, 1u << 12u),
           "k22_integration_test_write32(device, SIM_SCGC5, 1u << 12u)");
    expect(state, k22_integration_test_write32(device, PORTD_PCR0, 9u << 16u),
           "k22_integration_test_write32(device, PORTD_PCR0, 9u << 16u)");
    kinetis_k22_gpio_drive(device, 3u, 0u, false);
    kinetis_k22_gpio_drive(device, 3u, 0u, true);
    expect(state, irq_level(device, 62u), "irq_level(device, 62u)");
    expect(state, k22_integration_test_write32(device, PORTD_ISFR, 1u),
           "k22_integration_test_write32(device, PORTD_ISFR, 1u)");
    expect(state, !irq_level(device, 62u), "!irq_level(device, 62u)");

    expect(state, k22_integration_test_write32(device, SIM_SCGC4, 1u << 18u),
           "k22_integration_test_write32(device, SIM_SCGC4, 1u << 18u)");
    expect(state, write8(device, USB0_INTEN, 1u << 3u), "write8(device, USB0_INTEN, 1u << 3u)");
    expect(state, write8(device, USB0_ENDPT3, 1u), "write8(device, USB0_ENDPT3, 1u)");
    expect(state, kinetis_k22_usb_token(device, 3u, 0x69u, false),
           "kinetis_k22_usb_token(device, 3u, 0x69u, false)");
    expect(state, irq_level(device, 53u), "irq_level(device, 53u)");
    expect(state, write8(device, USB0_ISTAT, 1u << 3u), "write8(device, USB0_ISTAT, 1u << 3u)");
    expect(state, !irq_level(device, 53u), "!irq_level(device, 53u)");

    expect(state, k22_integration_test_write32(device, SIM_SCGC6, 1u << 15u),
           "k22_integration_test_write32(device, SIM_SCGC6, 1u << 15u)");
    expect(state, k22_integration_test_write32(device, I2S0_TCSR, UINT32_C(0x80000100)),
           "k22_integration_test_write32(device, I2S0_TCSR, UINT32_C(0x80000100))");
    expect(state, k22_integration_test_write32(device, I2S0_TDR0, 0x12345678u),
           "k22_integration_test_write32(device, I2S0_TDR0, 0x12345678u)");
    expect(state, irq_level(device, 28u), "irq_level(device, 28u)");
    expect(state, k22_integration_test_write32(device, I2S0_TCSR, UINT32_C(0x80000000)),
           "k22_integration_test_write32(device, I2S0_TCSR, UINT32_C(0x80000000))");
    expect(state, !irq_level(device, 28u), "!irq_level(device, 28u)");
    expect(state, k22_integration_test_write32(device, I2S0_RCSR, UINT32_C(0x80000100)),
           "k22_integration_test_write32(device, I2S0_RCSR, UINT32_C(0x80000100))");
    expect(state, kinetis_k22_i2s_receive(device, 0x87654321u),
           "kinetis_k22_i2s_receive(device, 0x87654321u)");
    expect(state, irq_level(device, 29u), "irq_level(device, 29u)");
    expect(state, k22_integration_test_write32(device, I2S0_RCSR, UINT32_C(0x80000000)),
           "k22_integration_test_write32(device, I2S0_RCSR, UINT32_C(0x80000000))");
    expect(state, !irq_level(device, 29u), "!irq_level(device, 29u)");
}

void k22_integration_test_expect_can_irq_level(TestState* state) {
    KinetisK22* device =
        k22_integration_test_create_f12_device(state, KINETIS_K22_PACKAGE_MD_144_MAPBGA);
    expect(state, k22_integration_test_write32(device, SIM_SCGC6, 1u << 4u),
           "k22_integration_test_write32(device, SIM_SCGC6, 1u << 4u)");
    expect(state, k22_integration_test_write32(device, CAN0_MCR, 0x0fu),
           "k22_integration_test_write32(device, CAN0_MCR, 0x0fu)");
    expect(state, k22_integration_test_write32(device, CAN0_CTRL1, 0u),
           "k22_integration_test_write32(device, CAN0_CTRL1, 0u)");
    expect(state, k22_integration_test_write32(device, CAN0_IMASK1, 1u),
           "k22_integration_test_write32(device, CAN0_IMASK1, 1u)");
    expect(state, k22_integration_test_write32(device, CAN0_MB0_CS, 4u << 24u),
           "k22_integration_test_write32(device, CAN0_MB0_CS, 4u << 24u)");
    const KinetisK22CanFrame frame = {
        .identifier = 0x123u,
        .length = 8u,
        .data = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u},
    };
    expect(state, kinetis_k22_can_receive(device, &frame),
           "kinetis_k22_can_receive(device, &frame)");
    expect(state, irq_level(device, 75u), "irq_level(device, 75u)");
    expect(state, k22_integration_test_write32(device, CAN0_IFLAG1, 1u),
           "k22_integration_test_write32(device, CAN0_IFLAG1, 1u)");
    expect(state, !irq_level(device, 75u), "!irq_level(device, 75u)");
    kinetis_k22_destroy(device);
}

void k22_integration_test_expect_memory_domains(TestState* state) {
    KinetisK22* device =
        k22_integration_test_create_f12_device(state, KINETIS_K22_PACKAGE_LQ_144_LQFP);
    CortexM4* cpu = kinetis_k22_cpu(device);
    const CortexM4* constant_cpu = kinetis_k22_cpu_const(device);
    expect(state, constant_cpu == cpu, "constant_cpu == cpu");
    expect(state, kinetis_k22_cpu_const(NULL) == NULL, "kinetis_k22_cpu_const(NULL) == NULL");

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
    expect(state, k22_integration_test_read32(device, flexram + 4u, &value),
           "k22_integration_test_read32(device, flexram + 4u, &value)");
    expect(state, value == 0xa5a55a5au, "value == 0xa5a55a5au");

    const uint32_t flash = 0x100u;
    expect(state, !kinetis_k22_memory_write(device, flash, 1u, CORTEX_M4_ACCESS_DATA, 0x5au),
           "!kinetis_k22_memory_write(device, flash, 1u, CORTEX_M4_ACCESS_DATA, 0x5au)");
    uint8_t byte = 0x5au;
    expect(state, kinetis_k22_write(device, flash, &byte, sizeof(byte)),
           "kinetis_k22_write(device, flash, &byte, sizeof(byte))");
    byte = 0u;
    expect(state, k22_integration_test_read8(device, flash, &byte),
           "k22_integration_test_read8(device, flash, &byte)");
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

void k22_integration_test_expect_manifest_fallback(TestState* state, KinetisK22* device) {
    uint32_t value = 0;
    expect(state, k22_integration_test_read32(device, FMC_PFAPR, &value),
           "k22_integration_test_read32(device, FMC_PFAPR, &value)");
    expect(state, value == 0x00f8003fu, "value == 0x00f8003fu");
    expect(state, k22_integration_test_write32(device, FMC_PFAPR, UINT32_MAX),
           "k22_integration_test_write32(device, FMC_PFAPR, UINT32_MAX)");
    expect(state, k22_integration_test_read32(device, FMC_PFAPR, &value),
           "k22_integration_test_read32(device, FMC_PFAPR, &value)");
    expect(state, value == 0x00ffffffu, "value == 0x00ffffffu");
    expect(state, !k22_integration_test_read32(device, FMC_PFAPR + 0x0cu, &value),
           "!k22_integration_test_read32(device, FMC_PFAPR + 0x0cu, &value)");
    expect(state, !k22_integration_test_write32(device, FMC_PFAPR + 0x0cu, UINT32_MAX),
           "!k22_integration_test_write32(device, FMC_PFAPR + 0x0cu, UINT32_MAX)");
    expect(state,
           !kinetis_k22_peripheral_write(device, FMC_PFAPR, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA,
                                         0u),
           "!kinetis_k22_peripheral_write( device, FMC_PFAPR, 4u, "
           "CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, 0u)");

    uint8_t flash = 0x5au;
    expect(state, kinetis_k22_write(device, 0x100u, &flash, sizeof(flash)),
           "kinetis_k22_write(device, 0x100u, &flash, sizeof(flash))");
    expect(state, k22_integration_test_write32(device, FMC_PFAPR, 0x10u),
           "k22_integration_test_write32(device, FMC_PFAPR, 0x10u)");
    expect(state, !kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "!kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, kinetis_k22_dma_read(device, 0x100u, 1u, &value),
           "kinetis_k22_dma_read(device, 0x100u, 1u, &value)");
    expect(state, value == 0x5au, "value == 0x5au");
    expect(state, k22_integration_test_write32(device, FMC_PFAPR, 4u),
           "k22_integration_test_write32(device, FMC_PFAPR, 4u)");
    expect(state, !kinetis_k22_dma_read(device, 0x100u, 1u, &value),
           "!kinetis_k22_dma_read(device, 0x100u, 1u, &value)");
    expect(state, kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, value == 0x5au, "value == 0x5au");
    expect(state, k22_integration_test_write32(device, FMC_PFAPR, UINT32_MAX),
           "k22_integration_test_write32(device, FMC_PFAPR, UINT32_MAX)");
    expect(state, k22_integration_test_read32(device, FMC_PFB0CR, &value),
           "k22_integration_test_read32(device, FMC_PFB0CR, &value)");
    expect(state, k22_integration_test_write32(device, FMC_PFB0CR, (value & ~0x18u) | 0x00f00000u),
           "k22_integration_test_write32(device, FMC_PFB0CR, (value & ~0x18u) | 0x00f00000u)");

    const uint32_t alias = 0x42000000u + (FMC_TAGVDW0S0 - 0x40000000u) * 32u + 5u * 4u;
    expect(state, k22_integration_test_write32(device, alias, 1),
           "k22_integration_test_write32(device, alias, 1)");
    expect(state, k22_integration_test_read32(device, FMC_TAGVDW0S0, &value),
           "k22_integration_test_read32(device, FMC_TAGVDW0S0, &value)");
    expect(state, value == 0x20u, "value == 0x20u");
    expect(state, k22_integration_test_read32(device, alias, &value),
           "k22_integration_test_read32(device, alias, &value)");
    expect(state, value == 1, "value == 1");
    expect(state, k22_integration_test_write32(device, FMC_TAGVDW0S0, 0x21u),
           "k22_integration_test_write32(device, FMC_TAGVDW0S0, 0x21u)");
    expect(state, k22_integration_test_write32(device, FMC_TAGVDW1S0, 0x41u),
           "k22_integration_test_write32(device, FMC_TAGVDW1S0, 0x41u)");
    expect(state, k22_integration_test_write32(device, FMC_DATAW0S0UM, 0x12345678u),
           "k22_integration_test_write32(device, FMC_DATAW0S0UM, 0x12345678u)");
    expect(state, k22_integration_test_write32(device, FMC_DATAW1S0UM, 0x89abcdefu),
           "k22_integration_test_write32(device, FMC_DATAW1S0UM, 0x89abcdefu)");
    expect(state, k22_integration_test_write32(device, FMC_PFB0CR, (1u << 19u) | (1u << 20u)),
           "k22_integration_test_write32(device, FMC_PFB0CR, (1u << 19u) | (1u << 20u))");
    expect(state, k22_integration_test_read32(device, FMC_TAGVDW0S0, &value),
           "k22_integration_test_read32(device, FMC_TAGVDW0S0, &value)");
    expect(state, value == 0u, "value == 0u");
    expect(state, k22_integration_test_read32(device, FMC_TAGVDW1S0, &value),
           "k22_integration_test_read32(device, FMC_TAGVDW1S0, &value)");
    expect(state, value == 0x41u, "value == 0x41u");
    expect(state, k22_integration_test_read32(device, FMC_DATAW0S0UM, &value),
           "k22_integration_test_read32(device, FMC_DATAW0S0UM, &value)");
    expect(state, value == 0u, "value == 0u");
    expect(state, k22_integration_test_read32(device, FMC_DATAW1S0UM, &value),
           "k22_integration_test_read32(device, FMC_DATAW1S0UM, &value)");
    expect(state, value == 0x89abcdefu, "value == 0x89abcdefu");
    expect(state, k22_integration_test_read32(device, FMC_PFB0CR, &value),
           "k22_integration_test_read32(device, FMC_PFB0CR, &value)");
    expect(state, (value & 0x00f80000u) == 0u, "(value & 0x00f80000u) == 0u");
    expect(state, k22_integration_test_write32(device, FMC_PFB0CR, 1u << 21u),
           "k22_integration_test_write32(device, FMC_PFB0CR, 1u << 21u)");
    expect(state, k22_integration_test_read32(device, FMC_TAGVDW1S0, &value),
           "k22_integration_test_read32(device, FMC_TAGVDW1S0, &value)");
    expect(state, value == 0u, "value == 0u");
    expect(state, k22_integration_test_read32(device, FMC_DATAW1S0UM, &value),
           "k22_integration_test_read32(device, FMC_DATAW1S0UM, &value)");
    expect(state, value == 0u, "value == 0u");
    expect(state, !k22_integration_test_read32(device, 0x43ffffffu, &value),
           "!k22_integration_test_read32(device, 0x43ffffffu, &value)");

    expect(state, k22_integration_test_read32(device, MCM_PLASC, &value),
           "k22_integration_test_read32(device, MCM_PLASC, &value)");
    expect(state, value == 0x0017001fu, "value == 0x0017001fu");
    expect(state, !k22_integration_test_write32(device, MCM_PLASC, UINT32_MAX),
           "!k22_integration_test_write32(device, MCM_PLASC, UINT32_MAX)");
    expect(state, k22_integration_test_read32(device, MCM_PLASC, &value),
           "k22_integration_test_read32(device, MCM_PLASC, &value)");
    expect(state, value == 0x0017001fu, "value == 0x0017001fu");
}

void k22_integration_test_expect_fmc_cache(TestState* state) {
    KinetisK22* device =
        k22_integration_test_create_device(state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    expect(state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    uint32_t value = 0u;
    expect(state, k22_integration_test_read32(device, FMC_PFB0CR, &value),
           "k22_integration_test_read32(device, FMC_PFB0CR, &value)");
    expect(state, k22_integration_test_write32(device, FMC_PFB0CR, value | 0x0ef00000u),
           "k22_integration_test_write32(device, FMC_PFB0CR, value | 0x0ef00000u)");
    uint8_t byte = 0x5au;
    expect(state, kinetis_k22_write(device, 0x100u, &byte, sizeof(byte)),
           "kinetis_k22_write(device, 0x100u, &byte, sizeof(byte))");
    expect(state, kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, value == 0x5au, "value == 0x5au");
    expect(state, k22_integration_test_read32(device, FMC_TAGVDW0S0, &value),
           "k22_integration_test_read32(device, FMC_TAGVDW0S0, &value)");
    expect(state, (value & 1u) != 0u, "(value & 1u) != 0u");
    expect(state, k22_integration_test_read32(device, FMC_DATAW0S0LM, &value),
           "k22_integration_test_read32(device, FMC_DATAW0S0LM, &value)");
    expect(state, (value & 0xffu) == 0x5au, "(value & 0xffu) == 0x5au");
    expect(state, kinetis_k22_flash_controller_write(device, 0x100u, 1u, 0xa5u),
           "kinetis_k22_flash_controller_write(device, 0x100u, 1u, 0xa5u)");
    expect(state, kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, value == 0x5au, "value == 0x5au");
    expect(state, k22_integration_test_read32(device, FMC_PFB0CR, &value),
           "k22_integration_test_read32(device, FMC_PFB0CR, &value)");
    expect(state, k22_integration_test_write32(device, FMC_PFB0CR, value | (1u << 20u)),
           "k22_integration_test_write32(device, FMC_PFB0CR, value | (1u << 20u))");
    expect(state, kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, value == 0xa5u, "value == 0xa5u");
    kinetis_k22_destroy(device);
}

void k22_integration_test_expect_fmc_invalidation_and_locking(TestState* state) {
    KinetisK22* device =
        k22_integration_test_create_device(state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    expect(state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    uint8_t byte = 0x5au;
    uint32_t control = 0u;
    uint32_t tag = 0u;
    uint32_t value = 0u;
    expect(state, kinetis_k22_write(device, 0x100u, &byte, sizeof(byte)),
           "kinetis_k22_write(device, 0x100u, &byte, sizeof(byte))");
    expect(state, k22_integration_test_read32(device, FMC_PFB0CR, &control),
           "k22_integration_test_read32(device, FMC_PFB0CR, &control)");
    expect(state, k22_integration_test_write32(device, FMC_PFB0CR, control | 0x0ef00000u),
           "k22_integration_test_write32(device, FMC_PFB0CR, control | 0x0ef00000u)");
    expect(state, kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, k22_integration_test_read32(device, FMC_TAGVDW0S0, &tag),
           "k22_integration_test_read32(device, FMC_TAGVDW0S0, &tag)");
    expect(state, k22_integration_test_write32(device, FMC_TAGVDW1S0, tag),
           "k22_integration_test_write32(device, FMC_TAGVDW1S0, tag)");
    device->fmc_bank[1][0] = 0u;
    expect(state, kinetis_k22_memory_write(device, 0x100u, 1u, CORTEX_M4_ACCESS_DEBUG, 0xa5u),
           "kinetis_k22_memory_write(device, 0x100u, 1u, CORTEX_M4_ACCESS_DEBUG, 0xa5u)");
    expect(state, k22_integration_test_read32(device, FMC_TAGVDW0S0, &value),
           "k22_integration_test_read32(device, FMC_TAGVDW0S0, &value)");
    expect(state, value == 0u, "value == 0u");
    expect(state, k22_integration_test_read32(device, FMC_TAGVDW1S0, &value),
           "k22_integration_test_read32(device, FMC_TAGVDW1S0, &value)");
    expect(state, value == 0u, "value == 0u");

    expect(state, k22_integration_test_write32(device, FMC_PFB0CR, control | 0x0ff00000u),
           "k22_integration_test_write32(device, FMC_PFB0CR, control | 0x0ff00000u)");
    expect(state, kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(state, value == 0xa5u, "value == 0xa5u");
    expect(state, k22_integration_test_read32(device, FMC_TAGVDW0S0, &value),
           "k22_integration_test_read32(device, FMC_TAGVDW0S0, &value)");
    expect(state, value == 0u, "value == 0u");
    kinetis_k22_destroy(device);
}

void k22_integration_test_expect_clock_gates(TestState* state, KinetisK22* device) {
    uint32_t value = 0;
    uint8_t status = 0;
    CortexM4* cpu = kinetis_k22_cpu(device);
    expect(state, k22_integration_test_read32(device, SIM_SCGC4, &value),
           "k22_integration_test_read32(device, SIM_SCGC4, &value)");
    expect(state, (value & (1u << 11)) == 0, "(value & (1u << 11)) == 0");
    expect(state, cortex_m4_write_memory(cpu, SIM_SCGC4, 4, 0xf0100830u),
           "cortex_m4_write_memory(cpu, SIM_SCGC4, 4, 0xf0100830u)");
    expect(state, cortex_m4_write_memory(cpu, UART1_C2, 1, 0x24u),
           "cortex_m4_write_memory(cpu, UART1_C2, 1, 0x24u)");
    expect(state, kinetis_k22_serial_receive(device, KINETIS_K22_SERIAL_UART1, 0x5au, 0),
           "kinetis_k22_serial_receive(device, KINETIS_K22_SERIAL_UART1, 0x5au, 0)");
    expect(state, k22_integration_test_read8(device, UART1_S1, &status),
           "k22_integration_test_read8(device, UART1_S1, &status)");
    expect(state, (status & 0x20u) != 0, "(status & 0x20u) != 0");
    expect(state, cortex_m4_get_irq_pending(cpu, 33), "cortex_m4_get_irq_pending(cpu, 33)");
}

void k22_integration_test_expect_package_serial_extensions(TestState* state) {
    KinetisK22* small =
        k22_integration_test_create_f12_device(state, KINETIS_K22_PACKAGE_LH_64_LQFP);
    expect(state, !kinetis_k22_serial_receive(small, KINETIS_K22_SERIAL_UART3, 0x11u, 0u),
           "!kinetis_k22_serial_receive(small, KINETIS_K22_SERIAL_UART3, 0x11u, 0u)");
    expect(state, !kinetis_k22_serial_receive(small, KINETIS_K22_SERIAL_SPI1, 0x22u, 0u),
           "!kinetis_k22_serial_receive(small, KINETIS_K22_SERIAL_SPI1, 0x22u, 0u)");
    kinetis_k22_destroy(small);

    KinetisK22* large =
        k22_integration_test_create_f12_device(state, KINETIS_K22_PACKAGE_MD_144_MAPBGA);
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
    expect(state, k22_integration_test_read8(large, 0x4006d007u, &value),
           "k22_integration_test_read8(large, 0x4006d007u, &value)");
    expect(state, value == 0x33u, "value == 0x33u");
    expect(state, k22_integration_test_read8(large, 0x400ea007u, &value),
           "k22_integration_test_read8(large, 0x400ea007u, &value)");
    expect(state, value == 0x44u, "value == 0x44u");
    expect(state, k22_integration_test_read8(large, 0x400eb007u, &value),
           "k22_integration_test_read8(large, 0x400eb007u, &value)");
    expect(state, value == 0x55u, "value == 0x55u");
    kinetis_k22_destroy(large);
}
