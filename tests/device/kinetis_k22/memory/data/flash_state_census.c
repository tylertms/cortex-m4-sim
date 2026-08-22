#include "device/kinetis_k22/memory/data/internal.h"

typedef struct {
    uint32_t random;
    uint32_t commands;
    uint32_t accepted;
    uint64_t fingerprint;
} FlashStateCensus;

static uint32_t next_random(FlashStateCensus* census) {
    uint32_t value = census->random;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    census->random = value;
    return value;
}

static void mix(FlashStateCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static void set_fccob(K22Data* data, uint8_t index, uint8_t value) {
    static const uint8_t offsets[] = {7u, 6u, 5u, 4u, 11u, 10u, 9u, 8u, 15u, 14u, 13u, 12u};
    data->flash[offsets[index]] = value;
}

static uint32_t choose_address(const K22Data* data, uint32_t random) {
    static const uint32_t special[] = {0u,     1u,      8u,        0x10u,     0x3f0u,    0x400u,
                                       0x410u, 0x7ff0u, 0x800000u, 0x800010u, 0x80fff0u, 0xffffffu};
    if ((random & 3u) != 0u)
        return special[(random >> 8u) % (sizeof(special) / sizeof(special[0]))];
    const uint32_t size = data->profile->program_flash_size;
    return (random >> 8u) % (size + 1u);
}

static void randomize_state(K22Data* data, TestBus* bus, FlashStateCensus* census,
                            uint32_t random) {
    const uint32_t second = next_random(census);
    for (uint8_t index = 0u; index < 12u; index++)
        set_fccob(data, index, (uint8_t)next_random(census));
    data->flash[0] = 0x80u;
    data->flash[1] = (uint8_t)random;
    data->flash[2] = (uint8_t)(second >> 8u);
    data->flash[0x10u] = (uint8_t)(random >> 16u);
    data->flash[0x11u] = (uint8_t)(random >> 24u);
    data->flash[0x12u] = (uint8_t)second;
    data->flash[0x13u] = (uint8_t)(second >> 8u);
    data->flash[0x17u] = (uint8_t)(second >> 16u);
    static const uint8_t partition_codes[] = {0x00u, 0x03u, 0x04u, 0x05u, 0x08u,
                                              0x0bu, 0x0cu, 0x0du, 0x0fu, 0xffu};
    data->flash_data_ifr[0x3fcu] =
        partition_codes[(random >> 20u) % (sizeof(partition_codes) / sizeof(partition_codes[0]))];
    data->flash_partitioned = data->flexnvm != NULL && (random & 1u) != 0u;
    data->flexram_eeprom = data->flexram != NULL && (random & 2u) != 0u;
    data->flash_key_blocked = (random & 4u) != 0u;
    data->flash_swap_address = ((random >> 8u) & 0x3ff0u);
    data->flash_swap_mode = (uint8_t)((random >> 4u) % 5u);
    data->flash_swap_current_block = (uint8_t)((random >> 12u) & 1u);
    data->flash_swap_next_block = (uint8_t)((random >> 13u) & 1u);
    bus->fail_read = (random & 8u) != 0u;
    bus->fail_write = (random & 16u) != 0u;
}

void k22_data_test_test_flash_state_census(TestState* state) {
    static const K22ProfileId profiles[] = {K22_PROFILE_MK22F12810, K22_PROFILE_MK22FX51212,
                                            K22_PROFILE_MK22FN51212, K22_PROFILE_MK22FN1M012};
    static const uint8_t commands[] = {0x00u, 0x01u, 0x02u, 0x03u, 0x06u, 0x07u,
                                       0x08u, 0x09u, 0x0bu, 0x40u, 0x41u, 0x43u,
                                       0x45u, 0x46u, 0x80u, 0x81u, 0xffu};
    static TestBus bus;
    FlashStateCensus census = {UINT32_C(0x13198a2e), 0u, 0u, UINT64_C(14695981039346656037)};
    for (size_t profile_index = 0u; profile_index < sizeof(profiles) / sizeof(profiles[0]);
         profile_index++) {
        memset(&bus, 0, sizeof(bus));
        memset(bus.flash, 0xff, sizeof(bus.flash));
        K22Data* data = k22_data_test_create(state, &bus, profiles[profile_index]);
        if (data == NULL)
            continue;
        for (uint32_t iteration = 0u; iteration < 10000u; iteration++) {
            const uint32_t random = next_random(&census);
            randomize_state(data, &bus, &census, random);
            const uint8_t command = commands[random % (sizeof(commands) / sizeof(commands[0]))];
            const uint32_t address = choose_address(data, random);
            set_fccob(data, 0u, command);
            set_fccob(data, 1u, (uint8_t)(address >> 16u));
            set_fccob(data, 2u, (uint8_t)(address >> 8u));
            set_fccob(data, 3u, (uint8_t)address);
            const bool launched = k22_data_internal_flash_write(data, FTFA, 1u, 0x80u);
            k22_data_advance(data, 2000u);
            uint32_t status = 0u;
            const bool read = k22_data_internal_flash_read(data, FTFA, 1u, &status);
            census.commands += launched;
            census.accepted += read && (status & 0x30u) == 0u;
            mix(&census, command | (launched << 8u) | (read << 9u));
            mix(&census, address);
            mix(&census, status);
            mix(&census, data->flash_swap_mode | ((uint32_t)data->flash_busy_banks << 8u));
        }
        k22_data_destroy(data);
    }
    expect(state,
           census.commands == 40000u && census.accepted == 3865u &&
               census.fingerprint == UINT64_C(17716325120892081316),
           "flash state census matches");
}
