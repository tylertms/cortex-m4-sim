#include "device/kinetis_k22/communication/sdhc.h"

#include <string.h>

#include "test.h"

enum {
    SDHC_BASE = 0x400b1000u,
};

typedef struct {
    uint8_t memory[4096];
    uint32_t random;
    uint32_t reads;
    uint32_t writes;
    uint64_t fingerprint;
} SdhcCensus;

static bool bus_read(void* context, uint32_t address, uint8_t size, uint32_t* value) {
    SdhcCensus* census = context;
    if (value == NULL || address > sizeof(census->memory) ||
        size > sizeof(census->memory) - address)
        return false;
    *value = 0u;
    memcpy(value, census->memory + address, size);
    return true;
}

static bool bus_write(void* context, uint32_t address, uint8_t size, uint32_t value) {
    SdhcCensus* census = context;
    if (address > sizeof(census->memory) || size > sizeof(census->memory) - address)
        return false;
    memcpy(census->memory + address, &value, size);
    return true;
}

static uint32_t next_random(SdhcCensus* census) {
    uint32_t value = census->random;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    census->random = value;
    return value;
}

static void mix(SdhcCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static void randomize_state(K22Sdhc* sdhc, SdhcCensus* census) {
    for (uint8_t index = 0u; index < 64u; index++)
        sdhc->registers[index] = next_random(census);
    const uint32_t first = next_random(census);
    const uint32_t second = next_random(census);
    sdhc->transfer_address = first % (sdhc->card_size + 1u);
    sdhc->transfer_remaining = second % (sdhc->card_size - sdhc->transfer_address + 1u);
    sdhc->block_length = (first >> 8u) & 0x1fffu;
    sdhc->relative_address = second;
    sdhc->present = (first & 1u) != 0u;
    sdhc->write_protected = (first & 2u) != 0u;
    sdhc->clock_enabled = (first & 4u) != 0u;
    sdhc->application_command = (first & 8u) != 0u;
    sdhc->selected = (first & 16u) != 0u;
    sdhc->transfer_read = (first & 32u) != 0u;
    sdhc->transfer_multiple = (first & 64u) != 0u;
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    SdhcCensus census = {{0u}, UINT32_C(0xa4093822), 0u, 0u, UINT64_C(14695981039346656037)};
    K22Sdhc sdhc;
    expect(&state, k22_sdhc_init(&sdhc, (K22SdhcBus){&census, bus_read, bus_write}),
           "initialize SDHC state census");
    uint8_t card[2048] = {0u};
    expect(&state, k22_sdhc_insert(&sdhc, card, sizeof(card), false),
           "insert SDHC state census card");
    for (uint32_t iteration = 0u; iteration < 100000u; iteration++) {
        randomize_state(&sdhc, &census);
        const uint32_t random = next_random(&census);
        const uint32_t address = SDHC_BASE + ((random >> 8u) % 66u) * 4u;
        const uint8_t size = (uint8_t)(random % 6u);
        uint32_t value = UINT32_MAX;
        const bool read = k22_sdhc_read(&sdhc, address, size, &value);
        const bool written = k22_sdhc_write(&sdhc, address, size, random ^ UINT32_C(0x5aa5a55a));
        census.reads += read;
        census.writes += written;
        mix(&census, read | (written << 1u));
        mix(&census, value);
        mix(&census, sdhc.registers[(random >> 16u) % 64u]);
        mix(&census, k22_sdhc_irq(&sdhc));
    }
    expect(&state,
           census.reads == 2864u && census.writes == 1723u &&
               census.fingerprint == UINT64_C(8440844145325666953),
           "SDHC state census matches");
    k22_sdhc_destroy(&sdhc);
    return test_finish(&state);
}
