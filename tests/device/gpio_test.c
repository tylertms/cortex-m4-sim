#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "test.h"

enum {
    GPIOA_PDOR = 0x400ff000u,
    GPIOA_PSOR = 0x400ff004u,
    GPIOA_PCOR = 0x400ff008u,
    GPIOA_PTOR = 0x400ff00cu,
    GPIOA_PDIR = 0x400ff010u,
    GPIOA_PDDR = 0x400ff014u,
    PORTA_PCR3 = 0x4004900cu,
    PORTA_IRQ = 59,
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
    write32(&state, device, GPIOA_PDDR, 1u << 3);
    write32(&state, device, GPIOA_PSOR, 1u << 3);
    TEST_EXPECT(&state, read32(&state, device, GPIOA_PDOR) == (1u << 3));
    TEST_EXPECT(&state, read32(&state, device, GPIOA_PDIR) == (1u << 3));
    write32(&state, device, GPIOA_PTOR, 1u << 3);
    TEST_EXPECT(&state, read32(&state, device, GPIOA_PDOR) == 0);
    write32(&state, device, GPIOA_PDOR, 1u << 3);
    write32(&state, device, GPIOA_PCOR, 1u << 3);
    TEST_EXPECT(&state, read32(&state, device, GPIOA_PDOR) == 0);

    write32(&state, device, GPIOA_PDDR, 0);
    write32(&state, device, PORTA_PCR3, 3u);
    TEST_EXPECT(&state, read32(&state, device, GPIOA_PDIR) == (1u << 3));
    write32(&state, device, PORTA_PCR3, 9u << 16);
    kinetis_k22_gpio_drive(device, 0, 3, false);
    kinetis_k22_gpio_drive(device, 0, 3, true);
    TEST_EXPECT(&state, (read32(&state, device, PORTA_PCR3) & (1u << 24)) != 0);
    TEST_EXPECT(&state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), PORTA_IRQ));
    write32(&state, device, PORTA_PCR3, 1u << 24);
    TEST_EXPECT(&state, (read32(&state, device, PORTA_PCR3) & (1u << 24)) == 0);

    KinetisK22* copy = kinetis_k22_create(kinetis_k22_default_configuration());
    TEST_EXPECT(&state, copy != NULL);
    TEST_EXPECT(&state, kinetis_k22_copy(copy, device));
    TEST_EXPECT(&state, read32(&state, copy, GPIOA_PDIR) == (1u << 3));
    kinetis_k22_gpio_release(copy, 0, 3);
    TEST_EXPECT(&state, read32(&state, copy, GPIOA_PDIR) == 0);
    TEST_EXPECT(&state, read32(&state, device, GPIOA_PDIR) == (1u << 3));
    kinetis_k22_destroy(copy);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
