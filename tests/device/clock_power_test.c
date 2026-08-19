#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "test.h"

enum {
    LPTMR0_CSR = 0x40040000u,
    MCG_C1 = 0x40064000u,
    MCG_C6 = 0x40064005u,
    MCG_S = 0x40064006u,
    SMC_PMPROT = 0x4007e000u,
    SMC_PMCTRL = 0x4007e001u,
    SMC_PMSTAT = 0x4007e003u,
    SIM_SCGC5 = 0x40048038u,
};

static void write_value(TestState* state, KinetisK22* device, uint32_t address,
                        uint8_t size, uint32_t value) {
    TEST_EXPECT(state, kinetis_k22_write(device, address, &value, size));
}

static uint32_t read_value(TestState* state, KinetisK22* device, uint32_t address,
                           uint8_t size) {
    uint32_t value = 0;
    TEST_EXPECT(state, kinetis_k22_read(device, address, &value, size));
    return value;
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = kinetis_k22_create(kinetis_k22_default_configuration());
    TEST_EXPECT(&state, device != NULL);

    write_value(&state, device, SMC_PMPROT, 1, 0x02u);
    write_value(&state, device, SMC_PMCTRL, 1, 0x60u);
    TEST_EXPECT(&state, read_value(&state, device, SMC_PMSTAT, 1) == 0x80u);
    write_value(&state, device, SMC_PMCTRL, 1, 0);
    TEST_EXPECT(&state, read_value(&state, device, SMC_PMSTAT, 1) == 1);

    write_value(&state, device, MCG_C1, 1, 0x32u);
    TEST_EXPECT(&state, (read_value(&state, device, MCG_S, 1) & 0x1cu) == 0);
    write_value(&state, device, MCG_C6, 1, 0);
    TEST_EXPECT(&state, (read_value(&state, device, MCG_S, 1) & 0x1cu) == 0);

    write_value(&state, device, SIM_SCGC5, 4,
                read_value(&state, device, SIM_SCGC5, 4) | 1u);
    write_value(&state, device, LPTMR0_CSR, 4, 1);
    TEST_EXPECT(&state, read_value(&state, device, LPTMR0_CSR, 4) == 1u);
    write_value(&state, device, LPTMR0_CSR, 4, 0);
    TEST_EXPECT(&state, read_value(&state, device, LPTMR0_CSR, 4) == 0);

    write_value(&state, device, SIM_SCGC5, 4, 0xa55ac33cu);
    TEST_EXPECT(&state, read_value(&state, device, SIM_SCGC5, 4) == 0x00040382u);

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
