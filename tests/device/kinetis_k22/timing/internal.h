#ifndef KINETIS_K22_TIMING_TEST_INTERNAL_H
#define KINETIS_K22_TIMING_TEST_INTERNAL_H

#include "device/kinetis_k22/timing/api.h"
#include "test.h"

#include <string.h>

enum {
    SIM_SCGC3 = 0x40048030u,
    SIM_SOPT7 = 0x40048018u,
    SIM_SCGC5 = 0x40048038u,
    SIM_SCGC6 = 0x4004803cu,
    SIM_CLKDIV1 = 0x40048044u,
    SIM_SDID = 0x40048024u,
    MCG_C1 = 0x40064000u,
    MCG_C2 = 0x40064001u,
    MCG_C4 = 0x40064003u,
    MCG_C5 = 0x40064004u,
    MCG_C6 = 0x40064005u,
    MCG_S = 0x40064006u,
    LLWU_PE1 = 0x4007c000u,
    LLWU_ME = 0x4007c004u,
    LLWU_F1 = 0x4007c005u,
    LLWU_F3 = 0x4007c007u,
    LLWU_FILT1 = 0x4007c008u,
    PMC_LVDSC1 = 0x4007d000u,
    PMC_LVDSC2 = 0x4007d001u,
    PMC_REGSC = 0x4007d002u,
    SMC_PMPROT = 0x4007e000u,
    SMC_PMCTRL = 0x4007e001u,
    SMC_PMSTAT = 0x4007e003u,
    RCM_SRS0 = 0x4007f000u,
    RCM_SSRS0 = 0x4007f008u,
    PIT_MCR = 0x40037000u,
    PIT_LDVAL0 = 0x40037100u,
    PIT_CVAL0 = 0x40037104u,
    PIT_TCTRL0 = 0x40037108u,
    PIT_TFLG0 = 0x4003710cu,
    PIT_LDVAL1 = 0x40037110u,
    PIT_CVAL1 = 0x40037114u,
    PIT_TCTRL1 = 0x40037118u,
    PIT_TFLG1 = 0x4003711cu,
    PIT_LDVAL2 = 0x40037120u,
    PIT_CVAL2 = 0x40037124u,
    PIT_TCTRL2 = 0x40037128u,
    PIT_TFLG2 = 0x4003712cu,
    PIT_LDVAL3 = 0x40037130u,
    PIT_CVAL3 = 0x40037134u,
    PIT_TCTRL3 = 0x40037138u,
    PIT_TFLG3 = 0x4003713cu,
    LPTMR_CSR = 0x40040000u,
    LPTMR_PSR = 0x40040004u,
    LPTMR_CMR = 0x40040008u,
    LPTMR_CNR = 0x4004000cu,
    RTC_TSR = 0x4003d000u,
    RTC_TPR = 0x4003d004u,
    RTC_TAR = 0x4003d008u,
    RTC_TCR = 0x4003d00cu,
    RTC_CR = 0x4003d010u,
    RTC_SR = 0x4003d014u,
    RTC_LR = 0x4003d018u,
    RTC_IER = 0x4003d01cu,
    RTC_WAR = 0x4003d800u,
    RTC_RAR = 0x4003d804u,
    PDB_SC = 0x40036000u,
    PDB_MOD = 0x40036004u,
    PDB_CNT = 0x40036008u,
    PDB_IDLY = 0x4003600cu,
    FTM0_SC = 0x40038000u,
    FTM0_CNT = 0x40038004u,
    FTM0_MOD = 0x40038008u,
    FTM0_C0SC = 0x4003800cu,
    FTM0_C0V = 0x40038010u,
    FTM0_C1SC = 0x40038014u,
    FTM0_C1V = 0x40038018u,
    FTM0_CNTIN = 0x4003804cu,
    FTM0_STATUS = 0x40038050u,
    FTM0_MODE = 0x40038054u,
    FTM0_SYNC = 0x40038058u,
    FTM0_OUTINIT = 0x4003805cu,
    FTM0_OUTMASK = 0x40038060u,
    FTM0_COMBINE = 0x40038064u,
    FTM0_DEADTIME = 0x40038068u,
    FTM0_EXTTRIG = 0x4003806cu,
    FTM0_POL = 0x40038070u,
    FTM0_FMS = 0x40038074u,
    FTM0_FILTER = 0x40038078u,
    FTM0_FLTCTRL = 0x4003807cu,
    FTM0_QDCTRL = 0x40038080u,
    FTM0_CONF = 0x40038084u,
    FTM0_FLTPOL = 0x40038088u,
    FTM0_SYNCONF = 0x4003808cu,
    FTM0_INVCTRL = 0x40038090u,
    FTM0_SWOCTRL = 0x40038094u,
    FTM1_EXTTRIG = 0x4003906cu,
    WDOG_STCTRLH = 0x40052000u,
    WDOG_TOVALH = 0x40052004u,
    WDOG_TOVALL = 0x40052006u,
    WDOG_WINH = 0x40052008u,
    WDOG_WINL = 0x4005200au,
    WDOG_REFRESH = 0x4005200cu,
    WDOG_UNLOCK = 0x4005200eu,
    WDOG_PRESC = 0x40052016u,
    EWM_CTRL = 0x40061000u,
    EWM_SERV = 0x40061001u,
    EWM_CMPL = 0x40061002u,
    EWM_CMPH = 0x40061003u,
};

typedef struct {
    bool irq[128];
    uint32_t irq_assertions[128];
    uint32_t irq_changes;
    uint32_t dma_requests;
    uint32_t dma_triggers[4];
    uint8_t last_dma;
    uint32_t resets;
    uint8_t last_srs0;
    uint8_t last_srs1;
    uint32_t adc_triggers;
    uint32_t dac_triggers;
    uint32_t alternate_triggers;
    uint8_t last_trigger_instance;
    uint8_t last_trigger_channel;
} Observations;

uint64_t k22_timing_internal_clock_ticks(uint64_t* remainder, uint32_t cycles, uint32_t source_hz,
                                         uint32_t core_hz);

K22TimingSignals k22_timing_test_signals(Observations* observations);
uint32_t k22_timing_test_cycles_for_ticks(const K22Timing* timing, uint32_t ticks,
                                          uint32_t clock_hz);
void k22_timing_test_disable_watchdog_fixture(K22Timing* timing);
void k22_timing_test_expect_read(TestState* state, K22Timing* timing, uint32_t address,
                                 uint8_t size, uint32_t expected);
void k22_timing_test_expect_write(TestState* state, K22Timing* timing, uint32_t address,
                                  uint8_t size, uint32_t value);
void k22_timing_test_test_clock_tree_and_power(TestState* state, K22Timing* timing);
void k22_timing_test_test_ftm_input_capture(TestState* state, const K22Profile* profile);
void k22_timing_test_test_ftm_output(TestState* state, const K22Profile* profile);
void k22_timing_test_test_ftm(TestState* state, K22Timing* timing, Observations* observations);
void k22_timing_test_test_low_leakage_wakeup(TestState* state, K22Timing* timing,
                                             Observations* observations);
void k22_timing_test_test_low_voltage_control(TestState* state, K22Timing* timing,
                                              Observations* observations);
void k22_timing_test_test_lptmr(TestState* state, K22Timing* timing, Observations* observations);
void k22_timing_test_test_pdb(TestState* state, K22Timing* timing, Observations* observations);
void k22_timing_test_test_pit(TestState* state, K22Timing* timing, Observations* observations);
void k22_timing_test_test_profiles_and_reset(TestState* state);
void k22_timing_test_test_rtc_protection_and_compensation(TestState* state,
                                                          const K22Profile* profile);
void k22_timing_test_test_rtc(TestState* state, K22Timing* timing, Observations* observations);

#endif
