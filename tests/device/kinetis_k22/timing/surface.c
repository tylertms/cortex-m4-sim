#include "device/kinetis_k22/timing/internal.h"

static void unlock_watchdog(TestState* state, K22Timing* timing) {
    k22_timing_test_expect_write(state, timing, WDOG_UNLOCK, 2, 0xc520u);
    k22_timing_test_expect_write(state, timing, WDOG_UNLOCK, 2, 0xd928u);
}

static void test_watchdogs(TestState* state, K22Timing* timing, Observations* observations) {
    unlock_watchdog(state, timing);
    k22_timing_test_expect_write(state, timing, WDOG_TOVALH, 2, 0);
    k22_timing_test_expect_write(state, timing, WDOG_TOVALL, 2, 3u);
    k22_timing_test_expect_write(state, timing, WDOG_PRESC, 2, 0);
    k22_timing_test_expect_write(state, timing, WDOG_STCTRLH, 2, 1u);
    k22_timing_advance(timing,
                       k22_timing_test_cycles_for_ticks(timing, 260u, timing->bus_clock_hz));
    k22_timing_advance(timing, k22_timing_test_cycles_for_ticks(timing, 1u, 1000u));
    k22_timing_test_expect_write(state, timing, WDOG_REFRESH, 2, 0xa602u);
    k22_timing_test_expect_write(state, timing, WDOG_REFRESH, 2, 0xb480u);
    k22_timing_advance(timing, k22_timing_test_cycles_for_ticks(timing, 2u, 1000u));
    expect(state, observations->resets == 0, "observations->resets == 0");
    k22_timing_advance(timing, k22_timing_test_cycles_for_ticks(timing, 1u, 1000u));
    expect(state, observations->resets == 1u, "observations->resets == 1u");
    expect(state, observations->last_srs0 == 0x20u, "observations->last_srs0 == 0x20u");
    k22_timing_test_expect_read(state, timing, RCM_SRS0, 1, 0x20u);
    k22_timing_test_expect_read(state, timing, WDOG_STCTRLH + 0x14u, 2, 1u);
    k22_timing_test_disable_watchdog_fixture(timing);
    expect(state, k22_timing_ewm_output(timing), "k22_timing_ewm_output(timing)");
    k22_timing_test_expect_write(state, timing, EWM_CMPL, 1, 1u);
    k22_timing_test_expect_write(state, timing, EWM_CMPH, 1, 3u);
    k22_timing_test_expect_write(state, timing, EWM_CTRL, 1, 9u);
    expect(state, !k22_timing_ewm_output(timing), "!k22_timing_ewm_output(timing)");
    k22_timing_advance(timing, k22_timing_test_cycles_for_ticks(timing, 2u, 1000u));
    k22_timing_test_expect_write(state, timing, EWM_SERV, 1, 0xb4u);
    k22_timing_test_expect_write(state, timing, EWM_SERV, 1, 0x2cu);
    expect(state, !k22_timing_ewm_output(timing), "!k22_timing_ewm_output(timing)");
    k22_timing_advance(timing, k22_timing_test_cycles_for_ticks(timing, 3u, 1000u));
    expect(state, k22_timing_ewm_output(timing), "k22_timing_ewm_output(timing)");
    expect(state, observations->irq[22u], "observations->irq[22u]");
    expect(state, observations->resets == 1u, "observations->resets == 1u");
    k22_timing_test_expect_write(state, timing, EWM_CTRL, 1, 1u);
    expect(state, !observations->irq[22u], "!observations->irq[22u]");
}

static void test_copy_and_split_advance(TestState* state, const K22Profile* profile) {
    Observations first_observations = {0};
    Observations second_observations = {0};
    K22Timing first;
    K22Timing second;
    expect(state,
           k22_timing_init(&first, profile, 8000000u, 32768u,
                           k22_timing_test_signals(&first_observations)),
           "k22_timing_init(&first, profile, 8000000u, 32768u, "
           "k22_timing_test_signals(&first_observations))");
    k22_timing_test_disable_watchdog_fixture(&first);
    k22_timing_test_expect_write(state, &first, SIM_SCGC6, 4, first.sim_scgc6 | (1u << 23u));
    k22_timing_test_expect_write(state, &first, PIT_MCR, 4, 0);
    k22_timing_test_expect_write(state, &first, PIT_LDVAL0, 4, 6u);
    k22_timing_test_expect_write(state, &first, PIT_TCTRL0, 4, 3u);
    expect(state, k22_timing_copy(&second, &first, k22_timing_test_signals(&second_observations)),
           "k22_timing_copy(&second, &first, k22_timing_test_signals(&second_observations))");
    k22_timing_advance(&first, 1000003u);
    for (uint32_t count = 0; count < 1000u; count++)
        k22_timing_advance(&second, 1000u);
    k22_timing_advance(&second, 3u);
    expect(state, first.pit[0].current == second.pit[0].current,
           "first.pit[0].current == second.pit[0].current");
    expect(state, first.pit[0].flag == second.pit[0].flag,
           "first.pit[0].flag == second.pit[0].flag");
    expect(state, first.pit_remainder == second.pit_remainder,
           "first.pit_remainder == second.pit_remainder");
    expect(state, first.elapsed_core_cycles == second.elapsed_core_cycles,
           "first.elapsed_core_cycles == second.elapsed_core_cycles");

    k22_timing_reset(&first, 0x82u, 0u);
    k22_timing_test_disable_watchdog_fixture(&first);
    k22_timing_test_expect_write(state, &first, SIM_SCGC5, 4, first.sim_scgc5 | 1u);
    k22_timing_test_expect_write(state, &first, LPTMR_PSR, 4, 9u);
    k22_timing_test_expect_write(state, &first, LPTMR_CMR, 4, 4u);
    expect(state, k22_timing_set_lptmr_input(&first, 0u, false),
           "k22_timing_set_lptmr_input(&first, 0u, false)");
    k22_timing_test_expect_write(state, &first, LPTMR_CSR, 4, 3u);
    k22_timing_advance(&first, k22_timing_test_cycles_for_ticks(&first, 1u, 1000u));
    expect(state, k22_timing_copy(&second, &first, k22_timing_test_signals(&second_observations)),
           "k22_timing_copy(&second, &first, k22_timing_test_signals(&second_observations))");
    k22_timing_advance(&first, k22_timing_test_cycles_for_ticks(&first, 1u, 1000u));
    k22_timing_advance(&second, k22_timing_test_cycles_for_ticks(&second, 1u, 1000u));
    expect(state, k22_timing_set_lptmr_input(&first, 0u, true),
           "k22_timing_set_lptmr_input(&first, 0u, true)");
    expect(state, k22_timing_set_lptmr_input(&second, 0u, true),
           "k22_timing_set_lptmr_input(&second, 0u, true)");
    k22_timing_advance(&first, k22_timing_test_cycles_for_ticks(&first, 2u, 1000u));
    k22_timing_advance(&second, k22_timing_test_cycles_for_ticks(&second, 2u, 1000u));
    expect(state, first.lptmr_counter == 1u, "first.lptmr_counter == 1u");
    expect(state, first.lptmr_counter == second.lptmr_counter,
           "first.lptmr_counter == second.lptmr_counter");
    expect(state, first.lptmr_filter_ticks == second.lptmr_filter_ticks,
           "first.lptmr_filter_ticks == second.lptmr_filter_ticks");
}

static void test_sim_surface(TestState* state, K22Timing* timing) {
    const uint32_t writable[] = {
        0x40047000u, 0x40047004u, 0x40048004u, 0x4004800cu, 0x40048010u, 0x40048018u, 0x4004801cu,
        0x40048030u, 0x40048034u, 0x40048038u, 0x4004803cu, 0x40048040u, 0x40048044u, 0x40048048u,
    };
    for (size_t index = 0; index < sizeof(writable) / sizeof(writable[0]); index++) {
        k22_timing_test_expect_write(state, timing, writable[index], 4,
                                     0x5a5a0000u + (uint32_t)index);
        expect(state, k22_timing_read(timing, writable[index], 4, &(uint32_t){0}),
               "k22_timing_read(timing, writable[index], 4, &(uint32_t){0})");
    }
    k22_timing_test_expect_read(state, timing, SIM_SOPT7, 4, 5u);
    k22_timing_test_expect_read(state, timing, 0x4004804cu, 4, 0xff0f0f00u);
    k22_timing_test_expect_read(state, timing, 0x40048050u, 4, 0x7f7f0000u);
    expect(state, !k22_timing_read(timing, 0x40048028u, 4, &(uint32_t){0}),
           "!k22_timing_read(timing, 0x40048028u, 4, &(uint32_t){0})");
    expect(state, !k22_timing_write(timing, 0x40048028u, 4, 0),
           "!k22_timing_write(timing, 0x40048028u, 4, 0)");
}

static void test_control_surface(TestState* state, K22Timing* timing, Observations* observations) {
    const uint8_t mcg_offsets[] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 11, 12, 13};
    for (size_t index = 0; index < sizeof(mcg_offsets); index++) {
        const uint32_t address = MCG_C1 + mcg_offsets[index];
        expect(state, k22_timing_read(timing, address, 1, &(uint32_t){0}),
               "k22_timing_read(timing, address, 1, &(uint32_t){0})");
        k22_timing_test_expect_write(state, timing, address, 1, (uint32_t)index);
    }
    k22_timing_test_expect_write(state, timing, MCG_C1 + 3u, 1, 0xe0u);
    k22_timing_test_expect_write(state, timing, MCG_C1 + 1u, 1, 0);
    k22_timing_test_expect_write(state, timing, MCG_C1, 1, 0x40u);
    expect(state, k22_timing_core_clock_hz(timing) == 32768u,
           "k22_timing_core_clock_hz(timing) == 32768u");
    k22_timing_test_expect_write(state, timing, MCG_C1 + 1u, 1, 1u);
    expect(state, k22_timing_core_clock_hz(timing) == 4000000u,
           "k22_timing_core_clock_hz(timing) == 4000000u");
    k22_timing_test_expect_write(state, timing, 0x40065000u, 1, 0x80u);
    k22_timing_test_expect_write(state, timing, 0x40065002u, 1, 3u);
    k22_timing_test_expect_read(state, timing, 0x40065000u, 1, 0x80u);
    k22_timing_test_expect_read(state, timing, 0x40065002u, 1, 3u);
    for (uint32_t offset = 0; offset < 10u; offset++) {
        if (offset != 7u)
            k22_timing_test_expect_write(state, timing, 0x4007c000u + offset, 1, 0x55u);
        expect(state, k22_timing_read(timing, 0x4007c000u + offset, 1, &(uint32_t){0}),
               "k22_timing_read(timing, 0x4007c000u + offset, 1, &(uint32_t){0})");
    }
    timing->llwu[5] = 1u;
    k22_timing_test_expect_write(state, timing, 0x4007c005u, 1, 1u);
    expect(state, !observations->irq[21], "!observations->irq[21]");
    for (uint32_t offset = 0; offset < 3u; offset++) {
        k22_timing_test_expect_write(state, timing, 0x4007d000u + offset, 1, 0xa5u);
        expect(state, k22_timing_read(timing, 0x4007d000u + offset, 1, &(uint32_t){0}),
               "k22_timing_read(timing, 0x4007d000u + offset, 1, &(uint32_t){0})");
    }
    k22_timing_test_expect_write(state, timing, SMC_PMPROT, 1, 0x20u);
    k22_timing_test_expect_write(state, timing, SMC_PMCTRL, 1, 0x40u);
    k22_timing_test_expect_read(state, timing, SMC_PMSTAT, 1, 4u);
    k22_timing_test_expect_write(state, timing, 0x4007e002u, 1, 0xa5u);
    k22_timing_test_expect_read(state, timing, 0x4007e002u, 1, 0xa5u);
    k22_timing_test_expect_write(state, timing, 0x4007f004u, 1, 0x5au);
    k22_timing_test_expect_write(state, timing, 0x4007f005u, 1, 0xa5u);
    k22_timing_test_expect_read(state, timing, 0x4007f004u, 1, 0x5au);
    k22_timing_test_expect_read(state, timing, 0x4007f005u, 1, 0xa5u);
    expect(state, !k22_timing_write(timing, RCM_SRS0, 1, 0),
           "!k22_timing_write(timing, RCM_SRS0, 1, 0)");
}

static void test_timer_register_surface(TestState* state, K22Timing* timing) {
    const uint32_t pit_registers[] = {PIT_LDVAL0, PIT_CVAL0, PIT_TCTRL0, PIT_TFLG0};
    for (size_t index = 0; index < sizeof(pit_registers) / sizeof(pit_registers[0]); index++)
        expect(state, k22_timing_read(timing, pit_registers[index], 4, &(uint32_t){0}),
               "k22_timing_read(timing, pit_registers[index], 4, &(uint32_t){0})");
    k22_timing_test_expect_read(state, timing, LPTMR_PSR, 4, timing->lptmr_psr);
    k22_timing_test_expect_read(state, timing, LPTMR_CMR, 4, timing->lptmr_cmr);
    k22_timing_test_expect_write(state, timing, LPTMR_CSR, 4, 0);
    const uint32_t rtc_registers[] = {
        RTC_TAR, 0x4003d00cu, 0x4003d010u, RTC_SR, 0x4003d018u, RTC_IER, 0x4003d800u, 0x4003d804u,
    };
    for (size_t index = 0; index < sizeof(rtc_registers) / sizeof(rtc_registers[0]); index++) {
        expect(state, k22_timing_read(timing, rtc_registers[index], 4, &(uint32_t){0}),
               "k22_timing_read(timing, rtc_registers[index], 4, &(uint32_t){0})");
    }
    k22_timing_test_expect_write(state, timing, RTC_TSR, 4, 0u);
    k22_timing_test_expect_write(state, timing, RTC_TPR, 4, 123u);
    k22_timing_test_expect_read(state, timing, RTC_TPR, 4, 123u);
    const uint32_t pdb_offsets[] = {0x10u,  0x14u,  0x18u,  0x1cu,  0x38u,  0x3cu,  0x40u, 0x44u,
                                    0x150u, 0x154u, 0x158u, 0x15cu, 0x190u, 0x194u, 0x198u};
    for (size_t index = 0; index < sizeof(pdb_offsets) / sizeof(pdb_offsets[0]); index++) {
        const uint32_t address = PDB_SC + pdb_offsets[index];
        k22_timing_test_expect_write(state, timing, address, 4, (uint32_t)index);
        k22_timing_test_expect_read(
            state, timing, address, 4,
            pdb_offsets[index] == 0x14u || pdb_offsets[index] == 0x3cu ? 0u : (uint32_t)index);
    }
    k22_timing_test_expect_read(state, timing, PDB_MOD, 4, timing->pdb_mod);
    k22_timing_test_expect_read(state, timing, PDB_IDLY, 4, timing->pdb_idly);
    k22_timing_test_expect_write(state, timing, PDB_CNT, 4, 123u);
    expect(state, !k22_timing_read(timing, PDB_SC + 0x48u, 4, &(uint32_t){0}),
           "!k22_timing_read(timing, PDB_SC + 0x48u, 4, &(uint32_t){0})");
}

static void test_ftm_surface(TestState* state, K22Timing* timing) {
    const uint32_t offsets[] = {4u, 8u, 0x0cu, 0x10u, 0x4cu, 0x50u, 0x54u, 0x98u};
    for (size_t index = 0; index < sizeof(offsets) / sizeof(offsets[0]); index++) {
        const uint32_t address = FTM0_SC + offsets[index];
        k22_timing_test_expect_write(state, timing, address, 4, 0x1234u + (uint32_t)index);
        expect(state, k22_timing_read(timing, address, 4, &(uint32_t){0}),
               "k22_timing_read(timing, address, 4, &(uint32_t){0})");
    }
    k22_timing_test_expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 | (1u << 24u));
    k22_timing_test_expect_write(state, timing, FTM0_MOD, 4, 7u);
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0x10u);
    k22_timing_advance(timing, timing->core_clock_hz / 32768u + 1u);
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0x18u);
    k22_timing_advance(timing, timing->core_clock_hz / 8000000u + 1u);
}

static void test_watchdog_surface(TestState* state, K22Timing* timing, Observations* observations) {
    unlock_watchdog(state, timing);
    for (uint32_t offset = 0; offset < 0x18u; offset += 2u) {
        if (offset != 0x0cu && offset != 0x0eu && offset != 0x10u && offset != 0x12u)
            k22_timing_test_expect_write(state, timing, WDOG_STCTRLH + offset, 2, offset);
        expect(state, k22_timing_read(timing, WDOG_STCTRLH + offset, 2, &(uint32_t){0}),
               "k22_timing_read(timing, WDOG_STCTRLH + offset, 2, &(uint32_t){0})");
    }
    k22_timing_test_expect_write(state, timing, WDOG_UNLOCK, 2, 0);
    k22_timing_test_expect_write(state, timing, WDOG_REFRESH, 2, 0);
    k22_timing_test_expect_write(state, timing, EWM_CMPL, 1, 1u);
    k22_timing_test_expect_write(state, timing, EWM_CMPH, 1, 4u);
    k22_timing_test_expect_write(state, timing, EWM_CTRL + 5u, 1, 2u);
    for (uint32_t offset = 0; offset < 6u; offset++) {
        if (offset != 4u)
            expect(state, k22_timing_read(timing, EWM_CTRL + offset, 1, &(uint32_t){0}),
                   "k22_timing_read(timing, EWM_CTRL + offset, 1, &(uint32_t){0})");
    }
    k22_timing_test_expect_write(state, timing, EWM_SERV, 1, 0);
    expect(state, observations->resets == 0u, "observations->resets == 0u");
    expect(state, k22_timing_ewm_output(timing), "k22_timing_ewm_output(timing)");
}

static void test_register_surface(TestState* state, const K22Profile* profile) {
    K22Timing timing;
    Observations observations = {0};
    expect(
        state,
        k22_timing_init(&timing, profile, 8000000u, 32768u, k22_timing_test_signals(&observations)),
        "k22_timing_init(&timing, profile, 8000000u, 32768u, "
        "k22_timing_test_signals(&observations))");
    k22_timing_test_disable_watchdog_fixture(&timing);
    test_sim_surface(state, &timing);
    k22_timing_reset(&timing, 0x82u, 0);
    k22_timing_test_disable_watchdog_fixture(&timing);
    test_control_surface(state, &timing, &observations);
    k22_timing_reset(&timing, 0x82u, 0);
    k22_timing_test_disable_watchdog_fixture(&timing);
    test_timer_register_surface(state, &timing);
    k22_timing_reset(&timing, 0x82u, 0);
    k22_timing_test_disable_watchdog_fixture(&timing);
    test_ftm_surface(state, &timing);
    k22_timing_reset(&timing, 0x82u, 0);
    test_watchdog_surface(state, &timing, &observations);
}

static void test_edge_paths(TestState* state, const K22Profile* profile) {
    K22Timing timing;
    Observations observations = {0};
    expect(
        state,
        k22_timing_init(&timing, profile, 8000000u, 32768u, k22_timing_test_signals(&observations)),
        "k22_timing_init(&timing, profile, 8000000u, 32768u, "
        "k22_timing_test_signals(&observations))");
    k22_timing_test_disable_watchdog_fixture(&timing);
    timing.external_oscillator_hz = 0u;
    k22_timing_test_expect_write(state, &timing, MCG_C1, 1, 0x80u);
    expect(state, k22_timing_core_clock_hz(&timing) == timing.slow_irc_hz,
           "k22_timing_core_clock_hz(&timing) == timing.slow_irc_hz");
    timing.external_oscillator_hz = 8000000u;
    k22_timing_test_expect_write(state, &timing, MCG_C1 + 1u, 1, 4u);
    k22_timing_test_expect_write(state, &timing, MCG_C1, 1, 0x80u);
    k22_timing_test_expect_read(state, &timing, MCG_S, 1, 0x0au);
    k22_timing_test_expect_write(state, &timing, MCG_C1 + 3u, 1, 0x80u);
    k22_timing_test_expect_write(state, &timing, MCG_C1, 1, 4u);
    k22_timing_test_expect_write(state, &timing, SIM_SCGC5, 4, timing.sim_scgc5 | 1u);
    for (uint32_t source = 0; source < 4u; source++) {
        k22_timing_test_expect_write(state, &timing, LPTMR_CSR, 4, 0);
        k22_timing_test_expect_write(state, &timing, LPTMR_PSR, 4,
                                     source | (source == 0 ? 0u : 4u));
        k22_timing_test_expect_write(state, &timing, LPTMR_CMR, 4, 0xffffu);
        k22_timing_test_expect_write(state, &timing, LPTMR_CSR, 4, 1u);
        k22_timing_advance(&timing, timing.core_clock_hz);
    }
    k22_timing_test_expect_write(state, &timing, LPTMR_CSR, 4, 0u);
    k22_timing_test_expect_write(state, &timing, SIM_SCGC6, 4,
                                 timing.sim_scgc6 | (1u << 29u) | (1u << 22u));
    k22_timing_test_expect_write(state, &timing, RTC_TSR, 4, UINT32_MAX);
    k22_timing_test_expect_write(state, &timing, RTC_IER, 4, 2u);
    k22_timing_test_expect_write(state, &timing, RTC_CR, 4, 0x100u);
    k22_timing_test_expect_write(state, &timing, RTC_SR, 4, 0x10u);
    const uint32_t alternate_before = observations.alternate_triggers;
    k22_timing_advance(&timing, timing.core_clock_hz);
    expect(state, observations.irq[46], "observations.irq[46]");
    expect(state, observations.alternate_triggers == alternate_before + 2u,
           "observations.alternate_triggers == alternate_before + 2u");
    expect(state, observations.last_trigger_instance == 12u,
           "observations.last_trigger_instance == 12u");
    k22_timing_test_expect_write(state, &timing, PDB_MOD, 4, 1u);
    k22_timing_test_expect_write(state, &timing, PDB_SC, 4, 1u);
    k22_timing_advance(&timing, 2u);
    k22_timing_test_expect_read(state, &timing, PDB_SC, 4, 0u);
    k22_timing_reset(&timing, 0x82u, 0);
    memset(&observations, 0, sizeof(observations));
    unlock_watchdog(state, &timing);
    k22_timing_test_expect_write(state, &timing, WDOG_TOVALH, 2, 0u);
    k22_timing_test_expect_write(state, &timing, WDOG_TOVALL, 2, 10u);
    k22_timing_test_expect_write(state, &timing, WDOG_WINH, 2, 0u);
    k22_timing_test_expect_write(state, &timing, WDOG_WINL, 2, 2u);
    k22_timing_test_expect_write(state, &timing, WDOG_STCTRLH, 2, 9u);
    k22_timing_advance(&timing,
                       k22_timing_test_cycles_for_ticks(&timing, 260u, timing.bus_clock_hz));
    k22_timing_test_expect_write(state, &timing, WDOG_REFRESH, 2, 0xa602u);
    k22_timing_test_expect_write(state, &timing, WDOG_REFRESH, 2, 0xb480u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");
    k22_timing_reset(&timing, 0x82u, 0);
    unlock_watchdog(state, &timing);
    k22_timing_test_expect_write(state, &timing, WDOG_TOVALH, 2, 0);
    k22_timing_test_expect_write(state, &timing, WDOG_TOVALL, 2, 1u);
    k22_timing_test_expect_write(state, &timing, WDOG_PRESC, 2, 0);
    k22_timing_test_expect_write(state, &timing, WDOG_STCTRLH, 2, 5u);
    k22_timing_advance(&timing,
                       k22_timing_test_cycles_for_ticks(&timing, 260u, timing.bus_clock_hz));
    k22_timing_advance(&timing, k22_timing_test_cycles_for_ticks(&timing, 1u, 1000u));
    expect(state, observations.irq[22], "observations.irq[22]");
    k22_timing_reset(&timing, 0x82u, 0);
    expect(state, !k22_timing_read(&timing, PIT_MCR, 1, &(uint32_t){0}),
           "!k22_timing_read(&timing, PIT_MCR, 1, &(uint32_t){0})");
    expect(state, !k22_timing_write(&timing, PIT_MCR, 1, 0),
           "!k22_timing_write(&timing, PIT_MCR, 1, 0)");
    expect(state, !k22_timing_read(&timing, PIT_LDVAL0 + 2u, 4, &(uint32_t){0}),
           "!k22_timing_read(&timing, PIT_LDVAL0 + 2u, 4, &(uint32_t){0})");
    expect(state, !k22_timing_write(&timing, PIT_LDVAL0 + 2u, 4, 0),
           "!k22_timing_write(&timing, PIT_LDVAL0 + 2u, 4, 0)");
    expect(state, !k22_timing_read(&timing, LPTMR_CSR + 2u, 4, &(uint32_t){0}),
           "!k22_timing_read(&timing, LPTMR_CSR + 2u, 4, &(uint32_t){0})");
    expect(state, !k22_timing_write(&timing, LPTMR_CSR + 2u, 4, 0),
           "!k22_timing_write(&timing, LPTMR_CSR + 2u, 4, 0)");
    expect(state, !k22_timing_read(&timing, RTC_SR + 0x0cu, 4, &(uint32_t){0}),
           "!k22_timing_read(&timing, RTC_SR + 0x0cu, 4, &(uint32_t){0})");
    expect(state, !k22_timing_write(&timing, RTC_SR + 0x0cu, 4, 0),
           "!k22_timing_write(&timing, RTC_SR + 0x0cu, 4, 0)");
    expect(state, !k22_timing_read(&timing, FTM0_SC, 2, &(uint32_t){0}),
           "!k22_timing_read(&timing, FTM0_SC, 2, &(uint32_t){0})");
    expect(state, !k22_timing_write(&timing, FTM0_SC, 2, 0),
           "!k22_timing_write(&timing, FTM0_SC, 2, 0)");
    expect(state, !k22_timing_read(&timing, FTM0_SC + 0x9cu, 4, &(uint32_t){0}),
           "!k22_timing_read(&timing, FTM0_SC + 0x9cu, 4, &(uint32_t){0})");
    expect(state, !k22_timing_read(&timing, WDOG_STCTRLH + 1u, 2, &(uint32_t){0}),
           "!k22_timing_read(&timing, WDOG_STCTRLH + 1u, 2, &(uint32_t){0})");
    expect(state, !k22_timing_write(&timing, WDOG_STCTRLH + 1u, 2, 0),
           "!k22_timing_write(&timing, WDOG_STCTRLH + 1u, 2, 0)");
    expect(state, !k22_timing_read(&timing, EWM_CTRL + 4u, 1, &(uint32_t){0}),
           "!k22_timing_read(&timing, EWM_CTRL + 4u, 1, &(uint32_t){0})");
    expect(state, !k22_timing_write(&timing, EWM_CTRL + 4u, 1, 0),
           "!k22_timing_write(&timing, EWM_CTRL + 4u, 1, 0)");
    expect(state, !k22_timing_read(NULL, 0, 1, &(uint32_t){0}),
           "!k22_timing_read(NULL, 0, 1, &(uint32_t){0})");
    expect(state, !k22_timing_write(NULL, 0, 1, 0), "!k22_timing_write(NULL, 0, 1, 0)");
    k22_timing_advance(NULL, 1u);
    k22_timing_reset(NULL, 0, 0);
    k22_timing_set_debug_halted(NULL, true);
}

int main(void) {
    TestState state = {0};
    uint64_t remainder = 0u;
    expect(&state,
           k22_timing_internal_clock_ticks(&remainder, 1u, 0u, 1u) == 0u &&
               k22_timing_internal_clock_ticks(&remainder, 1u, 1u, 0u) == 0u,
           "clock conversion rejects stopped clocks");
    Observations observations = {0};
    K22Timing timing;
    const K22Profile* profile = k22_profile_get(K22_PROFILE_MK22FN51212);
    k22_timing_test_test_profiles_and_reset(&state);
    expect(
        &state,
        k22_timing_init(&timing, profile, 8000000u, 32768u, k22_timing_test_signals(&observations)),
        "k22_timing_init(&timing, profile, 8000000u, 32768u, "
        "k22_timing_test_signals(&observations))");
    k22_timing_test_test_clock_tree_and_power(&state, &timing);
    k22_timing_reset(&timing, 0x82u, 0);
    memset(&observations, 0, sizeof(observations));
    k22_timing_test_test_low_voltage_control(&state, &timing, &observations);
    k22_timing_reset(&timing, 0x82u, 0);
    memset(&observations, 0, sizeof(observations));
    k22_timing_test_test_low_leakage_wakeup(&state, &timing, &observations);
    k22_timing_reset(&timing, 0x82u, 0);
    memset(&observations, 0, sizeof(observations));
    k22_timing_test_disable_watchdog_fixture(&timing);
    k22_timing_test_test_pit(&state, &timing, &observations);
    k22_timing_test_test_lptmr(&state, &timing, &observations);
    k22_timing_test_test_rtc(&state, &timing, &observations);
    k22_timing_test_test_rtc_protection_and_compensation(&state, profile);
    k22_timing_test_test_pdb(&state, &timing, &observations);
    k22_timing_test_test_ftm(&state, &timing, &observations);
    k22_timing_test_test_ftm_input_capture(&state, profile);
    k22_timing_test_test_ftm_output(&state, profile);
    k22_timing_reset(&timing, 0x82u, 0);
    memset(&observations, 0, sizeof(observations));
    test_watchdogs(&state, &timing, &observations);
    test_copy_and_split_advance(&state, profile);
    expect(&state, !k22_timing_read(&timing, SIM_SCGC3, 4, &(uint32_t){0}),
           "!k22_timing_read(&timing, SIM_SCGC3, 4, &(uint32_t){0})");
    test_register_surface(&state, k22_profile_get(K22_PROFILE_MK22FN1M012));
    test_edge_paths(&state, profile);
    expect(&state, !k22_timing_init(NULL, profile, 0, 0, k22_timing_test_signals(NULL)),
           "!k22_timing_init(NULL, profile, 0, 0, k22_timing_test_signals(NULL))");
    expect(&state, !k22_timing_copy(NULL, &timing, k22_timing_test_signals(NULL)),
           "!k22_timing_copy(NULL, &timing, k22_timing_test_signals(NULL))");
    expect(&state, k22_timing_core_clock_hz(NULL) == 0, "k22_timing_core_clock_hz(NULL) == 0");
    return test_finish(&state);
}
