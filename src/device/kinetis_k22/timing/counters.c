#include "internal.h"

static uint32_t advance_pit_channel(K22Timing* timing, uint8_t channel, uint64_t ticks) {
    K22PitChannel* pit = &timing->pit[channel];
    if ((pit->control & 1u) == 0 || ticks == 0) {
        return 0;
    }
    const uint64_t first = (uint64_t)pit->current + 1u;
    uint32_t expirations = 0;
    if (ticks >= first) {
        ticks -= first;
        expirations = 1u;
        const uint64_t period = (uint64_t)pit->load + 1u;
        const uint64_t additional = ticks / period;
        expirations = additional >= UINT32_MAX ? UINT32_MAX : expirations + (uint32_t)additional;
        pit->current = pit->load - (uint32_t)(ticks % period);
        pit->flag = true;
        if ((pit->control & 2u) != 0) {
            k22_timing_internal_set_irq(timing, IRQ_PIT0 + channel, true);
        }
        k22_timing_internal_trigger_dma(timing, channel);
        k22_timing_internal_trigger_adc_alternate(timing, (uint8_t)(4u + channel));
    } else {
        pit->current -= (uint32_t)ticks;
    }
    return expirations;
}

void k22_timing_internal_advance_pit(K22Timing* timing, uint32_t cycles) {
    if (!k22_timing_internal_has(timing, K22_PERIPHERAL_PIT) ||
        (timing->sim_scgc6 & (1u << 23u)) == 0 || (timing->pit_mcr & 2u) != 0 ||
        ((timing->pit_mcr & 1u) != 0u && timing->debug_halted)) {
        return;
    }
    const uint64_t ticks = k22_timing_internal_clock_ticks(
        &timing->pit_remainder, cycles, timing->bus_clock_hz, timing->core_clock_hz);
    uint64_t source = ticks;
    for (uint8_t channel = 0; channel < 4; channel++) {
        if ((timing->pit[channel].control & 4u) == 0) {
            source = ticks;
        }
        source = advance_pit_channel(timing, channel, source);
    }
}

bool k22_timing_internal_pit_read(const K22Timing* timing, uint32_t address, uint8_t size,
                                  uint32_t* value) {
    if (size != 4) {
        return false;
    }
    if (address == PIT_BASE) {
        *value = timing->pit_mcr;
        return true;
    }
    if (address < PIT_CHANNEL_BASE || address >= PIT_CHANNEL_BASE + 0x40u) {
        return false;
    }
    const uint8_t channel = (uint8_t)((address - PIT_CHANNEL_BASE) / 0x10u);
    switch ((address - PIT_CHANNEL_BASE) & 0x0fu) {
    case 0:
        *value = timing->pit[channel].load;
        return true;
    case 4:
        *value = timing->pit[channel].current;
        return true;
    case 8:
        *value = timing->pit[channel].control;
        return true;
    case 12:
        *value = timing->pit[channel].flag ? 1u : 0u;
        return true;
    default:
        return false;
    }
}

bool k22_timing_internal_pit_write(K22Timing* timing, uint32_t address, uint8_t size,
                                   uint32_t value) {
    if (size != 4) {
        return false;
    }
    if (address == PIT_BASE) {
        timing->pit_mcr = value & 3u;
        return true;
    }
    if (address < PIT_CHANNEL_BASE || address >= PIT_CHANNEL_BASE + 0x40u) {
        return false;
    }
    const uint8_t channel = (uint8_t)((address - PIT_CHANNEL_BASE) / 0x10u);
    switch ((address - PIT_CHANNEL_BASE) & 0x0fu) {
    case 0:
        timing->pit[channel].load = value;
        return true;
    case 8: {
        K22PitChannel* pit = &timing->pit[channel];
        const bool was_enabled = (pit->control & 1u) != 0u;
        pit->control = value & (channel == 0u ? 3u : 7u);
        if (!was_enabled && (pit->control & 1u) != 0u)
            pit->current = pit->load;
        k22_timing_internal_set_irq(timing, IRQ_PIT0 + channel,
                                    pit->flag && (pit->control & 2u) != 0u);
        return true;
    }
    case 12:
        if ((value & 1u) != 0) {
            timing->pit[channel].flag = false;
            k22_timing_internal_set_irq(timing, IRQ_PIT0 + channel, false);
        }
        return true;
    default:
        return false;
    }
}

static uint32_t lptmr_clock(const K22Timing* timing) {
    switch (timing->lptmr_psr & 3u) {
    case 0:
        return (timing->mcg[1] & 1u) != 0 ? timing->fast_irc_hz : timing->slow_irc_hz;
    case 1:
        return timing->lpo_hz;
    case 2:
        return timing->rtc_oscillator_hz;
    default:
        return timing->external_oscillator_hz;
    }
}

static bool lptmr_running(const K22Timing* timing) {
    return k22_timing_internal_has(timing, K22_PERIPHERAL_LPTMR0) &&
           (timing->sim_scgc5 & 1u) != 0 && (timing->lptmr_csr & 1u) != 0;
}

bool k22_timing_internal_lptmr_selected_active(const K22Timing* timing) {
    const uint8_t input = (uint8_t)((timing->lptmr_csr >> 4u) & 3u);
    if (input >= 3u)
        return false;
    const bool high = timing->lptmr_input[input];
    return (timing->lptmr_csr & 8u) == 0 ? high : !high;
}

static void increment_lptmr(K22Timing* timing, uint64_t ticks) {
    if (ticks == 0)
        return;
    const uint32_t compare = timing->lptmr_cmr & 0xffffu;
    if ((timing->lptmr_csr & 4u) == 0) {
        const uint64_t period = (uint64_t)compare + 1u;
        const uint64_t total = (uint64_t)timing->lptmr_counter + ticks;
        if (total >= period) {
            timing->lptmr_csr |= 0x80u;
            if ((timing->lptmr_csr & 0x40u) != 0)
                k22_timing_internal_set_irq(timing, IRQ_LPTMR, true);
            k22_timing_internal_trigger_adc_alternate(timing, 14u);
        }
        timing->lptmr_counter = (uint16_t)(total % period);
        return;
    }
    const uint32_t distance = ((compare - timing->lptmr_counter) & 0xffffu) + 1u;
    if (ticks >= distance) {
        timing->lptmr_csr |= 0x80u;
        if ((timing->lptmr_csr & 0x40u) != 0)
            k22_timing_internal_set_irq(timing, IRQ_LPTMR, true);
        k22_timing_internal_trigger_adc_alternate(timing, 14u);
    }
    timing->lptmr_counter = (uint16_t)((uint64_t)timing->lptmr_counter + ticks);
}

static void sample_lptmr_filter(K22Timing* timing, uint32_t cycles) {
    const uint8_t prescale = (uint8_t)((timing->lptmr_psr >> 3u) & 15u);
    if (prescale == 0u)
        return;
    const uint64_t samples = k22_timing_internal_clock_ticks(
        &timing->lptmr_filter_remainder, cycles, lptmr_clock(timing), timing->core_clock_hz);
    const bool active = k22_timing_internal_lptmr_selected_active(timing);
    if (active == timing->lptmr_observed_active) {
        timing->lptmr_filter_ticks = 0u;
        return;
    }
    const uint32_t threshold = 1u << prescale;
    const uint64_t total = (uint64_t)timing->lptmr_filter_ticks + samples;
    if (total < threshold) {
        timing->lptmr_filter_ticks = (uint32_t)total;
        return;
    }
    timing->lptmr_filter_ticks = 0u;
    timing->lptmr_observed_active = active;
    if (active)
        increment_lptmr(timing, 1u);
}

void k22_timing_internal_advance_lptmr(K22Timing* timing, uint32_t cycles) {
    if (!lptmr_running(timing)) {
        return;
    }
    if ((timing->lptmr_csr & 2u) != 0) {
        if ((timing->lptmr_psr & 4u) == 0)
            sample_lptmr_filter(timing, cycles);
        return;
    }
    uint32_t source_hz = lptmr_clock(timing);
    if ((timing->lptmr_psr & 4u) == 0) {
        source_hz >>= ((timing->lptmr_psr >> 3u) & 15u) + 1u;
    }
    const uint64_t ticks = k22_timing_internal_clock_ticks(&timing->lptmr_remainder, cycles,
                                                           source_hz, timing->core_clock_hz);
    increment_lptmr(timing, ticks);
}

bool k22_timing_set_lptmr_input(K22Timing* timing, uint8_t input, bool high) {
    if (timing == NULL || timing->profile == NULL || input >= 3u ||
        !k22_timing_internal_has(timing, K22_PERIPHERAL_LPTMR0))
        return false;
    timing->lptmr_input[input] = high;
    if (!lptmr_running(timing) || (timing->lptmr_csr & 2u) == 0 || (timing->lptmr_psr & 4u) == 0 ||
        ((timing->lptmr_csr >> 4u) & 3u) != input)
        return true;
    const bool active = k22_timing_internal_lptmr_selected_active(timing);
    if (active != timing->lptmr_observed_active) {
        timing->lptmr_observed_active = active;
        if (active)
            increment_lptmr(timing, 1u);
    }
    return true;
}

uint32_t k22_timing_internal_rtc_access_reset(const K22Timing* timing) {
    return timing->profile->id == K22_PROFILE_MK22FN1M012 ||
                   timing->profile->id == K22_PROFILE_MK22FX51212
               ? 0xffffu
               : 0xffu;
}

void k22_timing_internal_update_rtc_irq(const K22Timing* timing) {
    const uint32_t enabled_flags = timing->rtc_ier & timing->rtc_sr & 7u;
    k22_timing_internal_set_irq(timing, IRQ_RTC, enabled_flags != 0u);
}

static uint32_t rtc_second_ticks(const K22Timing* timing) {
    const int8_t compensation = (int8_t)(timing->rtc_tcr >> 16u);
    return (uint32_t)(32768 - compensation);
}

static void rtc_complete_second(K22Timing* timing) {
    const uint8_t interval_counter = (uint8_t)(timing->rtc_tcr >> 24u);
    if (interval_counter == 0u) {
        timing->rtc_tcr = (timing->rtc_tcr & 0xffffu) | ((timing->rtc_tcr & 0xff00u) << 16u) |
                          ((timing->rtc_tcr & 0xffu) << 16u);
    } else {
        timing->rtc_tcr = (timing->rtc_tcr & 0xffffu) | ((uint32_t)(interval_counter - 1u) << 24u);
    }
    const bool overflow = timing->rtc_tsr == UINT32_MAX;
    if (overflow) {
        timing->rtc_tsr = 0u;
        timing->rtc_tpr = 0u;
        timing->rtc_subsecond_ticks = 0u;
        timing->rtc_sr |= 2u;
    } else {
        timing->rtc_tsr++;
    }
    k22_timing_internal_trigger_adc_alternate(timing, 13u);
    k22_timing_internal_set_irq(timing, IRQ_RTC_SECONDS, (timing->rtc_ier & 0x10u) != 0u);
    k22_timing_internal_set_irq(timing, IRQ_RTC_SECONDS, false);
    if (timing->rtc_tsr == timing->rtc_tar) {
        timing->rtc_sr |= 4u;
        k22_timing_internal_trigger_adc_alternate(timing, 12u);
    }
    k22_timing_internal_update_rtc_irq(timing);
}

void k22_timing_internal_advance_rtc(K22Timing* timing, uint32_t cycles) {
    if (!k22_timing_internal_has(timing, K22_PERIPHERAL_RTC) || (timing->rtc_cr & 0x100u) == 0u ||
        (timing->rtc_sr & 0x10u) == 0 || (timing->rtc_sr & 3u) != 0) {
        return;
    }
    const uint64_t ticks = k22_timing_internal_clock_ticks(
        &timing->rtc_remainder, cycles, timing->rtc_oscillator_hz, timing->core_clock_hz);
    uint64_t remaining = ticks;
    while (remaining != 0u && (timing->rtc_sr & 2u) == 0u) {
        const uint32_t second_ticks = rtc_second_ticks(timing);
        const uint32_t needed = second_ticks - timing->rtc_subsecond_ticks;
        if (remaining < needed) {
            timing->rtc_subsecond_ticks += (uint32_t)remaining;
            remaining = 0u;
        } else {
            remaining -= needed;
            timing->rtc_subsecond_ticks = 0u;
            rtc_complete_second(timing);
        }
    }
    timing->rtc_tpr =
        (uint16_t)(timing->rtc_subsecond_ticks > 0x7fffu ? 0x7fffu : timing->rtc_subsecond_ticks);
}

static uint32_t pdb_divider(uint32_t sc) {
    static const uint16_t multipliers[4] = {1u, 10u, 20u, 40u};
    return (1u << ((sc >> 12u) & 7u)) * multipliers[(sc >> 2u) & 3u];
}

bool k22_timing_internal_pdb_auxiliary_offset(uint32_t offset) {
    return (offset >= 0x10u && offset <= 0x1cu && (offset & 3u) == 0) ||
           (offset >= 0x38u && offset <= 0x44u && (offset & 3u) == 0) ||
           (offset >= 0x150u && offset <= 0x15cu && (offset & 3u) == 0) ||
           (offset >= 0x190u && offset <= 0x198u && (offset & 3u) == 0);
}

static bool counter_reached(uint16_t start, uint64_t ticks, uint32_t period, uint16_t target) {
    if (ticks >= period)
        return true;
    const uint32_t distance =
        target > start ? (uint32_t)target - start : period - ((uint32_t)start - target);
    return ticks >= distance;
}

void k22_timing_internal_advance_pdb(K22Timing* timing, uint32_t cycles) {
    if (!k22_timing_internal_has(timing, K22_PERIPHERAL_PDB0) ||
        (timing->sim_scgc6 & (1u << 22u)) == 0 || (timing->pdb_sc & 1u) == 0) {
        return;
    }
    const uint32_t source_hz = timing->bus_clock_hz / pdb_divider(timing->pdb_sc);
    const uint64_t ticks = k22_timing_internal_clock_ticks(&timing->pdb_remainder, cycles,
                                                           source_hz, timing->core_clock_hz);
    if (ticks == 0) {
        return;
    }
    const uint64_t period = (uint64_t)timing->pdb_mod + 1u;
    const uint64_t total = (uint64_t)timing->pdb_counter + ticks;
    const bool delayed =
        timing->pdb_idly <= timing->pdb_mod &&
        counter_reached((uint16_t)(total - ticks), ticks, (uint32_t)period, timing->pdb_idly);
    timing->pdb_counter = (uint16_t)(total % period);
    for (uint8_t channel = 0; channel < 2u; channel++) {
        const uint32_t base = 0x10u + (uint32_t)channel * 0x28u;
        const uint32_t control = timing->pdb_registers[base >> 2u];
        for (uint8_t pretrigger = 0; pretrigger < 2u; pretrigger++) {
            const uint16_t delay =
                (uint16_t)timing->pdb_registers[(base + 8u + (uint32_t)pretrigger * 4u) >> 2u];
            if ((control & (1u << pretrigger)) != 0 &&
                counter_reached((uint16_t)(total - ticks), ticks, (uint32_t)period, delay)) {
                timing->pdb_registers[(base + 4u) >> 2u] |= 1u << pretrigger;
                k22_timing_internal_trigger(timing, K22_TIMING_TRIGGER_PDB_ADC, channel,
                                            pretrigger);
            }
        }
    }
    for (uint8_t instance = 0; instance < 2u; instance++) {
        const uint32_t interval_offset = 0x150u + (uint32_t)instance * 8u;
        const uint32_t control_offset = interval_offset + 4u;
        const uint16_t interval = (uint16_t)timing->pdb_registers[interval_offset >> 2u];
        const uint32_t control = timing->pdb_registers[control_offset >> 2u];
        if ((control & 1u) != 0 && interval <= timing->pdb_mod &&
            counter_reached((uint16_t)(total - ticks), ticks, (uint32_t)period, interval)) {
            k22_timing_internal_trigger(timing, K22_TIMING_TRIGGER_PDB_DAC, instance, 0);
        }
    }
    if (delayed) {
        timing->pdb_sc |= 1u << 6u;
        if ((timing->pdb_sc & (1u << 5u)) != 0) {
            k22_timing_internal_set_irq(timing, IRQ_PDB, true);
        }
    }
    if ((timing->pdb_sc & 2u) == 0 && total >= period) {
        timing->pdb_sc &= ~1u;
    }
}

bool k22_timing_internal_ftm_location(const K22Timing* timing, uint32_t address, uint8_t* index,
                                      uint32_t* offset) {
    const K22PeripheralId ids[4] = {K22_PERIPHERAL_FTM0, K22_PERIPHERAL_FTM1, K22_PERIPHERAL_FTM2,
                                    K22_PERIPHERAL_FTM3};
    for (uint8_t item = 0; item < 4; item++) {
        K22PeripheralBlock block;
        if (k22_profile_peripheral_block(timing->profile, ids[item], &block) &&
            address >= block.address && address < block.address + block.size) {
            *index = item;
            *offset = address - block.address;
            return true;
        }
    }
    return false;
}
