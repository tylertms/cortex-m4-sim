#include "device/kinetis_k22/timing/internal.h"

typedef struct {
    uint8_t edge;
    bool previous;
    bool high;
} LlwuEdgeCase;

static void test_invalid_state(TestState* state, K22Timing* timing) {
    K22Timing invalid = {0};
    K22Timing destination;
    uint32_t value = 0u;

    expect(state, !k22_timing_read(&invalid, 0u, 1u, &value),
           "k22_timing_read rejects an uninitialized state");
    expect(state, !k22_timing_read(timing, 0u, 1u, NULL),
           "k22_timing_read rejects a null destination");
    expect(state, !k22_timing_write(&invalid, 0u, 1u, 0u),
           "k22_timing_write rejects an uninitialized state");
    k22_timing_advance(&invalid, 1u);
    k22_timing_advance(timing, 0u);
    k22_timing_reset(&invalid, 0u, 0u);
    k22_timing_warm_reset(NULL, 0u, 0u);
    k22_timing_warm_reset(&invalid, 0u, 0u);
    expect(state, !k22_timing_trigger_low_voltage_warning(&invalid),
           "low-voltage warning rejects an uninitialized state");
    expect(state, !k22_timing_trigger_low_voltage_detect(&invalid),
           "low-voltage detect rejects an uninitialized state");
    expect(state, !k22_timing_set_llwu_pin(&invalid, 0u, false),
           "LLWU pin input rejects an uninitialized state");
    expect(state, !k22_timing_trigger_llwu_module(&invalid, 0u),
           "LLWU module input rejects an uninitialized state");
    k22_timing_set_cpu_sleeping(&invalid, true, true);
    expect(state, !k22_timing_set_ewm_input(&invalid, false),
           "EWM input rejects an uninitialized state");
    expect(state, !k22_timing_ewm_output(&invalid), "EWM output rejects an uninitialized state");
    expect(state, !k22_timing_copy(&destination, NULL, k22_timing_test_signals(NULL)),
           "timing copy rejects a null source");
    expect(state, !k22_timing_copy(&destination, &invalid, k22_timing_test_signals(NULL)),
           "timing copy rejects an uninitialized source");
    expect(state, k22_timing_bus_clock_hz(NULL) == 0u, "null timing state has no bus clock");
}

static void test_voltage_modes(TestState* state, K22Timing* timing) {
    const uint8_t modes[] = {1u, 2u, 0x80u, 4u};

    for (size_t index = 0u; index < sizeof(modes) / sizeof(modes[0]); index++) {
        timing->smc[3] = modes[index];
        expect(state, k22_timing_trigger_low_voltage_warning(timing),
               "low-voltage warning accepts each power mode");
        expect(state, k22_timing_trigger_low_voltage_detect(timing),
               "low-voltage detect accepts each power mode");
    }
}

static void test_llwu_edges(TestState* state, K22Timing* timing) {
    const LlwuEdgeCase cases[] = {
        {0u, false, false}, {1u, false, true}, {1u, true, false}, {1u, false, false},
        {2u, true, false},  {2u, false, true}, {2u, true, true},  {3u, false, true},
        {3u, true, false},  {3u, true, true},
    };
    const uint8_t pin = 15u;

    timing->smc[3] = 0x20u;
    timing->llwu[8] = 14u;
    timing->llwu[9] = 14u;
    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        timing->llwu[3] = (uint8_t)(cases[index].edge << 6u);
        timing->llwu_pin_level[pin] = cases[index].previous;
        expect(state, k22_timing_set_llwu_pin(timing, pin, cases[index].high),
               "LLWU accepts every edge transition");
    }

    timing->smc[3] = 1u;
    timing->llwu[4] = 1u;
    expect(state, k22_timing_trigger_llwu_module(timing, 0u),
           "LLWU ignores enabled modules outside low-leakage mode");
    timing->smc[3] = 0x20u;
    timing->llwu[4] = 0u;
    expect(state, k22_timing_trigger_llwu_module(timing, 0u),
           "LLWU ignores disabled modules in low-leakage mode");
}

static void enter_sleep(K22Timing* timing, uint8_t stop_mode, uint8_t protection) {
    timing->cpu_sleeping = false;
    timing->deep_sleeping = false;
    timing->smc_run_status = 1u;
    timing->smc[0] = protection;
    timing->smc[1] = stop_mode;
    k22_timing_set_cpu_sleeping(timing, true, true);
}

static void test_sleep_modes(K22Timing* timing) {
    enter_sleep(timing, 0u, 0u);
    enter_sleep(timing, 2u, 0u);
    enter_sleep(timing, 2u, 0x20u);
    enter_sleep(timing, 3u, 0u);
    enter_sleep(timing, 3u, 8u);
    enter_sleep(timing, 4u, 0u);
    enter_sleep(timing, 4u, 2u);
    enter_sleep(timing, 1u, 0u);

    timing->cpu_sleeping = false;
    timing->deep_sleeping = false;
    timing->smc_run_status = 4u;
    k22_timing_set_cpu_sleeping(timing, true, false);
    k22_timing_set_cpu_sleeping(timing, false, false);
    timing->smc[3] = 0x40u;
    k22_timing_set_cpu_sleeping(timing, false, false);
}

void k22_timing_test_test_api_boundaries(TestState* state, K22Timing* timing) {
    test_invalid_state(state, timing);
    test_voltage_modes(state, timing);
    test_llwu_edges(state, timing);
    test_sleep_modes(timing);
}
