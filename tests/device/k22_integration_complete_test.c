#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "kinetis_k22_internal.h"
#include "test.h"

enum {
    FMC_PFAPR = 0x4001f000u,
    FMC_PFB0CR = 0x4001f004u,
    FMC_TAGVDW0S0 = 0x4001f100u,
    FMC_TAGVDW1S0 = 0x4001f120u,
    FTFA_FSTAT = 0x40020000u,
    FTFA_FCCOB0 = 0x40020004u,
    PDB_SC = 0x40036000u,
    PDB_MOD = 0x40036004u,
    PDB_CH0C1 = 0x40036010u,
    PDB_CH0DLY0 = 0x40036018u,
    PDB_DACINT0 = 0x40036150u,
    PDB_DACINTC0 = 0x40036154u,
    ADC0_SC1A = 0x4003b000u,
    ADC0_CFG1 = 0x4003b008u,
    ADC0_RA = 0x4003b010u,
    ADC0_SC2 = 0x4003b020u,
    DAC0_DAT0L = 0x4003f000u,
    DAC0_DAT1L = 0x4003f002u,
    DAC0_C0 = 0x4003f021u,
    DAC0_C1 = 0x4003f022u,
    DAC0_C2 = 0x4003f023u,
    DAC1_DAT0L = 0x40028000u,
    SIM_SCGC4 = 0x40048034u,
    SIM_SCGC6 = 0x4004803cu,
    SIM_SCGC7 = 0x40048040u,
    SIM_SCGC1 = 0x40048028u,
    SIM_SCGC3 = 0x40048030u,
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

static bool read16(KinetisK22* device, uint32_t address, uint16_t* value) {
    return kinetis_k22_read(device, address, value, sizeof(*value));
}

static bool write32(KinetisK22* device, uint32_t address, uint32_t value) {
    return kinetis_k22_write(device, address, &value, sizeof(value));
}

static bool cpu_write8(KinetisK22* device, uint32_t address, uint8_t value) {
    return cortex_m4_write_memory(kinetis_k22_cpu(device), address, 1u, value);
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
    TEST_EXPECT(state, cpu_write8(device, FTFA_FCCOB0, 0x06u));
    TEST_EXPECT(state, cpu_write8(device, FTFA_FCCOB0 + 1u, 0x00u));
    TEST_EXPECT(state, cpu_write8(device, FTFA_FCCOB0 + 2u, 0x10u));
    TEST_EXPECT(state, cpu_write8(device, FTFA_FCCOB0 + 3u, 0x00u));
    TEST_EXPECT(state, cpu_write8(device, FTFA_FCCOB0 + 4u, 0x12u));
    TEST_EXPECT(state, cpu_write8(device, FTFA_FCCOB0 + 5u, 0x34u));
    TEST_EXPECT(state, cpu_write8(device, FTFA_FCCOB0 + 6u, 0x56u));
    TEST_EXPECT(state, cpu_write8(device, FTFA_FCCOB0 + 7u, 0x78u));
    TEST_EXPECT(state, cpu_write8(device, FTFA_FSTAT, 0x80u));
    TEST_EXPECT(state, read32(device, target, &value));
    TEST_EXPECT(state, value == 0x12345678u);
    const uint32_t fstat_bit_band =
        0x42000000u + (FTFA_FSTAT - 0x40000000u) * 32u + 4u * 4u;
    TEST_EXPECT(state, write32(device, fstat_bit_band, 1u));
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

    const uint32_t alias = 0x42000000u + (FMC_TAGVDW0S0 - 0x40000000u) * 32u + 5u * 4u;
    TEST_EXPECT(state, write32(device, alias, 1));
    TEST_EXPECT(state, read32(device, FMC_TAGVDW0S0, &value));
    TEST_EXPECT(state, value == 0x20u);
    TEST_EXPECT(state, read32(device, alias, &value));
    TEST_EXPECT(state, value == 1);
    TEST_EXPECT(state, write32(device, FMC_TAGVDW0S0, 0x21u));
    TEST_EXPECT(state, write32(device, FMC_TAGVDW1S0, 0x41u));
    TEST_EXPECT(state, write32(device, FMC_PFB0CR, (1u << 19u) | (1u << 20u)));
    TEST_EXPECT(state, read32(device, FMC_TAGVDW0S0, &value));
    TEST_EXPECT(state, value == 0x20u);
    TEST_EXPECT(state, read32(device, FMC_TAGVDW1S0, &value));
    TEST_EXPECT(state, value == 0x41u);
    TEST_EXPECT(state, read32(device, FMC_PFB0CR, &value));
    TEST_EXPECT(state, (value & 0x00f80000u) == 0u);
    TEST_EXPECT(state, write32(device, FMC_PFB0CR, 1u << 21u));
    TEST_EXPECT(state, read32(device, FMC_TAGVDW1S0, &value));
    TEST_EXPECT(state, value == 0x40u);
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
    expect_sdhc_integration(&state);
    KinetisK22* device = create_device(&state, KINETIS_K22_PACKAGE_DC_121_XFBGA);
    TEST_EXPECT(&state, kinetis_k22_reset(device));
    TEST_EXPECT(&state, kinetis_k22_core_clock_hz(device) == 20971520u);
    TEST_EXPECT(&state, kinetis_k22_bus_clock_hz(device) == 20971520u);
    expect_manifest_fallback(&state, device);
    expect_integrated_flash_command(&state, device);
    expect_clock_gates(&state, device);
    expect_endpoint_event_order(&state, device);
    expect_pdb_data_triggers(&state, device);
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
