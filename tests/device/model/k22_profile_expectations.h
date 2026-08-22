#ifndef KINETIS_K22_SIM_K22_PROFILE_EXPECTATIONS_H
#define KINETIS_K22_SIM_K22_PROFILE_EXPECTATIONS_H

#include <stddef.h>
#include <stdint.h>

#include "k22_profile.h"

typedef struct {
    K22PeripheralId id;
    uint32_t address;
    uint32_t size;
} K22ExpectedBlock;

typedef struct {
    K22ProfileId id;
    const char* name;
    uint32_t program_flash_size;
    uint32_t sram_lower_address;
    uint32_t sram_lower_size;
    uint32_t sram_upper_address;
    uint32_t sram_upper_size;
    uint32_t flexnvm_address;
    uint32_t flexnvm_size;
    uint32_t flexram_address;
    uint32_t flexram_size;
    uint32_t sim_sdid_reset;
    uint32_t sim_sdid_mask;
    uint32_t maximum_core_clock_hz;
    uint16_t external_irq_count;
    const K22ExpectedBlock* blocks;
    size_t block_count;
} K22ExpectedProfile;

#define EXPECTED_BLOCK(peripheral, base, length) {K22_PERIPHERAL_##peripheral, base, length}

static const K22ExpectedBlock expected_mk22f12810_blocks[] = {
    EXPECTED_BLOCK(FLASH_CONFIG, 0x00000400u, 0x0eu), EXPECTED_BLOCK(DMA, 0x40008000u, 0x1080u),
    EXPECTED_BLOCK(FMC, 0x4001f000u, 0x300u),         EXPECTED_BLOCK(FTFA, 0x40020000u, 0x2cu),
    EXPECTED_BLOCK(DMAMUX, 0x40021000u, 0x04u),       EXPECTED_BLOCK(ADC1, 0x40027000u, 0x70u),
    EXPECTED_BLOCK(LPUART0, 0x4002a000u, 0x18u),      EXPECTED_BLOCK(SPI0, 0x4002c000u, 0x8cu),
    EXPECTED_BLOCK(SPI1, 0x4002d000u, 0x8cu),         EXPECTED_BLOCK(I2S0, 0x4002f000u, 0x108u),
    EXPECTED_BLOCK(CRC, 0x40032000u, 0x0cu),          EXPECTED_BLOCK(PDB0, 0x40036000u, 0x19cu),
    EXPECTED_BLOCK(PIT, 0x40037000u, 0x140u),         EXPECTED_BLOCK(FTM0, 0x40038000u, 0x9cu),
    EXPECTED_BLOCK(FTM1, 0x40039000u, 0x9cu),         EXPECTED_BLOCK(FTM2, 0x4003a000u, 0x9cu),
    EXPECTED_BLOCK(ADC0, 0x4003b000u, 0x70u),         EXPECTED_BLOCK(RTC, 0x4003d000u, 0x808u),
    EXPECTED_BLOCK(RFVBAT, 0x4003e000u, 0x20u),       EXPECTED_BLOCK(DAC0, 0x4003f000u, 0x24u),
    EXPECTED_BLOCK(LPTMR0, 0x40040000u, 0x10u),       EXPECTED_BLOCK(RFSYS, 0x40041000u, 0x20u),
    EXPECTED_BLOCK(SIM, 0x40047000u, 0x1064u),        EXPECTED_BLOCK(PORTA, 0x40049000u, 0xa4u),
    EXPECTED_BLOCK(PORTB, 0x4004a000u, 0xa4u),        EXPECTED_BLOCK(PORTC, 0x4004b000u, 0xa4u),
    EXPECTED_BLOCK(PORTD, 0x4004c000u, 0xccu),        EXPECTED_BLOCK(PORTE, 0x4004d000u, 0xa4u),
    EXPECTED_BLOCK(WDOG, 0x40052000u, 0x18u),         EXPECTED_BLOCK(EWM, 0x40061000u, 0x06u),
    EXPECTED_BLOCK(MCG, 0x40064000u, 0x0eu),          EXPECTED_BLOCK(OSC, 0x40065000u, 0x03u),
    EXPECTED_BLOCK(I2C0, 0x40066000u, 0x0cu),         EXPECTED_BLOCK(I2C1, 0x40067000u, 0x0cu),
    EXPECTED_BLOCK(UART0, 0x4006a000u, 0x40u),        EXPECTED_BLOCK(UART1, 0x4006b000u, 0x17u),
    EXPECTED_BLOCK(UART2, 0x4006c000u, 0x17u),        EXPECTED_BLOCK(USB0, 0x40072000u, 0x15du),
    EXPECTED_BLOCK(CMP0, 0x40073000u, 0x06u),         EXPECTED_BLOCK(CMP1, 0x40073008u, 0x06u),
    EXPECTED_BLOCK(VREF, 0x40074000u, 0x02u),         EXPECTED_BLOCK(LLWU, 0x4007c000u, 0x0au),
    EXPECTED_BLOCK(PMC, 0x4007d000u, 0x03u),          EXPECTED_BLOCK(SMC, 0x4007e000u, 0x04u),
    EXPECTED_BLOCK(RCM, 0x4007f000u, 0x0au),          EXPECTED_BLOCK(GPIOA, 0x400ff000u, 0x18u),
    EXPECTED_BLOCK(GPIOB, 0x400ff040u, 0x18u),        EXPECTED_BLOCK(GPIOC, 0x400ff080u, 0x18u),
    EXPECTED_BLOCK(GPIOD, 0x400ff0c0u, 0x18u),        EXPECTED_BLOCK(GPIOE, 0x400ff100u, 0x18u),
    EXPECTED_BLOCK(MCM, 0xe0080008u, 0x3cu),
};

static const K22ExpectedBlock expected_mk22f25612_blocks[] = {
    EXPECTED_BLOCK(FLASH_CONFIG, 0x00000400u, 0x0eu), EXPECTED_BLOCK(DMA, 0x40008000u, 0x1200u),
    EXPECTED_BLOCK(FMC, 0x4001f000u, 0x300u),         EXPECTED_BLOCK(FTFA, 0x40020000u, 0x2cu),
    EXPECTED_BLOCK(DMAMUX, 0x40021000u, 0x10u),       EXPECTED_BLOCK(ADC1, 0x40027000u, 0x70u),
    EXPECTED_BLOCK(RNG, 0x40029000u, 0x10u),          EXPECTED_BLOCK(LPUART0, 0x4002a000u, 0x18u),
    EXPECTED_BLOCK(SPI0, 0x4002c000u, 0x8cu),         EXPECTED_BLOCK(SPI1, 0x4002d000u, 0x8cu),
    EXPECTED_BLOCK(I2S0, 0x4002f000u, 0x108u),        EXPECTED_BLOCK(CRC, 0x40032000u, 0x0cu),
    EXPECTED_BLOCK(PDB0, 0x40036000u, 0x19cu),        EXPECTED_BLOCK(PIT, 0x40037000u, 0x140u),
    EXPECTED_BLOCK(FTM0, 0x40038000u, 0x9cu),         EXPECTED_BLOCK(FTM1, 0x40039000u, 0x9cu),
    EXPECTED_BLOCK(FTM2, 0x4003a000u, 0x9cu),         EXPECTED_BLOCK(ADC0, 0x4003b000u, 0x70u),
    EXPECTED_BLOCK(RTC, 0x4003d000u, 0x808u),         EXPECTED_BLOCK(RFVBAT, 0x4003e000u, 0x20u),
    EXPECTED_BLOCK(DAC0, 0x4003f000u, 0x24u),         EXPECTED_BLOCK(LPTMR0, 0x40040000u, 0x10u),
    EXPECTED_BLOCK(RFSYS, 0x40041000u, 0x20u),        EXPECTED_BLOCK(SIM, 0x40047000u, 0x1064u),
    EXPECTED_BLOCK(PORTA, 0x40049000u, 0xa4u),        EXPECTED_BLOCK(PORTB, 0x4004a000u, 0xa4u),
    EXPECTED_BLOCK(PORTC, 0x4004b000u, 0xa4u),        EXPECTED_BLOCK(PORTD, 0x4004c000u, 0xccu),
    EXPECTED_BLOCK(PORTE, 0x4004d000u, 0xa4u),        EXPECTED_BLOCK(WDOG, 0x40052000u, 0x18u),
    EXPECTED_BLOCK(EWM, 0x40061000u, 0x06u),          EXPECTED_BLOCK(MCG, 0x40064000u, 0x0eu),
    EXPECTED_BLOCK(OSC, 0x40065000u, 0x03u),          EXPECTED_BLOCK(I2C0, 0x40066000u, 0x0cu),
    EXPECTED_BLOCK(I2C1, 0x40067000u, 0x0cu),         EXPECTED_BLOCK(UART0, 0x4006a000u, 0x40u),
    EXPECTED_BLOCK(UART1, 0x4006b000u, 0x17u),        EXPECTED_BLOCK(UART2, 0x4006c000u, 0x17u),
    EXPECTED_BLOCK(USB0, 0x40072000u, 0x15du),        EXPECTED_BLOCK(CMP0, 0x40073000u, 0x06u),
    EXPECTED_BLOCK(CMP1, 0x40073008u, 0x06u),         EXPECTED_BLOCK(VREF, 0x40074000u, 0x02u),
    EXPECTED_BLOCK(LLWU, 0x4007c000u, 0x0au),         EXPECTED_BLOCK(PMC, 0x4007d000u, 0x03u),
    EXPECTED_BLOCK(SMC, 0x4007e000u, 0x04u),          EXPECTED_BLOCK(RCM, 0x4007f000u, 0x0au),
    EXPECTED_BLOCK(GPIOA, 0x400ff000u, 0x18u),        EXPECTED_BLOCK(GPIOB, 0x400ff040u, 0x18u),
    EXPECTED_BLOCK(GPIOC, 0x400ff080u, 0x18u),        EXPECTED_BLOCK(GPIOD, 0x400ff0c0u, 0x18u),
    EXPECTED_BLOCK(GPIOE, 0x400ff100u, 0x18u),        EXPECTED_BLOCK(MCM, 0xe0080008u, 0x3cu),
};

static const K22ExpectedBlock expected_mk22f51212_blocks[] = {
    EXPECTED_BLOCK(FLASH_CONFIG, 0x00000400u, 0x0eu), EXPECTED_BLOCK(DMA, 0x40008000u, 0x1200u),
    EXPECTED_BLOCK(FB, 0x4000c000u, 0x64u),           EXPECTED_BLOCK(FMC, 0x4001f000u, 0x300u),
    EXPECTED_BLOCK(FTFA, 0x40020000u, 0x2cu),         EXPECTED_BLOCK(DMAMUX, 0x40021000u, 0x10u),
    EXPECTED_BLOCK(FTM3, 0x40026000u, 0x9cu),         EXPECTED_BLOCK(ADC1, 0x40027000u, 0x70u),
    EXPECTED_BLOCK(DAC1, 0x40028000u, 0x24u),         EXPECTED_BLOCK(RNG, 0x40029000u, 0x10u),
    EXPECTED_BLOCK(LPUART0, 0x4002a000u, 0x18u),      EXPECTED_BLOCK(SPI0, 0x4002c000u, 0x8cu),
    EXPECTED_BLOCK(SPI1, 0x4002d000u, 0x8cu),         EXPECTED_BLOCK(I2S0, 0x4002f000u, 0x108u),
    EXPECTED_BLOCK(CRC, 0x40032000u, 0x0cu),          EXPECTED_BLOCK(PDB0, 0x40036000u, 0x19cu),
    EXPECTED_BLOCK(PIT, 0x40037000u, 0x140u),         EXPECTED_BLOCK(FTM0, 0x40038000u, 0x9cu),
    EXPECTED_BLOCK(FTM1, 0x40039000u, 0x9cu),         EXPECTED_BLOCK(FTM2, 0x4003a000u, 0x9cu),
    EXPECTED_BLOCK(ADC0, 0x4003b000u, 0x70u),         EXPECTED_BLOCK(RTC, 0x4003d000u, 0x808u),
    EXPECTED_BLOCK(RFVBAT, 0x4003e000u, 0x20u),       EXPECTED_BLOCK(DAC0, 0x4003f000u, 0x24u),
    EXPECTED_BLOCK(LPTMR0, 0x40040000u, 0x10u),       EXPECTED_BLOCK(RFSYS, 0x40041000u, 0x20u),
    EXPECTED_BLOCK(SIM, 0x40047000u, 0x1064u),        EXPECTED_BLOCK(PORTA, 0x40049000u, 0xa4u),
    EXPECTED_BLOCK(PORTB, 0x4004a000u, 0xa4u),        EXPECTED_BLOCK(PORTC, 0x4004b000u, 0xa4u),
    EXPECTED_BLOCK(PORTD, 0x4004c000u, 0xccu),        EXPECTED_BLOCK(PORTE, 0x4004d000u, 0xa4u),
    EXPECTED_BLOCK(WDOG, 0x40052000u, 0x18u),         EXPECTED_BLOCK(EWM, 0x40061000u, 0x06u),
    EXPECTED_BLOCK(MCG, 0x40064000u, 0x0eu),          EXPECTED_BLOCK(OSC, 0x40065000u, 0x03u),
    EXPECTED_BLOCK(I2C0, 0x40066000u, 0x0cu),         EXPECTED_BLOCK(I2C1, 0x40067000u, 0x0cu),
    EXPECTED_BLOCK(UART0, 0x4006a000u, 0x40u),        EXPECTED_BLOCK(UART1, 0x4006b000u, 0x17u),
    EXPECTED_BLOCK(UART2, 0x4006c000u, 0x17u),        EXPECTED_BLOCK(USB0, 0x40072000u, 0x15du),
    EXPECTED_BLOCK(CMP0, 0x40073000u, 0x06u),         EXPECTED_BLOCK(CMP1, 0x40073008u, 0x06u),
    EXPECTED_BLOCK(VREF, 0x40074000u, 0x02u),         EXPECTED_BLOCK(LLWU, 0x4007c000u, 0x0au),
    EXPECTED_BLOCK(PMC, 0x4007d000u, 0x03u),          EXPECTED_BLOCK(SMC, 0x4007e000u, 0x04u),
    EXPECTED_BLOCK(RCM, 0x4007f000u, 0x0au),          EXPECTED_BLOCK(GPIOA, 0x400ff000u, 0x18u),
    EXPECTED_BLOCK(GPIOB, 0x400ff040u, 0x18u),        EXPECTED_BLOCK(GPIOC, 0x400ff080u, 0x18u),
    EXPECTED_BLOCK(GPIOD, 0x400ff0c0u, 0x18u),        EXPECTED_BLOCK(GPIOE, 0x400ff100u, 0x18u),
    EXPECTED_BLOCK(MCM, 0xe0080008u, 0x3cu),
};

static const K22ExpectedBlock expected_mk22f12_blocks[] = {
    EXPECTED_BLOCK(FLASH_CONFIG, 0x00000400u, 0x10u), EXPECTED_BLOCK(AIPS0, 0x40000000u, 0x70u),
    EXPECTED_BLOCK(AIPS1, 0x40080000u, 0x70u),        EXPECTED_BLOCK(AXBS, 0x40004000u, 0xc04u),
    EXPECTED_BLOCK(DMA, 0x40008000u, 0x1200u),        EXPECTED_BLOCK(FB, 0x4000c000u, 0x64u),
    EXPECTED_BLOCK(SYSMPU, 0x4000d000u, 0x830u),      EXPECTED_BLOCK(FMC, 0x4001f000u, 0x300u),
    EXPECTED_BLOCK(FTFE, 0x40020000u, 0x18u),         EXPECTED_BLOCK(DMAMUX, 0x40021000u, 0x10u),
    EXPECTED_BLOCK(CAN0, 0x40024000u, 0x8c0u),        EXPECTED_BLOCK(SPI0, 0x4002c000u, 0x8cu),
    EXPECTED_BLOCK(SPI1, 0x4002d000u, 0x8cu),         EXPECTED_BLOCK(SPI2, 0x400ac000u, 0x8cu),
    EXPECTED_BLOCK(I2S0, 0x4002f000u, 0x108u),        EXPECTED_BLOCK(CRC, 0x40032000u, 0x0cu),
    EXPECTED_BLOCK(USBDCD, 0x40035000u, 0x1cu),       EXPECTED_BLOCK(PDB0, 0x40036000u, 0x1a0u),
    EXPECTED_BLOCK(PIT, 0x40037000u, 0x140u),         EXPECTED_BLOCK(FTM0, 0x40038000u, 0x9cu),
    EXPECTED_BLOCK(FTM1, 0x40039000u, 0x9cu),         EXPECTED_BLOCK(ADC0, 0x4003b000u, 0x70u),
    EXPECTED_BLOCK(RTC, 0x4003d000u, 0x808u),         EXPECTED_BLOCK(RFVBAT, 0x4003e000u, 0x20u),
    EXPECTED_BLOCK(LPTMR0, 0x40040000u, 0x10u),       EXPECTED_BLOCK(RFSYS, 0x40041000u, 0x20u),
    EXPECTED_BLOCK(SIM, 0x40047000u, 0x1064u),        EXPECTED_BLOCK(PORTA, 0x40049000u, 0xa4u),
    EXPECTED_BLOCK(PORTB, 0x4004a000u, 0xa4u),        EXPECTED_BLOCK(PORTC, 0x4004b000u, 0xa4u),
    EXPECTED_BLOCK(PORTD, 0x4004c000u, 0xccu),        EXPECTED_BLOCK(PORTE, 0x4004d000u, 0xa4u),
    EXPECTED_BLOCK(WDOG, 0x40052000u, 0x18u),         EXPECTED_BLOCK(EWM, 0x40061000u, 0x04u),
    EXPECTED_BLOCK(CMT, 0x40062000u, 0x0cu),          EXPECTED_BLOCK(MCG, 0x40064000u, 0x0eu),
    EXPECTED_BLOCK(OSC, 0x40065000u, 0x01u),          EXPECTED_BLOCK(I2C0, 0x40066000u, 0x0cu),
    EXPECTED_BLOCK(I2C1, 0x40067000u, 0x0cu),         EXPECTED_BLOCK(UART0, 0x4006a000u, 0x20u),
    EXPECTED_BLOCK(UART1, 0x4006b000u, 0x17u),        EXPECTED_BLOCK(UART2, 0x4006c000u, 0x17u),
    EXPECTED_BLOCK(UART3, 0x4006d000u, 0x17u),        EXPECTED_BLOCK(SDHC, 0x400b1000u, 0x1000u),
    EXPECTED_BLOCK(USB0, 0x40072000u, 0x115u),        EXPECTED_BLOCK(CMP0, 0x40073000u, 0x06u),
    EXPECTED_BLOCK(CMP1, 0x40073008u, 0x06u),         EXPECTED_BLOCK(CMP2, 0x40073010u, 0x06u),
    EXPECTED_BLOCK(VREF, 0x40074000u, 0x02u),         EXPECTED_BLOCK(LLWU, 0x4007c000u, 0x0bu),
    EXPECTED_BLOCK(PMC, 0x4007d000u, 0x03u),          EXPECTED_BLOCK(SMC, 0x4007e000u, 0x04u),
    EXPECTED_BLOCK(RCM, 0x4007f000u, 0x08u),          EXPECTED_BLOCK(FTM2, 0x400b8000u, 0x9cu),
    EXPECTED_BLOCK(FTM3, 0x400b9000u, 0x9cu),         EXPECTED_BLOCK(ADC1, 0x400bb000u, 0x70u),
    EXPECTED_BLOCK(DAC0, 0x400cc000u, 0x24u),         EXPECTED_BLOCK(DAC1, 0x400cd000u, 0x24u),
    EXPECTED_BLOCK(I2C2, 0x400e6000u, 0x0cu),         EXPECTED_BLOCK(UART4, 0x400ea000u, 0x17u),
    EXPECTED_BLOCK(UART5, 0x400eb000u, 0x17u),        EXPECTED_BLOCK(GPIOA, 0x400ff000u, 0x18u),
    EXPECTED_BLOCK(GPIOB, 0x400ff040u, 0x18u),        EXPECTED_BLOCK(GPIOC, 0x400ff080u, 0x18u),
    EXPECTED_BLOCK(GPIOD, 0x400ff0c0u, 0x18u),        EXPECTED_BLOCK(GPIOE, 0x400ff100u, 0x18u),
    EXPECTED_BLOCK(MCM, 0xe0080008u, 0x2cu),
};

#define EXPECTED_COUNT(array) (sizeof(array) / sizeof((array)[0]))

static const K22ExpectedProfile expected_k22_profiles[] = {
    {.id = K22_PROFILE_MK22F12810,
     .name = "MK22F12810",
     .program_flash_size = 0x20000u,
     .sram_lower_address = 0x1fffe000u,
     .sram_lower_size = 0x2000u,
     .sram_upper_address = 0x20000000u,
     .sram_upper_size = 0x4000u,
     .sim_sdid_reset = 0x680u,
     .sim_sdid_mask = 0x000f0f80u,
     .maximum_core_clock_hz = 100000000u,
     .external_irq_count = 86u,
     .blocks = expected_mk22f12810_blocks,
     .block_count = EXPECTED_COUNT(expected_mk22f12810_blocks)},
    {.id = K22_PROFILE_MK22FN12812,
     .name = "MK22FN12812",
     .program_flash_size = 0x20000u,
     .sram_lower_address = 0x1fffc000u,
     .sram_lower_size = 0x4000u,
     .sram_upper_address = 0x20000000u,
     .sram_upper_size = 0x8000u,
     .sim_sdid_reset = 0xa80u,
     .sim_sdid_mask = 0x000f0f80u,
     .maximum_core_clock_hz = 120000000u,
     .external_irq_count = 86u,
     .blocks = expected_mk22f25612_blocks,
     .block_count = EXPECTED_COUNT(expected_mk22f25612_blocks)},
    {.id = K22_PROFILE_MK22FN25612,
     .name = "MK22FN25612",
     .program_flash_size = 0x40000u,
     .sram_lower_address = 0x1fffc000u,
     .sram_lower_size = 0x4000u,
     .sram_upper_address = 0x20000000u,
     .sram_upper_size = 0x8000u,
     .sim_sdid_reset = 0xa80u,
     .sim_sdid_mask = 0x000f0f80u,
     .maximum_core_clock_hz = 120000000u,
     .external_irq_count = 86u,
     .blocks = expected_mk22f25612_blocks,
     .block_count = EXPECTED_COUNT(expected_mk22f25612_blocks)},
    {.id = K22_PROFILE_MK22FN51212,
     .name = "MK22FN51212",
     .program_flash_size = 0x80000u,
     .sram_lower_address = 0x1fff0000u,
     .sram_lower_size = 0x10000u,
     .sram_upper_address = 0x20000000u,
     .sram_upper_size = 0x10000u,
     .sim_sdid_reset = 0xe80u,
     .sim_sdid_mask = 0x000f0f80u,
     .maximum_core_clock_hz = 120000000u,
     .external_irq_count = 86u,
     .blocks = expected_mk22f51212_blocks,
     .block_count = EXPECTED_COUNT(expected_mk22f51212_blocks)},
    {.id = K22_PROFILE_MK22FN1M012,
     .name = "MK22FN1M012",
     .program_flash_size = 0x100000u,
     .sram_lower_address = 0x1fff0000u,
     .sram_lower_size = 0x10000u,
     .sram_upper_address = 0x20000000u,
     .sram_upper_size = 0x10000u,
     .flexram_address = 0x14000000u,
     .flexram_size = 0x1000u,
     .sim_sdid_reset = 0x300u,
     .sim_sdid_mask = 0xffff0f80u,
     .maximum_core_clock_hz = 120000000u,
     .external_irq_count = 82u,
     .blocks = expected_mk22f12_blocks,
     .block_count = EXPECTED_COUNT(expected_mk22f12_blocks)},
    {.id = K22_PROFILE_MK22FX51212,
     .name = "MK22FX51212",
     .program_flash_size = 0x80000u,
     .sram_lower_address = 0x1fff0000u,
     .sram_lower_size = 0x10000u,
     .sram_upper_address = 0x20000000u,
     .sram_upper_size = 0x10000u,
     .flexnvm_address = 0x10000000u,
     .flexnvm_size = 0x20000u,
     .flexram_address = 0x14000000u,
     .flexram_size = 0x1000u,
     .sim_sdid_reset = 0x300u,
     .sim_sdid_mask = 0xffff0f80u,
     .maximum_core_clock_hz = 120000000u,
     .external_irq_count = 82u,
     .blocks = expected_mk22f12_blocks,
     .block_count = EXPECTED_COUNT(expected_mk22f12_blocks)},
};

#endif
