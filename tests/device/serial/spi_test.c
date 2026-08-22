#include "kinetis_k22.h"

#include <stdint.h>

#include "test.h"

enum {
    SIM_SCGC6 = 0x4004803cu,
    SPI0_SR = 0x4002c02cu,
    SPI0_RSER = 0x4002c030u,
    SPI0_PUSHR = 0x4002c034u,
    SPI0_POPR = 0x4002c038u,
    SPI0_IRQ = 26,
};

static uint32_t read32(TestState* state, KinetisK22* device, uint32_t address) {
    uint32_t value = 0;
    expect(state, kinetis_k22_read(device, address, &value, sizeof(value)),
           "kinetis_k22_read(device, address, &value, sizeof(value))");
    return value;
}

static void write32(TestState* state, KinetisK22* device, uint32_t address, uint32_t value) {
    expect(state, kinetis_k22_write(device, address, &value, sizeof(value)),
           "kinetis_k22_write(device, address, &value, sizeof(value))");
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = kinetis_k22_create(kinetis_k22_default_configuration());
    expect(&state, device != NULL, "device != NULL");
    write32(&state, device, SIM_SCGC6, read32(&state, device, SIM_SCGC6) | (1u << 12));
    write32(&state, device, SPI0_RSER, 1u << 17);
    expect(&state, kinetis_k22_spi0_receive(device, 0x1234u),
           "kinetis_k22_spi0_receive(device, 0x1234u)");
    expect(&state, (read32(&state, device, SPI0_SR) & (1u << 17)) != 0,
           "(read32(&state, device, SPI0_SR) & (1u << 17)) != 0");
    expect(&state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), SPI0_IRQ),
           "cortex_m4_get_irq_pending(kinetis_k22_cpu(device), SPI0_IRQ)");
    expect(&state, read32(&state, device, SPI0_POPR) == 0x1234u,
           "read32(&state, device, SPI0_POPR) == 0x1234u");
    expect(&state, (read32(&state, device, SPI0_SR) & (1u << 17)) == 0,
           "(read32(&state, device, SPI0_SR) & (1u << 17)) == 0");
    write32(&state, device, SPI0_PUSHR, 0xabcdu);
    uint16_t output = 0;
    expect(&state, kinetis_k22_spi0_transmit(device, &output),
           "kinetis_k22_spi0_transmit(device, &output)");
    expect(&state, output == 0xabcdu, "output == 0xabcdu");
    expect(&state, !kinetis_k22_spi0_transmit(device, &output),
           "!kinetis_k22_spi0_transmit(device, &output)");
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
