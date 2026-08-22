#include "internal.h"

static uint32_t wdog_timeout(const K22Timing* timing) {
    return ((uint32_t)timing->wdog[2] << 16u) | timing->wdog[3];
}

void k22_timing_internal_signal_reset(K22Timing* timing, uint8_t srs0, uint8_t srs1) {
    const uint64_t generation = timing->reset_generation;
    if (timing->signals.reset != NULL) {
        timing->signals.reset(timing->signals.context, srs0, srs1);
    }
    if (timing->reset_generation == generation)
        k22_timing_warm_reset(timing, srs0, srs1);
}

static void update_watchdog_irq(const K22Timing* timing) {
    const bool watchdog = (timing->wdog[1] & 0x8000u) != 0u;
    const bool external = timing->ewm_output && (timing->ewm_ctrl & 8u) != 0u;
    k22_timing_internal_set_irq(timing, IRQ_WDOG_EWM, watchdog || external);
}

static bool wdog_exception(K22Timing* timing) {
    timing->wdog_counter = 0u;
    if ((timing->wdog[0] & 4u) == 0u) {
        k22_timing_internal_signal_reset(timing, 0x20u, 0u);
        return true;
    }
    if (timing->wdog_reset_pending) {
        k22_timing_internal_signal_reset(timing, 0x20u, 0u);
        return true;
    }
    timing->wdog[1] |= 0x8000u;
    timing->wdog_reset_pending = true;
    timing->wdog_reset_deadline = timing->wdog_bus_cycles + 256u;
    update_watchdog_irq(timing);
    return false;
}

static void wdog_apply_update(K22Timing* timing) {
    const uint16_t immediate = 0x00e4u;
    const uint16_t disabled_test = timing->wdog[0] & 0x4000u;
    for (uint8_t byte = 0u; byte < 24u; byte++) {
        if ((timing->wdog_update_mask & (UINT32_C(1) << byte)) == 0u)
            continue;
        const uint8_t index = byte / 2u;
        const uint16_t mask = byte % 2u == 0u ? 0x00ffu : 0xff00u;
        timing->wdog[index] =
            (timing->wdog[index] & (uint16_t)~mask) | (timing->wdog_pending[index] & mask);
    }
    timing->wdog[0] =
        (timing->wdog[0] & immediate) | (timing->wdog_pending[0] & (uint16_t)~immediate);
    timing->wdog[0] = (timing->wdog[0] & 0x7cffu) | disabled_test;
    timing->wdog[1] = (timing->wdog[1] & 0x8000u) | 1u;
    timing->wdog[11] &= 0x0700u;
    timing->wdog_update_open = false;
    timing->wdog_update_mask = 0u;
    timing->wdog_counter = 0u;
    timing->wdog_remainder = 0u;
}

static bool wdog_process_bus_time(K22Timing* timing) {
    if (timing->wdog_reset_pending && timing->wdog_bus_cycles >= timing->wdog_reset_deadline) {
        k22_timing_internal_signal_reset(timing, 0x20u, 0u);
        return true;
    }
    if (timing->wdog_unlock_stage == 1u &&
        timing->wdog_bus_cycles > timing->wdog_sequence_deadline) {
        timing->wdog_unlock_stage = 0u;
        return wdog_exception(timing);
    }
    if (timing->wdog_refresh_stage == 1u &&
        timing->wdog_bus_cycles > timing->wdog_sequence_deadline) {
        timing->wdog_refresh_stage = 0u;
        return wdog_exception(timing);
    }
    if (timing->wdog_initial_unlock_required && !timing->wdog_initial_debug_pause &&
        timing->wdog_unlock_stage == 0u && timing->wdog_bus_cycles > timing->wdog_update_deadline) {
        timing->wdog_initial_unlock_required = false;
        return wdog_exception(timing);
    }
    if (timing->wdog_update_open && timing->wdog_bus_cycles >= timing->wdog_update_deadline) {
        if (!timing->wdog_update_written) {
            timing->wdog_update_open = false;
            return wdog_exception(timing);
        }
        wdog_apply_update(timing);
    }
    return false;
}

static bool wdog_advance_bus(K22Timing* timing, uint64_t ticks) {
    timing->wdog_bus_cycles += ticks;
    return wdog_process_bus_time(timing);
}

static bool wdog_running(const K22Timing* timing) {
    if ((timing->wdog[0] & 1u) == 0u)
        return false;
    if (timing->debug_halted && (timing->wdog[0] & 0x20u) == 0u)
        return false;
    if (!timing->cpu_sleeping)
        return true;
    if (timing->deep_sleeping)
        return (timing->wdog[0] & 0x40u) != 0u;
    return (timing->wdog[0] & 0x80u) != 0u;
}

static uint32_t wdog_effective_timeout(const K22Timing* timing) {
    const uint32_t timeout = wdog_timeout(timing);
    if ((timing->wdog[0] & 0x4c00u) != 0x0c00u)
        return timeout;
    const uint8_t byte = (uint8_t)((timing->wdog[0] >> 12u) & 3u);
    return (timeout >> (byte * 8u)) & 0xffu;
}

void k22_timing_watchdog_advance(K22Timing* timing, uint32_t ticks) {
    if (timing == NULL || timing->profile == NULL || ticks == 0u ||
        !k22_timing_internal_has(timing, K22_PERIPHERAL_WDOG) || !wdog_running(timing))
        return;
    const uint32_t timeout = wdog_effective_timeout(timing);
    if (timeout == 0u || timing->wdog_counter >= timeout ||
        ticks >= timeout - timing->wdog_counter) {
        (void)wdog_exception(timing);
    } else {
        timing->wdog_counter += ticks;
    }
}

void k22_timing_internal_advance_wdog(K22Timing* timing, uint32_t cycles) {
    if (!k22_timing_internal_has(timing, K22_PERIPHERAL_WDOG))
        return;
    const bool update_open = timing->wdog_update_open;
    const uint64_t bus_ticks = k22_timing_internal_clock_ticks(
        &timing->wdog_bus_remainder, cycles, timing->bus_clock_hz, timing->core_clock_hz);
    if (wdog_advance_bus(timing, bus_ticks) || (update_open && !timing->wdog_update_open) ||
        !wdog_running(timing))
        return;
    const bool test = (timing->wdog[0] & 0x4400u) == 0x0400u;
    const uint32_t source_hz =
        test || (timing->wdog[0] & (1u << 13u)) != 0u ? timing->bus_clock_hz : timing->lpo_hz;
    const uint32_t divider = ((timing->wdog[11] & 0x700u) >> 8u) + 1u;
    const uint64_t ticks = k22_timing_internal_clock_ticks(
        &timing->wdog_remainder, cycles, source_hz / divider, timing->core_clock_hz);
    k22_timing_watchdog_advance(timing, ticks > UINT32_MAX ? UINT32_MAX : (uint32_t)ticks);
}

static void assert_ewm_output(K22Timing* timing) {
    timing->ewm_output = true;
    update_watchdog_irq(timing);
}

void k22_timing_internal_advance_ewm(K22Timing* timing, uint32_t cycles) {
    if (!k22_timing_internal_has(timing, K22_PERIPHERAL_EWM) || (timing->ewm_ctrl & 1u) == 0u ||
        timing->ewm_output || timing->cpu_sleeping)
        return;
    const uint32_t source_hz = timing->lpo_hz / ((uint32_t)timing->ewm_prescaler + 1u);
    const uint64_t ticks = k22_timing_internal_clock_ticks(&timing->ewm_remainder, cycles,
                                                           source_hz, timing->core_clock_hz);
    const uint32_t increment = ticks > UINT32_MAX ? UINT32_MAX : (uint32_t)ticks;
    timing->ewm_counter = increment >= UINT32_MAX - timing->ewm_counter
                              ? UINT32_MAX
                              : timing->ewm_counter + increment;
    if (timing->ewm_cmph != 0xffu && timing->ewm_counter >= timing->ewm_cmph)
        assert_ewm_output(timing);
}

bool k22_timing_internal_read_wdog(const K22Timing* timing, uint32_t address, uint8_t size,
                                   uint32_t* value) {
    if ((size != 1u && size != 2u) || address < WDOG_BASE || address + size > WDOG_BASE + 0x18u ||
        (size == 2u && (address & 1u) != 0u)) {
        return false;
    }
    const uint8_t index = (uint8_t)((address - WDOG_BASE) / 2u);
    uint16_t register_value;
    if (index == 8u) {
        register_value = (uint16_t)(timing->wdog_counter >> 16u);
    } else if (index == 9u) {
        register_value = (uint16_t)timing->wdog_counter;
    } else {
        register_value = timing->wdog[index];
    }
    *value = size == 1u ? (register_value >> (((address - WDOG_BASE) & 1u) * 8u)) & 0xffu
                        : register_value;
    return true;
}

static bool sequence_byte_allowed(uint8_t lane, uint8_t value, uint16_t first, uint16_t second) {
    const uint8_t shift = lane * 8u;
    return value == ((first >> shift) & 0xffu) || value == ((second >> shift) & 0xffu);
}

static uint16_t merge_wdog_write(uint16_t previous, uint32_t address, uint8_t size,
                                 uint32_t value) {
    if (size == 2u)
        return (uint16_t)value;
    const uint8_t shift = (uint8_t)((address & 1u) * 8u);
    const uint16_t mask = (uint16_t)(0xffu << shift);
    return (previous & (uint16_t)~mask) | (uint16_t)(((uint8_t)value) << shift);
}

static void accept_wdog_unlock(K22Timing* timing, uint16_t value) {
    if (value == 0xc520u) {
        timing->wdog_unlock_stage = 1u;
        timing->wdog_sequence_deadline = timing->wdog_bus_cycles + 20u;
        return;
    }
    if (value == 0xd928u && timing->wdog_unlock_stage == 1u &&
        timing->wdog_bus_cycles <= timing->wdog_sequence_deadline) {
        timing->wdog_unlock_stage = 0u;
        timing->wdog_initial_unlock_required = false;
        timing->wdog_update_open = true;
        timing->wdog_update_written = false;
        timing->wdog_update_mask = 0u;
        timing->wdog_update_ready = timing->wdog_bus_cycles + 1u;
        timing->wdog_update_deadline = timing->wdog_update_ready + 256u;
        memcpy(timing->wdog_pending, timing->wdog, sizeof(timing->wdog));
        return;
    }
    timing->wdog_unlock_stage = 0u;
    (void)wdog_exception(timing);
}

static void accept_wdog_refresh(K22Timing* timing, uint16_t value) {
    if (value == 0xa602u) {
        timing->wdog_refresh_stage = 1u;
        timing->wdog_sequence_deadline = timing->wdog_bus_cycles + 20u;
        return;
    }
    if (value == 0xb480u && timing->wdog_refresh_stage == 1u &&
        timing->wdog_bus_cycles <= timing->wdog_sequence_deadline) {
        const uint32_t window = ((uint32_t)timing->wdog[4] << 16u) | timing->wdog[5];
        if ((timing->wdog[0] & (1u << 3u)) != 0u && timing->wdog_counter < window)
            (void)wdog_exception(timing);
        else
            timing->wdog_counter = 0u;
        timing->wdog_refresh_stage = 0u;
        return;
    }
    timing->wdog_refresh_stage = 0u;
    (void)wdog_exception(timing);
}

bool k22_timing_internal_write_wdog(K22Timing* timing, uint32_t address, uint8_t size,
                                    uint32_t value) {
    if ((size != 1u && size != 2u) || address < WDOG_BASE || address + size > WDOG_BASE + 0x18u ||
        (size == 2u && (address & 1u) != 0u)) {
        return false;
    }
    if (wdog_advance_bus(timing, 1u))
        return true;
    const uint8_t index = (uint8_t)((address - WDOG_BASE) / 2u);
    const uint8_t lane = (uint8_t)((address - WDOG_BASE) & 1u);
    if (index == 7u) {
        if (timing->wdog_refresh_stage != 0u || timing->wdog_update_open)
            return true;
        if (!timing->wdog_initial_unlock_required && (timing->wdog[0] & 0x10u) == 0u)
            return true;
        timing->wdog[index] = merge_wdog_write(timing->wdog[index], address, size, value);
        if (size == 1u && !sequence_byte_allowed(lane, (uint8_t)value, 0xc520u, 0xd928u)) {
            (void)wdog_exception(timing);
        } else if (size == 2u || timing->wdog[index] == 0xc520u || timing->wdog[index] == 0xd928u) {
            accept_wdog_unlock(timing, timing->wdog[index]);
        }
        return true;
    }
    if (index == 6u) {
        if (timing->wdog_unlock_stage != 0u || timing->wdog_update_open)
            return true;
        timing->wdog[index] = merge_wdog_write(timing->wdog[index], address, size, value);
        if (size == 1u && !sequence_byte_allowed(lane, (uint8_t)value, 0xa602u, 0xb480u)) {
            (void)wdog_exception(timing);
        } else if (size == 2u || timing->wdog[index] == 0xa602u || timing->wdog[index] == 0xb480u) {
            accept_wdog_refresh(timing, timing->wdog[index]);
        }
        return true;
    }
    if (index == 1u) {
        const uint16_t written =
            size == 1u ? (uint16_t)((uint8_t)value << (lane * 8u)) : (uint16_t)value;
        if ((written & 0x8000u) != 0u) {
            timing->wdog[1] &= 0x7fffu;
            update_watchdog_irq(timing);
        }
        if (timing->wdog_update_open && timing->wdog_bus_cycles >= timing->wdog_update_ready)
            timing->wdog_update_written = true;
        return true;
    }
    if (index == 10u) {
        const uint16_t written =
            size == 1u ? (uint16_t)((uint8_t)value << (lane * 8u)) : (uint16_t)value;
        timing->wdog[10] &= (uint16_t)~written;
        return true;
    }
    if (index == 8u || index == 9u)
        return true;
    const uint8_t byte = (uint8_t)(index * 2u + lane);
    const uint32_t update_bits = size == 2u ? UINT32_C(3) << (index * 2u) : UINT32_C(1) << byte;
    if (!timing->wdog_update_open || timing->wdog_bus_cycles < timing->wdog_update_ready ||
        index >= 12u || (timing->wdog_update_mask & update_bits) != 0u) {
        return true;
    }
    timing->wdog_pending[index] =
        merge_wdog_write(timing->wdog_pending[index], address, size, value);
    timing->wdog_update_mask |= update_bits;
    timing->wdog_update_written = true;
    if (index == 0u) {
        const uint16_t immediate = 0x00e4u;
        timing->wdog[0] =
            (timing->wdog[0] & (uint16_t)~immediate) | (timing->wdog_pending[0] & immediate);
    }
    return true;
}

bool k22_timing_internal_read_ewm(const K22Timing* timing, uint32_t address, uint8_t size,
                                  uint32_t* value) {
    if (size != 1 || address < EWM_BASE || address > EWM_BASE + 5u || address == EWM_BASE + 4u) {
        return false;
    }
    switch (address - EWM_BASE) {
    case 0:
        *value = timing->ewm_ctrl;
        return true;
    case 1:
        *value = 0;
        return true;
    case 2:
        *value = timing->ewm_cmpl;
        return true;
    case 3:
        *value = timing->ewm_cmph;
        return true;
    case 5:
        *value = timing->ewm_prescaler;
        return true;
    default:
        return false;
    }
}

bool k22_timing_internal_write_ewm(K22Timing* timing, uint32_t address, uint8_t size,
                                   uint32_t value) {
    if (size != 1) {
        return false;
    }
    switch (address - EWM_BASE) {
    case 0:
        if (!timing->ewm_control_written) {
            timing->ewm_ctrl = (uint8_t)value & 15u;
            timing->ewm_control_written = true;
            if ((timing->ewm_ctrl & 1u) != 0u) {
                timing->ewm_counter = 0u;
                timing->ewm_output = false;
            }
        } else if (((uint8_t)value & 7u) != (timing->ewm_ctrl & 7u)) {
            return false;
        } else {
            timing->ewm_ctrl = (timing->ewm_ctrl & 7u) | ((uint8_t)value & 8u);
        }
        update_watchdog_irq(timing);
        return true;
    case 1:
        if (value == 0xb4u) {
            timing->ewm_service_stage = 1u;
            timing->ewm_service_deadline = timing->wdog_bus_cycles + 15u;
            timing->ewm_service_paused = false;
        } else if (value == 0x2cu && timing->ewm_service_stage == 1u &&
                   !timing->ewm_service_paused &&
                   timing->wdog_bus_cycles <= timing->ewm_service_deadline) {
            const bool input_asserted = (timing->ewm_ctrl & 4u) != 0u &&
                                        timing->ewm_input == ((timing->ewm_ctrl & 2u) != 0u);
            if (timing->ewm_counter > timing->ewm_cmpl && timing->ewm_counter < timing->ewm_cmph &&
                !input_asserted)
                timing->ewm_counter = 0u;
            else
                assert_ewm_output(timing);
            timing->ewm_service_stage = 0u;
            timing->ewm_service_paused = false;
        } else {
            timing->ewm_service_stage = 0u;
            timing->ewm_service_paused = false;
        }
        return true;
    case 2:
        if (timing->ewm_cmpl_written)
            return false;
        timing->ewm_cmpl = (uint8_t)value;
        timing->ewm_cmpl_written = true;
        return true;
    case 3:
        if (timing->ewm_cmph_written)
            return false;
        timing->ewm_cmph = (uint8_t)value;
        timing->ewm_cmph_written = true;
        return true;
    case 5:
        if (timing->ewm_prescaler_written)
            return false;
        timing->ewm_prescaler = (uint8_t)value;
        timing->ewm_prescaler_written = true;
        return true;
    default:
        return false;
    }
}

bool k22_timing_internal_ftm_read(K22Timing* timing, uint8_t index, uint32_t offset, uint8_t size,
                                  uint32_t* value) {
    if (size != 4 || (offset & 3u) != 0) {
        return false;
    }
    K22FtmState* ftm = &timing->ftm[index];
    if (offset == 0) {
        *value = ftm->sc;
        if ((*value & 0x80u) != 0u)
            ftm->overflow_flag_read = true;
    } else if (offset == 4)
        *value = ftm->counter;
    else if (offset == 8)
        *value = ftm->modulo;
    else if (offset >= 0x0cu && offset < 0x4cu) {
        const uint8_t channel = (uint8_t)((offset - 0x0cu) / 8u);
        if (channel >= k22_timing_internal_ftm_channel_count(index))
            return false;
        if (((offset - 0x0cu) & 4u) == 0u) {
            *value = ftm->channel_sc[channel];
            if ((*value & 0x80u) != 0u)
                ftm->channel_flag_read[channel] = true;
        } else {
            *value = ftm->channel_value[channel];
        }
    } else if (offset == 0x4cu)
        *value = ftm->initial;
    else if (offset == 0x50u) {
        uint32_t status = 0;
        const uint8_t channels = k22_timing_internal_ftm_channel_count(index);
        for (uint8_t channel = 0; channel < channels; channel++) {
            status |= ((ftm->channel_sc[channel] >> 7u) & 1u) << channel;
        }
        *value = status;
    } else if (offset >= 0x54u && offset <= 0x98u) {
        *value = ftm->registers[(offset - 0x54u) / 4u];
        if (offset == 0x6cu && (*value & 0x80u) != 0u)
            ftm->trigger_flag_read = true;
        if (offset == 0x74u) {
            if ((*value & 0x40u) != 0u)
                ftm->write_protection_read = true;
            ftm->fault_flags_read_mask |= (uint8_t)(*value & 0x0fu);
            if ((*value & 0x80u) != 0u)
                ftm->fault_aggregate_read = true;
        }
    } else
        return false;
    return true;
}
