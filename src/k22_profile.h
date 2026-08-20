#ifndef CORTEX_M4_K22_PROFILE_H
#define CORTEX_M4_K22_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kinetis_k22.h"

typedef KinetisK22Profile K22ProfileId;

#define K22_PROFILE_MK22F12810 KINETIS_K22_PROFILE_MK22F12810
#define K22_PROFILE_MK22FN12812 KINETIS_K22_PROFILE_MK22FN12812
#define K22_PROFILE_MK22FN25612 KINETIS_K22_PROFILE_MK22FN25612
#define K22_PROFILE_MK22FN51212 KINETIS_K22_PROFILE_MK22FN51212
#define K22_PROFILE_MK22FN1M012 KINETIS_K22_PROFILE_MK22FN1M012
#define K22_PROFILE_MK22FX51212 KINETIS_K22_PROFILE_MK22FX51212
#define K22_PROFILE_COUNT KINETIS_K22_PROFILE_COUNT

typedef enum {
    K22_PERIPHERAL_FLASH_CONFIG,
    K22_PERIPHERAL_AIPS0,
    K22_PERIPHERAL_AIPS1,
    K22_PERIPHERAL_AXBS,
    K22_PERIPHERAL_DMA,
    K22_PERIPHERAL_FB,
    K22_PERIPHERAL_SYSMPU,
    K22_PERIPHERAL_FMC,
    K22_PERIPHERAL_FTFA,
    K22_PERIPHERAL_FTFE,
    K22_PERIPHERAL_DMAMUX,
    K22_PERIPHERAL_CAN0,
    K22_PERIPHERAL_FTM0,
    K22_PERIPHERAL_FTM1,
    K22_PERIPHERAL_FTM2,
    K22_PERIPHERAL_FTM3,
    K22_PERIPHERAL_ADC0,
    K22_PERIPHERAL_ADC1,
    K22_PERIPHERAL_DAC0,
    K22_PERIPHERAL_DAC1,
    K22_PERIPHERAL_RNG,
    K22_PERIPHERAL_LPUART0,
    K22_PERIPHERAL_SPI0,
    K22_PERIPHERAL_SPI1,
    K22_PERIPHERAL_SPI2,
    K22_PERIPHERAL_SDHC,
    K22_PERIPHERAL_I2S0,
    K22_PERIPHERAL_CRC,
    K22_PERIPHERAL_USBDCD,
    K22_PERIPHERAL_PDB0,
    K22_PERIPHERAL_PIT,
    K22_PERIPHERAL_RTC,
    K22_PERIPHERAL_RFVBAT,
    K22_PERIPHERAL_LPTMR0,
    K22_PERIPHERAL_RFSYS,
    K22_PERIPHERAL_SIM,
    K22_PERIPHERAL_PORTA,
    K22_PERIPHERAL_PORTB,
    K22_PERIPHERAL_PORTC,
    K22_PERIPHERAL_PORTD,
    K22_PERIPHERAL_PORTE,
    K22_PERIPHERAL_WDOG,
    K22_PERIPHERAL_EWM,
    K22_PERIPHERAL_CMT,
    K22_PERIPHERAL_MCG,
    K22_PERIPHERAL_OSC,
    K22_PERIPHERAL_I2C0,
    K22_PERIPHERAL_I2C1,
    K22_PERIPHERAL_I2C2,
    K22_PERIPHERAL_UART0,
    K22_PERIPHERAL_UART1,
    K22_PERIPHERAL_UART2,
    K22_PERIPHERAL_UART3,
    K22_PERIPHERAL_UART4,
    K22_PERIPHERAL_UART5,
    K22_PERIPHERAL_USB0,
    K22_PERIPHERAL_CMP0,
    K22_PERIPHERAL_CMP1,
    K22_PERIPHERAL_CMP2,
    K22_PERIPHERAL_VREF,
    K22_PERIPHERAL_LLWU,
    K22_PERIPHERAL_PMC,
    K22_PERIPHERAL_SMC,
    K22_PERIPHERAL_RCM,
    K22_PERIPHERAL_GPIOA,
    K22_PERIPHERAL_GPIOB,
    K22_PERIPHERAL_GPIOC,
    K22_PERIPHERAL_GPIOD,
    K22_PERIPHERAL_GPIOE,
    K22_PERIPHERAL_MCM,
    K22_PERIPHERAL_COUNT,
} K22PeripheralId;

typedef enum {
    K22_CPU_ARCHITECTURE_ARMV7E_M,
} K22CpuArchitecture;

typedef struct {
    K22CpuArchitecture architecture;
    uint8_t core_revision_major;
    uint8_t core_revision_minor;
    uint8_t nvic_priority_bits;
    uint16_t external_irq_count;
    bool little_endian;
    bool has_fpu;
    bool has_mpu;
    bool has_vtor;
    bool has_systick;
    uint32_t maximum_core_clock_hz;
} K22CpuOptions;

typedef struct {
    K22PeripheralId id;
    uint32_t address;
    uint32_t size;
} K22PeripheralBlock;

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
    K22CpuOptions cpu;
    const K22PeripheralBlock* peripheral_blocks;
    size_t peripheral_block_count;
} K22Profile;

typedef struct {
    K22PeripheralId id;
    uint32_t block_address;
    uint32_t block_size;
    uint32_t offset;
} K22PeripheralLocation;

const K22Profile* k22_profile_get(K22ProfileId id);
const K22Profile* k22_profile_find(const char* name);
bool k22_profile_has_peripheral(const K22Profile* profile, K22PeripheralId id);
bool k22_profile_peripheral_block(const K22Profile* profile, K22PeripheralId id,
                                  K22PeripheralBlock* block);
bool k22_profile_resolve_peripheral(const K22Profile* profile, uint32_t address,
                                    uint8_t access_size, K22PeripheralLocation* location);

#endif
