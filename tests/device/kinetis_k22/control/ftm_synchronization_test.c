#include "device/kinetis_k22/timing/api.h"
#include "test.h"

enum {
    SIM_SCGC6 = 0x4004803cu,
    FTM0_SC = 0x40038000u,
    FTM0_CNT = 0x40038004u,
    FTM0_MOD = 0x40038008u,
    FTM0_C0SC = 0x4003800cu,
    FTM0_C0V = 0x40038010u,
    FTM0_CNTIN = 0x4003804cu,
    FTM0_MODE = 0x40038054u,
    FTM0_SYNC = 0x40038058u,
    FTM0_OUTMASK = 0x40038060u,
    FTM0_COMBINE = 0x40038064u,
    FTM0_SYNCONF = 0x4003808cu,
    FTM0_INVCTRL = 0x40038090u,
    FTM0_SWOCTRL = 0x40038094u,
    FTM0_PWMLOAD = 0x40038098u,
};

static void write_register(TestState* state, K22Timing* timing, uint32_t address, uint32_t value) {
    expect(state, k22_timing_write(timing, address, 4u, value),
           "k22_timing_write(timing, address, 4u, value)");
}

static void expect_register(TestState* state, K22Timing* timing, uint32_t address,
                            uint32_t expected) {
    uint32_t value = 0u;
    expect(state, k22_timing_read(timing, address, 4u, &value),
           "k22_timing_read(timing, address, 4u, &value)");
    expect(state, value == expected, "value == expected");
}

static void initialize(TestState* state, K22Timing* timing) {
    const K22Profile* profile = k22_profile_get(K22_PROFILE_MK22FN51212);
    expect(state, k22_timing_init(timing, profile, 8000000u, 32768u, (K22TimingSignals){0}),
           "k22_timing_init(timing, profile, 8000000u, 32768u, (K22TimingSignals){0})");
    write_register(state, timing, SIM_SCGC6, timing->sim_scgc6 | (1u << 24u));
}

static void set_counter(TestState* state, K22Timing* timing, uint32_t value) {
    write_register(state, timing, FTM0_CNT, 0u);
    write_register(state, timing, FTM0_SC, 8u);
    k22_timing_advance(timing, value - 2u);
    write_register(state, timing, FTM0_SC, 0u);
}

static void test_legacy_updates(TestState* state) {
    K22Timing timing;
    initialize(state, &timing);
    write_register(state, &timing, FTM0_CNTIN, 3u);
    write_register(state, &timing, FTM0_MOD, 10u);
    write_register(state, &timing, FTM0_CNT, 0u);
    write_register(state, &timing, FTM0_SC, 8u);
    write_register(state, &timing, FTM0_CNTIN, 7u);
    expect_register(state, &timing, FTM0_CNTIN, 3u);
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_CNTIN, 7u);

    write_register(state, &timing, FTM0_SC, 0u);
    write_register(state, &timing, FTM0_CNTIN, 0u);
    write_register(state, &timing, FTM0_MOD, 3u);
    write_register(state, &timing, FTM0_C0SC, 0x10u);
    write_register(state, &timing, FTM0_C0V, 1u);
    write_register(state, &timing, FTM0_CNT, 0u);
    write_register(state, &timing, FTM0_SC, 8u);
    write_register(state, &timing, FTM0_C0V, 2u);
    expect_register(state, &timing, FTM0_C0V, 1u);
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_C0V, 2u);

    write_register(state, &timing, FTM0_MOD, 7u);
    expect_register(state, &timing, FTM0_MOD, 3u);
    k22_timing_advance(&timing, 3u);
    expect_register(state, &timing, FTM0_MOD, 7u);

    write_register(state, &timing, FTM0_SC, 0u);
    write_register(state, &timing, FTM0_MOD, 3u);
    write_register(state, &timing, FTM0_C0SC, 0x28u);
    write_register(state, &timing, FTM0_C0V, 1u);
    write_register(state, &timing, FTM0_CNT, 0u);
    write_register(state, &timing, FTM0_SC, 8u);
    write_register(state, &timing, FTM0_C0V, 2u);
    expect_register(state, &timing, FTM0_C0V, 1u);
    k22_timing_advance(&timing, 4u);
    expect_register(state, &timing, FTM0_C0V, 2u);
}

static void test_software_synchronization(TestState* state) {
    K22Timing timing;
    initialize(state, &timing);
    write_register(state, &timing, FTM0_MODE, 5u);
    write_register(state, &timing, FTM0_COMBINE, 1u << 5u);
    write_register(state, &timing, FTM0_SYNCONF, (1u << 9u) | (1u << 8u) | (1u << 7u) | (1u << 2u));
    write_register(state, &timing, FTM0_MOD, 3u);
    write_register(state, &timing, FTM0_CNTIN, 0u);
    write_register(state, &timing, FTM0_C0V, 1u);
    write_register(state, &timing, FTM0_CNT, 0u);
    write_register(state, &timing, FTM0_SC, 8u);
    write_register(state, &timing, FTM0_MOD, 7u);
    write_register(state, &timing, FTM0_CNTIN, 2u);
    write_register(state, &timing, FTM0_C0V, 4u);
    expect_register(state, &timing, FTM0_MOD, 3u);
    expect_register(state, &timing, FTM0_CNTIN, 0u);
    expect_register(state, &timing, FTM0_C0V, 1u);
    write_register(state, &timing, FTM0_SYNC, 0x80u);
    expect_register(state, &timing, FTM0_MOD, 7u);
    expect_register(state, &timing, FTM0_CNTIN, 2u);
    expect_register(state, &timing, FTM0_C0V, 4u);
    expect_register(state, &timing, FTM0_CNT, 2u);
    expect_register(state, &timing, FTM0_SYNC, 0u);
}

static void test_deferred_software_synchronization(TestState* state) {
    K22Timing timing;
    initialize(state, &timing);
    write_register(state, &timing, FTM0_MODE, 5u);
    write_register(state, &timing, FTM0_COMBINE, 1u << 5u);
    write_register(state, &timing, FTM0_SYNCONF, (1u << 9u) | (1u << 7u) | (1u << 2u));
    write_register(state, &timing, FTM0_MOD, 3u);
    write_register(state, &timing, FTM0_CNTIN, 0u);
    write_register(state, &timing, FTM0_C0V, 1u);
    write_register(state, &timing, FTM0_CNT, 0u);
    write_register(state, &timing, FTM0_SC, 8u);
    write_register(state, &timing, FTM0_MOD, 7u);
    write_register(state, &timing, FTM0_CNTIN, 2u);
    write_register(state, &timing, FTM0_C0V, 4u);
    write_register(state, &timing, FTM0_SYNC, 0x82u);
    expect_register(state, &timing, FTM0_MOD, 3u);
    expect_register(state, &timing, FTM0_CNTIN, 0u);
    expect_register(state, &timing, FTM0_C0V, 1u);
    expect_register(state, &timing, FTM0_SYNC, 0x82u);
    k22_timing_advance(&timing, 4u);
    expect_register(state, &timing, FTM0_MOD, 7u);
    expect_register(state, &timing, FTM0_CNTIN, 2u);
    expect_register(state, &timing, FTM0_C0V, 4u);
    expect_register(state, &timing, FTM0_SYNC, 2u);
}

static void test_unsynchronized_output_compare(TestState* state) {
    K22Timing timing;
    initialize(state, &timing);
    write_register(state, &timing, FTM0_MODE, 5u);
    write_register(state, &timing, FTM0_MOD, 3u);
    write_register(state, &timing, FTM0_C0SC, 0x10u);
    write_register(state, &timing, FTM0_C0V, 1u);
    write_register(state, &timing, FTM0_CNT, 0u);
    write_register(state, &timing, FTM0_SC, 8u);
    write_register(state, &timing, FTM0_C0V, 2u);
    expect_register(state, &timing, FTM0_C0V, 1u);
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_C0V, 2u);
}

static void test_intermediate_load(TestState* state, uint32_t load_select, uint32_t cycles) {
    K22Timing timing;
    initialize(state, &timing);
    write_register(state, &timing, FTM0_MODE, 5u);
    write_register(state, &timing, FTM0_COMBINE, 1u << 5u);
    write_register(state, &timing, FTM0_SYNCONF, 1u << 2u);
    write_register(state, &timing, FTM0_MOD, 3u);
    write_register(state, &timing, FTM0_CNTIN, 0u);
    write_register(state, &timing, FTM0_C0SC, 0x28u);
    write_register(state, &timing, FTM0_C0V, 1u);
    write_register(state, &timing, FTM0_CNT, 0u);
    write_register(state, &timing, FTM0_SC, 8u);
    write_register(state, &timing, FTM0_MOD, 7u);
    write_register(state, &timing, FTM0_CNTIN, 2u);
    write_register(state, &timing, FTM0_C0V, 4u);
    write_register(state, &timing, FTM0_PWMLOAD, (1u << 9u) | load_select);
    expect_register(state, &timing, FTM0_PWMLOAD, (1u << 9u) | load_select);
    k22_timing_advance(&timing, cycles);
    expect_register(state, &timing, FTM0_MOD, 7u);
    expect_register(state, &timing, FTM0_CNTIN, 2u);
    expect_register(state, &timing, FTM0_C0V, 4u);
    expect_register(state, &timing, FTM0_PWMLOAD, load_select);
}

static void test_hardware_trigger_domain(TestState* state) {
    expect(state, !k22_timing_trigger_ftm_hardware(NULL, 0u, 0u),
           "!k22_timing_trigger_ftm_hardware(NULL, 0u, 0u)");
    K22Timing timing;
    initialize(state, &timing);
    expect(state, !k22_timing_trigger_ftm_hardware(&timing, 4u, 0u),
           "!k22_timing_trigger_ftm_hardware(&timing, 4u, 0u)");
    expect(state, !k22_timing_trigger_ftm_hardware(&timing, 0u, 3u),
           "!k22_timing_trigger_ftm_hardware(&timing, 0u, 3u)");
    write_register(state, &timing, FTM0_MODE, 5u);
    write_register(state, &timing, FTM0_CNTIN, 2u);
    write_register(state, &timing, FTM0_CNT, 0u);
    write_register(state, &timing, FTM0_SYNCONF, (1u << 16u) | (1u << 7u));
    set_counter(state, &timing, 9u);
    expect(state, k22_timing_trigger_ftm_hardware(&timing, 0u, 0u),
           "k22_timing_trigger_ftm_hardware(&timing, 0u, 0u)");
    expect_register(state, &timing, FTM0_CNT, 9u);
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_CNT, 9u);
    write_register(state, &timing, FTM0_SYNC, 1u << 4u);
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_CNT, 9u);

    for (uint8_t trigger = 0u; trigger < 3u; trigger++) {
        set_counter(state, &timing, 7u + trigger);
        write_register(state, &timing, FTM0_SYNC, 1u << (trigger + 4u));
        expect(state, k22_timing_trigger_ftm_hardware(&timing, 0u, trigger),
               "k22_timing_trigger_ftm_hardware(&timing, 0u, trigger)");
        expect_register(state, &timing, FTM0_CNT, 7u + trigger);
        k22_timing_advance(&timing, 1u);
        expect_register(state, &timing, FTM0_CNT, 2u);
        expect_register(state, &timing, FTM0_SYNC, 0u);
    }

    set_counter(state, &timing, 11u);
    write_register(state, &timing, FTM0_SYNCONF, (1u << 16u) | (1u << 7u) | 1u);
    write_register(state, &timing, FTM0_SYNC, 1u << 4u);
    expect(state, k22_timing_trigger_ftm_hardware(&timing, 0u, 0u),
           "k22_timing_trigger_ftm_hardware(&timing, 0u, 0u)");
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_CNT, 2u);
    expect_register(state, &timing, FTM0_SYNC, 1u << 4u);
}

static void test_hardware_output_synchronization(TestState* state) {
    K22Timing timing;
    initialize(state, &timing);
    write_register(state, &timing, FTM0_MODE, 5u);
    write_register(state, &timing, FTM0_SYNC, (1u << 4u) | (1u << 3u));
    write_register(state, &timing, FTM0_SYNCONF, (1u << 7u) | (1u << 5u) | (1u << 4u));
    write_register(state, &timing, FTM0_OUTMASK, 1u);
    write_register(state, &timing, FTM0_INVCTRL, 1u);
    write_register(state, &timing, FTM0_SWOCTRL, 0x0101u);
    expect_register(state, &timing, FTM0_OUTMASK, 0u);
    expect_register(state, &timing, FTM0_INVCTRL, 0u);
    expect_register(state, &timing, FTM0_SWOCTRL, 0u);
    expect(state, k22_timing_trigger_ftm_hardware(&timing, 0u, 0u),
           "k22_timing_trigger_ftm_hardware(&timing, 0u, 0u)");
    expect_register(state, &timing, FTM0_OUTMASK, 0u);
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_OUTMASK, 0u);
    expect_register(state, &timing, FTM0_INVCTRL, 0u);
    expect_register(state, &timing, FTM0_SWOCTRL, 0u);
    write_register(state, &timing, FTM0_SYNC, (1u << 4u) | (1u << 3u));
    write_register(state, &timing, FTM0_SYNCONF,
                   (1u << 20u) | (1u << 19u) | (1u << 18u) | (1u << 7u) | (1u << 5u) | (1u << 4u));
    expect(state, k22_timing_trigger_ftm_hardware(&timing, 0u, 0u),
           "k22_timing_trigger_ftm_hardware(&timing, 0u, 0u)");
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_OUTMASK, 1u);
    expect_register(state, &timing, FTM0_INVCTRL, 1u);
    expect_register(state, &timing, FTM0_SWOCTRL, 0x0101u);
}

static void prepare_hardware_write_buffers(TestState* state, K22Timing* timing, uint32_t synconf,
                                           uint32_t sync) {
    initialize(state, timing);
    write_register(state, timing, FTM0_MODE, 5u);
    write_register(state, timing, FTM0_COMBINE, 1u << 5u);
    write_register(state, timing, FTM0_SYNCONF, synconf | (1u << 7u) | (1u << 2u));
    write_register(state, timing, FTM0_SYNC, sync | (1u << 4u));
    write_register(state, timing, FTM0_MOD, 3u);
    write_register(state, timing, FTM0_CNTIN, 0u);
    write_register(state, timing, FTM0_C0V, 1u);
    write_register(state, timing, FTM0_CNT, 0u);
    write_register(state, timing, FTM0_SC, 15u);
    write_register(state, timing, FTM0_MOD, 7u);
    write_register(state, timing, FTM0_CNTIN, 2u);
    write_register(state, timing, FTM0_C0V, 4u);
}

static void test_immediate_hardware_write_buffers(TestState* state) {
    K22Timing timing;
    prepare_hardware_write_buffers(state, &timing, (1u << 17u) | (1u << 16u), 0u);
    expect(state, k22_timing_trigger_ftm_hardware(&timing, 0u, 0u),
           "k22_timing_trigger_ftm_hardware(&timing, 0u, 0u)");
    expect_register(state, &timing, FTM0_MOD, 3u);
    expect_register(state, &timing, FTM0_CNTIN, 0u);
    expect_register(state, &timing, FTM0_C0V, 1u);
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_MOD, 7u);
    expect_register(state, &timing, FTM0_CNTIN, 2u);
    expect_register(state, &timing, FTM0_C0V, 4u);
    expect_register(state, &timing, FTM0_CNT, 2u);
}

static void test_hardware_write_buffer_gate(TestState* state) {
    K22Timing timing;
    prepare_hardware_write_buffers(state, &timing, 1u << 16u, 0u);
    expect(state, k22_timing_trigger_ftm_hardware(&timing, 0u, 0u),
           "k22_timing_trigger_ftm_hardware(&timing, 0u, 0u)");
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_MOD, 3u);
    expect_register(state, &timing, FTM0_CNTIN, 0u);
    expect_register(state, &timing, FTM0_C0V, 1u);
    expect_register(state, &timing, FTM0_CNT, 0u);
}

static void test_deferred_hardware_write_buffers(TestState* state) {
    K22Timing timing;
    prepare_hardware_write_buffers(state, &timing, 1u << 17u, 2u);
    write_register(state, &timing, FTM0_SC, 8u);
    expect(state, k22_timing_trigger_ftm_hardware(&timing, 0u, 0u),
           "k22_timing_trigger_ftm_hardware(&timing, 0u, 0u)");
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_MOD, 3u);
    expect_register(state, &timing, FTM0_CNTIN, 0u);
    expect_register(state, &timing, FTM0_C0V, 1u);
    k22_timing_advance(&timing, 3u);
    expect_register(state, &timing, FTM0_MOD, 7u);
    expect_register(state, &timing, FTM0_CNTIN, 2u);
    expect_register(state, &timing, FTM0_C0V, 4u);
}

static void prepare_legacy_hardware_sync(TestState* state, K22Timing* timing, uint32_t mode,
                                         uint32_t sync) {
    initialize(state, timing);
    write_register(state, timing, FTM0_MODE, mode);
    write_register(state, timing, FTM0_COMBINE, 1u << 5u);
    write_register(state, timing, FTM0_SYNC, sync | (1u << 4u));
    write_register(state, timing, FTM0_MOD, 3u);
    write_register(state, timing, FTM0_CNTIN, 0u);
    write_register(state, timing, FTM0_C0V, 1u);
    write_register(state, timing, FTM0_CNT, 0u);
    write_register(state, timing, FTM0_SC, 15u);
    write_register(state, timing, FTM0_MOD, 7u);
    write_register(state, timing, FTM0_C0V, 4u);
}

static void test_legacy_hardware_synchronization(TestState* state) {
    K22Timing timing;
    prepare_legacy_hardware_sync(state, &timing, 5u, (1u << 3u) | (1u << 2u));
    write_register(state, &timing, FTM0_OUTMASK, 1u);
    expect(state, k22_timing_trigger_ftm_hardware(&timing, 0u, 0u),
           "k22_timing_trigger_ftm_hardware(&timing, 0u, 0u)");
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_MOD, 7u);
    expect_register(state, &timing, FTM0_C0V, 4u);
    expect_register(state, &timing, FTM0_CNT, 0u);
    expect_register(state, &timing, FTM0_OUTMASK, 1u);
    expect_register(state, &timing, FTM0_SYNC, (1u << 3u) | (1u << 2u));

    prepare_legacy_hardware_sync(state, &timing, 13u, (1u << 3u) | (1u << 2u));
    write_register(state, &timing, FTM0_OUTMASK, 1u);
    expect(state, k22_timing_trigger_ftm_hardware(&timing, 0u, 0u),
           "k22_timing_trigger_ftm_hardware(&timing, 0u, 0u)");
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_MOD, 3u);
    expect_register(state, &timing, FTM0_C0V, 1u);
    expect_register(state, &timing, FTM0_CNT, 0u);
    expect_register(state, &timing, FTM0_OUTMASK, 1u);
}

static void test_deferred_legacy_hardware_synchronization(TestState* state) {
    K22Timing timing;
    prepare_legacy_hardware_sync(state, &timing, 5u, 2u);
    write_register(state, &timing, FTM0_SC, 8u);
    expect(state, k22_timing_trigger_ftm_hardware(&timing, 0u, 0u),
           "k22_timing_trigger_ftm_hardware(&timing, 0u, 0u)");
    k22_timing_advance(&timing, 1u);
    expect_register(state, &timing, FTM0_MOD, 3u);
    expect_register(state, &timing, FTM0_C0V, 1u);
    k22_timing_advance(&timing, 3u);
    expect_register(state, &timing, FTM0_MOD, 7u);
    expect_register(state, &timing, FTM0_C0V, 4u);
}

int main(void) {
    TestState state = {0};
    test_legacy_updates(&state);
    test_software_synchronization(&state);
    test_deferred_software_synchronization(&state);
    test_unsynchronized_output_compare(&state);
    test_intermediate_load(&state, 1u, 1u);
    test_intermediate_load(&state, 0u, 4u);
    test_hardware_trigger_domain(&state);
    test_hardware_output_synchronization(&state);
    test_immediate_hardware_write_buffers(&state);
    test_hardware_write_buffer_gate(&state);
    test_deferred_hardware_write_buffers(&state);
    test_legacy_hardware_synchronization(&state);
    test_deferred_legacy_hardware_synchronization(&state);
    return test_finish(&state);
}
