#ifndef KINETIS_K22_INTEGRATION_TEST_INTERNAL_H
#define KINETIS_K22_INTEGRATION_TEST_INTERNAL_H

#include "kinetis_k22.h"

#include <stdint.h>

#include "device/kinetis_k22/internal.h"
#include "k22_test.h"
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

bool k22_integration_test_cpu_write8(KinetisK22* device, uint32_t address, uint8_t value);
bool k22_integration_test_read16(KinetisK22* device, uint32_t address, uint16_t* value);
bool k22_integration_test_read32(KinetisK22* device, uint32_t address, uint32_t* value);
bool k22_integration_test_read8(KinetisK22* device, uint32_t address, uint8_t* value);
bool k22_integration_test_write16(KinetisK22* device, uint32_t address, uint16_t value);
bool k22_integration_test_write32(KinetisK22* device, uint32_t address, uint32_t value);
KinetisK22* k22_integration_test_create_device(TestState* state, KinetisK22Package package);
KinetisK22* k22_integration_test_create_f12_device(TestState* state, KinetisK22Package package);
void k22_integration_test_expect_can_irq_level(TestState* state);
void k22_integration_test_expect_clock_gates(TestState* state, KinetisK22* device);
void k22_integration_test_expect_fmc_cache(TestState* state);
void k22_integration_test_expect_fmc_invalidation_and_locking(TestState* state);
void k22_integration_test_expect_integrated_flash_command(TestState* state, KinetisK22* device);
void k22_integration_test_expect_integrated_flash_swap(TestState* state);
void k22_integration_test_expect_io_irq_levels(TestState* state, KinetisK22* device);
void k22_integration_test_expect_manifest_fallback(TestState* state, KinetisK22* device);
void k22_integration_test_expect_memory_domains(TestState* state);
void k22_integration_test_expect_package_selection(TestState* state);
void k22_integration_test_expect_package_serial_extensions(TestState* state);

#endif
