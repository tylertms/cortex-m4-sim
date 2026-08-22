
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
