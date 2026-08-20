#include "kinetis_k22.h"

#include <stdint.h>

#include "test.h"

enum {
    ADC0_SC1A = 0x4003b000u,
    ADC0_CFG1 = 0x4003b008u,
    ADC0_RA = 0x4003b010u,
    ADC0_SC3 = 0x4003b024u,
    ADC0_IRQ = 39,
    SIM_SCGC6 = 0x4004803cu,
};

static uint32_t read32(TestState* state, KinetisK22* device, uint32_t address) {
    uint32_t value = 0;
    TEST_EXPECT(state, kinetis_k22_read(device, address, &value, sizeof(value)));
    return value;
}

static void write32(TestState* state, KinetisK22* device, uint32_t address,
                    uint32_t value) {
    TEST_EXPECT(state, kinetis_k22_write(device, address, &value, sizeof(value)));
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = kinetis_k22_create(kinetis_k22_default_configuration());
    TEST_EXPECT(&state, device != NULL);
    write32(&state, device, SIM_SCGC6, read32(&state, device, SIM_SCGC6) | (1u << 27));
    write32(&state, device, ADC0_CFG1, 0x0cu);
    kinetis_k22_set_adc0_channel(device, 7, 0x345u);
    write32(&state, device, ADC0_SC1A, 7u | 0x40u);
    kinetis_k22_advance(device, 18u);
    TEST_EXPECT(&state, (read32(&state, device, ADC0_SC1A) & 0x80u) != 0);
    TEST_EXPECT(&state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), ADC0_IRQ));
    TEST_EXPECT(&state, read32(&state, device, ADC0_RA) == 0x345u);
    TEST_EXPECT(&state, (read32(&state, device, ADC0_SC1A) & 0x80u) == 0);
    cortex_m4_set_irq(kinetis_k22_cpu(device), ADC0_IRQ, false);
    write32(&state, device, ADC0_SC1A, 31u);
    TEST_EXPECT(&state, (read32(&state, device, ADC0_SC1A) & 0x80u) == 0);
    write32(&state, device, ADC0_SC3, 0x80u);
    TEST_EXPECT(&state, (read32(&state, device, ADC0_SC3) & 0x80u) == 0);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
