#include "kinetis_k22.h"

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
    UART0 = 0x4006a000u,
};

static KinetisK22* create_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.profile = KINETIS_K22_PROFILE_MK22FN1M012;
    configuration.package = KINETIS_K22_PACKAGE_LQ_144_LQFP;
    configuration.flash_size = 4096u;
    configuration.sram_size = 65536u;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(state, device != NULL, "device != NULL");
    const uint32_t vectors[2] = {0x20001000u, 0x101u};
    expect(state, kinetis_k22_load(device, 0u, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0u, vectors, sizeof(vectors))");
    expect(state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    return device;
}

static void io_write(TestState* state, KinetisK22* device, uint32_t address, uint8_t size,
                     uint32_t value) {
    expect(state, k22_io_write(&device->io, address, size, value),
           "k22_io_write(&device->io, address, size, value)");
}

static void serial_write(TestState* state, KinetisK22* device, uint32_t address,
                         uint8_t size, uint32_t value) {
    expect(state, k22_serial_write(&device->serial, address, size, value),
           "k22_serial_write(&device->serial, address, size, value)");
}

static void test_data_api(TestState* state, KinetisK22* device) {
    expect(state, kinetis_k22_set_adc_channel(device, 0u, 31u, 0x1234u),
           "kinetis_k22_set_adc_channel(device, 0u, 31u, 0x1234u)");
    expect(state, !kinetis_k22_set_adc_channel(device, 2u, 0u, 0u),
           "!kinetis_k22_set_adc_channel(device, 2u, 0u, 0u)");
    expect(state, !kinetis_k22_set_adc_channel(NULL, 0u, 0u, 0u),
           "!kinetis_k22_set_adc_channel(NULL, 0u, 0u, 0u)");
    kinetis_k22_set_adc0_channel(device, 3u, 0x4567u);
    kinetis_k22_set_adc0_channel(NULL, 3u, 0u);
    expect(state, kinetis_k22_set_cmp_input(device, 1u, 7u, 1u),
           "kinetis_k22_set_cmp_input(device, 1u, 7u, 1u)");
    expect(state, !kinetis_k22_set_cmp_input(device, 3u, 0u, 0u),
           "!kinetis_k22_set_cmp_input(device, 3u, 0u, 0u)");
    expect(state, !kinetis_k22_set_cmp_input(NULL, 0u, 0u, 0u),
           "!kinetis_k22_set_cmp_input(NULL, 0u, 0u, 0u)");
    expect(state, kinetis_k22_set_lptmr_input(device, 2u, true),
           "kinetis_k22_set_lptmr_input(device, 2u, true)");
    expect(state, !kinetis_k22_set_lptmr_input(device, 3u, false),
           "!kinetis_k22_set_lptmr_input(device, 3u, false)");
    expect(state, !kinetis_k22_set_lptmr_input(NULL, 0u, false),
           "!kinetis_k22_set_lptmr_input(NULL, 0u, false)");
    expect(state, kinetis_k22_trigger_low_voltage_warning(device),
           "kinetis_k22_trigger_low_voltage_warning(device)");
    expect(state, kinetis_k22_trigger_low_voltage_detect(device),
           "kinetis_k22_trigger_low_voltage_detect(device)");
    expect(state, !kinetis_k22_trigger_low_voltage_warning(NULL),
           "!kinetis_k22_trigger_low_voltage_warning(NULL)");
    expect(state, !kinetis_k22_trigger_low_voltage_detect(NULL),
           "!kinetis_k22_trigger_low_voltage_detect(NULL)");
    expect(state, kinetis_k22_set_ewm_input(device, true),
           "kinetis_k22_set_ewm_input(device, true)");
    expect(state, kinetis_k22_ewm_output(device), "kinetis_k22_ewm_output(device)");
    expect(state, !kinetis_k22_set_ewm_input(NULL, false),
           "!kinetis_k22_set_ewm_input(NULL, false)");
    expect(state, !kinetis_k22_ewm_output(NULL), "!kinetis_k22_ewm_output(NULL)");
    expect(state, kinetis_k22_set_llwu_pin(device, 0u, true),
           "kinetis_k22_set_llwu_pin(device, 0u, true)");
    expect(state, !kinetis_k22_set_llwu_pin(device, 16u, false),
           "!kinetis_k22_set_llwu_pin(device, 16u, false)");
    expect(state, !kinetis_k22_set_llwu_pin(NULL, 0u, false),
           "!kinetis_k22_set_llwu_pin(NULL, 0u, false)");
    expect(state, kinetis_k22_trigger_llwu_module(device, 0u),
           "kinetis_k22_trigger_llwu_module(device, 0u)");
    expect(state, !kinetis_k22_trigger_llwu_module(device, 8u),
           "!kinetis_k22_trigger_llwu_module(device, 8u)");
    expect(state, !kinetis_k22_trigger_llwu_module(NULL, 0u),
           "!kinetis_k22_trigger_llwu_module(NULL, 0u)");
    expect(state, kinetis_k22_set_ftm_input(device, 0u, 0u, true),
           "kinetis_k22_set_ftm_input(device, 0u, 0u, true)");
    expect(state, !kinetis_k22_set_ftm_input(device, 4u, 0u, false),
           "!kinetis_k22_set_ftm_input(device, 4u, 0u, false)");
    expect(state, !kinetis_k22_set_ftm_input(NULL, 0u, 0u, false),
           "!kinetis_k22_set_ftm_input(NULL, 0u, 0u, false)");
    expect(state, kinetis_k22_set_ftm_fault(device, 0u, 0u, true),
           "kinetis_k22_set_ftm_fault(device, 0u, 0u, true)");
    expect(state, !kinetis_k22_set_ftm_fault(device, 4u, 0u, false),
           "!kinetis_k22_set_ftm_fault(device, 4u, 0u, false)");
    expect(state, !kinetis_k22_set_ftm_fault(device, 0u, 4u, false),
           "!kinetis_k22_set_ftm_fault(device, 0u, 4u, false)");
    expect(state, !kinetis_k22_set_ftm_fault(NULL, 0u, 0u, false),
           "!kinetis_k22_set_ftm_fault(NULL, 0u, 0u, false)");
    expect(state, kinetis_k22_trigger_ftm_hardware(device, 0u, 0u),
           "kinetis_k22_trigger_ftm_hardware(device, 0u, 0u)");
    expect(state, !kinetis_k22_trigger_ftm_hardware(device, 4u, 0u),
           "!kinetis_k22_trigger_ftm_hardware(device, 4u, 0u)");
    expect(state, !kinetis_k22_trigger_ftm_hardware(device, 0u, 3u),
           "!kinetis_k22_trigger_ftm_hardware(device, 0u, 3u)");
    expect(state, !kinetis_k22_trigger_ftm_hardware(NULL, 0u, 0u),
           "!kinetis_k22_trigger_ftm_hardware(NULL, 0u, 0u)");
    bool ftm_output = true;
    expect(state, kinetis_k22_get_ftm_output(device, 0u, 0u, &ftm_output),
           "kinetis_k22_get_ftm_output(device, 0u, 0u, &ftm_output)");
    expect(state, !ftm_output, "!ftm_output");
    expect(state, !kinetis_k22_get_ftm_output(device, 4u, 0u, &ftm_output),
           "!kinetis_k22_get_ftm_output(device, 4u, 0u, &ftm_output)");
    expect(state, !kinetis_k22_get_ftm_output(NULL, 0u, 0u, &ftm_output),
           "!kinetis_k22_get_ftm_output(NULL, 0u, 0u, &ftm_output)");
    uint16_t output = 0u;
    expect(state, kinetis_k22_get_dac_output(device, 0u, &output),
           "kinetis_k22_get_dac_output(device, 0u, &output)");
    expect(state, !kinetis_k22_get_dac_output(device, 2u, &output),
           "!kinetis_k22_get_dac_output(device, 2u, &output)");
    expect(state, !kinetis_k22_get_dac_output(NULL, 0u, &output),
           "!kinetis_k22_get_dac_output(NULL, 0u, &output)");
    kinetis_k22_rng_seed(device, 0x12345678u);
    kinetis_k22_rng_seed(NULL, 0u);
}

static void test_serial_api(TestState* state, KinetisK22* device) {
    expect(state, k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_UART0, true),
           "k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_UART0, true)");
    serial_write(state, device, UART0 + 3u, 1u, 0x08u);
    serial_write(state, device, UART0 + 7u, 1u, 0x44u);
    expect(state, kinetis_k22_serial_receive(device, KINETIS_K22_SERIAL_UART1, 0x5au, 0u),
           "kinetis_k22_serial_receive(device, KINETIS_K22_SERIAL_UART1, 0x5au, 0u)");
    expect(state, !kinetis_k22_serial_receive(NULL, KINETIS_K22_SERIAL_UART1, 0u, 0u),
           "!kinetis_k22_serial_receive(NULL, KINETIS_K22_SERIAL_UART1, 0u, 0u)");
    uint16_t value = 0u;
    expect(state, !kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART1, &value),
           "!kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART1, &value)");
    expect(state, !kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART0, &value),
           "!kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART0, &value)");
    expect(state, k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_UART0, true),
           "k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_UART0, true)");
    k22_serial_advance_endpoint(&device->serial, K22_SERIAL_UART0);
    expect(state, kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART0, &value),
           "kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART0, &value)");
    expect(state, value == 0x44u, "value == 0x44u");
    expect(
        state,
        !kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_ENDPOINT_COUNT, &value),
        "!kinetis_k22_serial_transmit( device, KINETIS_K22_SERIAL_ENDPOINT_COUNT, &value)");

    expect(state, k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_SPI0, true),
           "k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_SPI0, true)");
    serial_write(state, device, SPI0, 4u, 0u);
    serial_write(state, device, SPI0 + 0x34u, 4u, 0x98030055u);
    k22_serial_advance(&device->serial, 64u);
    KinetisK22SpiTransfer spi;
    expect(state, kinetis_k22_spi_transfer(device, KINETIS_K22_SERIAL_SPI0, &spi),
           "kinetis_k22_spi_transfer(device, KINETIS_K22_SERIAL_SPI0, &spi)");
    expect(state, spi.data == 0x55u, "spi.data == 0x55u");
    expect(state, spi.chip_selects == 3u, "spi.chip_selects == 3u");
    expect(state, spi.clock_and_transfer_attributes == 1u,
           "spi.clock_and_transfer_attributes == 1u");
    expect(state, spi.continuous_chip_select, "spi.continuous_chip_select");
    expect(state, spi.end_of_queue, "spi.end_of_queue");
    expect(state, !kinetis_k22_spi_transfer(device, KINETIS_K22_SERIAL_UART0, &spi),
           "!kinetis_k22_spi_transfer(device, KINETIS_K22_SERIAL_UART0, &spi)");
    expect(state, !kinetis_k22_spi_transfer(device, KINETIS_K22_SERIAL_SPI0, NULL),
           "!kinetis_k22_spi_transfer(device, KINETIS_K22_SERIAL_SPI0, NULL)");

    expect(state, k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_I2C0, true),
           "k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_I2C0, true)");
    expect(state, k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_I2C1, true),
           "k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_I2C1, true)");
    serial_write(state, device, I2C0 + 2u, 1u, 0xf0u);
    serial_write(state, device, I2C1 + 2u, 1u, 0xf0u);
    serial_write(state, device, I2C0 + 4u, 1u, 0x52u);
    KinetisK22I2cTransfer i2c;
    expect(state, kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C1, &i2c),
           "kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C1, &i2c)");
    expect(state, i2c.type == KINETIS_K22_I2C_START, "i2c.type == KINETIS_K22_I2C_START");
    expect(state, kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C0, &i2c),
           "kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C0, &i2c)");
    expect(state, i2c.type == KINETIS_K22_I2C_START, "i2c.type == KINETIS_K22_I2C_START");
    expect(state, kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C0, &i2c),
           "kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C0, &i2c)");
    expect(state, i2c.type == KINETIS_K22_I2C_WRITE, "i2c.type == KINETIS_K22_I2C_WRITE");
    expect(state, i2c.value == 0x52u, "i2c.value == 0x52u");
    expect(state, !kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_UART0, &i2c),
           "!kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_UART0, &i2c)");
    expect(state, !kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C0, NULL),
           "!kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C0, NULL)");
    expect(state, kinetis_k22_i2c_acknowledge(device, KINETIS_K22_SERIAL_I2C0, false),
           "kinetis_k22_i2c_acknowledge(device, KINETIS_K22_SERIAL_I2C0, false)");
    expect(state, !kinetis_k22_i2c_acknowledge(device, KINETIS_K22_SERIAL_UART0, true),
           "!kinetis_k22_i2c_acknowledge(device, KINETIS_K22_SERIAL_UART0, true)");
    expect(state, kinetis_k22_i2c_lose_arbitration(device, KINETIS_K22_SERIAL_I2C0),
           "kinetis_k22_i2c_lose_arbitration(device, KINETIS_K22_SERIAL_I2C0)");
    expect(state, !kinetis_k22_i2c_lose_arbitration(device, KINETIS_K22_SERIAL_UART0),
           "!kinetis_k22_i2c_lose_arbitration(device, KINETIS_K22_SERIAL_UART0)");
    expect(state, kinetis_k22_i2c_receive(device, KINETIS_K22_SERIAL_I2C0, 0x6bu),
           "kinetis_k22_i2c_receive(device, KINETIS_K22_SERIAL_I2C0, 0x6bu)");
    expect(state, !kinetis_k22_i2c_receive(device, KINETIS_K22_SERIAL_UART0, 0u),
           "!kinetis_k22_i2c_receive(device, KINETIS_K22_SERIAL_UART0, 0u)");
}

static void test_io_api(TestState* state, KinetisK22* device) {
    k22_io_set_clock(&device->io, K22_PERIPHERAL_USB0, true);
    io_write(state, device, USB0 + 0x84u, 1u, 1u << 3u);
    io_write(state, device, USB0 + 0x94u, 1u, 1u);
    expect(state, kinetis_k22_usb_token(device, 3u, 0x69u, false),
           "kinetis_k22_usb_token(device, 3u, 0x69u, false)");
    expect(state, !kinetis_k22_usb_token(NULL, 0u, 0u, false),
           "!kinetis_k22_usb_token(NULL, 0u, 0u, false)");

    k22_io_set_clock(&device->io, K22_PERIPHERAL_CAN0, true);
    io_write(state, device, CAN0, 4u, 0x0fu);
    io_write(state, device, CAN0 + 0x10u, 4u, 0u);
    io_write(state, device, CAN0 + 0x28u, 4u, 1u);
    io_write(state, device, CAN0 + 0x80u, 4u, 4u << 24u);
    KinetisK22CanFrame frame = {0x123u, 2u, {1u, 2u}, false, false};
    expect(state, kinetis_k22_can_receive(device, &frame),
           "kinetis_k22_can_receive(device, &frame)");
    expect(state, !kinetis_k22_can_receive(device, NULL),
           "!kinetis_k22_can_receive(device, NULL)");
    expect(state, !kinetis_k22_can_receive(NULL, &frame),
           "!kinetis_k22_can_receive(NULL, &frame)");

    k22_io_set_clock(&device->io, K22_PERIPHERAL_I2S0, true);
    io_write(state, device, I2S0, 4u, UINT32_C(0x80000000));
    io_write(state, device, I2S0 + 0x80u, 4u, UINT32_C(0x80000000));
    io_write(state, device, I2S0 + 0x20u, 4u, 0x11223344u);
    uint32_t sample = 0u;
    expect(state, kinetis_k22_i2s_transmit(device, &sample),
           "kinetis_k22_i2s_transmit(device, &sample)");
    expect(state, sample == 0x11223344u, "sample == 0x11223344u");
    expect(state, kinetis_k22_i2s_receive(device, 0x55667788u),
           "kinetis_k22_i2s_receive(device, 0x55667788u)");
    expect(state, !kinetis_k22_i2s_transmit(NULL, &sample),
           "!kinetis_k22_i2s_transmit(NULL, &sample)");
    expect(state, !kinetis_k22_i2s_receive(NULL, 0u), "!kinetis_k22_i2s_receive(NULL, 0u)");

    kinetis_k22_gpio_drive(device, 0u, 1u, true);
    kinetis_k22_gpio_release(device, 0u, 1u);
    kinetis_k22_gpio_drive(NULL, 0u, 0u, false);
    kinetis_k22_gpio_release(NULL, 0u, 0u);
}

static void test_guards(TestState* state, KinetisK22* device) {
    expect(state, kinetis_k22_set_usb_charger(device, KINETIS_K22_USB_CHARGER_NONE),
           "kinetis_k22_set_usb_charger(device, KINETIS_K22_USB_CHARGER_NONE)");
    expect(state, kinetis_k22_set_usb_charger(device, KINETIS_K22_USB_CHARGER_ERROR),
           "kinetis_k22_set_usb_charger(device, KINETIS_K22_USB_CHARGER_ERROR)");
    expect(state, !kinetis_k22_set_usb_charger(device, (KinetisK22UsbCharger)UINT8_MAX),
           "!kinetis_k22_set_usb_charger(device, (KinetisK22UsbCharger)UINT8_MAX)");
    expect(state, !kinetis_k22_set_usb_charger(NULL, KINETIS_K22_USB_CHARGER_NONE),
           "!kinetis_k22_set_usb_charger(NULL, KINETIS_K22_USB_CHARGER_NONE)");
    expect(state, kinetis_k22_set_usb_pullup(device, true),
           "kinetis_k22_set_usb_pullup(device, true)");
    expect(state, kinetis_k22_set_usb_pullup(device, false),
           "kinetis_k22_set_usb_pullup(device, false)");
    expect(state, !kinetis_k22_set_usb_pullup(NULL, true),
           "!kinetis_k22_set_usb_pullup(NULL, true)");
    KinetisK22Event event;
    while (kinetis_k22_next_event(device, &event)) {
    }
    expect(state, !kinetis_k22_next_event(device, &event),
           "!kinetis_k22_next_event(device, &event)");
    expect(state, !kinetis_k22_next_event(NULL, &event),
           "!kinetis_k22_next_event(NULL, &event)");
    expect(state, !kinetis_k22_next_event(device, NULL),
           "!kinetis_k22_next_event(device, NULL)");
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
