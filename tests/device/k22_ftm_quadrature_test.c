#include "k22_timing.h"
#include "test.h"

enum {
    SIM_SCGC6 = 0x4004803cu,
    FTM0_BASE = 0x40038000u,
    FTM1_BASE = 0x40039000u,
    FTM2_BASE = 0x4003a000u,
    FTM_SC = 0u,
    FTM_CNT = 4u,
    FTM_MOD = 8u,
    FTM_C0SC = 0x0cu,
    FTM_CNTIN = 0x4cu,
    FTM_MODE = 0x54u,
    FTM_EXTTRIG = 0x6cu,
    FTM_FILTER = 0x78u,
    FTM_QDCTRL = 0x80u,
    IRQ_FTM1 = 43u,
};

typedef struct {
    bool level[128];
    uint32_t transitions[128];
} IrqRecorder;

static void record_irq(void* context, uint8_t irq, bool asserted) {
    IrqRecorder* recorder = context;
    if (recorder->level[irq] != asserted) {
        recorder->level[irq] = asserted;
        recorder->transitions[irq]++;
    }
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

static void initialize(TestState* state, K22Timing* timing, IrqRecorder* recorder,
                       uint8_t instance, uint16_t initial, uint16_t modulo,
                       uint32_t qdctrl) {
    const K22Profile* profile = k22_profile_get(K22_PROFILE_MK22FN51212);
    const K22TimingSignals signals = {
        .context = recorder,
        .irq = record_irq,
    };
    TEST_EXPECT(state, k22_timing_init(timing, profile, 8000000u, 32768u, signals));
    write_register(state, timing, SIM_SCGC6, timing->sim_scgc6 | (1u << (24u + instance)));
    const uint32_t base = FTM0_BASE + (uint32_t)instance * 0x1000u;
    write_register(state, timing, base + FTM_MODE, 5u);
    write_register(state, timing, base + FTM_CNTIN, initial);
    write_register(state, timing, base + FTM_MOD, modulo);
    write_register(state, timing, base + FTM_CNT, 0u);
    write_register(state, timing, base + FTM_QDCTRL, qdctrl);
    write_register(state, timing, base + FTM_SC, 0x48u);
}

static void set_phase(TestState* state, K22Timing* timing, uint8_t instance, uint8_t phase,
                      bool high, uint32_t cycles) {
    TEST_EXPECT(state, k22_timing_set_ftm_input(timing, instance, phase, high));
    k22_timing_advance(timing, cycles);
}

static uint32_t counter(TestState* state, K22Timing* timing, uint32_t base) {
    return read_register(state, timing, base + FTM_CNT);
}

static bool output(TestState* state, K22Timing* timing, uint8_t instance) {
    bool high = true;
    TEST_EXPECT(state, k22_timing_get_ftm_output(timing, instance, 0u, &high));
    return high;
}

static void clear_overflow(TestState* state, K22Timing* timing, uint32_t base) {
    TEST_EXPECT(state, (read_register(state, timing, base + FTM_SC) & 0x80u) != 0u);
    write_register(state, timing, base + FTM_SC, 0x48u);
}

static void test_phase_encoding(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 1u, 0u, 3u, 1u);
    set_phase(state, &timing, 1u, 0u, true, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 1u);
    set_phase(state, &timing, 1u, 1u, true, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 2u);
    set_phase(state, &timing, 1u, 0u, false, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 3u);
    write_register(state, &timing, FTM1_BASE + FTM_EXTTRIG, 0x40u);
    set_phase(state, &timing, 1u, 1u, false, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 0u);
    TEST_EXPECT(state, (read_register(state, &timing, FTM1_BASE + FTM_QDCTRL) & 7u) == 7u);
    TEST_EXPECT(state, recorder.level[IRQ_FTM1]);
    TEST_EXPECT(state,
                (read_register(state, &timing, FTM1_BASE + FTM_EXTTRIG) & 0xc0u) == 0xc0u);
    clear_overflow(state, &timing, FTM1_BASE);
    TEST_EXPECT(state, !recorder.level[IRQ_FTM1]);

    set_phase(state, &timing, 1u, 1u, true, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 3u);
    TEST_EXPECT(state, (read_register(state, &timing, FTM1_BASE + FTM_QDCTRL) & 7u) == 1u);
    TEST_EXPECT(state, recorder.level[IRQ_FTM1]);
}

static void test_count_and_direction(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 1u, 0u, 7u, 9u);
    set_phase(state, &timing, 1u, 1u, true, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 0u);
    set_phase(state, &timing, 1u, 0u, true, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 1u);
    set_phase(state, &timing, 1u, 0u, false, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 1u);
    set_phase(state, &timing, 1u, 1u, false, 3u);
    set_phase(state, &timing, 1u, 0u, true, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 0u);
    TEST_EXPECT(state,
                (read_register(state, &timing, FTM1_BASE + FTM_QDCTRL) & 0x0du) == 9u);
}

static void test_modulo_boundaries(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 1u, 2u, 4u, 9u);
    set_phase(state, &timing, 1u, 1u, true, 3u);
    for (uint8_t pulse = 0u; pulse < 3u; pulse++) {
        set_phase(state, &timing, 1u, 0u, true, 3u);
        set_phase(state, &timing, 1u, 0u, false, 3u);
    }
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 2u);
    TEST_EXPECT(state, (read_register(state, &timing, FTM1_BASE + FTM_QDCTRL) & 7u) == 7u);
    clear_overflow(state, &timing, FTM1_BASE);
    set_phase(state, &timing, 1u, 1u, false, 3u);
    set_phase(state, &timing, 1u, 0u, true, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 4u);
    TEST_EXPECT(state, (read_register(state, &timing, FTM1_BASE + FTM_QDCTRL) & 7u) == 1u);
}

static void test_filters(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 1u, 0u, 7u, 0x89u);
    write_register(state, &timing, FTM1_BASE + FTM_FILTER, 1u);
    set_phase(state, &timing, 1u, 1u, true, 3u);
    set_phase(state, &timing, 1u, 0u, true, 7u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 0u);
    k22_timing_advance(&timing, 1u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 1u);

    initialize(state, &timing, &recorder, 1u, 0u, 7u, 9u);
    write_register(state, &timing, FTM1_BASE + FTM_FILTER, 0x0fu);
    set_phase(state, &timing, 1u, 1u, true, 3u);
    set_phase(state, &timing, 1u, 0u, true, 2u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 0u);
    k22_timing_advance(&timing, 1u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 1u);

    initialize(state, &timing, &recorder, 1u, 0u, 7u, 0x89u);
    set_phase(state, &timing, 1u, 1u, true, 3u);
    set_phase(state, &timing, 1u, 0u, true, 2u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 0u);
    k22_timing_advance(&timing, 1u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 1u);

    initialize(state, &timing, &recorder, 1u, 0u, 7u, 0x41u);
    write_register(state, &timing, FTM1_BASE + FTM_FILTER, 0x10u);
    set_phase(state, &timing, 1u, 1u, true, 7u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 0u);
    k22_timing_advance(&timing, 1u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 7u);

    initialize(state, &timing, &recorder, 1u, 0u, 7u, 0x89u);
    write_register(state, &timing, FTM1_BASE + FTM_FILTER, 1u);
    set_phase(state, &timing, 1u, 1u, true, 3u);
    set_phase(state, &timing, 1u, 0u, true, 4u);
    set_phase(state, &timing, 1u, 0u, false, 4u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 0u);
    set_phase(state, &timing, 1u, 0u, true, 8u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 1u);
}

static void test_polarity(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 1u, 0u, 7u, 0u);
    write_register(state, &timing, FTM1_BASE + FTM_SC, 0u);
    set_phase(state, &timing, 1u, 0u, true, 3u);
    set_phase(state, &timing, 1u, 1u, true, 3u);
    write_register(state, &timing, FTM1_BASE + FTM_CNT, 0u);
    write_register(state, &timing, FTM1_BASE + FTM_QDCTRL, 0x29u);
    write_register(state, &timing, FTM1_BASE + FTM_SC, 0x48u);
    set_phase(state, &timing, 1u, 0u, false, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 1u);

    initialize(state, &timing, &recorder, 1u, 0u, 7u, 0u);
    write_register(state, &timing, FTM1_BASE + FTM_SC, 0u);
    set_phase(state, &timing, 1u, 1u, false, 3u);
    write_register(state, &timing, FTM1_BASE + FTM_CNT, 0u);
    write_register(state, &timing, FTM1_BASE + FTM_QDCTRL, 0x19u);
    write_register(state, &timing, FTM1_BASE + FTM_SC, 0x48u);
    set_phase(state, &timing, 1u, 0u, true, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 1u);
}

static void test_clock_and_mode_priority(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 1u, 0u, 7u, 9u);
    write_register(state, &timing, FTM1_BASE + FTM_SC, 0u);
    set_phase(state, &timing, 1u, 1u, true, 3u);
    set_phase(state, &timing, 1u, 0u, true, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 0u);
    write_register(state, &timing, FTM1_BASE + FTM_SC, 0x4fu);
    k22_timing_advance(&timing, 100u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 0u);
    set_phase(state, &timing, 1u, 0u, false, 3u);
    set_phase(state, &timing, 1u, 0u, true, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 1u);
    write_register(state, &timing, FTM1_BASE + FTM_C0SC, 0x4cu);
    set_phase(state, &timing, 1u, 0u, false, 3u);
    TEST_EXPECT(state, (read_register(state, &timing, FTM1_BASE + FTM_C0SC) & 0x80u) == 0u);
    timing.ftm[1].channel_output[0] = true;
    TEST_EXPECT(state, !output(state, &timing, 1u));

    initialize(state, &timing, &recorder, 1u, 0u, 7u, 9u);
    set_phase(state, &timing, 1u, 1u, true, 3u);
    k22_timing_set_debug_halted(&timing, true);
    set_phase(state, &timing, 1u, 0u, true, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 0u);
    k22_timing_set_debug_halted(&timing, false);
    set_phase(state, &timing, 1u, 0u, false, 3u);
    set_phase(state, &timing, 1u, 0u, true, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM1_BASE) == 1u);
}

static void test_capable_instances_and_status_bits(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 2u, 0u, 7u, 9u);
    set_phase(state, &timing, 2u, 1u, true, 3u);
    set_phase(state, &timing, 2u, 0u, true, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM2_BASE) == 1u);
    write_register(state, &timing, FTM2_BASE + FTM_QDCTRL, 0x0bu);
    TEST_EXPECT(state,
                (read_register(state, &timing, FTM2_BASE + FTM_QDCTRL) & 0x0fu) == 0x0du);

    initialize(state, &timing, &recorder, 0u, 0u, 7u, 1u);
    k22_timing_advance(&timing, 3u);
    TEST_EXPECT(state, counter(state, &timing, FTM0_BASE) == 3u);
    TEST_EXPECT(state, !timing.ftm[0].quadrature_capable);
    TEST_EXPECT(state, timing.ftm[1].quadrature_capable);
    TEST_EXPECT(state, timing.ftm[2].quadrature_capable);
    TEST_EXPECT(state, !timing.ftm[3].quadrature_capable);
    k22_timing_reset(&timing, 0x82u, 0u);
    TEST_EXPECT(state, timing.ftm[1].quadrature_capable);
    TEST_EXPECT(state, timing.ftm[2].quadrature_capable);
}

int main(void) {
    TestState state = {0};
    test_phase_encoding(&state);
    test_count_and_direction(&state);
    test_modulo_boundaries(&state);
    test_filters(&state);
    test_polarity(&state);
    test_clock_and_mode_priority(&state);
    test_capable_instances_and_status_bits(&state);
    return test_finish(&state);
}
