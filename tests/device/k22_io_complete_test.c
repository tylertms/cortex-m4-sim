#include "k22_io.h"

#include <stdint.h>
#include <string.h>

#include "test.h"

enum {
    PORTA = 0x40049000u,
    GPIOA = 0x400ff000u,
    USB0 = 0x40072000u,
    CAN0 = 0x40024000u,
    I2S0 = 0x4002f000u,
    FLEXBUS = 0x4000c000u,
    SYSMPU = 0x4000d000u,
};

#define MCM UINT32_C(0xe0080008)

typedef struct {
    K22IoEvent events[128];
    size_t count;
} EventLog;

static void record_event(void* context, const K22IoEvent* event) {
    EventLog* log = context;
    if (log->count < sizeof(log->events) / sizeof(log->events[0]))
        log->events[log->count++] = *event;
}

static uint32_t read_value(TestState* state, K22Io* io, uint32_t address, uint8_t size) {
    uint32_t value = 0;
    TEST_EXPECT(state, k22_io_read(io, address, size, &value));
    return value;
}

static void write_value(TestState* state, K22Io* io, uint32_t address, uint8_t size,
                        uint32_t value) {
    TEST_EXPECT(state, k22_io_write(io, address, size, value));
}

static uint32_t bit_alias(uint32_t address, uint8_t bit) {
    return 0x42000000u + (address - 0x40000000u) * 32u + (uint32_t)bit * 4u;
}

static bool has_event(const EventLog* log, K22IoEventType type, uint32_t source) {
    for (size_t index = 0; index < log->count; index++) {
        if (log->events[index].type == type && log->events[index].source == source)
            return true;
    }
    return false;
}

static const K22IoEvent* find_event(const EventLog* log, K22IoEventType type,
                                    uint32_t source) {
    for (size_t index = log->count; index > 0; index--) {
        if (log->events[index - 1u].type == type &&
            log->events[index - 1u].source == source)
            return &log->events[index - 1u];
    }
    return NULL;
}

static void test_reset_clock_and_configuration(TestState* state) {
    const K22Profile* profile = k22_profile_get(K22_PROFILE_MK22FN51212);
    EventLog log = {0};
    K22IoConfiguration configuration = k22_io_default_configuration(profile);
    configuration.package_pin_mask[0] = 0x0fu;
    configuration.flash_configuration[0] = 0x12u;
    configuration.flash_configuration[1] = 0x34u;
    configuration.event_handler = record_event;
    configuration.event_context = &log;
    K22Io io;
    TEST_EXPECT(state, !k22_io_init(NULL, configuration));
    TEST_EXPECT(state, k22_io_init(&io, configuration));
    TEST_EXPECT(state, read_value(state, &io, 0x400u, 2) == 0x3412u);
    TEST_EXPECT(state, !k22_io_write(&io, 0x400u, 1, 0));
    uint32_t value = 0;
    TEST_EXPECT(state, !k22_io_read(&io, PORTA, 4, &value));
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_ACCESS_ERROR, PORTA));
    k22_io_set_clock(&io, K22_PERIPHERAL_PORTA, true);
    TEST_EXPECT(state, k22_io_clock_enabled(&io, K22_PERIPHERAL_PORTA));
    TEST_EXPECT(state, k22_io_clock_enabled(&io, K22_PERIPHERAL_GPIOA));
    TEST_EXPECT(state, read_value(state, &io, PORTA, 4) == 0x702u);
    TEST_EXPECT(state, !k22_io_read(&io, PORTA + 16u, 4, &value));
    TEST_EXPECT(state, read_value(state, &io, MCM, 2) == 0x1fu);
    TEST_EXPECT(state, read_value(state, &io, MCM + 2u, 2) == 0x17u);
    TEST_EXPECT(state, !k22_io_write(&io, MCM, 2, 0));
    write_value(state, &io, MCM + 4u, 4, 1u);
    TEST_EXPECT(state, read_value(state, &io, MCM + 4u, 4) == 1u);
    TEST_EXPECT(state, !k22_io_read(&io, 0x40012340u, 4, &value));
}

static void test_gpio_mux_pull_open_drain_and_lock(TestState* state) {
    EventLog log = {0};
    K22IoConfiguration configuration =
        k22_io_default_configuration(k22_profile_get(K22_PROFILE_MK22FN51212));
    configuration.package_pin_mask[0] = 0x0fu;
    configuration.event_handler = record_event;
    configuration.event_context = &log;
    K22Io io;
    TEST_EXPECT(state, k22_io_init(&io, configuration));
    k22_io_set_clock(&io, K22_PERIPHERAL_GPIOA, true);
    write_value(state, &io, PORTA, 4, 1u << 8);
    write_value(state, &io, GPIOA + 0x14u, 4, 1u);
    write_value(state, &io, GPIOA + 4u, 4, 1u);
    TEST_EXPECT(state, read_value(state, &io, GPIOA, 4) == 1u);
    TEST_EXPECT(state, (read_value(state, &io, GPIOA + 0x10u, 4) & 1u) != 0u);
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_GPIO_OUTPUT, 0));
    write_value(state, &io, PORTA, 4, (1u << 8) | (1u << 5) | 3u);
    TEST_EXPECT(state, (read_value(state, &io, GPIOA + 0x10u, 4) & 1u) != 0u);
    TEST_EXPECT(state, k22_io_drive_pin(&io, 0, 0, false));
    TEST_EXPECT(state, (read_value(state, &io, GPIOA + 0x10u, 4) & 1u) == 0);
    TEST_EXPECT(state, k22_io_release_pin(&io, 0, 0));
    TEST_EXPECT(state, (read_value(state, &io, GPIOA + 0x10u, 4) & 1u) != 0);
    write_value(state, &io, PORTA + 4u, 4, (1u << 15) | (1u << 8));
    write_value(state, &io, PORTA + 4u, 4, 0);
    TEST_EXPECT(state, (read_value(state, &io, PORTA + 4u, 4) & (1u << 15)) != 0);
    write_value(state, &io, PORTA + 0x80u, 4, (4u << 16) | (1u << 8));
    TEST_EXPECT(state, (read_value(state, &io, PORTA + 8u, 4) & (7u << 8)) == (1u << 8));
    write_value(state, &io, bit_alias(GPIOA + 0x14u, 2), 4, 1u);
    write_value(state, &io, bit_alias(GPIOA, 2), 4, 1u);
    TEST_EXPECT(state, read_value(state, &io, bit_alias(GPIOA, 2), 4) == 1u);
    TEST_EXPECT(state, (read_value(state, &io, GPIOA, 4) & 4u) != 0);
    TEST_EXPECT(state, !k22_io_drive_pin(&io, 0, 7, true));
    TEST_EXPECT(state, !k22_io_release_pin(&io, 6, 0));
}

static void test_gpio_interrupt_dma_filter_and_bit_band(TestState* state) {
    EventLog log = {0};
    K22IoConfiguration configuration =
        k22_io_default_configuration(k22_profile_get(K22_PROFILE_MK22FN51212));
    configuration.package_pin_mask[3] = 3u;
    configuration.event_handler = record_event;
    configuration.event_context = &log;
    K22Io io;
    TEST_EXPECT(state, k22_io_init(&io, configuration));
    k22_io_set_clock(&io, K22_PERIPHERAL_PORTD, true);
    write_value(state, &io, 0x4004c000u, 4, 9u << 16);
    TEST_EXPECT(state, k22_io_drive_pin(&io, 3, 0, false));
    TEST_EXPECT(state, k22_io_drive_pin(&io, 3, 0, true));
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_IRQ, 62u));
    TEST_EXPECT(state, k22_io_irq_asserted(&io, 62u));
    TEST_EXPECT(state, (read_value(state, &io, 0x4004c0a0u, 4) & 1u) != 0);
    write_value(state, &io, 0x4004c000u, 1, 3u);
    TEST_EXPECT(state, (read_value(state, &io, 0x4004c000u, 4) & (1u << 24)) != 0);
    write_value(state, &io, 0x4004c0a0u, 4, 1u);
    TEST_EXPECT(state, !k22_io_irq_asserted(&io, 62u));
    TEST_EXPECT(state, (read_value(state, &io, 0x4004c000u, 4) & (1u << 24)) == 0);
    write_value(state, &io, 0x4004c004u, 4, 1u << 16);
    TEST_EXPECT(state, k22_io_drive_pin(&io, 3, 1, false));
    TEST_EXPECT(state, k22_io_drive_pin(&io, 3, 1, true));
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_DMA, 97u));
    write_value(state, &io, 0x4004c0c0u, 4, 1u);
    write_value(state, &io, 0x4004c0c8u, 1, 3u);
    write_value(state, &io, 0x4004c000u, 4, 10u << 16);
    TEST_EXPECT(state, k22_io_drive_pin(&io, 3, 0, false));
    k22_io_advance(&io, 3);
    TEST_EXPECT(state, (k22_io_pin_input(&io, 3) & 1u) != 0);
    k22_io_advance(&io, 1);
    TEST_EXPECT(state, (k22_io_pin_input(&io, 3) & 1u) == 0);
    write_value(state, &io, 0x4004c000u, 4, (10u << 16) | 3u);
    TEST_EXPECT(state, k22_io_release_pin(&io, 3, 0));
    k22_io_advance(&io, 4);
    TEST_EXPECT(state, (k22_io_pin_input(&io, 3) & 1u) != 0);
    write_value(state, &io, 0x4004c0c0u, 4, 3u);
    TEST_EXPECT(state, k22_io_drive_pin(&io, 3, 1, true));
    TEST_EXPECT(state, k22_io_release_pin(&io, 3, 1));
    k22_io_set_clock(&io, K22_PERIPHERAL_USB0, true);
    write_value(state, &io, bit_alias(USB0 + 0x84u, 3), 4, 1u);
    TEST_EXPECT(state, read_value(state, &io, USB0 + 0x84u, 1) == 8u);
    TEST_EXPECT(state, read_value(state, &io, bit_alias(USB0 + 0x84u, 3), 4) == 1u);
}

static void test_usb(TestState* state) {
    EventLog log = {0};
    K22IoConfiguration configuration =
        k22_io_default_configuration(k22_profile_get(K22_PROFILE_MK22FN51212));
    configuration.event_handler = record_event;
    configuration.event_context = &log;
    K22Io io;
    TEST_EXPECT(state, k22_io_init(&io, configuration));
    k22_io_set_clock(&io, K22_PERIPHERAL_USB0, true);
    TEST_EXPECT(state, read_value(state, &io, USB0, 1) == 4u);
    TEST_EXPECT(state, read_value(state, &io, USB0 + 4u, 1) == 0xfbu);
    TEST_EXPECT(state, read_value(state, &io, USB0 + 8u, 1) == 0x33u);
    TEST_EXPECT(state, !k22_io_write(&io, USB0, 1, 0));
    write_value(state, &io, USB0 + 0x84u, 1, (1u << 3) | (1u << 2));
    write_value(state, &io, USB0 + 0x94u, 1, 1u);
    TEST_EXPECT(state, k22_io_usb_token(&io, 3, 0x69u, false));
    TEST_EXPECT(state, read_value(state, &io, USB0 + 0x90u, 1) == 0x30u);
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_USB_TOKEN, 3u));
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_IRQ, 53u));
    TEST_EXPECT(state, k22_io_irq_asserted(&io, 53u));
    write_value(state, &io, USB0 + 0x80u, 1, 1u << 3);
    TEST_EXPECT(state, (read_value(state, &io, USB0 + 0x80u, 1) & (1u << 3)) == 0);
    TEST_EXPECT(state, !k22_io_irq_asserted(&io, 53u));
    k22_io_advance(&io, 2500u);
    TEST_EXPECT(state, read_value(state, &io, USB0 + 0xa0u, 1) == 2u);
    TEST_EXPECT(state, (read_value(state, &io, USB0 + 0x80u, 1) & (1u << 2)) != 0);
    write_value(state, &io, USB0 + 0xd0u, 1, 0x80u);
    TEST_EXPECT(state, read_value(state, &io, USB0, 1) == 4u);
    TEST_EXPECT(state, read_value(state, &io, USB0 + 0x94u, 1) == 0);
}

static void test_can(TestState* state) {
    EventLog log = {0};
    K22IoConfiguration configuration =
        k22_io_default_configuration(k22_profile_get(K22_PROFILE_MK22FN1M012));
    configuration.event_handler = record_event;
    configuration.event_context = &log;
    K22Io io;
    TEST_EXPECT(state, k22_io_init(&io, configuration));
    k22_io_set_clock(&io, K22_PERIPHERAL_CAN0, true);
    TEST_EXPECT(state, read_value(state, &io, CAN0, 4) == 0xd890000fu);
    write_value(state, &io, CAN0, 4, 0x0fu);
    write_value(state, &io, CAN0 + 0x10u, 4, 0);
    write_value(state, &io, CAN0 + 0x28u, 4, 1u);
    write_value(state, &io, CAN0 + 0x80u, 4, 4u << 24);
    K22CanFrame frame = {0x123u, 8, {0, 1, 2, 3, 4, 5, 6, 7}, false, false};
    TEST_EXPECT(state, k22_io_can_receive(&io, &frame));
    TEST_EXPECT(state, (read_value(state, &io, CAN0 + 0x30u, 4) & 1u) != 0);
    TEST_EXPECT(state, read_value(state, &io, CAN0 + 0x84u, 4) == 0x123u);
    TEST_EXPECT(state, read_value(state, &io, CAN0 + 0x88u, 4) == 0x00010203u);
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_IRQ, 75u));
    TEST_EXPECT(state, k22_io_irq_asserted(&io, 75u));
    write_value(state, &io, CAN0 + 0x30u, 4, 1u);
    TEST_EXPECT(state, read_value(state, &io, CAN0 + 0x30u, 4) == 0);
    TEST_EXPECT(state, !k22_io_irq_asserted(&io, 75u));
    write_value(state, &io, CAN0 + 0x94u, 4, 0x321u);
    write_value(state, &io, CAN0 + 0x98u, 4, 0x01020304u);
    write_value(state, &io, CAN0 + 0x9cu, 4, 0x05060708u);
    write_value(state, &io, CAN0 + 0x90u, 4, (0xcu << 24) | (3u << 16));
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_CAN_TRANSMIT, 1u));
    const K22IoEvent* transmit = find_event(&log, K22_IO_EVENT_CAN_TRANSMIT, 1u);
    TEST_EXPECT(state, transmit != NULL);
    TEST_EXPECT(state, transmit->length == 3u);
    TEST_EXPECT(state, transmit->data[0] == 1u);
    TEST_EXPECT(state, transmit->data[7] == 8u);
    k22_io_advance(&io, 17u);
    TEST_EXPECT(state, read_value(state, &io, CAN0 + 8u, 4) == 17u);
    write_value(state, &io, CAN0, 4, 1u << 25);
    TEST_EXPECT(state, read_value(state, &io, CAN0, 4) == 0xd890000fu);
}

static void test_i2s(TestState* state) {
    EventLog log = {0};
    K22IoConfiguration configuration =
        k22_io_default_configuration(k22_profile_get(K22_PROFILE_MK22FN51212));
    configuration.event_handler = record_event;
    configuration.event_context = &log;
    K22Io io;
    TEST_EXPECT(state, k22_io_init(&io, configuration));
    k22_io_set_clock(&io, K22_PERIPHERAL_I2S0, true);
    write_value(state, &io, I2S0, 4, UINT32_C(0x80000100));
    write_value(state, &io, I2S0 + 0x80u, 4, UINT32_C(0x80000100));
    write_value(state, &io, I2S0 + 0x20u, 4, 0x12345678u);
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_I2S_TRANSMIT, 0));
    uint32_t sample = 0;
    TEST_EXPECT(state, k22_io_i2s_transmit(&io, &sample));
    TEST_EXPECT(state, sample == 0x12345678u);
    TEST_EXPECT(state, k22_io_i2s_receive(&io, 0x87654321u));
    TEST_EXPECT(state, read_value(state, &io, I2S0 + 0xa0u, 4) == 0x87654321u);
    TEST_EXPECT(state, read_value(state, &io, I2S0 + 0xa0u, 4) == 0);
    TEST_EXPECT(state, (read_value(state, &io, I2S0 + 0x80u, 4) & (1u << 18)) != 0);
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_IRQ, 28u));
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_IRQ, 29u));
    TEST_EXPECT(state, k22_io_irq_asserted(&io, 28u));
    TEST_EXPECT(state, !k22_io_irq_asserted(&io, 29u));
    write_value(state, &io, I2S0, 4, UINT32_C(0x80000000));
    TEST_EXPECT(state, !k22_io_irq_asserted(&io, 28u));
}

static void test_flexbus_sysmpu_copy_and_reset(TestState* state) {
    EventLog log = {0};
    K22IoConfiguration configuration =
        k22_io_default_configuration(k22_profile_get(K22_PROFILE_MK22FN1M012));
    configuration.event_handler = record_event;
    configuration.event_context = &log;
    K22Io io;
    K22Io copy;
    TEST_EXPECT(state, k22_io_init(&io, configuration));
    k22_io_set_clock(&io, K22_PERIPHERAL_FB, true);
    k22_io_set_clock(&io, K22_PERIPHERAL_SYSMPU, true);
    write_value(state, &io, FLEXBUS, 4, 0x60000000u);
    write_value(state, &io, FLEXBUS + 4u, 4, 1u);
    TEST_EXPECT(state, read_value(state, &io, FLEXBUS, 4) == 0x60000000u);
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_FLEXBUS_TRANSFER, 0));
    TEST_EXPECT(state, k22_io_flexbus_transfer(&io, 0x60001234u, 4u, false, 0u));
    TEST_EXPECT(state, k22_io_flexbus_transfer(&io, 0x60005678u, 2u, true, 0x55aau));
    TEST_EXPECT(state, !k22_io_flexbus_transfer(&io, 0x50000000u, 4u, false, 0u));
    TEST_EXPECT(state, !k22_io_flexbus_transfer(&io, 0x60000000u, 3u, false, 0u));
    TEST_EXPECT(state, read_value(state, &io, SYSMPU, 4) == 0x00815101u);
    TEST_EXPECT(state, read_value(state, &io, SYSMPU + 0x404u, 4) == UINT32_MAX);
    TEST_EXPECT(state, read_value(state, &io, SYSMPU + 0x408u, 4) == 0x0061f7dfu);
    TEST_EXPECT(state, k22_io_sysmpu_access(&io, 0x1000u, 0, false, K22_SYSMPU_WRITE));
    for (uint8_t region = 0; region < 12; region++)
        write_value(state, &io, SYSMPU + 0x40cu + (uint32_t)region * 16u, 4, 0);
    write_value(state, &io, SYSMPU + 0x400u, 4, 0x1000u);
    write_value(state, &io, SYSMPU + 0x404u, 4, 0x1fffu);
    write_value(state, &io, SYSMPU + 0x408u, 4, 4u | (3u << 3));
    TEST_EXPECT(state, read_value(state, &io, SYSMPU + 0x800u, 4) == (4u | (3u << 3)));
    write_value(state, &io, SYSMPU + 0x800u, 4, 7u);
    TEST_EXPECT(state, read_value(state, &io, SYSMPU + 0x408u, 4) == 7u);
    write_value(state, &io, SYSMPU + 0x408u, 4, 4u | (3u << 3));
    write_value(state, &io, SYSMPU + 0x40cu, 4, 1u);
    TEST_EXPECT(state, k22_io_sysmpu_access(&io, 0x1800u, 0, false, K22_SYSMPU_READ));
    TEST_EXPECT(state, !k22_io_sysmpu_access(&io, 0x1800u, 0, false, K22_SYSMPU_WRITE));
    TEST_EXPECT(state, read_value(state, &io, SYSMPU + 0x10u, 4) == 0x1800u);
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_ACCESS_ERROR, 0x1800u));
    write_value(state, &io, SYSMPU, 4, 1u << 27);
    TEST_EXPECT(state, read_value(state, &io, SYSMPU, 4) == 0x00815100u);
    TEST_EXPECT(state, !k22_io_write(&io, SYSMPU + 0x10u, 4, 1u));
    write_value(state, &io, SYSMPU + 0x400u, 4, 0x1234u);
    TEST_EXPECT(state, read_value(state, &io, SYSMPU + 0x400u, 4) == 0x1234u);
    TEST_EXPECT(state, k22_io_init(&copy, configuration));
    TEST_EXPECT(state, k22_io_copy(&copy, &io));
    TEST_EXPECT(state, read_value(state, &copy, FLEXBUS, 4) == 0x60000000u);
    k22_io_reset(&copy);
    TEST_EXPECT(state, !k22_io_clock_enabled(&copy, K22_PERIPHERAL_FB));
    TEST_EXPECT(state, k22_io_clock_enabled(&copy, K22_PERIPHERAL_MCM));
    TEST_EXPECT(state, k22_io_copy(&copy, &copy));
    TEST_EXPECT(state, !k22_io_copy(NULL, &io));
}

static void test_edges_and_fail_closed_access(TestState* state) {
    EventLog log = {0};
    K22IoConfiguration configuration =
        k22_io_default_configuration(k22_profile_get(K22_PROFILE_MK22FN1M012));
    configuration.event_handler = record_event;
    configuration.event_context = &log;
    K22Io io;
    TEST_EXPECT(state, k22_io_init(&io, configuration));
    k22_io_reset(NULL);
    k22_io_advance(NULL, 1);
    k22_io_advance(&io, 0);
    k22_io_set_clock(&io, K22_PERIPHERAL_COUNT, true);
    TEST_EXPECT(state, !k22_io_clock_enabled(NULL, K22_PERIPHERAL_USB0));
    uint32_t value = 0;
    TEST_EXPECT(state, !k22_io_read(NULL, PORTA, 4, &value));
    TEST_EXPECT(state, !k22_io_read(&io, PORTA, 3, &value));
    TEST_EXPECT(state, !k22_io_read(&io, PORTA, 4, NULL));
    TEST_EXPECT(state, !k22_io_write(NULL, PORTA, 4, 0));
    TEST_EXPECT(state, !k22_io_write(&io, PORTA, 3, 0));
    TEST_EXPECT(state, !k22_io_write(&io, PORTA, 4, 0));
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_ACCESS_ERROR, PORTA));
    k22_io_set_clock(&io, K22_PERIPHERAL_PORTD, true);
    write_value(state, &io, 0x4004c0c0u, 4, 3u);
    write_value(state, &io, 0x4004c0c4u, 1, 1u);
    write_value(state, &io, 0x4004c0c8u, 1, 7u);
    TEST_EXPECT(state, read_value(state, &io, 0x4004c0c0u, 4) == 3u);
    TEST_EXPECT(state, read_value(state, &io, 0x4004c0c4u, 1) == 1u);
    TEST_EXPECT(state, read_value(state, &io, 0x4004c0c8u, 1) == 7u);
    TEST_EXPECT(state, read_value(state, &io, 0x4004c080u, 4) == 0);
    write_value(state, &io, 0x4004c0c0u, 4, 0);
    write_value(state, &io, 0x4004c000u, 4, 3u << 16);
    TEST_EXPECT(state, k22_io_drive_pin(&io, 3, 0, true));
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_DMA, 96u));
    TEST_EXPECT(state, !k22_io_irq_asserted(&io, 62u));
    write_value(state, &io, 0x4004c000u, 4, 8u << 16);
    TEST_EXPECT(state, k22_io_drive_pin(&io, 3, 0, false));
    write_value(state, &io, 0x4004c000u, 4, 12u << 16);
    TEST_EXPECT(state, k22_io_drive_pin(&io, 3, 0, true));
    write_value(state, &io, 0x4004c000u, 4, (9u << 16) | (1u << 15) | (1u << 24));
    TEST_EXPECT(state, k22_io_drive_pin(&io, 3, 0, false));
    TEST_EXPECT(state, k22_io_drive_pin(&io, 3, 0, true));
    write_value(state, &io, 0x4004c000u, 4, 1u << 24);
    TEST_EXPECT(state, (read_value(state, &io, 0x4004c000u, 4) & (1u << 24)) == 0);
    write_value(state, &io, 0x4004c084u, 4, (1u << 16) | (1u << 8));
    TEST_EXPECT(state, (read_value(state, &io, 0x4004c040u, 4) & (7u << 8)) != 0);
    write_value(state, &io, 0x400ff0c0u + 0x14u, 4, 3u);
    write_value(state, &io, 0x400ff0c0u, 4, 3u);
    write_value(state, &io, 0x400ff0c0u + 8u, 4, 1u);
    TEST_EXPECT(state, read_value(state, &io, 0x400ff0c0u, 4) == 2u);
    write_value(state, &io, 0x400ff0c0u + 0x0cu, 4, 3u);
    TEST_EXPECT(state, read_value(state, &io, 0x400ff0c0u, 4) == 1u);
    k22_io_set_clock(&io, K22_PERIPHERAL_USB0, true);
    write_value(state, &io, USB0 + 0x114u, 1, 0x55u);
    TEST_EXPECT(state, read_value(state, &io, USB0 + 0x114u, 1) == 0x55u);
    TEST_EXPECT(state, !k22_io_read(&io, USB0 + 0x114u, 2, &value));
    TEST_EXPECT(state, !k22_io_usb_token(&io, 16, 0, false));
    TEST_EXPECT(state, !k22_io_usb_token(&io, 0, 0, false));
    k22_io_set_clock(&io, K22_PERIPHERAL_CAN0, true);
    write_value(state, &io, CAN0, 4, 0x0fu);
    write_value(state, &io, CAN0 + 0x28u, 4, 2u);
    write_value(state, &io, CAN0 + 0x94u, 4, 0x321u);
    write_value(state, &io, CAN0 + 0x90u, 4, (0xcu << 24) | (1u << 16));
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_IRQ, 75u));
    write_value(state, &io, CAN0 + 0x10u, 4, UINT32_MAX);
    write_value(state, &io, CAN0 + 0xa0u, 4, 4u << 24);
    write_value(state, &io, CAN0 + 0xa4u, 4, 0x456u);
    K22CanFrame frame = {0x123u, 1, {9, 0, 0, 0, 0, 0, 0, 0}, true, true};
    TEST_EXPECT(state, !k22_io_can_receive(&io, &frame));
    frame.identifier = 0x456u;
    TEST_EXPECT(state, k22_io_can_receive(&io, &frame));
    TEST_EXPECT(state,
                (read_value(state, &io, CAN0 + 0xa0u, 4) & ((1u << 21) | (1u << 20))) != 0);
    k22_io_set_clock(&io, K22_PERIPHERAL_I2S0, true);
    write_value(state, &io, I2S0, 4, UINT32_C(0x80000001));
    for (uint8_t index = 0; index < K22_IO_FIFO_CAPACITY; index++)
        write_value(state, &io, I2S0 + 0x20u, 4, index);
    write_value(state, &io, I2S0 + 0x20u, 4, 99u);
    TEST_EXPECT(state, (read_value(state, &io, I2S0, 4) & (1u << 18)) != 0);
    for (uint8_t index = 0; index < K22_IO_FIFO_CAPACITY; index++)
        TEST_EXPECT(state, k22_io_i2s_transmit(&io, &value));
    TEST_EXPECT(state, !k22_io_i2s_transmit(&io, &value));
    write_value(state, &io, I2S0 + 0x100u, 4, 0x1234u);
    TEST_EXPECT(state, read_value(state, &io, I2S0 + 0x100u, 4) == 0x1234u);
    write_value(state, &io, I2S0, 4, 0);
    write_value(state, &io, I2S0 + 0x80u, 4, UINT32_C(0x80000001));
    TEST_EXPECT(state, k22_io_i2s_receive(&io, 0xa5a55a5au));
    TEST_EXPECT(state, has_event(&log, K22_IO_EVENT_DMA, 1u));
    write_value(state, &io, I2S0 + 0x80u, 4, 0);
    TEST_EXPECT(state, read_value(state, &io, I2S0 + 0xa0u, 4) == 0u);
    k22_io_set_clock(&io, K22_PERIPHERAL_SYSMPU, true);
    write_value(state, &io, SYSMPU, 4, (1u << 27) | 1u);
    write_value(state, &io, SYSMPU + 0x408u, 4, 1u << 31);
    TEST_EXPECT(state, k22_io_sysmpu_access(&io, 0, 7, false, K22_SYSMPU_READ));
    TEST_EXPECT(state, !k22_io_sysmpu_access(&io, 0, 7, false, K22_SYSMPU_EXECUTE));
    TEST_EXPECT(state, !k22_io_sysmpu_access(&io, 0, 8, false, K22_SYSMPU_READ));
    TEST_EXPECT(state, !k22_io_sysmpu_access(NULL, 0, 0, false, K22_SYSMPU_READ));

    K22IoConfiguration quiet_configuration =
        k22_io_default_configuration(k22_profile_get(K22_PROFILE_MK22FN1M012));
    K22Io quiet;
    TEST_EXPECT(state, k22_io_init(&quiet, quiet_configuration));
    TEST_EXPECT(state, !k22_io_read(&quiet, PORTA, 4, &value));
    k22_io_set_clock(&quiet, K22_PERIPHERAL_CAN0, true);
    write_value(state, &quiet, CAN0, 4, 0x0fu);
    write_value(state, &quiet, CAN0 + 0x90u, 4, (0xcu << 24) | (1u << 16));
}

int main(void) {
    TestState state = {0};
    test_reset_clock_and_configuration(&state);
    test_gpio_mux_pull_open_drain_and_lock(&state);
    test_gpio_interrupt_dma_filter_and_bit_band(&state);
    test_usb(&state);
    test_can(&state);
    test_i2s(&state);
    test_flexbus_sysmpu_copy_and_reset(&state);
    test_edges_and_fail_closed_access(&state);
    return test_finish(&state);
}
