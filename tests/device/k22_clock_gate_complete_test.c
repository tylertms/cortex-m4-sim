#include "kinetis_k22.h"

#include <stdint.h>

#include "kinetis_k22_internal.h"
#include "test.h"

enum {
    SIM_SCGC1 = 0x40048028u,
    SIM_SCGC2 = 0x4004802cu,
    SIM_SCGC3 = 0x40048030u,
    SIM_SCGC4 = 0x40048034u,
    SIM_SCGC5 = 0x40048038u,
    SIM_SCGC6 = 0x4004803cu,
    SIM_SCGC7 = 0x40048040u,
};

typedef struct {
    K22PeripheralId peripheral;
    uint32_t register_address;
    uint8_t bit;
} GateCase;

typedef struct {
    uint32_t address;
    uint8_t size;
} RegisterAccess;

static const GateCase cases[] = {
    {K22_PERIPHERAL_DMA, SIM_SCGC7, 1u},     {K22_PERIPHERAL_FB, SIM_SCGC7, 0u},
    {K22_PERIPHERAL_SYSMPU, SIM_SCGC7, 2u},  {K22_PERIPHERAL_FTFE, SIM_SCGC6, 0u},
    {K22_PERIPHERAL_DMAMUX, SIM_SCGC6, 1u},  {K22_PERIPHERAL_CAN0, SIM_SCGC6, 4u},
    {K22_PERIPHERAL_FTM0, SIM_SCGC6, 24u},   {K22_PERIPHERAL_FTM1, SIM_SCGC6, 25u},
    {K22_PERIPHERAL_FTM2, SIM_SCGC3, 24u},   {K22_PERIPHERAL_FTM3, SIM_SCGC3, 25u},
    {K22_PERIPHERAL_ADC0, SIM_SCGC6, 27u},   {K22_PERIPHERAL_ADC1, SIM_SCGC3, 27u},
    {K22_PERIPHERAL_DAC0, SIM_SCGC2, 12u},   {K22_PERIPHERAL_DAC1, SIM_SCGC2, 13u},
    {K22_PERIPHERAL_SPI0, SIM_SCGC6, 12u},   {K22_PERIPHERAL_SPI1, SIM_SCGC6, 13u},
    {K22_PERIPHERAL_SPI2, SIM_SCGC3, 12u},   {K22_PERIPHERAL_SDHC, SIM_SCGC3, 17u},
    {K22_PERIPHERAL_I2S0, SIM_SCGC6, 15u},   {K22_PERIPHERAL_CRC, SIM_SCGC6, 18u},
    {K22_PERIPHERAL_USBDCD, SIM_SCGC6, 21u}, {K22_PERIPHERAL_PDB0, SIM_SCGC6, 22u},
    {K22_PERIPHERAL_PIT, SIM_SCGC6, 23u},    {K22_PERIPHERAL_RTC, SIM_SCGC6, 29u},
    {K22_PERIPHERAL_LPTMR0, SIM_SCGC5, 0u},  {K22_PERIPHERAL_CMT, SIM_SCGC4, 2u},
    {K22_PERIPHERAL_I2C0, SIM_SCGC4, 6u},    {K22_PERIPHERAL_I2C1, SIM_SCGC4, 7u},
    {K22_PERIPHERAL_I2C2, SIM_SCGC1, 6u},    {K22_PERIPHERAL_UART0, SIM_SCGC4, 10u},
    {K22_PERIPHERAL_UART1, SIM_SCGC4, 11u},  {K22_PERIPHERAL_UART2, SIM_SCGC4, 12u},
    {K22_PERIPHERAL_UART3, SIM_SCGC4, 13u},  {K22_PERIPHERAL_UART4, SIM_SCGC1, 10u},
    {K22_PERIPHERAL_UART5, SIM_SCGC1, 11u},  {K22_PERIPHERAL_USB0, SIM_SCGC4, 18u},
    {K22_PERIPHERAL_CMP0, SIM_SCGC4, 19u},   {K22_PERIPHERAL_CMP1, SIM_SCGC4, 19u},
    {K22_PERIPHERAL_CMP2, SIM_SCGC4, 19u},   {K22_PERIPHERAL_VREF, SIM_SCGC4, 20u},
    {K22_PERIPHERAL_PORTA, SIM_SCGC5, 9u},   {K22_PERIPHERAL_PORTB, SIM_SCGC5, 10u},
    {K22_PERIPHERAL_PORTC, SIM_SCGC5, 11u},  {K22_PERIPHERAL_PORTD, SIM_SCGC5, 12u},
    {K22_PERIPHERAL_PORTE, SIM_SCGC5, 13u},  {K22_PERIPHERAL_GPIOA, SIM_SCGC5, 9u},
    {K22_PERIPHERAL_GPIOB, SIM_SCGC5, 10u},  {K22_PERIPHERAL_GPIOC, SIM_SCGC5, 11u},
    {K22_PERIPHERAL_GPIOD, SIM_SCGC5, 12u},  {K22_PERIPHERAL_GPIOE, SIM_SCGC5, 13u},
};

static const GateCase small_cases[] = {
    {K22_PERIPHERAL_RNG, SIM_SCGC6, 9u},   {K22_PERIPHERAL_LPUART0, SIM_SCGC6, 10u},
    {K22_PERIPHERAL_FTM2, SIM_SCGC6, 26u}, {K22_PERIPHERAL_FTM3, SIM_SCGC6, 6u},
    {K22_PERIPHERAL_ADC1, SIM_SCGC6, 7u},  {K22_PERIPHERAL_DAC1, SIM_SCGC6, 8u},
};

static KinetisK22* create_device(TestState* state, KinetisK22Profile profile,
                                 KinetisK22Package package) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.profile = profile;
    configuration.package = package;
    configuration.flash_size = 4096u;
    configuration.sram_size = 65536u;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(state, device != NULL);
    return device;
}

static RegisterAccess readable_register(KinetisK22* device, K22PeripheralId id) {
    K22PeripheralBlock block;
    if (!k22_profile_peripheral_block(device->profile, id, &block)) {
        return (RegisterAccess){0u, 0u};
    }
    for (size_t index = 0u; index < device->manifest->register_count; index++) {
        const K22RegisterDescriptor* descriptor = &device->manifest->registers[index];
        if (descriptor->address >= block.address &&
            descriptor->address < block.address + block.size &&
            (descriptor->access & K22_REGISTER_ACCESS_READ) != 0u) {
            return (RegisterAccess){descriptor->address, (uint8_t)(descriptor->width / 8u)};
        }
    }
    const uint8_t sizes[] = {1u, 2u, 4u};
    for (size_t index = 0u; index < sizeof(sizes); index++) {
        uint32_t value = 0u;
        if (kinetis_k22_peripheral_read(device, block.address, sizes[index],
                                        CORTEX_M4_ACCESS_DEBUG, &value)) {
            return (RegisterAccess){block.address, sizes[index]};
        }
    }
    return (RegisterAccess){0u, 0u};
}

static void clear_gates(TestState* state, KinetisK22* device) {
    const uint32_t registers[] = {SIM_SCGC1, SIM_SCGC2, SIM_SCGC3, SIM_SCGC4,
                                  SIM_SCGC5, SIM_SCGC6, SIM_SCGC7};
    for (size_t index = 0u; index < sizeof(registers) / sizeof(registers[0]); index++) {
        if (k22_register_manifest_lookup(device->profile->id, registers[index], 32u) !=
            NULL) {
            TEST_EXPECT(state, kinetis_k22_peripheral_write(device, registers[index], 4u,
                                                            CORTEX_M4_ACCESS_DEBUG, 0u));
        }
    }
}

static void test_cases(TestState* state, KinetisK22* device, const GateCase* values,
                       size_t count) {
    clear_gates(state, device);
    for (size_t index = 0u; index < count; index++) {
        const RegisterAccess access = readable_register(device, values[index].peripheral);
        TEST_EXPECT(state, access.size != 0u);
        if (access.size == 0u) {
            continue;
        }
        uint32_t value = 0u;
        TEST_EXPECT(state, !kinetis_k22_peripheral_read(device, access.address, access.size,
                                                        CORTEX_M4_ACCESS_DATA, &value));
        TEST_EXPECT(state, kinetis_k22_peripheral_write(
                               device, values[index].register_address, 4u,
                               CORTEX_M4_ACCESS_DEBUG, 1u << values[index].bit));
        const bool enabled = kinetis_k22_peripheral_read(
            device, access.address, access.size, CORTEX_M4_ACCESS_DATA, &value);
        TEST_EXPECT(state, enabled);
        TEST_EXPECT(state,
                    kinetis_k22_peripheral_write(device, values[index].register_address, 4u,
                                                 CORTEX_M4_ACCESS_DEBUG, 0u));
    }
}

int main(void) {
    TestState state = {0};
    KinetisK22* large = create_device(&state, KINETIS_K22_PROFILE_MK22FN1M012,
                                      KINETIS_K22_PACKAGE_LQ_144_LQFP);
    KinetisK22* small = create_device(&state, KINETIS_K22_PROFILE_MK22FN51212,
                                      KINETIS_K22_PACKAGE_DC_121_XFBGA);
    test_cases(&state, large, cases, sizeof(cases) / sizeof(cases[0]));
    test_cases(&state, small, small_cases, sizeof(small_cases) / sizeof(small_cases[0]));
    kinetis_k22_destroy(large);
    kinetis_k22_destroy(small);
    return test_finish(&state);
}
