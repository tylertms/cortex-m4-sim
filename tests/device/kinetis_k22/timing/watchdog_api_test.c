#include "device/kinetis_k22/timing/api.h"

#include <stdint.h>

#include "test.h"

enum {
    WDOG_STCTRLH = 0x40052000u,
    WDOG_STCTRLL = 0x40052002u,
    WDOG_TOVALH = 0x40052004u,
    WDOG_TOVALL = 0x40052006u,
    WDOG_WINH = 0x40052008u,
    WDOG_WINL = 0x4005200au,
    WDOG_REFRESH = 0x4005200cu,
    WDOG_UNLOCK = 0x4005200eu,
    WDOG_TMROUTH = 0x40052010u,
    WDOG_TMROUTL = 0x40052012u,
    WDOG_RSTCNT = 0x40052014u,
    WDOG_PRESC = 0x40052016u,
    EWM_CTRL = 0x40061000u,
    EWM_SERV = 0x40061001u,
    EWM_CMPL = 0x40061002u,
    EWM_CMPH = 0x40061003u,
    EWM_CLKPRESCALER = 0x40061005u,
};

typedef struct {
    bool irq;
    uint32_t resets;
    uint8_t srs0;
    uint8_t srs1;
} Observations;

static void irq_signal(void* context, uint8_t irq, bool asserted) {
    if (irq == 22u)
        ((Observations*)context)->irq = asserted;
}

static void reset_signal(void* context, uint8_t srs0, uint8_t srs1) {
    Observations* observations = context;
    observations->resets++;
    observations->srs0 = srs0;
    observations->srs1 = srs1;
}

static K22Timing make_timing(TestState* state, Observations* observations) {
    K22Timing timing;
    const K22TimingSignals signals = {
        .context = observations,
        .irq = irq_signal,
        .reset = reset_signal,
    };
    expect(state,
           k22_timing_init(&timing, k22_profile_get(K22_PROFILE_MK22FN51212), 8000000u, 32768u,
                           signals),
           "k22_timing_init(&timing, k22_profile_get(K22_PROFILE_MK22FN51212), 8000000u, "
           "32768u, signals)");
    return timing;
}

static void write_register(TestState* state, K22Timing* timing, uint32_t address, uint32_t value) {
    const uint8_t size = address >= EWM_CTRL ? 1u : 2u;
    expect(state, k22_timing_write(timing, address, size, value),
           "k22_timing_write(timing, address, size, value)");
}

static uint32_t read_register(TestState* state, K22Timing* timing, uint32_t address) {
    uint32_t value = UINT32_MAX;
    const uint8_t size = address >= EWM_CTRL ? 1u : 2u;
    expect(state, k22_timing_read(timing, address, size, &value),
           "k22_timing_read(timing, address, size, &value)");
    return value;
}

static void write_byte(TestState* state, K22Timing* timing, uint32_t address, uint8_t value) {
    expect(state, k22_timing_write(timing, address, 1u, value),
           "k22_timing_write(timing, address, 1u, value)");
}

static uint8_t read_byte(TestState* state, K22Timing* timing, uint32_t address) {
    uint32_t value = UINT32_MAX;
    expect(state, k22_timing_read(timing, address, 1u, &value),
           "k22_timing_read(timing, address, 1u, &value)");
    return (uint8_t)value;
}

static uint32_t core_cycles_for_bus_cycles(const K22Timing* timing, uint32_t bus_cycles) {
    return (uint32_t)(((uint64_t)bus_cycles * timing->core_clock_hz + timing->bus_clock_hz - 1u) /
                      timing->bus_clock_hz);
}

static uint32_t core_cycles_for_lpo_cycles(const K22Timing* timing, uint32_t lpo_cycles) {
    return (uint32_t)(((uint64_t)lpo_cycles * timing->core_clock_hz + timing->lpo_hz - 1u) /
                      timing->lpo_hz);
}

static void advance_bus(K22Timing* timing, uint32_t bus_cycles) {
    k22_timing_advance(timing, core_cycles_for_bus_cycles(timing, bus_cycles));
}

static void unlock(TestState* state, K22Timing* timing) {
    write_register(state, timing, WDOG_UNLOCK, 0xc520u);
    write_register(state, timing, WDOG_UNLOCK, 0xd928u);
}

static void configure(TestState* state, K22Timing* timing, uint32_t timeout, uint32_t window,
                      uint16_t control) {
    unlock(state, timing);
    write_register(state, timing, WDOG_TOVALH, timeout >> 16u);
    write_register(state, timing, WDOG_TOVALL, timeout);
    write_register(state, timing, WDOG_WINH, window >> 16u);
    write_register(state, timing, WDOG_WINL, window);
    write_register(state, timing, WDOG_PRESC, 0u);
    write_register(state, timing, WDOG_STCTRLH, control);
    advance_bus(timing, 260u);
}

static void disable_watchdog(K22Timing* timing) {
    timing->wdog[0] = 0u;
    timing->wdog_pending[0] = 0u;
    timing->wdog_initial_unlock_required = false;
    timing->wdog_update_open = false;
    timing->wdog_reset_pending = false;
}

static void test_configuration_windows(TestState* state) {
    Observations observations = {0};
    K22Timing timing = make_timing(state, &observations);
    advance_bus(&timing, 256u);
    expect(state, observations.resets == 0u, "observations.resets == 0u");
    advance_bus(&timing, 1u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");
    expect(state, observations.srs0 == 0x20u, "observations.srs0 == 0x20u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    write_register(state, &timing, WDOG_UNLOCK, 0xc520u);
    advance_bus(&timing, 19u);
    write_register(state, &timing, WDOG_UNLOCK, 0xd928u);
    write_register(state, &timing, WDOG_TOVALH, 0u);
    write_register(state, &timing, WDOG_TOVALL, 10u);
    write_register(state, &timing, WDOG_STCTRLH, 0x11u);
    expect(state, read_register(state, &timing, WDOG_TOVALL) == 0x4b4cu,
           "read_register(state, &timing, WDOG_TOVALL) == 0x4b4cu");
    advance_bus(&timing, 260u);
    expect(state, observations.resets == 0u, "observations.resets == 0u");
    expect(state, read_register(state, &timing, WDOG_TOVALL) == 10u,
           "read_register(state, &timing, WDOG_TOVALL) == 10u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    write_register(state, &timing, WDOG_UNLOCK, 0xc520u);
    advance_bus(&timing, 20u);
    write_register(state, &timing, WDOG_UNLOCK, 0xd928u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    unlock(state, &timing);
    advance_bus(&timing, 257u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");
}

static void test_refresh_and_interrupt(TestState* state) {
    Observations observations = {0};
    K22Timing timing = make_timing(state, &observations);
    configure(state, &timing, 100u, 20u, 0x19u);
    k22_timing_watchdog_advance(&timing, 20u);
    write_register(state, &timing, WDOG_REFRESH, 0xa602u);
    advance_bus(&timing, 19u);
    write_register(state, &timing, WDOG_REFRESH, 0xb480u);
    expect(state, timing.wdog_counter == 0u, "timing.wdog_counter == 0u");
    expect(state, observations.resets == 0u, "observations.resets == 0u");
    k22_timing_watchdog_advance(&timing, 19u);
    write_register(state, &timing, WDOG_REFRESH, 0xa602u);
    write_register(state, &timing, WDOG_REFRESH, 0xb480u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    configure(state, &timing, 100u, 0u, 4u);
    write_register(state, &timing, WDOG_REFRESH, 0u);
    expect(state, observations.irq, "observations.irq");
    expect(state, (read_register(state, &timing, WDOG_STCTRLL) & 0x8000u) != 0u,
           "(read_register(state, &timing, WDOG_STCTRLL) & 0x8000u) != 0u");
    write_register(state, &timing, WDOG_STCTRLL, 0x8000u);
    expect(state, !observations.irq, "!observations.irq");
    const uint32_t before_reset = (uint32_t)(timing.wdog_reset_deadline - timing.wdog_bus_cycles);
    advance_bus(&timing, before_reset - 1u);
    expect(state, observations.resets == 0u, "observations.resets == 0u");
    advance_bus(&timing, 1u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");
    expect(state, read_register(state, &timing, WDOG_RSTCNT) == 1u,
           "read_register(state, &timing, WDOG_RSTCNT) == 1u");
    write_register(state, &timing, WDOG_RSTCNT, 1u);
    expect(state, read_register(state, &timing, WDOG_RSTCNT) == 0u,
           "read_register(state, &timing, WDOG_RSTCNT) == 0u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    configure(state, &timing, 100u, 0u, 4u);
    write_register(state, &timing, WDOG_REFRESH, 0u);
    write_register(state, &timing, WDOG_REFRESH, 0u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    configure(state, &timing, 100u, 0u, 1u);
    write_register(state, &timing, WDOG_REFRESH, 0xa602u);
    advance_bus(&timing, 21u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");
}

static void test_pause_and_byte_modes(TestState* state) {
    Observations observations = {0};
    K22Timing timing = make_timing(state, &observations);
    configure(state, &timing, 2u, 0u, 1u);
    k22_timing_set_cpu_sleeping(&timing, true, false);
    k22_timing_watchdog_advance(&timing, 2u);
    expect(state, observations.resets == 0u, "observations.resets == 0u");
    k22_timing_set_cpu_sleeping(&timing, true, true);
    k22_timing_watchdog_advance(&timing, 2u);
    expect(state, observations.resets == 0u, "observations.resets == 0u");
    k22_timing_set_cpu_sleeping(&timing, false, false);
    k22_timing_set_debug_halted(&timing, true);
    k22_timing_watchdog_advance(&timing, 2u);
    expect(state, observations.resets == 0u, "observations.resets == 0u");
    k22_timing_set_debug_halted(&timing, false);
    k22_timing_watchdog_advance(&timing, 2u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    configure(state, &timing, 2u, 0u, 0x41u);
    k22_timing_set_cpu_sleeping(&timing, true, true);
    k22_timing_watchdog_advance(&timing, 2u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    configure(state, &timing, 0x11223344u, 0u, 0x0c01u);
    k22_timing_watchdog_advance(&timing, 0x43u);
    expect(state, observations.resets == 0u, "observations.resets == 0u");
    k22_timing_watchdog_advance(&timing, 1u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");
}

static void test_byte_access_and_control(TestState* state) {
    Observations observations = {0};
    K22Timing timing = make_timing(state, &observations);
    write_byte(state, &timing, WDOG_UNLOCK, 0x20u);
    write_byte(state, &timing, WDOG_UNLOCK + 1u, 0xc5u);
    write_byte(state, &timing, WDOG_UNLOCK, 0x28u);
    write_byte(state, &timing, WDOG_UNLOCK + 1u, 0xd9u);
    write_byte(state, &timing, WDOG_TOVALL, 0x34u);
    write_byte(state, &timing, WDOG_TOVALL + 1u, 0x12u);
    advance_bus(&timing, 260u);
    expect(state, read_register(state, &timing, WDOG_TOVALL) == 0x1234u,
           "read_register(state, &timing, WDOG_TOVALL) == 0x1234u");
    k22_timing_watchdog_advance(&timing, 20u);
    write_byte(state, &timing, WDOG_REFRESH, 0x02u);
    write_byte(state, &timing, WDOG_REFRESH + 1u, 0xa6u);
    write_byte(state, &timing, WDOG_REFRESH, 0x80u);
    write_byte(state, &timing, WDOG_REFRESH + 1u, 0xb4u);
    expect(state, timing.wdog_counter == 0u, "timing.wdog_counter == 0u");

    k22_timing_watchdog_advance(&timing, 0x123u);
    expect(state, read_register(state, &timing, WDOG_TMROUTH) == 0u,
           "read_register(state, &timing, WDOG_TMROUTH) == 0u");
    expect(state, read_register(state, &timing, WDOG_TMROUTL) == 0x123u,
           "read_register(state, &timing, WDOG_TMROUTL) == 0x123u");
    expect(state, read_byte(state, &timing, WDOG_TMROUTL) == 0x23u,
           "read_byte(state, &timing, WDOG_TMROUTL) == 0x23u");
    expect(state, read_byte(state, &timing, WDOG_TMROUTL + 1u) == 1u,
           "read_byte(state, &timing, WDOG_TMROUTL + 1u) == 1u");
    write_register(state, &timing, WDOG_TMROUTL, 0u);
    expect(state, read_register(state, &timing, WDOG_TMROUTL) == 0x123u,
           "read_register(state, &timing, WDOG_TMROUTL) == 0x123u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    k22_timing_set_debug_halted(&timing, true);
    advance_bus(&timing, 1000u);
    expect(state, observations.resets == 0u, "observations.resets == 0u");
    k22_timing_set_debug_halted(&timing, false);
    advance_bus(&timing, 256u);
    expect(state, observations.resets == 0u, "observations.resets == 0u");
    advance_bus(&timing, 1u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    unlock(state, &timing);
    write_register(state, &timing, WDOG_TOVALH, 0u);
    write_register(state, &timing, WDOG_TOVALL, 0x100u);
    write_register(state, &timing, WDOG_PRESC, UINT16_MAX);
    write_register(state, &timing, WDOG_STCTRLH, UINT16_MAX);
    advance_bus(&timing, 260u);
    expect(state, read_register(state, &timing, WDOG_STCTRLH) == 0x7cffu,
           "read_register(state, &timing, WDOG_STCTRLH) == 0x7cffu");
    expect(state, read_register(state, &timing, WDOG_PRESC) == 0x0700u,
           "read_register(state, &timing, WDOG_PRESC) == 0x0700u");
    expect(state, read_register(state, &timing, WDOG_STCTRLL) == 1u,
           "read_register(state, &timing, WDOG_STCTRLL) == 1u");
    write_byte(state, &timing, WDOG_STCTRLL + 1u, 0x80u);
    expect(state, (read_register(state, &timing, WDOG_STCTRLL) & 0x8000u) == 0u,
           "(read_register(state, &timing, WDOG_STCTRLL) & 0x8000u) == 0u");
    write_byte(state, &timing, WDOG_RSTCNT, 1u);
    expect(state, read_register(state, &timing, WDOG_RSTCNT) == 0u,
           "read_register(state, &timing, WDOG_RSTCNT) == 0u");
    k22_timing_watchdog_advance(&timing, 0x44u);
    expect(state, !observations.irq, "!observations.irq");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    configure(state, &timing, 100u, 0u, 1u);
    write_byte(state, &timing, WDOG_REFRESH, 0u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    write_byte(state, &timing, WDOG_UNLOCK, 0u);
    expect(state, observations.resets == 1u, "observations.resets == 1u");
}

static void configure_ewm(TestState* state, K22Timing* timing, uint8_t low, uint8_t high,
                          uint8_t control) {
    disable_watchdog(timing);
    write_register(state, timing, EWM_CMPL, low);
    write_register(state, timing, EWM_CMPH, high);
    write_register(state, timing, EWM_CLKPRESCALER, 0u);
    write_register(state, timing, EWM_CTRL, control);
}

static void service_ewm(TestState* state, K22Timing* timing) {
    write_register(state, timing, EWM_SERV, 0xb4u);
    write_register(state, timing, EWM_SERV, 0x2cu);
}

static void test_external_watchdog(TestState* state) {
    Observations observations = {0};
    K22Timing timing = make_timing(state, &observations);
    expect(state, k22_timing_ewm_output(&timing), "k22_timing_ewm_output(&timing)");
    expect(state, k22_timing_set_ewm_input(&timing, true),
           "k22_timing_set_ewm_input(&timing, true)");
    configure_ewm(state, &timing, 1u, 4u, 0x0du);
    expect(state, !k22_timing_ewm_output(&timing), "!k22_timing_ewm_output(&timing)");
    expect(state, !k22_timing_write(&timing, EWM_CMPL, 1u, 2u),
           "!k22_timing_write(&timing, EWM_CMPL, 1u, 2u)");
    k22_timing_advance(&timing, core_cycles_for_lpo_cycles(&timing, 2u));
    expect(state, timing.ewm_counter == 2u, "timing.ewm_counter == 2u");
    expect(state, !k22_timing_ewm_output(&timing), "!k22_timing_ewm_output(&timing)");
    service_ewm(state, &timing);
    expect(state, !k22_timing_ewm_output(&timing), "!k22_timing_ewm_output(&timing)");
    expect(state, timing.ewm_counter == 0u, "timing.ewm_counter == 0u");
    write_register(state, &timing, EWM_SERV, 0u);
    expect(state, !k22_timing_ewm_output(&timing), "!k22_timing_ewm_output(&timing)");
    k22_timing_advance(&timing, core_cycles_for_lpo_cycles(&timing, 4u));
    expect(state, k22_timing_ewm_output(&timing), "k22_timing_ewm_output(&timing)");
    expect(state, observations.irq, "observations.irq");
    write_register(state, &timing, EWM_CTRL, 5u);
    expect(state, !observations.irq, "!observations.irq");
    expect(state, observations.resets == 0u, "observations.resets == 0u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    expect(state, k22_timing_set_ewm_input(&timing, true),
           "k22_timing_set_ewm_input(&timing, true)");
    configure_ewm(state, &timing, 1u, 4u, 0x0du);
    k22_timing_advance(&timing, core_cycles_for_lpo_cycles(&timing, 1u));
    service_ewm(state, &timing);
    expect(state, k22_timing_ewm_output(&timing), "k22_timing_ewm_output(&timing)");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    expect(state, k22_timing_set_ewm_input(&timing, true),
           "k22_timing_set_ewm_input(&timing, true)");
    configure_ewm(state, &timing, 1u, 4u, 0x0fu);
    k22_timing_advance(&timing, core_cycles_for_lpo_cycles(&timing, 2u));
    service_ewm(state, &timing);
    expect(state, k22_timing_ewm_output(&timing), "k22_timing_ewm_output(&timing)");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    configure_ewm(state, &timing, 1u, 100u, 1u);
    k22_timing_advance(&timing, core_cycles_for_lpo_cycles(&timing, 2u));
    write_register(state, &timing, EWM_SERV, 0xb4u);
    advance_bus(&timing, 15u);
    write_register(state, &timing, EWM_SERV, 0x2cu);
    expect(state, !k22_timing_ewm_output(&timing), "!k22_timing_ewm_output(&timing)");
    expect(state, timing.ewm_counter == 0u, "timing.ewm_counter == 0u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    configure_ewm(state, &timing, 1u, 100u, 1u);
    k22_timing_advance(&timing, core_cycles_for_lpo_cycles(&timing, 2u));
    write_register(state, &timing, EWM_SERV, 0xb4u);
    advance_bus(&timing, 16u);
    write_register(state, &timing, EWM_SERV, 0x2cu);
    expect(state, !k22_timing_ewm_output(&timing), "!k22_timing_ewm_output(&timing)");
    expect(state, timing.ewm_counter == 2u, "timing.ewm_counter == 2u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    configure_ewm(state, &timing, 1u, 4u, 1u);
    k22_timing_set_cpu_sleeping(&timing, true, false);
    k22_timing_advance(&timing, core_cycles_for_lpo_cycles(&timing, 4u));
    expect(state, !k22_timing_ewm_output(&timing), "!k22_timing_ewm_output(&timing)");
    expect(state, timing.ewm_counter == 0u, "timing.ewm_counter == 0u");
    k22_timing_set_cpu_sleeping(&timing, false, false);
    k22_timing_advance(&timing, core_cycles_for_lpo_cycles(&timing, 4u));
    expect(state, k22_timing_ewm_output(&timing), "k22_timing_ewm_output(&timing)");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    configure_ewm(state, &timing, 1u, 100u, 1u);
    k22_timing_advance(&timing, core_cycles_for_lpo_cycles(&timing, 2u));
    write_register(state, &timing, EWM_SERV, 0xb4u);
    advance_bus(&timing, 5u);
    k22_timing_set_cpu_sleeping(&timing, true, true);
    advance_bus(&timing, 100u);
    k22_timing_set_cpu_sleeping(&timing, false, false);
    advance_bus(&timing, 10u);
    write_register(state, &timing, EWM_SERV, 0x2cu);
    expect(state, !k22_timing_ewm_output(&timing), "!k22_timing_ewm_output(&timing)");
    expect(state, timing.ewm_counter == 0u, "timing.ewm_counter == 0u");

    observations = (Observations){0};
    k22_timing_reset(&timing, 0x82u, 0u);
    configure_ewm(state, &timing, 1u, 4u, 1u);
    k22_timing_set_debug_halted(&timing, true);
    k22_timing_advance(&timing, core_cycles_for_lpo_cycles(&timing, 4u));
    expect(state, k22_timing_ewm_output(&timing), "k22_timing_ewm_output(&timing)");
    expect(state, !k22_timing_set_ewm_input(NULL, false), "!k22_timing_set_ewm_input(NULL, false)");
    expect(state, !k22_timing_ewm_output(NULL), "!k22_timing_ewm_output(NULL)");
}

int main(void) {
    TestState state = {0};
    test_configuration_windows(&state);
    test_refresh_and_interrupt(&state);
    test_pause_and_byte_modes(&state);
    test_byte_access_and_control(&state);
    test_external_watchdog(&state);
    return test_finish(&state);
}
