#include "internal.h"

void k22_timing_internal_set_irq(const K22Timing* timing, uint8_t irq, bool asserted) {
    if (timing->signals.irq != NULL) {
        timing->signals.irq(timing->signals.context, irq, asserted);
    }
}

void k22_timing_internal_update_pmc_irq(const K22Timing* timing) {
    const bool detect = (timing->pmc[0] & 0xa0u) == 0xa0u;
    const bool warning = (timing->pmc[1] & 0xa0u) == 0xa0u;
    k22_timing_internal_set_irq(timing, IRQ_LVD, detect || warning);
}

void k22_timing_internal_update_llwu_irq(const K22Timing* timing) {
    const bool pin = (timing->llwu[5] | timing->llwu[6]) != 0u;
    const bool module = timing->llwu[7] != 0u;
    const bool filter = ((timing->llwu[8] | timing->llwu[9]) & 0x80u) != 0u;
    k22_timing_internal_set_irq(timing, IRQ_LLWU, pin || module || filter);
}

void k22_timing_internal_request_dma(const K22Timing* timing, uint8_t source) {
    if (timing->signals.dma != NULL) {
        timing->signals.dma(timing->signals.context, source);
    }
}

void k22_timing_internal_trigger_dma(const K22Timing* timing, uint8_t channel) {
    if (timing->signals.dma_trigger != NULL)
        timing->signals.dma_trigger(timing->signals.context, channel);
}

void k22_timing_internal_trigger(K22Timing* timing, K22TimingTrigger type, uint8_t instance,
                                 uint8_t channel) {
    if (timing->signals.trigger != NULL)
        timing->signals.trigger(timing->signals.context, type, instance, channel);
}

void k22_timing_internal_trigger_adc_alternate(K22Timing* timing, uint8_t source) {
    k22_timing_internal_trigger(timing, K22_TIMING_TRIGGER_ADC_ALTERNATE, source, 0u);
}

bool k22_timing_internal_has(const K22Timing* timing, K22PeripheralId peripheral) {
    return timing->profile != NULL && k22_profile_has_peripheral(timing->profile, peripheral);
}

bool k22_timing_internal_contains(const K22Timing* timing, K22PeripheralId peripheral,
                                  uint32_t address, uint8_t size) {
    K22PeripheralLocation location;
    return k22_profile_resolve_peripheral(timing->profile, address, size, &location) &&
           location.id == peripheral;
}
