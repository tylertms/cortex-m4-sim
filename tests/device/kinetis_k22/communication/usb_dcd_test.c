#include "device/kinetis_k22/communication/usb_dcd.h"

#include <stdint.h>
#include <string.h>

#include "test.h"

enum {
    CONTROL = 0x40035000u,
    CLOCK = 0x40035004u,
    STATUS = 0x40035008u,
    TIMER0 = 0x40035010u,
    TIMER1 = 0x40035014u,
    TIMER2 = 0x40035018u,
    IF = 1u << 8u,
    IE = 1u << 16u,
    BC12 = 1u << 17u,
    START = 1u << 24u,
    SR = 1u << 25u,
    ACTIVE = 1u << 22u,
};

static uint32_t read_value(TestState* state, K22UsbDcd* usbdcd, uint32_t address) {
    uint32_t value = UINT32_MAX;
    expect(state, k22_usbdcd_read(usbdcd, address, 4u, &value),
           "k22_usbdcd_read(usbdcd, address, 4u, &value)");
    return value;
}

static void write_value(TestState* state, K22UsbDcd* usbdcd, uint32_t address, uint32_t value) {
    expect(state, k22_usbdcd_write(usbdcd, address, 4u, value),
           "k22_usbdcd_write(usbdcd, address, 4u, value)");
}

static void configure(TestState* state, K22UsbDcd* usbdcd, bool bc12) {
    write_value(state, usbdcd, CLOCK, 4u);
    write_value(state, usbdcd, TIMER0, 2u << 16u);
    write_value(state, usbdcd, TIMER1, (2u << 16u) | 3u);
    write_value(state, usbdcd, TIMER2, (2u << 16u) | 2u);
    write_value(state, usbdcd, CONTROL, IE | (bc12 ? BC12 : 0u));
}

static void test_reset_and_access(TestState* state) {
    K22UsbDcd usbdcd;
    k22_usbdcd_reset(&usbdcd);
    expect(state, read_value(state, &usbdcd, CONTROL) == IE,
           "read_value(state, &usbdcd, CONTROL) == IE");
    expect(state, read_value(state, &usbdcd, CLOCK) == 0xc1u,
           "read_value(state, &usbdcd, CLOCK) == 0xc1u");
    expect(state, read_value(state, &usbdcd, STATUS) == 0u,
           "read_value(state, &usbdcd, STATUS) == 0u");
    expect(state, read_value(state, &usbdcd, TIMER0) == 0x00100000u,
           "read_value(state, &usbdcd, TIMER0) == 0x00100000u");
    expect(state, read_value(state, &usbdcd, TIMER1) == 0x000a0028u,
           "read_value(state, &usbdcd, TIMER1) == 0x000a0028u");
    expect(state, read_value(state, &usbdcd, TIMER2) == 0x00280001u,
           "read_value(state, &usbdcd, TIMER2) == 0x00280001u");
    uint32_t value = 0u;
    expect(state, !k22_usbdcd_read(NULL, CONTROL, 4u, &value),
           "!k22_usbdcd_read(NULL, CONTROL, 4u, &value)");
    expect(state, !k22_usbdcd_read(&usbdcd, CONTROL, 2u, &value),
           "!k22_usbdcd_read(&usbdcd, CONTROL, 2u, &value)");
    expect(state, !k22_usbdcd_read(&usbdcd, CONTROL + 1u, 4u, &value),
           "!k22_usbdcd_read(&usbdcd, CONTROL + 1u, 4u, &value)");
    expect(state, !k22_usbdcd_read(&usbdcd, CONTROL + 0x0cu, 4u, &value),
           "unimplemented USB DCD register is rejected");
    expect(state, !k22_usbdcd_read(&usbdcd, CONTROL, 4u, NULL),
           "!k22_usbdcd_read(&usbdcd, CONTROL, 4u, NULL)");
    expect(state, !k22_usbdcd_write(NULL, CONTROL, 4u, 0u),
           "!k22_usbdcd_write(NULL, CONTROL, 4u, 0u)");
    expect(state, !k22_usbdcd_write(&usbdcd, STATUS, 4u, 0u),
           "!k22_usbdcd_write(&usbdcd, STATUS, 4u, 0u)");
    expect(state, !k22_usbdcd_set_charger(NULL, KINETIS_K22_USB_CHARGER_NONE),
           "!k22_usbdcd_set_charger(NULL, KINETIS_K22_USB_CHARGER_NONE)");
    expect(state, !k22_usbdcd_set_charger(&usbdcd, (KinetisK22UsbCharger)UINT8_MAX),
           "!k22_usbdcd_set_charger(&usbdcd, (KinetisK22UsbCharger)UINT8_MAX)");
    expect(state, !k22_usbdcd_set_pullup(NULL, true), "!k22_usbdcd_set_pullup(NULL, true)");
    expect(state, !k22_usbdcd_irq(NULL), "!k22_usbdcd_irq(NULL)");
    k22_usbdcd_reset(NULL);
    write_value(state, &usbdcd, CLOCK, 0u);
    write_value(state, &usbdcd, CONTROL, IE | START);
    k22_usbdcd_advance(&usbdcd, 1u);
    k22_usbdcd_reset(&usbdcd);
    k22_usbdcd_advance(&usbdcd, 1u);
}

static void test_standard_host(TestState* state) {
    K22UsbDcd usbdcd;
    k22_usbdcd_reset(&usbdcd);
    configure(state, &usbdcd, false);
    expect(state, k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_STANDARD_HOST),
           "k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_STANDARD_HOST)");
    write_value(state, &usbdcd, CONTROL, IE | START);
    expect(state, read_value(state, &usbdcd, STATUS) == ACTIVE,
           "read_value(state, &usbdcd, STATUS) == ACTIVE");
    expect(state, (read_value(state, &usbdcd, TIMER0) & 0xfffu) == 2u,
           "(read_value(state, &usbdcd, TIMER0) & 0xfffu) == 2u");
    k22_usbdcd_advance(&usbdcd, 1u);
    expect(state, read_value(state, &usbdcd, STATUS) == ACTIVE,
           "read_value(state, &usbdcd, STATUS) == ACTIVE");
    k22_usbdcd_advance(&usbdcd, 1u);
    expect(state, read_value(state, &usbdcd, STATUS) == (ACTIVE | (1u << 18u)),
           "read_value(state, &usbdcd, STATUS) == (ACTIVE | (1u << 18u))");
    k22_usbdcd_advance(&usbdcd, 4u);
    expect(state, read_value(state, &usbdcd, STATUS) == ((2u << 18u) | (1u << 16u)),
           "read_value(state, &usbdcd, STATUS) == ((2u << 18u) | (1u << 16u))");
    expect(state, k22_usbdcd_irq(&usbdcd), "k22_usbdcd_irq(&usbdcd)");
    expect(state, (read_value(state, &usbdcd, CONTROL) & IF) != 0u,
           "(read_value(state, &usbdcd, CONTROL) & IF) != 0u");
    write_value(state, &usbdcd, CONTROL, IE | 1u);
    expect(state, !k22_usbdcd_irq(&usbdcd), "!k22_usbdcd_irq(&usbdcd)");
    expect(state, (read_value(state, &usbdcd, CONTROL) & IF) == 0u,
           "(read_value(state, &usbdcd, CONTROL) & IF) == 0u");
}

static void test_bc12(TestState* state, KinetisK22UsbCharger charger, uint32_t expected_result) {
    K22UsbDcd usbdcd;
    k22_usbdcd_reset(&usbdcd);
    configure(state, &usbdcd, true);
    expect(state, k22_usbdcd_set_charger(&usbdcd, charger),
           "k22_usbdcd_set_charger(&usbdcd, charger)");
    write_value(state, &usbdcd, CONTROL, IE | BC12 | START);
    k22_usbdcd_advance(&usbdcd, 6u);
    expect(state, read_value(state, &usbdcd, STATUS) == (ACTIVE | (1u << 18u)),
           "read_value(state, &usbdcd, STATUS) == (ACTIVE | (1u << 18u))");
    expect(state, !k22_usbdcd_irq(&usbdcd), "!k22_usbdcd_irq(&usbdcd)");
    k22_usbdcd_advance(&usbdcd, 2u);
    expect(state, read_value(state, &usbdcd, STATUS) == (ACTIVE | (2u << 18u) | (2u << 16u)),
           "read_value(state, &usbdcd, STATUS) == (ACTIVE | (2u << 18u) | (2u << 16u))");
    expect(state, k22_usbdcd_irq(&usbdcd), "k22_usbdcd_irq(&usbdcd)");
    write_value(state, &usbdcd, CONTROL, IE | BC12 | 1u);
    k22_usbdcd_advance(&usbdcd, 2u);
    expect(state, read_value(state, &usbdcd, STATUS) == ((3u << 18u) | (expected_result << 16u)),
           "read_value(state, &usbdcd, STATUS) == ((3u << 18u) | (expected_result << 16u))");
    expect(state, k22_usbdcd_irq(&usbdcd), "k22_usbdcd_irq(&usbdcd)");
}

static void test_bc11_pullup(TestState* state) {
    K22UsbDcd usbdcd;
    k22_usbdcd_reset(&usbdcd);
    configure(state, &usbdcd, false);
    expect(state, k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_CHARGING_PORT),
           "k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_CHARGING_PORT)");
    write_value(state, &usbdcd, CONTROL, IE | START);
    k22_usbdcd_advance(&usbdcd, 6u);
    expect(state, read_value(state, &usbdcd, STATUS) == (ACTIVE | (1u << 18u)),
           "read_value(state, &usbdcd, STATUS) == (ACTIVE | (1u << 18u))");
    expect(state, !k22_usbdcd_irq(&usbdcd), "!k22_usbdcd_irq(&usbdcd)");
    k22_usbdcd_advance(&usbdcd, 2u);
    expect(state, read_value(state, &usbdcd, STATUS) == (ACTIVE | (2u << 18u) | (2u << 16u)),
           "read_value(state, &usbdcd, STATUS) == (ACTIVE | (2u << 18u) | (2u << 16u))");
    write_value(state, &usbdcd, CONTROL, IE | 1u);
    k22_usbdcd_advance(&usbdcd, 10u);
    expect(state, read_value(state, &usbdcd, STATUS) == (ACTIVE | (2u << 18u) | (2u << 16u)),
           "read_value(state, &usbdcd, STATUS) == (ACTIVE | (2u << 18u) | (2u << 16u))");
    expect(state, k22_usbdcd_set_pullup(&usbdcd, true), "k22_usbdcd_set_pullup(&usbdcd, true)");
    k22_usbdcd_advance(&usbdcd, 3u);
    expect(state, read_value(state, &usbdcd, STATUS) == ((3u << 18u) | (2u << 16u)),
           "read_value(state, &usbdcd, STATUS) == ((3u << 18u) | (2u << 16u))");
}

static void test_data_contact_debounce(TestState* state) {
    K22UsbDcd usbdcd;
    k22_usbdcd_reset(&usbdcd);
    configure(state, &usbdcd, false);
    expect(state, k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_STANDARD_HOST),
           "k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_STANDARD_HOST)");
    write_value(state, &usbdcd, CONTROL, IE | START);
    k22_usbdcd_advance(&usbdcd, 1u);
    expect(state, k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_NONE),
           "k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_NONE)");
    k22_usbdcd_advance(&usbdcd, 5u);
    expect(state, read_value(state, &usbdcd, STATUS) == ACTIVE,
           "read_value(state, &usbdcd, STATUS) == ACTIVE");
    expect(state, k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_STANDARD_HOST),
           "k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_STANDARD_HOST)");
    k22_usbdcd_advance(&usbdcd, 1u);
    expect(state, read_value(state, &usbdcd, STATUS) == ACTIVE,
           "read_value(state, &usbdcd, STATUS) == ACTIVE");
    k22_usbdcd_advance(&usbdcd, 1u);
    expect(state, read_value(state, &usbdcd, STATUS) == (ACTIVE | (1u << 18u)),
           "read_value(state, &usbdcd, STATUS) == (ACTIVE | (1u << 18u))");
}

static void test_clock_unit_and_interrupt_mask(TestState* state) {
    K22UsbDcd usbdcd;
    k22_usbdcd_reset(&usbdcd);
    write_value(state, &usbdcd, CLOCK, (1u << 2u) | 1u);
    write_value(state, &usbdcd, TIMER0, 0u);
    write_value(state, &usbdcd, TIMER1, 0u);
    expect(state, k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_STANDARD_HOST),
           "k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_STANDARD_HOST)");
    write_value(state, &usbdcd, CONTROL, START);
    k22_usbdcd_advance(&usbdcd, 2999u);
    expect(state, read_value(state, &usbdcd, STATUS) == (ACTIVE | (1u << 18u)),
           "read_value(state, &usbdcd, STATUS) == (ACTIVE | (1u << 18u))");
    k22_usbdcd_advance(&usbdcd, 1u);
    expect(state, read_value(state, &usbdcd, STATUS) == ((2u << 18u) | (1u << 16u)),
           "read_value(state, &usbdcd, STATUS) == ((2u << 18u) | (1u << 16u))");
    expect(state, (read_value(state, &usbdcd, CONTROL) & IF) != 0u,
           "(read_value(state, &usbdcd, CONTROL) & IF) != 0u");
    expect(state, !k22_usbdcd_irq(&usbdcd), "!k22_usbdcd_irq(&usbdcd)");
}

static void test_error_timeout_and_software_reset(TestState* state) {
    K22UsbDcd usbdcd;
    k22_usbdcd_reset(&usbdcd);
    configure(state, &usbdcd, false);
    expect(state, k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_ERROR),
           "k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_ERROR)");
    write_value(state, &usbdcd, CONTROL, IE | START);
    expect(state, !k22_usbdcd_write(&usbdcd, CLOCK, 4u, 8u),
           "!k22_usbdcd_write(&usbdcd, CLOCK, 4u, 8u)");
    k22_usbdcd_advance(&usbdcd, 6u);
    expect(state, read_value(state, &usbdcd, STATUS) == ((1u << 20u) | (2u << 18u)),
           "read_value(state, &usbdcd, STATUS) == ((1u << 20u) | (2u << 18u))");

    write_value(state, &usbdcd, CONTROL, IE | SR);
    expect(state, read_value(state, &usbdcd, CONTROL) == (IE | START),
           "read_value(state, &usbdcd, CONTROL) == (IE | START)");
    expect(state, read_value(state, &usbdcd, STATUS) == 0u,
           "read_value(state, &usbdcd, STATUS) == 0u");
    expect(state, read_value(state, &usbdcd, CLOCK) == 4u,
           "read_value(state, &usbdcd, CLOCK) == 4u");
    expect(state, read_value(state, &usbdcd, TIMER0) == (2u << 16u),
           "read_value(state, &usbdcd, TIMER0) == (2u << 16u)");
    expect(state, read_value(state, &usbdcd, TIMER1) == ((2u << 16u) | 3u),
           "read_value(state, &usbdcd, TIMER1) == ((2u << 16u) | 3u)");
    expect(state, read_value(state, &usbdcd, TIMER2) == ((2u << 16u) | 2u),
           "read_value(state, &usbdcd, TIMER2) == ((2u << 16u) | 2u)");

    expect(state, k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_NONE),
           "k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_NONE)");
    write_value(state, &usbdcd, TIMER0, 0u);
    write_value(state, &usbdcd, CONTROL, IE | START);
    k22_usbdcd_advance(&usbdcd, 1000u);
    expect(state,
           (read_value(state, &usbdcd, STATUS) & (ACTIVE | (1u << 21u) | (1u << 20u))) ==
               (ACTIVE | (1u << 21u) | (1u << 20u)),
           "(read_value(state, &usbdcd, STATUS) & (ACTIVE | (1u << 21u) | (1u << 20u))) == "
           "(ACTIVE | (1u << 21u) | (1u << 20u))");
    expect(state, k22_usbdcd_irq(&usbdcd), "k22_usbdcd_irq(&usbdcd)");
    k22_usbdcd_advance(&usbdcd, 5000u);
    expect(state, (read_value(state, &usbdcd, TIMER0) & 0xfffu) == 0xfffu,
           "(read_value(state, &usbdcd, TIMER0) & 0xfffu) == 0xfffu");
    expect(state, k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_STANDARD_HOST),
           "k22_usbdcd_set_charger(&usbdcd, KINETIS_K22_USB_CHARGER_STANDARD_HOST)");
    k22_usbdcd_advance(&usbdcd, 6u);
    expect(state,
           read_value(state, &usbdcd, STATUS) ==
               ((1u << 21u) | (1u << 20u) | (2u << 18u) | (1u << 16u)),
           "read_value(state, &usbdcd, STATUS) == ((1u << 21u) | (1u << 20u) | (2u << 18u) "
           "| (1u << 16u))");
}

static void test_copy(TestState* state) {
    K22UsbDcd source;
    K22UsbDcd destination;
    k22_usbdcd_reset(&source);
    configure(state, &source, true);
    expect(state, k22_usbdcd_set_charger(&source, KINETIS_K22_USB_CHARGER_DEDICATED),
           "k22_usbdcd_set_charger(&source, KINETIS_K22_USB_CHARGER_DEDICATED)");
    write_value(state, &source, CONTROL, IE | BC12 | START);
    k22_usbdcd_advance(&source, 3u);
    expect(state, k22_usbdcd_copy(&destination, &source), "k22_usbdcd_copy(&destination, &source)");
    expect(state, memcmp(&destination, &source, sizeof(source)) == 0,
           "memcmp(&destination, &source, sizeof(source)) == 0");
    expect(state, !k22_usbdcd_copy(NULL, &source), "!k22_usbdcd_copy(NULL, &source)");
    expect(state, !k22_usbdcd_copy(&destination, NULL), "!k22_usbdcd_copy(&destination, NULL)");
    k22_usbdcd_advance(NULL, 1u);
    k22_usbdcd_advance(&destination, 0u);
}

int main(void) {
    TestState state = {0};
    test_reset_and_access(&state);
    test_standard_host(&state);
    test_bc12(&state, KINETIS_K22_USB_CHARGER_CHARGING_PORT, 2u);
    test_bc12(&state, KINETIS_K22_USB_CHARGER_DEDICATED, 3u);
    test_bc11_pullup(&state);
    test_data_contact_debounce(&state);
    test_clock_unit_and_interrupt_mask(&state);
    test_error_timeout_and_software_reset(&state);
    test_copy(&state);
    return test_finish(&state);
}
