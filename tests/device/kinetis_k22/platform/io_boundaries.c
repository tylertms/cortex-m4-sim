#include "device/kinetis_k22/gpio/io.h"

#include "test.h"

enum {
    USB0 = 0x40072000u,
    CAN0 = 0x40024000u,
    I2S0 = 0x4002f000u,
    FLEXBUS = 0x4000c000u,
    SYSMPU = 0x4000d000u,
};

static void expect_read(TestState* state, K22Io* io, uint32_t address, uint8_t size) {
    uint32_t value = 0u;
    expect(state, k22_io_read(io, address, size, &value), "peripheral boundary read succeeds");
}

static void expect_rejected_read(TestState* state, K22Io* io, uint32_t address, uint8_t size) {
    uint32_t value = 0u;
    expect(state, !k22_io_read(io, address, size, &value), "invalid peripheral read is rejected");
}

static void test_usb_offsets(TestState* state, K22Io* io) {
    const uint16_t offsets[] = {0x00u,  0x1cu,  0x80u,  0xbcu,  0xc0u,  0xfcu,  0x100u,
                                0x104u, 0x108u, 0x10cu, 0x110u, 0x114u, 0x140u, 0x144u,
                                0x148u, 0x14cu, 0x154u, 0x158u, 0x15cu};
    for (size_t index = 0u; index < sizeof(offsets) / sizeof(offsets[0]); index++)
        expect_read(state, io, USB0 + offsets[index], 1u);

    expect_rejected_read(state, io, USB0 + 0x20u, 1u);
    expect_rejected_read(state, io, USB0 + 0x80u, 2u);
    expect(state, !k22_io_write(io, USB0, 1u, 0u), "USB identity register is read-only");
    expect(state, !k22_io_write(io, USB0 + 0x90u, 1u, 0u), "USB status register is read-only");
    expect(state, !k22_io_write(io, USB0 + 0xa0u, 1u, 0u), "USB frame register is read-only");
}

static void test_can_offsets(TestState* state, K22Io* io) {
    const uint16_t offsets[] = {0x00u, 0x80u, 0x17cu, 0x880u, 0x8bcu};
    const uint8_t read_only[] = {0x08u, 0x1cu, 0x38u, 0x44u, 0x4cu};
    for (size_t index = 0u; index < sizeof(offsets) / sizeof(offsets[0]); index++)
        expect_read(state, io, CAN0 + offsets[index], 4u);
    for (size_t index = 0u; index < sizeof(read_only) / sizeof(read_only[0]); index++)
        expect(state, !k22_io_write(io, CAN0 + read_only[index], 4u, 0u),
               "CAN read-only register rejects writes");

    expect_rejected_read(state, io, CAN0 + 0x7cu, 4u);
    expect_rejected_read(state, io, CAN0 + 0x80u, 2u);
    expect_rejected_read(state, io, CAN0 + 0x82u, 4u);
    expect(state, !k22_io_write(io, CAN0 + 0x7cu, 4u, 0u), "undefined CAN register rejects writes");
}

static void test_i2s_offsets(TestState* state, K22Io* io) {
    const uint16_t offsets[] = {0x00u, 0x20u, 0x24u, 0x40u, 0x44u, 0x60u,  0x80u, 0x94u,
                                0xa0u, 0xa4u, 0xc0u, 0xc4u, 0xe0u, 0x100u, 0x104u};
    const uint8_t read_only[] = {0x40u, 0x44u, 0xc0u, 0xc4u};
    for (size_t index = 0u; index < sizeof(offsets) / sizeof(offsets[0]); index++)
        expect_read(state, io, I2S0 + offsets[index], 4u);
    for (size_t index = 0u; index < sizeof(read_only) / sizeof(read_only[0]); index++)
        expect(state, !k22_io_write(io, I2S0 + read_only[index], 4u, 0u),
               "I2S receive register rejects writes");

    expect_rejected_read(state, io, I2S0 + 0x18u, 4u);
    expect_rejected_read(state, io, I2S0 + 0x20u, 2u);
    expect_rejected_read(state, io, I2S0 + 0x22u, 4u);
}

static void test_bus_offsets(TestState* state, K22Io* io) {
    const uint8_t flexbus_offsets[] = {0u, 4u, 8u, 12u, 0x44u, 0x60u};
    for (size_t index = 0u; index < sizeof(flexbus_offsets) / sizeof(flexbus_offsets[0]); index++)
        expect_read(state, io, FLEXBUS + flexbus_offsets[index], 4u);
    expect_rejected_read(state, io, FLEXBUS + 0x48u, 4u);
    expect_rejected_read(state, io, FLEXBUS + 4u, 2u);

    const uint16_t sysmpu_offsets[] = {0u, 0x10u, 0x54u, 0x400u, 0x4bcu, 0x800u, 0x82cu};
    for (size_t index = 0u; index < sizeof(sysmpu_offsets) / sizeof(sysmpu_offsets[0]); index++)
        expect_read(state, io, SYSMPU + sysmpu_offsets[index], 4u);
    expect_rejected_read(state, io, SYSMPU + 0x0cu, 4u);
    expect_rejected_read(state, io, SYSMPU + 0x12u, 4u);
}

void k22_io_test_peripheral_boundaries(TestState* state) {
    K22Io usb;
    K22IoConfiguration usb_configuration =
        k22_io_default_configuration(k22_profile_get(K22_PROFILE_MK22FN51212));
    expect(state, k22_io_init(&usb, usb_configuration), "USB boundary fixture initializes");
    k22_io_set_clock(&usb, K22_PERIPHERAL_USB0, true);
    test_usb_offsets(state, &usb);

    K22Io bus;
    K22IoConfiguration bus_configuration =
        k22_io_default_configuration(k22_profile_get(K22_PROFILE_MK22FN1M012));
    expect(state, k22_io_init(&bus, bus_configuration), "bus boundary fixture initializes");
    k22_io_set_clock(&bus, K22_PERIPHERAL_CAN0, true);
    k22_io_set_clock(&bus, K22_PERIPHERAL_I2S0, true);
    k22_io_set_clock(&bus, K22_PERIPHERAL_FB, true);
    k22_io_set_clock(&bus, K22_PERIPHERAL_SYSMPU, true);
    test_can_offsets(state, &bus);
    test_i2s_offsets(state, &bus);
    test_bus_offsets(state, &bus);
}
