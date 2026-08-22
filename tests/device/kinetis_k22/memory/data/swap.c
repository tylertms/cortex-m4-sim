#include "device/kinetis_k22/memory/data/internal.h"

static void test_flash_command_guards(TestState* state) {
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN1M012);
    uint32_t value = 0u;
    expect(state, !k22_data_internal_flash_read(data, FTFA - 1u, 1u, &value),
           "!k22_data_internal_flash_read(data, FTFA - 1u, 1u, &value)");
    expect(state, !k22_data_internal_flash_read(data, FTFA + 0x14u, 1u, &value),
           "!k22_data_internal_flash_read(data, FTFA + 0x14u, 1u, &value)");
    expect(state, !k22_data_internal_flash_write(data, FTFA - 1u, 1u, 0u),
           "!k22_data_internal_flash_write(data, FTFA - 1u, 1u, 0u)");
    expect(state, !k22_data_internal_flash_write(data, FTFA + 2u, 1u, 0u),
           "!k22_data_internal_flash_write(data, FTFA + 2u, 1u, 0u)");
    data->flash[0] = 0u;
    expect(state, k22_data_internal_flash_write(data, FTFA + 4u, 1u, 0x55u),
           "k22_data_internal_flash_write(data, FTFA + 4u, 1u, 0x55u)");
    expect(state, k22_data_internal_flash_write(data, FTFA + 0x10u, 1u, 0u),
           "k22_data_internal_flash_write(data, FTFA + 0x10u, 1u, 0u)");
    data->flash[0] = 0x80u;
    k22_data_test_flash_command_without_address(state, data, 0xffu, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 1u, 8u);
    k22_data_test_flash_command_without_address(state, data, 0x41u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_flash_command_without_address(state, data, 0x43u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    bus.flash[0u] = 0u;
    k22_data_test_flash_command(state, data, 0x08u, 1u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    expect(state, bus.flash[0u] == 0u, "bus.flash[0u] == 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_flash_command(state, data, 0x09u, 1u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    expect(state, bus.flash[0u] == 0u, "bus.flash[0u] == 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_flash_command(state, data, 0x08u, 0x700000u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 4u, 0u);
    k22_data_test_flash_command(state, data, 0x03u, 1u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    k22_data_test_write_fccob(state, data, 1u, 8u);
    k22_data_test_flash_command_without_address(state, data, 0x41u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_flash_command_without_address(state, data, 0x43u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 8u, 0u);
    k22_data_test_flash_command(state, data, 0x03u, 1u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FX51212);
    uint8_t configuration[16];
    memset(configuration, 0xff, sizeof(configuration));
    expect(state, k22_data_set_flash_configuration(data, configuration, sizeof(configuration)),
           "k22_data_set_flash_configuration(data, configuration, sizeof(configuration))");
    k22_data_reset(data);
    k22_data_test_write_fccob(state, data, 1u, 0u);
    k22_data_test_flash_command_without_address(state, data, 0x81u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 3u, 0u);
    k22_data_test_write_fccob(state, data, 4u, 2u);
    k22_data_test_write_fccob(state, data, 5u, 1u);
    k22_data_test_flash_command_without_address(state, data, 0x80u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 3u, 0u);
    k22_data_test_write_fccob(state, data, 4u, 2u);
    k22_data_test_write_fccob(state, data, 5u, 3u);
    k22_data_test_flash_command_without_address(state, data, 0x80u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) == 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) == 0u");
    k22_data_test_write_fccob(state, data, 4u, 0u);
    k22_data_test_flash_command(state, data, 0x00u, 0x800000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_value(state, data, 0x10000000u, 1u, 0u);
    k22_data_test_flash_command(state, data, 0x08u, 0x817ff0u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    expect(state, k22_data_test_read_value(state, data, 0x10000000u, 1u) == 0u,
           "k22_data_test_read_value(state, data, 0x10000000u, 1u) == 0u");
    k22_data_destroy(data);
}

static void test_flash_swap_lifecycle(TestState* state) {
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN1M012);

    k22_data_test_write_fccob(state, data, 4u, 4u);
    k22_data_test_flash_command(state, data, 0x46u, 0u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 4u, 8u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, k22_data_test_read_fccob(state, data, 5u) == 0u,
           "k22_data_test_read_fccob(state, data, 5u) == 0u");
    expect(state, k22_data_test_read_fccob(state, data, 6u) == 0u,
           "k22_data_test_read_fccob(state, data, 6u) == 0u");
    expect(state, k22_data_test_read_fccob(state, data, 7u) == 0u,
           "k22_data_test_read_fccob(state, data, 7u) == 0u");

    k22_data_test_write_fccob(state, data, 4u, 1u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x21u) == 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x21u) == 0u");
    expect(state, bus.flash[0x1000u] == 0u, "bus.flash[0x1000u] == 0u");
    expect(state, bus.flash[0x1001u] == 0xffu, "bus.flash[0x1001u] == 0xffu");
    k22_data_test_write_fccob(state, data, 4u, 1u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 4u, 2u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 4u, 2u);
    k22_data_test_flash_command(state, data, 0x46u, 0x2000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 4u, 0x10u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 4u, 8u);
    k22_data_test_flash_command(state, data, 0x46u, 0x80000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    const uint8_t phrase[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    k22_data_test_set_flash_data(state, data, phrase, sizeof(phrase));
    k22_data_test_flash_command(state, data, 0x07u, 0x1000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x10u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x10u) != 0u");
    expect(state, bus.flash[0x1001u] == 0xffu, "bus.flash[0x1001u] == 0xffu");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_flash_command(state, data, 0x09u, 0x1000u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x10u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x10u) != 0u");
    expect(state, bus.flash[0x1001u] == 0xffu, "bus.flash[0x1001u] == 0xffu");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 4u, 8u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, k22_data_test_read_fccob(state, data, 5u) == 3u,
           "k22_data_test_read_fccob(state, data, 5u) == 3u");
    expect(state, k22_data_test_read_fccob(state, data, 6u) == 0u,
           "k22_data_test_read_fccob(state, data, 6u) == 0u");
    expect(state, k22_data_test_read_fccob(state, data, 7u) == 0u,
           "k22_data_test_read_fccob(state, data, 7u) == 0u");

    k22_data_test_write_fccob(state, data, 4u, 4u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, bus.flash[0x1000u] == 0u, "bus.flash[0x1000u] == 0u");
    expect(state, bus.flash[0x1001u] == 0u, "bus.flash[0x1001u] == 0u");
    k22_data_test_flash_command(state, data, 0x09u, 0x81000u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x10u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x10u) != 0u");
    expect(state, bus.flash[0x81000u] == 0xffu, "bus.flash[0x81000u] == 0xffu");
    k22_data_test_clear_flash_status(state, data);
    k22_data_reset(data);
    expect(state, (k22_data_test_read_value(state, data, FTFA + 1u, 1u) & 8u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA + 1u, 1u) & 8u) != 0u");
    expect(state, k22_data_program_flash_address(data, 0x20u) == 0x80020u,
           "k22_data_program_flash_address(data, 0x20u) == 0x80020u");
    expect(state, k22_data_program_flash_address(data, 0x80020u) == 0x20u,
           "k22_data_program_flash_address(data, 0x80020u) == 0x20u");
    k22_data_test_write_fccob(state, data, 4u, 8u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, k22_data_test_read_fccob(state, data, 5u) == 1u,
           "k22_data_test_read_fccob(state, data, 5u) == 1u");
    expect(state, k22_data_test_read_fccob(state, data, 6u) == 1u,
           "k22_data_test_read_fccob(state, data, 6u) == 1u");
    expect(state, k22_data_test_read_fccob(state, data, 7u) == 1u,
           "k22_data_test_read_fccob(state, data, 7u) == 1u");

    k22_data_test_write_fccob(state, data, 4u, 2u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, bus.flash[0x81000u] == 0u, "bus.flash[0x81000u] == 0u");
    expect(state, bus.flash[0x81001u] == 0xffu, "bus.flash[0x81001u] == 0xffu");
    k22_data_test_flash_command(state, data, 0x09u, 0x81000u, 2000u);
    k22_data_test_write_fccob(state, data, 4u, 8u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, k22_data_test_read_fccob(state, data, 5u) == 3u,
           "k22_data_test_read_fccob(state, data, 5u) == 3u");
    expect(state, k22_data_test_read_fccob(state, data, 6u) == 1u,
           "k22_data_test_read_fccob(state, data, 6u) == 1u");
    expect(state, k22_data_test_read_fccob(state, data, 7u) == 1u,
           "k22_data_test_read_fccob(state, data, 7u) == 1u");
    k22_data_test_write_fccob(state, data, 4u, 4u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, bus.flash[0x81000u] == 0u, "bus.flash[0x81000u] == 0u");
    expect(state, bus.flash[0x81001u] == 0u, "bus.flash[0x81001u] == 0u");
    k22_data_reset(data);
    expect(state, (k22_data_test_read_value(state, data, FTFA + 1u, 1u) & 8u) == 0u,
           "(k22_data_test_read_value(state, data, FTFA + 1u, 1u) & 8u) == 0u");
    expect(state, k22_data_program_flash_address(data, 0x20u) == 0x20u,
           "k22_data_program_flash_address(data, 0x20u) == 0x20u");

    k22_data_test_write_fccob(state, data, 4u, 1u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 4u, 8u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1001u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_flash_command(state, data, 0x46u, 0x400u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_destroy(data);
}

static void test_flash_swap_indicator_failures(TestState* state) {
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN1M012);
    bus.fail_write = true;
    k22_data_test_write_fccob(state, data, 4u, 1u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u");
    bus.fail_write = false;
    k22_data_test_write_fccob(state, data, 4u, 8u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, k22_data_test_read_fccob(state, data, 5u) == 0u,
           "k22_data_test_read_fccob(state, data, 5u) == 0u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN1M012);
    k22_data_test_write_fccob(state, data, 4u, 1u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    bus.fail_write = true;
    k22_data_test_write_fccob(state, data, 4u, 4u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u");
    bus.fail_write = false;
    k22_data_test_write_fccob(state, data, 4u, 8u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, k22_data_test_read_fccob(state, data, 5u) == 3u,
           "k22_data_test_read_fccob(state, data, 5u) == 3u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN1M012);
    k22_data_test_write_fccob(state, data, 4u, 1u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    k22_data_test_write_fccob(state, data, 4u, 4u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    k22_data_reset(data);
    bus.fail_write = true;
    k22_data_test_write_fccob(state, data, 4u, 2u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u");
    bus.fail_write = false;
    k22_data_test_write_fccob(state, data, 4u, 8u);
    k22_data_test_flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, k22_data_test_read_fccob(state, data, 5u) == 1u,
           "k22_data_test_read_fccob(state, data, 5u) == 1u");
    k22_data_destroy(data);
}

static void test_flash_partition_codes(TestState* state) {
    static const struct {
        uint8_t code;
        uint32_t data_size;
    } cases[] = {{0x00u, 0x20000u}, {0x03u, 0x18000u}, {0x04u, 0x10000u},
                 {0x05u, 0u},       {0x08u, 0u},       {0x0bu, 0x8000u},
                 {0x0cu, 0x10000u}, {0x0du, 0x20000u}, {0x0fu, 0x20000u}};
    const uint8_t phrase[8] = {1u, 3u, 5u, 7u, 9u, 11u, 13u, 15u};
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        TestBus bus;
        memset(&bus, 0, sizeof(bus));
        memset(bus.flash, 0xff, sizeof(bus.flash));
        K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FX51212);
        k22_data_test_write_fccob(state, data, 3u, 0u);
        k22_data_test_write_fccob(state, data, 4u, cases[index].data_size == 0x20000u ? 0x0fu : 2u);
        k22_data_test_write_fccob(state, data, 5u, cases[index].code);
        k22_data_test_flash_command_without_address(state, data, 0x80u, 2000u);
        expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) == 0u,
               "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) == 0u");
        k22_data_test_set_flash_data(state, data, phrase, sizeof(phrase));
        const uint32_t address = cases[index].data_size == 0u
                                     ? 0x800000u
                                     : 0x800000u + cases[index].data_size - sizeof(phrase);
        k22_data_test_flash_command(state, data, 0x07u, address, 40u);
        expect(state,
               ((k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u) ==
                   (cases[index].data_size == 0u),
               "((k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u) == "
               "(cases[index].data_size == 0u)");
        if (cases[index].data_size != 0u) {
            expect(state,
                   k22_data_test_read_value(state, data, 0x10000000u + cases[index].data_size - 8u,
                                            4u) == 0x07050301u,
                   "k22_data_test_read_value(state, data, 0x10000000u + cases[index].data_size - "
                   "8u, 4u) "
                   "== 0x07050301u");
            k22_data_test_clear_flash_status(state, data);
            k22_data_test_flash_command(state, data, 0x07u, 0x800000u + cases[index].data_size,
                                        40u);
            expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
                   "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
        } else {
            k22_data_test_clear_flash_status(state, data);
            k22_data_test_flash_command(state, data, 0x08u, 0x800000u, 2000u);
            expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
                   "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
        }
        k22_data_destroy(data);
    }
}

int main(void) {
    TestState state = {0};
    k22_data_reset(NULL);
    k22_data_test_test_profile_boundaries(&state);
    k22_data_test_test_api_boundaries(&state);
    k22_data_test_test_dma(&state);
    k22_data_test_test_dma_advanced(&state);
    k22_data_test_test_dmamux_triggers(&state);
    k22_data_test_test_dmamux_source_matrix(&state);
    k22_data_test_test_dma_arbitration_and_control(&state);
    k22_data_test_test_adc(&state);
    k22_data_test_test_adc_compare_dma_and_continuous(&state);
    k22_data_test_test_dac_cmp_vref(&state);
    k22_data_test_test_rng_crc(&state);
    k22_data_test_test_flash_flex_copy(&state);
    k22_data_test_test_flash_collision_lifecycle(&state);
    k22_data_test_test_flash_controller_geometry(&state);
    k22_data_test_test_flash_commands_and_failures(&state);
    k22_data_test_test_flash_command_semantics(&state);
    k22_data_test_test_flash_command_census(&state);
    k22_data_test_test_flash_state_census(&state);
    k22_data_test_test_state_census(&state);
    test_flash_command_guards(&state);
    test_flash_swap_lifecycle(&state);
    test_flash_swap_indicator_failures(&state);
    test_flash_partition_codes(&state);
    return test_finish(&state);
}
