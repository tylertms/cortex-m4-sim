#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "test.h"

enum {
    DMA_SERQ = 0x4000801bu,
    DMA_TCD0 = 0x40009000u,
    DMAMUX_CHCFG0 = 0x40021000u,
    UART1_D = 0x4006b007u,
};

static void write(TestState* state, KinetisK22* device, uint32_t address, const void* value,
                  size_t size) {
    TEST_EXPECT(state, kinetis_k22_write(device, address, value, size));
}

static uint8_t read8(TestState* state, KinetisK22* device, uint32_t address) {
    uint8_t value = 0;
    TEST_EXPECT(state, kinetis_k22_read(device, address, &value, sizeof(value)));
    return value;
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = kinetis_k22_create(kinetis_k22_default_configuration());
    TEST_EXPECT(&state, device != NULL);
    const uint32_t source = 0x20000000u;
    const uint32_t destination = UART1_D;
    const int16_t source_offset = 1;
    const int16_t destination_offset = 0;
    const uint32_t minor_count = 1;
    const uint16_t iterations = 1;
    const uint16_t control = 1u << 3;
    const uint8_t mux = 0x80u | 5u;
    const uint8_t channel = 0;
    const uint8_t payload = 0x6du;
    write(&state, device, source, &payload, sizeof(payload));
    write(&state, device, DMA_TCD0, &source, sizeof(source));
    write(&state, device, DMA_TCD0 + 4, &source_offset, sizeof(source_offset));
    write(&state, device, DMA_TCD0 + 8, &minor_count, sizeof(minor_count));
    write(&state, device, DMA_TCD0 + 0x10, &destination, sizeof(destination));
    write(&state, device, DMA_TCD0 + 0x14, &destination_offset, sizeof(destination_offset));
    write(&state, device, DMA_TCD0 + 0x16, &iterations, sizeof(iterations));
    write(&state, device, DMA_TCD0 + 0x1c, &control, sizeof(control));
    write(&state, device, DMAMUX_CHCFG0, &mux, sizeof(mux));
    write(&state, device, DMA_SERQ, &channel, sizeof(channel));
    const uint8_t trigger = 0;
    write(&state, device, UART1_D, &trigger, sizeof(trigger));
    uint8_t output = 0;
    TEST_EXPECT(&state, kinetis_k22_uart1_transmit(device, &output));
    TEST_EXPECT(&state, output == 0);
    TEST_EXPECT(&state, kinetis_k22_uart1_transmit(device, &output));
    TEST_EXPECT(&state, output == payload);
    TEST_EXPECT(&state, !kinetis_k22_uart1_transmit(device, &output));
    TEST_EXPECT(&state, read8(&state, device, DMA_TCD0 + 0x16) == 0);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
