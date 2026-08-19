#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "kinetis_k22_internal.h"
#include "test.h"

enum {
    USB0 = 0x40072000u,
    CAN0 = 0x40024000u,
    I2S0 = 0x4002f000u,
    SPI0 = 0x4002c000u,
    I2C0 = 0x40066000u,
    I2C1 = 0x40067000u,
};

static KinetisK22* create_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.profile = KINETIS_K22_PROFILE_MK22FN1M012;
    configuration.package = KINETIS_K22_PACKAGE_LQ_144_LQFP;
    configuration.flash_size = 4096u;
    configuration.sram_size = 65536u;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(state, device != NULL);
    const uint32_t vectors[2] = {0x20001000u, 0x101u};
    TEST_EXPECT(state, kinetis_k22_load(device, 0u, vectors, sizeof(vectors)));
    TEST_EXPECT(state, kinetis_k22_reset(device));
    return device;
}

static void io_write(TestState* state, KinetisK22* device, uint32_t address, uint8_t size,
                     uint32_t value) {
    TEST_EXPECT(state, k22_io_write(&device->io, address, size, value));
}

static void serial_write(TestState* state, KinetisK22* device, uint32_t address,
                         uint8_t size, uint32_t value) {
    TEST_EXPECT(state, k22_serial_write(&device->serial, address, size, value));
}

static void test_data_api(TestState* state, KinetisK22* device) {
    TEST_EXPECT(state, kinetis_k22_set_adc_channel(device, 0u, 31u, 0x1234u));
    TEST_EXPECT(state, !kinetis_k22_set_adc_channel(device, 2u, 0u, 0u));
    TEST_EXPECT(state, !kinetis_k22_set_adc_channel(NULL, 0u, 0u, 0u));
    kinetis_k22_set_adc0_channel(device, 3u, 0x4567u);
    kinetis_k22_set_adc0_channel(NULL, 3u, 0u);
    TEST_EXPECT(state, kinetis_k22_set_cmp_input(device, 1u, 7u, 1u));
    TEST_EXPECT(state, !kinetis_k22_set_cmp_input(device, 3u, 0u, 0u));
    TEST_EXPECT(state, !kinetis_k22_set_cmp_input(NULL, 0u, 0u, 0u));
    TEST_EXPECT(state, kinetis_k22_set_lptmr_input(device, 2u, true));
    TEST_EXPECT(state, !kinetis_k22_set_lptmr_input(device, 3u, false));
    TEST_EXPECT(state, !kinetis_k22_set_lptmr_input(NULL, 0u, false));
    TEST_EXPECT(state, kinetis_k22_set_ftm_input(device, 0u, 0u, true));
    TEST_EXPECT(state, !kinetis_k22_set_ftm_input(device, 4u, 0u, false));
    TEST_EXPECT(state, !kinetis_k22_set_ftm_input(NULL, 0u, 0u, false));
    TEST_EXPECT(state, kinetis_k22_trigger_ftm_hardware(device, 0u, 0u));
    TEST_EXPECT(state, !kinetis_k22_trigger_ftm_hardware(device, 4u, 0u));
    TEST_EXPECT(state, !kinetis_k22_trigger_ftm_hardware(device, 0u, 3u));
    TEST_EXPECT(state, !kinetis_k22_trigger_ftm_hardware(NULL, 0u, 0u));
    bool ftm_output = true;
    TEST_EXPECT(state, kinetis_k22_get_ftm_output(device, 0u, 0u, &ftm_output));
    TEST_EXPECT(state, !ftm_output);
    TEST_EXPECT(state, !kinetis_k22_get_ftm_output(device, 4u, 0u, &ftm_output));
    TEST_EXPECT(state, !kinetis_k22_get_ftm_output(NULL, 0u, 0u, &ftm_output));
    uint16_t output = 0u;
    TEST_EXPECT(state, kinetis_k22_get_dac_output(device, 0u, &output));
    TEST_EXPECT(state, !kinetis_k22_get_dac_output(device, 2u, &output));
    TEST_EXPECT(state, !kinetis_k22_get_dac_output(NULL, 0u, &output));
    kinetis_k22_rng_seed(device, 0x12345678u);
    kinetis_k22_rng_seed(NULL, 0u);
}

static void test_serial_api(TestState* state, KinetisK22* device) {
    TEST_EXPECT(state,
                kinetis_k22_serial_receive(device, KINETIS_K22_SERIAL_UART1, 0x5au, 0u));
    TEST_EXPECT(state, !kinetis_k22_serial_receive(NULL, KINETIS_K22_SERIAL_UART1, 0u, 0u));
    uint16_t value = 0u;
    TEST_EXPECT(state,
                !kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART1, &value));
    TEST_EXPECT(state, !kinetis_k22_serial_transmit(
                           device, KINETIS_K22_SERIAL_ENDPOINT_COUNT, &value));

    TEST_EXPECT(state,
                k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_SPI0, true));
    serial_write(state, device, SPI0, 4u, 0u);
    serial_write(state, device, SPI0 + 0x34u, 4u, 0x98030055u);
    k22_serial_advance(&device->serial, 64u);
    KinetisK22SpiTransfer spi;
    TEST_EXPECT(state, kinetis_k22_spi_transfer(device, KINETIS_K22_SERIAL_SPI0, &spi));
    TEST_EXPECT(state, spi.data == 0x55u);
    TEST_EXPECT(state, spi.chip_selects == 3u);
    TEST_EXPECT(state, spi.clock_and_transfer_attributes == 1u);
    TEST_EXPECT(state, spi.continuous_chip_select);
    TEST_EXPECT(state, spi.end_of_queue);
    TEST_EXPECT(state, !kinetis_k22_spi_transfer(device, KINETIS_K22_SERIAL_UART0, &spi));
    TEST_EXPECT(state, !kinetis_k22_spi_transfer(device, KINETIS_K22_SERIAL_SPI0, NULL));

    TEST_EXPECT(state,
                k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_I2C0, true));
    TEST_EXPECT(state,
                k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_I2C1, true));
    serial_write(state, device, I2C0 + 2u, 1u, 0xf0u);
    serial_write(state, device, I2C1 + 2u, 1u, 0xf0u);
    serial_write(state, device, I2C0 + 4u, 1u, 0x52u);
    KinetisK22I2cTransfer i2c;
    TEST_EXPECT(state, kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C1, &i2c));
    TEST_EXPECT(state, i2c.type == KINETIS_K22_I2C_START);
    TEST_EXPECT(state, kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C0, &i2c));
    TEST_EXPECT(state, i2c.type == KINETIS_K22_I2C_START);
    TEST_EXPECT(state, kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C0, &i2c));
    TEST_EXPECT(state, i2c.type == KINETIS_K22_I2C_WRITE);
    TEST_EXPECT(state, i2c.value == 0x52u);
    TEST_EXPECT(state, !kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_UART0, &i2c));
    TEST_EXPECT(state, !kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C0, NULL));
    TEST_EXPECT(state, kinetis_k22_i2c_acknowledge(device, KINETIS_K22_SERIAL_I2C0, false));
    TEST_EXPECT(state,
                !kinetis_k22_i2c_acknowledge(device, KINETIS_K22_SERIAL_UART0, true));
    TEST_EXPECT(state, kinetis_k22_i2c_lose_arbitration(device, KINETIS_K22_SERIAL_I2C0));
    TEST_EXPECT(state, !kinetis_k22_i2c_lose_arbitration(device, KINETIS_K22_SERIAL_UART0));
    TEST_EXPECT(state, kinetis_k22_i2c_receive(device, KINETIS_K22_SERIAL_I2C0, 0x6bu));
    TEST_EXPECT(state, !kinetis_k22_i2c_receive(device, KINETIS_K22_SERIAL_UART0, 0u));
}

static void test_io_api(TestState* state, KinetisK22* device) {
    k22_io_set_clock(&device->io, K22_PERIPHERAL_USB0, true);
    io_write(state, device, USB0 + 0x84u, 1u, 1u << 3u);
    io_write(state, device, USB0 + 0x94u, 1u, 1u);
    TEST_EXPECT(state, kinetis_k22_usb_token(device, 3u, 0x69u, false));
    TEST_EXPECT(state, !kinetis_k22_usb_token(NULL, 0u, 0u, false));

    k22_io_set_clock(&device->io, K22_PERIPHERAL_CAN0, true);
    io_write(state, device, CAN0, 4u, 0x0fu);
    io_write(state, device, CAN0 + 0x10u, 4u, 0u);
    io_write(state, device, CAN0 + 0x28u, 4u, 1u);
    io_write(state, device, CAN0 + 0x80u, 4u, 4u << 24u);
    KinetisK22CanFrame frame = {0x123u, 2u, {1u, 2u}, false, false};
    TEST_EXPECT(state, kinetis_k22_can_receive(device, &frame));
    TEST_EXPECT(state, !kinetis_k22_can_receive(device, NULL));
    TEST_EXPECT(state, !kinetis_k22_can_receive(NULL, &frame));

    k22_io_set_clock(&device->io, K22_PERIPHERAL_I2S0, true);
    io_write(state, device, I2S0, 4u, UINT32_C(0x80000000));
    io_write(state, device, I2S0 + 0x80u, 4u, UINT32_C(0x80000000));
    io_write(state, device, I2S0 + 0x20u, 4u, 0x11223344u);
    uint32_t sample = 0u;
    TEST_EXPECT(state, kinetis_k22_i2s_transmit(device, &sample));
    TEST_EXPECT(state, sample == 0x11223344u);
    TEST_EXPECT(state, kinetis_k22_i2s_receive(device, 0x55667788u));
    TEST_EXPECT(state, !kinetis_k22_i2s_transmit(NULL, &sample));
    TEST_EXPECT(state, !kinetis_k22_i2s_receive(NULL, 0u));

    kinetis_k22_gpio_drive(device, 0u, 1u, true);
    kinetis_k22_gpio_release(device, 0u, 1u);
    kinetis_k22_gpio_drive(NULL, 0u, 0u, false);
    kinetis_k22_gpio_release(NULL, 0u, 0u);
}

static void test_guards(TestState* state, KinetisK22* device) {
    TEST_EXPECT(state, kinetis_k22_set_usb_charger(device, KINETIS_K22_USB_CHARGER_NONE));
    TEST_EXPECT(state,
                !kinetis_k22_set_usb_charger(device, (KinetisK22UsbCharger)UINT8_MAX));
    TEST_EXPECT(state, !kinetis_k22_set_usb_charger(NULL, KINETIS_K22_USB_CHARGER_NONE));
    KinetisK22Event event;
    while (kinetis_k22_next_event(device, &event)) {
    }
    TEST_EXPECT(state, !kinetis_k22_next_event(device, &event));
    TEST_EXPECT(state, !kinetis_k22_next_event(NULL, &event));
    TEST_EXPECT(state, !kinetis_k22_next_event(device, NULL));
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = create_device(&state);
    test_data_api(&state, device);
    test_serial_api(&state, device);
    test_io_api(&state, device);
    test_guards(&state, device);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
