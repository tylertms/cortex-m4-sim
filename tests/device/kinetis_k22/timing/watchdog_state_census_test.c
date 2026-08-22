#include "device/kinetis_k22/timing/support.h"

typedef struct {
    uint32_t random;
    uint32_t reads;
    uint32_t writes;
    uint32_t resets;
    uint64_t fingerprint;
} WatchdogCensus;

static uint32_t next_random(WatchdogCensus* census) {
    uint32_t value = census->random;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    census->random = value;
    return value;
}

static void mix(WatchdogCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static void reset_signal(void* context, uint8_t srs0, uint8_t srs1) {
    WatchdogCensus* census = context;
    census->resets++;
    mix(census, srs0 | ((uint32_t)srs1 << 8u));
}

static void randomize_state(K22Timing* timing, WatchdogCensus* census) {
    for (uint8_t index = 0u; index < 12u; index++) {
        timing->wdog[index] = (uint16_t)next_random(census);
        timing->wdog_pending[index] = (uint16_t)next_random(census);
    }
    const uint32_t first = next_random(census);
    const uint32_t second = next_random(census);
    timing->wdog_counter = first;
    timing->wdog_update_mask = second;
    timing->wdog_unlock_stage = (uint8_t)(first >> 24u) % 3u;
    timing->wdog_refresh_stage = (uint8_t)(second >> 24u) % 3u;
    timing->wdog_bus_cycles = ((uint64_t)first << 32u) | second;
    timing->wdog_sequence_deadline = timing->wdog_bus_cycles + (first & 31u);
    timing->wdog_update_ready = timing->wdog_bus_cycles + ((first >> 5u) & 31u);
    timing->wdog_update_deadline = timing->wdog_bus_cycles + ((first >> 10u) & 31u);
    timing->wdog_reset_deadline = timing->wdog_bus_cycles + ((first >> 15u) & 31u);
    timing->wdog_initial_unlock_required = (first & 1u) != 0u;
    timing->wdog_initial_debug_pause = (first & 2u) != 0u;
    timing->wdog_update_open = (first & 4u) != 0u;
    timing->wdog_update_written = (first & 8u) != 0u;
    timing->wdog_reset_pending = (first & 16u) != 0u;
    timing->debug_halted = (first & 32u) != 0u;
    timing->cpu_sleeping = (first & 64u) != 0u;
    timing->ewm_ctrl = (uint8_t)second;
    timing->ewm_cmpl = (uint8_t)(second >> 8u);
    timing->ewm_cmph = (uint8_t)(second >> 16u);
    timing->ewm_prescaler = (uint8_t)(second >> 24u);
    timing->ewm_service_stage = (uint8_t)(first >> 20u) % 3u;
    timing->ewm_control_written = (second & 1u) != 0u;
    timing->ewm_cmpl_written = (second & 2u) != 0u;
    timing->ewm_cmph_written = (second & 4u) != 0u;
    timing->ewm_prescaler_written = (second & 8u) != 0u;
    timing->ewm_input = (second & 16u) != 0u;
    timing->ewm_output = (second & 32u) != 0u;
    timing->ewm_service_paused = (second & 64u) != 0u;
    timing->ewm_counter = first >> 8u;
    timing->ewm_service_deadline = timing->wdog_bus_cycles + (second & 31u);
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    WatchdogCensus census = {UINT32_C(0x13198a2e), 0u, 0u, 0u, UINT64_C(14695981039346656037)};
    K22Timing timing;
    const K22TimingSignals signals = {.context = &census, .reset = reset_signal};
    expect(&state,
           k22_timing_init(&timing, k22_profile_get(K22_PROFILE_MK22FN1M012), 8000000u, 32768u,
                           signals),
           "initialize watchdog state census");
    for (uint32_t iteration = 0u; iteration < 100000u; iteration++) {
        randomize_state(&timing, &census);
        const uint32_t random = next_random(&census);
        const uint8_t size = (uint8_t)(random % 4u);
        const uint32_t wdog_address = WDOG_STCTRLH + ((random >> 8u) % 25u);
        const uint32_t ewm_address = EWM_CTRL + ((random >> 16u) % 7u);
        uint32_t value = UINT32_MAX;
        const bool wdog_read = k22_timing_read(&timing, wdog_address, size, &value);
        const bool wdog_write = k22_timing_write(&timing, wdog_address, size, random);
        const bool ewm_read = k22_timing_read(&timing, ewm_address, size, &value);
        const bool ewm_write = k22_timing_write(&timing, ewm_address, size, random >> 8u);
        k22_timing_watchdog_advance(&timing, random & 63u);
        k22_timing_advance(&timing, (random >> 6u) & 63u);
        census.reads += wdog_read + ewm_read;
        census.writes += wdog_write + ewm_write;
        mix(&census, wdog_read | (wdog_write << 1u) | (ewm_read << 2u) | (ewm_write << 3u));
        mix(&census, value);
        mix(&census, timing.wdog_counter ^ timing.ewm_counter);
    }
    expect(&state,
           census.reads == 53930u && census.writes == 47484u && census.resets == 60747u &&
               census.fingerprint == UINT64_C(11336035918486394776),
           "watchdog state census matches");
    return test_finish(&state);
}
