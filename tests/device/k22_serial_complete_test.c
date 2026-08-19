#include "k22_serial.h"
#include "test.h"

#include <string.h>

enum {
    LPUART0_BASE = 0x4002a000u,
    SPI0_BASE = 0x4002c000u,
    SPI1_BASE = 0x4002d000u,
    I2C0_BASE = 0x40066000u,
    I2C2_BASE = 0x400e6000u,
    UART0_BASE = 0x4006a000u,
};

static uint32_t read_register(TestState* state, K22Serial* serial, uint32_t address,
                              uint8_t size) {
    uint32_t value = 0;
    TEST_EXPECT(state, k22_serial_read(serial, address, size, &value));
    return value;
}

static void write_register(TestState* state, K22Serial* serial, uint32_t address,
                           uint8_t size, uint32_t value) {
    TEST_EXPECT(state, k22_serial_write(serial, address, size, value));
}

static K22Serial create_serial(TestState* state, K22ProfileId profile_id) {
    K22Serial serial;
    TEST_EXPECT(state, k22_serial_init(&serial, k22_profile_get(profile_id)));
    return serial;
}

static void expect_event(TestState* state, K22Serial* serial, K22SerialEndpoint endpoint,
                         K22SerialEventType type, uint16_t value) {
    K22SerialEvent event;
    TEST_EXPECT(state, k22_serial_pop_event(serial, &event));
    TEST_EXPECT(state, event.endpoint == endpoint);
    TEST_EXPECT(state, event.type == type);
    TEST_EXPECT(state, event.value == value);
}

static void test_profiles_and_gates(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    uint32_t value = 0;
    TEST_EXPECT(state, serial.lpuart0.present);
    TEST_EXPECT(state, serial.spi[1].present);
    TEST_EXPECT(state, !serial.i2c[2].present);
    TEST_EXPECT(state, !k22_serial_read(&serial, UART0_BASE + 4, 1, &value));
    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, true));
    TEST_EXPECT(state, read_register(state, &serial, UART0_BASE + 4, 1) == 0xc0u);
    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, false));
    TEST_EXPECT(state, !k22_serial_write(&serial, UART0_BASE + 3, 1, 0xffu));
    TEST_EXPECT(state, !k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C2, true));
    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART1, true));
    TEST_EXPECT(state,
                !k22_serial_read(&serial, serial.uart[1].base + serial.uart[1].block_size,
                                 1, &value));

    serial = create_serial(state, K22_PROFILE_MK22FN1M012);
    TEST_EXPECT(state, !serial.lpuart0.present);
    TEST_EXPECT(state, !serial.spi[1].present);
    TEST_EXPECT(state, serial.i2c[2].present);
    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C2, true));
    TEST_EXPECT(state, read_register(state, &serial, I2C2_BASE + 3, 1) == 0);
}

static void test_uart_transfer_status_interrupt_and_dma(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, true));
    write_register(state, &serial, UART0_BASE, 1, 0);
    write_register(state, &serial, UART0_BASE + 1, 1, 1);
    write_register(state, &serial, UART0_BASE + 3, 1, 0x2cu);
    write_register(state, &serial, UART0_BASE + 6, 1, 0x02u);
    write_register(state, &serial, UART0_BASE + 0x0b, 1, 0x20u);
    TEST_EXPECT(state, k22_serial_push_receive(&serial, K22_SERIAL_UART0, 0x15au, 0x02u));
    k22_serial_advance(&serial, 159);
    TEST_EXPECT(state, (read_register(state, &serial, UART0_BASE + 4, 1) & 0x20u) == 0);
    k22_serial_advance(&serial, 1);
    uint32_t status = read_register(state, &serial, UART0_BASE + 4, 1);
    TEST_EXPECT(state, (status & 0x22u) == 0x22u);
    TEST_EXPECT(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0));
    TEST_EXPECT(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0_ERROR));
    TEST_EXPECT(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_UART0_RECEIVE));
    TEST_EXPECT(state, read_register(state, &serial, UART0_BASE + 7, 1) == 0x5au);
    TEST_EXPECT(state, (read_register(state, &serial, UART0_BASE + 4, 1) & 0x2fu) == 0);
    TEST_EXPECT(state, !k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0));

    write_register(state, &serial, UART0_BASE + 7, 1, 0xa5u);
    k22_serial_advance(&serial, 159);
    uint16_t transmitted = 0;
    TEST_EXPECT(state, !k22_serial_pop_transmit(&serial, K22_SERIAL_UART0, &transmitted));
    k22_serial_advance(&serial, 1);
    TEST_EXPECT(state, k22_serial_pop_transmit(&serial, K22_SERIAL_UART0, &transmitted));
    TEST_EXPECT(state, transmitted == 0xa5u);
    TEST_EXPECT(state, (read_register(state, &serial, UART0_BASE + 4, 1) & 0xc0u) == 0xc0u);
}

static void test_uart_fifo_overrun_flush_and_copy(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART1, true));
    uint32_t base = serial.uart[1].base;
    write_register(state, &serial, base, 1, 0);
    write_register(state, &serial, base + 1, 1, 1);
    write_register(state, &serial, base + 3, 1, 0x04u);
    TEST_EXPECT(state, k22_serial_push_receive(&serial, K22_SERIAL_UART1, 0x11, 0));
    TEST_EXPECT(state, k22_serial_push_receive(&serial, K22_SERIAL_UART1, 0x22, 0));
    k22_serial_advance(&serial, 320);
    TEST_EXPECT(state, (read_register(state, &serial, base + 4, 1) & 0x08u) != 0);
    TEST_EXPECT(state, (read_register(state, &serial, base + 0x12, 1) & 0x04u) != 0);
    write_register(state, &serial, base + 0x12, 1, 0x04u);
    write_register(state, &serial, base + 0x11, 1, 0x40u);
    TEST_EXPECT(state, read_register(state, &serial, base + 0x16, 1) == 0);

    write_register(state, &serial, base + 0x10, 1, 0x88u);
    for (uint16_t value = 0; value < 8; value++)
        TEST_EXPECT(state, k22_serial_push_receive(&serial, K22_SERIAL_UART1, value, 0));
    k22_serial_advance(&serial, 1280);
    TEST_EXPECT(state, read_register(state, &serial, base + 0x16, 1) == 1);

    K22Serial copy;
    TEST_EXPECT(state, k22_serial_copy(&copy, &serial));
    TEST_EXPECT(state, memcmp(&copy, &serial, sizeof(serial)) == 0);
    k22_serial_reset(&copy);
    TEST_EXPECT(state, !copy.uart[1].clock_enabled);
    TEST_EXPECT(state, copy.uart[1].receive.count == 0);
    TEST_EXPECT(state, serial.uart[1].receive.count != 0);
}

static void test_lpuart(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_LPUART0, true));
    TEST_EXPECT(state, read_register(state, &serial, LPUART0_BASE, 4) == 0x0f000004u);
    TEST_EXPECT(state, read_register(state, &serial, LPUART0_BASE + 4, 4) == 0x00c00000u);
    write_register(state, &serial, LPUART0_BASE, 4, 0x0f200001u);
    write_register(state, &serial, LPUART0_BASE + 8, 4,
                   (1u << 18) | (1u << 19) | (1u << 21));
    TEST_EXPECT(state, k22_serial_push_receive(&serial, K22_SERIAL_LPUART0, 0x155u, 1));
    k22_serial_advance(&serial, 160);
    TEST_EXPECT(state, (read_register(state, &serial, LPUART0_BASE + 4, 4) & 0x00210000u) ==
                           0x00210000u);
    TEST_EXPECT(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_LPUART0));
    TEST_EXPECT(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_LPUART0_RECEIVE));
    TEST_EXPECT(state,
                (read_register(state, &serial, LPUART0_BASE + 0x0c, 4) & 0x3ffu) == 0x155u);
    write_register(state, &serial, LPUART0_BASE + 4, 4, 0x00010000u);
    TEST_EXPECT(state,
                (read_register(state, &serial, LPUART0_BASE + 4, 4) & 0x00010000u) == 0);
}

static void test_spi_transfer_fifo_interrupt_dma_and_errors(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI0, true));
    TEST_EXPECT(state, read_register(state, &serial, SPI0_BASE, 4) == 0x00004001u);
    write_register(state, &serial, SPI0_BASE, 4, 0);
    write_register(state, &serial, SPI0_BASE + 0x30, 4, 3u << 16);
    TEST_EXPECT(state, k22_serial_push_receive(&serial, K22_SERIAL_SPI0, 0x1234u, 0));
    write_register(state, &serial, SPI0_BASE + 0x34, 4, 0xabcdu);
    k22_serial_advance(&serial, 63);
    TEST_EXPECT(state,
                (read_register(state, &serial, SPI0_BASE + 0x2c, 4) & (1u << 17)) == 0);
    k22_serial_advance(&serial, 1);
    TEST_EXPECT(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_SPI0_RECEIVE));
    TEST_EXPECT(state, !k22_serial_irq(&serial, K22_SERIAL_IRQ_SPI0));
    TEST_EXPECT(state, read_register(state, &serial, SPI0_BASE + 0x38, 4) == 0x1234u);
    uint16_t transmitted;
    TEST_EXPECT(state, k22_serial_pop_transmit(&serial, K22_SERIAL_SPI0, &transmitted));
    TEST_EXPECT(state, transmitted == 0xabcdu);

    write_register(state, &serial, SPI0_BASE + 0x30, 4, 1u << 17);
    TEST_EXPECT(state, k22_serial_push_receive(&serial, K22_SERIAL_SPI0, 0x5678u, 0));
    write_register(state, &serial, SPI0_BASE + 0x34, 4, 0x90030011u);
    k22_serial_advance(&serial, 64);
    TEST_EXPECT(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_SPI0));
    TEST_EXPECT(state, read_register(state, &serial, SPI0_BASE + 0x38, 4) == 0x5678u);
    K22SerialSpiTransfer transfer;
    TEST_EXPECT(state, k22_serial_pop_spi_transfer(&serial, K22_SERIAL_SPI0, &transfer));
    TEST_EXPECT(state, transfer.data == 0x11u);
    TEST_EXPECT(state, transfer.chip_selects == 3u);
    TEST_EXPECT(state, transfer.clock_and_transfer_attributes == 1u);
    TEST_EXPECT(state, transfer.continuous_chip_select);
    TEST_EXPECT(state, !transfer.end_of_queue);
    TEST_EXPECT(state, read_register(state, &serial, SPI0_BASE + 0x38, 4) == 0x5678u);
    TEST_EXPECT(state,
                (read_register(state, &serial, SPI0_BASE + 0x2c, 4) & (1u << 19)) != 0);
    write_register(state, &serial, SPI0_BASE + 0x2c, 4, 1u << 19);
    TEST_EXPECT(state,
                (read_register(state, &serial, SPI0_BASE + 0x2c, 4) & (1u << 19)) == 0);

    write_register(state, &serial, SPI0_BASE, 4, 1u << 11);
    TEST_EXPECT(state, serial.spi[0].transmit.count == 0);
    TEST_EXPECT(state, (read_register(state, &serial, SPI0_BASE, 4) & (1u << 11)) == 0);
}

static void test_spi_profile_presence(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN1M012);
    uint32_t value;
    TEST_EXPECT(state, !k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI1, true));
    TEST_EXPECT(state, !k22_serial_read(&serial, SPI1_BASE, 4, &value));
}

static void test_i2c_master_events_timing_irq_and_dma(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C0, true));
    write_register(state, &serial, I2C0_BASE + 2, 1, 0xf1u);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_START, 0);
    write_register(state, &serial, I2C0_BASE + 4, 1, 0xa4u);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_WRITE, 0xa4u);
    k22_serial_advance(&serial, 179);
    TEST_EXPECT(state, (read_register(state, &serial, I2C0_BASE + 3, 1) & 2u) == 0);
    k22_serial_advance(&serial, 1);
    TEST_EXPECT(state, (read_register(state, &serial, I2C0_BASE + 3, 1) & 0x83u) == 0x82u);
    TEST_EXPECT(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_I2C0));
    TEST_EXPECT(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_I2C0));
    write_register(state, &serial, I2C0_BASE + 3, 1, 2u);
    TEST_EXPECT(state, !k22_serial_irq(&serial, K22_SERIAL_IRQ_I2C0));

    TEST_EXPECT(state, k22_serial_i2c_set_acknowledge(&serial, K22_SERIAL_I2C0, false));
    write_register(state, &serial, I2C0_BASE + 4, 1, 0xa5u);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_WRITE, 0xa5u);
    k22_serial_advance(&serial, 180);
    TEST_EXPECT(state, (read_register(state, &serial, I2C0_BASE + 3, 1) & 1u) != 0);
    write_register(state, &serial, I2C0_BASE + 2, 1, 0xf5u);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_REPEATED_START, 0);
    write_register(state, &serial, I2C0_BASE + 2, 1, 0xe1u);
    TEST_EXPECT(state, k22_serial_push_receive(&serial, K22_SERIAL_I2C0, 0x5au, 0));
    read_register(state, &serial, I2C0_BASE + 4, 1);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_READ, 0);
    k22_serial_advance(&serial, 180);
    TEST_EXPECT(state, read_register(state, &serial, I2C0_BASE + 4, 1) == 0x5au);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_READ, 0);
    write_register(state, &serial, I2C0_BASE + 2, 1, 0xc1u);
    expect_event(state, &serial, K22_SERIAL_I2C0, K22_SERIAL_EVENT_I2C_STOP, 0);
}

static void test_i2c_arbitration_disable_slave_and_reset(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN1M012);
    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C2, true));
    write_register(state, &serial, I2C2_BASE, 1, 0x52u);
    write_register(state, &serial, I2C2_BASE + 2, 1, 0xc0u);
    TEST_EXPECT(state, k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C2, 0x29u, true));
    TEST_EXPECT(state, (read_register(state, &serial, I2C2_BASE + 3, 1) & 0x46u) == 0x46u);
    TEST_EXPECT(state,
                !k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C2, 0x2au, false));
    write_register(state, &serial, I2C2_BASE + 4, 1, 0x9au);
    uint16_t slave_transmit;
    TEST_EXPECT(state, k22_serial_pop_transmit(&serial, K22_SERIAL_I2C2, &slave_transmit));
    TEST_EXPECT(state, slave_transmit == 0x9au);

    TEST_EXPECT(state,
                k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C2, 0x29u, false));
    TEST_EXPECT(state, k22_serial_push_receive(&serial, K22_SERIAL_I2C2, 0x6bu, 0));
    TEST_EXPECT(state, read_register(state, &serial, I2C2_BASE + 4, 1) == 0x6bu);

    write_register(state, &serial, I2C2_BASE + 2, 1, 0xf0u);
    expect_event(state, &serial, K22_SERIAL_I2C2, K22_SERIAL_EVENT_I2C_START, 0);
    TEST_EXPECT(state, k22_serial_i2c_lose_arbitration(&serial, K22_SERIAL_I2C2));
    TEST_EXPECT(state, (read_register(state, &serial, I2C2_BASE + 3, 1) & 0x12u) == 0x12u);
    TEST_EXPECT(state, (read_register(state, &serial, I2C2_BASE + 2, 1) & 0x20u) == 0);
    write_register(state, &serial, I2C2_BASE + 3, 1, 0x12u);
    TEST_EXPECT(state, (read_register(state, &serial, I2C2_BASE + 3, 1) & 0x12u) == 0);
    write_register(state, &serial, I2C2_BASE + 2, 1, 0);
    TEST_EXPECT(state, read_register(state, &serial, I2C2_BASE + 3, 1) == 0);
    k22_serial_reset(&serial);
    TEST_EXPECT(state, serial.i2c[2].acknowledge);
    TEST_EXPECT(state, !serial.i2c[2].clock_enabled);
}

static void test_register_edge_paths(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    k22_serial_set_clocks(&serial, 96000000u, 48000000u);
    TEST_EXPECT(state, serial.core_clock_hz == 96000000u);
    TEST_EXPECT(state, serial.bus_clock_hz == 48000000u);
    k22_serial_set_clocks(NULL, 0, 0);
    k22_serial_reset(NULL);
    k22_serial_advance(NULL, 1);

    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, true));
    TEST_EXPECT(state, read_register(state, &serial, UART0_BASE + 7, 1) == 0);
    write_register(state, &serial, UART0_BASE + 4, 1, 0x1fu);
    write_register(state, &serial, UART0_BASE + 5, 1, 0xffu);
    write_register(state, &serial, UART0_BASE + 0x10, 1, 0x88u);
    for (uint16_t value = 0; value < 9; value++)
        write_register(state, &serial, UART0_BASE + 7, 1, value);
    TEST_EXPECT(state, (read_register(state, &serial, UART0_BASE + 0x12, 1) & 0x02u) != 0);
    write_register(state, &serial, UART0_BASE + 0x11, 1, 0x80u);
    TEST_EXPECT(state, read_register(state, &serial, UART0_BASE + 0x14, 1) == 0);
    uint32_t ignored;
    TEST_EXPECT(state, !k22_serial_read(&serial, UART0_BASE, 4, &ignored));
    TEST_EXPECT(state, !k22_serial_write(&serial, UART0_BASE, 2, 0));

    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_LPUART0, true));
    TEST_EXPECT(state, read_register(state, &serial, LPUART0_BASE + 0x0c, 4) == 0);
    write_register(state, &serial, LPUART0_BASE, 4, 0x0f000001u);
    write_register(state, &serial, LPUART0_BASE + 8, 4, 1u << 19);
    write_register(state, &serial, LPUART0_BASE + 0x0c, 4, 0x11u);
    write_register(state, &serial, LPUART0_BASE + 0x0c, 4, 0x22u);
    TEST_EXPECT(state,
                (read_register(state, &serial, LPUART0_BASE + 4, 4) & (1u << 19)) != 0);
    k22_serial_advance(&serial, 160);
    uint16_t output;
    TEST_EXPECT(state, k22_serial_pop_transmit(&serial, K22_SERIAL_LPUART0, &output));
    TEST_EXPECT(state, output == 0x11u);
    TEST_EXPECT(state, !k22_serial_read(&serial, LPUART0_BASE + 1, 1, &ignored));

    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI0, true));
    write_register(state, &serial, SPI0_BASE, 4, 0);
    for (uint32_t value = 0; value < 5; value++)
        write_register(state, &serial, SPI0_BASE + 0x34, 4, value);
    TEST_EXPECT(state,
                (read_register(state, &serial, SPI0_BASE + 0x2c, 4) & (1u << 27)) != 0);
    write_register(state, &serial, SPI0_BASE, 4, (1u << 10) | (1u << 11));
    TEST_EXPECT(state, serial.spi[0].receive.count == 0);
    TEST_EXPECT(state, serial.spi[0].transmit.count == 0);
    write_register(state, &serial, SPI0_BASE + 0x0c, 4, 0xb8000000u);
    write_register(state, &serial, SPI0_BASE, 4, 0);
    for (uint16_t value = 0; value < 5; value++) {
        TEST_EXPECT(state, k22_serial_push_receive(&serial, K22_SERIAL_SPI0, value, 0));
        write_register(state, &serial, SPI0_BASE + 0x34, 4,
                       value == 4 ? (1u << 27) | value : value);
        k22_serial_advance(&serial, 16);
    }
    uint32_t spi_status = read_register(state, &serial, SPI0_BASE + 0x2c, 4);
    TEST_EXPECT(state, (spi_status & (1u << 19)) != 0);
    TEST_EXPECT(state, (spi_status & (1u << 28)) != 0);
    for (size_t index = 0; index < 4; index++)
        TEST_EXPECT(state, k22_serial_pop_transmit(&serial, K22_SERIAL_SPI0, &output));
    K22SerialSpiTransfer transfer;
    TEST_EXPECT(state, k22_serial_pop_spi_transfer(&serial, K22_SERIAL_SPI0, &transfer));
    TEST_EXPECT(state, transfer.data == 4);
    TEST_EXPECT(state, transfer.chip_selects == 0);
    TEST_EXPECT(state, transfer.clock_and_transfer_attributes == 0);
    TEST_EXPECT(state, !transfer.continuous_chip_select);
    TEST_EXPECT(state, transfer.end_of_queue);
    TEST_EXPECT(state, !k22_serial_read(&serial, SPI0_BASE, 1, &ignored));
    TEST_EXPECT(state, !k22_serial_write(&serial, SPI0_BASE + 2, 4, 0));

    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C0, true));
    write_register(state, &serial, I2C0_BASE + 6, 1, 0xffu);
    TEST_EXPECT(state, (read_register(state, &serial, I2C0_BASE + 6, 1) & 0x1fu) == 0x1fu);
    TEST_EXPECT(state, !k22_serial_read(&serial, I2C0_BASE, 2, &ignored));
    TEST_EXPECT(state, !k22_serial_write(&serial, I2C0_BASE, 4, 0));
    TEST_EXPECT(state, !k22_serial_i2c_set_acknowledge(&serial, K22_SERIAL_UART0, true));
    TEST_EXPECT(state, !k22_serial_i2c_lose_arbitration(&serial, K22_SERIAL_I2C1));
    TEST_EXPECT(state, !k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C1, 0, false));
}

static void test_event_capacity(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C0, true));
    for (size_t index = 0; index < 33; index++) {
        write_register(state, &serial, I2C0_BASE + 2, 1, 0xf0u);
        write_register(state, &serial, I2C0_BASE + 2, 1, 0xd0u);
    }
    TEST_EXPECT(state, serial.event_count == K22_SERIAL_EVENT_CAPACITY);
    K22SerialEvent event;
    TEST_EXPECT(state, k22_serial_pop_event(&serial, &event));
    TEST_EXPECT(state, event.type == K22_SERIAL_EVENT_I2C_START);
}

static void test_signal_and_overflow_matrix(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    TEST_EXPECT(state, !k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_ADC0, true));
    TEST_EXPECT(state, !k22_serial_irq(&serial, K22_SERIAL_IRQ_UART0));
    TEST_EXPECT(state, !k22_serial_dma_request(&serial, K22_SERIAL_DMA_UART0_TRANSMIT));
    TEST_EXPECT(state, !k22_serial_dma_request(&serial, K22_SERIAL_DMA_LPUART0_TRANSMIT));
    TEST_EXPECT(state, !k22_serial_dma_request(&serial, K22_SERIAL_DMA_SPI0_TRANSMIT));

    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_UART0, true));
    write_register(state, &serial, UART0_BASE + 0x0b, 1, 0x80u);
    TEST_EXPECT(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_UART0_TRANSMIT));

    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_LPUART0, true));
    write_register(state, &serial, LPUART0_BASE, 4, 0x0f800001u);
    TEST_EXPECT(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_LPUART0_TRANSMIT));
    write_register(state, &serial, LPUART0_BASE + 8, 4, (1u << 18) | (1u << 27));
    TEST_EXPECT(state, k22_serial_push_receive(&serial, K22_SERIAL_LPUART0, 1, 8));
    TEST_EXPECT(state, k22_serial_push_receive(&serial, K22_SERIAL_LPUART0, 2, 0));
    k22_serial_advance(&serial, 320);
    TEST_EXPECT(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_LPUART0));
    TEST_EXPECT(state,
                (read_register(state, &serial, LPUART0_BASE + 4, 4) & (1u << 19)) != 0);

    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI0, true));
    write_register(state, &serial, SPI0_BASE + 0x30, 4, 3u << 24);
    TEST_EXPECT(state, k22_serial_dma_request(&serial, K22_SERIAL_DMA_SPI0_TRANSMIT));
    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_SPI1, true));
    write_register(state, &serial, SPI1_BASE + 0x30, 4, 1u << 25);
    TEST_EXPECT(state, k22_serial_irq(&serial, K22_SERIAL_IRQ_SPI1));

    TEST_EXPECT(state, k22_serial_set_clock_gate(&serial, K22_PERIPHERAL_I2C0, true));
    write_register(state, &serial, I2C0_BASE, 1, 0x52u);
    write_register(state, &serial, I2C0_BASE + 2, 1, 0x80u);
    TEST_EXPECT(state,
                k22_serial_i2c_slave_address(&serial, K22_SERIAL_I2C0, 0x29u, false));
    for (uint16_t value = 0; value < K22_SERIAL_FIFO_CAPACITY; value++)
        TEST_EXPECT(state, k22_serial_push_receive(&serial, K22_SERIAL_I2C0, value, 0));
    TEST_EXPECT(state, !k22_serial_push_receive(&serial, K22_SERIAL_I2C0, 0xffu, 0));

    uint32_t ignored;
    TEST_EXPECT(state, !k22_serial_write(&serial, 0x50000000u, 1, 0));
    TEST_EXPECT(state, !k22_serial_read(&serial, 0x50000000u, 1, &ignored));
}

static void test_invalid_operations(TestState* state) {
    K22Serial serial = create_serial(state, K22_PROFILE_MK22FN51212);
    uint32_t value;
    uint16_t output;
    TEST_EXPECT(state, !k22_serial_init(NULL, serial.profile));
    TEST_EXPECT(state, !k22_serial_init(&serial, NULL));
    TEST_EXPECT(state, !k22_serial_copy(NULL, &serial));
    TEST_EXPECT(state, !k22_serial_copy(&serial, NULL));
    TEST_EXPECT(state, !k22_serial_read(NULL, UART0_BASE, 1, &value));
    TEST_EXPECT(state, !k22_serial_read(&serial, UART0_BASE, 1, NULL));
    TEST_EXPECT(state, !k22_serial_write(NULL, UART0_BASE, 1, 0));
    TEST_EXPECT(state, !k22_serial_read(&serial, 0x50000000u, 1, &value));
    TEST_EXPECT(state, !k22_serial_push_receive(NULL, K22_SERIAL_UART0, 0, 0));
    TEST_EXPECT(state,
                !k22_serial_push_receive(&serial, K22_SERIAL_ENDPOINT_INVALID, 0, 0));
    TEST_EXPECT(state, !k22_serial_pop_transmit(NULL, K22_SERIAL_UART0, &output));
    TEST_EXPECT(state, !k22_serial_pop_transmit(&serial, K22_SERIAL_UART0, NULL));
    K22SerialSpiTransfer transfer;
    TEST_EXPECT(state, !k22_serial_pop_spi_transfer(NULL, K22_SERIAL_SPI0, &transfer));
    TEST_EXPECT(state, !k22_serial_pop_spi_transfer(&serial, K22_SERIAL_SPI0, NULL));
    TEST_EXPECT(state, !k22_serial_pop_spi_transfer(&serial, K22_SERIAL_UART0, &transfer));
    TEST_EXPECT(state, !k22_serial_pop_event(&serial, NULL));
    K22SerialEvent event;
    TEST_EXPECT(state, !k22_serial_pop_event(&serial, &event));
    TEST_EXPECT(state, !k22_serial_i2c_set_acknowledge(NULL, K22_SERIAL_I2C0, true));
    TEST_EXPECT(state, !k22_serial_i2c_lose_arbitration(NULL, K22_SERIAL_I2C0));
    TEST_EXPECT(state, !k22_serial_i2c_slave_address(NULL, K22_SERIAL_I2C0, 0, false));
    TEST_EXPECT(state, !k22_serial_irq(&serial, K22_SERIAL_IRQ_COUNT));
    TEST_EXPECT(state, !k22_serial_dma_request(&serial, K22_SERIAL_DMA_COUNT));
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
