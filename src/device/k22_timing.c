#include "k22_timing.h"

#include <limits.h>
#include <string.h>

enum {
    SIM_BASE = 0x40047000u,
    SIM_SOPT1 = SIM_BASE,
    SIM_SOPT1CFG = SIM_BASE + 0x4u,
    SIM_SCGC3 = SIM_BASE + 0x1030u,
    SIM_SOPT2 = SIM_BASE + 0x1004u,
    SIM_SOPT4 = SIM_BASE + 0x100cu,
    SIM_SOPT5 = SIM_BASE + 0x1010u,
    SIM_SOPT7 = SIM_BASE + 0x1018u,
    SIM_SOPT8 = SIM_BASE + 0x101cu,
    SIM_SDID = SIM_BASE + 0x1024u,
    SIM_SCGC4 = SIM_BASE + 0x1034u,
    SIM_SCGC5 = SIM_BASE + 0x1038u,
    SIM_SCGC6 = SIM_BASE + 0x103cu,
    SIM_SCGC7 = SIM_BASE + 0x1040u,
    SIM_CLKDIV1 = SIM_BASE + 0x1044u,
    SIM_CLKDIV2 = SIM_BASE + 0x1048u,
    SIM_FCFG1 = SIM_BASE + 0x104cu,
    SIM_FCFG2 = SIM_BASE + 0x1050u,
    MCG_BASE = 0x40064000u,
    OSC_BASE = 0x40065000u,
    LLWU_BASE = 0x4007c000u,
    PMC_BASE = 0x4007d000u,
    SMC_BASE = 0x4007e000u,
    RCM_BASE = 0x4007f000u,
    PIT_BASE = 0x40037000u,
    PIT_CHANNEL_BASE = PIT_BASE + 0x100u,
    LPTMR_BASE = 0x40040000u,
    RTC_BASE = 0x4003d000u,
    PDB_BASE = 0x40036000u,
    WDOG_BASE = 0x40052000u,
    EWM_BASE = 0x40061000u,
    IRQ_LVD = 20u,
    IRQ_LLWU = 21u,
    IRQ_WDOG_EWM = 22u,
    IRQ_FTM0 = 42u,
    IRQ_RTC = 46u,
    IRQ_RTC_SECONDS = 47u,
    IRQ_PIT0 = 48u,
    IRQ_PDB = 52u,
    IRQ_LPTMR = 58u,
    IRQ_FTM3 = 71u,
};

static void set_irq(const K22Timing* timing, uint8_t irq, bool asserted) {
    if (timing->signals.irq != NULL) {
        timing->signals.irq(timing->signals.context, irq, asserted);
    }
}

static void update_pmc_irq(const K22Timing* timing) {
    const bool detect = (timing->pmc[0] & 0xa0u) == 0xa0u;
    const bool warning = (timing->pmc[1] & 0xa0u) == 0xa0u;
    set_irq(timing, IRQ_LVD, detect || warning);
}

static void update_llwu_irq(const K22Timing* timing) {
    const bool pin = (timing->llwu[5] | timing->llwu[6]) != 0u;
    const bool module = timing->llwu[7] != 0u;
    const bool filter = ((timing->llwu[8] | timing->llwu[9]) & 0x80u) != 0u;
    set_irq(timing, IRQ_LLWU, pin || module || filter);
}

static void request_dma(const K22Timing* timing, uint8_t source) {
    if (timing->signals.dma != NULL) {
        timing->signals.dma(timing->signals.context, source);
    }
}

static void trigger_dma(const K22Timing* timing, uint8_t channel) {
    if (timing->signals.dma_trigger != NULL)
        timing->signals.dma_trigger(timing->signals.context, channel);
}

static void trigger(K22Timing* timing, K22TimingTrigger type, uint8_t instance, uint8_t channel) {
    if (timing->signals.trigger != NULL)
        timing->signals.trigger(timing->signals.context, type, instance, channel);
}

static void trigger_adc_alternate(K22Timing* timing, uint8_t source) {
    trigger(timing, K22_TIMING_TRIGGER_ADC_ALTERNATE, source, 0u);
}

static bool has(const K22Timing* timing, K22PeripheralId peripheral) {
    return timing->profile != NULL && k22_profile_has_peripheral(timing->profile, peripheral);
}

static bool contains(const K22Timing* timing, K22PeripheralId peripheral, uint32_t address,
                     uint8_t size) {
    K22PeripheralLocation location;
    return k22_profile_resolve_peripheral(timing->profile, address, size, &location) &&
           location.id == peripheral;
}

static bool mcg_register(uint32_t offset) {
    return offset <= 6u || offset == 8u || (offset >= 10u && offset <= 13u);
}

static uint64_t clock_ticks(uint64_t* remainder, uint32_t cycles, uint32_t source_hz,
                            uint32_t core_hz) {
    if (source_hz == 0 || core_hz == 0) {
        return 0;
    }
    const uint64_t scaled = *remainder + (uint64_t)cycles * source_hz;
    *remainder = scaled % core_hz;
    return scaled / core_hz;
}

static uint32_t fll_clock(const K22Timing* timing) {
    const uint8_t c4 = timing->mcg[3];
    const uint16_t multipliers[4] = {640u, 1280u, 1920u, 2560u};
    uint32_t reference = timing->slow_irc_hz;
    if ((timing->mcg[0] & 4u) == 0) {
        const uint8_t index = (timing->mcg[0] >> 3u) & 7u;
        const uint16_t low_range_dividers[8] = {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u};
        const uint16_t high_range_dividers[8] = {32u, 64u, 128u, 256u, 512u, 1024u, 1280u, 1536u};
        const uint16_t divider =
            (timing->mcg[1] & 0x30u) == 0 ? low_range_dividers[index] : high_range_dividers[index];
        reference = timing->external_oscillator_hz / divider;
    }
    uint32_t multiplier = multipliers[(c4 >> 5u) & 3u];
    if ((c4 & 0x80u) != 0) {
        const uint16_t dmx_multipliers[4] = {732u, 1464u, 2197u, 2929u};
        multiplier = dmx_multipliers[(c4 >> 5u) & 3u];
    }
    return reference == 0 ? timing->slow_irc_hz * multiplier : reference * multiplier;
}

static uint32_t pll_clock(const K22Timing* timing) {
    if (timing->external_oscillator_hz == 0) {
        return 0;
    }
    const uint32_t divider = (timing->mcg[4] & 0x1fu) + 1u;
    const uint32_t multiplier = (timing->mcg[5] & 0x1fu) + 24u;
    return (uint32_t)(((uint64_t)timing->external_oscillator_hz * multiplier) / (divider * 2u));
}

static void update_clocks(K22Timing* timing) {
    const uint8_t clks = (timing->mcg[0] >> 6u) & 3u;
    uint32_t mcgout = 0;
    uint8_t status = timing->mcg[6] & 1u;
    if (timing->external_oscillator_hz != 0 && (timing->mcg[1] & 4u) != 0) {
        status |= 2u;
    }
    if (clks == 1u) {
        mcgout = (timing->mcg[1] & 1u) != 0 ? timing->fast_irc_hz : timing->slow_irc_hz;
        status |= 1u << 2u;
        status |= 1u << 4u;
    } else if (clks == 2u) {
        mcgout = timing->external_oscillator_hz;
        status |= 2u << 2u;
    } else if ((timing->mcg[5] & 0x40u) != 0) {
        mcgout = pll_clock(timing);
        status |= 3u << 2u;
        status |= (1u << 5u) | (1u << 6u);
    } else {
        mcgout = fll_clock(timing);
        if ((timing->mcg[0] & 4u) != 0) {
            status |= 1u << 4u;
        }
    }
    if (mcgout == 0) {
        mcgout = timing->slow_irc_hz;
    }
    timing->mcg[6] = status;
    const uint32_t core_divider = ((timing->sim_clkdiv1 >> 28u) & 15u) + 1u;
    const uint32_t bus_divider = ((timing->sim_clkdiv1 >> 24u) & 15u) + 1u;
    const uint32_t flash_divider = ((timing->sim_clkdiv1 >> 16u) & 15u) + 1u;
    timing->core_clock_hz = mcgout / core_divider;
    timing->bus_clock_hz = mcgout / bus_divider;
    timing->flash_clock_hz = mcgout / flash_divider;
    if (timing->core_clock_hz == 0) {
        timing->core_clock_hz = 1;
    }
}

static uint32_t sim_fcfg1(const K22Timing* timing) {
    return timing->profile->program_flash_size >= 1024u * 1024u ||
                   timing->profile->flexnvm_size != 0
               ? 0xff0f0f00u
               : 0x0f0f0f00u;
}

static bool read_sim(const K22Timing* timing, uint32_t address, uint8_t size, uint32_t* value) {
    if (size != 4) {
        return false;
    }
    switch (address) {
    case SIM_SOPT1:
        *value = timing->sim_sopt1;
        return true;
    case SIM_SOPT1CFG:
        *value = timing->sim_sopt1cfg;
        return true;
    case SIM_SOPT2:
        *value = timing->sim_sopt2;
        return true;
    case SIM_SOPT4:
        *value = timing->sim_sopt4;
        return true;
    case SIM_SOPT5:
        *value = timing->sim_sopt5;
        return true;
    case SIM_SOPT7:
        *value = timing->sim_sopt7;
        return true;
    case SIM_SOPT8:
        *value = timing->sim_sopt8;
        return true;
    case SIM_SDID:
        *value = timing->profile->sim_sdid_reset;
        return true;
    case SIM_SCGC3:
        if (timing->profile->id != K22_PROFILE_MK22FN1M012 &&
            timing->profile->id != K22_PROFILE_MK22FX51212)
            return false;
        *value = timing->sim_scgc3;
        return true;
    case SIM_SCGC4:
        *value = timing->sim_scgc4;
        return true;
    case SIM_SCGC5:
        *value = timing->sim_scgc5;
        return true;
    case SIM_SCGC6:
        *value = timing->sim_scgc6;
        return true;
    case SIM_SCGC7:
        *value = timing->sim_scgc7;
        return true;
    case SIM_CLKDIV1:
        *value = timing->sim_clkdiv1;
        return true;
    case SIM_CLKDIV2:
        *value = timing->sim_clkdiv2;
        return true;
    case SIM_FCFG1:
        *value = sim_fcfg1(timing);
        return true;
    case SIM_FCFG2:
        *value = timing->profile->id == K22_PROFILE_MK22FN1M012 ||
                         timing->profile->id == K22_PROFILE_MK22FX51212
                     ? 0x7f7f0000u
                     : 0x7fff0000u;
        return true;
    default:
        return false;
    }
}

static bool write_sim(K22Timing* timing, uint32_t address, uint8_t size, uint32_t value) {
    if (size != 4) {
        return false;
    }
    switch (address) {
    case SIM_SOPT1:
        timing->sim_sopt1 = value;
        return true;
    case SIM_SOPT1CFG:
        timing->sim_sopt1cfg = value & 0x01000000u;
        return true;
    case SIM_SOPT2:
        timing->sim_sopt2 = value;
        return true;
    case SIM_SOPT4:
        timing->sim_sopt4 = value;
        return true;
    case SIM_SOPT5:
        timing->sim_sopt5 = value;
        return true;
    case SIM_SOPT7:
        timing->sim_sopt7 = value & 0x00009f9fu;
        return true;
    case SIM_SOPT8:
        timing->sim_sopt8 = value;
        return true;
    case SIM_SCGC3:
        if (timing->profile->id != K22_PROFILE_MK22FN1M012 &&
            timing->profile->id != K22_PROFILE_MK22FX51212)
            return false;
        timing->sim_scgc3 = value;
        return true;
    case SIM_SCGC4:
        timing->sim_scgc4 = value;
        return true;
    case SIM_SCGC5:
        timing->sim_scgc5 = (timing->sim_scgc5 & ~0x00003e01u) | (value & 0x00003e01u);
        return true;
    case SIM_SCGC6:
        timing->sim_scgc6 = value;
        return true;
    case SIM_SCGC7:
        timing->sim_scgc7 = value;
        return true;
    case SIM_CLKDIV1:
        timing->sim_clkdiv1 = value;
        update_clocks(timing);
        return true;
    case SIM_CLKDIV2:
        timing->sim_clkdiv2 = value;
        return true;
    default:
        return false;
    }
}

static bool read_byte_block(const uint8_t* data, uint32_t base, uint32_t length, uint32_t address,
                            uint8_t size, uint32_t* value) {
    if (size != 1 || address < base || address >= base + length) {
        return false;
    }
    *value = data[address - base];
    return true;
}
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
            set_irq(timing, IRQ_PIT0 + channel, true);
        }
        trigger_dma(timing, channel);
        trigger_adc_alternate(timing, (uint8_t)(4u + channel));
    } else {
        pit->current -= (uint32_t)ticks;
    }
    return expirations;
}

static void advance_pit(K22Timing* timing, uint32_t cycles) {
    if (!has(timing, K22_PERIPHERAL_PIT) || (timing->sim_scgc6 & (1u << 23u)) == 0 ||
        (timing->pit_mcr & 2u) != 0 || ((timing->pit_mcr & 1u) != 0u && timing->debug_halted)) {
        return;
    }
    const uint64_t ticks =
        clock_ticks(&timing->pit_remainder, cycles, timing->bus_clock_hz, timing->core_clock_hz);
    uint64_t source = ticks;
    for (uint8_t channel = 0; channel < 4; channel++) {
        if ((timing->pit[channel].control & 4u) == 0) {
            source = ticks;
        }
        source = advance_pit_channel(timing, channel, source);
    }
}

static bool pit_read(const K22Timing* timing, uint32_t address, uint8_t size, uint32_t* value) {
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

static bool pit_write(K22Timing* timing, uint32_t address, uint8_t size, uint32_t value) {
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
        set_irq(timing, IRQ_PIT0 + channel, pit->flag && (pit->control & 2u) != 0u);
        return true;
    }
    case 12:
        if ((value & 1u) != 0) {
            timing->pit[channel].flag = false;
            set_irq(timing, IRQ_PIT0 + channel, false);
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
    return has(timing, K22_PERIPHERAL_LPTMR0) && (timing->sim_scgc5 & 1u) != 0 &&
           (timing->lptmr_csr & 1u) != 0;
}

static bool lptmr_selected_active(const K22Timing* timing) {
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
                set_irq(timing, IRQ_LPTMR, true);
            trigger_adc_alternate(timing, 14u);
        }
        timing->lptmr_counter = (uint16_t)(total % period);
        return;
    }
    const uint32_t distance = ((compare - timing->lptmr_counter) & 0xffffu) + 1u;
    if (ticks >= distance) {
        timing->lptmr_csr |= 0x80u;
        if ((timing->lptmr_csr & 0x40u) != 0)
            set_irq(timing, IRQ_LPTMR, true);
        trigger_adc_alternate(timing, 14u);
    }
    timing->lptmr_counter = (uint16_t)((uint64_t)timing->lptmr_counter + ticks);
}

static void sample_lptmr_filter(K22Timing* timing, uint32_t cycles) {
    const uint8_t prescale = (uint8_t)((timing->lptmr_psr >> 3u) & 15u);
    if (prescale == 0u)
        return;
    const uint64_t samples = clock_ticks(&timing->lptmr_filter_remainder, cycles,
                                         lptmr_clock(timing), timing->core_clock_hz);
    const bool active = lptmr_selected_active(timing);
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

static void advance_lptmr(K22Timing* timing, uint32_t cycles) {
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
    const uint64_t ticks =
        clock_ticks(&timing->lptmr_remainder, cycles, source_hz, timing->core_clock_hz);
    increment_lptmr(timing, ticks);
}

bool k22_timing_set_lptmr_input(K22Timing* timing, uint8_t input, bool high) {
    if (timing == NULL || timing->profile == NULL || input >= 3u ||
        !has(timing, K22_PERIPHERAL_LPTMR0))
        return false;
    timing->lptmr_input[input] = high;
    if (!lptmr_running(timing) || (timing->lptmr_csr & 2u) == 0 || (timing->lptmr_psr & 4u) == 0 ||
        ((timing->lptmr_csr >> 4u) & 3u) != input)
        return true;
    const bool active = lptmr_selected_active(timing);
    if (active != timing->lptmr_observed_active) {
        timing->lptmr_observed_active = active;
        if (active)
            increment_lptmr(timing, 1u);
    }
    return true;
}

static uint32_t rtc_access_reset(const K22Timing* timing) {
    return timing->profile->id == K22_PROFILE_MK22FN1M012 ||
                   timing->profile->id == K22_PROFILE_MK22FX51212
               ? 0xffffu
               : 0xffu;
}

static void update_rtc_irq(const K22Timing* timing) {
    const uint32_t enabled_flags = timing->rtc_ier & timing->rtc_sr & 7u;
    set_irq(timing, IRQ_RTC, enabled_flags != 0u);
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
    trigger_adc_alternate(timing, 13u);
    set_irq(timing, IRQ_RTC_SECONDS, (timing->rtc_ier & 0x10u) != 0u);
    set_irq(timing, IRQ_RTC_SECONDS, false);
    if (timing->rtc_tsr == timing->rtc_tar) {
        timing->rtc_sr |= 4u;
        trigger_adc_alternate(timing, 12u);
    }
    update_rtc_irq(timing);
}

static void advance_rtc(K22Timing* timing, uint32_t cycles) {
    if (!has(timing, K22_PERIPHERAL_RTC) || (timing->rtc_cr & 0x100u) == 0u ||
        (timing->rtc_sr & 0x10u) == 0 || (timing->rtc_sr & 3u) != 0) {
        return;
    }
    const uint64_t ticks = clock_ticks(&timing->rtc_remainder, cycles, timing->rtc_oscillator_hz,
                                       timing->core_clock_hz);
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

static bool pdb_auxiliary_offset(uint32_t offset) {
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

static void advance_pdb(K22Timing* timing, uint32_t cycles) {
    if (!has(timing, K22_PERIPHERAL_PDB0) || (timing->sim_scgc6 & (1u << 22u)) == 0 ||
        (timing->pdb_sc & 1u) == 0) {
        return;
    }
    const uint32_t source_hz = timing->bus_clock_hz / pdb_divider(timing->pdb_sc);
    const uint64_t ticks =
        clock_ticks(&timing->pdb_remainder, cycles, source_hz, timing->core_clock_hz);
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
                trigger(timing, K22_TIMING_TRIGGER_PDB_ADC, channel, pretrigger);
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
            trigger(timing, K22_TIMING_TRIGGER_PDB_DAC, instance, 0);
        }
    }
    if (delayed) {
        timing->pdb_sc |= 1u << 6u;
        if ((timing->pdb_sc & (1u << 5u)) != 0) {
            set_irq(timing, IRQ_PDB, true);
        }
    }
    if ((timing->pdb_sc & 2u) == 0 && total >= period) {
        timing->pdb_sc &= ~1u;
    }
}

static bool ftm_location(const K22Timing* timing, uint32_t address, uint8_t* index,
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
static uint8_t ftm_irq(uint8_t index) { return index == 3u ? IRQ_FTM3 : IRQ_FTM0 + index; }

static uint8_t ftm_dma_source(uint8_t module, uint8_t channel) {
    static const uint8_t bases[4] = {20u, 28u, 30u, 32u};
    return bases[module] + channel;
}

static uint8_t ftm_trigger_bit(uint8_t channel) {
    static const uint8_t bits[6] = {4u, 5u, 0u, 1u, 2u, 3u};
    return channel < 6u ? bits[channel] : UINT8_MAX;
}

static uint8_t ftm_channel_count(uint8_t index) { return index == 0u || index == 3u ? 8u : 2u; }

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
        channel >= ftm_channel_count(instance))
        return false;
    const K22PeripheralId peripheral = (K22PeripheralId)(K22_PERIPHERAL_FTM0 + instance);
    if (!has(timing, peripheral))
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
    if (!has(timing, peripheral))
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
    if (!has(timing, peripheral))
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

static uint8_t ftm_fault_mode(const K22FtmState* ftm) {
    return (uint8_t)((ftm->registers[0] >> 5u) & 3u);
}

static bool ftm_fault_channel_enabled(const K22FtmState* ftm, uint8_t channel) {
    const uint8_t mode = ftm_fault_mode(ftm);
    const uint8_t shift = (uint8_t)((channel / 2u) * 8u + 6u);
    return mode != 0u && (ftm->registers[4] & (1u << shift)) != 0u &&
           (mode != 1u || (channel & 1u) == 0u);
}

bool k22_timing_get_ftm_output(const K22Timing* timing, uint8_t instance, uint8_t channel,
                               bool* high) {
    if (timing == NULL || high == NULL || timing->profile == NULL || instance >= 4u ||
        channel >= ftm_channel_count(instance))
        return false;
    const K22PeripheralId peripheral = (K22PeripheralId)(K22_PERIPHERAL_FTM0 + instance);
    if (!has(timing, peripheral))
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

static void update_ftm_irq(const K22Timing* timing, uint8_t index) {
    const K22FtmState* ftm = &timing->ftm[index];
    bool asserted = (ftm->sc & 0xc0u) == 0xc0u;
    const uint8_t channels = ftm_channel_count(index);
    for (uint8_t channel = 0u; channel < channels; channel++)
        asserted = asserted || (ftm->channel_sc[channel] & 0xc0u) == 0xc0u;
    asserted = asserted || ((ftm->registers[0] & 0x80u) != 0u && (ftm->registers[8] & 0x80u) != 0u);
    set_irq(timing, ftm_irq(index), asserted);
}

static void ftm_trigger(K22Timing* timing, uint8_t index) {
    K22FtmState* ftm = &timing->ftm[index];
    ftm->registers[6] |= 0x80u;
    ftm->trigger_flag_read = false;
    trigger_adc_alternate(timing, (uint8_t)(8u + index));
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

static uint8_t ftm_active_fault_mask(const K22FtmState* ftm) {
    if (ftm_fault_mode(ftm) == 0u)
        return 0u;
    uint8_t active = 0u;
    const uint8_t enabled = (uint8_t)ftm->registers[10] & 0x0fu;
    for (uint8_t input = 0u; input < 4u; input++) {
        if ((enabled & (1u << input)) != 0u && ftm->fault_filtered_input[input])
            active |= (uint8_t)(1u << input);
    }
    return active;
}

static void ftm_update_fault_status(K22FtmState* ftm) {
    const uint32_t flags = ftm->registers[8] & 0x0fu;
    ftm->registers[8] &= 0x4fu;
    if (ftm_active_fault_mask(ftm) != 0u)
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
    ftm_update_fault_status(ftm);
    update_ftm_irq(timing, index);
}

static bool ftm_fault_processing_active(const K22FtmState* ftm) {
    return (ftm_fault_mode(ftm) != 0u && (ftm->registers[10] & 0x0fu) != 0u) ||
           ftm->fault_output_active;
}

static void ftm_advance_fault_inputs(K22Timing* timing, uint8_t index, uint32_t cycles) {
    K22FtmState* ftm = &timing->ftm[index];
    if (!ftm_gate(timing, index))
        return;
    const uint64_t ticks =
        clock_ticks(&ftm->fault_remainder, cycles, timing->bus_clock_hz, timing->core_clock_hz);
    const uint8_t enabled = ftm_fault_mode(ftm) == 0u ? 0u : (uint8_t)ftm->registers[10] & 0x0fu;
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
    ftm_update_fault_status(ftm);
    if (ftm->fault_output_active && ftm_fault_mode(ftm) == 3u && ftm_active_fault_mask(ftm) == 0u)
        ftm->fault_release_pending = true;
    update_ftm_irq(timing, index);
}

static void ftm_fault_cycle_boundary(K22Timing* timing, uint8_t index, bool new_cycle) {
    K22FtmState* ftm = &timing->ftm[index];
    if (!new_cycle || !ftm->fault_release_pending)
        return;
    if (ftm_fault_mode(ftm) == 3u)
        ftm->registers[8] &= ~0x0fu;
    ftm->fault_output_active = false;
    ftm->fault_release_pending = false;
    ftm->fault_flags_read_mask = 0u;
    ftm->fault_aggregate_read = false;
    ftm_update_fault_status(ftm);
    update_ftm_irq(timing, index);
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
        request_dma(timing, ftm_dma_source(index, channel));
    const uint8_t trigger_bit = ftm_trigger_bit(channel);
    if (trigger_bit != UINT8_MAX && (ftm->registers[6] & (1u << trigger_bit)) != 0u)
        ftm_trigger(timing, index);
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

static bool ftm_output_compare_mode(const K22FtmState* ftm, uint8_t channel) {
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
    if (count == 0u || !ftm_output_compare_mode(ftm, channel))
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
        if (ftm->channel_value_pending[channel] && ftm_output_compare_mode(ftm, channel) &&
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
            ftm_trigger(timing, index);
        update_ftm_irq(timing, index);
        return;
    }
    const uint32_t span = last - first;
    const uint32_t period = span * 2u;
    const uint8_t channels = ftm_channel_count(index);
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
        ftm_trigger(timing, index);
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
    update_ftm_irq(timing, index);
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
    const uint64_t ticks = clock_ticks(&ftm->remainder, cycles, source_hz, timing->core_clock_hz);
    if (ticks == 0) {
        return;
    }
    if ((ftm->sc & (1u << 5u)) != 0u) {
        advance_ftm_up_down(timing, index, ticks);
        return;
    }
    const uint8_t channels = ftm_channel_count(index);
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
        const bool output_compare = ftm_output_compare_mode(ftm, channel);
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
        ftm_trigger(timing, index);
    if (overflows != 0u)
        ftm_apply_legacy_boundary_values(ftm, channels);
    for (uint8_t channel = 0u; channel < channels; channel++) {
        ftm_output_compare_match(ftm, channel, matches[channel]);
        ftm_edge_aligned_pwm_advance(ftm, channel, matches[channel], overflows);
        ftm_combine_pwm_advance(ftm, channel);
    }
    ftm_loading_point(ftm, overflows != 0u, overflows != 0u, match_mask);
    update_ftm_irq(timing, index);
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

static bool ftm_input_capture_mode(const K22FtmState* ftm, uint8_t channel) {
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
            ftm_trigger(timing, index);
    }
    update_ftm_irq(timing, index);
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
    if (previous == current || !selected || !ftm_input_capture_mode(ftm, channel))
        return;
    ftm->channel_value[channel] = ftm->counter;
    ftm_channel_event(timing, index, channel);
    if ((ftm->channel_sc[channel] & 2u) != 0u) {
        ftm->counter = ftm->initial;
        ftm->counting_down = false;
        ftm->overflow_count = 0u;
        ftm->remainder = 0u;
        if ((ftm->registers[6] & (1u << 6u)) != 0u)
            ftm_trigger(timing, index);
    }
    update_ftm_irq(timing, index);
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

static void ftm_apply_software_sync(K22FtmState* ftm) {
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
    const uint8_t channels = ftm_channel_count(index);
    if (!ftm_gate(timing, index))
        return;
    const uint8_t divider = (uint8_t)(ftm->registers[5] >> 6u);
    const uint8_t shift = divider < 2u ? 0u : divider == 2u ? 2u : 4u;
    const uint64_t ticks = clock_ticks(&ftm->deadtime_remainder, cycles,
                                       timing->bus_clock_hz >> shift, timing->core_clock_hz);
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

static void advance_ftm(K22Timing* timing, uint8_t index, uint32_t cycles) {
    K22FtmState* ftm = &timing->ftm[index];
    ftm_apply_system_clock_updates(ftm);
    ftm_process_hardware_triggers(timing, index);
    const uint8_t channels = ftm_channel_count(index);
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
static uint32_t wdog_timeout(const K22Timing* timing) {
    return ((uint32_t)timing->wdog[2] << 16u) | timing->wdog[3];
}

static void signal_reset(K22Timing* timing, uint8_t srs0, uint8_t srs1) {
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
    set_irq(timing, IRQ_WDOG_EWM, watchdog || external);
}

static bool wdog_exception(K22Timing* timing) {
    timing->wdog_counter = 0u;
    if ((timing->wdog[0] & 4u) == 0u) {
        signal_reset(timing, 0x20u, 0u);
        return true;
    }
    if (timing->wdog_reset_pending) {
        signal_reset(timing, 0x20u, 0u);
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
        signal_reset(timing, 0x20u, 0u);
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
        !has(timing, K22_PERIPHERAL_WDOG) || !wdog_running(timing))
        return;
    const uint32_t timeout = wdog_effective_timeout(timing);
    if (timeout == 0u || timing->wdog_counter >= timeout ||
        ticks >= timeout - timing->wdog_counter) {
        (void)wdog_exception(timing);
    } else {
        timing->wdog_counter += ticks;
    }
}

static void advance_wdog(K22Timing* timing, uint32_t cycles) {
    if (!has(timing, K22_PERIPHERAL_WDOG))
        return;
    const bool update_open = timing->wdog_update_open;
    const uint64_t bus_ticks = clock_ticks(&timing->wdog_bus_remainder, cycles,
                                           timing->bus_clock_hz, timing->core_clock_hz);
    if (wdog_advance_bus(timing, bus_ticks) || (update_open && !timing->wdog_update_open) ||
        !wdog_running(timing))
        return;
    const bool test = (timing->wdog[0] & 0x4400u) == 0x0400u;
    const uint32_t source_hz =
        test || (timing->wdog[0] & (1u << 13u)) != 0u ? timing->bus_clock_hz : timing->lpo_hz;
    const uint32_t divider = ((timing->wdog[11] & 0x700u) >> 8u) + 1u;
    const uint64_t ticks =
        clock_ticks(&timing->wdog_remainder, cycles, source_hz / divider, timing->core_clock_hz);
    k22_timing_watchdog_advance(timing, ticks > UINT32_MAX ? UINT32_MAX : (uint32_t)ticks);
}

static void assert_ewm_output(K22Timing* timing) {
    timing->ewm_output = true;
    update_watchdog_irq(timing);
}

static void advance_ewm(K22Timing* timing, uint32_t cycles) {
    if (!has(timing, K22_PERIPHERAL_EWM) || (timing->ewm_ctrl & 1u) == 0u || timing->ewm_output ||
        timing->cpu_sleeping)
        return;
    const uint32_t source_hz = timing->lpo_hz / ((uint32_t)timing->ewm_prescaler + 1u);
    const uint64_t ticks =
        clock_ticks(&timing->ewm_remainder, cycles, source_hz, timing->core_clock_hz);
    const uint32_t increment = ticks > UINT32_MAX ? UINT32_MAX : (uint32_t)ticks;
    timing->ewm_counter = increment >= UINT32_MAX - timing->ewm_counter
                              ? UINT32_MAX
                              : timing->ewm_counter + increment;
    if (timing->ewm_cmph != 0xffu && timing->ewm_counter >= timing->ewm_cmph)
        assert_ewm_output(timing);
}

static bool read_wdog(const K22Timing* timing, uint32_t address, uint8_t size, uint32_t* value) {
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

static bool write_wdog(K22Timing* timing, uint32_t address, uint8_t size, uint32_t value) {
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

static bool read_ewm(const K22Timing* timing, uint32_t address, uint8_t size, uint32_t* value) {
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

static bool write_ewm(K22Timing* timing, uint32_t address, uint8_t size, uint32_t value) {
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

static bool ftm_read(K22Timing* timing, uint8_t index, uint32_t offset, uint8_t size,
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
        if (channel >= ftm_channel_count(index))
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
        const uint8_t channels = ftm_channel_count(index);
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
static uint32_t ftm_register_mask(uint8_t index, uint8_t register_index) {
    static const uint32_t masks[18] = {
        0x000000ffu, 0x000000ffu, 0x000000ffu, 0x000000ffu, 0x7f7f7f7fu, 0x000000ffu,
        0x000000ffu, 0x000000ffu, 0x000000efu, 0x0000ffffu, 0x00000fffu, 0x000000ffu,
        0x000006dfu, 0x0000000fu, 0x001f1fb5u, 0x0000000fu, 0x0000ffffu, 0x000002ffu,
    };
    uint32_t mask = masks[register_index];
    if (ftm_channel_count(index) == 2u) {
        if (register_index == 2u || register_index == 3u || register_index == 7u)
            mask &= 3u;
        else if (register_index == 4u)
            mask &= 0x7fu;
        else if (register_index == 16u)
            mask &= 0x0303u;
        else if (register_index == 17u)
            mask &= 0x0203u;
    }
    return mask;
}

static uint32_t ftm_write_protected_mask(uint8_t register_index) {
    static const uint32_t masks[18] = {
        0x00000071u, 0u,          0u,          0u, 0x57575757u, 0x000000ffu, 0u, 0x000000ffu, 0u,
        0u,          0x000000ffu, 0x00000001u, 0u, 0x0000000fu, 0u,          0u, 0u,          0u,
    };
    return masks[register_index];
}

static bool ftm_write(K22Timing* timing, uint8_t index, uint32_t offset, uint8_t size,
                      uint32_t value) {
    if (size != 4 || (offset & 3u) != 0) {
        return false;
    }
    K22FtmState* ftm = &timing->ftm[index];
    if (offset == 0) {
        const bool clock_stopped = (ftm->sc & 0x18u) == 0u;
        uint32_t flag = ftm->sc & 0x80u;
        if ((value & 0x80u) == 0u && ftm->overflow_flag_read)
            flag = 0u;
        ftm->sc = flag | (value & 0x7fu);
        ftm->overflow_flag_read = false;
        if (clock_stopped && (ftm->sc & 0x18u) != 0u && ftm->counter == ftm->initial &&
            (ftm->registers[6] & (1u << 6u)) != 0u)
            ftm_trigger(timing, index);
        update_ftm_irq(timing, index);
    } else if (offset == 4) {
        ftm->counter = ftm->initial;
        ftm->counting_down = false;
        ftm->overflow_count = 0u;
        const uint8_t channels = ftm_channel_count(index);
        for (uint8_t channel = 0u; channel < channels; channel++) {
            if (!ftm_output_compare_mode(ftm, channel))
                ftm->channel_output[channel] = (ftm->registers[2] & (1u << channel)) != 0u;
        }
        if ((ftm->registers[6] & (1u << 6u)) != 0u)
            ftm_trigger(timing, index);
    } else if (offset == 8) {
        ftm->modulo_buffer = (uint16_t)value;
        if ((ftm->sc & 0x18u) == 0u) {
            ftm->modulo = ftm->modulo_buffer;
            ftm->modulo_pending = false;
        } else {
            ftm->modulo_pending = true;
        }
    } else if (offset >= 0x0cu && offset < 0x4cu) {
        const uint8_t channel = (uint8_t)((offset - 0x0cu) / 8u);
        if (channel >= ftm_channel_count(index))
            return false;
        if (((offset - 0x0cu) & 4u) == 0) {
            uint32_t flag = ftm->channel_sc[channel] & 0x80u;
            if ((value & 0x80u) == 0u && ftm->channel_flag_read[channel])
                flag = 0u;
            ftm->channel_sc[channel] = flag | (value & 0x7fu);
            ftm->channel_flag_read[channel] = false;
            update_ftm_irq(timing, index);
        } else if (!ftm_input_capture_mode(ftm, channel)) {
            ftm->channel_value_buffer[channel] = (uint16_t)value;
            if ((ftm->sc & 0x18u) == 0u) {
                ftm->channel_value[channel] = ftm->channel_value_buffer[channel];
                ftm->channel_value_pending[channel] = false;
            } else {
                ftm->channel_value_pending[channel] = true;
            }
        }
    } else if (offset == 0x4cu) {
        ftm->initial_buffer = (uint16_t)value;
        if ((ftm->sc & 0x18u) == 0u) {
            ftm->initial = ftm->initial_buffer;
            ftm->initial_pending = false;
        } else {
            ftm->initial_pending = true;
        }
    } else if (offset == 0x50u) {
        const uint8_t channels = ftm_channel_count(index);
        for (uint8_t channel = 0; channel < channels; channel++) {
            if ((value & (1u << channel)) == 0) {
                ftm->channel_sc[channel] &= ~0x80u;
                ftm->channel_flag_read[channel] = false;
            }
        }
        update_ftm_irq(timing, index);
    } else if (offset >= 0x54u && offset <= 0x98u) {
        const uint8_t register_index = (uint8_t)((offset - 0x54u) / 4u);
        value &= ftm_register_mask(index, register_index);
        if (offset == 0x54u) {
            const uint32_t current = ftm->registers[register_index];
            const uint32_t protected_mask = ftm_write_protected_mask(register_index);
            uint32_t next = (current & protected_mask) | (value & ~protected_mask & ~6u);
            if ((current & 4u) != 0u)
                next = (next & ~protected_mask) | (value & protected_mask);
            if ((current & 4u) != 0u || ((value & 4u) != 0u && ftm->write_protection_read))
                next |= 4u;
            if ((current & 4u) == 0u && (value & 4u) != 0u && ftm->write_protection_read) {
                ftm->registers[8] &= ~0x40u;
                ftm->write_protection_read = false;
            }
            ftm->registers[register_index] = next;
            if ((value & 2u) != 0u) {
                const uint8_t channels = ftm_channel_count(index);
                for (uint8_t channel = 0u; channel < channels; channel++)
                    ftm->channel_output[channel] = (ftm->registers[2] & (1u << channel)) != 0u;
            }
        } else if (offset == 0x58u) {
            ftm->registers[1] = value;
            if ((value & 0x80u) != 0u) {
                ftm_apply_software_sync(ftm);
            } else {
                ftm->software_sync_pending = false;
            }
        } else if (offset == 0x60u) {
            ftm->outmask_buffer = value;
            ftm->outmask_pending = true;
        } else if (offset == 0x74u) {
            uint8_t flags = (uint8_t)ftm->registers[8] & 0x0fu;
            const uint8_t active = ftm_active_fault_mask(ftm);
            if (ftm->fault_aggregate_read && (value & 0x80u) == 0u && active == 0u) {
                flags = 0u;
            } else {
                for (uint8_t input = 0u; input < 4u; input++) {
                    const uint8_t bit = (uint8_t)(1u << input);
                    if ((ftm->fault_flags_read_mask & bit) != 0u && (value & bit) == 0u &&
                        (active & bit) == 0u)
                        flags &= (uint8_t)~bit;
                }
            }
            ftm->registers[8] = (ftm->registers[8] & 0x40u) | flags;
            if ((value & 0x40u) != 0u) {
                ftm->registers[8] |= 0x40u;
                ftm->registers[0] &= ~4u;
            }
            ftm->fault_flags_read_mask = 0u;
            ftm->fault_aggregate_read = false;
            if (ftm->fault_output_active) {
                if (ftm_fault_mode(ftm) == 3u)
                    ftm->fault_release_pending = active == 0u;
                else
                    ftm->fault_release_pending = flags == 0u;
            }
            ftm_update_fault_status(ftm);
            ftm->write_protection_read = false;
            update_ftm_irq(timing, index);
        } else if (offset == 0x80u) {
            const uint32_t current = ftm->registers[register_index];
            if ((ftm->registers[0] & 4u) == 0u)
                value = (value & ~1u) | (current & 1u);
            ftm->registers[register_index] = (value & ~6u) | (current & 6u);
        } else if (offset == 0x6cu) {
            const uint32_t mask = index == 0u || index == 3u ? 0xffu : 0xf0u;
            uint32_t next = (ftm->registers[register_index] & 0x80u) | (value & mask & 0x7fu);
            if ((value & 0x80u) == 0u && ftm->trigger_flag_read)
                next &= ~0x80u;
            ftm->registers[register_index] = next;
            ftm->trigger_flag_read = false;
        } else if (offset == 0x90u) {
            ftm->invctrl_buffer = value;
            ftm->invctrl_pending = true;
        } else if (offset == 0x94u) {
            ftm->swoctrl_buffer = value;
            ftm->swoctrl_pending = true;
        } else if (offset == 0x98u) {
            ftm->registers[17] = value;
        } else {
            const uint32_t protected_mask = ftm_write_protected_mask(register_index);
            if ((ftm->registers[0] & 4u) == 0u)
                value =
                    (value & ~protected_mask) | (ftm->registers[register_index] & protected_mask);
            ftm->registers[register_index] = value;
        }
        if (offset == 0x54u || offset == 0x7cu || offset == 0x88u) {
            if (ftm_fault_mode(ftm) == 0u) {
                ftm->fault_output_active = false;
                ftm->fault_release_pending = false;
            }
            const uint8_t enabled =
                ftm_fault_mode(ftm) == 0u ? 0u : (uint8_t)ftm->registers[10] & 0x0fu;
            for (uint8_t input = 0u; input < 4u; input++) {
                if ((enabled & (1u << input)) == 0u) {
                    ftm->fault_filtered_input[input] = false;
                    ftm->fault_input_age[input] = 0u;
                }
            }
            ftm_update_fault_status(ftm);
            update_ftm_irq(timing, index);
        }
    } else
        return false;
    return true;
}

static bool rtc_access_allowed(uint32_t access, uint32_t offset) {
    return offset > 0x1cu || (access & (1u << (offset >> 2u))) != 0u;
}

static void rtc_software_reset(K22Timing* timing) {
    timing->rtc_tsr = 0u;
    timing->rtc_tpr = 0u;
    timing->rtc_tar = 0u;
    timing->rtc_tcr = 0u;
    timing->rtc_cr = 1u;
    timing->rtc_sr = 1u;
    timing->rtc_lr = rtc_access_reset(timing);
    timing->rtc_ier = 7u;
    timing->rtc_remainder = 0u;
    timing->rtc_subsecond_ticks = 0u;
    update_rtc_irq(timing);
    set_irq(timing, IRQ_RTC_SECONDS, false);
}

static bool rtc_read(K22Timing* timing, uint32_t offset, uint32_t* value) {
    if (offset != 0x800u && offset != 0x804u && !rtc_access_allowed(timing->rtc_rar, offset)) {
        *value = 0u;
        return true;
    }
    switch (offset) {
    case 0:
        *value = (timing->rtc_sr & 3u) == 0u ? timing->rtc_tsr : 0u;
        return true;
    case 4:
        *value = (timing->rtc_sr & 3u) == 0u ? timing->rtc_tpr : 0u;
        return true;
    case 8:
        *value = timing->rtc_tar;
        return true;
    case 12:
        *value = timing->rtc_tcr;
        return true;
    case 16:
        *value = timing->rtc_cr;
        return true;
    case 20:
        *value = timing->rtc_sr;
        return true;
    case 24:
        *value = timing->rtc_lr;
        return true;
    case 28:
        *value = timing->rtc_ier;
        return true;
    case 0x800:
        *value = timing->rtc_war;
        return true;
    case 0x804:
        *value = timing->rtc_rar;
        return true;
    default:
        return false;
    }
}

static bool rtc_write(K22Timing* timing, uint32_t offset, uint32_t value) {
    if (offset == 0x800u) {
        timing->rtc_war &= value & rtc_access_reset(timing);
        return true;
    }
    if (offset == 0x804u) {
        timing->rtc_rar &= value & rtc_access_reset(timing);
        return true;
    }
    if (!rtc_access_allowed(timing->rtc_war, offset))
        return true;
    switch (offset) {
    case 0:
        if ((timing->rtc_sr & 0x10u) == 0u) {
            timing->rtc_tsr = value;
            timing->rtc_sr &= ~3u;
            update_rtc_irq(timing);
        }
        return true;
    case 4:
        if ((timing->rtc_sr & 0x10u) == 0u) {
            timing->rtc_tpr = (uint16_t)value & 0x7fffu;
            timing->rtc_subsecond_ticks = timing->rtc_tpr;
        }
        return true;
    case 8:
        timing->rtc_tar = value;
        timing->rtc_sr &= ~4u;
        update_rtc_irq(timing);
        return true;
    case 12:
        if ((timing->rtc_lr & 8u) != 0u)
            timing->rtc_tcr = (timing->rtc_tcr & 0xffff0000u) | (value & 0xffffu);
        return true;
    case 16:
        if ((timing->rtc_lr & 0x10u) != 0u) {
            if ((value & 1u) != 0u)
                rtc_software_reset(timing);
            else
                timing->rtc_cr = value & 0x3f1eu;
        }
        return true;
    case 20:
        if ((timing->rtc_lr & 0x20u) != 0u ||
            ((timing->rtc_cr & 8u) != 0u && ((timing->rtc_sr & 0x13u) != 0x10u))) {
            timing->rtc_sr = (timing->rtc_sr & 7u) | (value & 0x10u);
            update_rtc_irq(timing);
        }
        return true;
    case 24:
        if ((timing->rtc_lr & 0x40u) != 0u)
            timing->rtc_lr &= value | ~UINT32_C(0x78);
        return true;
    case 28:
        timing->rtc_ier = value & 0x17u;
        update_rtc_irq(timing);
        return true;
    default:
        return false;
    }
}

static bool read_timed_register(K22Timing* timing, uint32_t address, uint8_t size,
                                uint32_t* value) {
    if (address >= PIT_BASE && address < PIT_BASE + 0x140u && has(timing, K22_PERIPHERAL_PIT))
        return pit_read(timing, address, size, value);
    if (address >= LPTMR_BASE && address < LPTMR_BASE + 0x10u && size == 4 &&
        has(timing, K22_PERIPHERAL_LPTMR0)) {
        switch (address - LPTMR_BASE) {
        case 0:
            *value = timing->lptmr_csr;
            return true;
        case 4:
            *value = timing->lptmr_psr;
            return true;
        case 8:
            *value = timing->lptmr_cmr;
            return true;
        case 12:
            *value = timing->lptmr_latched_counter;
            return true;
        default:
            return false;
        }
    }
    if (address >= RTC_BASE && address <= RTC_BASE + 0x804u && size == 4 &&
        has(timing, K22_PERIPHERAL_RTC))
        return rtc_read(timing, address - RTC_BASE, value);
    if (address >= PDB_BASE && address < PDB_BASE + 0x1a0u && size == 4 &&
        has(timing, K22_PERIPHERAL_PDB0)) {
        const uint32_t offset = address - PDB_BASE;
        if (offset == 0)
            *value = timing->pdb_sc;
        else if (offset == 4)
            *value = timing->pdb_mod;
        else if (offset == 8)
            *value = timing->pdb_counter;
        else if (offset == 12)
            *value = timing->pdb_idly;
        else if (pdb_auxiliary_offset(offset)) {
            *value = timing->pdb_registers[offset >> 2u];
        } else
            return false;
        return true;
    }
    uint8_t index;
    uint32_t offset;
    return ftm_location(timing, address, &index, &offset) &&
           ftm_read(timing, index, offset, size, value);
}

static bool write_timed_register(K22Timing* timing, uint32_t address, uint8_t size,
                                 uint32_t value) {
    if (address >= PIT_BASE && address < PIT_BASE + 0x140u && has(timing, K22_PERIPHERAL_PIT))
        return pit_write(timing, address, size, value);
    if (address >= LPTMR_BASE && address < LPTMR_BASE + 0x10u && size == 4 &&
        has(timing, K22_PERIPHERAL_LPTMR0)) {
        switch (address - LPTMR_BASE) {
        case 0: {
            const bool was_enabled = (timing->lptmr_csr & 1u) != 0;
            if ((timing->lptmr_csr & 1u) != 0) {
                const uint32_t configuration = timing->lptmr_csr & 0x3eu;
                timing->lptmr_csr =
                    (timing->lptmr_csr & 0x80u & ~value) | configuration | (value & 0x41u);
            } else {
                timing->lptmr_csr = value & 0x7fu;
            }
            if ((timing->lptmr_csr & 1u) == 0) {
                timing->lptmr_csr &= ~0x80u;
                timing->lptmr_counter = 0;
                timing->lptmr_latched_counter = 0;
                timing->lptmr_remainder = 0u;
                timing->lptmr_filter_remainder = 0u;
                timing->lptmr_filter_ticks = 0u;
            } else if (!was_enabled) {
                timing->lptmr_observed_active = lptmr_selected_active(timing);
                timing->lptmr_filter_ticks = 0u;
            }
            set_irq(timing, IRQ_LPTMR, (timing->lptmr_csr & 0xc0u) == 0xc0u);
            return true;
        }
        case 4:
            if ((timing->lptmr_csr & 1u) == 0)
                timing->lptmr_psr = value & 0x7fu;
            return true;
        case 8:
            if ((timing->lptmr_csr & 1u) == 0 || (timing->lptmr_csr & 0x80u) != 0)
                timing->lptmr_cmr = value & 0xffffu;
            return true;
        case 12:
            timing->lptmr_latched_counter = timing->lptmr_counter;
            return true;
        default:
            return false;
        }
    }
    if (address >= RTC_BASE && address <= RTC_BASE + 0x804u && size == 4 &&
        has(timing, K22_PERIPHERAL_RTC))
        return rtc_write(timing, address - RTC_BASE, value);
    if (address >= PDB_BASE && address < PDB_BASE + 0x1a0u && size == 4 &&
        has(timing, K22_PERIPHERAL_PDB0)) {
        const uint32_t offset = address - PDB_BASE;
        if (offset == 0) {
            if ((value & (1u << 6u)) == 0)
                set_irq(timing, IRQ_PDB, false);
            timing->pdb_sc = (timing->pdb_sc & value & (1u << 6u)) | (value & ~(1u << 6u));
            if ((value & (1u << 16u)) != 0)
                timing->pdb_counter = 0;
        } else if (offset == 4)
            timing->pdb_mod = (uint16_t)value;
        else if (offset == 8)
            return true;
        else if (offset == 12)
            timing->pdb_idly = (uint16_t)value;
        else if (pdb_auxiliary_offset(offset)) {
            if (offset == 0x14u || offset == 0x3cu)
                timing->pdb_registers[offset >> 2u] &= ~value;
            else
                timing->pdb_registers[offset >> 2u] = value;
        } else
            return false;
        return true;
    }
    uint8_t index;
    uint32_t offset;
    return ftm_location(timing, address, &index, &offset) &&
           ftm_write(timing, index, offset, size, value);
}

bool k22_timing_init(K22Timing* timing, const K22Profile* profile, uint32_t external_oscillator_hz,
                     uint32_t rtc_oscillator_hz, K22TimingSignals signals) {
    if (timing == NULL || profile == NULL) {
        return false;
    }
    memset(timing, 0, sizeof(*timing));
    timing->profile = profile;
    timing->signals = signals;
    timing->external_oscillator_hz = external_oscillator_hz;
    timing->rtc_oscillator_hz = rtc_oscillator_hz == 0 ? 32768u : rtc_oscillator_hz;
    timing->slow_irc_hz = 32768u;
    timing->fast_irc_hz = 4000000u;
    timing->lpo_hz = 1000u;
    k22_timing_reset(timing, 0x82u, 0);
    return true;
}

void k22_timing_reset(K22Timing* timing, uint8_t srs0, uint8_t srs1) {
    if (timing == NULL || timing->profile == NULL) {
        return;
    }
    const K22Profile* profile = timing->profile;
    const K22TimingSignals signals = timing->signals;
    const uint32_t external = timing->external_oscillator_hz;
    const uint32_t rtc = timing->rtc_oscillator_hz;
    const uint64_t elapsed = timing->elapsed_core_cycles;
    const uint64_t generation = timing->reset_generation;
    const uint8_t sticky0 = timing->rcm[8];
    const uint8_t sticky1 = timing->rcm[9];
    memset(timing, 0, sizeof(*timing));
    timing->profile = profile;
    timing->signals = signals;
    timing->external_oscillator_hz = external;
    timing->rtc_oscillator_hz = rtc;
    timing->slow_irc_hz = 32768u;
    timing->fast_irc_hz = 4000000u;
    timing->lpo_hz = 1000u;
    timing->elapsed_core_cycles = elapsed;
    timing->reset_generation = generation + 1u;
    timing->sim_sopt1 = timing->profile->id == K22_PROFILE_MK22F12810 ? 0u : 0x80000000u;
    timing->sim_sopt2 = 0x1000u;
    timing->sim_scgc4 = 0xf0100030u;
    timing->sim_scgc5 = 0x00040182u;
    timing->sim_scgc6 = 0x40000001u;
    timing->sim_scgc7 = timing->profile->id == K22_PROFILE_MK22FN1M012 ||
                                timing->profile->id == K22_PROFILE_MK22FX51212
                            ? 6u
                            : 2u;
    timing->sim_clkdiv1 =
        timing->profile->id <= K22_PROFILE_MK22FN25612 ? 0x00010000u : 0x00110000u;
    timing->mcg[0] = 4u;
    timing->mcg[1] = 0x80u;
    timing->mcg[6] = 0x10u;
    timing->mcg[8] = 2u;
    timing->mcg[13] = 0x80u;
    if (timing->profile->id == K22_PROFILE_MK22FN1M012 ||
        timing->profile->id == K22_PROFILE_MK22FX51212) {
        timing->llwu[10] = 0x02u;
    }
    timing->pmc[0] = 0x10u;
    timing->pmc[2] = 4u;
    timing->smc[2] = 3u;
    timing->smc[3] = 1u;
    timing->smc_run_status = 1u;
    timing->rcm[0] = srs0;
    timing->rcm[1] = srs1;
    if (srs0 == 0x82u && srs1 == 0) {
        timing->rcm[8] = srs0;
        timing->rcm[9] = srs1;
    } else {
        timing->rcm[8] = sticky0 | srs0;
        timing->rcm[9] = sticky1 | srs1;
    }
    timing->pit_mcr = timing->profile->id == K22_PROFILE_MK22FN1M012 ||
                              timing->profile->id == K22_PROFILE_MK22FX51212
                          ? 2u
                          : 6u;
    timing->rtc_sr = 1u;
    timing->rtc_lr = rtc_access_reset(timing);
    timing->rtc_ier = 7u;
    timing->rtc_war = rtc_access_reset(timing);
    timing->rtc_rar = rtc_access_reset(timing);
    timing->pdb_mod = 0xffffu;
    timing->pdb_idly = 0xffffu;
    for (uint8_t index = 0; index < 4; index++) {
        timing->ftm[index].modulo = 0;
        timing->ftm[index].registers[0] = 4u;
        timing->ftm[index].quadrature_capable = index == 1u || index == 2u;
    }
    timing->wdog[0] = 0x01d3u;
    timing->wdog[1] = 1u;
    timing->wdog[2] = 0x004cu;
    timing->wdog[3] = 0x4b4cu;
    timing->wdog[5] = 0x10u;
    timing->wdog[6] = 0xb480u;
    timing->wdog[7] = 0xd928u;
    timing->wdog[11] = 0x0400u;
    memcpy(timing->wdog_pending, timing->wdog, sizeof(timing->wdog));
    timing->wdog_initial_unlock_required = true;
    timing->wdog_update_deadline = 256u;
    timing->ewm_cmph = 0xffu;
    timing->ewm_output = true;
    update_clocks(timing);
    update_rtc_irq(timing);
    set_irq(timing, IRQ_RTC_SECONDS, false);
    set_irq(timing, IRQ_WDOG_EWM, false);
}

void k22_timing_warm_reset(K22Timing* timing, uint8_t srs0, uint8_t srs1) {
    if (timing == NULL || timing->profile == NULL)
        return;
    const uint32_t tsr = timing->rtc_tsr;
    const uint16_t tpr = timing->rtc_tpr;
    const uint32_t tar = timing->rtc_tar;
    const uint32_t tcr = timing->rtc_tcr;
    const uint32_t cr = timing->rtc_cr;
    const uint32_t sr = timing->rtc_sr;
    const uint32_t lr = timing->rtc_lr;
    const uint32_t ier = timing->rtc_ier;
    const uint64_t remainder = timing->rtc_remainder;
    const uint32_t subsecond_ticks = timing->rtc_subsecond_ticks;
    const uint32_t lptmr_csr = timing->lptmr_csr;
    const uint32_t lptmr_psr = timing->lptmr_psr;
    const uint32_t lptmr_cmr = timing->lptmr_cmr;
    const uint16_t lptmr_counter = timing->lptmr_counter;
    const uint16_t lptmr_latched_counter = timing->lptmr_latched_counter;
    const uint64_t lptmr_remainder = timing->lptmr_remainder;
    const uint64_t lptmr_filter_remainder = timing->lptmr_filter_remainder;
    const uint32_t lptmr_filter_ticks = timing->lptmr_filter_ticks;
    const bool lptmr_input[3] = {timing->lptmr_input[0], timing->lptmr_input[1],
                                 timing->lptmr_input[2]};
    const bool lptmr_observed_active = timing->lptmr_observed_active;
    const uint16_t wdog_reset_count = timing->wdog[10];
    k22_timing_reset(timing, srs0, srs1);
    timing->rtc_tsr = tsr;
    timing->rtc_tpr = tpr;
    timing->rtc_tar = tar;
    timing->rtc_tcr = tcr;
    timing->rtc_cr = cr;
    timing->rtc_sr = sr;
    timing->rtc_lr = lr;
    timing->rtc_ier = ier;
    timing->rtc_remainder = remainder;
    timing->rtc_subsecond_ticks = subsecond_ticks;
    timing->wdog[10] = (srs0 & 0x20u) != 0u && wdog_reset_count != UINT16_MAX
                           ? (uint16_t)(wdog_reset_count + 1u)
                           : wdog_reset_count;
    update_rtc_irq(timing);
    if ((srs0 & 0x84u) == 0) {
        timing->lptmr_csr = lptmr_csr;
        timing->lptmr_psr = lptmr_psr;
        timing->lptmr_cmr = lptmr_cmr;
        timing->lptmr_counter = lptmr_counter;
        timing->lptmr_latched_counter = lptmr_latched_counter;
        timing->lptmr_remainder = lptmr_remainder;
        timing->lptmr_filter_remainder = lptmr_filter_remainder;
        timing->lptmr_filter_ticks = lptmr_filter_ticks;
        timing->lptmr_input[0] = lptmr_input[0];
        timing->lptmr_input[1] = lptmr_input[1];
        timing->lptmr_input[2] = lptmr_input[2];
        timing->lptmr_observed_active = lptmr_observed_active;
        set_irq(timing, IRQ_LPTMR, (timing->lptmr_csr & 0xc0u) == 0xc0u);
    }
}

bool k22_timing_read(K22Timing* timing, uint32_t address, uint8_t size, uint32_t* value) {
    if (timing == NULL || timing->profile == NULL || value == NULL) {
        return false;
    }
    if (address >= SIM_BASE && address < SIM_BASE + 0x2000u && has(timing, K22_PERIPHERAL_SIM)) {
        return read_sim(timing, address, size, value);
    }
    if (address >= MCG_BASE && address < MCG_BASE + 14u && mcg_register(address - MCG_BASE) &&
        has(timing, K22_PERIPHERAL_MCG)) {
        return read_byte_block(timing->mcg, MCG_BASE, 14u, address, size, value);
    }
    if (address == OSC_BASE && size == 1 && has(timing, K22_PERIPHERAL_OSC)) {
        *value = timing->osc_cr;
        return true;
    }
    if (address == OSC_BASE + 2u && size == 1 && has(timing, K22_PERIPHERAL_OSC)) {
        *value = timing->osc_div;
        return true;
    }
    if (contains(timing, K22_PERIPHERAL_LLWU, address, size))
        return read_byte_block(timing->llwu, LLWU_BASE, 11u, address, size, value);
    if (contains(timing, K22_PERIPHERAL_PMC, address, size))
        return read_byte_block(timing->pmc, PMC_BASE, 3u, address, size, value);
    if (contains(timing, K22_PERIPHERAL_SMC, address, size))
        return read_byte_block(timing->smc, SMC_BASE, 4u, address, size, value);
    if (contains(timing, K22_PERIPHERAL_RCM, address, size))
        return read_byte_block(timing->rcm, RCM_BASE, 10u, address, size, value);
    if (address >= WDOG_BASE && address < WDOG_BASE + 0x18u && has(timing, K22_PERIPHERAL_WDOG))
        return read_wdog(timing, address, size, value);
    if (address >= EWM_BASE && address < EWM_BASE + 6u && has(timing, K22_PERIPHERAL_EWM))
        return read_ewm(timing, address, size, value);
    return read_timed_register(timing, address, size, value);
}

static bool write_control_register(K22Timing* timing, uint32_t address, uint8_t size,
                                   uint32_t value) {
    if (address >= MCG_BASE && address < MCG_BASE + 14u && mcg_register(address - MCG_BASE) &&
        size == 1 && has(timing, K22_PERIPHERAL_MCG)) {
        if (address != MCG_BASE + 6u)
            timing->mcg[address - MCG_BASE] = (uint8_t)value;
        update_clocks(timing);
        return true;
    }
    if (address == OSC_BASE && size == 1 && has(timing, K22_PERIPHERAL_OSC)) {
        timing->osc_cr = (uint8_t)value;
        return true;
    }
    if (address == OSC_BASE + 2u && size == 1 && has(timing, K22_PERIPHERAL_OSC)) {
        timing->osc_div = (uint8_t)value;
        return true;
    }
    if (contains(timing, K22_PERIPHERAL_LLWU, address, size)) {
        const uint8_t offset = (uint8_t)(address - LLWU_BASE);
        if (offset == 5u || offset == 6u)
            timing->llwu[offset] &= (uint8_t)~value;
        else if (offset == 8u || offset == 9u)
            timing->llwu[offset] =
                (timing->llwu[offset] & 0x80u & (uint8_t)~value) | ((uint8_t)value & 0x6fu);
        else if (offset != 7u)
            timing->llwu[offset] = (uint8_t)value;
        update_llwu_irq(timing);
        return true;
    }
    if (contains(timing, K22_PERIPHERAL_PMC, address, size)) {
        const uint8_t offset = (uint8_t)(address - PMC_BASE);
        if (offset == 0u) {
            uint8_t flag = timing->pmc[0] & 0x80u;
            if (((uint8_t)value & 0x40u) != 0u)
                flag = 0u;
            uint8_t reset_enable = timing->pmc[0] & 0x10u;
            if (!timing->pmc_lvdre_written) {
                reset_enable = (uint8_t)value & 0x10u;
                timing->pmc_lvdre_written = true;
            }
            timing->pmc[0] = flag | reset_enable | ((uint8_t)value & 0x23u);
        } else if (offset == 1u) {
            uint8_t flag = timing->pmc[1] & 0x80u;
            if (((uint8_t)value & 0x40u) != 0u)
                flag = 0u;
            timing->pmc[1] = flag | ((uint8_t)value & 0x23u);
        } else {
            uint8_t status = timing->pmc[2] & 0x0cu;
            if (((uint8_t)value & 8u) != 0u)
                status &= 0xf7u;
            timing->pmc[2] = status | ((uint8_t)value & 0x11u);
        }
        update_pmc_irq(timing);
        return true;
    }
    if (contains(timing, K22_PERIPHERAL_SMC, address, size)) {
        const uint8_t offset = (uint8_t)(address - SMC_BASE);
        if (offset == 0u)
            timing->smc[0] |= (uint8_t)value & 0xaau;
        else if (offset == 1u) {
            timing->smc[1] = (uint8_t)value & 0xe7u;
            const uint8_t mode = (uint8_t)value & 0x60u;
            if (mode == 0x40u && (timing->smc[0] & 0x20u) != 0u)
                timing->smc_run_status = 4u;
            else if (mode == 0x60u && (timing->smc[0] & 0x80u) != 0u)
                timing->smc_run_status = 0x80u;
            else if (mode == 0u)
                timing->smc_run_status = 1u;
            if (!timing->cpu_sleeping)
                timing->smc[3] = timing->smc_run_status;
        } else if (offset == 2u)
            timing->smc[2] = (uint8_t)value;
        return true;
    }
    if (contains(timing, K22_PERIPHERAL_RCM, address, size)) {
        const uint8_t offset = (uint8_t)(address - RCM_BASE);
        if (offset == 4u || offset == 5u)
            timing->rcm[offset] = (uint8_t)value;
        else if (offset == 8u || offset == 9u)
            timing->rcm[offset] &= (uint8_t)~value;
        else
            return false;
        return true;
    }
    return false;
}

bool k22_timing_write(K22Timing* timing, uint32_t address, uint8_t size, uint32_t value) {
    if (timing == NULL || timing->profile == NULL) {
        return false;
    }
    if (address >= SIM_BASE && address < SIM_BASE + 0x2000u && has(timing, K22_PERIPHERAL_SIM))
        return write_sim(timing, address, size, value);
    if (write_control_register(timing, address, size, value))
        return true;
    if (address >= WDOG_BASE && address < WDOG_BASE + 0x18u && has(timing, K22_PERIPHERAL_WDOG))
        return write_wdog(timing, address, size, value);
    if (address >= EWM_BASE && address < EWM_BASE + 6u && has(timing, K22_PERIPHERAL_EWM))
        return write_ewm(timing, address, size, value);
    return write_timed_register(timing, address, size, value);
}

void k22_timing_advance(K22Timing* timing, uint32_t core_cycles) {
    if (timing == NULL || timing->profile == NULL || core_cycles == 0) {
        return;
    }
    timing->elapsed_core_cycles += core_cycles;
    advance_wdog(timing, core_cycles);
    advance_ewm(timing, core_cycles);
    advance_pit(timing, core_cycles);
    advance_lptmr(timing, core_cycles);
    advance_rtc(timing, core_cycles);
    advance_pdb(timing, core_cycles);
    for (uint8_t index = 0; index < 4; index++) {
        const K22PeripheralId id = (K22PeripheralId)(K22_PERIPHERAL_FTM0 + index);
        if (has(timing, id))
            advance_ftm(timing, index, core_cycles);
    }
}

void k22_timing_set_debug_halted(K22Timing* timing, bool halted) {
    if (timing == NULL || timing->debug_halted == halted)
        return;
    timing->debug_halted = halted;
    if (halted && timing->wdog_initial_unlock_required &&
        timing->wdog_bus_cycles <= timing->wdog_update_deadline) {
        timing->wdog_initial_debug_remaining =
            timing->wdog_update_deadline - timing->wdog_bus_cycles;
        timing->wdog_initial_debug_pause = true;
    } else if (!halted && timing->wdog_initial_debug_pause) {
        timing->wdog_update_deadline =
            timing->wdog_bus_cycles + timing->wdog_initial_debug_remaining;
        timing->wdog_initial_debug_pause = false;
    }
}

bool k22_timing_trigger_low_voltage_warning(K22Timing* timing) {
    if (timing == NULL || timing->profile == NULL || !has(timing, K22_PERIPHERAL_PMC))
        return false;
    if (timing->smc[3] != 1u && timing->smc[3] != 2u && timing->smc[3] != 0x80u)
        return true;
    timing->pmc[1] |= 0x80u;
    update_pmc_irq(timing);
    return true;
}

bool k22_timing_trigger_low_voltage_detect(K22Timing* timing) {
    if (timing == NULL || timing->profile == NULL || !has(timing, K22_PERIPHERAL_PMC))
        return false;
    if (timing->smc[3] != 1u && timing->smc[3] != 2u && timing->smc[3] != 0x80u)
        return true;
    timing->pmc[0] |= 0x80u;
    if ((timing->pmc[0] & 0x10u) != 0u)
        signal_reset(timing, 2u, 0u);
    else
        update_pmc_irq(timing);
    return true;
}

static bool llwu_edge_detected(uint8_t edge, bool previous, bool high) {
    return (edge == 1u && !previous && high) || (edge == 2u && previous && !high) ||
           (edge == 3u && previous != high);
}

static bool llwu_low_leakage(const K22Timing* timing) {
    return timing->smc[3] == 0x20u || timing->smc[3] == 0x40u;
}

static void llwu_wake(K22Timing* timing) {
    if (timing->smc[3] == 0x40u)
        signal_reset(timing, 1u, 0u);
    else
        update_llwu_irq(timing);
}

bool k22_timing_set_llwu_pin(K22Timing* timing, uint8_t pin, bool high) {
    if (timing == NULL || timing->profile == NULL || pin >= 16u ||
        !has(timing, K22_PERIPHERAL_LLWU))
        return false;
    const bool previous = timing->llwu_pin_level[pin];
    timing->llwu_pin_level[pin] = high;
    if (!llwu_low_leakage(timing))
        return true;
    bool wake = false;
    const uint8_t pin_edge = (timing->llwu[pin / 4u] >> ((pin & 3u) * 2u)) & 3u;
    if (llwu_edge_detected(pin_edge, previous, high)) {
        timing->llwu[5u + pin / 8u] |= (uint8_t)(1u << (pin & 7u));
        wake = true;
    }
    for (uint8_t filter = 0u; filter < 2u; filter++) {
        const uint8_t control = timing->llwu[8u + filter];
        if ((control & 15u) == pin && llwu_edge_detected((control >> 5u) & 3u, previous, high)) {
            timing->llwu[8u + filter] |= 0x80u;
            wake = true;
        }
    }
    if (wake)
        llwu_wake(timing);
    return true;
}

bool k22_timing_trigger_llwu_module(K22Timing* timing, uint8_t module) {
    if (timing == NULL || timing->profile == NULL || module >= 8u ||
        !has(timing, K22_PERIPHERAL_LLWU))
        return false;
    if (llwu_low_leakage(timing) && (timing->llwu[4] & (1u << module)) != 0u) {
        timing->llwu[7] |= (uint8_t)(1u << module);
        llwu_wake(timing);
    }
    return true;
}

void k22_timing_set_cpu_sleeping(K22Timing* timing, bool sleeping, bool deep_sleep) {
    if (timing == NULL || timing->profile == NULL ||
        (timing->cpu_sleeping == sleeping && timing->deep_sleeping == deep_sleep))
        return;
    if (sleeping && !timing->cpu_sleeping && timing->ewm_service_stage == 1u &&
        timing->wdog_bus_cycles <= timing->ewm_service_deadline) {
        timing->ewm_service_remaining = timing->ewm_service_deadline - timing->wdog_bus_cycles;
        timing->ewm_service_paused = true;
    } else if (!sleeping && timing->ewm_service_paused) {
        timing->ewm_service_deadline = timing->wdog_bus_cycles + timing->ewm_service_remaining;
        timing->ewm_service_paused = false;
    }
    timing->cpu_sleeping = sleeping;
    timing->deep_sleeping = sleeping && deep_sleep;
    if (!sleeping) {
        if (timing->smc[3] != 0x40u)
            timing->smc[3] = timing->smc_run_status;
        return;
    }
    if (!deep_sleep) {
        timing->smc[3] = timing->smc_run_status == 4u ? 8u : timing->smc_run_status;
        return;
    }
    const uint8_t stop_mode = timing->smc[1] & 7u;
    if (stop_mode == 0u)
        timing->smc[3] = 2u;
    else if (stop_mode == 2u && (timing->smc[0] & 0x20u) != 0u)
        timing->smc[3] = 0x10u;
    else if (stop_mode == 3u && (timing->smc[0] & 8u) != 0u)
        timing->smc[3] = 0x20u;
    else if (stop_mode == 4u && (timing->smc[0] & 2u) != 0u)
        timing->smc[3] = 0x40u;
}

bool k22_timing_set_ewm_input(K22Timing* timing, bool high) {
    if (timing == NULL || timing->profile == NULL || !has(timing, K22_PERIPHERAL_EWM))
        return false;
    timing->ewm_input = high;
    return true;
}

bool k22_timing_ewm_output(const K22Timing* timing) {
    return timing != NULL && timing->profile != NULL && has(timing, K22_PERIPHERAL_EWM) &&
           timing->ewm_output;
}

bool k22_timing_copy(K22Timing* destination, const K22Timing* source, K22TimingSignals signals) {
    if (destination == NULL || source == NULL || source->profile == NULL) {
        return false;
    }
    *destination = *source;
    destination->signals = signals;
    return true;
}

uint32_t k22_timing_core_clock_hz(const K22Timing* timing) {
    return timing == NULL ? 0 : timing->core_clock_hz;
}

uint32_t k22_timing_bus_clock_hz(const K22Timing* timing) {
    return timing == NULL ? 0 : timing->bus_clock_hz;
}
