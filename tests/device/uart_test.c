#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "test.h"

enum {
    UART1_C2 = 0x4006b003u,
    UART1_S1 = 0x4006b004u,
    UART1_C3 = 0x4006b006u,
    UART1_D = 0x4006b007u,
    UART1_IRQ = 33,
    UART1_ERROR_IRQ = 34,
};

static uint8_t read8(TestState* state, KinetisK22* device, uint32_t address) {
    uint8_t value = 0;
    TEST_EXPECT(state, kinetis_k22_read(device, address, &value, sizeof(value)));
    return value;
}

static void write8(TestState* state, KinetisK22* device, uint32_t address, uint8_t value) {
    TEST_EXPECT(state, kinetis_k22_write(device, address, &value, sizeof(value)));
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = kinetis_k22_create(kinetis_k22_default_configuration());
    TEST_EXPECT(&state, device != NULL);
    write8(&state, device, UART1_C2, 0x20u);
    write8(&state, device, UART1_C3, 0x08u);
    TEST_EXPECT(&state, kinetis_k22_uart1_receive(device, 0x5au, 0x08u));
    TEST_EXPECT(&state, (read8(&state, device, UART1_S1) & 0x20u) != 0);
    TEST_EXPECT(&state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), UART1_IRQ));
    TEST_EXPECT(&state,
                cortex_m4_get_irq_pending(kinetis_k22_cpu(device), UART1_ERROR_IRQ));
    TEST_EXPECT(&state, read8(&state, device, UART1_D) == 0x5au);
    TEST_EXPECT(&state, (read8(&state, device, UART1_S1) & 0x20u) == 0);
    write8(&state, device, UART1_D, 0xa5u);
    uint8_t output = 0;
    TEST_EXPECT(&state, kinetis_k22_uart1_transmit(device, &output));
    TEST_EXPECT(&state, output == 0xa5u);
    TEST_EXPECT(&state, !kinetis_k22_uart1_transmit(device, &output));
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
