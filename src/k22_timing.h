#ifndef CORTEX_M4_K22_TIMING_H
#define CORTEX_M4_K22_TIMING_H

#include "k22_profile.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*K22TimingIrqSignal)(void* context, uint8_t irq, bool asserted);
typedef void (*K22TimingDmaSignal)(void* context, uint8_t source);
typedef void (*K22TimingResetSignal)(void* context, uint8_t srs0, uint8_t srs1);

typedef enum {
    K22_TIMING_TRIGGER_PDB_ADC,
    K22_TIMING_TRIGGER_PDB_DAC,
    K22_TIMING_TRIGGER_ADC_ALTERNATE,
} K22TimingTrigger;

typedef void (*K22TimingTriggerSignal)(void* context, K22TimingTrigger trigger,
                                       uint8_t instance, uint8_t channel);

typedef struct {
    void* context;
    K22TimingIrqSignal irq;
    K22TimingDmaSignal dma;
    K22TimingResetSignal reset;
    K22TimingTriggerSignal trigger;
} K22TimingSignals;

typedef struct {
    uint32_t load;
    uint32_t current;
    uint32_t control;
    bool flag;
} K22PitChannel;

typedef struct {
    uint32_t sc;
    uint16_t counter;
    uint16_t modulo;
    uint16_t initial;
    uint32_t channel_sc[8];
    uint16_t channel_value[8];
    uint16_t channel_value_buffer[8];
    uint32_t registers[20];
    uint16_t modulo_buffer;
    uint16_t initial_buffer;
    uint32_t outmask_buffer;
    uint32_t invctrl_buffer;
    uint32_t swoctrl_buffer;
    uint64_t remainder;
    bool trigger_flag_read;
    bool overflow_flag_read;
    bool channel_flag_read[8];
    bool channel_input[8];
    bool channel_filtered_input[8];
    bool channel_output[8];
    bool channel_deadtime_output[8];
    bool channel_value_pending[8];
    uint32_t channel_input_age[8];
    uint32_t channel_deadtime_remaining[8];
    bool outmask_pending;
    bool invctrl_pending;
    bool swoctrl_pending;
    bool modulo_pending;
    bool initial_pending;
    bool software_sync_pending;
    bool hardware_sync_pending;
    bool write_protection_read;
    bool counting_down;
    uint8_t hardware_trigger_pending_mask;
    uint8_t overflow_count;
    uint64_t deadtime_remainder;
} K22FtmState;

typedef struct {
    const K22Profile* profile;
    K22TimingSignals signals;
    uint32_t external_oscillator_hz;
    uint32_t rtc_oscillator_hz;
    uint32_t slow_irc_hz;
    uint32_t fast_irc_hz;
    uint32_t lpo_hz;
    uint32_t core_clock_hz;
    uint32_t bus_clock_hz;
    uint32_t flash_clock_hz;
    uint64_t elapsed_core_cycles;
    bool debug_halted;
    uint32_t sim_sopt1;
    uint32_t sim_sopt1cfg;
    uint32_t sim_sopt2;
    uint32_t sim_sopt4;
    uint32_t sim_sopt5;
    uint32_t sim_sopt7;
    uint32_t sim_sopt8;
    uint32_t sim_scgc3;
    uint32_t sim_scgc4;
    uint32_t sim_scgc5;
    uint32_t sim_scgc6;
    uint32_t sim_scgc7;
    uint32_t sim_clkdiv1;
    uint32_t sim_clkdiv2;
    uint8_t mcg[14];
    uint8_t osc_cr;
    uint8_t osc_div;
    uint8_t llwu[11];
    uint8_t pmc[3];
    uint8_t smc[4];
    uint8_t rcm[10];
    uint32_t pit_mcr;
    K22PitChannel pit[4];
    uint64_t pit_remainder;
    uint32_t lptmr_csr;
    uint32_t lptmr_psr;
    uint32_t lptmr_cmr;
    uint16_t lptmr_counter;
    uint16_t lptmr_latched_counter;
    uint64_t lptmr_remainder;
    uint64_t lptmr_filter_remainder;
    uint32_t lptmr_filter_ticks;
    bool lptmr_input[3];
    bool lptmr_observed_active;
    uint32_t rtc_tsr;
    uint16_t rtc_tpr;
    uint32_t rtc_tar;
    uint32_t rtc_tcr;
    uint32_t rtc_cr;
    uint32_t rtc_sr;
    uint32_t rtc_lr;
    uint32_t rtc_ier;
    uint32_t rtc_war;
    uint32_t rtc_rar;
    uint64_t rtc_remainder;
    uint32_t rtc_subsecond_ticks;
    uint32_t pdb_sc;
    uint16_t pdb_mod;
    uint16_t pdb_counter;
    uint16_t pdb_idly;
    uint32_t pdb_registers[104];
    uint64_t pdb_remainder;
    K22FtmState ftm[4];
    uint16_t wdog[12];
    uint32_t wdog_counter;
    uint16_t wdog_unlock_stage;
    uint16_t wdog_refresh_stage;
    uint64_t wdog_remainder;
    uint8_t ewm_ctrl;
    uint8_t ewm_cmpl;
    uint8_t ewm_cmph;
    uint8_t ewm_prescaler;
    uint8_t ewm_service_stage;
    uint32_t ewm_counter;
    uint64_t ewm_remainder;
} K22Timing;

bool k22_timing_init(K22Timing* timing, const K22Profile* profile,
                     uint32_t external_oscillator_hz, uint32_t rtc_oscillator_hz,
                     K22TimingSignals signals);
bool k22_timing_read(K22Timing* timing, uint32_t address, uint8_t size, uint32_t* value);
bool k22_timing_write(K22Timing* timing, uint32_t address, uint8_t size, uint32_t value);
void k22_timing_advance(K22Timing* timing, uint32_t core_cycles);
void k22_timing_set_debug_halted(K22Timing* timing, bool halted);
bool k22_timing_set_lptmr_input(K22Timing* timing, uint8_t input, bool high);
bool k22_timing_set_ftm_input(K22Timing* timing, uint8_t instance, uint8_t channel,
                              bool high);
bool k22_timing_trigger_ftm_hardware(K22Timing* timing, uint8_t instance, uint8_t trigger);
bool k22_timing_get_ftm_output(const K22Timing* timing, uint8_t instance, uint8_t channel,
                               bool* high);
void k22_timing_reset(K22Timing* timing, uint8_t srs0, uint8_t srs1);
void k22_timing_warm_reset(K22Timing* timing, uint8_t srs0, uint8_t srs1);
bool k22_timing_copy(K22Timing* destination, const K22Timing* source,
                     K22TimingSignals signals);
uint32_t k22_timing_core_clock_hz(const K22Timing* timing);
uint32_t k22_timing_bus_clock_hz(const K22Timing* timing);

#endif
