#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "test.h"

enum {
    SPI0_SR = 0x4002c02cu,
    SPI0_RSER = 0x4002c030u,
    SPI0_PUSHR = 0x4002c034u,
    SPI0_POPR = 0x4002c038u,
    SPI0_IRQ = 26,
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
    write32(&state, device, SPI0_RSER, 1u << 17);
    TEST_EXPECT(&state, kinetis_k22_spi0_receive(device, 0x1234u));
    TEST_EXPECT(&state, (read32(&state, device, SPI0_SR) & (1u << 17)) != 0);
    TEST_EXPECT(&state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), SPI0_IRQ));
    TEST_EXPECT(&state, read32(&state, device, SPI0_POPR) == 0x1234u);
    TEST_EXPECT(&state, (read32(&state, device, SPI0_SR) & (1u << 17)) == 0);
    write32(&state, device, SPI0_PUSHR, 0xabcdu);
    uint16_t output = 0;
    TEST_EXPECT(&state, kinetis_k22_spi0_transmit(device, &output));
    TEST_EXPECT(&state, output == 0xabcdu);
    TEST_EXPECT(&state, !kinetis_k22_spi0_transmit(device, &output));
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
