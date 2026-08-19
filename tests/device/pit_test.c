#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "test.h"

enum {
    PIT_MCR = 0x40037000u,
    PIT0_LDVAL = 0x40037100u,
    PIT0_CVAL = 0x40037104u,
    PIT0_TCTRL = 0x40037108u,
    PIT0_TFLG = 0x4003710cu,
    PIT0_IRQ = 48,
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
    write32(&state, device, SIM_SCGC6,
            read32(&state, device, SIM_SCGC6) | (1u << 23));
    write32(&state, device, PIT_MCR, 0);
    write32(&state, device, PIT0_LDVAL, 2);
    write32(&state, device, PIT0_TCTRL, 3);
    kinetis_k22_advance(device, 2);
    TEST_EXPECT(&state, read32(&state, device, PIT0_CVAL) == 0);
    TEST_EXPECT(&state, read32(&state, device, PIT0_TFLG) == 0);
    kinetis_k22_advance(device, 1);
    TEST_EXPECT(&state, read32(&state, device, PIT0_CVAL) == 2);
    TEST_EXPECT(&state, read32(&state, device, PIT0_TFLG) == 1);
    TEST_EXPECT(&state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), PIT0_IRQ));
    write32(&state, device, PIT0_TFLG, 1);
    TEST_EXPECT(&state, read32(&state, device, PIT0_TFLG) == 0);
    cortex_m4_set_irq(kinetis_k22_cpu(device), PIT0_IRQ, false);
    TEST_EXPECT(&state, !cortex_m4_get_irq_pending(kinetis_k22_cpu(device), PIT0_IRQ));
    write32(&state, device, PIT0_TCTRL, 0);
    kinetis_k22_advance(device, 100);
    TEST_EXPECT(&state, read32(&state, device, PIT0_CVAL) == 2);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
