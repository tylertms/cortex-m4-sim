#include "device/kinetis_k22/timing/support.h"

void k22_timing_test_test_pdb(TestState* state, K22Timing* timing, Observations* observations) {
    k22_timing_test_expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 | (1u << 22u));
    k22_timing_test_expect_write(state, timing, PDB_MOD, 4, 4u);
    k22_timing_test_expect_write(state, timing, PDB_IDLY, 4, 2u);
    k22_timing_test_expect_write(state, timing, PDB_SC + 0x10u, 4, 3u);
    k22_timing_test_expect_write(state, timing, PDB_SC + 0x18u, 4, 1u);
    k22_timing_test_expect_write(state, timing, PDB_SC + 0x1cu, 4, 2u);
    k22_timing_test_expect_write(state, timing, PDB_SC + 0x150u, 4, 2u);
    k22_timing_test_expect_write(state, timing, PDB_SC + 0x154u, 4, 1u);
    k22_timing_test_expect_write(state, timing, PDB_SC, 4, 0x23u);
    k22_timing_advance(timing, 2u);
    k22_timing_test_expect_read(state, timing, PDB_CNT, 4, 2u);
    k22_timing_test_expect_read(state, timing, PDB_SC, 4, 0x63u);
    k22_timing_test_expect_read(state, timing, PDB_SC + 0x14u, 4, 3u);
    expect(state, observations->adc_triggers == 2u, "observations->adc_triggers == 2u");
    expect(state, observations->dac_triggers == 1u, "observations->dac_triggers == 1u");
    expect(state, observations->last_trigger_instance == 0u,
           "observations->last_trigger_instance == 0u");
    expect(state, observations->last_trigger_channel == 0u,
           "observations->last_trigger_channel == 0u");
    k22_timing_test_expect_write(state, timing, PDB_SC + 0x14u, 4, 3u);
    k22_timing_test_expect_read(state, timing, PDB_SC + 0x14u, 4, 0u);
    expect(state, observations->irq[52], "observations->irq[52]");
    k22_timing_test_expect_write(state, timing, PDB_SC, 4, 0x23u);
    expect(state, !observations->irq[52], "!observations->irq[52]");
}

void k22_timing_test_test_ftm(TestState* state, K22Timing* timing, Observations* observations) {
    k22_timing_test_expect_write(state, timing, SIM_SCGC6, 4, timing->sim_scgc6 | (1u << 24u));
    k22_timing_test_expect_write(state, timing, FTM0_MOD, 4, 3u);
    k22_timing_test_expect_write(state, timing, FTM0_C0V, 4, 2u);
    k22_timing_test_expect_write(state, timing, FTM0_C0SC, 4, 0x51u);
    k22_timing_test_expect_write(state, timing, FTM0_EXTTRIG, 4, 0x10u);
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0x48u);
    k22_timing_advance(timing, 4u);
    k22_timing_test_expect_read(state, timing, FTM0_CNT, 4, 0u);
    k22_timing_test_expect_read(state, timing, FTM0_SC, 4, 0xc8u);
    expect(state, observations->irq[42], "observations->irq[42]");
    expect(state, observations->dma_requests != 0, "observations->dma_requests != 0");
    expect(state, observations->last_dma == 20u, "observations->last_dma == 20u");
    expect(state, observations->last_trigger_instance == 8u,
           "observations->last_trigger_instance == 8u");
    k22_timing_test_expect_read(state, timing, FTM0_EXTTRIG, 4, 0x90u);
    k22_timing_test_expect_write(state, timing, FTM0_EXTTRIG, 4, 0x90u);
    k22_timing_test_expect_write(state, timing, FTM0_EXTTRIG, 4, 0x10u);
    k22_timing_test_expect_read(state, timing, FTM0_EXTTRIG, 4, 0x90u);
    k22_timing_test_expect_write(state, timing, FTM0_EXTTRIG, 4, 0x10u);
    k22_timing_test_expect_read(state, timing, FTM0_EXTTRIG, 4, 0x10u);
    const uint32_t alternate_before = observations->alternate_triggers;
    k22_timing_test_expect_write(state, timing, FTM0_EXTTRIG, 4, 0x40u);
    k22_timing_advance(timing, 4u);
    expect(state, observations->alternate_triggers == alternate_before + 1u,
           "observations->alternate_triggers == alternate_before + 1u");
    k22_timing_test_expect_read(state, timing, FTM0_EXTTRIG, 4, 0xc0u);
    k22_timing_test_expect_write(state, timing, FTM0_EXTTRIG, 4, 0x40u);
    k22_timing_test_expect_read(state, timing, FTM0_SC, 4, 0xc8u);
    k22_timing_test_expect_read(state, timing, FTM0_C0SC, 4, 0xd1u);
    k22_timing_test_expect_write(state, timing, FTM0_C0SC, 4, 0x51u);
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0x48u);
    expect(state, !observations->irq[42], "!observations->irq[42]");

    k22_timing_test_expect_write(state, timing, FTM0_C0V, 4, 1u);
    k22_timing_test_expect_write(state, timing, FTM0_C1V, 4, 2u);
    k22_timing_test_expect_write(state, timing, FTM0_C0SC, 4, 0x50u);
    k22_timing_test_expect_write(state, timing, FTM0_C1SC, 4, 0x50u);
    k22_timing_advance(timing, 4u);
    k22_timing_test_expect_read(state, timing, FTM0_STATUS, 4, 3u);
    expect(state, observations->irq[42], "observations->irq[42]");
    k22_timing_test_expect_read(state, timing, FTM0_C0SC, 4, 0xd0u);
    k22_timing_test_expect_write(state, timing, FTM0_C0SC, 4, 0x50u);
    expect(state, observations->irq[42], "observations->irq[42]");
    k22_timing_test_expect_read(state, timing, FTM0_STATUS, 4, 2u);
    k22_timing_test_expect_write(state, timing, FTM0_STATUS, 4, 0u);
    expect(state, observations->irq[42], "observations->irq[42]");
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0x48u);
    expect(state, observations->irq[42], "observations->irq[42]");
    k22_timing_test_expect_read(state, timing, FTM0_SC, 4, 0xc8u);
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0x48u);
    expect(state, !observations->irq[42], "!observations->irq[42]");

    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0u);
    k22_timing_test_expect_write(state, timing, FTM0_MOD, 4, 100u);
    k22_timing_test_expect_write(state, timing, FTM0_CNT, 4, 0u);
    k22_timing_test_expect_write(state, timing, FTM0_C0V, 4, 1u);
    k22_timing_test_expect_write(state, timing, FTM0_C0SC, 4, 0x50u);
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0x08u);
    k22_timing_advance(timing, 1u);
    expect(state, observations->irq[42], "observations->irq[42]");
    k22_timing_test_expect_write(state, timing, FTM0_C0SC, 4, 0x50u);
    expect(state, observations->irq[42], "observations->irq[42]");
    k22_timing_test_expect_read(state, timing, FTM0_C0SC, 4, 0xd0u);
    k22_timing_test_expect_write(state, timing, FTM0_C0SC, 4, 0x50u);
    expect(state, !observations->irq[42], "!observations->irq[42]");

    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0u);
    k22_timing_test_expect_write(state, timing, FTM0_SC + 0x4cu, 4, 1u);
    k22_timing_test_expect_write(state, timing, FTM0_MOD, 4, 4u);
    const uint32_t center_write_trigger_before = observations->alternate_triggers;
    k22_timing_test_expect_write(state, timing, FTM0_CNT, 4, 0xaaaau);
    expect(state, observations->alternate_triggers == center_write_trigger_before + 1u,
           "observations->alternate_triggers == center_write_trigger_before + 1u");
    k22_timing_test_expect_read(state, timing, FTM0_CNT, 4, 1u);
    k22_timing_test_expect_write(state, timing, FTM0_C1SC, 4, 0u);
    k22_timing_test_expect_write(state, timing, FTM0_C1V, 4, 2u);
    k22_timing_test_expect_write(state, timing, FTM0_C1V + 8u, 4, 1u);
    k22_timing_test_expect_write(state, timing, FTM0_C0V, 4, 2u);
    k22_timing_test_expect_write(state, timing, FTM0_C0SC, 4, 0x68u);
    const uint32_t center_start_trigger_before = observations->alternate_triggers;
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0x68u);
    expect(state, observations->alternate_triggers == center_start_trigger_before + 1u,
           "observations->alternate_triggers == center_start_trigger_before + 1u");
    const uint32_t center_cycle_trigger_before = observations->alternate_triggers;
    k22_timing_advance(timing, 1u);
    k22_timing_test_expect_read(state, timing, FTM0_CNT, 4, 2u);
    k22_timing_test_expect_read(state, timing, FTM0_STATUS, 4, 3u);
    k22_timing_test_expect_read(state, timing, FTM0_C0SC, 4, 0xe8u);
    k22_timing_test_expect_write(state, timing, FTM0_C0SC, 4, 0x68u);
    k22_timing_test_expect_read(state, timing, FTM0_C1SC, 4, 0x80u);
    k22_timing_test_expect_write(state, timing, FTM0_C1SC, 4, 0u);
    k22_timing_advance(timing, 2u);
    k22_timing_test_expect_read(state, timing, FTM0_CNT, 4, 4u);
    k22_timing_test_expect_read(state, timing, FTM0_SC, 4, 0x68u);
    k22_timing_advance(timing, 1u);
    k22_timing_test_expect_read(state, timing, FTM0_CNT, 4, 3u);
    k22_timing_test_expect_read(state, timing, FTM0_SC, 4, 0xe8u);
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0x68u);
    expect(state, observations->alternate_triggers == center_cycle_trigger_before,
           "observations->alternate_triggers == center_cycle_trigger_before");
    k22_timing_advance(timing, 1u);
    k22_timing_test_expect_read(state, timing, FTM0_CNT, 4, 2u);
    k22_timing_test_expect_read(state, timing, FTM0_C0SC, 4, 0xe8u);
    k22_timing_test_expect_write(state, timing, FTM0_C0SC, 4, 0x68u);
    k22_timing_test_expect_read(state, timing, FTM0_C1SC, 4, 0x80u);
    k22_timing_test_expect_write(state, timing, FTM0_C1SC, 4, 0u);
    k22_timing_advance(timing, 1u);
    k22_timing_test_expect_read(state, timing, FTM0_CNT, 4, 1u);
    k22_timing_test_expect_read(state, timing, FTM0_STATUS, 4, 0u);
    expect(state, observations->alternate_triggers == center_cycle_trigger_before + 1u,
           "observations->alternate_triggers == center_cycle_trigger_before + 1u");
    expect(state, !observations->irq[42], "!observations->irq[42]");
    k22_timing_set_debug_halted(timing, true);
    k22_timing_advance(timing, 3u);
    k22_timing_test_expect_read(state, timing, FTM0_CNT, 4, 1u);
    k22_timing_set_debug_halted(timing, false);
    const uint32_t center_dma_before = observations->dma_requests;
    const uint32_t center_trigger_before = observations->alternate_triggers;
    k22_timing_test_expect_write(state, timing, FTM0_EXTTRIG, 4, 0x10u);
    k22_timing_test_expect_write(state, timing, FTM0_C0SC, 4, 0x69u);
    k22_timing_advance(timing, 13u);
    k22_timing_test_expect_read(state, timing, FTM0_CNT, 4, 2u);
    expect(state, observations->dma_requests == center_dma_before + 1u,
           "observations->dma_requests == center_dma_before + 1u");
    expect(state, observations->alternate_triggers == center_trigger_before + 1u,
           "observations->alternate_triggers == center_trigger_before + 1u");
    k22_timing_test_expect_read(state, timing, FTM0_C0SC, 4, 0xe9u);
    k22_timing_test_expect_write(state, timing, FTM0_C0SC, 4, 0u);
    k22_timing_test_expect_read(state, timing, FTM0_SC, 4, 0xe8u);
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0u);
    k22_timing_test_expect_write(state, timing, FTM0_EXTTRIG, 4, 0x40u);
    k22_timing_test_expect_write(state, timing, FTM0_SC + 0x4cu, 4, 3u);
    k22_timing_test_expect_write(state, timing, FTM0_MOD, 4, 3u);
    const uint32_t redundant_write_trigger_before = observations->alternate_triggers;
    k22_timing_test_expect_write(state, timing, FTM0_CNT, 4, 0u);
    expect(state, observations->alternate_triggers == redundant_write_trigger_before + 1u,
           "observations->alternate_triggers == redundant_write_trigger_before + 1u");
    const uint32_t redundant_start_trigger_before = observations->alternate_triggers;
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0x68u);
    expect(state, observations->alternate_triggers == redundant_start_trigger_before + 1u,
           "observations->alternate_triggers == redundant_start_trigger_before + 1u");
    const uint32_t redundant_trigger_before = observations->alternate_triggers;
    k22_timing_advance(timing, 1u);
    expect(state, observations->alternate_triggers == redundant_trigger_before + 1u,
           "observations->alternate_triggers == redundant_trigger_before + 1u");
    k22_timing_test_expect_read(state, timing, FTM0_CNT, 4, 3u);
    k22_timing_test_expect_read(state, timing, FTM0_SC, 4, 0xe8u);
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0u);
    k22_timing_test_expect_write(state, timing, FTM0_EXTTRIG, 4, 0u);
    k22_timing_test_expect_write(state, timing, FTM0_SC + 0x4cu, 4, 0u);
    k22_timing_test_expect_write(state, timing, FTM0_MOD, 4, 1u);
    k22_timing_test_expect_write(state, timing, FTM0_CONF, 4, 2u);
    k22_timing_test_expect_write(state, timing, FTM0_EXTTRIG, 4, 0x40u);
    k22_timing_test_expect_write(state, timing, FTM0_CNT, 4, 0u);
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0x48u);
    const uint32_t periodic_trigger_before = observations->alternate_triggers;
    k22_timing_advance(timing, 2u);
    expect(state, observations->alternate_triggers == periodic_trigger_before + 1u,
           "observations->alternate_triggers == periodic_trigger_before + 1u");
    k22_timing_test_expect_read(state, timing, FTM0_SC, 4, 0xc8u);
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0x48u);
    const uint32_t periodic_skip_trigger_before = observations->alternate_triggers;
    k22_timing_advance(timing, 4u);
    expect(state, observations->alternate_triggers == periodic_skip_trigger_before + 1u,
           "observations->alternate_triggers == periodic_skip_trigger_before + 1u");
    k22_timing_test_expect_read(state, timing, FTM0_SC, 4, 0x48u);
    const uint32_t periodic_resume_trigger_before = observations->alternate_triggers;
    k22_timing_advance(timing, 2u);
    expect(state, observations->alternate_triggers == periodic_resume_trigger_before + 1u,
           "observations->alternate_triggers == periodic_resume_trigger_before + 1u");
    k22_timing_test_expect_read(state, timing, FTM0_SC, 4, 0xc8u);
    k22_timing_test_expect_write(state, timing, FTM0_SC, 4, 0x48u);
    k22_timing_test_expect_write(state, timing, FTM0_CNT, 4, 0xffffu);
    k22_timing_advance(timing, 2u);
    k22_timing_test_expect_read(state, timing, FTM0_SC, 4, 0xc8u);
    k22_timing_test_expect_write(state, timing, FTM1_EXTTRIG, 4, UINT32_MAX);
    k22_timing_test_expect_read(state, timing, FTM1_EXTTRIG, 4, 0x70u);
}

void k22_timing_test_test_ftm_input_capture(TestState* state, const K22Profile* profile) {
    K22Timing timing;
    Observations observations = {0};
    expect(
        state,
        k22_timing_init(&timing, profile, 8000000u, 32768u, k22_timing_test_signals(&observations)),
        "k22_timing_init(&timing, profile, 8000000u, 32768u, "
        "k22_timing_test_signals(&observations))");
    k22_timing_test_disable_watchdog_fixture(&timing);
    k22_timing_test_expect_write(state, &timing, SIM_SCGC6, 4, timing.sim_scgc6 | (1u << 24u));
    k22_timing_test_expect_write(state, &timing, FTM0_CNTIN, 4, 5u);
    k22_timing_test_expect_write(state, &timing, FTM0_MOD, 4, 100u);
    k22_timing_test_expect_write(state, &timing, FTM0_EXTTRIG, 4, 0x40u);
    k22_timing_test_expect_write(state, &timing, FTM0_CNT, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0SC, 4, 0x47u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0V, 4, 0x1234u);
    k22_timing_test_expect_read(state, &timing, FTM0_C0V, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_SC, 4, 8u);
    expect(state, k22_timing_set_ftm_input(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_input(&timing, 0u, 0u, true)");
    const uint32_t first_dma_before = observations.dma_requests;
    const uint32_t first_trigger_before = observations.alternate_triggers;
    k22_timing_advance(&timing, 2u);
    k22_timing_test_expect_read(state, &timing, FTM0_C0V, 4, 0u);
    k22_timing_set_debug_halted(&timing, true);
    k22_timing_advance(&timing, 1u);
    k22_timing_test_expect_read(state, &timing, FTM0_C0V, 4, 7u);
    k22_timing_test_expect_read(state, &timing, FTM0_CNT, 4, 5u);
    expect(state, observations.irq[42], "observations.irq[42]");
    expect(state, observations.dma_requests == first_dma_before + 1u,
           "observations.dma_requests == first_dma_before + 1u");
    expect(state, observations.alternate_triggers == first_trigger_before + 1u,
           "observations.alternate_triggers == first_trigger_before + 1u");
    k22_timing_set_debug_halted(&timing, false);
    k22_timing_test_expect_read(state, &timing, FTM0_C0SC, 4, 0xc7u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0SC, 4, 0x47u);
    expect(state, k22_timing_set_ftm_input(&timing, 0u, 0u, false),
           "k22_timing_set_ftm_input(&timing, 0u, 0u, false)");
    k22_timing_advance(&timing, 3u);
    k22_timing_test_expect_read(state, &timing, FTM0_C0SC, 4, 0x47u);

    k22_timing_test_expect_write(state, &timing, FTM0_C0SC, 4, 0x4du);
    k22_timing_test_expect_write(state, &timing, FTM0_FILTER, 4, 2u);
    expect(state, k22_timing_set_ftm_input(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_input(&timing, 0u, 0u, true)");
    const uint32_t filtered_dma_before = observations.dma_requests;
    k22_timing_advance(&timing, 11u);
    k22_timing_test_expect_read(state, &timing, FTM0_C0SC, 4, 0x4du);
    k22_timing_advance(&timing, 1u);
    k22_timing_test_expect_read(state, &timing, FTM0_C0V, 4, 20u);
    expect(state, observations.dma_requests == filtered_dma_before + 1u,
           "observations.dma_requests == filtered_dma_before + 1u");
    k22_timing_test_expect_read(state, &timing, FTM0_C0SC, 4, 0xcdu);
    k22_timing_test_expect_write(state, &timing, FTM0_C0SC, 4, 0x4du);
    expect(state, k22_timing_set_ftm_input(&timing, 0u, 0u, false),
           "k22_timing_set_ftm_input(&timing, 0u, 0u, false)");
    k22_timing_advance(&timing, 4u);
    expect(state, k22_timing_set_ftm_input(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_input(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 20u);
    k22_timing_test_expect_read(state, &timing, FTM0_C0SC, 4, 0x4du);
    expect(state, !k22_timing_set_ftm_input(&timing, 1u, 2u, true),
           "!k22_timing_set_ftm_input(&timing, 1u, 2u, true)");
    expect(state, !k22_timing_set_ftm_input(&timing, 4u, 0u, true),
           "!k22_timing_set_ftm_input(&timing, 4u, 0u, true)");
    expect(state, !k22_timing_set_ftm_input(NULL, 0u, 0u, true),
           "!k22_timing_set_ftm_input(NULL, 0u, 0u, true)");
}

static void expect_ftm_output(TestState* state, const K22Timing* timing, bool expected) {
    bool high = !expected;
    expect(state, k22_timing_get_ftm_output(timing, 0u, 0u, &high),
           "k22_timing_get_ftm_output(timing, 0u, 0u, &high)");
    expect(state, high == expected, "high == expected");
}

void k22_timing_test_test_ftm_output(TestState* state, const K22Profile* profile) {
    K22Timing timing;
    Observations observations = {0};
    expect(
        state,
        k22_timing_init(&timing, profile, 8000000u, 32768u, k22_timing_test_signals(&observations)),
        "k22_timing_init(&timing, profile, 8000000u, 32768u, "
        "k22_timing_test_signals(&observations))");
    k22_timing_test_disable_watchdog_fixture(&timing);
    k22_timing_test_expect_write(state, &timing, SIM_SCGC6, 4, timing.sim_scgc6 | (1u << 24u));
    k22_timing_test_expect_write(state, &timing, FTM0_MOD, 4, 5u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0V, 4, 3u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0SC, 4, 0x14u);
    k22_timing_test_expect_write(state, &timing, FTM0_CNT, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_SC, 4, 8u);
    expect_ftm_output(state, &timing, false);
    k22_timing_advance(&timing, 3u);
    expect_ftm_output(state, &timing, true);
    k22_timing_advance(&timing, 6u);
    expect_ftm_output(state, &timing, false);
    k22_timing_test_expect_write(state, &timing, FTM0_C0SC, 4, 0x1cu);
    k22_timing_advance(&timing, 6u);
    expect_ftm_output(state, &timing, true);
    k22_timing_test_expect_write(state, &timing, FTM0_C0SC, 4, 0x18u);
    k22_timing_advance(&timing, 6u);
    expect_ftm_output(state, &timing, false);

    k22_timing_test_expect_write(state, &timing, FTM0_SC, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_OUTINIT, 4, 1u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0SC, 4, 0x28u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0V, 4, 3u);
    k22_timing_test_expect_write(state, &timing, FTM0_CNT, 4, 0u);
    expect_ftm_output(state, &timing, true);
    k22_timing_test_expect_write(state, &timing, FTM0_SC, 4, 8u);
    k22_timing_advance(&timing, 3u);
    expect_ftm_output(state, &timing, false);
    k22_timing_advance(&timing, 3u);
    expect_ftm_output(state, &timing, true);
    k22_timing_test_expect_write(state, &timing, FTM0_C0SC, 4, 0x24u);
    k22_timing_test_expect_write(state, &timing, FTM0_OUTINIT, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_CNT, 4, 0u);
    expect_ftm_output(state, &timing, false);
    k22_timing_advance(&timing, 3u);
    expect_ftm_output(state, &timing, true);
    k22_timing_advance(&timing, 3u);
    expect_ftm_output(state, &timing, false);

    k22_timing_test_expect_read(state, &timing, FTM0_C0SC, 4, 0xa4u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0SC, 4, 0x28u);
    k22_timing_test_expect_write(state, &timing, FTM0_OUTINIT, 4, 1u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0V, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_CNT, 4, 0u);
    k22_timing_advance(&timing, 6u);
    expect_ftm_output(state, &timing, false);
    k22_timing_test_expect_read(state, &timing, FTM0_C0SC, 4, 0xa8u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0SC, 4, 0x28u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0V, 4, 6u);
    k22_timing_advance(&timing, 6u);
    expect_ftm_output(state, &timing, true);
    k22_timing_test_expect_read(state, &timing, FTM0_C0SC, 4, 0x28u);

    k22_timing_test_expect_write(state, &timing, FTM0_SC, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_MOD, 4, 4u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0V, 4, 2u);
    k22_timing_test_expect_write(state, &timing, FTM0_OUTINIT, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0SC, 4, 0x28u);
    k22_timing_test_expect_write(state, &timing, FTM0_CNT, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_SC, 4, 0x28u);
    k22_timing_advance(&timing, 2u);
    expect_ftm_output(state, &timing, false);
    k22_timing_advance(&timing, 4u);
    expect_ftm_output(state, &timing, true);
    k22_timing_advance(&timing, 4u);
    expect_ftm_output(state, &timing, false);

    k22_timing_test_expect_read(state, &timing, FTM0_C0SC, 4, 0xa8u);
    k22_timing_test_expect_write(state, &timing, FTM0_C0SC, 4, 0x14u);
    k22_timing_test_expect_write(state, &timing, FTM0_SC, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_QDCTRL, 4, 1u);
    k22_timing_test_expect_write(state, &timing, FTM0_CNT, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_SC, 4, 8u);
    k22_timing_advance(&timing, 3u);
    expect_ftm_output(state, &timing, true);
    k22_timing_test_expect_read(state, &timing, FTM0_C0SC, 4, 0x94u);

    k22_timing_test_expect_write(state, &timing, FTM0_SC, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_QDCTRL, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_OUTINIT, 4, 1u);
    k22_timing_test_expect_write(state, &timing, FTM0_MODE, 4, 2u);
    k22_timing_test_expect_read(state, &timing, FTM0_MODE, 4, 4u);
    expect_ftm_output(state, &timing, true);
    k22_timing_test_expect_write(state, &timing, FTM0_SWOCTRL, 4, 1u);
    expect_ftm_output(state, &timing, true);
    k22_timing_advance(&timing, 1u);
    expect_ftm_output(state, &timing, false);
    k22_timing_test_expect_write(state, &timing, FTM0_SWOCTRL, 4, 0x101u);
    expect_ftm_output(state, &timing, false);
    k22_timing_advance(&timing, 1u);
    expect_ftm_output(state, &timing, true);
    k22_timing_test_expect_write(state, &timing, FTM0_OUTMASK, 4, 1u);
    expect_ftm_output(state, &timing, true);
    k22_timing_advance(&timing, 1u);
    expect_ftm_output(state, &timing, false);
    k22_timing_test_expect_write(state, &timing, FTM0_OUTMASK, 4, 0u);
    expect_ftm_output(state, &timing, false);
    k22_timing_advance(&timing, 1u);
    expect_ftm_output(state, &timing, true);
    k22_timing_test_expect_write(state, &timing, FTM0_SWOCTRL, 4, 0u);
    k22_timing_advance(&timing, 1u);
    k22_timing_test_expect_write(state, &timing, FTM0_OUTINIT, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_MODE, 4, 2u);
    expect_ftm_output(state, &timing, false);
    k22_timing_test_expect_write(state, &timing, FTM0_COMBINE, 4, 4u);
    k22_timing_test_expect_write(state, &timing, FTM0_SWOCTRL, 4, 0x101u);
    k22_timing_advance(&timing, 1u);
    expect_ftm_output(state, &timing, false);
    k22_timing_test_expect_write(state, &timing, FTM0_COMBINE, 4, 2u);
    k22_timing_test_expect_write(state, &timing, FTM0_SWOCTRL, 4, 0x303u);
    k22_timing_advance(&timing, 1u);
    expect_ftm_output(state, &timing, true);
    bool channel_one = true;
    expect(state, k22_timing_get_ftm_output(&timing, 0u, 1u, &channel_one),
           "k22_timing_get_ftm_output(&timing, 0u, 1u, &channel_one)");
    expect(state, channel_one, "channel_one");

    k22_timing_test_expect_write(state, &timing, FTM0_COMBINE, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_SWOCTRL, 4, 0x101u);
    k22_timing_advance(&timing, 1u);
    k22_timing_test_expect_write(state, &timing, FTM0_POL, 4, 1u);
    expect_ftm_output(state, &timing, false);
    k22_timing_test_expect_write(state, &timing, FTM0_FMS, 4, 0x40u);
    k22_timing_test_expect_write(state, &timing, FTM0_POL, 4, 0u);
    k22_timing_test_expect_read(state, &timing, FTM0_POL, 4, 1u);
    k22_timing_test_expect_write(state, &timing, FTM0_COMBINE, 4, 0x7fu);
    k22_timing_test_expect_read(state, &timing, FTM0_COMBINE, 4, 0x28u);
    k22_timing_test_expect_write(state, &timing, FTM0_DEADTIME, 4, 0xffu);
    k22_timing_test_expect_read(state, &timing, FTM0_DEADTIME, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_FLTCTRL, 4, 0xfffu);
    k22_timing_test_expect_read(state, &timing, FTM0_FLTCTRL, 4, 0xf00u);
    k22_timing_test_expect_write(state, &timing, FTM0_QDCTRL, 4, 0xf9u);
    k22_timing_test_expect_read(state, &timing, FTM0_QDCTRL, 4, 0xf8u);
    k22_timing_test_expect_write(state, &timing, FTM0_FLTPOL, 4, 0xfu);
    k22_timing_test_expect_read(state, &timing, FTM0_FLTPOL, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_MODE, 4, 0xfdu);
    k22_timing_test_expect_read(state, &timing, FTM0_MODE, 4, 0x88u);
    k22_timing_test_expect_read(state, &timing, FTM0_FMS, 4, 0x40u);
    k22_timing_test_expect_write(state, &timing, FTM0_MODE, 4, 4u);
    k22_timing_test_expect_read(state, &timing, FTM0_MODE, 4, 4u);
    k22_timing_test_expect_read(state, &timing, FTM0_FMS, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_POL, 4, 0u);
    expect_ftm_output(state, &timing, true);
    k22_timing_test_expect_write(state, &timing, FTM0_COMBINE, 4, UINT32_MAX);
    k22_timing_test_expect_read(state, &timing, FTM0_COMBINE, 4, 0x7f7f7f7fu);
    k22_timing_test_expect_write(state, &timing, FTM0_FMS, 4, 0u);
    k22_timing_test_expect_read(state, &timing, FTM0_FMS, 4, 0u);

    k22_timing_test_expect_write(state, &timing, FTM0_SWOCTRL, 4, 0u);
    k22_timing_advance(&timing, 1u);
    k22_timing_test_expect_write(state, &timing, FTM0_OUTINIT, 4, 1u);
    k22_timing_test_expect_write(state, &timing, FTM0_MODE, 4, 2u);
    k22_timing_test_expect_write(state, &timing, FTM0_COMBINE, 4, 1u);
    k22_timing_test_expect_write(state, &timing, FTM0_INVCTRL, 4, 1u);
    expect_ftm_output(state, &timing, true);
    k22_timing_advance(&timing, 1u);
    expect_ftm_output(state, &timing, true);

    k22_timing_test_expect_write(state, &timing, FTM0_SYNCONF, 4,
                                 (1u << 12u) | (1u << 11u) | (1u << 10u) | (1u << 7u) | (1u << 5u) |
                                     (1u << 4u));
    k22_timing_test_expect_write(state, &timing, FTM0_SC, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_MOD, 4, 1u);
    k22_timing_test_expect_write(state, &timing, FTM0_CNT, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_SC, 4, 8u);
    k22_timing_test_expect_write(state, &timing, FTM0_SWOCTRL, 4, 1u);
    k22_timing_test_expect_write(state, &timing, FTM0_INVCTRL, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_SYNC, 4, 0x0au);
    k22_timing_test_expect_write(state, &timing, FTM0_OUTMASK, 4, 1u);
    k22_timing_advance(&timing, 2u);
    k22_timing_test_expect_read(state, &timing, FTM0_SWOCTRL, 4, 0u);
    k22_timing_test_expect_read(state, &timing, FTM0_INVCTRL, 4, 1u);
    k22_timing_test_expect_read(state, &timing, FTM0_OUTMASK, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_SYNC, 4, 0x8au);
    k22_timing_test_expect_read(state, &timing, FTM0_SWOCTRL, 4, 1u);
    k22_timing_test_expect_read(state, &timing, FTM0_INVCTRL, 4, 0u);
    k22_timing_test_expect_read(state, &timing, FTM0_OUTMASK, 4, 1u);
    k22_timing_test_expect_read(state, &timing, FTM0_SYNC, 4, 0x8au);
    expect_ftm_output(state, &timing, false);
    k22_timing_advance(&timing, 2u);
    k22_timing_test_expect_read(state, &timing, FTM0_SYNC, 4, 0x0au);

    k22_timing_test_expect_write(state, &timing, FTM0_SC, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_MOD, 4, 5u);
    k22_timing_test_expect_write(state, &timing, FTM0_CNT, 4, 0u);
    k22_timing_test_expect_write(state, &timing, FTM0_SC, 4, 8u);
    k22_timing_advance(&timing, 2u);
    k22_timing_test_expect_read(state, &timing, FTM0_CNT, 4, 2u);
    k22_timing_test_expect_write(state, &timing, FTM0_SYNCONF, 4, (1u << 8u) | (1u << 7u));
    k22_timing_test_expect_write(state, &timing, FTM0_SYNC, 4, 0x80u);
    k22_timing_test_expect_read(state, &timing, FTM0_CNT, 4, 0u);

    expect(state, !k22_timing_get_ftm_output(&timing, 1u, 2u, &(bool){false}),
           "!k22_timing_get_ftm_output(&timing, 1u, 2u, &(bool){false})");
    expect(state, !k22_timing_get_ftm_output(&timing, 4u, 0u, &(bool){false}),
           "!k22_timing_get_ftm_output(&timing, 4u, 0u, &(bool){false})");
    expect(state, !k22_timing_get_ftm_output(&timing, 0u, 0u, NULL),
           "!k22_timing_get_ftm_output(&timing, 0u, 0u, NULL)");
    expect(state, !k22_timing_get_ftm_output(NULL, 0u, 0u, &(bool){false}),
           "!k22_timing_get_ftm_output(NULL, 0u, 0u, &(bool){false})");
}
