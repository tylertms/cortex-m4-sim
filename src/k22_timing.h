#ifndef CORTEX_M4_K22_TIMING_H
#define CORTEX_M4_K22_TIMING_H

#include "k22_profile.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*K22TimingIrqSignal)(void* context, uint8_t irq, bool asserted);
typedef void (*K22TimingDmaSignal)(void* context, uint8_t source);
typedef void (*K22TimingResetSignal)(void* context, uint8_t srs0, uint8_t srs1);

typedef struct {
    void* context;
    K22TimingIrqSignal irq;
    K22TimingDmaSignal dma;
    K22TimingResetSignal reset;
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
    uint32_t registers[20];
    uint64_t remainder;
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
    uint64_t lptmr_remainder;
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
bool k22_timing_read(const K22Timing* timing, uint32_t address, uint8_t size,
                     uint32_t* value);
bool k22_timing_write(K22Timing* timing, uint32_t address, uint8_t size, uint32_t value);
void k22_timing_advance(K22Timing* timing, uint32_t core_cycles);
void k22_timing_reset(K22Timing* timing, uint8_t srs0, uint8_t srs1);
bool k22_timing_copy(K22Timing* destination, const K22Timing* source,
                     K22TimingSignals signals);
uint32_t k22_timing_core_clock_hz(const K22Timing* timing);
uint32_t k22_timing_bus_clock_hz(const K22Timing* timing);

#endif
