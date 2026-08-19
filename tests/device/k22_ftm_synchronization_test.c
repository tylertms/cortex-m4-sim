#include "k22_timing.h"
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
    FTM0_COMBINE = 0x40038064u,
    FTM0_SYNCONF = 0x4003808cu,
    FTM0_PWMLOAD = 0x40038098u,
};

static void write_register(TestState* state, K22Timing* timing, uint32_t address,
                           uint32_t value) {
    TEST_EXPECT(state, k22_timing_write(timing, address, 4u, value));
}

static void expect_register(TestState* state, K22Timing* timing, uint32_t address,
                            uint32_t expected) {
    uint32_t value = 0u;
    TEST_EXPECT(state, k22_timing_read(timing, address, 4u, &value));
    TEST_EXPECT(state, value == expected);
}

static void initialize(TestState* state, K22Timing* timing) {
    const K22Profile* profile = k22_profile_get(K22_PROFILE_MK22FN51212);
    TEST_EXPECT(state,
                k22_timing_init(timing, profile, 8000000u, 32768u, (K22TimingSignals){0}));
    write_register(state, timing, SIM_SCGC6, timing->sim_scgc6 | (1u << 24u));
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
    write_register(state, &timing, FTM0_SYNCONF,
                   (1u << 9u) | (1u << 8u) | (1u << 7u) | (1u << 2u));
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

static void test_intermediate_load(TestState* state, uint32_t load_select,
                                   uint32_t cycles) {
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

int main(void) {
    TestState state = {0};
    test_legacy_updates(&state);
    test_software_synchronization(&state);
    test_deferred_software_synchronization(&state);
    test_unsynchronized_output_compare(&state);
    test_intermediate_load(&state, 1u, 1u);
    test_intermediate_load(&state, 0u, 4u);
    return test_finish(&state);
}
