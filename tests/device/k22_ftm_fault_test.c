#include "k22_timing.h"
#include "test.h"

enum {
    SIM_SCGC6 = 0x4004803cu,
    FTM0_SC = 0x40038000u,
    FTM0_MOD = 0x40038008u,
    FTM0_MODE = 0x40038054u,
    FTM0_COMBINE = 0x40038064u,
    FTM0_POL = 0x40038070u,
    FTM0_FMS = 0x40038074u,
    FTM0_FLTCTRL = 0x4003807cu,
    FTM0_FLTPOL = 0x40038088u,
    FTM0_SWOCTRL = 0x40038094u,
    IRQ_FTM0 = 42u,
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
    expect(state, k22_timing_write(timing, address, 4u, value),
           "k22_timing_write(timing, address, 4u, value)");
}

static uint32_t read_register(TestState* state, K22Timing* timing, uint32_t address) {
    uint32_t value = 0u;
    expect(state, k22_timing_read(timing, address, 4u, &value),
           "k22_timing_read(timing, address, 4u, &value)");
    return value;
}

static bool output(TestState* state, K22Timing* timing, uint8_t channel) {
    bool high = false;
    expect(state, k22_timing_get_ftm_output(timing, 0u, channel, &high),
           "k22_timing_get_ftm_output(timing, 0u, channel, &high)");
    return high;
}

static void initialize(TestState* state, K22Timing* timing, IrqRecorder* recorder,
                       uint32_t mode, uint32_t combine, uint32_t fault_control) {
    const K22Profile* profile = k22_profile_get(K22_PROFILE_MK22FN51212);
    const K22TimingSignals signals = {
        .context = recorder,
        .irq = record_irq,
    };
    expect(state, k22_timing_init(timing, profile, 8000000u, 32768u, signals),
           "k22_timing_init(timing, profile, 8000000u, 32768u, signals)");
    write_register(state, timing, SIM_SCGC6, timing->sim_scgc6 | (1u << 24u));
    write_register(state, timing, FTM0_MODE, mode | 5u);
    write_register(state, timing, FTM0_MOD, 3u);
    write_register(state, timing, FTM0_COMBINE, combine);
    write_register(state, timing, FTM0_FLTCTRL, fault_control);
    write_register(state, timing, FTM0_SWOCTRL, 0x0303u);
    k22_timing_advance(timing, 1u);
}

static void expect_outputs(TestState* state, K22Timing* timing, bool first, bool second) {
    expect(state, output(state, timing, 0u) == first, "output(state, timing, 0u) == first");
    expect(state, output(state, timing, 1u) == second,
           "output(state, timing, 1u) == second");
}

static void test_manual_fault_lifecycle(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 0xc0u, 0x40u, 1u);
    expect_outputs(state, &timing, true, true);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 2u);
    expect(state, read_register(state, &timing, FTM0_FMS) == 0u,
           "read_register(state, &timing, FTM0_FMS) == 0u");
    k22_timing_advance(&timing, 1u);
    expect(state, read_register(state, &timing, FTM0_FMS) == 0xa1u,
           "read_register(state, &timing, FTM0_FMS) == 0xa1u");
    expect(state, recorder.level[IRQ_FTM0], "recorder.level[IRQ_FTM0]");
    expect_outputs(state, &timing, false, false);

    write_register(state, &timing, FTM0_FMS, 0u);
    expect(state, (read_register(state, &timing, FTM0_FMS) & 0x81u) == 0x81u,
           "(read_register(state, &timing, FTM0_FMS) & 0x81u) == 0x81u");
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, false),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, false)");
    k22_timing_advance(&timing, 3u);
    write_register(state, &timing, FTM0_FMS, 0u);
    expect(state, (read_register(state, &timing, FTM0_FMS) & 0x81u) == 0u,
           "(read_register(state, &timing, FTM0_FMS) & 0x81u) == 0u");
    expect(state, !recorder.level[IRQ_FTM0], "!recorder.level[IRQ_FTM0]");
    expect_outputs(state, &timing, false, false);

    write_register(state, &timing, FTM0_SC, 8u);
    k22_timing_advance(&timing, 3u);
    expect_outputs(state, &timing, false, false);
    k22_timing_advance(&timing, 1u);
    expect_outputs(state, &timing, true, true);
}

static void test_automatic_fault_lifecycle(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 0xe0u, 0x40u, 1u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 3u);
    expect(state, (read_register(state, &timing, FTM0_FMS) & 0xa1u) == 0xa1u,
           "(read_register(state, &timing, FTM0_FMS) & 0xa1u) == 0xa1u");
    expect_outputs(state, &timing, false, false);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, false),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, false)");
    k22_timing_advance(&timing, 3u);
    expect(state, (read_register(state, &timing, FTM0_FMS) & 0x81u) == 0x81u,
           "(read_register(state, &timing, FTM0_FMS) & 0x81u) == 0x81u");
    write_register(state, &timing, FTM0_FMS, 0u);
    expect(state, (read_register(state, &timing, FTM0_FMS) & 0x81u) == 0u,
           "(read_register(state, &timing, FTM0_FMS) & 0x81u) == 0u");
    expect(state, !recorder.level[IRQ_FTM0], "!recorder.level[IRQ_FTM0]");
    expect_outputs(state, &timing, false, false);
    write_register(state, &timing, FTM0_SC, 8u);
    k22_timing_advance(&timing, 3u);
    expect_outputs(state, &timing, false, false);
    k22_timing_advance(&timing, 1u);
    expect(state, (read_register(state, &timing, FTM0_FMS) & 0x81u) == 0u,
           "(read_register(state, &timing, FTM0_FMS) & 0x81u) == 0u");
    expect(state, !recorder.level[IRQ_FTM0], "!recorder.level[IRQ_FTM0]");
    expect_outputs(state, &timing, true, true);
}

static void test_fault_modes_and_safe_polarity(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 0x20u, 0x40u, 1u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 3u);
    expect_outputs(state, &timing, false, true);

    initialize(state, &timing, &recorder, 0x40u, 0u, 1u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 3u);
    expect_outputs(state, &timing, true, true);
    expect(state, (read_register(state, &timing, FTM0_FMS) & 0x81u) == 0x81u,
           "(read_register(state, &timing, FTM0_FMS) & 0x81u) == 0x81u");

    initialize(state, &timing, &recorder, 0x40u, 0x40u, 1u);
    write_register(state, &timing, FTM0_POL, 1u);
    k22_timing_advance(&timing, 1u);
    expect_outputs(state, &timing, false, true);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 3u);
    expect_outputs(state, &timing, true, false);

    initialize(state, &timing, &recorder, 0u, 0x40u, 1u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 16u);
    expect(state, read_register(state, &timing, FTM0_FMS) == 0u,
           "read_register(state, &timing, FTM0_FMS) == 0u");

    initialize(state, &timing, &recorder, 0x40u, 0x40u, 0u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 16u);
    expect(state, read_register(state, &timing, FTM0_FMS) == 0u,
           "read_register(state, &timing, FTM0_FMS) == 0u");
}

static void test_fault_inputs_and_filter(TestState* state) {
    for (uint8_t input = 0u; input < 4u; input++) {
        K22Timing timing;
        IrqRecorder recorder = {0};
        initialize(state, &timing, &recorder, 0x40u, 0x40u, 1u << input);
        expect(state, k22_timing_set_ftm_fault(&timing, 0u, input, true),
               "k22_timing_set_ftm_fault(&timing, 0u, input, true)");
        k22_timing_advance(&timing, 3u);
        const uint32_t expected = 0xa0u | (1u << input);
        expect(state, read_register(state, &timing, FTM0_FMS) == expected,
               "read_register(state, &timing, FTM0_FMS) == expected");
    }

    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 0x40u, 0x40u, 0x211u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 5u);
    expect(state, read_register(state, &timing, FTM0_FMS) == 0u,
           "read_register(state, &timing, FTM0_FMS) == 0u");
    k22_timing_advance(&timing, 1u);
    expect(state, read_register(state, &timing, FTM0_FMS) == 0xa1u,
           "read_register(state, &timing, FTM0_FMS) == 0xa1u");

    initialize(state, &timing, &recorder, 0x40u, 0x40u, 0x11u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 2u);
    expect(state, read_register(state, &timing, FTM0_FMS) == 0u,
           "read_register(state, &timing, FTM0_FMS) == 0u");
    k22_timing_advance(&timing, 1u);
    expect(state, read_register(state, &timing, FTM0_FMS) == 0xa1u,
           "read_register(state, &timing, FTM0_FMS) == 0xa1u");

    initialize(state, &timing, &recorder, 0x40u, 0x40u, 0x211u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 5u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, false),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, false)");
    k22_timing_advance(&timing, 8u);
    expect(state, read_register(state, &timing, FTM0_FMS) == 0u,
           "read_register(state, &timing, FTM0_FMS) == 0u");

    initialize(state, &timing, &recorder, 0x40u, 0x40u, 1u);
    write_register(state, &timing, FTM0_FLTPOL, 1u);
    k22_timing_advance(&timing, 2u);
    expect(state, read_register(state, &timing, FTM0_FMS) == 0u,
           "read_register(state, &timing, FTM0_FMS) == 0u");
    k22_timing_advance(&timing, 1u);
    expect(state, read_register(state, &timing, FTM0_FMS) == 0xa1u,
           "read_register(state, &timing, FTM0_FMS) == 0xa1u");
}

static void test_flag_clearing_and_irq_control(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 0xc0u, 0x40u, 0x0fu);
    for (uint8_t input = 0u; input < 4u; input++)
        expect(state, k22_timing_set_ftm_fault(&timing, 0u, input, true),
               "k22_timing_set_ftm_fault(&timing, 0u, input, true)");
    k22_timing_advance(&timing, 3u);
    expect(state, read_register(state, &timing, FTM0_FMS) == 0xafu,
           "read_register(state, &timing, FTM0_FMS) == 0xafu");
    for (uint8_t input = 0u; input < 4u; input++)
        expect(state, k22_timing_set_ftm_fault(&timing, 0u, input, false),
               "k22_timing_set_ftm_fault(&timing, 0u, input, false)");
    k22_timing_advance(&timing, 3u);
    write_register(state, &timing, FTM0_FMS, 0x8eu);
    expect(state, (read_register(state, &timing, FTM0_FMS) & 0x8fu) == 0x8eu,
           "(read_register(state, &timing, FTM0_FMS) & 0x8fu) == 0x8eu");
    write_register(state, &timing, FTM0_FMS, 0u);
    expect(state, (read_register(state, &timing, FTM0_FMS) & 0x8fu) == 0u,
           "(read_register(state, &timing, FTM0_FMS) & 0x8fu) == 0u");

    initialize(state, &timing, &recorder, 0xc0u, 0x40u, 1u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 3u);
    expect(state, recorder.level[IRQ_FTM0], "recorder.level[IRQ_FTM0]");
    write_register(state, &timing, FTM0_MODE, 0x45u);
    expect(state, !recorder.level[IRQ_FTM0], "!recorder.level[IRQ_FTM0]");
    write_register(state, &timing, FTM0_MODE, 0xc5u);
    expect(state, recorder.level[IRQ_FTM0], "recorder.level[IRQ_FTM0]");
    expect(state, recorder.transitions[IRQ_FTM0] >= 3u,
           "recorder.transitions[IRQ_FTM0] >= 3u");
}

static void test_clear_sequence_and_retrigger(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 0x40u, 0x40u, 1u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 3u);
    expect(state, (timing.ftm[0].registers[8] & 0x81u) == 0x81u,
           "(timing.ftm[0].registers[8] & 0x81u) == 0x81u");
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, false),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, false)");
    k22_timing_advance(&timing, 3u);
    write_register(state, &timing, FTM0_FMS, 0u);
    expect(state, (timing.ftm[0].registers[8] & 0x81u) == 0x81u,
           "(timing.ftm[0].registers[8] & 0x81u) == 0x81u");
    expect(state, (read_register(state, &timing, FTM0_FMS) & 0x81u) == 0x81u,
           "(read_register(state, &timing, FTM0_FMS) & 0x81u) == 0x81u");

    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 3u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, false),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, false)");
    k22_timing_advance(&timing, 3u);
    write_register(state, &timing, FTM0_FMS, 0u);
    expect(state, (timing.ftm[0].registers[8] & 0x81u) == 0x81u,
           "(timing.ftm[0].registers[8] & 0x81u) == 0x81u");
    expect(state, (read_register(state, &timing, FTM0_FMS) & 0x81u) == 0x81u,
           "(read_register(state, &timing, FTM0_FMS) & 0x81u) == 0x81u");
    write_register(state, &timing, FTM0_FMS, 0u);
    expect(state, (timing.ftm[0].registers[8] & 0x81u) == 0u,
           "(timing.ftm[0].registers[8] & 0x81u) == 0u");
}

static void test_all_pairs_and_mode_disable(TestState* state) {
    for (uint8_t pair = 0u; pair < 4u; pair++) {
        K22Timing timing;
        IrqRecorder recorder = {0};
        const uint8_t first = (uint8_t)(pair * 2u);
        initialize(state, &timing, &recorder, 0x40u, 0x40u << (pair * 8u), 1u);
        const uint32_t software = (3u << first) | (3u << (first + 8u));
        write_register(state, &timing, FTM0_SWOCTRL, software);
        k22_timing_advance(&timing, 1u);
        expect(state, output(state, &timing, first), "output(state, &timing, first)");
        expect(state, output(state, &timing, (uint8_t)(first + 1u)),
               "output(state, &timing, (uint8_t)(first + 1u))");
        expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
               "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
        k22_timing_advance(&timing, 3u);
        expect(state, !output(state, &timing, first), "!output(state, &timing, first)");
        expect(state, !output(state, &timing, (uint8_t)(first + 1u)),
               "!output(state, &timing, (uint8_t)(first + 1u))");
    }

    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 0x40u, 0x40u, 1u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 3u);
    expect_outputs(state, &timing, false, false);
    write_register(state, &timing, FTM0_MODE, 5u);
    expect_outputs(state, &timing, true, true);
    expect(state, (read_register(state, &timing, FTM0_FMS) & 0x81u) == 0x81u,
           "(read_register(state, &timing, FTM0_FMS) & 0x81u) == 0x81u");
}

static void test_center_aligned_release(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 0x40u, 0x40u, 1u);
    write_register(state, &timing, FTM0_MOD, 2u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 3u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, false),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, false)");
    k22_timing_advance(&timing, 3u);
    expect(state, read_register(state, &timing, FTM0_FMS) == 0x81u,
           "read_register(state, &timing, FTM0_FMS) == 0x81u");
    write_register(state, &timing, FTM0_FMS, 0u);
    write_register(state, &timing, FTM0_SC, 0x28u);
    k22_timing_advance(&timing, 3u);
    expect_outputs(state, &timing, false, false);
    k22_timing_advance(&timing, 1u);
    expect_outputs(state, &timing, true, true);
}

static void test_bulk_advance_fault_order(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 0x60u, 0x40u, 1u);
    write_register(state, &timing, FTM0_MOD, 5u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, true),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, true)");
    k22_timing_advance(&timing, 3u);
    write_register(state, &timing, FTM0_SC, 8u);
    k22_timing_advance(&timing, 4u);
    expect(state, k22_timing_set_ftm_fault(&timing, 0u, 0u, false),
           "k22_timing_set_ftm_fault(&timing, 0u, 0u, false)");
    k22_timing_advance(&timing, 4u);
    expect_outputs(state, &timing, false, false);
    k22_timing_advance(&timing, 4u);
    expect_outputs(state, &timing, true, true);
}

static void test_invalid_fault_inputs(TestState* state) {
    K22Timing timing;
    IrqRecorder recorder = {0};
    initialize(state, &timing, &recorder, 0x40u, 0x40u, 1u);
    expect(state, !k22_timing_set_ftm_fault(NULL, 0u, 0u, true),
           "!k22_timing_set_ftm_fault(NULL, 0u, 0u, true)");
    expect(state, !k22_timing_set_ftm_fault(&timing, 4u, 0u, true),
           "!k22_timing_set_ftm_fault(&timing, 4u, 0u, true)");
    expect(state, !k22_timing_set_ftm_fault(&timing, 0u, 4u, true),
           "!k22_timing_set_ftm_fault(&timing, 0u, 4u, true)");
}

int main(void) {
    TestState state = {0};
    test_manual_fault_lifecycle(&state);
    test_automatic_fault_lifecycle(&state);
    test_fault_modes_and_safe_polarity(&state);
    test_fault_inputs_and_filter(&state);
    test_flag_clearing_and_irq_control(&state);
    test_clear_sequence_and_retrigger(&state);
    test_all_pairs_and_mode_disable(&state);
    test_center_aligned_release(&state);
    test_bulk_advance_fault_order(&state);
    test_invalid_fault_inputs(&state);
    return test_finish(&state);
}
