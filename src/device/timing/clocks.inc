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
