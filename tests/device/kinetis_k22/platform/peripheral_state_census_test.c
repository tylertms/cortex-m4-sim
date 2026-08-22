#include "device/kinetis_k22/internal.h"

#include "test.h"

typedef struct {
    uint32_t random;
    uint32_t accepted;
    uint32_t rejected;
    uint64_t fingerprint;
} PeripheralStateCensus;

static uint32_t next_random(PeripheralStateCensus* census) {
    uint32_t value = census->random;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    census->random = value;
    return value;
}

static void mix(PeripheralStateCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static void randomize_state(KinetisK22* device, PeripheralStateCensus* census, uint32_t random) {
    const uint32_t second = next_random(census);
    kinetis_k22_internal_raw_store(device, K22_AIPS0 + 0x20u + ((random >> 20u) & 0x1cu), 4u,
                                   random);
    kinetis_k22_internal_raw_store(device, K22_AIPS1 + 0x20u + ((second >> 20u) & 0x1cu), 4u,
                                   second);
    const uint32_t axbs = K22_AXBS + ((random >> 8u) % 5u) * 0x100u;
    kinetis_k22_internal_raw_store(device, axbs + 0x10u, 4u, second);
    for (uint8_t offset = 0u; offset < 12u; offset++)
        kinetis_k22_internal_raw_store(device, K22_CMT + offset, 1u, (uint8_t)next_random(census));
    device->cmt_cycles = random & 0x3ffu;
    device->cmt_period_ticks = device->cmt_cycles + (second & 0x3ffu) + 1u;
    device->cmt_bus_remainder = random & 0xffffu;
    device->cmt_mark_ticks = second & 0x3ffu;
    device->cmt_carrier_high_ticks = random & 0xffu;
    device->cmt_carrier_period_ticks = (second & 0xffu) + 1u;
    device->cmt_carrier_offset_ticks = random & 0x3ffu;
    device->cmt_output_delay_ticks = second & 0x3ffu;
    device->cmt_eoc_read = (random & 1u) != 0u;
    device->cmt_running = (random & 2u) != 0u;
    device->cmt_stop_pending = (random & 4u) != 0u;
    device->cmt_fsk_secondary = (random & 8u) != 0u;
    device->cmt_extended_space = (random & 16u) != 0u;
    device->cmt_dma_pending = (random & 32u) != 0u;
    device->timing.sim_scgc3 = random;
    device->timing.sim_scgc4 = second;
    device->timing.sim_scgc5 = random ^ second;
    device->timing.sim_scgc6 = ~random;
    device->timing.sim_scgc7 = ~second;
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    PeripheralStateCensus census = {UINT32_C(0xbb67ae85), 0u, 0u, UINT64_C(14695981039346656037)};
    KinetisK22* device =
        kinetis_k22_create(kinetis_k22_configuration(KINETIS_K22_PROFILE_MK22FN1M012));
    expect(&state, device != NULL, "create peripheral state census simulator");
    if (device == NULL)
        return test_finish(&state);
    for (uint32_t iteration = 0u; iteration < 100000u; iteration++) {
        const uint32_t random = next_random(&census);
        randomize_state(device, &census, random);
        const K22RegisterDescriptor* descriptor =
            &device->manifest->registers[(random >> 8u) % device->manifest->register_count];
        K22PeripheralLocation location;
        const bool resolved = k22_profile_resolve_peripheral(
            device->profile, descriptor->address, (uint8_t)(descriptor->width / 8u), &location);
        if (!resolved) {
            expect(&state, false, "resolve peripheral state census register");
            continue;
        }
        const uint8_t size = (uint8_t[]){1u, 2u, 4u}[random % 3u];
        const CortexM4Access access = (CortexM4Access)((random >> 20u) % 5u);
        const bool write = (random & 1u) != 0u;
        const uint32_t peripheral_address =
            K22_PERIPHERAL_BASE + ((random >> 4u) % K22_PERIPHERAL_SIZE);
        const uint32_t axbs_address = K22_AXBS + ((random >> 12u) % 0x520u);
        const bool aips =
            kinetis_k22_internal_aips_access_allowed(device, peripheral_address, access, write);
        const bool axbs = kinetis_k22_internal_axbs_write_allowed(device, axbs_address);
        const bool clock = kinetis_k22_internal_peripheral_clock_enabled(
            device, (K22PeripheralId)((random >> 24u) % (K22_PERIPHERAL_COUNT + 2u)));
        const bool debug_clock = kinetis_k22_internal_enable_debug_clock(
            device, (K22PeripheralId)((random >> 16u) % (K22_PERIPHERAL_COUNT + 2u)));
        uint32_t value = UINT32_MAX;
        const bool read = kinetis_k22_internal_semantic_read(device, location.id,
                                                             descriptor->address, size, &value);
        const bool written = kinetis_k22_internal_semantic_write(
            device, location.id, descriptor->address, size, random ^ UINT32_C(0xa5a55a5a));
        const bool endpoint = kinetis_k22_internal_serial_endpoint_available(
            device,
            (KinetisK22SerialEndpoint)((random >> 12u) % (KINETIS_K22_SERIAL_ENDPOINT_COUNT + 2u)));
        kinetis_k22_internal_cmt_advance(device, (random & 31u) + 1u);
        if ((iteration & 15u) == 0u) {
            kinetis_k22_sync_clock_gates(device);
            kinetis_k22_refresh_signals(device);
        }
        const uint32_t results = aips | (axbs << 1u) | (clock << 2u) | (debug_clock << 3u) |
                                 (read << 4u) | (written << 5u) | (endpoint << 6u);
        const uint32_t accepted = aips + axbs + clock + debug_clock + read + written + endpoint;
        census.accepted += accepted;
        census.rejected += 7u - accepted;
        mix(&census, descriptor->address);
        mix(&census, value);
        mix(&census, results);
        mix(&census, (uint32_t)device->cmt_cycles ^ device->timing.sim_scgc6);
    }
    expect(&state,
           census.accepted == 411772u && census.rejected == 288228u &&
               census.fingerprint == UINT64_C(1118456075959424554),
           "peripheral state census matches");
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
