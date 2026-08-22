#include "device/kinetis_k22/timing/support.h"

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

static void dma_trigger_signal(void* context, uint8_t channel) {
    Observations* observations = context;
    observations->dma_triggers[channel]++;
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

K22TimingSignals k22_timing_test_signals(Observations* observations) {
    K22TimingSignals value = {
        .context = observations,
        .irq = irq_signal,
        .dma = dma_signal,
        .reset = reset_signal,
        .trigger = trigger_signal,
        .dma_trigger = dma_trigger_signal,
    };
    return value;
}

void k22_timing_test_expect_read(TestState* state, K22Timing* timing, uint32_t address,
                                 uint8_t size, uint32_t expected) {
    uint32_t value = 0xdeadbeefu;
    expect(state, k22_timing_read(timing, address, size, &value),
           "k22_timing_read(timing, address, size, &value)");
    if (value != expected) {
        fprintf(stderr, "address 0x%08x: expected 0x%08x, got 0x%08x\n", address, expected, value);
    }
    expect(state, value == expected, "value == expected");
}

void k22_timing_test_expect_write(TestState* state, K22Timing* timing, uint32_t address,
                                  uint8_t size, uint32_t value) {
    expect(state, k22_timing_write(timing, address, size, value),
           "k22_timing_write(timing, address, size, value)");
}

uint32_t k22_timing_test_cycles_for_ticks(const K22Timing* timing, uint32_t ticks,
                                          uint32_t clock_hz) {
    return (uint32_t)(((uint64_t)timing->core_clock_hz * ticks + clock_hz - 1u) / clock_hz);
}

void k22_timing_test_disable_watchdog_fixture(K22Timing* timing) {
    timing->wdog[0] = 0u;
    timing->wdog_pending[0] = 0u;
    timing->wdog_initial_unlock_required = false;
    timing->wdog_update_open = false;
    timing->wdog_reset_pending = false;
}

void k22_timing_test_test_profiles_and_reset(TestState* state) {
    for (K22ProfileId id = 0; id < K22_PROFILE_COUNT; id++) {
        K22Timing timing;
        Observations observations = {0};
        const K22Profile* profile = k22_profile_get(id);
        expect(state, profile != NULL, "profile != NULL");
        expect(state,
               k22_timing_init(&timing, profile, 8000000u, 32768u,
                               k22_timing_test_signals(&observations)),
               "k22_timing_init(&timing, profile, 8000000u, 32768u, "
               "k22_timing_test_signals(&observations))");
        k22_timing_test_expect_read(state, &timing, SIM_SDID, 4, profile->sim_sdid_reset);
        k22_timing_test_expect_read(state, &timing, 0x4004804cu, 4,
                                    id == K22_PROFILE_MK22FN1M012 || id == K22_PROFILE_MK22FX51212
                                        ? 0xff0f0f00u
                                        : 0x0f0f0f00u);
        expect(state,
               k22_timing_read(&timing, SIM_SCGC3, 4, &(uint32_t){0}) ==
                   (id == K22_PROFILE_MK22FN1M012 || id == K22_PROFILE_MK22FX51212),
               "k22_timing_read(&timing, SIM_SCGC3, 4, &(uint32_t){0}) == (id == "
               "K22_PROFILE_MK22FN1M012 || id == K22_PROFILE_MK22FX51212)");
        if (id != K22_PROFILE_MK22FN1M012 && id != K22_PROFILE_MK22FX51212)
            expect(state, !k22_timing_write(&timing, SIM_SCGC3, 4, 0u),
                   "profile without SCGC3 rejects the register");
        k22_timing_test_expect_read(state, &timing, RCM_SRS0, 1, 0x82u);
        k22_timing_test_expect_read(
            state, &timing, 0x40037000u, 4,
            id == K22_PROFILE_MK22FN1M012 || id == K22_PROFILE_MK22FX51212 ? 2u : 6u);
        k22_timing_test_expect_read(state, &timing, 0x4003d014u, 4, 1u);
        k22_timing_test_expect_read(state, &timing, 0x40052000u, 2, 0x01d3u);
        expect(state, k22_timing_core_clock_hz(&timing) == 20971520u,
               "k22_timing_core_clock_hz(&timing) == 20971520u");
        expect(state, k22_timing_bus_clock_hz(&timing) == 20971520u,
               "k22_timing_bus_clock_hz(&timing) == 20971520u");
        expect(state, !k22_timing_read(&timing, MCG_S + 1u, 1, &(uint32_t){0}),
               "!k22_timing_read(&timing, MCG_S + 1u, 1, &(uint32_t){0})");
        expect(state, !k22_timing_read(&timing, SIM_SDID, 1, &(uint32_t){0}),
               "!k22_timing_read(&timing, SIM_SDID, 1, &(uint32_t){0})");
        expect(state, !k22_timing_write(&timing, SIM_SDID, 4, 0),
               "!k22_timing_write(&timing, SIM_SDID, 4, 0)");
    }
}

void k22_timing_test_test_clock_tree_and_power(TestState* state, K22Timing* timing) {
    k22_timing_test_expect_write(state, timing, MCG_C1, 1, 0x32u);
    k22_timing_test_expect_write(state, timing, MCG_C2, 1, 0xa0u);
    k22_timing_test_expect_write(state, timing, MCG_C4, 1, 0x60u);
    k22_timing_test_expect_write(state, timing, SIM_CLKDIV1, 4, 0x03030000u);
    expect(state, k22_timing_core_clock_hz(timing) == 16000000u,
           "k22_timing_core_clock_hz(timing) == 16000000u");
    expect(state, k22_timing_bus_clock_hz(timing) == 4000000u,
           "k22_timing_bus_clock_hz(timing) == 4000000u");
    k22_timing_test_expect_write(state, timing, MCG_C1, 1, 0x80u);
    expect(state, k22_timing_core_clock_hz(timing) == 8000000u,
           "k22_timing_core_clock_hz(timing) == 8000000u");
    k22_timing_test_expect_read(state, timing, MCG_S, 1, 0x08u);
    k22_timing_test_expect_write(state, timing, SIM_CLKDIV1, 4, 0x12000000u);
    expect(state, k22_timing_core_clock_hz(timing) == 4000000u,
           "k22_timing_core_clock_hz(timing) == 4000000u");
    expect(state, k22_timing_bus_clock_hz(timing) == 8000000u / 3u,
           "k22_timing_bus_clock_hz(timing) == 8000000u / 3u");
    k22_timing_test_expect_write(state, timing, MCG_C5, 1, 0);
    k22_timing_test_expect_write(state, timing, MCG_C6, 1, 0x40u);
    k22_timing_test_expect_write(state, timing, MCG_C1, 1, 0);
    expect(state, k22_timing_core_clock_hz(timing) == 96000000u / 2u,
           "k22_timing_core_clock_hz(timing) == 96000000u / 2u");
    k22_timing_test_expect_read(state, timing, MCG_S, 1, 0x6cu);
    k22_timing_test_expect_write(state, timing, SMC_PMPROT, 1, 0x80u);
    k22_timing_test_expect_write(state, timing, SMC_PMCTRL, 1, 0x60u);
    k22_timing_test_expect_read(state, timing, SMC_PMSTAT, 1, 0x80u);
    k22_timing_test_expect_write(state, timing, SMC_PMCTRL, 1, 0);
    k22_timing_test_expect_read(state, timing, SMC_PMSTAT, 1, 1u);
    k22_timing_test_expect_write(state, timing, RCM_SSRS0, 1, 0x80u);
    k22_timing_test_expect_read(state, timing, RCM_SSRS0, 1, 2u);
}

void k22_timing_test_test_low_voltage_control(TestState* state, K22Timing* timing,
                                              Observations* observations) {
    k22_timing_test_expect_read(state, timing, PMC_LVDSC1, 1u, 0x10u);
    k22_timing_test_expect_read(state, timing, PMC_LVDSC2, 1u, 0u);
    k22_timing_test_expect_read(state, timing, PMC_REGSC, 1u, 4u);
    k22_timing_test_expect_write(state, timing, PMC_LVDSC1, 1u, 0x20u);
    k22_timing_test_expect_write(state, timing, PMC_LVDSC1, 1u, 0x30u);
    k22_timing_test_expect_read(state, timing, PMC_LVDSC1, 1u, 0x20u);
    expect(state, k22_timing_trigger_low_voltage_detect(timing),
           "k22_timing_trigger_low_voltage_detect(timing)");
    k22_timing_test_expect_read(state, timing, PMC_LVDSC1, 1u, 0xa0u);
    expect(state, observations->irq[20u], "observations->irq[20u]");
    k22_timing_test_expect_write(state, timing, PMC_LVDSC1, 1u, 0x60u);
    k22_timing_test_expect_read(state, timing, PMC_LVDSC1, 1u, 0x20u);
    expect(state, !observations->irq[20u], "!observations->irq[20u]");

    k22_timing_test_expect_write(state, timing, PMC_LVDSC2, 1u, 0x20u);
    expect(state, k22_timing_trigger_low_voltage_warning(timing),
           "k22_timing_trigger_low_voltage_warning(timing)");
    k22_timing_test_expect_read(state, timing, PMC_LVDSC2, 1u, 0xa0u);
    expect(state, observations->irq[20u], "observations->irq[20u]");
    k22_timing_test_expect_write(state, timing, PMC_LVDSC2, 1u, 0x60u);
    k22_timing_test_expect_read(state, timing, PMC_LVDSC2, 1u, 0x20u);
    expect(state, !observations->irq[20u], "!observations->irq[20u]");

    timing->pmc[2] |= 8u;
    k22_timing_test_expect_write(state, timing, PMC_REGSC, 1u, 0x19u);
    k22_timing_test_expect_read(state, timing, PMC_REGSC, 1u, 0x15u);

    const uint32_t reset_count = observations->resets;
    k22_timing_reset(timing, 0x82u, 0u);
    expect(state, k22_timing_trigger_low_voltage_detect(timing),
           "k22_timing_trigger_low_voltage_detect(timing)");
    expect(state, observations->resets == reset_count + 1u,
           "observations->resets == reset_count + 1u");
    k22_timing_test_expect_read(state, timing, RCM_SRS0, 1u, 2u);
    expect(state, !k22_timing_trigger_low_voltage_warning(NULL),
           "!k22_timing_trigger_low_voltage_warning(NULL)");
    expect(state, !k22_timing_trigger_low_voltage_detect(NULL),
           "!k22_timing_trigger_low_voltage_detect(NULL)");
}

void k22_timing_test_test_low_leakage_wakeup(TestState* state, K22Timing* timing,
                                             Observations* observations) {
    k22_timing_set_cpu_sleeping(timing, true, true);
    k22_timing_test_expect_read(state, timing, SMC_PMSTAT, 1u, 2u);
    k22_timing_set_cpu_sleeping(timing, false, false);
    k22_timing_test_expect_read(state, timing, SMC_PMSTAT, 1u, 1u);

    k22_timing_test_expect_write(state, timing, SMC_PMPROT, 1u, 0x0au);
    k22_timing_test_expect_write(state, timing, SMC_PMCTRL, 1u, 3u);
    k22_timing_set_cpu_sleeping(timing, true, true);
    k22_timing_test_expect_read(state, timing, SMC_PMSTAT, 1u, 0x20u);
    k22_timing_test_expect_write(state, timing, LLWU_PE1, 1u, 1u);
    expect(state, k22_timing_set_llwu_pin(timing, 0u, false),
           "k22_timing_set_llwu_pin(timing, 0u, false)");
    expect(state, k22_timing_set_llwu_pin(timing, 0u, true),
           "k22_timing_set_llwu_pin(timing, 0u, true)");
    k22_timing_test_expect_read(state, timing, LLWU_F1, 1u, 1u);
    expect(state, observations->irq[21u], "observations->irq[21u]");
    k22_timing_test_expect_write(state, timing, LLWU_F1, 1u, 1u);
    expect(state, !observations->irq[21u], "!observations->irq[21u]");
    k22_timing_set_cpu_sleeping(timing, false, false);
    k22_timing_test_expect_read(state, timing, SMC_PMSTAT, 1u, 1u);

    k22_timing_test_expect_write(state, timing, LLWU_FILT1, 1u, 0x22u);
    k22_timing_set_cpu_sleeping(timing, true, true);
    expect(state, k22_timing_set_llwu_pin(timing, 2u, false),
           "k22_timing_set_llwu_pin(timing, 2u, false)");
    expect(state, k22_timing_set_llwu_pin(timing, 2u, true),
           "k22_timing_set_llwu_pin(timing, 2u, true)");
    k22_timing_test_expect_read(state, timing, LLWU_FILT1, 1u, 0xa2u);
    expect(state, observations->irq[21u], "observations->irq[21u]");
    k22_timing_test_expect_write(state, timing, LLWU_FILT1, 1u, 0x80u);
    k22_timing_test_expect_read(state, timing, LLWU_FILT1, 1u, 0u);
    expect(state, !observations->irq[21u], "!observations->irq[21u]");

    k22_timing_reset(timing, 0x82u, 0u);
    memset(observations, 0, sizeof(*observations));
    k22_timing_test_expect_write(state, timing, SMC_PMPROT, 1u, 8u);
    k22_timing_test_expect_write(state, timing, SMC_PMCTRL, 1u, 3u);
    k22_timing_test_expect_write(state, timing, LLWU_ME, 1u, 8u);
    k22_timing_set_cpu_sleeping(timing, true, true);
    expect(state, k22_timing_trigger_llwu_module(timing, 3u),
           "k22_timing_trigger_llwu_module(timing, 3u)");
    k22_timing_test_expect_read(state, timing, LLWU_F3, 1u, 8u);
    expect(state, observations->irq[21u], "observations->irq[21u]");

    k22_timing_reset(timing, 0x82u, 0u);
    memset(observations, 0, sizeof(*observations));
    k22_timing_test_expect_write(state, timing, SMC_PMPROT, 1u, 0x20u);
    k22_timing_test_expect_write(state, timing, SMC_PMCTRL, 1u, 0x40u);
    k22_timing_test_expect_read(state, timing, SMC_PMSTAT, 1u, 4u);
    expect(state, k22_timing_trigger_low_voltage_warning(timing),
           "k22_timing_trigger_low_voltage_warning(timing)");
    expect(state, k22_timing_trigger_low_voltage_detect(timing),
           "k22_timing_trigger_low_voltage_detect(timing)");
    k22_timing_test_expect_read(state, timing, PMC_LVDSC1, 1u, 0x10u);
    k22_timing_test_expect_read(state, timing, PMC_LVDSC2, 1u, 0u);
    expect(state, observations->resets == 0u, "observations->resets == 0u");

    k22_timing_reset(timing, 0x82u, 0u);
    memset(observations, 0, sizeof(*observations));
    k22_timing_test_expect_write(state, timing, SMC_PMPROT, 1u, 0x20u);
    k22_timing_test_expect_write(state, timing, SMC_PMCTRL, 1u, 2u);
    k22_timing_set_cpu_sleeping(timing, true, true);
    k22_timing_test_expect_read(state, timing, SMC_PMSTAT, 1u, 0x10u);
    k22_timing_set_cpu_sleeping(timing, false, false);
    k22_timing_test_expect_read(state, timing, SMC_PMSTAT, 1u, 1u);

    k22_timing_reset(timing, 0x82u, 0u);
    memset(observations, 0, sizeof(*observations));
    k22_timing_test_expect_write(state, timing, SMC_PMPROT, 1u, 2u);
    k22_timing_test_expect_write(state, timing, SMC_PMCTRL, 1u, 4u);
    k22_timing_test_expect_write(state, timing, LLWU_PE1, 1u, 4u);
    k22_timing_set_cpu_sleeping(timing, true, true);
    k22_timing_test_expect_read(state, timing, SMC_PMSTAT, 1u, 0x40u);
    expect(state, k22_timing_set_llwu_pin(timing, 1u, true),
           "k22_timing_set_llwu_pin(timing, 1u, true)");
    expect(state, observations->resets == 1u, "observations->resets == 1u");
    k22_timing_test_expect_read(state, timing, RCM_SRS0, 1u, 1u);

    expect(state, !k22_timing_set_llwu_pin(NULL, 0u, false),
           "!k22_timing_set_llwu_pin(NULL, 0u, false)");
    expect(state, !k22_timing_set_llwu_pin(timing, 16u, false),
           "!k22_timing_set_llwu_pin(timing, 16u, false)");
    expect(state, !k22_timing_trigger_llwu_module(NULL, 0u),
           "!k22_timing_trigger_llwu_module(NULL, 0u)");
    expect(state, !k22_timing_trigger_llwu_module(timing, 8u),
           "!k22_timing_trigger_llwu_module(timing, 8u)");
    k22_timing_set_cpu_sleeping(NULL, true, true);
}

void k22_timing_test_test_pit(TestState* state, K22Timing* timing, Observations* observations) {
    const uint32_t alternate_before = observations->alternate_triggers;
    k22_timing_test_expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 | (1u << 23u));
    k22_timing_test_expect_write(state, timing, PIT_MCR, 4, 0);
    k22_timing_test_expect_write(state, timing, PIT_LDVAL0, 4, 2u);
    k22_timing_test_expect_write(state, timing, PIT_LDVAL1, 4, 1u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL0, 4, 3u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL1, 4, 7u);
    k22_timing_advance(timing, 6u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL0, 4, 2u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL1, 4, 1u);
    expect(state, observations->irq[48], "observations->irq[48]");
    expect(state, observations->alternate_triggers == alternate_before + 2u,
           "observations->alternate_triggers == alternate_before + 2u");
    expect(state, observations->dma_triggers[0] == 1u, "observations->dma_triggers[0] == 1u");
    expect(state, observations->dma_triggers[1] == 1u, "observations->dma_triggers[1] == 1u");
    expect(state, observations->last_trigger_instance == 5u,
           "observations->last_trigger_instance == 5u");
    k22_timing_test_expect_write(state, timing, PIT_TFLG0, 4, 1u);
    expect(state, !observations->irq[48], "!observations->irq[48]");
    k22_timing_test_expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 & ~(1u << 23u));
    k22_timing_advance(timing, 100u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL0, 4, 2u);

    k22_timing_test_expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 | (1u << 23u));
    k22_timing_test_expect_write(state, timing, PIT_MCR, 4, 0u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL0, 4, 0u);
    k22_timing_test_expect_write(state, timing, PIT_LDVAL0, 4, 5u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL0, 4, 1u);
    k22_timing_advance(timing, 2u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL0, 4, 3u);
    k22_timing_test_expect_write(state, timing, PIT_LDVAL0, 4, 9u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL0, 4, 3u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL0, 4, 3u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL0, 4, 3u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL0, 4, 1u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL0, 4, 3u);
    k22_timing_advance(timing, 4u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL0, 4, 9u);
    k22_timing_test_expect_read(state, timing, PIT_TFLG0, 4, 1u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL0, 4, 3u);
    expect(state, observations->irq[48], "observations->irq[48]");
    k22_timing_test_expect_write(state, timing, PIT_TCTRL0, 4, 1u);
    expect(state, !observations->irq[48], "!observations->irq[48]");
    k22_timing_test_expect_write(state, timing, PIT_TCTRL0, 4, 7u);
    k22_timing_test_expect_read(state, timing, PIT_TCTRL0, 4, 3u);
    k22_timing_test_expect_write(state, timing, PIT_TFLG0, 4, 1u);

    k22_timing_test_expect_write(state, timing, PIT_TCTRL0, 4, 0u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL1, 4, 0u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL2, 4, 0u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL3, 4, 0u);
    k22_timing_test_expect_write(state, timing, PIT_LDVAL0, 4, 1u);
    k22_timing_test_expect_write(state, timing, PIT_LDVAL1, 4, 1u);
    k22_timing_test_expect_write(state, timing, PIT_LDVAL2, 4, 1u);
    k22_timing_test_expect_write(state, timing, PIT_LDVAL3, 4, 1u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL0, 4, 1u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL1, 4, 5u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL2, 4, 5u);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL3, 4, 7u);
    k22_timing_advance(timing, 15u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL0, 4, 0u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL1, 4, 0u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL2, 4, 0u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL3, 4, 0u);
    k22_timing_test_expect_read(state, timing, PIT_TFLG0, 4, 1u);
    k22_timing_test_expect_read(state, timing, PIT_TFLG1, 4, 1u);
    k22_timing_test_expect_read(state, timing, PIT_TFLG2, 4, 1u);
    k22_timing_test_expect_read(state, timing, PIT_TFLG3, 4, 0u);
    expect(state, !observations->irq[48], "!observations->irq[48]");
    expect(state, !observations->irq[49], "!observations->irq[49]");
    expect(state, !observations->irq[50], "!observations->irq[50]");
    expect(state, !observations->irq[51], "!observations->irq[51]");
    k22_timing_advance(timing, 1u);
    k22_timing_test_expect_read(state, timing, PIT_TFLG3, 4, 1u);
    expect(state, observations->irq[51], "observations->irq[51]");
    k22_timing_test_expect_write(state, timing, PIT_TFLG0, 4, 1u);
    k22_timing_test_expect_write(state, timing, PIT_TFLG1, 4, 1u);
    k22_timing_test_expect_write(state, timing, PIT_TFLG2, 4, 1u);
    k22_timing_test_expect_write(state, timing, PIT_TFLG3, 4, 1u);

    k22_timing_test_expect_write(state, timing, PIT_TCTRL0, 4, 0u);
    k22_timing_test_expect_write(state, timing, PIT_LDVAL0, 4, UINT32_MAX);
    k22_timing_test_expect_write(state, timing, PIT_TCTRL0, 4, 1u);
    timing->wdog[0] = 0u;
    k22_timing_advance(timing, UINT32_MAX);
    k22_timing_test_expect_read(state, timing, PIT_CVAL0, 4, 0u);
    k22_timing_test_expect_read(state, timing, PIT_TFLG0, 4, 0u);
    k22_timing_test_expect_write(state, timing, PIT_MCR, 4, 2u);
    k22_timing_advance(timing, 1u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL0, 4, 0u);
    k22_timing_test_expect_write(state, timing, PIT_MCR, 4, 0u);
    k22_timing_advance(timing, 1u);
    k22_timing_test_expect_read(state, timing, PIT_CVAL0, 4, UINT32_MAX);
    k22_timing_test_expect_read(state, timing, PIT_TFLG0, 4, 1u);
}

void k22_timing_test_test_lptmr(TestState* state, K22Timing* timing, Observations* observations) {
    const uint32_t alternate_before = observations->alternate_triggers;
    k22_timing_test_expect_write(state, timing, SIM_SCGC5, 4, timing->sim_scgc5 | 1u);
    k22_timing_test_expect_write(state, timing, LPTMR_PSR, 4, 5u);
    k22_timing_test_expect_write(state, timing, LPTMR_CMR, 4, 2u);
    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0x41u);
    k22_timing_advance(timing, k22_timing_test_cycles_for_ticks(timing, 2u, 1000u));
    k22_timing_test_expect_read(state, timing, LPTMR_CSR, 4, 0x41u);
    k22_timing_test_expect_write(state, timing, LPTMR_CNR, 4, UINT32_MAX);
    k22_timing_test_expect_read(state, timing, LPTMR_CNR, 4, 2u);
    k22_timing_advance(timing, k22_timing_test_cycles_for_ticks(timing, 1u, 1000u));
    k22_timing_test_expect_read(state, timing, LPTMR_CSR, 4, 0xc1u);
    expect(state, observations->irq[58], "observations->irq[58]");
    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0xc1u);
    expect(state, !observations->irq[58], "!observations->irq[58]");
    k22_timing_test_expect_write(state, timing, LPTMR_CNR, 4, 0u);
    k22_timing_test_expect_read(state, timing, LPTMR_CNR, 4, 0u);
    k22_timing_test_expect_write(state, timing, LPTMR_CMR, 4, 0u);
    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0u);
    k22_timing_test_expect_write(state, timing, LPTMR_CMR, 4, 0u);
    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 1u);
    k22_timing_advance(timing, k22_timing_test_cycles_for_ticks(timing, 1u, 1000u));
    k22_timing_test_expect_read(state, timing, LPTMR_CSR, 4, 0x81u);

    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0u);
    k22_timing_test_expect_write(state, timing, LPTMR_PSR, 4, 5u);
    k22_timing_test_expect_write(state, timing, LPTMR_CMR, 4, 5u);
    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0x47u);
    k22_timing_test_expect_write(state, timing, LPTMR_PSR, 4, 0x7fu);
    k22_timing_test_expect_read(state, timing, LPTMR_PSR, 4, 5u);
    k22_timing_test_expect_write(state, timing, LPTMR_CMR, 4, 9u);
    k22_timing_test_expect_read(state, timing, LPTMR_CMR, 4, 5u);
    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0x7fu);
    k22_timing_test_expect_read(state, timing, LPTMR_CSR, 4, 0x47u);

    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0u);
    k22_timing_test_expect_write(state, timing, LPTMR_PSR, 4, 4u);
    k22_timing_test_expect_write(state, timing, LPTMR_CMR, 4, 1u);
    expect(state, k22_timing_set_lptmr_input(timing, 0u, false),
           "k22_timing_set_lptmr_input(timing, 0u, false)");
    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0x43u);
    expect(state, k22_timing_set_lptmr_input(timing, 1u, true),
           "k22_timing_set_lptmr_input(timing, 1u, true)");
    expect(state, k22_timing_set_lptmr_input(timing, 0u, false),
           "k22_timing_set_lptmr_input(timing, 0u, false)");
    expect(state, k22_timing_set_lptmr_input(timing, 0u, true),
           "k22_timing_set_lptmr_input(timing, 0u, true)");
    k22_timing_test_expect_write(state, timing, LPTMR_CNR, 4, 0u);
    k22_timing_test_expect_read(state, timing, LPTMR_CNR, 4, 1u);
    expect(state, k22_timing_set_lptmr_input(timing, 0u, false),
           "k22_timing_set_lptmr_input(timing, 0u, false)");
    expect(state, k22_timing_set_lptmr_input(timing, 0u, true),
           "k22_timing_set_lptmr_input(timing, 0u, true)");
    k22_timing_test_expect_read(state, timing, LPTMR_CSR, 4, 0xc3u);
    expect(state, observations->irq[58], "observations->irq[58]");
    k22_timing_test_expect_write(state, timing, LPTMR_CNR, 4, 0u);
    k22_timing_test_expect_read(state, timing, LPTMR_CNR, 4, 0u);

    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0u);
    k22_timing_test_expect_write(state, timing, LPTMR_PSR, 4, 4u);
    k22_timing_test_expect_write(state, timing, LPTMR_CMR, 4, 3u);
    expect(state, k22_timing_set_lptmr_input(timing, 2u, true),
           "k22_timing_set_lptmr_input(timing, 2u, true)");
    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0x2fu);
    expect(state, k22_timing_set_lptmr_input(timing, 2u, false),
           "k22_timing_set_lptmr_input(timing, 2u, false)");
    expect(state, k22_timing_set_lptmr_input(timing, 2u, true),
           "k22_timing_set_lptmr_input(timing, 2u, true)");
    k22_timing_test_expect_write(state, timing, LPTMR_CNR, 4, 0u);
    k22_timing_test_expect_read(state, timing, LPTMR_CNR, 4, 1u);
    expect(state, k22_timing_set_lptmr_input(timing, 2u, false),
           "k22_timing_set_lptmr_input(timing, 2u, false)");
    k22_timing_test_expect_write(state, timing, LPTMR_CNR, 4, 0u);
    k22_timing_test_expect_read(state, timing, LPTMR_CNR, 4, 2u);

    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0u);
    k22_timing_test_expect_write(state, timing, LPTMR_PSR, 4, 4u);
    k22_timing_test_expect_write(state, timing, LPTMR_CMR, 4, 1u);
    expect(state, k22_timing_set_lptmr_input(timing, 0u, false),
           "k22_timing_set_lptmr_input(timing, 0u, false)");
    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0x47u);
    expect(state, k22_timing_set_lptmr_input(timing, 0u, false),
           "k22_timing_set_lptmr_input(timing, 0u, false)");
    expect(state, k22_timing_set_lptmr_input(timing, 0u, true),
           "k22_timing_set_lptmr_input(timing, 0u, true)");
    expect(state, k22_timing_set_lptmr_input(timing, 0u, false),
           "k22_timing_set_lptmr_input(timing, 0u, false)");
    expect(state, k22_timing_set_lptmr_input(timing, 0u, true),
           "k22_timing_set_lptmr_input(timing, 0u, true)");
    k22_timing_test_expect_read(state, timing, LPTMR_CSR, 4, 0xc7u);
    expect(state, observations->irq[58], "observations->irq[58]");
    expect(state, observations->alternate_triggers > alternate_before,
           "observations->alternate_triggers > alternate_before");
    expect(state, observations->last_trigger_instance == 14u,
           "observations->last_trigger_instance == 14u");
    k22_timing_test_expect_write(state, timing, LPTMR_CNR, 4, 0u);
    k22_timing_test_expect_read(state, timing, LPTMR_CNR, 4, 2u);
    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0xc7u);
    expect(state, !observations->irq[58], "!observations->irq[58]");

    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0u);
    k22_timing_test_expect_write(state, timing, LPTMR_PSR, 4, 9u);
    k22_timing_test_expect_write(state, timing, LPTMR_CMR, 4, 4u);
    expect(state, k22_timing_set_lptmr_input(timing, 0u, false),
           "k22_timing_set_lptmr_input(timing, 0u, false)");
    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 3u);
    k22_timing_advance(timing, k22_timing_test_cycles_for_ticks(timing, 2u, 1000u));
    k22_timing_advance(timing, k22_timing_test_cycles_for_ticks(timing, 1u, 1000u));
    expect(state, k22_timing_set_lptmr_input(timing, 0u, true),
           "k22_timing_set_lptmr_input(timing, 0u, true)");
    k22_timing_advance(timing, k22_timing_test_cycles_for_ticks(timing, 1u, 1000u));
    k22_timing_test_expect_write(state, timing, LPTMR_CNR, 4, 0u);
    k22_timing_test_expect_read(state, timing, LPTMR_CNR, 4, 0u);
    k22_timing_advance(timing, k22_timing_test_cycles_for_ticks(timing, 1u, 1000u));
    k22_timing_test_expect_write(state, timing, LPTMR_CNR, 4, 0u);
    k22_timing_test_expect_read(state, timing, LPTMR_CNR, 4, 1u);

    k22_timing_warm_reset(timing, 0x20u, 0u);
    k22_timing_test_expect_write(state, timing, LPTMR_CNR, 4, 0u);
    k22_timing_test_expect_read(state, timing, LPTMR_CNR, 4, 1u);
    k22_timing_warm_reset(timing, 0x04u, 0u);
    k22_timing_test_expect_read(state, timing, LPTMR_CSR, 4, 0u);
    expect(state, !k22_timing_set_lptmr_input(NULL, 0u, false),
           "!k22_timing_set_lptmr_input(NULL, 0u, false)");
    expect(state, !k22_timing_set_lptmr_input(timing, 3u, false),
           "!k22_timing_set_lptmr_input(timing, 3u, false)");
}

void k22_timing_test_test_rtc(TestState* state, K22Timing* timing, Observations* observations) {
    const uint32_t alternate_before = observations->alternate_triggers;
    k22_timing_test_expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 | (1u << 29u));
    k22_timing_test_expect_read(state, timing, RTC_TSR, 4, 0u);
    k22_timing_test_expect_read(state, timing, RTC_TPR, 4, 0u);
    k22_timing_test_expect_write(state, timing, RTC_TPR, 4, 7u);
    k22_timing_test_expect_write(state, timing, RTC_TSR, 4, 10u);
    k22_timing_test_expect_read(state, timing, RTC_TPR, 4, 7u);
    k22_timing_test_expect_write(state, timing, RTC_TPR, 4, 0u);
    k22_timing_test_expect_write(state, timing, RTC_TAR, 4, 11u);
    k22_timing_test_expect_write(state, timing, RTC_IER, 4, 0x14u);
    k22_timing_test_expect_write(state, timing, RTC_CR, 4, 0x100u);
    k22_timing_test_expect_write(state, timing, RTC_SR, 4, 0x10u);
    k22_timing_test_expect_write(state, timing, RTC_TSR, 4, 20u);
    k22_timing_test_expect_write(state, timing, RTC_TPR, 4, 20u);
    k22_timing_advance(timing, timing->core_clock_hz);
    k22_timing_test_expect_read(state, timing, RTC_TSR, 4, 11u);
    k22_timing_test_expect_read(state, timing, RTC_TPR, 4, 0u);
    expect(state, observations->irq[46], "observations->irq[46]");
    expect(state, !observations->irq[47], "!observations->irq[47]");
    expect(state, observations->irq_assertions[47] == 1u, "observations->irq_assertions[47] == 1u");
    k22_timing_test_expect_read(state, timing, RTC_SR, 4, 0x14u);
    expect(state, observations->alternate_triggers == alternate_before + 2u,
           "observations->alternate_triggers == alternate_before + 2u");
    expect(state, observations->last_trigger_instance == 12u,
           "observations->last_trigger_instance == 12u");
    k22_timing_test_expect_write(state, timing, RTC_TAR, 4, 15u);
    k22_timing_test_expect_read(state, timing, RTC_SR, 4, 0x10u);
    expect(state, !observations->irq[46], "!observations->irq[46]");
    const uint32_t retained_tsr = timing->rtc_tsr;
    const uint16_t retained_tpr = timing->rtc_tpr;
    k22_timing_test_expect_write(state, timing, RTC_RAR, 4, 0xfbu);
    k22_timing_test_expect_read(state, timing, RTC_TAR, 4, 0u);
    k22_timing_test_expect_write(state, timing, RTC_RAR, 4, 0xffu);
    k22_timing_test_expect_read(state, timing, RTC_TAR, 4, 0u);
    k22_timing_test_expect_write(state, timing, RTC_WAR, 4, 0xfbu);
    k22_timing_test_expect_write(state, timing, RTC_TAR, 4, 22u);
    k22_timing_warm_reset(timing, 0x20u, 0);
    expect(state, timing->rtc_tsr == retained_tsr, "timing->rtc_tsr == retained_tsr");
    expect(state, timing->rtc_tpr == retained_tpr, "timing->rtc_tpr == retained_tpr");
    k22_timing_test_expect_read(state, timing, RTC_WAR, 4, 0xffu);
    k22_timing_test_expect_read(state, timing, RTC_RAR, 4, 0xffu);
    k22_timing_test_expect_read(state, timing, RTC_TAR, 4, 15u);
    k22_timing_test_expect_read(state, timing, RCM_SRS0, 1, 0x20u);
}

void k22_timing_test_test_rtc_protection_and_compensation(TestState* state,
                                                          const K22Profile* profile) {
    Observations observations = {0};
    K22Timing timing;
    expect(
        state,
        k22_timing_init(&timing, profile, 8000000u, 32768u, k22_timing_test_signals(&observations)),
        "k22_timing_init(&timing, profile, 8000000u, 32768u, "
        "k22_timing_test_signals(&observations))");
    k22_timing_test_disable_watchdog_fixture(&timing);
    k22_timing_test_expect_write(state, &timing, RTC_TSR, 4, 1u);
    k22_timing_test_expect_write(state, &timing, RTC_SR, 4, 0x10u);
    k22_timing_advance(&timing, timing.core_clock_hz);
    k22_timing_test_expect_read(state, &timing, RTC_TSR, 4, 1u);
    k22_timing_test_expect_write(state, &timing, RTC_SR, 4, 0u);
    k22_timing_test_expect_write(state, &timing, RTC_CR, 4, 0x100u);
    k22_timing_test_expect_write(state, &timing, RTC_TCR, 4, 0x017fu);
    k22_timing_test_expect_write(state, &timing, RTC_SR, 4, 0x10u);
    k22_timing_advance(&timing, timing.core_clock_hz);
    k22_timing_test_expect_read(state, &timing, RTC_TSR, 4, 2u);
    k22_timing_test_expect_read(state, &timing, RTC_TCR, 4, 0x017f017fu);
    k22_timing_advance(&timing,
                       k22_timing_test_cycles_for_ticks(&timing, 32640u, timing.rtc_oscillator_hz));
    k22_timing_test_expect_read(state, &timing, RTC_TSR, 4, 2u);
    k22_timing_advance(&timing,
                       k22_timing_test_cycles_for_ticks(&timing, 1u, timing.rtc_oscillator_hz));
    k22_timing_test_expect_read(state, &timing, RTC_TSR, 4, 3u);
    k22_timing_test_expect_read(state, &timing, RTC_TCR, 4, 0x0000017fu);

    k22_timing_test_expect_write(state, &timing, RTC_SR, 4, 0u);
    k22_timing_test_expect_write(state, &timing, RTC_TCR, 4, 0x0080u);
    k22_timing_test_expect_write(state, &timing, RTC_TPR, 4, 0u);
    k22_timing_test_expect_write(state, &timing, RTC_SR, 4, 0x10u);
    k22_timing_advance(&timing, timing.core_clock_hz);
    k22_timing_test_expect_read(state, &timing, RTC_TCR, 4, 0x00800080u);
    const uint32_t before = timing.rtc_tsr;
    k22_timing_advance(&timing, timing.core_clock_hz);
    k22_timing_test_expect_read(state, &timing, RTC_TSR, 4, before);
    k22_timing_advance(&timing,
                       k22_timing_test_cycles_for_ticks(&timing, 128u, timing.rtc_oscillator_hz));
    k22_timing_test_expect_read(state, &timing, RTC_TSR, 4, before + 1u);

    k22_timing_test_expect_write(state, &timing, RTC_SR, 4, 0u);
    k22_timing_test_expect_write(state, &timing, RTC_TSR, 4, UINT32_MAX);
    k22_timing_test_expect_write(state, &timing, RTC_IER, 4, 2u);
    k22_timing_test_expect_write(state, &timing, RTC_CR, 4, 0x100u);
    k22_timing_test_expect_write(state, &timing, RTC_SR, 4, 0x10u);
    k22_timing_advance(
        &timing, timing.core_clock_hz +
                     k22_timing_test_cycles_for_ticks(&timing, 128u, timing.rtc_oscillator_hz));
    k22_timing_test_expect_read(state, &timing, RTC_SR, 4, 0x16u);
    k22_timing_test_expect_read(state, &timing, RTC_TSR, 4, 0u);
    k22_timing_test_expect_read(state, &timing, RTC_TPR, 4, 0u);
    expect(state, observations.irq[46], "observations.irq[46]");
    k22_timing_test_expect_write(state, &timing, RTC_SR, 4, 0u);
    k22_timing_test_expect_write(state, &timing, RTC_TSR, 4, 9u);
    k22_timing_test_expect_write(state, &timing, RTC_TAR, 4, 10u);
    k22_timing_test_expect_read(state, &timing, RTC_SR, 4, 0u);
    expect(state, !observations.irq[46], "!observations.irq[46]");

    k22_timing_test_expect_write(state, &timing, RTC_CR, 4, 0x108u);
    k22_timing_test_expect_write(state, &timing, RTC_LR, 4, 0xdfu);
    k22_timing_test_expect_write(state, &timing, RTC_SR, 4, 0x10u);
    k22_timing_test_expect_read(state, &timing, RTC_SR, 4, 0x10u);
    k22_timing_test_expect_write(state, &timing, RTC_SR, 4, 0u);
    k22_timing_test_expect_read(state, &timing, RTC_SR, 4, 0x10u);
    k22_timing_test_expect_write(state, &timing, RTC_CR, 4, 1u);
    k22_timing_test_expect_read(state, &timing, RTC_SR, 4, 1u);

    k22_timing_test_expect_write(state, &timing, RTC_TCR, 4, 0x1234u);
    k22_timing_test_expect_write(state, &timing, RTC_LR, 4, 0xf7u);
    k22_timing_test_expect_write(state, &timing, RTC_TCR, 4, 0x5678u);
    k22_timing_test_expect_read(state, &timing, RTC_TCR, 4, 0x00001234u);
    k22_timing_test_expect_write(state, &timing, RTC_CR, 4, 1u);
    k22_timing_test_expect_read(state, &timing, RTC_CR, 4, 1u);
    k22_timing_test_expect_read(state, &timing, RTC_SR, 4, 1u);
    k22_timing_test_expect_read(state, &timing, RTC_LR, 4, 0xffu);
    k22_timing_test_expect_write(state, &timing, RTC_CR, 4, 0u);
    k22_timing_test_expect_read(state, &timing, RTC_CR, 4, 0u);

    k22_timing_test_expect_write(state, &timing, RTC_WAR, 4, 0x7fu);
    k22_timing_test_expect_write(state, &timing, RTC_IER, 4, 0x17u);
    k22_timing_test_expect_read(state, &timing, RTC_IER, 4, 7u);
    k22_timing_test_expect_write(state, &timing, RTC_RAR, 4, 0x7fu);
    k22_timing_test_expect_read(state, &timing, RTC_IER, 4, 0u);
    k22_timing_test_expect_read(state, &timing, RTC_WAR, 4, 0x7fu);
    k22_timing_test_expect_read(state, &timing, RTC_RAR, 4, 0x7fu);
}
