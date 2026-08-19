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

static void request_dma(const K22Timing* timing, uint8_t source) {
    if (timing->signals.dma != NULL) {
        timing->signals.dma(timing->signals.context, source);
    }
}

static bool has(const K22Timing* timing, K22PeripheralId peripheral) {
    return timing->profile != NULL &&
           k22_profile_has_peripheral(timing->profile, peripheral);
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
    uint32_t reference =
        (timing->mcg[0] & 4u) != 0 ? timing->slow_irc_hz : timing->external_oscillator_hz;
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
    return (uint32_t)(((uint64_t)timing->external_oscillator_hz * multiplier) /
                      (divider * 2u));
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

static bool read_sim(const K22Timing* timing, uint32_t address, uint8_t size,
                     uint32_t* value) {
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
        *value = 0x7fff0000u;
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
        timing->sim_sopt7 = value;
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
        timing->sim_scgc5 =
            (timing->sim_scgc5 & ~0x00003e01u) | (value & 0x00003e01u);
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

static bool read_byte_block(const uint8_t* data, uint32_t base, uint32_t length,
                            uint32_t address, uint8_t size, uint32_t* value) {
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
        expirations =
            additional >= UINT32_MAX ? UINT32_MAX : expirations + (uint32_t)additional;
        pit->current = pit->load - (uint32_t)(ticks % period);
        pit->flag = true;
        if ((pit->control & 2u) != 0) {
            set_irq(timing, IRQ_PIT0 + channel, true);
        }
    } else {
        pit->current -= (uint32_t)ticks;
    }
    return expirations;
}

static void advance_pit(K22Timing* timing, uint32_t cycles) {
    if (!has(timing, K22_PERIPHERAL_PIT) || (timing->sim_scgc6 & (1u << 23u)) == 0 ||
        (timing->pit_mcr & 2u) != 0) {
        return;
    }
    const uint64_t ticks = clock_ticks(&timing->pit_remainder, cycles, timing->bus_clock_hz,
                                       timing->core_clock_hz);
    uint64_t source = ticks;
    for (uint8_t channel = 0; channel < 4; channel++) {
        if ((timing->pit[channel].control & 4u) == 0) {
            source = ticks;
        }
        source = advance_pit_channel(timing, channel, source);
    }
}

static bool pit_read(const K22Timing* timing, uint32_t address, uint8_t size,
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
        timing->pit[channel].current = value;
        return true;
    case 8:
        timing->pit[channel].control = value & 7u;
        return true;
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

static void advance_lptmr(K22Timing* timing, uint32_t cycles) {
    if (!has(timing, K22_PERIPHERAL_LPTMR0) || (timing->sim_scgc5 & 1u) == 0 ||
        (timing->lptmr_csr & 1u) == 0 || (timing->lptmr_csr & 2u) != 0) {
        return;
    }
    uint32_t source_hz = lptmr_clock(timing);
    if ((timing->lptmr_psr & 4u) == 0) {
        source_hz >>= ((timing->lptmr_psr >> 3u) & 15u) + 1u;
    }
    const uint64_t ticks =
        clock_ticks(&timing->lptmr_remainder, cycles, source_hz, timing->core_clock_hz);
    const uint32_t compare = timing->lptmr_cmr & 0xffffu;
    if (ticks == 0) {
        return;
    }
    const uint64_t total = (uint64_t)timing->lptmr_counter + ticks;
    if (compare != 0 && total >= compare) {
        timing->lptmr_csr |= 0x80u;
        if ((timing->lptmr_csr & 0x40u) != 0) {
            set_irq(timing, IRQ_LPTMR, true);
        }
        timing->lptmr_counter = (uint16_t)(total % ((uint64_t)compare + 1u));
    } else {
        timing->lptmr_counter = (uint16_t)total;
    }
}

static void update_rtc_irq(const K22Timing* timing) {
    set_irq(timing, IRQ_RTC,
            timing->rtc_tsr == timing->rtc_tar && (timing->rtc_ier & 4u) != 0);
}

static void advance_rtc(K22Timing* timing, uint32_t cycles) {
    if (!has(timing, K22_PERIPHERAL_RTC) || (timing->sim_scgc6 & (1u << 29u)) == 0 ||
        (timing->rtc_sr & 0x10u) == 0 || (timing->rtc_sr & 1u) != 0) {
        return;
    }
    const uint64_t ticks = clock_ticks(&timing->rtc_remainder, cycles,
                                       timing->rtc_oscillator_hz, timing->core_clock_hz);
    const uint64_t prescaled = (uint64_t)timing->rtc_tpr + ticks;
    const uint64_t seconds = prescaled >> 15u;
    timing->rtc_tpr = (uint16_t)(prescaled & 0x7fffu);
    if (seconds == 0) {
        return;
    }
    const uint32_t previous = timing->rtc_tsr;
    timing->rtc_tsr += (uint32_t)seconds;
    if ((timing->rtc_ier & 0x10u) != 0) {
        set_irq(timing, IRQ_RTC_SECONDS, true);
    }
    if (timing->rtc_tar > previous && timing->rtc_tar <= timing->rtc_tsr) {
        set_irq(timing, IRQ_RTC, (timing->rtc_ier & 4u) != 0);
    }
    if (timing->rtc_tsr < previous) {
        timing->rtc_sr |= 0x10u;
        set_irq(timing, IRQ_RTC, (timing->rtc_ier & 2u) != 0);
    }
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

static bool counter_reached(uint16_t start, uint64_t ticks, uint32_t period,
                            uint16_t target) {
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
    const bool delayed = timing->pdb_idly <= timing->pdb_mod &&
                         counter_reached((uint16_t)(total - ticks), ticks,
                                         (uint32_t)period, timing->pdb_idly);
    timing->pdb_counter = (uint16_t)(total % period);
    for (uint8_t channel = 0; channel < 2u; channel++) {
        const uint32_t base = 0x10u + (uint32_t)channel * 0x28u;
        const uint32_t control = timing->pdb_registers[base >> 2u];
        for (uint8_t pretrigger = 0; pretrigger < 2u; pretrigger++) {
            const uint16_t delay =
                (uint16_t)
                    timing->pdb_registers[(base + 8u + (uint32_t)pretrigger * 4u) >> 2u];
            if ((control & (1u << pretrigger)) != 0 &&
                counter_reached((uint16_t)(total - ticks), ticks, (uint32_t)period, delay))
                timing->pdb_registers[(base + 4u) >> 2u] |= 1u << pretrigger;
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
    const K22PeripheralId ids[4] = {K22_PERIPHERAL_FTM0, K22_PERIPHERAL_FTM1,
                                    K22_PERIPHERAL_FTM2, K22_PERIPHERAL_FTM3};
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

static bool ftm_gate(const K22Timing* timing, uint8_t index) {
    if (index == 3u) {
        if (timing->profile->id == K22_PROFILE_MK22FN1M012 ||
            timing->profile->id == K22_PROFILE_MK22FX51212)
            return (timing->sim_scgc3 & (1u << 25u)) != 0;
        return (timing->sim_scgc6 & (1u << 6u)) != 0;
    }
    return (timing->sim_scgc6 & (1u << (24u + index))) != 0;
}

static void advance_ftm(K22Timing* timing, uint8_t index, uint32_t cycles) {
    K22FtmState* ftm = &timing->ftm[index];
    const uint8_t clock_select = (uint8_t)((ftm->sc >> 3u) & 3u);
    if (!ftm_gate(timing, index) || clock_select == 0) {
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
        clock_ticks(&ftm->remainder, cycles, source_hz, timing->core_clock_hz);
    if (ticks == 0) {
        return;
    }
    const uint32_t first = ftm->initial;
    const uint32_t last = ftm->modulo >= first ? ftm->modulo : 0xffffu;
    const uint32_t period = last - first + 1u;
    const uint32_t start =
        ftm->counter < first || ftm->counter > last ? first : ftm->counter;
    const uint64_t relative = (uint64_t)(start - first) + ticks;
    const bool overflow = relative >= period;
    const uint8_t channels = index == 0u || index == 3u ? 8u : 2u;
    for (uint8_t channel = 0; channel < channels; channel++) {
        const uint32_t compare = ftm->channel_value[channel];
        const uint32_t distance =
            compare > start ? compare - start : period - (start - compare);
        if (compare >= first && compare <= last && ticks >= distance) {
            ftm->channel_sc[channel] |= 1u << 7u;
            if ((ftm->channel_sc[channel] & (1u << 6u)) != 0) {
                set_irq(timing, ftm_irq(index), true);
            }
            if ((ftm->channel_sc[channel] & 1u) != 0) {
                request_dma(timing, ftm_dma_source(index, channel));
            }
        }
    }
    ftm->counter = (uint16_t)(first + relative % period);
    if (overflow) {
        ftm->sc |= 1u << 7u;
        if ((ftm->sc & (1u << 6u)) != 0) {
            set_irq(timing, ftm_irq(index), true);
        }
    }
}

static uint32_t wdog_timeout(const K22Timing* timing) {
    return ((uint32_t)timing->wdog[2] << 16u) | timing->wdog[3];
}

static void signal_reset(K22Timing* timing, uint8_t srs0, uint8_t srs1) {
    if (timing->signals.reset != NULL) {
        timing->signals.reset(timing->signals.context, srs0, srs1);
    }
    k22_timing_reset(timing, srs0, srs1);
}

static void advance_wdog(K22Timing* timing, uint32_t cycles) {
    if (!has(timing, K22_PERIPHERAL_WDOG) || (timing->wdog[0] & 1u) == 0) {
        return;
    }
    const uint32_t source_hz =
        (timing->wdog[0] & (1u << 13u)) != 0 ? timing->bus_clock_hz : timing->lpo_hz;
    const uint32_t divider = ((timing->wdog[11] & 0x700u) >> 8u) + 1u;
    const uint64_t ticks = clock_ticks(&timing->wdog_remainder, cycles, source_hz / divider,
                                       timing->core_clock_hz);
    const uint32_t timeout = wdog_timeout(timing);
    if (timeout == 0 || timing->wdog_counter >= timeout ||
        ticks >= timeout - timing->wdog_counter) {
        if ((timing->wdog[0] & 4u) != 0) {
            set_irq(timing, IRQ_WDOG_EWM, true);
            timing->wdog_counter = 0;
        } else {
            signal_reset(timing, 0x20u, 0);
        }
    } else {
        timing->wdog_counter += (uint32_t)ticks;
    }
}

static void advance_ewm(K22Timing* timing, uint32_t cycles) {
    if (!has(timing, K22_PERIPHERAL_EWM) || (timing->ewm_ctrl & 1u) == 0) {
        return;
    }
    const uint32_t source_hz = timing->lpo_hz / ((uint32_t)timing->ewm_prescaler + 1u);
    const uint64_t ticks =
        clock_ticks(&timing->ewm_remainder, cycles, source_hz, timing->core_clock_hz);
    const uint32_t increment = ticks >= UINT32_MAX ? UINT32_MAX : (uint32_t)ticks;
    timing->ewm_counter = increment >= UINT32_MAX - timing->ewm_counter
                              ? UINT32_MAX
                              : timing->ewm_counter + increment;
    if (timing->ewm_counter > timing->ewm_cmph) {
        signal_reset(timing, 0, 2u);
    }
}

static bool read_wdog(const K22Timing* timing, uint32_t address, uint8_t size,
                      uint32_t* value) {
    if (size != 2 || address < WDOG_BASE || address >= WDOG_BASE + 0x18u ||
        ((address - WDOG_BASE) & 1u) != 0) {
        return false;
    }
    const uint8_t index = (uint8_t)((address - WDOG_BASE) / 2u);
    if (index == 8u) {
        *value = timing->wdog_counter >> 16u;
    } else if (index == 9u) {
        *value = timing->wdog_counter;
    } else {
        *value = timing->wdog[index];
    }
    return true;
}

static bool write_wdog(K22Timing* timing, uint32_t address, uint8_t size, uint32_t value) {
    if (size != 2 || address < WDOG_BASE || address >= WDOG_BASE + 0x18u ||
        ((address - WDOG_BASE) & 1u) != 0) {
        return false;
    }
    const uint8_t index = (uint8_t)((address - WDOG_BASE) / 2u);
    if (index == 7u) {
        if (value == 0xc520u) {
            timing->wdog_unlock_stage = 1u;
        } else if (value == 0xd928u && timing->wdog_unlock_stage == 1u) {
            timing->wdog_unlock_stage = 2u;
        } else {
            timing->wdog_unlock_stage = 0;
        }
        return true;
    }
    if (index == 6u) {
        if (value == 0xa602u) {
            timing->wdog_refresh_stage = 1u;
        } else if (value == 0xb480u && timing->wdog_refresh_stage == 1u) {
            const uint32_t window = ((uint32_t)timing->wdog[4] << 16u) | timing->wdog[5];
            if ((timing->wdog[0] & (1u << 3u)) != 0 && timing->wdog_counter < window) {
                signal_reset(timing, 0x20u, 0);
            } else {
                timing->wdog_counter = 0;
            }
            timing->wdog_refresh_stage = 0;
        } else {
            timing->wdog_refresh_stage = 0;
        }
        return true;
    }
    if (timing->wdog_unlock_stage != 2u || index == 8u || index == 9u) {
        return true;
    }
    timing->wdog[index] = (uint16_t)value;
    return true;
}

static bool read_ewm(const K22Timing* timing, uint32_t address, uint8_t size,
                     uint32_t* value) {
    if (size != 1 || address < EWM_BASE || address > EWM_BASE + 5u ||
        address == EWM_BASE + 4u) {
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
        if ((timing->ewm_ctrl & 1u) == 0) {
            timing->ewm_ctrl = (uint8_t)value & 3u;
        }
        return true;
    case 1:
        if (value == 0xb4u) {
            timing->ewm_service_stage = 1u;
        } else if (value == 0x2cu && timing->ewm_service_stage == 1u &&
                   timing->ewm_counter >= timing->ewm_cmpl &&
                   timing->ewm_counter <= timing->ewm_cmph) {
            timing->ewm_counter = 0;
            timing->ewm_service_stage = 0;
        } else {
            signal_reset(timing, 0, 2u);
        }
        return true;
    case 2:
        if ((timing->ewm_ctrl & 1u) == 0)
            timing->ewm_cmpl = (uint8_t)value;
        return true;
    case 3:
        if ((timing->ewm_ctrl & 1u) == 0)
            timing->ewm_cmph = (uint8_t)value;
        return true;
    case 5:
        if ((timing->ewm_ctrl & 1u) == 0)
            timing->ewm_prescaler = (uint8_t)value;
        return true;
    default:
        return false;
    }
}

static bool ftm_read(const K22Timing* timing, uint8_t index, uint32_t offset, uint8_t size,
                     uint32_t* value) {
    if (size != 4 || (offset & 3u) != 0) {
        return false;
    }
    const K22FtmState* ftm = &timing->ftm[index];
    if (offset == 0)
        *value = ftm->sc;
    else if (offset == 4)
        *value = ftm->counter;
    else if (offset == 8)
        *value = ftm->modulo;
    else if (offset >= 0x0cu && offset < 0x4cu) {
        const uint8_t channel = (uint8_t)((offset - 0x0cu) / 8u);
        if (channel >= (index == 0u || index == 3u ? 8u : 2u))
            return false;
        *value = ((offset - 0x0cu) & 4u) == 0 ? ftm->channel_sc[channel]
                                              : ftm->channel_value[channel];
    } else if (offset == 0x4cu)
        *value = ftm->initial;
    else if (offset == 0x50u) {
        uint32_t status = 0;
        const uint8_t channels = index == 0u || index == 3u ? 8u : 2u;
        for (uint8_t channel = 0; channel < channels; channel++) {
            status |= ((ftm->channel_sc[channel] >> 7u) & 1u) << channel;
        }
        *value = status;
    } else if (offset >= 0x54u && offset <= 0x98u) {
        *value = ftm->registers[(offset - 0x54u) / 4u];
    } else
        return false;
    return true;
}

static bool ftm_write(K22Timing* timing, uint8_t index, uint32_t offset, uint8_t size,
                      uint32_t value) {
    if (size != 4 || (offset & 3u) != 0) {
        return false;
    }
    K22FtmState* ftm = &timing->ftm[index];
    if (offset == 0) {
        ftm->sc = (ftm->sc & 0x80u & value) | (value & 0x7fu);
    } else if (offset == 4)
        ftm->counter = (uint16_t)value;
    else if (offset == 8)
        ftm->modulo = (uint16_t)value;
    else if (offset >= 0x0cu && offset < 0x4cu) {
        const uint8_t channel = (uint8_t)((offset - 0x0cu) / 8u);
        if (channel >= (index == 0u || index == 3u ? 8u : 2u))
            return false;
        if (((offset - 0x0cu) & 4u) == 0) {
            ftm->channel_sc[channel] =
                (ftm->channel_sc[channel] & value & 0x80u) | (value & 0x7fu);
            if ((ftm->channel_sc[channel] & 0x80u) == 0)
                set_irq(timing, ftm_irq(index), false);
        } else
            ftm->channel_value[channel] = (uint16_t)value;
    } else if (offset == 0x4cu)
        ftm->initial = (uint16_t)value;
    else if (offset == 0x50u) {
        const uint8_t channels = index == 0u || index == 3u ? 8u : 2u;
        for (uint8_t channel = 0; channel < channels; channel++) {
            if ((value & (1u << channel)) == 0)
                ftm->channel_sc[channel] &= ~0x80u;
        }
    } else if (offset >= 0x54u && offset <= 0x98u) {
        ftm->registers[(offset - 0x54u) / 4u] = value;
    } else
        return false;
    return true;
}

static bool read_timed_register(const K22Timing* timing, uint32_t address, uint8_t size,
                                uint32_t* value) {
    if (address >= PIT_BASE && address < PIT_BASE + 0x140u &&
        has(timing, K22_PERIPHERAL_PIT))
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
            *value = timing->lptmr_counter;
            return true;
        default:
            return false;
        }
    }
    if (address >= RTC_BASE && address <= RTC_BASE + 0x804u && size == 4 &&
        has(timing, K22_PERIPHERAL_RTC)) {
        switch (address - RTC_BASE) {
        case 0:
            *value = timing->rtc_tsr;
            return true;
        case 4:
            *value = timing->rtc_tpr;
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
    if (address >= PIT_BASE && address < PIT_BASE + 0x140u &&
        has(timing, K22_PERIPHERAL_PIT))
        return pit_write(timing, address, size, value);
    if (address >= LPTMR_BASE && address < LPTMR_BASE + 0x10u && size == 4 &&
        has(timing, K22_PERIPHERAL_LPTMR0)) {
        switch (address - LPTMR_BASE) {
        case 0:
            timing->lptmr_csr = (timing->lptmr_csr & 0x80u & value) | (value & 0x7fu);
            if ((timing->lptmr_csr & 0x80u) == 0)
                set_irq(timing, IRQ_LPTMR, false);
            if ((value & 1u) == 0)
                timing->lptmr_counter = 0;
            return true;
        case 4:
            timing->lptmr_psr = value & 0x7fu;
            return true;
        case 8:
            timing->lptmr_cmr = value & 0xffffu;
            return true;
        case 12:
            return true;
        default:
            return false;
        }
    }
    if (address >= RTC_BASE && address <= RTC_BASE + 0x804u && size == 4 &&
        has(timing, K22_PERIPHERAL_RTC)) {
        switch (address - RTC_BASE) {
        case 0:
            timing->rtc_tsr = value;
            timing->rtc_sr &= ~1u;
            return true;
        case 4:
            timing->rtc_tpr = (uint16_t)value & 0x7fffu;
            return true;
        case 8:
            timing->rtc_tar = value;
            update_rtc_irq(timing);
            return true;
        case 12:
            timing->rtc_tcr = value;
            return true;
        case 16:
            timing->rtc_cr = value;
            return true;
        case 20:
            timing->rtc_sr = value & 0x1fu;
            update_rtc_irq(timing);
            return true;
        case 24:
            timing->rtc_lr &= value;
            return true;
        case 28:
            timing->rtc_ier = value & 0x17u;
            update_rtc_irq(timing);
            return true;
        case 0x800:
            timing->rtc_war &= value;
            return true;
        case 0x804:
            timing->rtc_rar &= value;
            return true;
        default:
            return false;
        }
    }
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

bool k22_timing_init(K22Timing* timing, const K22Profile* profile,
                     uint32_t external_oscillator_hz, uint32_t rtc_oscillator_hz,
                     K22TimingSignals signals) {
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
    timing->sim_sopt1 = 0x80000000u;
    timing->sim_sopt2 = 0x1000u;
    timing->sim_scgc4 = 0xf0100030u;
    timing->sim_scgc5 = 0x00040182u;
    timing->sim_scgc6 = 0x40000001u;
    timing->sim_scgc7 = 2u;
    timing->sim_clkdiv1 = 0x00110000u;
    timing->mcg[0] = 4u;
    timing->mcg[1] = 0x80u;
    timing->mcg[6] = 0x10u;
    timing->mcg[8] = 2u;
    timing->mcg[13] = 0x80u;
    timing->pmc[0] = 0x10u;
    timing->pmc[2] = 4u;
    timing->smc[2] = 3u;
    timing->smc[3] = 1u;
    timing->rcm[0] = srs0;
    timing->rcm[1] = srs1;
    if (srs0 == 0x82u && srs1 == 0) {
        timing->rcm[8] = srs0;
        timing->rcm[9] = srs1;
    } else {
        timing->rcm[8] = sticky0 | srs0;
        timing->rcm[9] = sticky1 | srs1;
    }
    timing->pit_mcr = 6u;
    timing->rtc_sr = 1u;
    timing->rtc_lr = 0xffu;
    timing->rtc_ier = 7u;
    timing->rtc_war = 0xffu;
    timing->rtc_rar = 0xffu;
    timing->pdb_mod = 0xffffu;
    timing->pdb_idly = 0xffffu;
    for (uint8_t index = 0; index < 4; index++) {
        timing->ftm[index].modulo = 0;
        timing->ftm[index].registers[0] = 4u;
    }
    timing->wdog[0] = 0x01d3u;
    timing->wdog[1] = 1u;
    timing->wdog[2] = 0x004cu;
    timing->wdog[3] = 0x4b4cu;
    timing->wdog[5] = 0x10u;
    timing->wdog[6] = 0xb480u;
    timing->wdog[7] = 0xd928u;
    timing->wdog[11] = 0x0400u;
    timing->ewm_cmph = 0xffu;
    update_clocks(timing);
}

bool k22_timing_read(const K22Timing* timing, uint32_t address, uint8_t size,
                     uint32_t* value) {
    if (timing == NULL || timing->profile == NULL || value == NULL) {
        return false;
    }
    if (address >= SIM_BASE && address < SIM_BASE + 0x2000u &&
        has(timing, K22_PERIPHERAL_SIM)) {
        return read_sim(timing, address, size, value);
    }
    if (address >= MCG_BASE && address < MCG_BASE + 14u &&
        mcg_register(address - MCG_BASE) && has(timing, K22_PERIPHERAL_MCG)) {
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
    if (address >= WDOG_BASE && address < WDOG_BASE + 0x18u &&
        has(timing, K22_PERIPHERAL_WDOG))
        return read_wdog(timing, address, size, value);
    if (address >= EWM_BASE && address < EWM_BASE + 6u && has(timing, K22_PERIPHERAL_EWM))
        return read_ewm(timing, address, size, value);
    return read_timed_register(timing, address, size, value);
}

static bool write_control_register(K22Timing* timing, uint32_t address, uint8_t size,
                                   uint32_t value) {
    if (address >= MCG_BASE && address < MCG_BASE + 14u &&
        mcg_register(address - MCG_BASE) && size == 1 && has(timing, K22_PERIPHERAL_MCG)) {
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
        else if (offset != 7u)
            timing->llwu[offset] = (uint8_t)value;
        if ((timing->llwu[5] | timing->llwu[6] | timing->llwu[7]) == 0)
            set_irq(timing, IRQ_LLWU, false);
        return true;
    }
    if (contains(timing, K22_PERIPHERAL_PMC, address, size)) {
        const uint8_t offset = (uint8_t)(address - PMC_BASE);
        const uint8_t flags = offset < 2u ? 0xc0u : 8u;
        timing->pmc[offset] = ((uint8_t)value & (uint8_t)~flags) |
                              (timing->pmc[offset] & flags & (uint8_t)~value);
        if ((timing->pmc[0] & 0x20u) == 0 && (timing->pmc[1] & 0x20u) == 0)
            set_irq(timing, 20u, false);
        return true;
    }
    if (contains(timing, K22_PERIPHERAL_SMC, address, size)) {
        const uint8_t offset = (uint8_t)(address - SMC_BASE);
        if (offset == 0u)
            timing->smc[0] |= (uint8_t)value & 0x2au;
        else if (offset == 1u) {
            timing->smc[1] = (uint8_t)value & 0xe7u;
            const uint8_t mode = (uint8_t)value & 0x60u;
            if (mode == 0x40u && (timing->smc[0] & 0x20u) != 0)
                timing->smc[3] = 4u;
            else if (mode == 0x60u && (timing->smc[0] & 2u) != 0)
                timing->smc[3] = 0x80u;
            else
                timing->smc[3] = 1u;
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
    if (address >= SIM_BASE && address < SIM_BASE + 0x2000u &&
        has(timing, K22_PERIPHERAL_SIM))
        return write_sim(timing, address, size, value);
    if (write_control_register(timing, address, size, value))
        return true;
    if (address >= WDOG_BASE && address < WDOG_BASE + 0x18u &&
        has(timing, K22_PERIPHERAL_WDOG))
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

bool k22_timing_copy(K22Timing* destination, const K22Timing* source,
                     K22TimingSignals signals) {
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
