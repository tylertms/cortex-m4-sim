#include "kinetis_k22.h"

#include <stdint.h>

#include "k22_test.h"
#include "test.h"

enum {
    DMA_SERQ = 0x4000801bu,
    DMA_TCD0 = 0x40009000u,
    DMAMUX_CHCFG0 = 0x40021000u,
    SIM_SCGC4 = 0x40048034u,
    SIM_SCGC6 = 0x4004803cu,
    SIM_SCGC7 = 0x40048040u,
    UART1_C2 = 0x4006b003u,
    UART1_C5 = 0x4006b00bu,
    UART1_D = 0x4006b007u,
};

static void write(TestState* state, KinetisK22* device, uint32_t address, const void* value,
                  size_t size) {
    expect(state, kinetis_k22_write(device, address, value, size),
           "kinetis_k22_write(device, address, value, size)");
}

static uint8_t read8(TestState* state, KinetisK22* device, uint32_t address) {
    uint8_t value = 0;
    expect(state, kinetis_k22_read(device, address, &value, sizeof(value)),
           "kinetis_k22_read(device, address, &value, sizeof(value))");
    return value;
}

static uint32_t read32(TestState* state, KinetisK22* device, uint32_t address) {
    uint32_t value = 0;
    expect(state, kinetis_k22_read(device, address, &value, sizeof(value)),
           "kinetis_k22_read(device, address, &value, sizeof(value))");
    return value;
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = kinetis_k22_create(kinetis_k22_default_configuration());
    expect(&state, device != NULL, "device != NULL");
    expect(&state, k22_test_disable_watchdog(device), "k22_test_disable_watchdog(device)");
    const uint32_t scgc7 = read32(&state, device, SIM_SCGC7) | (1u << 1);
    const uint32_t scgc6 = read32(&state, device, SIM_SCGC6) | (1u << 1);
    const uint32_t scgc4 = read32(&state, device, SIM_SCGC4) | (1u << 11);
    write(&state, device, SIM_SCGC7, &scgc7, sizeof(scgc7));
    write(&state, device, SIM_SCGC6, &scgc6, sizeof(scgc6));
    write(&state, device, SIM_SCGC4, &scgc4, sizeof(scgc4));
    const uint8_t transmitter_enable = 0x08u;
    const uint8_t transmit_dma_enable = 0x80u;
    write(&state, device, UART1_C2, &transmitter_enable, sizeof(transmitter_enable));
    write(&state, device, UART1_C5, &transmit_dma_enable, sizeof(transmit_dma_enable));
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
    expect(
        &state,
        cortex_m4_write_memory(kinetis_k22_cpu(device), DMA_SERQ, sizeof(channel), channel),
        "cortex_m4_write_memory(kinetis_k22_cpu(device), DMA_SERQ, sizeof(channel), "
        "channel)");
    uint16_t output = 0;
    expect(&state, !kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART1, &output),
           "!kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART1, &output)");
    kinetis_k22_advance(device, UINT32_MAX);
    expect(&state, kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART1, &output),
           "kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART1, &output)");
    expect(&state, output == payload, "output == payload");
    expect(&state, !kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART1, &output),
           "!kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART1, &output)");
    expect(&state, read8(&state, device, DMA_TCD0 + 0x16) == 0,
           "read8(&state, device, DMA_TCD0 + 0x16) == 0");
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
