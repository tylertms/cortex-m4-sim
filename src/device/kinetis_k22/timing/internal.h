#ifndef KINETIS_K22_SIM_K22_TIMING_INTERNAL_H
#define KINETIS_K22_SIM_K22_TIMING_INTERNAL_H

#include "device/kinetis_k22/timing/api.h"

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

bool k22_timing_internal_contains(const K22Timing* timing, K22PeripheralId peripheral,
                                  uint32_t address, uint8_t size);
bool k22_timing_internal_ftm_input_capture_mode(const K22FtmState* ftm, uint8_t channel);
bool k22_timing_internal_ftm_location(const K22Timing* timing, uint32_t address, uint8_t* index,
                                      uint32_t* offset);
bool k22_timing_internal_ftm_output_compare_mode(const K22FtmState* ftm, uint8_t channel);
bool k22_timing_internal_ftm_read(K22Timing* timing, uint8_t index, uint32_t offset, uint8_t size,
                                  uint32_t* value);
bool k22_timing_internal_has(const K22Timing* timing, K22PeripheralId peripheral);
bool k22_timing_internal_lptmr_selected_active(const K22Timing* timing);
bool k22_timing_internal_mcg_register(uint32_t offset);
bool k22_timing_internal_pdb_auxiliary_offset(uint32_t offset);
bool k22_timing_internal_pit_read(const K22Timing* timing, uint32_t address, uint8_t size,
                                  uint32_t* value);
bool k22_timing_internal_pit_write(K22Timing* timing, uint32_t address, uint8_t size,
                                   uint32_t value);
bool k22_timing_internal_read_byte_block(const uint8_t* data, uint32_t base, uint32_t length,
                                         uint32_t address, uint8_t size, uint32_t* value);
bool k22_timing_internal_read_ewm(const K22Timing* timing, uint32_t address, uint8_t size,
                                  uint32_t* value);
bool k22_timing_internal_read_sim(const K22Timing* timing, uint32_t address, uint8_t size,
                                  uint32_t* value);
bool k22_timing_internal_read_wdog(const K22Timing* timing, uint32_t address, uint8_t size,
                                   uint32_t* value);
bool k22_timing_internal_write_ewm(K22Timing* timing, uint32_t address, uint8_t size,
                                   uint32_t value);
bool k22_timing_internal_write_sim(K22Timing* timing, uint32_t address, uint8_t size,
                                   uint32_t value);
bool k22_timing_internal_write_wdog(K22Timing* timing, uint32_t address, uint8_t size,
                                    uint32_t value);
uint32_t k22_timing_internal_rtc_access_reset(const K22Timing* timing);
uint64_t k22_timing_internal_clock_ticks(uint64_t* remainder, uint32_t cycles, uint32_t source_hz,
                                         uint32_t core_hz);
uint8_t k22_timing_internal_ftm_active_fault_mask(const K22FtmState* ftm);
uint8_t k22_timing_internal_ftm_channel_count(uint8_t index);
uint8_t k22_timing_internal_ftm_fault_mode(const K22FtmState* ftm);
void k22_timing_internal_advance_ewm(K22Timing* timing, uint32_t cycles);
void k22_timing_internal_advance_ftm(K22Timing* timing, uint8_t index, uint32_t cycles);
void k22_timing_internal_advance_lptmr(K22Timing* timing, uint32_t cycles);
void k22_timing_internal_advance_pdb(K22Timing* timing, uint32_t cycles);
void k22_timing_internal_advance_pit(K22Timing* timing, uint32_t cycles);
void k22_timing_internal_advance_rtc(K22Timing* timing, uint32_t cycles);
void k22_timing_internal_advance_wdog(K22Timing* timing, uint32_t cycles);
void k22_timing_internal_ftm_apply_software_sync(K22FtmState* ftm);
void k22_timing_internal_ftm_trigger(K22Timing* timing, uint8_t index);
void k22_timing_internal_ftm_update_fault_status(K22FtmState* ftm);
void k22_timing_internal_request_dma(const K22Timing* timing, uint8_t source);
void k22_timing_internal_set_irq(const K22Timing* timing, uint8_t irq, bool asserted);
void k22_timing_internal_signal_reset(K22Timing* timing, uint8_t srs0, uint8_t srs1);
void k22_timing_internal_trigger_adc_alternate(K22Timing* timing, uint8_t source);
void k22_timing_internal_trigger_dma(const K22Timing* timing, uint8_t channel);
void k22_timing_internal_trigger(K22Timing* timing, K22TimingTrigger type, uint8_t instance,
                                 uint8_t channel);
void k22_timing_internal_update_clocks(K22Timing* timing);
void k22_timing_internal_update_ftm_irq(const K22Timing* timing, uint8_t index);
void k22_timing_internal_update_llwu_irq(const K22Timing* timing);
void k22_timing_internal_update_pmc_irq(const K22Timing* timing);
void k22_timing_internal_update_rtc_irq(const K22Timing* timing);

#endif
