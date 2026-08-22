#include "device/kinetis_k22/communication/serial/api.h"
#include "test.h"

#include <string.h>

enum {
    LPUART0_BASE = 0x4002a000u,
    SPI0_BASE = 0x4002c000u,
    SPI1_BASE = 0x4002d000u,
    I2C0_BASE = 0x40066000u,
    I2C2_BASE = 0x400e6000u,
    UART0_BASE = 0x4006a000u,
    UART_S1 = 4u,
};

static uint32_t read_register(TestState* state, K22Serial* serial, uint32_t address, uint8_t size) {
    uint32_t value = 0;
    expect(state, k22_serial_read(serial, address, size, &value),
           "k22_serial_read(serial, address, size, &value)");
    return value;
}

static void write_register(TestState* state, K22Serial* serial, uint32_t address, uint8_t size,
                           uint32_t value) {
    expect(state, k22_serial_write(serial, address, size, value),
           "k22_serial_write(serial, address, size, value)");
}

static K22Serial create_serial(TestState* state, K22ProfileId profile_id) {
    K22Serial serial;
    expect(state, k22_serial_init(&serial, k22_profile_get(profile_id)),
           "k22_serial_init(&serial, k22_profile_get(profile_id))");
    return serial;
}

static void expect_event(TestState* state, K22Serial* serial, K22SerialEndpoint endpoint,
                         K22SerialEventType type, uint16_t value) {
    K22SerialEvent event;
    expect(state, k22_serial_pop_event(serial, &event), "k22_serial_pop_event(serial, &event)");
    expect(state, event.endpoint == endpoint, "event.endpoint == endpoint");
    expect(state, event.type == type, "event.type == type");
    expect(state, event.value == value, "event.value == value");
}

static void test_profiles_and_gates(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    uint32_t value = 0;
    expect(state, serial.lpuart0.present, "serial.lpuart0.present");
    expect(state, serial.spi[1].present, "serial.spi[1].present");
    expect(state, !serial.i2c[2].present, "!serial.i2c[2].present");
    expect(state, !k22_serial_read(&serial, UART0_BASE + 4, 1, &value),
           "!k22_serial_read(&serial, UART0_BASE + 4, 1, &value)");
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, true)");
    expect(state, read_register(state, &serial, UART0_BASE + 4, 1) == 0xc0u,
           "read_register(state, &serial, UART0_BASE + 4, 1) == 0xc0u");
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, false),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, false)");
    expect(state, !k22_serial_write(&serial, UART0_BASE + 3, 1, 0xffu),
           "!k22_serial_write(&serial, UART0_BASE + 3, 1, 0xffu)");
    expect(state, !k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C2, true),
           "!k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C2, true)");
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART1, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART1, true)");
    expect(state,
           !k22_serial_read(&serial, serial.uart[1].base + serial.uart[1].block_size, 1, &value),
           "!k22_serial_read(&serial, serial.uart[1].base + serial.uart[1].block_size, 1, "
           "&value)");

    serial = create_serial(state, K22_PROFILE_MK22FN1M012);
    expect(state, !serial.lpuart0.present, "!serial.lpuart0.present");
    expect(state, serial.spi[1].present, "serial.spi[1].present");
    expect(state, serial.spi[2].present, "serial.spi[2].present");
    expect(state, serial.uart[3].present, "serial.uart[3].present");
    expect(state, serial.uart[4].present, "serial.uart[4].present");
    expect(state, serial.uart[5].present, "serial.uart[5].present");
    expect(state, serial.i2c[2].present, "serial.i2c[2].present");
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C2, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C2, true)");
    expect(state, read_register(state, &serial, I2C2_BASE + 3, 1) == 0x80u,
           "read_register(state, &serial, I2C2_BASE + 3, 1) == 0x80u");
}

static void test_uart_transfer_status_interrupt_and_dma(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, true)");
    serial.uart[0].registers[UART_S1] |= 0x10u;
    write_register(state, &serial, UART0_BASE + 3, 1, 0x10u);
    expect(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0),
           "k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0)");
    write_register(state, &serial, UART0_BASE, 1, 0);
    write_register(state, &serial, UART0_BASE + 1, 1, 1);
    write_register(state, &serial, UART0_BASE + 3, 1, 0x2cu);
    write_register(state, &serial, UART0_BASE + 6, 1, 0x02u);
    write_register(state, &serial, UART0_BASE + 0x0b, 1, 0x20u);
    expect(state, k22_serial_push_receive(&serial, K22_SERIAL_UART0, 0x15au, 0x02u),
           "k22_serial_push_receive(&serial, K22_SERIAL_UART0, 0x15au, 0x02u)");
    k22_serial_advance(&serial, 159);
    expect(state, (read_register(state, &serial, UART0_BASE + 4, 1) & 0x20u) == 0,
           "(read_register(state, &serial, UART0_BASE + 4, 1) & 0x20u) == 0");
    k22_serial_advance(&serial, 1);
    uint32_t status = read_register(state, &serial, UART0_BASE + 4, 1);
    expect(state, (status & 0x22u) == 0x22u, "(status & 0x22u) == 0x22u");
    expect(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0),
           "k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0)");
    expect(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0_ERROR),
           "k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0_ERROR)");
    expect(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_UART0_RECEIVE),
           "k22_serial_dma_request(&serial, K22_SERIAL_DMA_UART0_RECEIVE)");
    expect(state, read_register(state, &serial, UART0_BASE + 7, 1) == 0x5au,
           "read_register(state, &serial, UART0_BASE + 7, 1) == 0x5au");
    expect(state, (read_register(state, &serial, UART0_BASE + 4, 1) & 0x2fu) == 0,
           "(read_register(state, &serial, UART0_BASE + 4, 1) & 0x2fu) == 0");
    expect(state, !k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0),
           "!k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0)");

    write_register(state, &serial, UART0_BASE + 7, 1, 0xa5u);
    k22_serial_advance(&serial, 159);
    uint16_t transmitted = 0;
    expect(state, !k22_serial_pop_transmit(&serial, K22_SERIAL_UART0, &transmitted),
           "!k22_serial_pop_transmit(&serial, K22_SERIAL_UART0, &transmitted)");
    k22_serial_advance(&serial, 1);
    expect(state, k22_serial_pop_transmit(&serial, K22_SERIAL_UART0, &transmitted),
           "k22_serial_pop_transmit(&serial, K22_SERIAL_UART0, &transmitted)");
    expect(state, transmitted == 0xa5u, "transmitted == 0xa5u");
    expect(state, (read_register(state, &serial, UART0_BASE + 4, 1) & 0xc0u) == 0xc0u,
           "(read_register(state, &serial, UART0_BASE + 4, 1) & 0xc0u) == 0xc0u");
}

static void test_uart_fifo_overrun_flush_and_copy(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART1, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART1, true)");
    uint32_t base = serial.uart[1].base;
    write_register(state, &serial, base, 1, 0);
    write_register(state, &serial, base + 1, 1, 1);
    write_register(state, &serial, base + 3, 1, 0x04u);
    expect(state, k22_serial_push_receive(&serial, K22_SERIAL_UART1, 0x11, 0),
           "k22_serial_push_receive(&serial, K22_SERIAL_UART1, 0x11, 0)");
    expect(state, k22_serial_push_receive(&serial, K22_SERIAL_UART1, 0x22, 0),
           "k22_serial_push_receive(&serial, K22_SERIAL_UART1, 0x22, 0)");
    k22_serial_advance(&serial, 320);
    expect(state, (read_register(state, &serial, base + 4, 1) & 0x08u) != 0,
           "(read_register(state, &serial, base + 4, 1) & 0x08u) != 0");
    expect(state, (read_register(state, &serial, base + 0x12, 1) & 0x04u) != 0,
           "(read_register(state, &serial, base + 0x12, 1) & 0x04u) != 0");
    write_register(state, &serial, base + 0x12, 1, 0x04u);
    write_register(state, &serial, base + 0x11, 1, 0x40u);
    expect(state, read_register(state, &serial, base + 0x16, 1) == 0,
           "read_register(state, &serial, base + 0x16, 1) == 0");

    write_register(state, &serial, base + 0x10, 1, 0x88u);
    for (uint16_t value = 0; value < 8; value++)
        expect(state, k22_serial_push_receive(&serial, K22_SERIAL_UART1, value, 0),
               "k22_serial_push_receive(&serial, K22_SERIAL_UART1, value, 0)");
    k22_serial_advance(&serial, 1280);
    expect(state, read_register(state, &serial, base + 0x16, 1) == 1,
           "read_register(state, &serial, base + 0x16, 1) == 1");

    K22Serial copy;
    expect(state, k22_serial_copy(&copy, &serial), "k22_serial_copy(&copy, &serial)");
    expect(state, memcmp(&copy, &serial, sizeof(serial)) == 0,
           "memcmp(&copy, &serial, sizeof(serial)) == 0");
    k22_serial_reset(&copy);
    expect(state, !copy.uart[1].clock_enabled, "!copy.uart[1].clock_enabled");
    expect(state, copy.uart[1].receive.count == 0, "copy.uart[1].receive.count == 0");
    expect(state, serial.uart[1].receive.count != 0, "serial.uart[1].receive.count != 0");
}

static void test_lpuart(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_LPUART0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_LPUART0, true)");
    expect(state, read_register(state, &serial, LPUART0_BASE, 4) == 0x0f000004u,
           "read_register(state, &serial, LPUART0_BASE, 4) == 0x0f000004u");
    expect(state, read_register(state, &serial, LPUART0_BASE + 4, 4) == 0x00c00000u,
           "read_register(state, &serial, LPUART0_BASE + 4, 4) == 0x00c00000u");
    write_register(state, &serial, LPUART0_BASE, 4, 0x0f200001u);
    write_register(state, &serial, LPUART0_BASE + 8, 4, (1u << 18) | (1u << 19) | (1u << 21));
    expect(state, k22_serial_push_receive(&serial, K22_SERIAL_LPUART0, 0x155u, 1),
           "k22_serial_push_receive(&serial, K22_SERIAL_LPUART0, 0x155u, 1)");
    k22_serial_advance(&serial, 160);
    expect(state, (read_register(state, &serial, LPUART0_BASE + 4, 4) & 0x00210000u) == 0x00210000u,
           "(read_register(state, &serial, LPUART0_BASE + 4, 4) & 0x00210000u) == "
           "0x00210000u");
    expect(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_LPUART0),
           "k22_serial_irq(&serial, K22_SERIAL_IRQ_LPUART0)");
    expect(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_LPUART0_RECEIVE),
           "k22_serial_dma_request(&serial, K22_SERIAL_DMA_LPUART0_RECEIVE)");
    expect(state, (read_register(state, &serial, LPUART0_BASE + 0x0c, 4) & 0x3ffu) == 0x155u,
           "(read_register(state, &serial, LPUART0_BASE + 0x0c, 4) & 0x3ffu) == 0x155u");
    write_register(state, &serial, LPUART0_BASE + 4, 4, 0x00010000u);
    expect(state, (read_register(state, &serial, LPUART0_BASE + 4, 4) & 0x00010000u) == 0,
           "(read_register(state, &serial, LPUART0_BASE + 4, 4) & 0x00010000u) == 0");
}

static void test_spi_transfer_fifo_interrupt_dma_and_errors(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI0, true)");
    expect(state, read_register(state, &serial, SPI0_BASE, 4) == 0x00004001u,
           "read_register(state, &serial, SPI0_BASE, 4) == 0x00004001u");
    write_register(state, &serial, SPI0_BASE, 4, 0);
    write_register(state, &serial, SPI0_BASE + 0x30, 4, 3u << 16);
    expect(state, k22_serial_push_receive(&serial, K22_SERIAL_SPI0, 0x1234u, 0),
           "k22_serial_push_receive(&serial, K22_SERIAL_SPI0, 0x1234u, 0)");
    write_register(state, &serial, SPI0_BASE + 0x34, 4, 0xabcdu);
    k22_serial_advance(&serial, 63);
    expect(state, (read_register(state, &serial, SPI0_BASE + 0x2c, 4) & (1u << 17)) == 0,
           "(read_register(state, &serial, SPI0_BASE + 0x2c, 4) & (1u << 17)) == 0");
    k22_serial_advance(&serial, 1);
    expect(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_SPI0_RECEIVE),
           "k22_serial_dma_request(&serial, K22_SERIAL_DMA_SPI0_RECEIVE)");
    expect(state, !k22_serial_irq(&serial, K22_SERIAL_IRQ_SPI0),
           "!k22_serial_irq(&serial, K22_SERIAL_IRQ_SPI0)");
    expect(state, read_register(state, &serial, SPI0_BASE + 0x38, 4) == 0x1234u,
           "read_register(state, &serial, SPI0_BASE + 0x38, 4) == 0x1234u");
    uint16_t transmitted;
    expect(state, k22_serial_pop_transmit(&serial, K22_SERIAL_SPI0, &transmitted),
           "k22_serial_pop_transmit(&serial, K22_SERIAL_SPI0, &transmitted)");
    expect(state, transmitted == 0xabcdu, "transmitted == 0xabcdu");

    write_register(state, &serial, SPI0_BASE + 0x30, 4, 1u << 17);
    expect(state, k22_serial_push_receive(&serial, K22_SERIAL_SPI0, 0x5678u, 0),
           "k22_serial_push_receive(&serial, K22_SERIAL_SPI0, 0x5678u, 0)");
    write_register(state, &serial, SPI0_BASE + 0x34, 4, 0x90030011u);
    k22_serial_advance(&serial, 64);
    expect(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_SPI0),
           "k22_serial_irq(&serial, K22_SERIAL_IRQ_SPI0)");
    expect(state, read_register(state, &serial, SPI0_BASE + 0x38, 4) == 0x5678u,
           "read_register(state, &serial, SPI0_BASE + 0x38, 4) == 0x5678u");
    K22SerialSpiTransfer transfer;
    expect(state, k22_serial_pop_spi_transfer(&serial, K22_SERIAL_SPI0, &transfer),
           "k22_serial_pop_spi_transfer(&serial, K22_SERIAL_SPI0, &transfer)");
    expect(state, transfer.data == 0x11u, "transfer.data == 0x11u");
    expect(state, transfer.chip_selects == 3u, "transfer.chip_selects == 3u");
    expect(state, transfer.clock_and_transfer_attributes == 1u,
           "transfer.clock_and_transfer_attributes == 1u");
    expect(state, transfer.continuous_chip_select, "transfer.continuous_chip_select");
    expect(state, !transfer.end_of_queue, "!transfer.end_of_queue");
    expect(state, read_register(state, &serial, SPI0_BASE + 0x38, 4) == 0x5678u,
           "read_register(state, &serial, SPI0_BASE + 0x38, 4) == 0x5678u");
    expect(state, (read_register(state, &serial, SPI0_BASE + 0x2c, 4) & (1u << 19)) != 0,
           "(read_register(state, &serial, SPI0_BASE + 0x2c, 4) & (1u << 19)) != 0");
    write_register(state, &serial, SPI0_BASE + 0x2c, 4, 1u << 19);
    expect(state, (read_register(state, &serial, SPI0_BASE + 0x2c, 4) & (1u << 19)) == 0,
           "(read_register(state, &serial, SPI0_BASE + 0x2c, 4) & (1u << 19)) == 0");

    write_register(state, &serial, SPI0_BASE, 4, 1u << 11);
    expect(state, serial.spi[0].transmit.count == 0, "serial.spi[0].transmit.count == 0");
    expect(state, (read_register(state, &serial, SPI0_BASE, 4) & (1u << 11)) == 0,
           "(read_register(state, &serial, SPI0_BASE, 4) & (1u << 11)) == 0");
}

static void test_spi_profile_presence(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN1M012);
    uint32_t value;
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI1, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI1, true)");
    expect(state, k22_serial_read(&serial, SPI1_BASE, 4, &value),
           "k22_serial_read(&serial, SPI1_BASE, 4, &value)");
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI2, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI2, true)");
    expect(state, k22_serial_read(&serial, 0x400ac000u, 4, &value),
           "k22_serial_read(&serial, 0x400ac000u, 4, &value)");
}

static void test_i2c_master_events_timing_irq_and_dma(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C0, true)");
    write_register(state, &serial, I2C0_BASE + 2, 1, 0xf1u);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_START, 0);
    write_register(state, &serial, I2C0_BASE + 4, 1, 0xa4u);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_WRITE, 0xa4u);
    k22_serial_advance(&serial, 179);
    expect(state, (read_register(state, &serial, I2C0_BASE + 3, 1) & 2u) == 0,
           "(read_register(state, &serial, I2C0_BASE + 3, 1) & 2u) == 0");
    k22_serial_advance(&serial, 1);
    expect(state, (read_register(state, &serial, I2C0_BASE + 3, 1) & 0x83u) == 0x82u,
           "(read_register(state, &serial, I2C0_BASE + 3, 1) & 0x83u) == 0x82u");
    expect(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_I2C0),
           "k22_serial_irq(&serial, K22_SERIAL_IRQ_I2C0)");
    expect(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_I2C0),
           "k22_serial_dma_request(&serial, K22_SERIAL_DMA_I2C0)");
    write_register(state, &serial, I2C0_BASE + 3, 1, 2u);
    expect(state, !k22_serial_irq(&serial, K22_SERIAL_IRQ_I2C0),
           "!k22_serial_irq(&serial, K22_SERIAL_IRQ_I2C0)");

    expect(state, k22_serial_i2c_set_acknowledge(&serial, K22_SERIAL_I2C0, false),
           "k22_serial_i2c_set_acknowledge(&serial, K22_SERIAL_I2C0, false)");
    write_register(state, &serial, I2C0_BASE + 4, 1, 0xa5u);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_WRITE, 0xa5u);
    k22_serial_advance(&serial, 180);
    expect(state, (read_register(state, &serial, I2C0_BASE + 3, 1) & 1u) != 0,
           "(read_register(state, &serial, I2C0_BASE + 3, 1) & 1u) != 0");
    write_register(state, &serial, I2C0_BASE + 2, 1, 0xf5u);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_REPEATED_START, 0);
    write_register(state, &serial, I2C0_BASE + 2, 1, 0xe1u);
    expect(state, k22_serial_push_receive(&serial, K22_SERIAL_I2C0, 0x5au, 0),
           "k22_serial_push_receive(&serial, K22_SERIAL_I2C0, 0x5au, 0)");
    read_register(state, &serial, I2C0_BASE + 4, 1);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_READ, 0);
    k22_serial_advance(&serial, 180);
    expect(state, read_register(state, &serial, I2C0_BASE + 4, 1) == 0x5au,
           "read_register(state, &serial, I2C0_BASE + 4, 1) == 0x5au");
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_READ, 0);
    write_register(state, &serial, I2C0_BASE + 2, 1, 0xc1u);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_STOP, 0);
    write_register(state, &serial, I2C0_BASE + 2, 1, 0xe1u);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_START, 0);
    write_register(state, &serial, I2C0_BASE + 2, 1, 0u);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_STOP, 0);
}

static void test_i2c_arbitration_disable_slave_and_reset(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN1M012);
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C2, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C2, true)");
    write_register(state, &serial, I2C2_BASE, 1, 0x52u);
    write_register(state, &serial, I2C2_BASE + 2, 1, 0xc0u);
    expect(state, k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C2, 0x29u, true),
           "k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C2, 0x29u, true)");
    expect(state, (read_register(state, &serial, I2C2_BASE + 3, 1) & 0x46u) == 0x46u,
           "(read_register(state, &serial, I2C2_BASE + 3, 1) & 0x46u) == 0x46u");
    expect(state, !k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C2, 0x2au, false),
           "!k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C2, 0x2au, false)");
    write_register(state, &serial, I2C2_BASE + 4, 1, 0x9au);
    uint16_t slave_transmit;
    expect(state, k22_serial_pop_transmit(&serial, K22_SERIAL_I2C2, &slave_transmit),
           "k22_serial_pop_transmit(&serial, K22_SERIAL_I2C2, &slave_transmit)");
    expect(state, slave_transmit == 0x9au, "slave_transmit == 0x9au");

    expect(state, k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C2, 0x29u, false),
           "k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C2, 0x29u, false)");
    expect(state, k22_serial_push_receive(&serial, K22_SERIAL_I2C2, 0x6bu, 0),
           "k22_serial_push_receive(&serial, K22_SERIAL_I2C2, 0x6bu, 0)");
    expect(state, read_register(state, &serial, I2C2_BASE + 4, 1) == 0x6bu,
           "read_register(state, &serial, I2C2_BASE + 4, 1) == 0x6bu");

    write_register(state, &serial, I2C2_BASE + 2, 1, 0xf0u);
    expect_event(state, &serial, K22_SERIAL_I2C2, K22_SERIAL_EVENT_I2C_START, 0);
    expect(state, k22_serial_i2c_lose_arbitration(&serial, K22_SERIAL_I2C2),
           "k22_serial_i2c_lose_arbitration(&serial, K22_SERIAL_I2C2)");
    expect(state, (read_register(state, &serial, I2C2_BASE + 3, 1) & 0x12u) == 0x12u,
           "(read_register(state, &serial, I2C2_BASE + 3, 1) & 0x12u) == 0x12u");
    expect(state, (read_register(state, &serial, I2C2_BASE + 2, 1) & 0x20u) == 0,
           "(read_register(state, &serial, I2C2_BASE + 2, 1) & 0x20u) == 0");
    write_register(state, &serial, I2C2_BASE + 3, 1, 0x12u);
    expect(state, (read_register(state, &serial, I2C2_BASE + 3, 1) & 0x12u) == 0,
           "(read_register(state, &serial, I2C2_BASE + 3, 1) & 0x12u) == 0");
    write_register(state, &serial, I2C2_BASE + 2, 1, 0);
    expect(state, read_register(state, &serial, I2C2_BASE + 3, 1) == 0,
           "read_register(state, &serial, I2C2_BASE + 3, 1) == 0");
    k22_serial_reset(&serial);
    expect(state, serial.i2c[2].acknowledge, "serial.i2c[2].acknowledge");
    expect(state, !serial.i2c[2].clock_enabled, "!serial.i2c[2].clock_enabled");
}

static void test_register_edge_paths(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    k22_serial_set_clocks(&serial, 96000000u, 48000000u);
    expect(state, serial.core_clock_hz == 96000000u, "serial.core_clock_hz == 96000000u");
    expect(state, serial.bus_clock_hz == 48000000u, "serial.bus_clock_hz == 48000000u");
    k22_serial_set_clocks(NULL, 0, 0);
    k22_serial_reset(NULL);
    k22_serial_advance(NULL, 1);

    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, true)");
    expect(state, read_register(state, &serial, UART0_BASE + 7, 1) == 0,
           "read_register(state, &serial, UART0_BASE + 7, 1) == 0");
    write_register(state, &serial, UART0_BASE + 4, 1, 0x1fu);
    write_register(state, &serial, UART0_BASE + 5, 1, 0xffu);
    write_register(state, &serial, UART0_BASE + 0x10, 1, 0x88u);
    for (uint16_t value = 0; value < 9; value++)
        write_register(state, &serial, UART0_BASE + 7, 1, value);
    expect(state, (read_register(state, &serial, UART0_BASE + 0x12, 1) & 0x02u) != 0,
           "(read_register(state, &serial, UART0_BASE + 0x12, 1) & 0x02u) != 0");
    write_register(state, &serial, UART0_BASE + 0x11, 1, 0x80u);
    expect(state, read_register(state, &serial, UART0_BASE + 0x14, 1) == 0,
           "read_register(state, &serial, UART0_BASE + 0x14, 1) == 0");
    uint32_t ignored;
    expect(state, !k22_serial_read(&serial, UART0_BASE, 4, &ignored),
           "!k22_serial_read(&serial, UART0_BASE, 4, &ignored)");
    expect(state, !k22_serial_write(&serial, UART0_BASE, 2, 0),
           "!k22_serial_write(&serial, UART0_BASE, 2, 0)");

    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_LPUART0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_LPUART0, true)");
    expect(state, read_register(state, &serial, LPUART0_BASE + 0x0c, 4) == 0x1000u,
           "read_register(state, &serial, LPUART0_BASE + 0x0c, 4) == 0x1000u");
    write_register(state, &serial, LPUART0_BASE, 4, 0x0f000001u);
    write_register(state, &serial, LPUART0_BASE + 8, 4, 1u << 19);
    write_register(state, &serial, LPUART0_BASE + 0x0c, 4, 0x11u);
    write_register(state, &serial, LPUART0_BASE + 0x0c, 4, 0x22u);
    expect(state, (read_register(state, &serial, LPUART0_BASE + 4, 4) & (1u << 19)) != 0,
           "(read_register(state, &serial, LPUART0_BASE + 4, 4) & (1u << 19)) != 0");
    k22_serial_advance(&serial, 160);
    uint16_t output;
    expect(state, k22_serial_pop_transmit(&serial, K22_SERIAL_LPUART0, &output),
           "k22_serial_pop_transmit(&serial, K22_SERIAL_LPUART0, &output)");
    expect(state, output == 0x11u, "output == 0x11u");
    expect(state, !k22_serial_read(&serial, LPUART0_BASE + 1, 1, &ignored),
           "!k22_serial_read(&serial, LPUART0_BASE + 1, 1, &ignored)");

    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI0, true)");
    write_register(state, &serial, SPI0_BASE, 4, 0);
    for (uint32_t value = 0; value < 5; value++)
        write_register(state, &serial, SPI0_BASE + 0x34, 4, value);
    expect(state, (read_register(state, &serial, SPI0_BASE + 0x2c, 4) & (1u << 27)) != 0,
           "(read_register(state, &serial, SPI0_BASE + 0x2c, 4) & (1u << 27)) != 0");
    write_register(state, &serial, SPI0_BASE, 4, (1u << 10) | (1u << 11));
    expect(state, serial.spi[0].receive.count == 0, "serial.spi[0].receive.count == 0");
    expect(state, serial.spi[0].transmit.count == 0, "serial.spi[0].transmit.count == 0");
    write_register(state, &serial, SPI0_BASE + 0x0c, 4, 0xb8000000u);
    write_register(state, &serial, SPI0_BASE, 4, 0);
    for (uint16_t value = 0; value < 5; value++) {
        expect(state, k22_serial_push_receive(&serial, K22_SERIAL_SPI0, value, 0),
               "k22_serial_push_receive(&serial, K22_SERIAL_SPI0, value, 0)");
        write_register(state, &serial, SPI0_BASE + 0x34, 4,
                       value == 4 ? (1u << 27) | value : value);
        k22_serial_advance(&serial, 16);
    }
    uint32_t spi_status = read_register(state, &serial, SPI0_BASE + 0x2c, 4);
    expect(state, (spi_status & (1u << 19)) != 0, "(spi_status & (1u << 19)) != 0");
    expect(state, (spi_status & (1u << 28)) != 0, "(spi_status & (1u << 28)) != 0");
    for (size_t index = 0; index < 4; index++)
        expect(state, k22_serial_pop_transmit(&serial, K22_SERIAL_SPI0, &output),
               "k22_serial_pop_transmit(&serial, K22_SERIAL_SPI0, &output)");
    K22SerialSpiTransfer transfer;
    expect(state, k22_serial_pop_spi_transfer(&serial, K22_SERIAL_SPI0, &transfer),
           "k22_serial_pop_spi_transfer(&serial, K22_SERIAL_SPI0, &transfer)");
    expect(state, transfer.data == 4, "transfer.data == 4");
    expect(state, transfer.chip_selects == 0, "transfer.chip_selects == 0");
    expect(state, transfer.clock_and_transfer_attributes == 0,
           "transfer.clock_and_transfer_attributes == 0");
    expect(state, !transfer.continuous_chip_select, "!transfer.continuous_chip_select");
    expect(state, transfer.end_of_queue, "transfer.end_of_queue");
    expect(state, !k22_serial_read(&serial, SPI0_BASE, 1, &ignored),
           "!k22_serial_read(&serial, SPI0_BASE, 1, &ignored)");
    expect(state, !k22_serial_write(&serial, SPI0_BASE + 2, 4, 0),
           "!k22_serial_write(&serial, SPI0_BASE + 2, 4, 0)");

    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C0, true)");
    write_register(state, &serial, I2C0_BASE + 6, 1, 0xffu);
    expect(state, (read_register(state, &serial, I2C0_BASE + 6, 1) & 0x1fu) == 0x1fu,
           "(read_register(state, &serial, I2C0_BASE + 6, 1) & 0x1fu) == 0x1fu");
    expect(state, !k22_serial_read(&serial, I2C0_BASE, 2, &ignored),
           "!k22_serial_read(&serial, I2C0_BASE, 2, &ignored)");
    expect(state, !k22_serial_write(&serial, I2C0_BASE, 4, 0),
           "!k22_serial_write(&serial, I2C0_BASE, 4, 0)");
    expect(state, !k22_serial_i2c_set_acknowledge(&serial, K22_SERIAL_UART0, true),
           "!k22_serial_i2c_set_acknowledge(&serial, K22_SERIAL_UART0, true)");
    expect(state, !k22_serial_i2c_lose_arbitration(&serial, K22_SERIAL_I2C1),
           "!k22_serial_i2c_lose_arbitration(&serial, K22_SERIAL_I2C1)");
    expect(state, !k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C1, 0, false),
           "!k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C1, 0, false)");
}

static void test_event_capacity(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C0, true)");
    for (size_t index = 0; index < 33; index++) {
        write_register(state, &serial, I2C0_BASE + 2, 1, 0xf0u);
        write_register(state, &serial, I2C0_BASE + 2, 1, 0xd0u);
    }
    expect(state, serial.event_count == K22_SERIAL_EVENT_CAPACITY,
           "serial.event_count == K22_SERIAL_EVENT_CAPACITY");
    K22SerialEvent event;
    expect(state, k22_serial_pop_event(&serial, &event), "k22_serial_pop_event(&serial, &event)");
    expect(state, event.type == K22_SERIAL_EVENT_I2C_START,
           "event.type == K22_SERIAL_EVENT_I2C_START");
}

static void test_signal_and_overflow_matrix(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    expect(state, !k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_ADC0, true),
           "!k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_ADC0, true)");
    expect(state, !k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0),
           "!k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0)");
    expect(state, !k22_serial_dma_request(&serial, K22_SERIAL_DMA_UART0_TRANSMIT),
           "!k22_serial_dma_request(&serial, K22_SERIAL_DMA_UART0_TRANSMIT)");
    expect(state, !k22_serial_dma_request(&serial, K22_SERIAL_DMA_LPUART0_TRANSMIT),
           "!k22_serial_dma_request(&serial, K22_SERIAL_DMA_LPUART0_TRANSMIT)");
    expect(state, !k22_serial_dma_request(&serial, K22_SERIAL_DMA_SPI0_TRANSMIT),
           "!k22_serial_dma_request(&serial, K22_SERIAL_DMA_SPI0_TRANSMIT)");

    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, true)");
    write_register(state, &serial, UART0_BASE + 0x0b, 1, 0x80u);
    expect(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_UART0_TRANSMIT),
           "k22_serial_dma_request(&serial, K22_SERIAL_DMA_UART0_TRANSMIT)");

    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_LPUART0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_LPUART0, true)");
    write_register(state, &serial, LPUART0_BASE, 4, 0x0f800001u);
    expect(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_LPUART0_TRANSMIT),
           "k22_serial_dma_request(&serial, K22_SERIAL_DMA_LPUART0_TRANSMIT)");
    write_register(state, &serial, LPUART0_BASE + 8, 4, (1u << 18) | (1u << 27));
    expect(state, k22_serial_push_receive(&serial, K22_SERIAL_LPUART0, 1, 8),
           "k22_serial_push_receive(&serial, K22_SERIAL_LPUART0, 1, 8)");
    expect(state, k22_serial_push_receive(&serial, K22_SERIAL_LPUART0, 2, 0),
           "k22_serial_push_receive(&serial, K22_SERIAL_LPUART0, 2, 0)");
    k22_serial_advance(&serial, 320);
    expect(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_LPUART0),
           "k22_serial_irq(&serial, K22_SERIAL_IRQ_LPUART0)");
    expect(state, (read_register(state, &serial, LPUART0_BASE + 4, 4) & (1u << 19)) != 0,
           "(read_register(state, &serial, LPUART0_BASE + 4, 4) & (1u << 19)) != 0");

    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI0, true)");
    write_register(state, &serial, SPI0_BASE + 0x30, 4, 3u << 24);
    expect(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_SPI0_TRANSMIT),
           "k22_serial_dma_request(&serial, K22_SERIAL_DMA_SPI0_TRANSMIT)");
    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI1, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI1, true)");
    write_register(state, &serial, SPI1_BASE + 0x30, 4, 1u << 25);
    expect(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_SPI1),
           "k22_serial_irq(&serial, K22_SERIAL_IRQ_SPI1)");

    expect(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C0, true),
           "k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C0, true)");
    write_register(state, &serial, I2C0_BASE, 1, 0x52u);
    write_register(state, &serial, I2C0_BASE + 2, 1, 0x80u);
    expect(state, k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C0, 0x29u, false),
           "k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C0, 0x29u, false)");
    for (uint16_t value = 0; value < K22_SERIAL_FIFO_CAPACITY; value++)
        expect(state, k22_serial_push_receive(&serial, K22_SERIAL_I2C0, value, 0),
               "k22_serial_push_receive(&serial, K22_SERIAL_I2C0, value, 0)");
    expect(state, !k22_serial_push_receive(&serial, K22_SERIAL_I2C0, 0xffu, 0),
           "!k22_serial_push_receive(&serial, K22_SERIAL_I2C0, 0xffu, 0)");

    uint32_t ignored;
    expect(state, !k22_serial_write(&serial, 0x50000000u, 1, 0),
           "!k22_serial_write(&serial, 0x50000000u, 1, 0)");
    expect(state, !k22_serial_read(&serial, 0x50000000u, 1, &ignored),
           "!k22_serial_read(&serial, 0x50000000u, 1, &ignored)");
}

static void test_invalid_operations(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    uint32_t value;
    uint16_t output;
    expect(state, !k22_serial_init(NULL, serial.profile), "!k22_serial_init(NULL, serial.profile)");
    expect(state, !k22_serial_init(&serial, NULL), "!k22_serial_init(&serial, NULL)");
    expect(state, !k22_serial_copy(NULL, &serial), "!k22_serial_copy(NULL, &serial)");
    expect(state, !k22_serial_copy(&serial, NULL), "!k22_serial_copy(&serial, NULL)");
    expect(state, !k22_serial_read(NULL, UART0_BASE, 1, &value),
           "!k22_serial_read(NULL, UART0_BASE, 1, &value)");
    expect(state, !k22_serial_read(&serial, UART0_BASE, 1, NULL),
           "!k22_serial_read(&serial, UART0_BASE, 1, NULL)");
    expect(state, !k22_serial_write(NULL, UART0_BASE, 1, 0),
           "!k22_serial_write(NULL, UART0_BASE, 1, 0)");
    expect(state, !k22_serial_read(&serial, 0x50000000u, 1, &value),
           "!k22_serial_read(&serial, 0x50000000u, 1, &value)");
    expect(state, !k22_serial_push_receive(NULL, K22_SERIAL_UART0, 0, 0),
           "!k22_serial_push_receive(NULL, K22_SERIAL_UART0, 0, 0)");
    expect(state, !k22_serial_push_receive(&serial, K22_SERIAL_ENDPOINT_INVALID, 0, 0),
           "!k22_serial_push_receive(&serial, K22_SERIAL_ENDPOINT_INVALID, 0, 0)");
    expect(state, !k22_serial_pop_transmit(NULL, K22_SERIAL_UART0, &output),
           "!k22_serial_pop_transmit(NULL, K22_SERIAL_UART0, &output)");
    expect(state, !k22_serial_pop_transmit(&serial, K22_SERIAL_UART0, NULL),
           "!k22_serial_pop_transmit(&serial, K22_SERIAL_UART0, NULL)");
    K22SerialSpiTransfer transfer;
    expect(state, !k22_serial_pop_spi_transfer(NULL, K22_SERIAL_SPI0, &transfer),
           "!k22_serial_pop_spi_transfer(NULL, K22_SERIAL_SPI0, &transfer)");
    expect(state, !k22_serial_pop_spi_transfer(&serial, K22_SERIAL_SPI0, NULL),
           "!k22_serial_pop_spi_transfer(&serial, K22_SERIAL_SPI0, NULL)");
    expect(state, !k22_serial_pop_spi_transfer(&serial, K22_SERIAL_UART0, &transfer),
           "!k22_serial_pop_spi_transfer(&serial, K22_SERIAL_UART0, &transfer)");
    expect(state, !k22_serial_pop_event(&serial, NULL), "!k22_serial_pop_event(&serial, NULL)");
    K22SerialEvent event;
    expect(state, !k22_serial_pop_event(&serial, &event), "!k22_serial_pop_event(&serial, &event)");
    expect(state, !k22_serial_i2c_set_acknowledge(NULL, K22_SERIAL_I2C0, true),
           "!k22_serial_i2c_set_acknowledge(NULL, K22_SERIAL_I2C0, true)");
    expect(state, !k22_serial_i2c_lose_arbitration(NULL, K22_SERIAL_I2C0),
           "!k22_serial_i2c_lose_arbitration(NULL, K22_SERIAL_I2C0)");
    expect(state, !k22_serial_i2c_slave_address(NULL, K22_SERIAL_I2C0, 0, false),
           "!k22_serial_i2c_slave_address(NULL, K22_SERIAL_I2C0, 0, false)");
    expect(state, !k22_serial_irq(&serial, K22_SERIAL_IRQ_COUNT),
           "!k22_serial_irq(&serial, K22_SERIAL_IRQ_COUNT)");
    expect(state, !k22_serial_dma_request(&serial, K22_SERIAL_DMA_COUNT),
           "!k22_serial_dma_request(&serial, K22_SERIAL_DMA_COUNT)");
}

int main(void) {
    TestState state = {0};
    test_profiles_and_gates(&state);
    test_uart_transfer_status_interrupt_and_dma(&state);
    test_uart_fifo_overrun_flush_and_copy(&state);
    test_lpuart(&state);
    test_spi_transfer_fifo_interrupt_dma_and_errors(&state);
    test_spi_profile_presence(&state);
    test_i2c_master_events_timing_irq_and_dma(&state);
    test_i2c_arbitration_disable_slave_and_reset(&state);
    test_register_edge_paths(&state);
    test_event_capacity(&state);
    test_signal_and_overflow_matrix(&state);
    test_invalid_operations(&state);
    return test_finish(&state);
}
