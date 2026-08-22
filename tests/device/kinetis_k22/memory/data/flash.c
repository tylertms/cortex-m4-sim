#include "device/kinetis_k22/memory/data/internal.h"

void k22_data_test_test_flash_flex_copy(TestState* state) {
    TestBus bus = {0};
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FX51212);
    expect(state, k22_data_test_read_value(state, data, FTFA, 1) == 0x80u,
           "k22_data_test_read_value(state, data, FTFA, 1) == 0x80u");
    k22_data_test_write_fccob(state, data, 0u, 0x07u);
    k22_data_test_write_fccob(state, data, 1u, 0x00u);
    k22_data_test_write_fccob(state, data, 2u, 0x10u);
    k22_data_test_write_fccob(state, data, 3u, 0x00u);
    k22_data_test_write_fccob(state, data, 4u, 0x12u);
    k22_data_test_write_fccob(state, data, 5u, 0x34u);
    k22_data_test_write_fccob(state, data, 6u, 0x56u);
    k22_data_test_write_fccob(state, data, 7u, 0x78u);
    k22_data_test_write_fccob(state, data, 8u, 0x9au);
    k22_data_test_write_fccob(state, data, 9u, 0xbcu);
    k22_data_test_write_fccob(state, data, 10u, 0xdeu);
    k22_data_test_write_fccob(state, data, 11u, 0xf0u);
    k22_data_test_write_value(state, data, FTFA, 1, 0x80u);
    expect(state, k22_data_test_read_value(state, data, FTFA, 1) == 0,
           "k22_data_test_read_value(state, data, FTFA, 1) == 0");
    expect(state, k22_data_test_load(bus.flash, 0x1000, 4) == 0x78563412u,
           "k22_data_test_load(bus.flash, 0x1000, 4) == 0x78563412u");
    expect(state, k22_data_test_load(bus.flash, 0x1004, 4) == 0xf0debc9au,
           "k22_data_test_load(bus.flash, 0x1004, 4) == 0xf0debc9au");
    k22_data_advance(data, 40);
    expect(state, k22_data_test_read_value(state, data, FTFA, 1) == 0x80u,
           "k22_data_test_read_value(state, data, FTFA, 1) == 0x80u");
    k22_data_test_write_fccob(state, data, 0u, 0xffu);
    k22_data_test_write_value(state, data, FTFA, 1, 0x80u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) != 0,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) != 0");
    k22_data_test_write_value(state, data, FTFA, 1, 0x20u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) == 0,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) == 0");

    uint8_t configuration[16];
    memset(configuration, 0xff, sizeof(configuration));
    configuration[8] = 0xfeu;
    configuration[0x0c] = 0xfeu;
    expect(state, k22_data_set_flash_configuration(data, configuration, sizeof(configuration)),
           "k22_data_set_flash_configuration(data, configuration, sizeof(configuration))");
    expect(state, k22_data_test_read_value(state, data, 0x408u, 1) == 0xfeu,
           "k22_data_test_read_value(state, data, 0x408u, 1) == 0xfeu");
    k22_data_reset(data);
    expect(state, k22_data_test_read_value(state, data, FTFA + 0x10, 1) == 0xfeu,
           "k22_data_test_read_value(state, data, FTFA + 0x10, 1) == 0xfeu");
    k22_data_test_write_fccob(state, data, 0u, 0x07u);
    k22_data_test_write_fccob(state, data, 1u, 0x00u);
    k22_data_test_write_fccob(state, data, 2u, 0x10u);
    k22_data_test_write_fccob(state, data, 3u, 0x00u);
    for (uint8_t index = 4u; index < 12u; index++)
        k22_data_test_write_fccob(state, data, index, 0xffu);
    k22_data_test_write_value(state, data, FTFA, 1, 0x80u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 0x10u) != 0,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 0x10u) != 0");

    expect(state, k22_data_test_read_value(state, data, 0x10000000u, 4) == UINT32_MAX,
           "k22_data_test_read_value(state, data, 0x10000000u, 4) == UINT32_MAX");
    k22_data_test_write_value(state, data, 0x10000000u, 4, 0x55aa55aau);
    k22_data_test_write_value(state, data, 0x10000000u, 4, 0xffff0000u);
    expect(state, k22_data_test_read_value(state, data, 0x10000000u, 4) == 0x55aa0000u,
           "k22_data_test_read_value(state, data, 0x10000000u, 4) == 0x55aa0000u");
    k22_data_test_write_value(state, data, 0x14000000u, 4, 0xdeadbeefu);
    expect(state, k22_data_test_read_value(state, data, 0x14000000u, 4) == 0xdeadbeefu,
           "k22_data_test_read_value(state, data, 0x14000000u, 4) == 0xdeadbeefu");

    K22Data* copy = k22_data_test_create(state, &bus, K22_PROFILE_MK22FX51212);
    expect(state, k22_data_copy(copy, data), "k22_data_copy(copy, data)");
    expect(state, k22_data_test_read_value(state, copy, 0x10000000u, 4) == 0x55aa0000u,
           "k22_data_test_read_value(state, copy, 0x10000000u, 4) == 0x55aa0000u");
    expect(state, k22_data_test_read_value(state, copy, 0x14000000u, 4) == 0xdeadbeefu,
           "k22_data_test_read_value(state, copy, 0x14000000u, 4) == 0xdeadbeefu");
    k22_data_reset(copy);
    expect(state, k22_data_test_read_value(state, copy, 0x10000000u, 4) == 0x55aa0000u,
           "k22_data_test_read_value(state, copy, 0x10000000u, 4) == 0x55aa0000u");
    expect(state, k22_data_test_read_value(state, copy, 0x14000000u, 4) == 0,
           "k22_data_test_read_value(state, copy, 0x14000000u, 4) == 0");
    k22_data_destroy(copy);
    k22_data_destroy(data);
}
void k22_data_test_test_flash_collision_lifecycle(TestState* state) {
    TestBus bus = {0};
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FX51212);
    k22_data_test_write_value(state, data, FTFA + 1u, 1u, 0xc0u);
    k22_data_test_write_fccob(state, data, 0u, 0x07u);
    k22_data_test_set_flash_address(state, data, 0x1000u);
    for (uint8_t index = 4u; index < 12u; index++)
        k22_data_test_write_fccob(state, data, index, index);
    k22_data_test_write_value(state, data, FTFA, 1u, 0x80u);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_FTFA],
           "!bus.interrupt[K22_DATA_INTERRUPT_FTFA]");
    expect(state, !k22_data_flash_read(data, false, 0x1000u, 1u),
           "!k22_data_flash_read(data, false, 0x1000u, 1u)");
    expect(state, k22_data_flash_read(data, false, 0x41000u, 1u),
           "k22_data_flash_read(data, false, 0x41000u, 1u)");
    expect(state, k22_data_flash_read(data, true, 0u, 1u),
           "k22_data_flash_read(data, true, 0u, 1u)");
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x40u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x40u) != 0u");
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_FLASH_COLLISION],
           "bus.interrupt[K22_DATA_INTERRUPT_FLASH_COLLISION]");
    k22_data_test_write_value(state, data, FTFA, 1u, 0x40u);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_FLASH_COLLISION],
           "!bus.interrupt[K22_DATA_INTERRUPT_FLASH_COLLISION]");
    k22_data_advance(data, 39u);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_FTFA],
           "!bus.interrupt[K22_DATA_INTERRUPT_FTFA]");
    k22_data_advance(data, 1u);
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_FTFA], "bus.interrupt[K22_DATA_INTERRUPT_FTFA]");
    k22_data_test_write_value(state, data, FTFA + 1u, 1u, 0u);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_FTFA],
           "!bus.interrupt[K22_DATA_INTERRUPT_FTFA]");

    k22_data_test_write_fccob(state, data, 0u, 0x07u);
    k22_data_test_set_flash_address(state, data, 0x800000u);
    for (uint8_t index = 4u; index < 12u; index++)
        k22_data_test_write_fccob(state, data, index, (uint8_t)(index + 0x10u));
    k22_data_test_write_value(state, data, FTFA, 1u, 0x80u);
    expect(state, k22_data_flash_read(data, false, 0u, 1u),
           "k22_data_flash_read(data, false, 0u, 1u)");
    expect(state, !k22_data_flash_read(data, true, 0u, 1u),
           "!k22_data_flash_read(data, true, 0u, 1u)");
    k22_data_advance(data, 40u);
    expect(state, k22_data_flash_read(data, true, 0u, 1u),
           "k22_data_flash_read(data, true, 0u, 1u)");
    k22_data_destroy(data);
}

void k22_data_test_set_flash_address(TestState* state, K22Data* data, uint32_t address) {
    k22_data_test_write_fccob(state, data, 1u, (uint8_t)(address >> 16u));
    k22_data_test_write_fccob(state, data, 2u, (uint8_t)(address >> 8u));
    k22_data_test_write_fccob(state, data, 3u, (uint8_t)address);
}

static void launch_flash(TestState* state, K22Data* data, uint32_t cycles) {
    k22_data_test_write_value(state, data, FTFA, 1, 0x80u);
    k22_data_advance(data, cycles);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 0x80u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 0x80u) != 0u");
}

void k22_data_test_test_flash_controller_geometry(TestState* state) {
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN1M012);
    bus.flash[0x1000] = 0;
    bus.flash[0x1ffc] = 0;
    bus.flash[0x2000] = 0;
    k22_data_test_write_fccob(state, data, 0u, 0x09u);
    k22_data_test_set_flash_address(state, data, 0x1000u);
    launch_flash(state, data, 2000u);
    expect(state, k22_data_test_load(bus.flash, 0x1000u, 4) == UINT32_MAX,
           "k22_data_test_load(bus.flash, 0x1000u, 4) == UINT32_MAX");
    expect(state, k22_data_test_load(bus.flash, 0x1ffcu, 4) == UINT32_MAX,
           "k22_data_test_load(bus.flash, 0x1ffcu, 4) == UINT32_MAX");
    expect(state, bus.flash[0x2000] == 0, "bus.flash[0x2000] == 0");

    k22_data_test_write_fccob(state, data, 0u, 0x43u);
    k22_data_test_write_fccob(state, data, 1u, 2u);
    const uint8_t once_data[8] = {0x12u, 0x34u, 0x56u, 0x78u, 0x9au, 0xbcu, 0xdeu, 0xf0u};
    for (uint8_t index = 0u; index < sizeof(once_data); index++)
        k22_data_test_write_fccob(state, data, (uint8_t)(4u + index), once_data[index]);
    launch_flash(state, data, 40u);
    k22_data_test_write_fccob(state, data, 0u, 0x41u);
    k22_data_test_write_fccob(state, data, 1u, 2u);
    launch_flash(state, data, 40u);
    for (uint8_t index = 0u; index < sizeof(once_data); index++)
        expect(state,
               k22_data_test_read_fccob(state, data, (uint8_t)(4u + index)) == once_data[index],
               "k22_data_test_read_fccob(state, data, (uint8_t)(4u + index)) == once_data[index]");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    bus.flash[0x1000] = 0;
    bus.flash[0x17fc] = 0;
    bus.flash[0x1800] = 0;
    k22_data_test_write_fccob(state, data, 0u, 0x09u);
    k22_data_test_set_flash_address(state, data, 0x1000u);
    launch_flash(state, data, 2000u);
    expect(state, k22_data_test_load(bus.flash, 0x1000u, 4) == UINT32_MAX,
           "k22_data_test_load(bus.flash, 0x1000u, 4) == UINT32_MAX");
    expect(state, k22_data_test_load(bus.flash, 0x17fcu, 4) == UINT32_MAX,
           "k22_data_test_load(bus.flash, 0x17fcu, 4) == UINT32_MAX");
    expect(state, bus.flash[0x1800] == 0, "bus.flash[0x1800] == 0");
    k22_data_test_write_fccob(state, data, 0u, 0x07u);
    k22_data_test_set_flash_address(state, data, 0x2000u);
    k22_data_test_write_value(state, data, FTFA, 1, 0x80u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) != 0u");
    k22_data_destroy(data);
}

void k22_data_test_flash_command(TestState* state, K22Data* data, uint8_t command, uint32_t address,
                                 uint32_t cycles) {
    k22_data_test_write_fccob(state, data, 0u, command);
    k22_data_test_set_flash_address(state, data, address);
    launch_flash(state, data, cycles);
}

void k22_data_test_flash_command_without_address(TestState* state, K22Data* data, uint8_t command,
                                                 uint32_t cycles) {
    k22_data_test_write_fccob(state, data, 0u, command);
    launch_flash(state, data, cycles);
}

void k22_data_test_test_flash_commands_and_failures(TestState* state) {
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN1M012);

    bus.flash[0x100u] = 0u;
    bus.flash[0x7fffcu] = 0u;
    bus.flash[0x80000u] = 0u;
    k22_data_test_flash_command(state, data, 0x08u, 0x100u, 2000u);
    expect(state, bus.flash[0x100u] == 0xffu, "bus.flash[0x100u] == 0xffu");
    expect(state, bus.flash[0x7fffcu] == 0xffu, "bus.flash[0x7fffcu] == 0xffu");
    expect(state, bus.flash[0x80000u] == 0u, "bus.flash[0x80000u] == 0u");

    bus.flash[0u] = 0u;
    bus.flash[0xfffffu] = 0u;
    k22_data_test_flash_command(state, data, 0x44u, 0u, 2000u);
    expect(state, bus.flash[0u] == 0xffu, "bus.flash[0u] == 0xffu");
    expect(state, bus.flash[0xfffffu] == 0xffu, "bus.flash[0xfffffu] == 0xffu");

    k22_data_test_write_fccob(state, data, 4u, 0u);
    k22_data_test_write_fccob(state, data, 5u, 1u);
    k22_data_test_write_fccob(state, data, 6u, 0u);
    k22_data_test_flash_command(state, data, 0x01u, 0x1000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 1u) == 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 1u) == 0u");
    bus.flash[0x1000u] = 0u;
    k22_data_test_flash_command(state, data, 0x01u, 0x1000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 1u) != 0u");
    k22_data_test_write_value(state, data, FTFA, 1, 1u);
    memset(bus.flash, 0xff, sizeof(bus.flash));
    k22_data_test_write_fccob(state, data, 1u, 0u);
    k22_data_test_flash_command(state, data, 0x40u, 0u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 1u) == 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 1u) == 0u");

    k22_data_test_store(bus.flash, 0x2000u, 4, 0x12345678u);
    k22_data_test_write_fccob(state, data, 4u, 1u);
    k22_data_test_write_fccob(state, data, 8u, 0x78u);
    k22_data_test_write_fccob(state, data, 9u, 0x56u);
    k22_data_test_write_fccob(state, data, 10u, 0x34u);
    k22_data_test_write_fccob(state, data, 11u, 0x12u);
    k22_data_test_flash_command(state, data, 0x02u, 0x2000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 1u) == 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 1u) == 0u");
    k22_data_test_write_fccob(state, data, 8u, 0x87u);
    k22_data_test_write_fccob(state, data, 9u, 0x65u);
    k22_data_test_write_fccob(state, data, 10u, 0x43u);
    k22_data_test_write_fccob(state, data, 11u, 0x21u);
    k22_data_test_flash_command(state, data, 0x02u, 0x2000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 1u) != 0u");
    k22_data_test_write_value(state, data, FTFA, 1, 1u);

    bus.fail_read = true;
    for (uint8_t index = 4u; index < 12u; index++)
        k22_data_test_write_fccob(state, data, index, (uint8_t)(index * 17u));
    k22_data_test_flash_command(state, data, 0x07u, 0x3000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) != 0u");
    bus.fail_read = false;
    k22_data_test_write_value(state, data, FTFA, 1, 0x20u);
    bus.fail_write = true;
    k22_data_test_flash_command(state, data, 0x09u, 0x4000u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) != 0u");
    bus.fail_write = false;
    k22_data_test_write_value(state, data, FTFA, 1, 0x20u);

    k22_data_test_write_fccob(state, data, 4u, 1u);
    k22_data_test_flash_command(state, data, 0x03u, 8u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 0x30u) == 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 0x30u) == 0u");
    k22_data_test_flash_command(state, data, 0x45u, 0u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) != 0u");
    k22_data_test_write_value(state, data, FTFA, 1, 0x20u);
    k22_data_test_write_fccob(state, data, 0u, 0x43u);
    k22_data_test_write_fccob(state, data, 1u, 1u);
    for (uint8_t index = 4u; index < 12u; index++)
        k22_data_test_write_fccob(state, data, index, (uint8_t)(index * 19u));
    launch_flash(state, data, 40u);
    launch_flash(state, data, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1) & 0x20u) != 0u");
    k22_data_test_write_value(state, data, FTFA, 1, 0x20u);
    k22_data_test_write_value(state, data, FTFA + 1u, 1, 0x80u);
    k22_data_test_flash_command(state, data, 0x03u, 8u, 40u);
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_FTFA], "bus.interrupt[K22_DATA_INTERRUPT_FTFA]");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = k22_data_test_create_without_program(state, &bus, K22_PROFILE_MK22FN51212);
    k22_data_test_write_fccob(state, data, 0u, 0x06u);
    k22_data_test_set_flash_address(state, data, 0x1000u);
    k22_data_test_write_fccob(state, data, 4u, 0x78u);
    k22_data_test_write_fccob(state, data, 5u, 0x56u);
    k22_data_test_write_fccob(state, data, 6u, 0x34u);
    k22_data_test_write_fccob(state, data, 7u, 0x12u);
    launch_flash(state, data, 40u);
    expect(state, k22_data_test_load(bus.flash, 0x1000u, 4) == 0x12345678u,
           "k22_data_test_load(bus.flash, 0x1000u, 4) == 0x12345678u");
    k22_data_destroy(data);
}

void k22_data_test_clear_flash_status(TestState* state, K22Data* data) {
    k22_data_test_write_value(state, data, FTFA, 1u, 0x70u);
}

void k22_data_test_set_flash_data(TestState* state, K22Data* data, const uint8_t* bytes,
                                  uint8_t length) {
    for (uint8_t index = 0u; index < length; index++)
        k22_data_test_write_fccob(state, data, (uint8_t)(4u + index), bytes[index]);
}

void k22_data_test_test_flash_command_semantics(TestState* state) {
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    const uint8_t longword[4] = {0x11u, 0x22u, 0x33u, 0x44u};
    k22_data_test_set_flash_data(state, data, longword, sizeof(longword));
    k22_data_test_flash_command(state, data, 0x06u, 0x2000u, 40u);
    expect(state, k22_data_test_load(bus.flash, 0x2000u, 4u) == 0x44332211u,
           "k22_data_test_load(bus.flash, 0x2000u, 4u) == 0x44332211u");
    k22_data_test_flash_command(state, data, 0x06u, 0x2000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_flash_command(state, data, 0x06u, 0x2001u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_flash_command(state, data, 0x07u, 0x3000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);

    k22_data_test_write_fccob(state, data, 1u, 0u);
    k22_data_test_set_flash_data(state, data, longword, sizeof(longword));
    k22_data_test_flash_command(state, data, 0x43u, 0u, 40u);
    k22_data_test_flash_command(state, data, 0x41u, 0u, 40u);
    for (uint8_t index = 0u; index < sizeof(longword); index++)
        expect(state,
               k22_data_test_read_fccob(state, data, (uint8_t)(4u + index)) == longword[index],
               "k22_data_test_read_fccob(state, data, (uint8_t)(4u + index)) == longword[index]");
    k22_data_test_flash_command(state, data, 0x43u, 0u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 8u, 1u);
    k22_data_test_flash_command(state, data, 0x03u, 0u, 40u);
    expect(state, k22_data_test_read_fccob(state, data, 4u) == 0x01u,
           "k22_data_test_read_fccob(state, data, 4u) == 0x01u");
    expect(state, k22_data_test_read_fccob(state, data, 6u) == 0x46u,
           "k22_data_test_read_fccob(state, data, 6u) == 0x46u");
    expect(state, k22_data_test_read_fccob(state, data, 7u) == 0x54u,
           "k22_data_test_read_fccob(state, data, 7u) == 0x54u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN1M012);
    uint8_t erased_configuration[16];
    memset(erased_configuration, 0xff, sizeof(erased_configuration));
    expect(
        state,
        k22_data_set_flash_configuration(data, erased_configuration, sizeof(erased_configuration)),
        "k22_data_set_flash_configuration(data, erased_configuration, "
        "sizeof(erased_configuration))");
    k22_data_reset(data);
    k22_data_test_write_fccob(state, data, 4u, 0u);
    k22_data_test_flash_command(state, data, 0x00u, 0x00000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 1u) == 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 1u) == 0u");
    bus.flash[0x7fffcu] = 0u;
    k22_data_test_flash_command(state, data, 0x00u, 0x00000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 4u, 0u);
    k22_data_test_write_fccob(state, data, 5u, 2u);
    k22_data_test_write_fccob(state, data, 6u, 0u);
    bus.flash[0x1010u] = 0u;
    k22_data_test_flash_command(state, data, 0x01u, 0x1000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 4u, 0u);
    k22_data_test_write_fccob(state, data, 5u, 0u);
    k22_data_test_flash_command(state, data, 0x01u, 0x1000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);

    for (uint8_t index = 0u; index < 16u; index++)
        k22_data_test_write_value(state, data, 0x14000000u + index, 1u, (uint8_t)(0x80u + index));
    k22_data_test_write_fccob(state, data, 4u, 0u);
    k22_data_test_write_fccob(state, data, 5u, 1u);
    k22_data_test_flash_command(state, data, 0x0bu, 0x3000u, 40u);
    for (uint8_t index = 0u; index < 16u; index++)
        expect(state, bus.flash[0x3000u + index] == (uint8_t)(0x80u + index),
               "bus.flash[0x3000u + index] == (uint8_t)(0x80u + index)");
    k22_data_test_flash_command(state, data, 0x0bu, 0x3000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 4u, 0u);
    k22_data_test_write_fccob(state, data, 5u, 0u);
    k22_data_test_flash_command(state, data, 0x0bu, 0x4000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 4u, 8u);
    k22_data_test_flash_command(state, data, 0x46u, 0x4000u, 40u);
    expect(state, k22_data_test_read_fccob(state, data, 5u) == 0u,
           "k22_data_test_read_fccob(state, data, 5u) == 0u");
    expect(state, k22_data_test_read_fccob(state, data, 6u) == 0u,
           "k22_data_test_read_fccob(state, data, 6u) == 0u");
    expect(state, k22_data_test_read_fccob(state, data, 7u) == 0u,
           "k22_data_test_read_fccob(state, data, 7u) == 0u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FX51212);
    uint8_t flex_configuration[16];
    memset(flex_configuration, 0xff, sizeof(flex_configuration));
    expect(state,
           k22_data_set_flash_configuration(data, flex_configuration, sizeof(flex_configuration)),
           "k22_data_set_flash_configuration(data, flex_configuration, "
           "sizeof(flex_configuration))");
    k22_data_reset(data);
    k22_data_test_write_fccob(state, data, 3u, 0u);
    k22_data_test_write_fccob(state, data, 4u, 2u);
    k22_data_test_write_fccob(state, data, 5u, 3u);
    k22_data_test_flash_command_without_address(state, data, 0x80u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA + 1u, 1u) & 3u) == 1u,
           "(k22_data_test_read_value(state, data, FTFA + 1u, 1u) & 3u) == 1u");
    expect(state, k22_data_test_read_value(state, data, 0x10000000u, 4u) == UINT32_MAX,
           "k22_data_test_read_value(state, data, 0x10000000u, 4u) == UINT32_MAX");
    k22_data_reset(data);
    expect(state, (k22_data_test_read_value(state, data, FTFA + 1u, 1u) & 3u) == 1u,
           "(k22_data_test_read_value(state, data, FTFA + 1u, 1u) & 3u) == 1u");
    k22_data_test_flash_command_without_address(state, data, 0x80u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_fccob(state, data, 1u, 0xffu);
    k22_data_test_flash_command_without_address(state, data, 0x81u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA + 1u, 1u) & 3u) == 2u,
           "(k22_data_test_read_value(state, data, FTFA + 1u, 1u) & 3u) == 2u");
    expect(state, k22_data_test_read_value(state, data, 0x14000000u, 4u) == UINT32_MAX,
           "k22_data_test_read_value(state, data, 0x14000000u, 4u) == UINT32_MAX");
    k22_data_test_write_fccob(state, data, 1u, 0u);
    k22_data_test_flash_command_without_address(state, data, 0x81u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA + 1u, 1u) & 3u) == 1u,
           "(k22_data_test_read_value(state, data, FTFA + 1u, 1u) & 3u) == 1u");
    k22_data_test_write_fccob(state, data, 1u, 1u);
    k22_data_test_flash_command_without_address(state, data, 0x81u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    const uint8_t phrase[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    k22_data_test_set_flash_data(state, data, phrase, sizeof(phrase));
    k22_data_test_flash_command(state, data, 0x07u, 0x800000u, 40u);
    expect(state, k22_data_test_read_value(state, data, 0x10000000u, 4u) == 0x04030201u,
           "k22_data_test_read_value(state, data, 0x10000000u, 4u) == 0x04030201u");
    expect(state, k22_data_test_read_value(state, data, 0x10000004u, 4u) == 0x08070605u,
           "k22_data_test_read_value(state, data, 0x10000004u, 4u) == 0x08070605u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FX51212);
    expect(state,
           k22_data_set_flash_configuration(data, flex_configuration, sizeof(flex_configuration)),
           "k22_data_set_flash_configuration(data, flex_configuration, "
           "sizeof(flex_configuration))");
    k22_data_reset(data);
    k22_data_test_write_fccob(state, data, 4u, 0u);
    k22_data_test_flash_command(state, data, 0x03u, 0u, 40u);
    expect(state, k22_data_test_read_fccob(state, data, 4u) == 0xffu,
           "k22_data_test_read_fccob(state, data, 4u) == 0xffu");
    k22_data_test_write_fccob(state, data, 4u, 0u);
    k22_data_test_flash_command(state, data, 0x00u, 0x800000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 1u) == 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 1u) == 0u");
    k22_data_test_write_fccob(state, data, 4u, 0u);
    k22_data_test_flash_command(state, data, 0x03u, 0x800000u, 40u);
    expect(state, k22_data_test_read_fccob(state, data, 4u) == 0xffu,
           "k22_data_test_read_fccob(state, data, 4u) == 0xffu");
    k22_data_test_write_fccob(state, data, 3u, 0u);
    k22_data_test_write_fccob(state, data, 4u, 0x02u);
    k22_data_test_write_fccob(state, data, 5u, 0x04u);
    k22_data_test_flash_command_without_address(state, data, 0x80u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) == 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) == 0u");
    k22_data_test_write_fccob(state, data, 4u, 0u);
    k22_data_test_flash_command(state, data, 0x00u, 0x800000u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_write_value(state, data, 0x10000000u, 1u, 0u);
    k22_data_test_write_fccob(state, data, 1u, 0u);
    k22_data_test_flash_command_without_address(state, data, 0x40u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 1u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    k22_data_test_flash_command(state, data, 0x08u, 0x800000u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    expect(state, k22_data_test_read_value(state, data, 0x10000000u, 1u) == 0u,
           "k22_data_test_read_value(state, data, 0x10000000u, 1u) == 0u");
    k22_data_test_clear_flash_status(state, data);
    bus.flash[0u] = 0u;
    k22_data_test_flash_command(state, data, 0x08u, 0u, 2000u);
    expect(state, bus.flash[0u] == 0xffu, "bus.flash[0u] == 0xffu");
    k22_data_test_write_value(state, data, 0x10000000u, 1u, 0u);
    k22_data_test_flash_command(state, data, 0x44u, 0u, 2000u);
    expect(state, k22_data_test_read_value(state, data, 0x10000000u, 1u) == 0xffu,
           "k22_data_test_read_value(state, data, 0x10000000u, 1u) == 0xffu");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    bus.flash[0u] = 0u;
    k22_data_test_flash_command(state, data, 0x08u, 0u, 2000u);
    expect(state, bus.flash[0u] == 0xffu, "bus.flash[0u] == 0xffu");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    k22_data_test_write_fccob(state, data, 1u, 0x10u);
    const uint8_t once_phrase[8] = {9u, 8u, 7u, 6u, 5u, 4u, 3u, 2u};
    k22_data_test_set_flash_data(state, data, once_phrase, sizeof(once_phrase));
    k22_data_test_flash_command_without_address(state, data, 0x43u, 40u);
    k22_data_test_write_fccob(state, data, 1u, 0x10u);
    k22_data_test_flash_command_without_address(state, data, 0x41u, 40u);
    for (uint8_t index = 0u; index < sizeof(once_phrase); index++)
        expect(
            state,
            k22_data_test_read_fccob(state, data, (uint8_t)(4u + index)) == once_phrase[index],
            "k22_data_test_read_fccob(state, data, (uint8_t)(4u + index)) == once_phrase[index]");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN1M012);
    uint8_t protected_configuration[16];
    memset(protected_configuration, 0xff, sizeof(protected_configuration));
    protected_configuration[8] = 0xfdu;
    protected_configuration[12] = 0xfeu;
    expect(state,
           k22_data_set_flash_configuration(data, protected_configuration,
                                            sizeof(protected_configuration)),
           "k22_data_set_flash_configuration(data, protected_configuration, "
           "sizeof(protected_configuration))");
    k22_data_reset(data);
    bus.flash[0u] = 0u;
    k22_data_test_flash_command(state, data, 0x08u, 0u, 2000u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x10u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x10u) != 0u");
    expect(state, bus.flash[0u] == 0u, "bus.flash[0u] == 0u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    uint8_t configuration[16];
    for (uint8_t index = 0u; index < sizeof(configuration); index++)
        configuration[index] = (uint8_t)(index + 1u);
    configuration[8] = 0xffu;
    configuration[9] = 0xffu;
    configuration[10] = 0xffu;
    configuration[11] = 0xffu;
    configuration[12] = 0x80u;
    expect(state, k22_data_set_flash_configuration(data, configuration, sizeof(configuration)),
           "k22_data_set_flash_configuration(data, configuration, sizeof(configuration))");
    k22_data_reset(data);
    k22_data_test_set_flash_data(state, data, configuration, 8u);
    k22_data_test_flash_command(state, data, 0x45u, 0u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA + 2u, 1u) & 3u) == 2u,
           "(k22_data_test_read_value(state, data, FTFA + 2u, 1u) & 3u) == 2u");
    k22_data_reset(data);
    configuration[0] ^= 0xffu;
    k22_data_test_set_flash_data(state, data, configuration, 8u);
    k22_data_test_flash_command(state, data, 0x45u, 0u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_test_clear_flash_status(state, data);
    configuration[0] ^= 0xffu;
    k22_data_test_set_flash_data(state, data, configuration, 8u);
    k22_data_test_flash_command(state, data, 0x45u, 0u, 40u);
    expect(state, (k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_destroy(data);
}
