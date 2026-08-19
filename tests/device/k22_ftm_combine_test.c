#include "k22_timing.h"
#include "test.h"

enum {
    SIM_SCGC6 = 0x4004803cu,
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
    FTM0_COMBINE = 0x40038064u,
    FTM0_QDCTRL = 0x40038080u,
    FTM0_INVCTRL = 0x40038090u,
    FTM0_SWOCTRL = 0x40038094u,
};

typedef struct {
    uint32_t requests[64];
} DmaRecorder;

static void record_dma(void* context, uint8_t source) {
    DmaRecorder* recorder = context;
    recorder->requests[source]++;
}

static void write_register(TestState* state, K22Timing* timing, uint32_t address,
                           uint32_t value) {
    TEST_EXPECT(state, k22_timing_write(timing, address, 4u, value));
}

static uint32_t read_register(TestState* state, K22Timing* timing, uint32_t address) {
    uint32_t value = 0u;
    TEST_EXPECT(state, k22_timing_read(timing, address, 4u, &value));
    return value;
}

static bool output(TestState* state, K22Timing* timing, uint8_t channel) {
    bool high = false;
    TEST_EXPECT(state, k22_timing_get_ftm_output(timing, 0u, channel, &high));
    return high;
}

static void initialize(TestState* state, K22Timing* timing, DmaRecorder* recorder) {
    const K22Profile* profile = k22_profile_get(K22_PROFILE_MK22FN51212);
    const K22TimingSignals signals = {
        .context = recorder,
        .dma = record_dma,
    };
    TEST_EXPECT(state, k22_timing_init(timing, profile, 8000000u, 32768u, signals));
    write_register(state, timing, SIM_SCGC6, timing->sim_scgc6 | (1u << 24u));
}

static void configure(TestState* state, K22Timing* timing, DmaRecorder* recorder,
                      uint32_t first_sc, uint32_t second_sc, uint16_t first_compare,
                      uint16_t second_compare, uint16_t modulo, uint32_t combine) {
    initialize(state, timing, recorder);
    write_register(state, timing, FTM0_MODE, 5u);
    write_register(state, timing, FTM0_CNTIN, 0u);
    write_register(state, timing, FTM0_MOD, modulo);
    write_register(state, timing, FTM0_COMBINE, combine);
    write_register(state, timing, FTM0_C0SC, first_sc);
    write_register(state, timing, FTM0_C1SC, second_sc);
    write_register(state, timing, FTM0_C0V, first_compare);
    write_register(state, timing, FTM0_C1V, second_compare);
    write_register(state, timing, FTM0_CNT, 0u);
    write_register(state, timing, FTM0_SC, 8u);
}

static void expect_outputs(TestState* state, K22Timing* timing, bool first, bool second) {
    TEST_EXPECT(state, output(state, timing, 0u) == first);
    TEST_EXPECT(state, output(state, timing, 1u) == second);
}

static void advance_and_expect(TestState* state, K22Timing* timing, uint32_t cycles,
                               bool first, bool second) {
    k22_timing_advance(timing, cycles);
    expect_outputs(state, timing, first, second);
}

static void test_combined_waveforms(TestState* state) {
    K22Timing timing;
    DmaRecorder recorder = {0};
    configure(state, &timing, &recorder, 0x28u, 0x08u, 2u, 5u, 7u, 3u);
    advance_and_expect(state, &timing, 1u, false, true);
    advance_and_expect(state, &timing, 1u, true, false);
    advance_and_expect(state, &timing, 2u, true, false);
    advance_and_expect(state, &timing, 1u, false, true);
    advance_and_expect(state, &timing, 3u, false, true);

    configure(state, &timing, &recorder, 0x24u, 0x04u, 2u, 5u, 7u, 3u);
    advance_and_expect(state, &timing, 1u, true, false);
    advance_and_expect(state, &timing, 1u, false, true);
    advance_and_expect(state, &timing, 3u, true, false);

    configure(state, &timing, &recorder, 0x28u, 0x08u, 2u, 5u, 7u, 1u);
    advance_and_expect(state, &timing, 2u, true, true);
    advance_and_expect(state, &timing, 3u, false, false);
}

static void test_complementary_control(TestState* state) {
    K22Timing timing;
    DmaRecorder recorder = {0};
    configure(state, &timing, &recorder, 0x28u, 0x08u, 2u, 5u, 7u, 3u);
    advance_and_expect(state, &timing, 2u, true, false);
    write_register(state, &timing, FTM0_INVCTRL, 1u);
    advance_and_expect(state, &timing, 1u, false, true);

    configure(state, &timing, &recorder, 0x28u, 0x08u, 2u, 5u, 7u, 2u);
    advance_and_expect(state, &timing, 8u, true, false);
    advance_and_expect(state, &timing, 2u, false, true);
    write_register(state, &timing, FTM0_INVCTRL, 1u);
    advance_and_expect(state, &timing, 1u, true, false);

    configure(state, &timing, &recorder, 0x28u, 0x08u, 2u, 5u, 7u, 3u);
    write_register(state, &timing, FTM0_SWOCTRL, 0x0303u);
    advance_and_expect(state, &timing, 1u, true, false);

    configure(state, &timing, &recorder, 0x28u, 0u, 2u, 5u, 7u, 3u);
    advance_and_expect(state, &timing, 2u, true, false);
}

static void expect_active_at(TestState* state, uint16_t first_compare,
                             uint16_t second_compare, uint16_t modulo, uint32_t counter,
                             bool expected) {
    K22Timing timing;
    DmaRecorder recorder = {0};
    configure(state, &timing, &recorder, 0x28u, 0x08u, first_compare, second_compare,
              modulo, 1u);
    advance_and_expect(state, &timing, counter, expected, expected);
}

static void test_compare_boundaries(TestState* state) {
    expect_active_at(state, 2u, 5u, 7u, 4u, true);
    expect_active_at(state, 5u, 2u, 7u, 6u, false);
    expect_active_at(state, 3u, 3u, 7u, 3u, false);
    expect_active_at(state, 0u, 5u, 7u, 1u, true);
    expect_active_at(state, 2u, 7u, 7u, 6u, true);
    expect_active_at(state, 2u, 9u, 7u, 6u, true);
    expect_active_at(state, 9u, 5u, 7u, 6u, false);
    expect_active_at(state, 2u, UINT16_MAX, 7u, 6u, true);
}

static void test_mode_selection(TestState* state) {
    K22Timing timing;
    DmaRecorder recorder = {0};
    configure(state, &timing, &recorder, 0x28u, 0x08u, 2u, 5u, 7u, 5u);
    advance_and_expect(state, &timing, 2u, false, false);

    configure(state, &timing, &recorder, 0x28u, 0x08u, 2u, 5u, 7u, 1u);
    write_register(state, &timing, FTM0_SC, 0x28u);
    advance_and_expect(state, &timing, 2u, false, false);

    configure(state, &timing, &recorder, 0x28u, 0x08u, 2u, 5u, 7u, 1u);
    write_register(state, &timing, FTM0_QDCTRL, 1u);
    advance_and_expect(state, &timing, 2u, false, false);
}

static void test_buffered_values(TestState* state) {
    K22Timing timing;
    DmaRecorder recorder = {0};
    configure(state, &timing, &recorder, 0x28u, 0x08u, 2u, 5u, 7u, 1u);
    write_register(state, &timing, FTM0_MODE, 4u);
    write_register(state, &timing, FTM0_C0V, 3u);
    write_register(state, &timing, FTM0_C1V, 6u);
    TEST_EXPECT(state, read_register(state, &timing, FTM0_C0V) == 2u);
    TEST_EXPECT(state, read_register(state, &timing, FTM0_C1V) == 5u);
    k22_timing_advance(&timing, 8u);
    TEST_EXPECT(state, read_register(state, &timing, FTM0_C0V) == 3u);
    TEST_EXPECT(state, read_register(state, &timing, FTM0_C1V) == 6u);
    advance_and_expect(state, &timing, 3u, true, true);
}

static void test_channel_events(TestState* state) {
    K22Timing timing;
    DmaRecorder recorder = {0};
    configure(state, &timing, &recorder, 0x69u, 0x49u, 2u, 5u, 7u, 1u);
    k22_timing_advance(&timing, 2u);
    TEST_EXPECT(state, (read_register(state, &timing, FTM0_STATUS) & 1u) != 0u);
    TEST_EXPECT(state, recorder.requests[20u] == 1u);
    TEST_EXPECT(state, recorder.requests[21u] == 0u);
    k22_timing_advance(&timing, 3u);
    TEST_EXPECT(state, (read_register(state, &timing, FTM0_STATUS) & 3u) == 3u);
    TEST_EXPECT(state, recorder.requests[21u] == 1u);
    k22_timing_advance(&timing, 8u);
    TEST_EXPECT(state, recorder.requests[20u] == 2u);
    TEST_EXPECT(state, recorder.requests[21u] == 2u);
}

int main(void) {
    TestState state = {0};
    test_combined_waveforms(&state);
    test_complementary_control(&state);
    test_compare_boundaries(&state);
    test_mode_selection(&state);
    test_buffered_values(&state);
    test_channel_events(&state);
    return test_finish(&state);
}
