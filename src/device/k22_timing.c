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

#include "timing/clocks.inc"
#include "timing/counters.inc"
#include "timing/ftm.inc"
#include "timing/watchdogs.inc"
#include "timing/registers.inc"
