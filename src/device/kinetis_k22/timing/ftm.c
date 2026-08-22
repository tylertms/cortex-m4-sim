#include "internal.h"

static uint8_t ftm_irq(uint8_t index) { return index == 3u ? IRQ_FTM3 : IRQ_FTM0 + index; }

static uint8_t ftm_dma_source(uint8_t module, uint8_t channel) {
    static const uint8_t bases[4] = {20u, 28u, 30u, 32u};
    return bases[module] + channel;
}

static uint8_t ftm_trigger_bit(uint8_t channel) {
    static const uint8_t bits[6] = {4u, 5u, 0u, 1u, 2u, 3u};
    return channel < 6u ? bits[channel] : UINT8_MAX;
}

uint8_t k22_timing_internal_ftm_channel_count(uint8_t index) {
    return index == 0u || index == 3u ? 8u : 2u;
}

static bool ftm_quadrature_enabled(const K22FtmState* ftm) {
    return ftm->quadrature_capable && (ftm->registers[11] & 1u) != 0u;
}

static bool ftm_combine_mode(const K22FtmState* ftm, uint8_t channel) {
    const uint8_t pair_shift = (uint8_t)((channel / 2u) * 8u);
    const uint32_t pair = ftm->registers[4] >> pair_shift;
    return (ftm->sc & (1u << 5u)) == 0u && !ftm_quadrature_enabled(ftm) && (pair & 1u) != 0u &&
           (pair & 4u) == 0u;
}

static bool ftm_complementary_mode(const K22FtmState* ftm, uint8_t channel) {
    const uint8_t pair_shift = (uint8_t)((channel / 2u) * 8u);
    const uint32_t pair = ftm->registers[4] >> pair_shift;
    const uint8_t first_channel = channel & 0xfeu;
    const bool output_compare =
        (ftm->sc & (1u << 5u)) == 0u && (ftm->channel_sc[first_channel] & 0x30u) == 0x10u;
    return !ftm_quadrature_enabled(ftm) && (pair & 2u) != 0u && (pair & 4u) == 0u &&
           !output_compare;
}

bool k22_timing_set_ftm_input(K22Timing* timing, uint8_t instance, uint8_t channel, bool high) {
    if (timing == NULL || timing->profile == NULL || instance >= 4u ||
        channel >= k22_timing_internal_ftm_channel_count(instance))
        return false;
    const K22PeripheralId peripheral = (K22PeripheralId)(K22_PERIPHERAL_FTM0 + instance);
    if (!k22_timing_internal_has(timing, peripheral))
        return false;
    K22FtmState* ftm = &timing->ftm[instance];
    if (ftm->channel_input[channel] != high) {
        ftm->channel_input[channel] = high;
        ftm->channel_input_age[channel] = 0u;
    }
    return true;
}

bool k22_timing_set_ftm_fault(K22Timing* timing, uint8_t instance, uint8_t input, bool high) {
    if (timing == NULL || timing->profile == NULL || instance >= 4u || input >= 4u)
        return false;
    const K22PeripheralId peripheral = (K22PeripheralId)(K22_PERIPHERAL_FTM0 + instance);
    if (!k22_timing_internal_has(timing, peripheral))
        return false;
    K22FtmState* ftm = &timing->ftm[instance];
    if (ftm->fault_input[input] != high) {
        ftm->fault_input[input] = high;
        ftm->fault_input_age[input] = 0u;
    }
    return true;
}

bool k22_timing_trigger_ftm_hardware(K22Timing* timing, uint8_t instance, uint8_t trigger) {
    if (timing == NULL || timing->profile == NULL || instance >= 4u || trigger >= 3u)
        return false;
    const K22PeripheralId peripheral = (K22PeripheralId)(K22_PERIPHERAL_FTM0 + instance);
    if (!k22_timing_internal_has(timing, peripheral))
        return false;
    timing->ftm[instance].hardware_trigger_pending_mask |= (uint8_t)(1u << trigger);
    return true;
}

static bool ftm_pre_deadtime_output(const K22FtmState* ftm, uint8_t channel) {
    const uint8_t pair_shift = (uint8_t)((channel / 2u) * 8u);
    const uint32_t pair = ftm->registers[4] >> pair_shift;
    const bool combined = ftm_combine_mode(ftm, channel);
    const bool complementary = ftm_complementary_mode(ftm, channel);
    const uint8_t first_channel = channel & 0xfeu;
    bool output = ftm->channel_output[channel];
    if ((combined || complementary) && (ftm->channel_sc[channel] & 0x0cu) != 0u) {
        output = ftm->channel_output[first_channel];
        const bool inverted_pair = (ftm->registers[15] & (1u << (channel / 2u))) != 0u;
        if (complementary && (((channel & 1u) != 0u) != inverted_pair))
            output = !output;
    }
    const bool dual_capture = (pair & 4u) != 0u;
    const bool software_enabled = (ftm->registers[16] & (1u << channel)) != 0u;
    if (!ftm_quadrature_enabled(ftm) && !dual_capture && software_enabled) {
        output = (ftm->registers[16] & (1u << (channel + 8u))) != 0u;
        const bool pair_software_enabled =
            (ftm->registers[16] & (3u << first_channel)) == (3u << first_channel);
        if ((channel & 1u) != 0u && complementary && pair_software_enabled && output &&
            (ftm->registers[16] & (1u << (first_channel + 8u))) != 0u)
            output = false;
    }
    return output;
}

static bool ftm_deadtime_enabled(const K22FtmState* ftm, uint8_t channel) {
    const uint8_t pair_shift = (uint8_t)((channel / 2u) * 8u);
    return ftm_complementary_mode(ftm, channel) &&
           (ftm->registers[4] & (1u << (pair_shift + 4u))) != 0u &&
           (ftm->registers[5] & 0x3fu) != 0u;
}

uint8_t k22_timing_internal_ftm_fault_mode(const K22FtmState* ftm) {
    return (uint8_t)((ftm->registers[0] >> 5u) & 3u);
}

static bool ftm_fault_channel_enabled(const K22FtmState* ftm, uint8_t channel) {
    const uint8_t mode = k22_timing_internal_ftm_fault_mode(ftm);
    const uint8_t shift = (uint8_t)((channel / 2u) * 8u + 6u);
    return mode != 0u && (ftm->registers[4] & (1u << shift)) != 0u &&
           (mode != 1u || (channel & 1u) == 0u);
}

bool k22_timing_get_ftm_output(const K22Timing* timing, uint8_t instance, uint8_t channel,
                               bool* high) {
    if (timing == NULL || high == NULL || timing->profile == NULL || instance >= 4u ||
        channel >= k22_timing_internal_ftm_channel_count(instance))
        return false;
    const K22PeripheralId peripheral = (K22PeripheralId)(K22_PERIPHERAL_FTM0 + instance);
    if (!k22_timing_internal_has(timing, peripheral))
        return false;
    const K22FtmState* ftm = &timing->ftm[instance];
    if (ftm_quadrature_enabled(ftm)) {
        *high = false;
        return true;
    }
    bool output = ftm_deadtime_enabled(ftm, channel) ? ftm->channel_deadtime_output[channel]
                                                     : ftm_pre_deadtime_output(ftm, channel);
    if ((ftm->registers[3] & (1u << channel)) != 0u)
        output = false;
    if (ftm->fault_output_active && ftm_fault_channel_enabled(ftm, channel))
        output = false;
    if ((ftm->registers[7] & (1u << channel)) != 0u)
        output = !output;
    *high = output;
    return true;
}

void k22_timing_internal_update_ftm_irq(const K22Timing* timing, uint8_t index) {
    const K22FtmState* ftm = &timing->ftm[index];
    bool asserted = (ftm->sc & 0xc0u) == 0xc0u;
    const uint8_t channels = k22_timing_internal_ftm_channel_count(index);
    for (uint8_t channel = 0u; channel < channels; channel++)
        asserted = asserted || (ftm->channel_sc[channel] & 0xc0u) == 0xc0u;
    asserted = asserted || ((ftm->registers[0] & 0x80u) != 0u && (ftm->registers[8] & 0x80u) != 0u);
    k22_timing_internal_set_irq(timing, ftm_irq(index), asserted);
}

void k22_timing_internal_ftm_trigger(K22Timing* timing, uint8_t index) {
    K22FtmState* ftm = &timing->ftm[index];
    ftm->registers[6] |= 0x80u;
    ftm->trigger_flag_read = false;
    k22_timing_internal_trigger_adc_alternate(timing, (uint8_t)(8u + index));
}

static bool ftm_gate(const K22Timing* timing, uint8_t index) {
    if (index == 3u) {
        if (timing->profile->id == K22_PROFILE_MK22FN1M012 ||
            timing->profile->id == K22_PROFILE_MK22FX51212)
            return (timing->sim_scgc3 & (1u << 25u)) != 0;
        return (timing->sim_scgc6 & (1u << 6u)) != 0;
    }
    return (timing->sim_scgc6 & (1u << (24u + index))) != 0;
}

uint8_t k22_timing_internal_ftm_active_fault_mask(const K22FtmState* ftm) {
    if (k22_timing_internal_ftm_fault_mode(ftm) == 0u)
        return 0u;
    uint8_t active = 0u;
    const uint8_t enabled = (uint8_t)ftm->registers[10] & 0x0fu;
    for (uint8_t input = 0u; input < 4u; input++) {
        if ((enabled & (1u << input)) != 0u && ftm->fault_filtered_input[input])
            active |= (uint8_t)(1u << input);
    }
    return active;
}

void k22_timing_internal_ftm_update_fault_status(K22FtmState* ftm) {
    const uint32_t flags = ftm->registers[8] & 0x0fu;
    ftm->registers[8] &= 0x4fu;
    if (k22_timing_internal_ftm_active_fault_mask(ftm) != 0u)
        ftm->registers[8] |= 0x20u;
    if (flags != 0u)
        ftm->registers[8] |= 0x80u;
}

static void ftm_detect_fault(K22Timing* timing, uint8_t index, uint8_t input) {
    K22FtmState* ftm = &timing->ftm[index];
    const uint8_t bit = (uint8_t)(1u << input);
    ftm->registers[8] |= bit;
    ftm->fault_flags_read_mask &= (uint8_t)~bit;
    ftm->fault_aggregate_read = false;
    ftm->fault_output_active = true;
    ftm->fault_release_pending = false;
    k22_timing_internal_ftm_update_fault_status(ftm);
    k22_timing_internal_update_ftm_irq(timing, index);
}

static bool ftm_fault_processing_active(const K22FtmState* ftm) {
    return (k22_timing_internal_ftm_fault_mode(ftm) != 0u && (ftm->registers[10] & 0x0fu) != 0u) ||
           ftm->fault_output_active;
}

static void ftm_advance_fault_inputs(K22Timing* timing, uint8_t index, uint32_t cycles) {
    K22FtmState* ftm = &timing->ftm[index];
    if (!ftm_gate(timing, index))
        return;
    const uint64_t ticks = k22_timing_internal_clock_ticks(
        &ftm->fault_remainder, cycles, timing->bus_clock_hz, timing->core_clock_hz);
    const uint8_t enabled =
        k22_timing_internal_ftm_fault_mode(ftm) == 0u ? 0u : (uint8_t)ftm->registers[10] & 0x0fu;
    const uint8_t filter_enable = (uint8_t)(ftm->registers[10] >> 4u) & 0x0fu;
    const uint8_t filter_value = (uint8_t)(ftm->registers[10] >> 8u) & 0x0fu;
    for (uint8_t input = 0u; input < 4u; input++) {
        const uint8_t bit = (uint8_t)(1u << input);
        if ((enabled & bit) == 0u) {
            ftm->fault_filtered_input[input] = false;
            ftm->fault_input_age[input] = 0u;
            continue;
        }
        const bool polarity = (ftm->registers[13] & bit) != 0u;
        const bool active = ftm->fault_input[input] != polarity;
        if (active == ftm->fault_filtered_input[input]) {
            ftm->fault_input_age[input] = 0u;
            continue;
        }
        const uint32_t threshold =
            (filter_enable & bit) != 0u && filter_value != 0u ? 4u + filter_value : 3u;
        ftm->fault_input_age[input] += (uint32_t)ticks;
        if (ftm->fault_input_age[input] < threshold)
            continue;
        ftm->fault_filtered_input[input] = active;
        ftm->fault_input_age[input] = 0u;
        if (active)
            ftm_detect_fault(timing, index, input);
    }
    k22_timing_internal_ftm_update_fault_status(ftm);
    if (ftm->fault_output_active && k22_timing_internal_ftm_fault_mode(ftm) == 3u &&
        k22_timing_internal_ftm_active_fault_mask(ftm) == 0u)
        ftm->fault_release_pending = true;
    k22_timing_internal_update_ftm_irq(timing, index);
}

static void ftm_fault_cycle_boundary(K22Timing* timing, uint8_t index, bool new_cycle) {
    K22FtmState* ftm = &timing->ftm[index];
    if (!new_cycle || !ftm->fault_release_pending)
        return;
    if (k22_timing_internal_ftm_fault_mode(ftm) == 3u)
        ftm->registers[8] &= ~0x0fu;
    ftm->fault_output_active = false;
    ftm->fault_release_pending = false;
    ftm->fault_flags_read_mask = 0u;
    ftm->fault_aggregate_read = false;
    k22_timing_internal_ftm_update_fault_status(ftm);
    k22_timing_internal_update_ftm_irq(timing, index);
}

static uint64_t ftm_phase_crossing_count(uint32_t phase, uint64_t ticks, uint32_t period,
                                         uint32_t target) {
    const uint32_t distance = target > phase ? target - phase : period - (phase - target);
    return ticks < distance ? 0u : 1u + (ticks - distance) / period;
}

static void ftm_channel_event(K22Timing* timing, uint8_t index, uint8_t channel) {
    K22FtmState* ftm = &timing->ftm[index];
    ftm->channel_sc[channel] |= 1u << 7u;
    ftm->channel_flag_read[channel] = false;
    if ((ftm->channel_sc[channel] & 1u) != 0)
        k22_timing_internal_request_dma(timing, ftm_dma_source(index, channel));
    const uint8_t trigger_bit = ftm_trigger_bit(channel);
    if (trigger_bit != UINT8_MAX && (ftm->registers[6] & (1u << trigger_bit)) != 0u)
        k22_timing_internal_ftm_trigger(timing, index);
}

static bool ftm_pair_mode_disabled(const K22FtmState* ftm, uint8_t channel) {
    const uint8_t pair_shift = (uint8_t)((channel / 2u) * 8u);
    return ((ftm->registers[4] >> pair_shift) & 5u) == 0u;
}

static void ftm_combine_pwm_advance(K22FtmState* ftm, uint8_t channel) {
    if ((channel & 1u) != 0u || !ftm_combine_mode(ftm, channel))
        return;
    const uint8_t edges = (uint8_t)((ftm->channel_sc[channel] >> 2u) & 3u);
    if (edges == 0u)
        return;
    const uint32_t first_compare = ftm->channel_value[channel];
    const uint32_t second_compare = ftm->channel_value[channel + 1u];
    const bool first_valid = first_compare >= ftm->initial && first_compare <= ftm->modulo;
    const bool second_valid = second_compare >= ftm->initial && second_compare <= ftm->modulo;
    bool active = false;
    if (first_valid) {
        if (second_valid)
            active = first_compare < second_compare && ftm->counter >= first_compare &&
                     ftm->counter < second_compare;
        else
            active = ftm->counter >= first_compare;
    }
    ftm->channel_output[channel] = edges == 2u ? active : !active;
}

bool k22_timing_internal_ftm_output_compare_mode(const K22FtmState* ftm, uint8_t channel) {
    return (ftm->sc & (1u << 5u)) == 0u && (ftm->channel_sc[channel] & 0x30u) == 0x10u &&
           !ftm_quadrature_enabled(ftm) && ftm_pair_mode_disabled(ftm, channel);
}

static bool ftm_edge_aligned_pwm_mode(const K22FtmState* ftm, uint8_t channel) {
    return (ftm->sc & (1u << 5u)) == 0u && (ftm->channel_sc[channel] & 0x20u) != 0u &&
           !ftm_quadrature_enabled(ftm) && ftm_pair_mode_disabled(ftm, channel);
}

static bool ftm_center_aligned_pwm_mode(const K22FtmState* ftm, uint8_t channel) {
    return (ftm->sc & (1u << 5u)) != 0u && !ftm_quadrature_enabled(ftm) &&
           ftm_pair_mode_disabled(ftm, channel);
}

static void ftm_output_compare_match(K22FtmState* ftm, uint8_t channel, uint64_t count) {
    if (count == 0u || !k22_timing_internal_ftm_output_compare_mode(ftm, channel))
        return;
    switch ((ftm->channel_sc[channel] >> 2u) & 3u) {
    case 1u:
        if ((count & 1u) != 0u)
            ftm->channel_output[channel] = !ftm->channel_output[channel];
        break;
    case 2u:
        ftm->channel_output[channel] = false;
        break;
    case 3u:
        ftm->channel_output[channel] = true;
        break;
    default:
        break;
    }
}

static void ftm_edge_aligned_pwm_advance(K22FtmState* ftm, uint8_t channel, uint64_t matches,
                                         uint64_t overflows) {
    if (!ftm_edge_aligned_pwm_mode(ftm, channel))
        return;
    const uint8_t edges = (uint8_t)((ftm->channel_sc[channel] >> 2u) & 3u);
    if (edges == 0u)
        return;
    const bool high_true = edges == 2u;
    const uint32_t compare = ftm->channel_value[channel];
    if (overflows != 0u) {
        const bool active = compare < ftm->initial || compare > ftm->modulo
                                ? true
                                : compare != ftm->initial && ftm->counter < compare;
        ftm->channel_output[channel] = high_true ? active : !active;
    } else if (matches != 0u) {
        ftm->channel_output[channel] = !high_true;
    }
}

static void ftm_center_aligned_pwm_advance(K22FtmState* ftm, uint8_t channel, uint64_t matches,
                                           uint64_t ticks) {
    if (ticks == 0u || !ftm_center_aligned_pwm_mode(ftm, channel))
        return;
    const uint8_t edges = (uint8_t)((ftm->channel_sc[channel] >> 2u) & 3u);
    if (edges == 0u)
        return;
    const bool high_true = edges == 2u;
    const uint16_t compare = ftm->channel_value[channel];
    bool active;
    if (compare <= ftm->initial || (compare & 0x8000u) != 0u)
        active = false;
    else if (compare >= ftm->modulo)
        active = true;
    else {
        if (matches == 0u)
            return;
        active = ftm->counter < compare || (ftm->counting_down && ftm->counter == compare);
    }
    ftm->channel_output[channel] = high_true ? active : !active;
}

static void ftm_overflow(K22Timing* timing, uint8_t index, uint64_t count) {
    if (count == 0u)
        return;
    K22FtmState* ftm = &timing->ftm[index];
    const uint8_t cycle = (uint8_t)((ftm->registers[12] & 0x1fu) + 1u);
    const uint8_t first_set =
        ftm->overflow_count == 0u ? 1u : (uint8_t)(cycle - ftm->overflow_count + 1u);
    if (count >= first_set) {
        ftm->sc |= 1u << 7u;
        ftm->overflow_flag_read = false;
    }
    ftm->overflow_count = (uint8_t)((ftm->overflow_count + count % cycle) % cycle);
}

static void ftm_apply_modulo(K22FtmState* ftm);
static void ftm_apply_channel_value(K22FtmState* ftm, uint8_t channel);
static bool ftm_pair_synchronization_enabled(const K22FtmState* ftm, uint8_t channel);
static void ftm_loading_point(K22FtmState* ftm, bool minimum, bool maximum,
                              uint8_t channel_matches);

static void ftm_apply_counter_change_values(K22FtmState* ftm, uint8_t channels) {
    const bool enhanced = (ftm->registers[0] & 1u) != 0u;
    for (uint8_t channel = 0u; channel < channels; channel++) {
        if (ftm->channel_value_pending[channel] &&
            k22_timing_internal_ftm_output_compare_mode(ftm, channel) &&
            (!enhanced || !ftm_pair_synchronization_enabled(ftm, channel)))
            ftm_apply_channel_value(ftm, channel);
    }
}

static void ftm_apply_legacy_boundary_values(K22FtmState* ftm, uint8_t channels) {
    if ((ftm->registers[0] & 1u) != 0u)
        return;
    ftm_apply_modulo(ftm);
    for (uint8_t channel = 0u; channel < channels; channel++) {
        if (ftm_edge_aligned_pwm_mode(ftm, channel) || ftm_center_aligned_pwm_mode(ftm, channel) ||
            ftm_combine_mode(ftm, channel))
            ftm_apply_channel_value(ftm, channel);
    }
}

static void advance_ftm_up_down(K22Timing* timing, uint8_t index, uint64_t ticks) {
    K22FtmState* ftm = &timing->ftm[index];
    const uint32_t first = ftm->initial;
    const uint32_t last = ftm->modulo;
    if (last <= first) {
        ftm->counter = (uint16_t)first;
        ftm->counting_down = false;
        ftm_overflow(timing, index, ticks);
        if ((ftm->registers[6] & (1u << 6u)) != 0u)
            k22_timing_internal_ftm_trigger(timing, index);
        k22_timing_internal_update_ftm_irq(timing, index);
        return;
    }
    const uint32_t span = last - first;
    const uint32_t period = span * 2u;
    const uint8_t channels = k22_timing_internal_ftm_channel_count(index);
    ftm_apply_counter_change_values(ftm, channels);
    uint32_t phase;
    if (ftm->counter < first || ftm->counter > last) {
        phase = 0u;
    } else if (ftm->counting_down) {
        phase = span + last - ftm->counter;
    } else {
        phase = ftm->counter - first;
    }
    uint64_t matches[8] = {0};
    uint8_t match_mask = 0u;
    for (uint8_t channel = 0u; channel < channels; channel++) {
        const uint32_t compare = ftm->channel_value[channel];
        if (compare <= first || compare >= last)
            continue;
        const uint32_t up_phase = compare - first;
        const uint32_t down_phase = period - up_phase;
        matches[channel] = ftm_phase_crossing_count(phase, ticks, period, up_phase) +
                           ftm_phase_crossing_count(phase, ticks, period, down_phase);
        if (matches[channel] != 0u) {
            match_mask |= (uint8_t)(1u << channel);
            ftm_channel_event(timing, index, channel);
        }
    }
    const uint64_t maximum_points = ftm_phase_crossing_count(phase, ticks, period, span + 1u);
    const uint64_t minimum_points = ftm_phase_crossing_count(phase, ticks, period, 0u);
    ftm_overflow(timing, index, maximum_points);
    ftm_fault_cycle_boundary(timing, index, minimum_points != 0u);
    if (minimum_points != 0u && (ftm->registers[6] & (1u << 6u)) != 0u)
        k22_timing_internal_ftm_trigger(timing, index);
    phase = (uint32_t)(((uint64_t)phase + ticks) % period);
    if (phase <= span) {
        ftm->counter = (uint16_t)(first + phase);
        ftm->counting_down = false;
    } else {
        ftm->counter = (uint16_t)(last - (phase - span));
        ftm->counting_down = true;
    }
    if (maximum_points != 0u)
        ftm_apply_legacy_boundary_values(ftm, channels);
    for (uint8_t channel = 0u; channel < channels; channel++)
        ftm_center_aligned_pwm_advance(ftm, channel, matches[channel], ticks);
    ftm_loading_point(ftm, minimum_points != 0u, maximum_points != 0u, match_mask);
    k22_timing_internal_update_ftm_irq(timing, index);
}

static void advance_ftm_counter(K22Timing* timing, uint8_t index, uint32_t cycles) {
    K22FtmState* ftm = &timing->ftm[index];
    if (ftm_quadrature_enabled(ftm))
        return;
    const uint8_t clock_select = (uint8_t)((ftm->sc >> 3u) & 3u);
    if (!ftm_gate(timing, index) || clock_select == 0 || timing->debug_halted) {
        return;
    }
    uint32_t source_hz = timing->bus_clock_hz;
    if (clock_select == 2u) {
        source_hz = timing->rtc_oscillator_hz;
    } else if (clock_select == 3u) {
        source_hz = timing->external_oscillator_hz;
    }
    source_hz >>= ftm->sc & 7u;
    const uint64_t ticks =
        k22_timing_internal_clock_ticks(&ftm->remainder, cycles, source_hz, timing->core_clock_hz);
    if (ticks == 0) {
        return;
    }
    if ((ftm->sc & (1u << 5u)) != 0u) {
        advance_ftm_up_down(timing, index, ticks);
        return;
    }
    const uint8_t channels = k22_timing_internal_ftm_channel_count(index);
    ftm_apply_counter_change_values(ftm, channels);
    const uint32_t first = ftm->initial;
    const uint32_t last = ftm->modulo >= first ? ftm->modulo : 0xffffu;
    const uint32_t period = last - first + 1u;
    const uint32_t start = ftm->counter < first || ftm->counter > last ? first : ftm->counter;
    const uint64_t relative = (uint64_t)(start - first) + ticks;
    const uint64_t overflows = relative / period;
    uint64_t matches[8] = {0};
    uint8_t match_mask = 0u;
    for (uint8_t channel = 0; channel < channels; channel++) {
        const uint32_t compare = ftm->channel_value[channel];
        const uint32_t distance = compare > start ? compare - start : period - (start - compare);
        const bool output_compare = k22_timing_internal_ftm_output_compare_mode(ftm, channel);
        const bool edge_aligned = ftm_edge_aligned_pwm_mode(ftm, channel);
        const bool combined = ftm_combine_mode(ftm, channel);
        const bool valid_compare = output_compare ? compare >= first && compare <= last
                                                  : compare > first && compare <= last;
        const bool combine_compare = combined && compare >= first && compare <= last;
        if ((output_compare || edge_aligned || combine_compare) &&
            (valid_compare || combine_compare) && ticks >= distance) {
            matches[channel] = 1u + (ticks - distance) / period;
            match_mask |= (uint8_t)(1u << channel);
            ftm_channel_event(timing, index, channel);
        }
    }
    ftm->counter = (uint16_t)(first + relative % period);
    ftm_overflow(timing, index, overflows);
    ftm_fault_cycle_boundary(timing, index, overflows != 0u);
    if (overflows != 0u && (ftm->registers[6] & (1u << 6u)) != 0u)
        k22_timing_internal_ftm_trigger(timing, index);
    if (overflows != 0u)
        ftm_apply_legacy_boundary_values(ftm, channels);
    for (uint8_t channel = 0u; channel < channels; channel++) {
        ftm_output_compare_match(ftm, channel, matches[channel]);
        ftm_edge_aligned_pwm_advance(ftm, channel, matches[channel], overflows);
        ftm_combine_pwm_advance(ftm, channel);
    }
    ftm_loading_point(ftm, overflows != 0u, overflows != 0u, match_mask);
    k22_timing_internal_update_ftm_irq(timing, index);
}

static uint32_t ftm_input_threshold(const K22FtmState* ftm, uint8_t channel) {
    if (channel >= 4u)
        return 3u;
    const uint8_t filter = (uint8_t)((ftm->registers[9] >> (channel * 4u)) & 15u);
    if (ftm_quadrature_enabled(ftm) && channel < 2u &&
        (ftm->registers[11] & (1u << (7u - channel))) == 0u)
        return 3u;
    return filter == 0u ? 3u : 4u + (uint32_t)filter * 4u;
}

bool k22_timing_internal_ftm_input_capture_mode(const K22FtmState* ftm, uint8_t channel) {
    const uint8_t pair_shift = (uint8_t)((channel / 2u) * 8u);
    const uint32_t pair = ftm->registers[4] >> pair_shift;
    return !ftm_quadrature_enabled(ftm) && (ftm->sc & (1u << 5u)) == 0u &&
           (ftm->channel_sc[channel] & 0x30u) == 0u && (ftm->channel_sc[channel] & 0x0cu) != 0u &&
           (pair & 5u) == 0u;
}

static void ftm_quadrature_step(K22Timing* timing, uint8_t index, bool increment) {
    K22FtmState* ftm = &timing->ftm[index];
    if ((ftm->sc & 0x18u) == 0u || timing->debug_halted)
        return;
    const uint16_t first = ftm->initial;
    const uint16_t last = ftm->modulo >= first ? ftm->modulo : UINT16_MAX;
    const uint16_t counter = ftm->counter < first || ftm->counter > last ? first : ftm->counter;
    bool overflow = false;
    if (increment) {
        ftm->registers[11] |= 4u;
        if (counter == last) {
            ftm->counter = first;
            ftm->registers[11] |= 2u;
            overflow = true;
        } else {
            ftm->counter = (uint16_t)(counter + 1u);
        }
    } else {
        ftm->registers[11] &= ~4u;
        if (counter == first) {
            ftm->counter = last;
            ftm->registers[11] &= ~2u;
            overflow = true;
        } else {
            ftm->counter = (uint16_t)(counter - 1u);
        }
    }
    if (overflow) {
        ftm_overflow(timing, index, 1u);
        ftm_fault_cycle_boundary(timing, index, true);
        if ((ftm->registers[6] & (1u << 6u)) != 0u)
            k22_timing_internal_ftm_trigger(timing, index);
    }
    k22_timing_internal_update_ftm_irq(timing, index);
}

static void ftm_quadrature_transition(K22Timing* timing, uint8_t index, uint8_t channel,
                                      bool previous, bool current) {
    K22FtmState* ftm = &timing->ftm[index];
    const bool polarity = (ftm->registers[11] & (1u << (5u - channel))) != 0u;
    const bool before = previous != polarity;
    const bool after = current != polarity;
    if (before == after)
        return;
    const bool phase_a = ftm->channel_filtered_input[0] != ((ftm->registers[11] & 0x20u) != 0u);
    const bool phase_b = ftm->channel_filtered_input[1] != ((ftm->registers[11] & 0x10u) != 0u);
    if ((ftm->registers[11] & 8u) != 0u) {
        if (channel == 0u && !before && after)
            ftm_quadrature_step(timing, index, phase_b);
        return;
    }
    const bool increment = channel == 0u ? after != phase_b : phase_a == after;
    ftm_quadrature_step(timing, index, increment);
}

static void ftm_capture_input(K22Timing* timing, uint8_t index, uint8_t channel, bool previous,
                              bool current) {
    K22FtmState* ftm = &timing->ftm[index];
    if (ftm_quadrature_enabled(ftm) && channel < 2u) {
        ftm_quadrature_transition(timing, index, channel, previous, current);
        return;
    }
    const uint8_t edges = (uint8_t)((ftm->channel_sc[channel] >> 2u) & 3u);
    const bool selected = current ? (edges & 1u) != 0u : (edges & 2u) != 0u;
    if (previous == current || !selected ||
        !k22_timing_internal_ftm_input_capture_mode(ftm, channel))
        return;
    ftm->channel_value[channel] = ftm->counter;
    ftm_channel_event(timing, index, channel);
    if ((ftm->channel_sc[channel] & 2u) != 0u) {
        ftm->counter = ftm->initial;
        ftm->counting_down = false;
        ftm->overflow_count = 0u;
        ftm->remainder = 0u;
        if ((ftm->registers[6] & (1u << 6u)) != 0u)
            k22_timing_internal_ftm_trigger(timing, index);
    }
    k22_timing_internal_update_ftm_irq(timing, index);
}

static void ftm_apply_outmask(K22FtmState* ftm) {
    if (ftm->outmask_pending) {
        ftm->registers[3] = ftm->outmask_buffer;
        ftm->outmask_pending = false;
    }
}

static void ftm_apply_invctrl(K22FtmState* ftm) {
    if (ftm->invctrl_pending) {
        ftm->registers[15] = ftm->invctrl_buffer;
        ftm->invctrl_pending = false;
    }
}

static void ftm_apply_swoctrl(K22FtmState* ftm) {
    if (ftm->swoctrl_pending) {
        ftm->registers[16] = ftm->swoctrl_buffer;
        ftm->swoctrl_pending = false;
    }
}

static void ftm_apply_modulo(K22FtmState* ftm) {
    if (ftm->modulo_pending) {
        ftm->modulo = ftm->modulo_buffer;
        ftm->modulo_pending = false;
    }
}

static void ftm_apply_initial(K22FtmState* ftm) {
    if (ftm->initial_pending) {
        ftm->initial = ftm->initial_buffer;
        ftm->initial_pending = false;
    }
}

static void ftm_apply_channel_value(K22FtmState* ftm, uint8_t channel) {
    if (ftm->channel_value_pending[channel]) {
        ftm->channel_value[channel] = ftm->channel_value_buffer[channel];
        ftm->channel_value_pending[channel] = false;
    }
}

static bool ftm_pair_synchronization_enabled(const K22FtmState* ftm, uint8_t channel) {
    return (ftm->registers[4] & (1u << ((channel / 2u) * 8u + 5u))) != 0u;
}

static void ftm_apply_synchronized_write_buffers(K22FtmState* ftm, bool enhanced) {
    if ((ftm->registers[0] & 1u) == 0u)
        return;
    ftm_apply_modulo(ftm);
    if (enhanced && (ftm->registers[14] & (1u << 2u)) != 0u)
        ftm_apply_initial(ftm);
    for (uint8_t channel = 0u; channel < 8u; channel++) {
        if (ftm_pair_synchronization_enabled(ftm, channel))
            ftm_apply_channel_value(ftm, channel);
    }
}

static void ftm_apply_intermediate_load(K22FtmState* ftm) {
    ftm_apply_modulo(ftm);
    if ((ftm->registers[14] & (1u << 2u)) != 0u)
        ftm_apply_initial(ftm);
    for (uint8_t channel = 0u; channel < 8u; channel++) {
        if (ftm_pair_synchronization_enabled(ftm, channel))
            ftm_apply_channel_value(ftm, channel);
    }
    ftm->registers[17] &= ~(1u << 9u);
}

static void ftm_loading_point(K22FtmState* ftm, bool minimum, bool maximum,
                              uint8_t channel_matches) {
    if (ftm->software_sync_pending && (((ftm->registers[1] & 1u) != 0u && minimum) ||
                                       ((ftm->registers[1] & 2u) != 0u && maximum))) {
        const bool enhanced = (ftm->registers[14] & (1u << 7u)) != 0u;
        const bool write_buffers = enhanced ? (ftm->registers[14] & (1u << 9u)) != 0u : true;
        if (write_buffers)
            ftm_apply_synchronized_write_buffers(ftm, enhanced);
        ftm->registers[1] &= ~0x80u;
        ftm->software_sync_pending = false;
    }
    if (ftm->hardware_sync_pending && (((ftm->registers[1] & 1u) != 0u && minimum) ||
                                       ((ftm->registers[1] & 2u) != 0u && maximum))) {
        const bool enhanced = (ftm->registers[14] & (1u << 7u)) != 0u;
        if (!enhanced || (ftm->registers[14] & (1u << 17u)) != 0u)
            ftm_apply_synchronized_write_buffers(ftm, enhanced);
        ftm->hardware_sync_pending = false;
    }
    const bool intermediate = maximum || (channel_matches & (uint8_t)ftm->registers[17]) != 0u;
    if ((ftm->registers[17] & (1u << 9u)) != 0u && intermediate)
        ftm_apply_intermediate_load(ftm);
}

static void ftm_apply_system_clock_updates(K22FtmState* ftm) {
    if ((ftm->registers[1] & 8u) == 0u)
        ftm_apply_outmask(ftm);
    if ((ftm->registers[14] & (1u << 4u)) == 0u)
        ftm_apply_invctrl(ftm);
    if ((ftm->registers[14] & (1u << 5u)) == 0u)
        ftm_apply_swoctrl(ftm);
    if (ftm->initial_pending &&
        (((ftm->registers[0] & 1u) == 0u) || (ftm->registers[14] & (1u << 2u)) == 0u))
        ftm_apply_initial(ftm);
}

void k22_timing_internal_ftm_apply_software_sync(K22FtmState* ftm) {
    const uint32_t synconf = ftm->registers[14];
    const bool enhanced = (synconf & (1u << 7u)) != 0u;
    if ((synconf & (1u << 12u)) != 0u && (synconf & (1u << 5u)) != 0u)
        ftm_apply_swoctrl(ftm);
    if ((synconf & (1u << 11u)) != 0u && (synconf & (1u << 4u)) != 0u)
        ftm_apply_invctrl(ftm);
    if ((enhanced && (synconf & (1u << 10u)) != 0u && (ftm->registers[1] & 8u) != 0u) ||
        (!enhanced && (ftm->registers[0] & 8u) == 0u && (ftm->registers[1] & 8u) != 0u))
        ftm_apply_outmask(ftm);
    const bool reset_counter =
        (enhanced && (synconf & (1u << 8u)) != 0u) || (!enhanced && (ftm->registers[1] & 4u) != 0u);
    if (reset_counter) {
        const bool write_buffers = enhanced ? (synconf & (1u << 9u)) != 0u : true;
        if (write_buffers)
            ftm_apply_synchronized_write_buffers(ftm, enhanced);
        ftm->counter = ftm->initial;
        ftm->counting_down = false;
        ftm->overflow_count = 0u;
        ftm->remainder = 0u;
        ftm->registers[1] &= ~0x80u;
        ftm->software_sync_pending = false;
    } else {
        ftm->software_sync_pending = true;
    }
}

static void ftm_apply_hardware_sync(K22FtmState* ftm) {
    const uint32_t synconf = ftm->registers[14];
    const bool enhanced = (synconf & (1u << 7u)) != 0u;
    if (!enhanced) {
        if ((ftm->registers[1] & 8u) != 0u)
            ftm_apply_outmask(ftm);
        const bool reset_counter = (ftm->registers[1] & 4u) != 0u;
        const bool synchronize_buffers = (ftm->registers[0] & 8u) == 0u;
        if (reset_counter) {
            if (synchronize_buffers)
                ftm_apply_synchronized_write_buffers(ftm, false);
            ftm->counter = ftm->initial;
            ftm->counting_down = false;
            ftm->overflow_count = 0u;
            ftm->remainder = 0u;
            ftm->hardware_sync_pending = false;
        } else {
            ftm->hardware_sync_pending = synchronize_buffers;
        }
        return;
    }
    if ((synconf & (1u << 20u)) != 0u && (synconf & (1u << 5u)) != 0u)
        ftm_apply_swoctrl(ftm);
    if ((synconf & (1u << 19u)) != 0u && (synconf & (1u << 4u)) != 0u)
        ftm_apply_invctrl(ftm);
    if ((synconf & (1u << 18u)) != 0u && (ftm->registers[1] & 8u) != 0u)
        ftm_apply_outmask(ftm);
    if ((synconf & (1u << 16u)) != 0u) {
        if ((synconf & (1u << 17u)) != 0u)
            ftm_apply_synchronized_write_buffers(ftm, true);
        ftm->counter = ftm->initial;
        ftm->counting_down = false;
        ftm->overflow_count = 0u;
        ftm->remainder = 0u;
        ftm->hardware_sync_pending = false;
    } else {
        ftm->hardware_sync_pending = true;
    }
}

static void ftm_process_hardware_triggers(K22Timing* timing, uint8_t index) {
    K22FtmState* ftm = &timing->ftm[index];
    if (ftm->hardware_trigger_pending_mask == 0u || !ftm_gate(timing, index))
        return;
    const uint8_t pending = ftm->hardware_trigger_pending_mask;
    ftm->hardware_trigger_pending_mask = 0u;
    const uint8_t enabled = (uint8_t)((ftm->registers[1] >> 4u) & 7u);
    const uint8_t detected = pending & enabled;
    if (detected == 0u)
        return;
    if ((ftm->registers[14] & 1u) == 0u)
        ftm->registers[1] &= ~((uint32_t)detected << 4u);
    ftm_apply_hardware_sync(ftm);
}

static bool ftm_has_deadtime(const K22FtmState* ftm, uint8_t channels) {
    for (uint8_t channel = 0u; channel < channels; channel += 2u) {
        if (ftm_deadtime_enabled(ftm, channel))
            return true;
    }
    return false;
}

static void ftm_advance_deadtime(K22Timing* timing, uint8_t index, uint32_t cycles) {
    K22FtmState* ftm = &timing->ftm[index];
    const uint8_t channels = k22_timing_internal_ftm_channel_count(index);
    if (!ftm_gate(timing, index))
        return;
    const uint8_t divider = (uint8_t)(ftm->registers[5] >> 6u);
    const uint8_t shift = divider < 2u ? 0u : divider == 2u ? 2u : 4u;
    const uint64_t ticks = k22_timing_internal_clock_ticks(
        &ftm->deadtime_remainder, cycles, timing->bus_clock_hz >> shift, timing->core_clock_hz);
    for (uint8_t channel = 0u; channel < channels; channel++) {
        const bool raw = ftm_pre_deadtime_output(ftm, channel);
        if (!ftm_deadtime_enabled(ftm, channel)) {
            ftm->channel_deadtime_output[channel] = raw;
            ftm->channel_deadtime_remaining[channel] = 0u;
        } else if (!raw) {
            ftm->channel_deadtime_output[channel] = false;
            ftm->channel_deadtime_remaining[channel] = 0u;
        } else if (!ftm->channel_deadtime_output[channel] &&
                   ftm->channel_deadtime_remaining[channel] == 0u) {
            ftm->channel_deadtime_remaining[channel] = ftm->registers[5] & 0x3fu;
        } else if (!ftm->channel_deadtime_output[channel] && ticks != 0u) {
            const uint32_t remaining = ftm->channel_deadtime_remaining[channel];
            if (ticks >= remaining) {
                ftm->channel_deadtime_remaining[channel] = 0u;
                ftm->channel_deadtime_output[channel] = true;
            } else {
                ftm->channel_deadtime_remaining[channel] -= (uint32_t)ticks;
            }
        }
    }
}

void k22_timing_internal_advance_ftm(K22Timing* timing, uint8_t index, uint32_t cycles) {
    K22FtmState* ftm = &timing->ftm[index];
    ftm_apply_system_clock_updates(ftm);
    ftm_process_hardware_triggers(timing, index);
    const uint8_t channels = k22_timing_internal_ftm_channel_count(index);
    uint32_t remaining = cycles;
    while (remaining != 0u) {
        uint32_t segment =
            ftm_has_deadtime(ftm, channels) || ftm_fault_processing_active(ftm) ? 1u : remaining;
        for (uint8_t channel = 0u; channel < channels; channel++) {
            if (ftm->channel_input[channel] == ftm->channel_filtered_input[channel])
                continue;
            const uint32_t threshold = ftm_input_threshold(ftm, channel);
            const uint32_t until_event = ftm->channel_input_age[channel] >= threshold
                                             ? 0u
                                             : threshold - ftm->channel_input_age[channel];
            if (until_event < segment)
                segment = until_event;
        }
        ftm_advance_fault_inputs(timing, index, segment);
        advance_ftm_counter(timing, index, segment);
        ftm_advance_deadtime(timing, index, segment);
        remaining -= segment;
        for (uint8_t channel = 0u; channel < channels; channel++) {
            if (ftm->channel_input[channel] == ftm->channel_filtered_input[channel])
                continue;
            ftm->channel_input_age[channel] += segment;
            if (ftm->channel_input_age[channel] < ftm_input_threshold(ftm, channel))
                continue;
            const bool previous = ftm->channel_filtered_input[channel];
            ftm->channel_filtered_input[channel] = ftm->channel_input[channel];
            ftm->channel_input_age[channel] = 0u;
            ftm_capture_input(timing, index, channel, previous,
                              ftm->channel_filtered_input[channel]);
        }
    }
}
