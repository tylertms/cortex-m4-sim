#include "device/kinetis_k22/timing/api.h"
#include "test.h"

enum {
    SIM_SCGC6 = 0x4004803cu,
    FTM0_SC = 0x40038000u,
    FTM0_CNT = 0x40038004u,
    FTM0_MOD = 0x40038008u,
    FTM0_C0SC = 0x4003800cu,
    FTM0_C0V = 0x40038010u,
    FTM0_C1V = 0x40038018u,
    FTM0_MODE = 0x40038054u,
    FTM0_COMBINE = 0x40038064u,
    FTM0_DEADTIME = 0x40038068u,
    FTM0_POL = 0x40038070u,
    FTM0_INVCTRL = 0x40038090u,
    FTM0_SWOCTRL = 0x40038094u,
};

static void write_register(TestState* state, K22Timing* timing, uint32_t address, uint32_t value) {
    expect(state, k22_timing_write(timing, address, 4u, value),
           "k22_timing_write(timing, address, 4u, value)");
}

static void initialize(TestState* state, K22Timing* timing) {
    const K22Profile* profile = k22_profile_get(K22_PROFILE_MK22FN51212);
    expect(state, k22_timing_init(timing, profile, 8000000u, 32768u, (K22TimingSignals){0}),
           "k22_timing_init(timing, profile, 8000000u, 32768u, (K22TimingSignals){0})");
    write_register(state, timing, SIM_SCGC6, timing->sim_scgc6 | (1u << 24u));
    write_register(state, timing, FTM0_MODE, 5u);
}

static bool output(TestState* state, K22Timing* timing, uint8_t channel) {
    bool high = false;
    expect(state, k22_timing_get_ftm_output(timing, 0u, channel, &high),
           "k22_timing_get_ftm_output(timing, 0u, channel, &high)");
    return high;
}

static void expect_pair(TestState* state, K22Timing* timing, bool first, bool second) {
    const bool first_output = output(state, timing, 0u);
    const bool second_output = output(state, timing, 1u);
    expect(state, first_output == first, "first_output == first");
    expect(state, second_output == second, "second_output == second");
    expect(state, !(first_output && second_output), "!(first_output && second_output)");
}

static void configure_pwm(TestState* state, K22Timing* timing, uint32_t combine, uint32_t deadtime,
                          uint16_t compare, uint16_t modulo) {
    initialize(state, timing);
    write_register(state, timing, FTM0_MOD, modulo);
    write_register(state, timing, FTM0_COMBINE, combine);
    write_register(state, timing, FTM0_DEADTIME, deadtime);
    write_register(state, timing, FTM0_C0SC, 0x28u);
    write_register(state, timing, FTM0_C0SC + 8u, 0x08u);
    write_register(state, timing, FTM0_C0V, 0u);
    write_register(state, timing, FTM0_C1V, compare);
    write_register(state, timing, FTM0_CNT, 0u);
    write_register(state, timing, FTM0_SC, 8u);
}

static void advance_and_expect(TestState* state, K22Timing* timing, uint32_t cycles, bool first,
                               bool second) {
    k22_timing_advance(timing, cycles);
    expect_pair(state, timing, first, second);
}

static void test_pair_transitions(TestState* state) {
    K22Timing timing;
    configure_pwm(state, &timing, 0x13u, 2u, 4u, 7u);
    expect(state, timing.ftm[0].registers[4] == 0x13u, "timing.ftm[0].registers[4] == 0x13u");
    expect(state, timing.ftm[0].registers[5] == 2u, "timing.ftm[0].registers[5] == 2u");
    expect(state, timing.ftm[0].channel_sc[0] == 0x28u, "timing.ftm[0].channel_sc[0] == 0x28u");
    k22_timing_advance(&timing, 1u);
    expect(state, timing.ftm[0].channel_deadtime_remaining[0] == 2u,
           "timing.ftm[0].channel_deadtime_remaining[0] == 2u");
    expect(state, !output(state, &timing, 0u) && !output(state, &timing, 1u),
           "!output(state, &timing, 0u) && !output(state, &timing, 1u)");
    k22_timing_advance(&timing, 1u);
    expect(state, timing.ftm[0].channel_deadtime_remaining[0] == 1u,
           "timing.ftm[0].channel_deadtime_remaining[0] == 1u");
    expect(state, !output(state, &timing, 0u) && !output(state, &timing, 1u),
           "!output(state, &timing, 0u) && !output(state, &timing, 1u)");
    k22_timing_advance(&timing, 1u);
    expect(state, timing.ftm[0].channel_deadtime_remaining[0] == 0u,
           "timing.ftm[0].channel_deadtime_remaining[0] == 0u");
    expect(state, timing.ftm[0].channel_deadtime_output[0],
           "timing.ftm[0].channel_deadtime_output[0]");
    expect(state, output(state, &timing, 0u) && !output(state, &timing, 1u),
           "output(state, &timing, 0u) && !output(state, &timing, 1u)");
    k22_timing_advance(&timing, 1u);
    expect(state, !output(state, &timing, 0u) && !output(state, &timing, 1u),
           "!output(state, &timing, 0u) && !output(state, &timing, 1u)");
    k22_timing_advance(&timing, 1u);
    expect(state, !output(state, &timing, 0u) && !output(state, &timing, 1u),
           "!output(state, &timing, 0u) && !output(state, &timing, 1u)");
    k22_timing_advance(&timing, 1u);
    expect(state, !output(state, &timing, 0u) && output(state, &timing, 1u),
           "!output(state, &timing, 0u) && output(state, &timing, 1u)");
    k22_timing_advance(&timing, 1u);
    expect(state, !output(state, &timing, 0u) && output(state, &timing, 1u),
           "!output(state, &timing, 0u) && output(state, &timing, 1u)");
    k22_timing_advance(&timing, 1u);
    expect(state, !output(state, &timing, 0u) && !output(state, &timing, 1u),
           "!output(state, &timing, 0u) && !output(state, &timing, 1u)");
}

static void test_deadtime_dividers(TestState* state) {
    static const uint32_t register_values[4] = {2u, 0x42u, 0x82u, 0xc2u};
    static const uint32_t activation_cycles[4] = {3u, 3u, 8u, 32u};
    for (uint8_t index = 0u; index < 4u; index++) {
        K22Timing timing;
        configure_pwm(state, &timing, 0x13u, register_values[index], 63u, 127u);
        advance_and_expect(state, &timing, activation_cycles[index] - 1u, false, false);
        advance_and_expect(state, &timing, 1u, true, false);
    }
}

static void test_polarity_and_inversion(TestState* state) {
    K22Timing timing;
    configure_pwm(state, &timing, 0x13u, 2u, 4u, 7u);
    write_register(state, &timing, FTM0_INVCTRL, 1u);
    advance_and_expect(state, &timing, 1u, false, false);
    advance_and_expect(state, &timing, 2u, false, true);

    configure_pwm(state, &timing, 0x13u, 2u, 4u, 7u);
    write_register(state, &timing, FTM0_INVCTRL, 1u);
    write_register(state, &timing, FTM0_POL, 3u);
    k22_timing_advance(&timing, 1u);
    expect(state, output(state, &timing, 0u), "output(state, &timing, 0u)");
    expect(state, output(state, &timing, 1u), "output(state, &timing, 1u)");
    k22_timing_advance(&timing, 2u);
    expect(state, output(state, &timing, 0u), "output(state, &timing, 0u)");
    expect(state, !output(state, &timing, 1u), "!output(state, &timing, 1u)");
}

static void test_disabled_modes(TestState* state) {
    K22Timing timing;
    configure_pwm(state, &timing, 0x13u, 0u, 4u, 7u);
    advance_and_expect(state, &timing, 1u, true, false);

    configure_pwm(state, &timing, 3u, 2u, 4u, 7u);
    advance_and_expect(state, &timing, 1u, true, false);

    configure_pwm(state, &timing, 0x11u, 2u, 4u, 7u);
    k22_timing_advance(&timing, 1u);
    expect(state, output(state, &timing, 0u), "output(state, &timing, 0u)");
    expect(state, output(state, &timing, 1u), "output(state, &timing, 1u)");
    expect(state, timing.ftm[0].channel_deadtime_remaining[0] == 0u,
           "timing.ftm[0].channel_deadtime_remaining[0] == 0u");

    configure_pwm(state, &timing, 0x17u, 2u, 4u, 7u);
    k22_timing_advance(&timing, 1u);
    expect(state, timing.ftm[0].channel_deadtime_remaining[0] == 0u,
           "timing.ftm[0].channel_deadtime_remaining[0] == 0u");

    initialize(state, &timing);
    write_register(state, &timing, FTM0_COMBINE, 0x12u);
    write_register(state, &timing, FTM0_DEADTIME, 2u);
    write_register(state, &timing, FTM0_MOD, 7u);
    write_register(state, &timing, FTM0_C0SC, 0x1cu);
    write_register(state, &timing, FTM0_C0V, 2u);
    write_register(state, &timing, FTM0_SC, 8u);
    k22_timing_advance(&timing, 2u);
    expect(state, output(state, &timing, 0u), "output(state, &timing, 0u)");
}

static void test_stopped_counter_and_all_pairs(TestState* state) {
    for (uint8_t pair = 0u; pair < 4u; pair++) {
        K22Timing timing;
        initialize(state, &timing);
        const uint8_t first = (uint8_t)(pair * 2u);
        const uint8_t shift = (uint8_t)(pair * 8u);
        write_register(state, &timing, FTM0_COMBINE, 0x12u << shift);
        write_register(state, &timing, FTM0_DEADTIME, 1u);
        write_register(state, &timing, FTM0_C0SC + first * 8u, 0x28u);
        write_register(state, &timing, FTM0_C0SC + (first + 1u) * 8u, 0x08u);
        const uint32_t control = (3u << first) | (1u << (first + 8u));
        write_register(state, &timing, FTM0_SWOCTRL, control);
        k22_timing_advance(&timing, 1u);
        expect(state, !output(state, &timing, first), "!output(state, &timing, first)");
        expect(state, !output(state, &timing, (uint8_t)(first + 1u)),
               "!output(state, &timing, (uint8_t)(first + 1u))");
        k22_timing_advance(&timing, 1u);
        expect(state, output(state, &timing, first), "output(state, &timing, first)");
        expect(state, !output(state, &timing, (uint8_t)(first + 1u)),
               "!output(state, &timing, (uint8_t)(first + 1u))");
    }
}

static void test_duty_suppression_and_aggregate_steps(TestState* state) {
    K22Timing aggregate;
    K22Timing stepped;
    configure_pwm(state, &aggregate, 0x13u, 4u, 3u, 7u);
    configure_pwm(state, &stepped, 0x13u, 4u, 3u, 7u);
    k22_timing_advance(&aggregate, 64u);
    for (uint8_t cycle = 0u; cycle < 64u; cycle++)
        k22_timing_advance(&stepped, 1u);
    for (uint8_t channel = 0u; channel < 2u; channel++)
        expect(state, output(state, &aggregate, channel) == output(state, &stepped, channel),
               "output(state, &aggregate, channel) == output(state, &stepped, channel)");
    expect(state,
           aggregate.ftm[0].channel_deadtime_remaining[0] ==
               stepped.ftm[0].channel_deadtime_remaining[0],
           "aggregate.ftm[0].channel_deadtime_remaining[0] == "
           "stepped.ftm[0].channel_deadtime_remaining[0]");
    expect(state,
           aggregate.ftm[0].channel_deadtime_remaining[1] ==
               stepped.ftm[0].channel_deadtime_remaining[1],
           "aggregate.ftm[0].channel_deadtime_remaining[1] == "
           "stepped.ftm[0].channel_deadtime_remaining[1]");
    for (uint8_t cycle = 0u; cycle < 32u; cycle++) {
        k22_timing_advance(&aggregate, 1u);
        expect(state, !output(state, &aggregate, 0u), "!output(state, &aggregate, 0u)");
    }

    K22Timing short_second;
    configure_pwm(state, &short_second, 0x13u, 4u, 6u, 7u);
    for (uint8_t cycle = 0u; cycle < 32u; cycle++) {
        k22_timing_advance(&short_second, 1u);
        expect(state, !output(state, &short_second, 1u), "!output(state, &short_second, 1u)");
    }
}

int main(void) {
    TestState state = {0};
    test_pair_transitions(&state);
    test_deadtime_dividers(&state);
    test_polarity_and_inversion(&state);
    test_disabled_modes(&state);
    test_stopped_counter_and_all_pairs(&state);
    test_duty_suppression_and_aggregate_steps(&state);
    return test_finish(&state);
}
