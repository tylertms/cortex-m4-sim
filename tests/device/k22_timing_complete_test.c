#include "k22_timing.h"
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
    FTM0_STATUS = 0x40038050u,
    FTM0_EXTTRIG = 0x4003806cu,
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

static void irq_signal(void* context, uint8_t irq, bool asserted) {
    Observations* observations = context;
    observations->irq[irq] = asserted;
    if (asserted)
        observations->irq_assertions[irq]++;
    observations->irq_changes++;
}

static void dma_signal(void* context, uint8_t source) {
    Observations* observations = context;
    observations->dma_requests++;
    observations->last_dma = source;
}

static void reset_signal(void* context, uint8_t srs0, uint8_t srs1) {
    Observations* observations = context;
    observations->resets++;
    observations->last_srs0 = srs0;
    observations->last_srs1 = srs1;
}

static void trigger_signal(void* context, K22TimingTrigger trigger, uint8_t instance,
                           uint8_t channel) {
    Observations* observations = context;
    if (trigger == K22_TIMING_TRIGGER_PDB_ADC)
        observations->adc_triggers++;
    else if (trigger == K22_TIMING_TRIGGER_PDB_DAC)
        observations->dac_triggers++;
    else
        observations->alternate_triggers++;
    observations->last_trigger_instance = instance;
    observations->last_trigger_channel = channel;
}

static K22TimingSignals signals(Observations* observations) {
    K22TimingSignals value = {observations, irq_signal, dma_signal, reset_signal,
                              trigger_signal};
    return value;
}

static void expect_read(TestState* state, K22Timing* timing, uint32_t address, uint8_t size,
                        uint32_t expected) {
    uint32_t value = 0xdeadbeefu;
    TEST_EXPECT(state, k22_timing_read(timing, address, size, &value));
    if (value != expected) {
        fprintf(stderr, "address 0x%08x: expected 0x%08x, got 0x%08x\n", address, expected,
                value);
    }
    TEST_EXPECT(state, value == expected);
}

static void expect_write(TestState* state, K22Timing* timing, uint32_t address,
                         uint8_t size, uint32_t value) {
    TEST_EXPECT(state, k22_timing_write(timing, address, size, value));
}

static uint32_t cycles_for_ticks(const K22Timing* timing, uint32_t ticks,
                                 uint32_t clock_hz) {
    return (uint32_t)(((uint64_t)timing->core_clock_hz * ticks + clock_hz - 1u) / clock_hz);
}

static void test_profiles_and_reset(TestState* state) {
    for (K22ProfileId id = 0; id < K22_PROFILE_COUNT; id++) {
        K22Timing timing;
        Observations observations = {0};
        const K22Profile* profile = k22_profile_get(id);
        TEST_EXPECT(state, profile != NULL);
        TEST_EXPECT(state, k22_timing_init(&timing, profile, 8000000u, 32768u,
                                           signals(&observations)));
        expect_read(state, &timing, SIM_SDID, 4, profile->sim_sdid_reset);
        expect_read(state, &timing, 0x4004804cu, 4,
                    id == K22_PROFILE_MK22FN1M012 || id == K22_PROFILE_MK22FX51212
                        ? 0xff0f0f00u
                        : 0x0f0f0f00u);
        TEST_EXPECT(state,
                    k22_timing_read(&timing, SIM_SCGC3, 4, &(uint32_t){0}) ==
                        (id == K22_PROFILE_MK22FN1M012 || id == K22_PROFILE_MK22FX51212));
        expect_read(state, &timing, RCM_SRS0, 1, 0x82u);
        expect_read(state, &timing, 0x40037000u, 4,
                    id == K22_PROFILE_MK22FN1M012 || id == K22_PROFILE_MK22FX51212 ? 2u
                                                                                   : 6u);
        expect_read(state, &timing, 0x4003d014u, 4, 1u);
        expect_read(state, &timing, 0x40052000u, 2, 0x01d3u);
        TEST_EXPECT(state, k22_timing_core_clock_hz(&timing) == 20971520u);
        TEST_EXPECT(state, k22_timing_bus_clock_hz(&timing) == 20971520u);
        TEST_EXPECT(state, !k22_timing_read(&timing, MCG_S + 1u, 1, &(uint32_t){0}));
        TEST_EXPECT(state, !k22_timing_read(&timing, SIM_SDID, 1, &(uint32_t){0}));
        TEST_EXPECT(state, !k22_timing_write(&timing, SIM_SDID, 4, 0));
    }
}

static void test_clock_tree_and_power(TestState* state, K22Timing* timing) {
    expect_write(state, timing, MCG_C1, 1, 0x32u);
    expect_write(state, timing, MCG_C2, 1, 0xa0u);
    expect_write(state, timing, MCG_C4, 1, 0x60u);
    expect_write(state, timing, SIM_CLKDIV1, 4, 0x03030000u);
    TEST_EXPECT(state, k22_timing_core_clock_hz(timing) == 16000000u);
    TEST_EXPECT(state, k22_timing_bus_clock_hz(timing) == 4000000u);
    expect_write(state, timing, MCG_C1, 1, 0x80u);
    TEST_EXPECT(state, k22_timing_core_clock_hz(timing) == 8000000u);
    expect_read(state, timing, MCG_S, 1, 0x08u);
    expect_write(state, timing, SIM_CLKDIV1, 4, 0x12000000u);
    TEST_EXPECT(state, k22_timing_core_clock_hz(timing) == 4000000u);
    TEST_EXPECT(state, k22_timing_bus_clock_hz(timing) == 8000000u / 3u);
    expect_write(state, timing, MCG_C5, 1, 0);
    expect_write(state, timing, MCG_C6, 1, 0x40u);
    expect_write(state, timing, MCG_C1, 1, 0);
    TEST_EXPECT(state, k22_timing_core_clock_hz(timing) == 96000000u / 2u);
    expect_read(state, timing, MCG_S, 1, 0x6cu);
    expect_write(state, timing, SMC_PMPROT, 1, 0x80u);
    expect_write(state, timing, SMC_PMCTRL, 1, 0x60u);
    expect_read(state, timing, SMC_PMSTAT, 1, 0x80u);
    expect_write(state, timing, SMC_PMCTRL, 1, 0);
    expect_read(state, timing, SMC_PMSTAT, 1, 1u);
    expect_write(state, timing, RCM_SSRS0, 1, 0x80u);
    expect_read(state, timing, RCM_SSRS0, 1, 2u);
}

static void test_pit(TestState* state, K22Timing* timing, Observations* observations) {
    const uint32_t alternate_before = observations->alternate_triggers;
    expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 | (1u << 23u));
    expect_write(state, timing, PIT_MCR, 4, 0);
    expect_write(state, timing, PIT_LDVAL0, 4, 2u);
    expect_write(state, timing, PIT_LDVAL1, 4, 1u);
    expect_write(state, timing, PIT_TCTRL0, 4, 3u);
    expect_write(state, timing, PIT_TCTRL1, 4, 7u);
    k22_timing_advance(timing, 6u);
    expect_read(state, timing, PIT_CVAL0, 4, 2u);
    expect_read(state, timing, PIT_CVAL1, 4, 1u);
    TEST_EXPECT(state, observations->irq[48]);
    TEST_EXPECT(state, observations->alternate_triggers == alternate_before + 2u);
    TEST_EXPECT(state, observations->last_trigger_instance == 5u);
    expect_write(state, timing, PIT_TFLG0, 4, 1u);
    TEST_EXPECT(state, !observations->irq[48]);
    expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 & ~(1u << 23u));
    k22_timing_advance(timing, 100u);
    expect_read(state, timing, PIT_CVAL0, 4, 2u);

    expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 | (1u << 23u));
    expect_write(state, timing, PIT_MCR, 4, 0u);
    expect_write(state, timing, PIT_TCTRL0, 4, 0u);
    expect_write(state, timing, PIT_LDVAL0, 4, 5u);
    expect_write(state, timing, PIT_TCTRL0, 4, 1u);
    k22_timing_advance(timing, 2u);
    expect_read(state, timing, PIT_CVAL0, 4, 3u);
    expect_write(state, timing, PIT_LDVAL0, 4, 9u);
    expect_read(state, timing, PIT_CVAL0, 4, 3u);
    expect_write(state, timing, PIT_TCTRL0, 4, 3u);
    expect_read(state, timing, PIT_CVAL0, 4, 3u);
    expect_write(state, timing, PIT_TCTRL0, 4, 1u);
    expect_read(state, timing, PIT_CVAL0, 4, 3u);
    k22_timing_advance(timing, 4u);
    expect_read(state, timing, PIT_CVAL0, 4, 9u);
    expect_read(state, timing, PIT_TFLG0, 4, 1u);
    expect_write(state, timing, PIT_TCTRL0, 4, 3u);
    TEST_EXPECT(state, observations->irq[48]);
    expect_write(state, timing, PIT_TCTRL0, 4, 1u);
    TEST_EXPECT(state, !observations->irq[48]);
    expect_write(state, timing, PIT_TCTRL0, 4, 7u);
    expect_read(state, timing, PIT_TCTRL0, 4, 3u);
    expect_write(state, timing, PIT_TFLG0, 4, 1u);

    expect_write(state, timing, PIT_TCTRL0, 4, 0u);
    expect_write(state, timing, PIT_TCTRL1, 4, 0u);
    expect_write(state, timing, PIT_TCTRL2, 4, 0u);
    expect_write(state, timing, PIT_TCTRL3, 4, 0u);
    expect_write(state, timing, PIT_LDVAL0, 4, 1u);
    expect_write(state, timing, PIT_LDVAL1, 4, 1u);
    expect_write(state, timing, PIT_LDVAL2, 4, 1u);
    expect_write(state, timing, PIT_LDVAL3, 4, 1u);
    expect_write(state, timing, PIT_TCTRL0, 4, 1u);
    expect_write(state, timing, PIT_TCTRL1, 4, 5u);
    expect_write(state, timing, PIT_TCTRL2, 4, 5u);
    expect_write(state, timing, PIT_TCTRL3, 4, 7u);
    k22_timing_advance(timing, 15u);
    expect_read(state, timing, PIT_CVAL0, 4, 0u);
    expect_read(state, timing, PIT_CVAL1, 4, 0u);
    expect_read(state, timing, PIT_CVAL2, 4, 0u);
    expect_read(state, timing, PIT_CVAL3, 4, 0u);
    expect_read(state, timing, PIT_TFLG0, 4, 1u);
    expect_read(state, timing, PIT_TFLG1, 4, 1u);
    expect_read(state, timing, PIT_TFLG2, 4, 1u);
    expect_read(state, timing, PIT_TFLG3, 4, 0u);
    TEST_EXPECT(state, !observations->irq[48]);
    TEST_EXPECT(state, !observations->irq[49]);
    TEST_EXPECT(state, !observations->irq[50]);
    TEST_EXPECT(state, !observations->irq[51]);
    k22_timing_advance(timing, 1u);
    expect_read(state, timing, PIT_TFLG3, 4, 1u);
    TEST_EXPECT(state, observations->irq[51]);
    expect_write(state, timing, PIT_TFLG0, 4, 1u);
    expect_write(state, timing, PIT_TFLG1, 4, 1u);
    expect_write(state, timing, PIT_TFLG2, 4, 1u);
    expect_write(state, timing, PIT_TFLG3, 4, 1u);

    expect_write(state, timing, PIT_TCTRL0, 4, 0u);
    expect_write(state, timing, PIT_LDVAL0, 4, UINT32_MAX);
    expect_write(state, timing, PIT_TCTRL0, 4, 1u);
    timing->wdog[0] = 0u;
    k22_timing_advance(timing, UINT32_MAX);
    expect_read(state, timing, PIT_CVAL0, 4, 0u);
    expect_read(state, timing, PIT_TFLG0, 4, 0u);
    expect_write(state, timing, PIT_MCR, 4, 2u);
    k22_timing_advance(timing, 1u);
    expect_read(state, timing, PIT_CVAL0, 4, 0u);
    expect_write(state, timing, PIT_MCR, 4, 0u);
    k22_timing_advance(timing, 1u);
    expect_read(state, timing, PIT_CVAL0, 4, UINT32_MAX);
    expect_read(state, timing, PIT_TFLG0, 4, 1u);
}

static void test_lptmr(TestState* state, K22Timing* timing, Observations* observations) {
    const uint32_t alternate_before = observations->alternate_triggers;
    expect_write(state, timing, SIM_SCGC5, 4, timing->sim_scgc5 | 1u);
    expect_write(state, timing, LPTMR_PSR, 4, 5u);
    expect_write(state, timing, LPTMR_CMR, 4, 2u);
    expect_write(state, timing, LPTMR_CSR, 4, 0x41u);
    k22_timing_advance(timing, cycles_for_ticks(timing, 2u, 1000u));
    expect_read(state, timing, LPTMR_CSR, 4, 0x41u);
    expect_write(state, timing, LPTMR_CNR, 4, UINT32_MAX);
    expect_read(state, timing, LPTMR_CNR, 4, 2u);
    k22_timing_advance(timing, cycles_for_ticks(timing, 1u, 1000u));
    expect_read(state, timing, LPTMR_CSR, 4, 0xc1u);
    TEST_EXPECT(state, observations->irq[58]);
    expect_write(state, timing, LPTMR_CSR, 4, 0xc1u);
    TEST_EXPECT(state, !observations->irq[58]);
    expect_write(state, timing, LPTMR_CNR, 4, 0u);
    expect_read(state, timing, LPTMR_CNR, 4, 0u);
    expect_write(state, timing, LPTMR_CMR, 4, 0u);
    expect_write(state, timing, LPTMR_CSR, 4, 0u);
    expect_write(state, timing, LPTMR_CMR, 4, 0u);
    expect_write(state, timing, LPTMR_CSR, 4, 1u);
    k22_timing_advance(timing, cycles_for_ticks(timing, 1u, 1000u));
    expect_read(state, timing, LPTMR_CSR, 4, 0x81u);

    expect_write(state, timing, LPTMR_CSR, 4, 0u);
    expect_write(state, timing, LPTMR_PSR, 4, 5u);
    expect_write(state, timing, LPTMR_CMR, 4, 5u);
    expect_write(state, timing, LPTMR_CSR, 4, 0x47u);
    expect_write(state, timing, LPTMR_PSR, 4, 0x7fu);
    expect_read(state, timing, LPTMR_PSR, 4, 5u);
    expect_write(state, timing, LPTMR_CMR, 4, 9u);
    expect_read(state, timing, LPTMR_CMR, 4, 5u);
    expect_write(state, timing, LPTMR_CSR, 4, 0x7fu);
    expect_read(state, timing, LPTMR_CSR, 4, 0x47u);

    expect_write(state, timing, LPTMR_CSR, 4, 0u);
    expect_write(state, timing, LPTMR_PSR, 4, 4u);
    expect_write(state, timing, LPTMR_CMR, 4, 1u);
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 0u, false));
    expect_write(state, timing, LPTMR_CSR, 4, 0x43u);
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 1u, true));
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 0u, false));
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 0u, true));
    expect_write(state, timing, LPTMR_CNR, 4, 0u);
    expect_read(state, timing, LPTMR_CNR, 4, 1u);
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 0u, false));
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 0u, true));
    expect_read(state, timing, LPTMR_CSR, 4, 0xc3u);
    TEST_EXPECT(state, observations->irq[58]);
    expect_write(state, timing, LPTMR_CNR, 4, 0u);
    expect_read(state, timing, LPTMR_CNR, 4, 0u);

    expect_write(state, timing, LPTMR_CSR, 4, 0u);
    expect_write(state, timing, LPTMR_PSR, 4, 4u);
    expect_write(state, timing, LPTMR_CMR, 4, 3u);
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 2u, true));
    expect_write(state, timing, LPTMR_CSR, 4, 0x2fu);
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 2u, false));
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 2u, true));
    expect_write(state, timing, LPTMR_CNR, 4, 0u);
    expect_read(state, timing, LPTMR_CNR, 4, 1u);
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 2u, false));
    expect_write(state, timing, LPTMR_CNR, 4, 0u);
    expect_read(state, timing, LPTMR_CNR, 4, 2u);

    expect_write(state, timing, LPTMR_CSR, 4, 0u);
    expect_write(state, timing, LPTMR_PSR, 4, 4u);
    expect_write(state, timing, LPTMR_CMR, 4, 1u);
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 0u, false));
    expect_write(state, timing, LPTMR_CSR, 4, 0x47u);
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 0u, false));
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 0u, true));
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 0u, false));
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 0u, true));
    expect_read(state, timing, LPTMR_CSR, 4, 0xc7u);
    TEST_EXPECT(state, observations->irq[58]);
    TEST_EXPECT(state, observations->alternate_triggers > alternate_before);
    TEST_EXPECT(state, observations->last_trigger_instance == 14u);
    expect_write(state, timing, LPTMR_CNR, 4, 0u);
    expect_read(state, timing, LPTMR_CNR, 4, 2u);
    expect_write(state, timing, LPTMR_CSR, 4, 0xc7u);
    TEST_EXPECT(state, !observations->irq[58]);

    expect_write(state, timing, LPTMR_CSR, 4, 0u);
    expect_write(state, timing, LPTMR_PSR, 4, 9u);
    expect_write(state, timing, LPTMR_CMR, 4, 4u);
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 0u, false));
    expect_write(state, timing, LPTMR_CSR, 4, 3u);
    k22_timing_advance(timing, cycles_for_ticks(timing, 2u, 1000u));
    k22_timing_advance(timing, cycles_for_ticks(timing, 1u, 1000u));
    TEST_EXPECT(state, k22_timing_set_lptmr_input(timing, 0u, true));
    k22_timing_advance(timing, cycles_for_ticks(timing, 1u, 1000u));
    expect_write(state, timing, LPTMR_CNR, 4, 0u);
    expect_read(state, timing, LPTMR_CNR, 4, 0u);
    k22_timing_advance(timing, cycles_for_ticks(timing, 1u, 1000u));
    expect_write(state, timing, LPTMR_CNR, 4, 0u);
    expect_read(state, timing, LPTMR_CNR, 4, 1u);

    k22_timing_warm_reset(timing, 0x20u, 0u);
    expect_write(state, timing, LPTMR_CNR, 4, 0u);
    expect_read(state, timing, LPTMR_CNR, 4, 1u);
    k22_timing_warm_reset(timing, 0x04u, 0u);
    expect_read(state, timing, LPTMR_CSR, 4, 0u);
    TEST_EXPECT(state, !k22_timing_set_lptmr_input(NULL, 0u, false));
    TEST_EXPECT(state, !k22_timing_set_lptmr_input(timing, 3u, false));
}

static void test_rtc(TestState* state, K22Timing* timing, Observations* observations) {
    const uint32_t alternate_before = observations->alternate_triggers;
    expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 | (1u << 29u));
    expect_read(state, timing, RTC_TSR, 4, 0u);
    expect_read(state, timing, RTC_TPR, 4, 0u);
    expect_write(state, timing, RTC_TPR, 4, 7u);
    expect_write(state, timing, RTC_TSR, 4, 10u);
    expect_read(state, timing, RTC_TPR, 4, 7u);
    expect_write(state, timing, RTC_TPR, 4, 0u);
    expect_write(state, timing, RTC_TAR, 4, 11u);
    expect_write(state, timing, RTC_IER, 4, 0x14u);
    expect_write(state, timing, RTC_CR, 4, 0x100u);
    expect_write(state, timing, RTC_SR, 4, 0x10u);
    expect_write(state, timing, RTC_TSR, 4, 20u);
    expect_write(state, timing, RTC_TPR, 4, 20u);
    k22_timing_advance(timing, timing->core_clock_hz);
    expect_read(state, timing, RTC_TSR, 4, 11u);
    expect_read(state, timing, RTC_TPR, 4, 0u);
    TEST_EXPECT(state, observations->irq[46]);
    TEST_EXPECT(state, !observations->irq[47]);
    TEST_EXPECT(state, observations->irq_assertions[47] == 1u);
    expect_read(state, timing, RTC_SR, 4, 0x14u);
    TEST_EXPECT(state, observations->alternate_triggers == alternate_before + 2u);
    TEST_EXPECT(state, observations->last_trigger_instance == 12u);
    expect_write(state, timing, RTC_TAR, 4, 15u);
    expect_read(state, timing, RTC_SR, 4, 0x10u);
    TEST_EXPECT(state, !observations->irq[46]);
    const uint32_t retained_tsr = timing->rtc_tsr;
    const uint16_t retained_tpr = timing->rtc_tpr;
    expect_write(state, timing, RTC_RAR, 4, 0xfbu);
    expect_read(state, timing, RTC_TAR, 4, 0u);
    expect_write(state, timing, RTC_RAR, 4, 0xffu);
    expect_read(state, timing, RTC_TAR, 4, 0u);
    expect_write(state, timing, RTC_WAR, 4, 0xfbu);
    expect_write(state, timing, RTC_TAR, 4, 22u);
    k22_timing_warm_reset(timing, 0x20u, 0);
    TEST_EXPECT(state, timing->rtc_tsr == retained_tsr);
    TEST_EXPECT(state, timing->rtc_tpr == retained_tpr);
    expect_read(state, timing, RTC_WAR, 4, 0xffu);
    expect_read(state, timing, RTC_RAR, 4, 0xffu);
    expect_read(state, timing, RTC_TAR, 4, 15u);
    expect_read(state, timing, RCM_SRS0, 1, 0x20u);
}

static void test_rtc_protection_and_compensation(TestState* state,
                                                 const K22Profile* profile) {
    Observations observations = {0};
    K22Timing timing;
    TEST_EXPECT(
        state, k22_timing_init(&timing, profile, 8000000u, 32768u, signals(&observations)));
    expect_write(state, &timing, RTC_TSR, 4, 1u);
    expect_write(state, &timing, RTC_SR, 4, 0x10u);
    k22_timing_advance(&timing, timing.core_clock_hz);
    expect_read(state, &timing, RTC_TSR, 4, 1u);
    expect_write(state, &timing, RTC_SR, 4, 0u);
    expect_write(state, &timing, RTC_CR, 4, 0x100u);
    expect_write(state, &timing, RTC_TCR, 4, 0x017fu);
    expect_write(state, &timing, RTC_SR, 4, 0x10u);
    k22_timing_advance(&timing, timing.core_clock_hz);
    expect_read(state, &timing, RTC_TSR, 4, 2u);
    expect_read(state, &timing, RTC_TCR, 4, 0x017f017fu);
    k22_timing_advance(&timing,
                       cycles_for_ticks(&timing, 32640u, timing.rtc_oscillator_hz));
    expect_read(state, &timing, RTC_TSR, 4, 2u);
    k22_timing_advance(&timing, cycles_for_ticks(&timing, 1u, timing.rtc_oscillator_hz));
    expect_read(state, &timing, RTC_TSR, 4, 3u);
    expect_read(state, &timing, RTC_TCR, 4, 0x0000017fu);

    expect_write(state, &timing, RTC_SR, 4, 0u);
    expect_write(state, &timing, RTC_TCR, 4, 0x0080u);
    expect_write(state, &timing, RTC_TPR, 4, 0u);
    expect_write(state, &timing, RTC_SR, 4, 0x10u);
    k22_timing_advance(&timing, timing.core_clock_hz);
    expect_read(state, &timing, RTC_TCR, 4, 0x00800080u);
    const uint32_t before = timing.rtc_tsr;
    k22_timing_advance(&timing, timing.core_clock_hz);
    expect_read(state, &timing, RTC_TSR, 4, before);
    k22_timing_advance(&timing, cycles_for_ticks(&timing, 128u, timing.rtc_oscillator_hz));
    expect_read(state, &timing, RTC_TSR, 4, before + 1u);

    expect_write(state, &timing, RTC_SR, 4, 0u);
    expect_write(state, &timing, RTC_TSR, 4, UINT32_MAX);
    expect_write(state, &timing, RTC_IER, 4, 2u);
    expect_write(state, &timing, RTC_CR, 4, 0x100u);
    expect_write(state, &timing, RTC_SR, 4, 0x10u);
    k22_timing_advance(&timing,
                       timing.core_clock_hz +
                           cycles_for_ticks(&timing, 128u, timing.rtc_oscillator_hz));
    expect_read(state, &timing, RTC_SR, 4, 0x16u);
    expect_read(state, &timing, RTC_TSR, 4, 0u);
    expect_read(state, &timing, RTC_TPR, 4, 0u);
    TEST_EXPECT(state, observations.irq[46]);
    expect_write(state, &timing, RTC_SR, 4, 0u);
    expect_write(state, &timing, RTC_TSR, 4, 9u);
    expect_write(state, &timing, RTC_TAR, 4, 10u);
    expect_read(state, &timing, RTC_SR, 4, 0u);
    TEST_EXPECT(state, !observations.irq[46]);

    expect_write(state, &timing, RTC_CR, 4, 0x108u);
    expect_write(state, &timing, RTC_LR, 4, 0xdfu);
    expect_write(state, &timing, RTC_SR, 4, 0x10u);
    expect_read(state, &timing, RTC_SR, 4, 0x10u);
    expect_write(state, &timing, RTC_SR, 4, 0u);
    expect_read(state, &timing, RTC_SR, 4, 0x10u);
    expect_write(state, &timing, RTC_CR, 4, 1u);
    expect_read(state, &timing, RTC_SR, 4, 1u);

    expect_write(state, &timing, RTC_TCR, 4, 0x1234u);
    expect_write(state, &timing, RTC_LR, 4, 0xf7u);
    expect_write(state, &timing, RTC_TCR, 4, 0x5678u);
    expect_read(state, &timing, RTC_TCR, 4, 0x00001234u);
    expect_write(state, &timing, RTC_CR, 4, 1u);
    expect_read(state, &timing, RTC_CR, 4, 1u);
    expect_read(state, &timing, RTC_SR, 4, 1u);
    expect_read(state, &timing, RTC_LR, 4, 0xffu);
    expect_write(state, &timing, RTC_CR, 4, 0u);
    expect_read(state, &timing, RTC_CR, 4, 0u);

    expect_write(state, &timing, RTC_WAR, 4, 0x7fu);
    expect_write(state, &timing, RTC_IER, 4, 0x17u);
    expect_read(state, &timing, RTC_IER, 4, 7u);
    expect_write(state, &timing, RTC_RAR, 4, 0x7fu);
    expect_read(state, &timing, RTC_IER, 4, 0u);
    expect_read(state, &timing, RTC_WAR, 4, 0x7fu);
    expect_read(state, &timing, RTC_RAR, 4, 0x7fu);
}

static void test_pdb(TestState* state, K22Timing* timing, Observations* observations) {
    expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 | (1u << 22u));
    expect_write(state, timing, PDB_MOD, 4, 4u);
    expect_write(state, timing, PDB_IDLY, 4, 2u);
    expect_write(state, timing, PDB_SC + 0x10u, 4, 3u);
    expect_write(state, timing, PDB_SC + 0x18u, 4, 1u);
    expect_write(state, timing, PDB_SC + 0x1cu, 4, 2u);
    expect_write(state, timing, PDB_SC + 0x150u, 4, 2u);
    expect_write(state, timing, PDB_SC + 0x154u, 4, 1u);
    expect_write(state, timing, PDB_SC, 4, 0x23u);
    k22_timing_advance(timing, 2u);
    expect_read(state, timing, PDB_CNT, 4, 2u);
    expect_read(state, timing, PDB_SC, 4, 0x63u);
    expect_read(state, timing, PDB_SC + 0x14u, 4, 3u);
    TEST_EXPECT(state, observations->adc_triggers == 2u);
    TEST_EXPECT(state, observations->dac_triggers == 1u);
    TEST_EXPECT(state, observations->last_trigger_instance == 0u);
    TEST_EXPECT(state, observations->last_trigger_channel == 0u);
    expect_write(state, timing, PDB_SC + 0x14u, 4, 3u);
    expect_read(state, timing, PDB_SC + 0x14u, 4, 0u);
    TEST_EXPECT(state, observations->irq[52]);
    expect_write(state, timing, PDB_SC, 4, 0x23u);
    TEST_EXPECT(state, !observations->irq[52]);
}

static void test_ftm(TestState* state, K22Timing* timing, Observations* observations) {
    expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 | (1u << 24u));
    expect_write(state, timing, FTM0_MOD, 4, 3u);
    expect_write(state, timing, FTM0_C0V, 4, 2u);
    expect_write(state, timing, FTM0_C0SC, 4, 0x51u);
    expect_write(state, timing, FTM0_EXTTRIG, 4, 0x10u);
    expect_write(state, timing, FTM0_SC, 4, 0x48u);
    k22_timing_advance(timing, 4u);
    expect_read(state, timing, FTM0_CNT, 4, 0u);
    expect_read(state, timing, FTM0_SC, 4, 0xc8u);
    TEST_EXPECT(state, observations->irq[42]);
    TEST_EXPECT(state, observations->dma_requests != 0);
    TEST_EXPECT(state, observations->last_dma == 20u);
    TEST_EXPECT(state, observations->last_trigger_instance == 8u);
    expect_read(state, timing, FTM0_EXTTRIG, 4, 0x90u);
    expect_write(state, timing, FTM0_EXTTRIG, 4, 0x90u);
    expect_write(state, timing, FTM0_EXTTRIG, 4, 0x10u);
    expect_read(state, timing, FTM0_EXTTRIG, 4, 0x90u);
    expect_write(state, timing, FTM0_EXTTRIG, 4, 0x10u);
    expect_read(state, timing, FTM0_EXTTRIG, 4, 0x10u);
    const uint32_t alternate_before = observations->alternate_triggers;
    expect_write(state, timing, FTM0_EXTTRIG, 4, 0x40u);
    k22_timing_advance(timing, 4u);
    TEST_EXPECT(state, observations->alternate_triggers == alternate_before + 1u);
    expect_read(state, timing, FTM0_EXTTRIG, 4, 0xc0u);
    expect_write(state, timing, FTM0_EXTTRIG, 4, 0x40u);
    expect_read(state, timing, FTM0_SC, 4, 0xc8u);
    expect_read(state, timing, FTM0_C0SC, 4, 0xd1u);
    expect_write(state, timing, FTM0_C0SC, 4, 0x51u);
    expect_write(state, timing, FTM0_SC, 4, 0x48u);
    TEST_EXPECT(state, !observations->irq[42]);

    expect_write(state, timing, FTM0_C0V, 4, 1u);
    expect_write(state, timing, FTM0_C1V, 4, 2u);
    expect_write(state, timing, FTM0_C0SC, 4, 0x50u);
    expect_write(state, timing, FTM0_C1SC, 4, 0x50u);
    k22_timing_advance(timing, 4u);
    expect_read(state, timing, FTM0_STATUS, 4, 3u);
    TEST_EXPECT(state, observations->irq[42]);
    expect_read(state, timing, FTM0_C0SC, 4, 0xd0u);
    expect_write(state, timing, FTM0_C0SC, 4, 0x50u);
    TEST_EXPECT(state, observations->irq[42]);
    expect_read(state, timing, FTM0_STATUS, 4, 2u);
    expect_write(state, timing, FTM0_STATUS, 4, 0u);
    TEST_EXPECT(state, observations->irq[42]);
    expect_write(state, timing, FTM0_SC, 4, 0x48u);
    TEST_EXPECT(state, observations->irq[42]);
    expect_read(state, timing, FTM0_SC, 4, 0xc8u);
    expect_write(state, timing, FTM0_SC, 4, 0x48u);
    TEST_EXPECT(state, !observations->irq[42]);

    expect_write(state, timing, FTM0_SC, 4, 0u);
    expect_write(state, timing, FTM0_MOD, 4, 100u);
    expect_write(state, timing, FTM0_CNT, 4, 0u);
    expect_write(state, timing, FTM0_C0V, 4, 1u);
    expect_write(state, timing, FTM0_C0SC, 4, 0x50u);
    expect_write(state, timing, FTM0_SC, 4, 0x08u);
    k22_timing_advance(timing, 1u);
    TEST_EXPECT(state, observations->irq[42]);
    expect_write(state, timing, FTM0_C0SC, 4, 0x50u);
    TEST_EXPECT(state, observations->irq[42]);
    expect_read(state, timing, FTM0_C0SC, 4, 0xd0u);
    expect_write(state, timing, FTM0_C0SC, 4, 0x50u);
    TEST_EXPECT(state, !observations->irq[42]);

    expect_write(state, timing, FTM0_SC, 4, 0u);
    expect_write(state, timing, FTM0_SC + 0x4cu, 4, 1u);
    expect_write(state, timing, FTM0_MOD, 4, 4u);
    expect_write(state, timing, FTM0_CNT, 4, 0xaaaau);
    expect_read(state, timing, FTM0_CNT, 4, 1u);
    expect_write(state, timing, FTM0_C1SC, 4, 0u);
    expect_write(state, timing, FTM0_C0V, 4, 2u);
    expect_write(state, timing, FTM0_C0SC, 4, 0x68u);
    expect_write(state, timing, FTM0_SC, 4, 0x68u);
    k22_timing_advance(timing, 1u);
    expect_read(state, timing, FTM0_CNT, 4, 2u);
    expect_read(state, timing, FTM0_STATUS, 4, 1u);
    expect_read(state, timing, FTM0_C0SC, 4, 0xe8u);
    expect_write(state, timing, FTM0_C0SC, 4, 0x68u);
    k22_timing_advance(timing, 2u);
    expect_read(state, timing, FTM0_CNT, 4, 4u);
    expect_read(state, timing, FTM0_SC, 4, 0x68u);
    k22_timing_advance(timing, 1u);
    expect_read(state, timing, FTM0_CNT, 4, 3u);
    expect_read(state, timing, FTM0_SC, 4, 0xe8u);
    expect_write(state, timing, FTM0_SC, 4, 0x68u);
    k22_timing_advance(timing, 1u);
    expect_read(state, timing, FTM0_CNT, 4, 2u);
    expect_read(state, timing, FTM0_C0SC, 4, 0xe8u);
    expect_write(state, timing, FTM0_C0SC, 4, 0x68u);
    k22_timing_advance(timing, 1u);
    expect_read(state, timing, FTM0_CNT, 4, 1u);
    TEST_EXPECT(state, !observations->irq[42]);
    k22_timing_set_debug_halted(timing, true);
    k22_timing_advance(timing, 3u);
    expect_read(state, timing, FTM0_CNT, 4, 1u);
    k22_timing_set_debug_halted(timing, false);
    const uint32_t center_dma_before = observations->dma_requests;
    const uint32_t center_trigger_before = observations->alternate_triggers;
    expect_write(state, timing, FTM0_EXTTRIG, 4, 0x10u);
    expect_write(state, timing, FTM0_C0SC, 4, 0x69u);
    k22_timing_advance(timing, 13u);
    expect_read(state, timing, FTM0_CNT, 4, 2u);
    TEST_EXPECT(state, observations->dma_requests == center_dma_before + 1u);
    TEST_EXPECT(state, observations->alternate_triggers == center_trigger_before + 1u);
    expect_read(state, timing, FTM0_C0SC, 4, 0xe9u);
    expect_write(state, timing, FTM0_C0SC, 4, 0u);
    expect_read(state, timing, FTM0_SC, 4, 0xe8u);
    expect_write(state, timing, FTM0_SC, 4, 0u);
    expect_write(state, timing, FTM0_EXTTRIG, 4, 0x40u);
    expect_write(state, timing, FTM0_SC + 0x4cu, 4, 3u);
    expect_write(state, timing, FTM0_MOD, 4, 3u);
    expect_write(state, timing, FTM0_CNT, 4, 0u);
    expect_write(state, timing, FTM0_SC, 4, 0x68u);
    const uint32_t redundant_trigger_before = observations->alternate_triggers;
    k22_timing_advance(timing, 1u);
    TEST_EXPECT(state, observations->alternate_triggers == redundant_trigger_before + 1u);
    expect_read(state, timing, FTM0_CNT, 4, 3u);
    expect_read(state, timing, FTM0_SC, 4, 0xe8u);
    expect_write(state, timing, FTM1_EXTTRIG, 4, UINT32_MAX);
    expect_read(state, timing, FTM1_EXTTRIG, 4, 0x70u);
}

static void unlock_watchdog(TestState* state, K22Timing* timing) {
    expect_write(state, timing, WDOG_UNLOCK, 2, 0xc520u);
    expect_write(state, timing, WDOG_UNLOCK, 2, 0xd928u);
}

static void test_watchdogs(TestState* state, K22Timing* timing,
                           Observations* observations) {
    unlock_watchdog(state, timing);
    expect_write(state, timing, WDOG_TOVALH, 2, 0);
    expect_write(state, timing, WDOG_TOVALL, 2, 3u);
    expect_write(state, timing, WDOG_PRESC, 2, 0);
    expect_write(state, timing, WDOG_STCTRLH, 2, 1u);
    k22_timing_advance(timing, cycles_for_ticks(timing, 1u, 1000u));
    expect_write(state, timing, WDOG_REFRESH, 2, 0xa602u);
    expect_write(state, timing, WDOG_REFRESH, 2, 0xb480u);
    k22_timing_advance(timing, cycles_for_ticks(timing, 2u, 1000u));
    TEST_EXPECT(state, observations->resets == 0);
    k22_timing_advance(timing, cycles_for_ticks(timing, 1u, 1000u));
    TEST_EXPECT(state, observations->resets == 1u);
    TEST_EXPECT(state, observations->last_srs0 == 0x20u);
    expect_read(state, timing, RCM_SRS0, 1, 0x20u);
    expect_write(state, timing, EWM_CMPL, 1, 1u);
    expect_write(state, timing, EWM_CMPH, 1, 3u);
    expect_write(state, timing, EWM_CTRL, 1, 1u);
    k22_timing_advance(timing, cycles_for_ticks(timing, 2u, 1000u));
    expect_write(state, timing, EWM_SERV, 1, 0xb4u);
    expect_write(state, timing, EWM_SERV, 1, 0x2cu);
    k22_timing_advance(timing, cycles_for_ticks(timing, 4u, 1000u));
    TEST_EXPECT(state, observations->resets == 2u);
    TEST_EXPECT(state, observations->last_srs1 == 2u);
}

static void test_copy_and_split_advance(TestState* state, const K22Profile* profile) {
    Observations first_observations = {0};
    Observations second_observations = {0};
    K22Timing first;
    K22Timing second;
    TEST_EXPECT(state, k22_timing_init(&first, profile, 8000000u, 32768u,
                                       signals(&first_observations)));
    expect_write(state, &first, SIM_SCGC6, 4, first.sim_scgc6 | (1u << 23u));
    expect_write(state, &first, PIT_MCR, 4, 0);
    expect_write(state, &first, PIT_LDVAL0, 4, 6u);
    expect_write(state, &first, PIT_TCTRL0, 4, 3u);
    TEST_EXPECT(state, k22_timing_copy(&second, &first, signals(&second_observations)));
    k22_timing_advance(&first, 1000003u);
    for (uint32_t count = 0; count < 1000u; count++)
        k22_timing_advance(&second, 1000u);
    k22_timing_advance(&second, 3u);
    TEST_EXPECT(state, first.pit[0].current == second.pit[0].current);
    TEST_EXPECT(state, first.pit[0].flag == second.pit[0].flag);
    TEST_EXPECT(state, first.pit_remainder == second.pit_remainder);
    TEST_EXPECT(state, first.elapsed_core_cycles == second.elapsed_core_cycles);

    k22_timing_reset(&first, 0x82u, 0u);
    expect_write(state, &first, SIM_SCGC5, 4, first.sim_scgc5 | 1u);
    expect_write(state, &first, LPTMR_PSR, 4, 9u);
    expect_write(state, &first, LPTMR_CMR, 4, 4u);
    TEST_EXPECT(state, k22_timing_set_lptmr_input(&first, 0u, false));
    expect_write(state, &first, LPTMR_CSR, 4, 3u);
    k22_timing_advance(&first, cycles_for_ticks(&first, 1u, 1000u));
    TEST_EXPECT(state, k22_timing_copy(&second, &first, signals(&second_observations)));
    k22_timing_advance(&first, cycles_for_ticks(&first, 1u, 1000u));
    k22_timing_advance(&second, cycles_for_ticks(&second, 1u, 1000u));
    TEST_EXPECT(state, k22_timing_set_lptmr_input(&first, 0u, true));
    TEST_EXPECT(state, k22_timing_set_lptmr_input(&second, 0u, true));
    k22_timing_advance(&first, cycles_for_ticks(&first, 2u, 1000u));
    k22_timing_advance(&second, cycles_for_ticks(&second, 2u, 1000u));
    TEST_EXPECT(state, first.lptmr_counter == 1u);
    TEST_EXPECT(state, first.lptmr_counter == second.lptmr_counter);
    TEST_EXPECT(state, first.lptmr_filter_ticks == second.lptmr_filter_ticks);
}

static void test_sim_surface(TestState* state, K22Timing* timing) {
    const uint32_t writable[] = {
        0x40047000u, 0x40047004u, 0x40048004u, 0x4004800cu, 0x40048010u,
        0x40048018u, 0x4004801cu, 0x40048030u, 0x40048034u, 0x40048038u,
        0x4004803cu, 0x40048040u, 0x40048044u, 0x40048048u,
    };
    for (size_t index = 0; index < sizeof(writable) / sizeof(writable[0]); index++) {
        expect_write(state, timing, writable[index], 4, 0x5a5a0000u + (uint32_t)index);
        TEST_EXPECT(state, k22_timing_read(timing, writable[index], 4, &(uint32_t){0}));
    }
    expect_read(state, timing, SIM_SOPT7, 4, 5u);
    expect_read(state, timing, 0x4004804cu, 4, 0xff0f0f00u);
    expect_read(state, timing, 0x40048050u, 4, 0x7f7f0000u);
    TEST_EXPECT(state, !k22_timing_read(timing, 0x40048028u, 4, &(uint32_t){0}));
    TEST_EXPECT(state, !k22_timing_write(timing, 0x40048028u, 4, 0));
}

static void test_control_surface(TestState* state, K22Timing* timing,
                                 Observations* observations) {
    const uint8_t mcg_offsets[] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 11, 12, 13};
    for (size_t index = 0; index < sizeof(mcg_offsets); index++) {
        const uint32_t address = MCG_C1 + mcg_offsets[index];
        TEST_EXPECT(state, k22_timing_read(timing, address, 1, &(uint32_t){0}));
        expect_write(state, timing, address, 1, (uint32_t)index);
    }
    expect_write(state, timing, MCG_C1 + 3u, 1, 0xe0u);
    expect_write(state, timing, MCG_C1 + 1u, 1, 0);
    expect_write(state, timing, MCG_C1, 1, 0x40u);
    TEST_EXPECT(state, k22_timing_core_clock_hz(timing) == 32768u);
    expect_write(state, timing, MCG_C1 + 1u, 1, 1u);
    TEST_EXPECT(state, k22_timing_core_clock_hz(timing) == 4000000u);
    expect_write(state, timing, 0x40065000u, 1, 0x80u);
    expect_write(state, timing, 0x40065002u, 1, 3u);
    expect_read(state, timing, 0x40065000u, 1, 0x80u);
    expect_read(state, timing, 0x40065002u, 1, 3u);
    for (uint32_t offset = 0; offset < 10u; offset++) {
        if (offset != 7u)
            expect_write(state, timing, 0x4007c000u + offset, 1, 0x55u);
        TEST_EXPECT(state,
                    k22_timing_read(timing, 0x4007c000u + offset, 1, &(uint32_t){0}));
    }
    timing->llwu[5] = 1u;
    expect_write(state, timing, 0x4007c005u, 1, 1u);
    TEST_EXPECT(state, !observations->irq[21]);
    for (uint32_t offset = 0; offset < 3u; offset++) {
        expect_write(state, timing, 0x4007d000u + offset, 1, 0xa5u);
        TEST_EXPECT(state,
                    k22_timing_read(timing, 0x4007d000u + offset, 1, &(uint32_t){0}));
    }
    expect_write(state, timing, SMC_PMPROT, 1, 0x20u);
    expect_write(state, timing, SMC_PMCTRL, 1, 0x40u);
    expect_read(state, timing, SMC_PMSTAT, 1, 4u);
    expect_write(state, timing, 0x4007e002u, 1, 0xa5u);
    expect_read(state, timing, 0x4007e002u, 1, 0xa5u);
    expect_write(state, timing, 0x4007f004u, 1, 0x5au);
    expect_write(state, timing, 0x4007f005u, 1, 0xa5u);
    expect_read(state, timing, 0x4007f004u, 1, 0x5au);
    expect_read(state, timing, 0x4007f005u, 1, 0xa5u);
    TEST_EXPECT(state, !k22_timing_write(timing, RCM_SRS0, 1, 0));
}

static void test_timer_register_surface(TestState* state, K22Timing* timing) {
    const uint32_t pit_registers[] = {PIT_LDVAL0, PIT_CVAL0, PIT_TCTRL0, PIT_TFLG0};
    for (size_t index = 0; index < sizeof(pit_registers) / sizeof(pit_registers[0]);
         index++)
        TEST_EXPECT(state,
                    k22_timing_read(timing, pit_registers[index], 4, &(uint32_t){0}));
    expect_read(state, timing, LPTMR_PSR, 4, timing->lptmr_psr);
    expect_read(state, timing, LPTMR_CMR, 4, timing->lptmr_cmr);
    expect_write(state, timing, LPTMR_CSR, 4, 0);
    const uint32_t rtc_registers[] = {
        RTC_TAR,     0x4003d00cu, 0x4003d010u, RTC_SR,
        0x4003d018u, RTC_IER,     0x4003d800u, 0x4003d804u,
    };
    for (size_t index = 0; index < sizeof(rtc_registers) / sizeof(rtc_registers[0]);
         index++) {
        TEST_EXPECT(state,
                    k22_timing_read(timing, rtc_registers[index], 4, &(uint32_t){0}));
    }
    expect_write(state, timing, RTC_TSR, 4, 0u);
    expect_write(state, timing, RTC_TPR, 4, 123u);
    expect_read(state, timing, RTC_TPR, 4, 123u);
    const uint32_t pdb_offsets[] = {0x10u,  0x14u,  0x18u,  0x1cu,  0x38u,
                                    0x3cu,  0x40u,  0x44u,  0x150u, 0x154u,
                                    0x158u, 0x15cu, 0x190u, 0x194u, 0x198u};
    for (size_t index = 0; index < sizeof(pdb_offsets) / sizeof(pdb_offsets[0]); index++) {
        const uint32_t address = PDB_SC + pdb_offsets[index];
        expect_write(state, timing, address, 4, (uint32_t)index);
        expect_read(state, timing, address, 4,
                    pdb_offsets[index] == 0x14u || pdb_offsets[index] == 0x3cu
                        ? 0u
                        : (uint32_t)index);
    }
    expect_read(state, timing, PDB_MOD, 4, timing->pdb_mod);
    expect_read(state, timing, PDB_IDLY, 4, timing->pdb_idly);
    expect_write(state, timing, PDB_CNT, 4, 123u);
    TEST_EXPECT(state, !k22_timing_read(timing, PDB_SC + 0x48u, 4, &(uint32_t){0}));
}

static void test_ftm_surface(TestState* state, K22Timing* timing) {
    const uint32_t offsets[] = {4u, 8u, 0x0cu, 0x10u, 0x4cu, 0x50u, 0x54u, 0x98u};
    for (size_t index = 0; index < sizeof(offsets) / sizeof(offsets[0]); index++) {
        const uint32_t address = FTM0_SC + offsets[index];
        expect_write(state, timing, address, 4, 0x1234u + (uint32_t)index);
        TEST_EXPECT(state, k22_timing_read(timing, address, 4, &(uint32_t){0}));
    }
    expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 | (1u << 24u));
    expect_write(state, timing, FTM0_MOD, 4, 7u);
    expect_write(state, timing, FTM0_SC, 4, 0x10u);
    k22_timing_advance(timing, timing->core_clock_hz / 32768u + 1u);
    expect_write(state, timing, FTM0_SC, 4, 0x18u);
    k22_timing_advance(timing, timing->core_clock_hz / 8000000u + 1u);
}

static void test_watchdog_surface(TestState* state, K22Timing* timing,
                                  Observations* observations) {
    unlock_watchdog(state, timing);
    for (uint32_t offset = 0; offset < 0x18u; offset += 2u) {
        if (offset != 0x0cu && offset != 0x0eu && offset != 0x10u && offset != 0x12u)
            expect_write(state, timing, WDOG_STCTRLH + offset, 2, offset);
        TEST_EXPECT(state,
                    k22_timing_read(timing, WDOG_STCTRLH + offset, 2, &(uint32_t){0}));
    }
    expect_write(state, timing, WDOG_UNLOCK, 2, 0);
    expect_write(state, timing, WDOG_REFRESH, 2, 0);
    expect_write(state, timing, EWM_CMPL, 1, 1u);
    expect_write(state, timing, EWM_CMPH, 1, 4u);
    expect_write(state, timing, EWM_CTRL + 5u, 1, 2u);
    for (uint32_t offset = 0; offset < 6u; offset++) {
        if (offset != 4u)
            TEST_EXPECT(state,
                        k22_timing_read(timing, EWM_CTRL + offset, 1, &(uint32_t){0}));
    }
    expect_write(state, timing, EWM_SERV, 1, 0);
    TEST_EXPECT(state, observations->resets != 0);
}

static void test_register_surface(TestState* state, const K22Profile* profile) {
    K22Timing timing;
    Observations observations = {0};
    TEST_EXPECT(
        state, k22_timing_init(&timing, profile, 8000000u, 32768u, signals(&observations)));
    test_sim_surface(state, &timing);
    k22_timing_reset(&timing, 0x82u, 0);
    test_control_surface(state, &timing, &observations);
    k22_timing_reset(&timing, 0x82u, 0);
    test_timer_register_surface(state, &timing);
    k22_timing_reset(&timing, 0x82u, 0);
    test_ftm_surface(state, &timing);
    k22_timing_reset(&timing, 0x82u, 0);
    test_watchdog_surface(state, &timing, &observations);
}

static void test_edge_paths(TestState* state, const K22Profile* profile) {
    K22Timing timing;
    Observations observations = {0};
    TEST_EXPECT(
        state, k22_timing_init(&timing, profile, 8000000u, 32768u, signals(&observations)));
    timing.external_oscillator_hz = 0u;
    expect_write(state, &timing, MCG_C1, 1, 0x80u);
    TEST_EXPECT(state, k22_timing_core_clock_hz(&timing) == timing.slow_irc_hz);
    timing.external_oscillator_hz = 8000000u;
    expect_write(state, &timing, MCG_C1 + 1u, 1, 4u);
    expect_write(state, &timing, MCG_C1, 1, 0x80u);
    expect_read(state, &timing, MCG_S, 1, 0x0au);
    expect_write(state, &timing, MCG_C1 + 3u, 1, 0x80u);
    expect_write(state, &timing, MCG_C1, 1, 4u);
    expect_write(state, &timing, SIM_SCGC5, 4, timing.sim_scgc5 | 1u);
    for (uint32_t source = 0; source < 4u; source++) {
        expect_write(state, &timing, LPTMR_CSR, 4, 0);
        expect_write(state, &timing, LPTMR_PSR, 4, source | (source == 0 ? 0u : 4u));
        expect_write(state, &timing, LPTMR_CMR, 4, 0xffffu);
        expect_write(state, &timing, LPTMR_CSR, 4, 1u);
        k22_timing_advance(&timing, timing.core_clock_hz);
    }
    expect_write(state, &timing, LPTMR_CSR, 4, 0u);
    expect_write(state, &timing, SIM_SCGC6, 4,
                 timing.sim_scgc6 | (1u << 29u) | (1u << 22u));
    expect_write(state, &timing, RTC_TSR, 4, UINT32_MAX);
    expect_write(state, &timing, RTC_IER, 4, 2u);
    expect_write(state, &timing, RTC_CR, 4, 0x100u);
    expect_write(state, &timing, RTC_SR, 4, 0x10u);
    const uint32_t alternate_before = observations.alternate_triggers;
    k22_timing_advance(&timing, timing.core_clock_hz);
    TEST_EXPECT(state, observations.irq[46]);
    TEST_EXPECT(state, observations.alternate_triggers == alternate_before + 2u);
    TEST_EXPECT(state, observations.last_trigger_instance == 12u);
    expect_write(state, &timing, PDB_MOD, 4, 1u);
    expect_write(state, &timing, PDB_SC, 4, 1u);
    k22_timing_advance(&timing, 2u);
    expect_read(state, &timing, PDB_SC, 4, 0u);
    k22_timing_reset(&timing, 0x82u, 0);
    memset(&observations, 0, sizeof(observations));
    unlock_watchdog(state, &timing);
    expect_write(state, &timing, WDOG_TOVALH, 2, 0u);
    expect_write(state, &timing, WDOG_TOVALL, 2, 10u);
    expect_write(state, &timing, WDOG_WINH, 2, 0u);
    expect_write(state, &timing, WDOG_WINL, 2, 2u);
    expect_write(state, &timing, WDOG_STCTRLH, 2, 9u);
    expect_write(state, &timing, WDOG_REFRESH, 2, 0xa602u);
    expect_write(state, &timing, WDOG_REFRESH, 2, 0xb480u);
    TEST_EXPECT(state, observations.resets == 1u);
    k22_timing_reset(&timing, 0x82u, 0);
    unlock_watchdog(state, &timing);
    expect_write(state, &timing, WDOG_TOVALH, 2, 0);
    expect_write(state, &timing, WDOG_TOVALL, 2, 1u);
    expect_write(state, &timing, WDOG_PRESC, 2, 0);
    expect_write(state, &timing, WDOG_STCTRLH, 2, 5u);
    k22_timing_advance(&timing, cycles_for_ticks(&timing, 1u, 1000u));
    TEST_EXPECT(state, observations.irq[22]);
    k22_timing_reset(&timing, 0x82u, 0);
    TEST_EXPECT(state, !k22_timing_read(&timing, PIT_MCR, 1, &(uint32_t){0}));
    TEST_EXPECT(state, !k22_timing_write(&timing, PIT_MCR, 1, 0));
    TEST_EXPECT(state, !k22_timing_read(&timing, PIT_LDVAL0 + 2u, 4, &(uint32_t){0}));
    TEST_EXPECT(state, !k22_timing_write(&timing, PIT_LDVAL0 + 2u, 4, 0));
    TEST_EXPECT(state, !k22_timing_read(&timing, LPTMR_CSR + 2u, 4, &(uint32_t){0}));
    TEST_EXPECT(state, !k22_timing_write(&timing, LPTMR_CSR + 2u, 4, 0));
    TEST_EXPECT(state, !k22_timing_read(&timing, RTC_SR + 0x0cu, 4, &(uint32_t){0}));
    TEST_EXPECT(state, !k22_timing_write(&timing, RTC_SR + 0x0cu, 4, 0));
    TEST_EXPECT(state, !k22_timing_read(&timing, FTM0_SC, 2, &(uint32_t){0}));
    TEST_EXPECT(state, !k22_timing_write(&timing, FTM0_SC, 2, 0));
    TEST_EXPECT(state, !k22_timing_read(&timing, FTM0_SC + 0x9cu, 4, &(uint32_t){0}));
    TEST_EXPECT(state, !k22_timing_read(&timing, WDOG_STCTRLH, 1, &(uint32_t){0}));
    TEST_EXPECT(state, !k22_timing_write(&timing, WDOG_STCTRLH, 1, 0));
    TEST_EXPECT(state, !k22_timing_read(&timing, EWM_CTRL + 4u, 1, &(uint32_t){0}));
    TEST_EXPECT(state, !k22_timing_write(&timing, EWM_CTRL + 4u, 1, 0));
    TEST_EXPECT(state, !k22_timing_read(NULL, 0, 1, &(uint32_t){0}));
    TEST_EXPECT(state, !k22_timing_write(NULL, 0, 1, 0));
    k22_timing_advance(NULL, 1u);
    k22_timing_reset(NULL, 0, 0);
    k22_timing_set_debug_halted(NULL, true);
}

int main(void) {
    TestState state = {0};
    Observations observations = {0};
    K22Timing timing;
    const K22Profile* profile = k22_profile_get(K22_PROFILE_MK22FN51212);
    test_profiles_and_reset(&state);
    TEST_EXPECT(&state, k22_timing_init(&timing, profile, 8000000u, 32768u,
                                        signals(&observations)));
    test_clock_tree_and_power(&state, &timing);
    k22_timing_reset(&timing, 0x82u, 0);
    memset(&observations, 0, sizeof(observations));
    test_pit(&state, &timing, &observations);
    test_lptmr(&state, &timing, &observations);
    test_rtc(&state, &timing, &observations);
    test_rtc_protection_and_compensation(&state, profile);
    test_pdb(&state, &timing, &observations);
    test_ftm(&state, &timing, &observations);
    k22_timing_reset(&timing, 0x82u, 0);
    memset(&observations, 0, sizeof(observations));
    test_watchdogs(&state, &timing, &observations);
    test_copy_and_split_advance(&state, profile);
    TEST_EXPECT(&state, !k22_timing_read(&timing, SIM_SCGC3, 4, &(uint32_t){0}));
    test_register_surface(&state, k22_profile_get(K22_PROFILE_MK22FN1M012));
    test_edge_paths(&state, profile);
    TEST_EXPECT(&state, !k22_timing_init(NULL, profile, 0, 0, signals(NULL)));
    TEST_EXPECT(&state, !k22_timing_copy(NULL, &timing, signals(NULL)));
    TEST_EXPECT(&state, k22_timing_core_clock_hz(NULL) == 0);
    return test_finish(&state);
}
