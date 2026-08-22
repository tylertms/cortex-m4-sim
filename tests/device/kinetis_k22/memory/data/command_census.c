#include "device/kinetis_k22/memory/data/internal.h"

typedef struct {
    uint32_t commands;
    uint32_t errors;
    uint64_t fingerprint;
} CommandCensus;

static void record(TestState* state, CommandCensus* census, K22Data* data, uint8_t command,
                   uint32_t address) {
    uint32_t status = 0u;
    expect(state, k22_data_internal_flash_read(data, FTFA, 1u, &status),
           "flash command census reads status");
    census->commands++;
    census->errors += (status & 0x31u) != 0u;
    census->fingerprint = (census->fingerprint ^ command) * UINT64_C(1099511628211);
    census->fingerprint = (census->fingerprint ^ address) * UINT64_C(1099511628211);
    census->fingerprint = (census->fingerprint ^ status) * UINT64_C(1099511628211);
}

void k22_data_test_test_flash_command_census(TestState* state) {
    static const K22ProfileId profiles[] = {
        K22_PROFILE_MK22F12810,
        K22_PROFILE_MK22FX51212,
        K22_PROFILE_MK22FN51212,
        K22_PROFILE_MK22FN1M012,
    };
    static const uint8_t commands[] = {
        0x00u, 0x01u, 0x02u, 0x03u, 0x06u, 0x07u, 0x08u, 0x09u, 0x0bu,
        0x40u, 0x41u, 0x43u, 0x45u, 0x46u, 0x80u, 0x81u, 0xffu,
    };
    static const uint32_t addresses[] = {
        0u, 1u, 8u, 0x10u, 0x7ff0u, 0x800000u, 0x800001u, 0xffffffu,
    };
    static TestBus bus;
    CommandCensus census = {0u, 0u, UINT64_C(14695981039346656037)};

    for (size_t profile_index = 0u; profile_index < sizeof(profiles) / sizeof(profiles[0]);
         profile_index++) {
        memset(&bus, 0, sizeof(bus));
        memset(bus.flash, 0xff, sizeof(bus.flash));
        K22Data* data = k22_data_test_create(state, &bus, profiles[profile_index]);
        if (data == NULL) {
            continue;
        }
        memset(data->flash_program_ifr, 0xff, sizeof(data->flash_program_ifr));
        memset(data->flash_data_ifr, 0xff, sizeof(data->flash_data_ifr));
        for (size_t command_index = 0u; command_index < sizeof(commands) / sizeof(commands[0]);
             command_index++) {
            for (size_t address_index = 0u;
                 address_index < sizeof(addresses) / sizeof(addresses[0]); address_index++) {
                k22_data_test_clear_flash_status(state, data);
                k22_data_test_write_fccob(state, data, 4u,
                                          (uint8_t)((command_index + address_index) & 3u));
                k22_data_test_write_fccob(state, data, 5u, (uint8_t)((address_index & 1u) + 1u));
                k22_data_test_write_fccob(state, data, 6u, (uint8_t)(command_index % 4u));
                k22_data_test_flash_command(
                    state, data, commands[command_index], addresses[address_index],
                    commands[command_index] == 0x08u || commands[command_index] == 0x09u ||
                            commands[command_index] == 0x80u
                        ? 2000u
                        : 40u);
                record(state, &census, data, commands[command_index], addresses[address_index]);
            }
        }
        data->flash_partitioned = true;
        data->flash_data_ifr[0x3fcu] = 0xaau;
        k22_data_test_clear_flash_status(state, data);
        k22_data_test_flash_command(state, data, 0x00u, 0x800000u, 40u);
        record(state, &census, data, 0x00u, 0x800000u);
        k22_data_destroy(data);
    }
    expect(state,
           census.commands == 548u && census.errors == 453u &&
               census.fingerprint == UINT64_C(4069876359329861505),
           "flash command census matches");
}
