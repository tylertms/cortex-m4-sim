#include "kinetis_k22.h"

#include <stdint.h>

#include "test.h"

enum {
    I2C0_C1 = 0x40066002u,
    I2C0_S = 0x40066003u,
    I2C0_D = 0x40066004u,
    I2C0_C1_RSTA = 0x42cc0048u,
    I2C0_C1_TXAK = 0x42cc004cu,
    I2C0_IRQ = 24,
    SIM_SCGC4 = 0x40048034u,
};

static uint8_t read8(TestState* state, KinetisK22* device, uint32_t address) {
    uint8_t value = 0;
    expect(state, kinetis_k22_read(device, address, &value, sizeof(value)),
           "kinetis_k22_read(device, address, &value, sizeof(value))");
    return value;
}

static void write8(TestState* state, KinetisK22* device, uint32_t address, uint8_t value) {
    expect(state, kinetis_k22_write(device, address, &value, sizeof(value)),
           "kinetis_k22_write(device, address, &value, sizeof(value))");
}

static void write32(TestState* state, KinetisK22* device, uint32_t address,
                    uint32_t value) {
    expect(state, kinetis_k22_write(device, address, &value, sizeof(value)),
           "kinetis_k22_write(device, address, &value, sizeof(value))");
}

static uint32_t read32(TestState* state, KinetisK22* device, uint32_t address) {
    uint32_t value = 0;
    expect(state, kinetis_k22_read(device, address, &value, sizeof(value)),
           "kinetis_k22_read(device, address, &value, sizeof(value))");
    return value;
}

static void expect_transfer(TestState* state, KinetisK22* device,
                            KinetisK22I2cTransferType type, uint8_t value) {
    KinetisK22I2cTransfer transfer = {0};
    expect(state, kinetis_k22_i2c0_transfer(device, &transfer),
           "kinetis_k22_i2c0_transfer(device, &transfer)");
    expect(state, transfer.type == type, "transfer.type == type");
    expect(state, transfer.value == value, "transfer.value == value");
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = kinetis_k22_create(kinetis_k22_default_configuration());
    expect(&state, device != NULL, "device != NULL");
    write32(&state, device, SIM_SCGC4, read32(&state, device, SIM_SCGC4) | (1u << 6));
    write8(&state, device, I2C0_C1, 0xf0u);
    expect_transfer(&state, device, KINETIS_K22_I2C_START, 0);
    write8(&state, device, I2C0_D, 0xa4u);
    expect_transfer(&state, device, KINETIS_K22_I2C_WRITE, 0xa4u);
    kinetis_k22_i2c0_acknowledge(device, true);
    expect(&state, (read8(&state, device, I2C0_S) & 3u) == 2u,
           "(read8(&state, device, I2C0_S) & 3u) == 2u");
    expect(&state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), I2C0_IRQ),
           "cortex_m4_get_irq_pending(kinetis_k22_cpu(device), I2C0_IRQ)");
    write8(&state, device, I2C0_S, 2u);
    expect(&state, (read8(&state, device, I2C0_S) & 2u) == 0,
           "(read8(&state, device, I2C0_S) & 2u) == 0");
    expect(&state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), I2C0_IRQ),
           "cortex_m4_get_irq_pending(kinetis_k22_cpu(device), I2C0_IRQ)");
    cortex_m4_set_irq(kinetis_k22_cpu(device), I2C0_IRQ, false);
    expect(&state, !cortex_m4_get_irq_pending(kinetis_k22_cpu(device), I2C0_IRQ),
           "!cortex_m4_get_irq_pending(kinetis_k22_cpu(device), I2C0_IRQ)");

    write8(&state, device, I2C0_C1, 0xf0u);
    write32(&state, device, I2C0_C1_RSTA, 1u);
    expect_transfer(&state, device, KINETIS_K22_I2C_REPEATED_START, 0);
    write8(&state, device, I2C0_C1, 0xe0u);
    write32(&state, device, I2C0_C1_TXAK, 1u);
    expect(&state, kinetis_k22_i2c0_receive(device, 0x5au),
           "kinetis_k22_i2c0_receive(device, 0x5au)");
    expect(&state, read8(&state, device, I2C0_D) == 0x5au,
           "read8(&state, device, I2C0_D) == 0x5au");
    expect_transfer(&state, device, KINETIS_K22_I2C_READ, 0);
    write8(&state, device, I2C0_C1, 0xc0u);
    expect_transfer(&state, device, KINETIS_K22_I2C_STOP, 0);
    KinetisK22I2cTransfer transfer = {0};
    expect(&state, !kinetis_k22_i2c0_transfer(device, &transfer),
           "!kinetis_k22_i2c0_transfer(device, &transfer)");

    const uint32_t vectors[] = {0x20001000u, 0x00000101u};
    const uint16_t stores[] = {0x7008u, 0x8008u};
    expect(&state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0, vectors, sizeof(vectors))");
    expect(&state, kinetis_k22_load(device, 0x100u, stores, sizeof(stores)),
           "kinetis_k22_load(device, 0x100u, stores, sizeof(stores))");
    expect(&state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    write32(&state, device, SIM_SCGC4, read32(&state, device, SIM_SCGC4) | (1u << 6));
    write8(&state, device, I2C0_C1, 0xf0u);
    expect_transfer(&state, device, KINETIS_K22_I2C_START, 0);
    cortex_m4_set_register(kinetis_k22_cpu(device), 0, 1u);
    cortex_m4_set_register(kinetis_k22_cpu(device), 1, I2C0_C1_RSTA);
    expect(&state, cortex_m4_step(kinetis_k22_cpu(device)).stop == CORTEX_M4_STOP_RUNNING,
           "cortex_m4_step(kinetis_k22_cpu(device)).stop == CORTEX_M4_STOP_RUNNING");
    expect_transfer(&state, device, KINETIS_K22_I2C_REPEATED_START, 0);
    cortex_m4_set_register(kinetis_k22_cpu(device), 1, I2C0_C1_TXAK);
    expect(&state, cortex_m4_step(kinetis_k22_cpu(device)).stop == CORTEX_M4_STOP_RUNNING,
           "cortex_m4_step(kinetis_k22_cpu(device)).stop == CORTEX_M4_STOP_RUNNING");
    expect(&state, (read8(&state, device, I2C0_C1) & 8u) != 0,
           "(read8(&state, device, I2C0_C1) & 8u) != 0");
    expect(&state, kinetis_k22_i2c0_lose_arbitration(device),
           "kinetis_k22_i2c0_lose_arbitration(device)");
    expect(&state, (read8(&state, device, I2C0_S) & 0x12u) == 0x12u,
           "(read8(&state, device, I2C0_S) & 0x12u) == 0x12u");
    expect(&state, (read8(&state, device, I2C0_C1) & 0x20u) == 0,
           "(read8(&state, device, I2C0_C1) & 0x20u) == 0");
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
