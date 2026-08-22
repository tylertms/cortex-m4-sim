#include "kinetis_k22.h"

#include "device/kinetis_k22/internal.h"
#include "test.h"

typedef struct {
    uint32_t random;
    uint32_t reads;
    uint32_t writes;
    uint32_t signals;
    uint64_t fingerprint;
} StatefulCensus;

static uint32_t next_random(StatefulCensus* census) {
    uint32_t value = census->random;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    census->random = value;
    return value;
}

static void mix(StatefulCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static void exercise_signal(KinetisK22* device, StatefulCensus* census, uint32_t random) {
    bool accepted = false;
    bool high = false;
    uint16_t output = 0u;
    switch (random % 16u) {
    case 0u:
        accepted = kinetis_k22_set_adc_channel(device, (uint8_t)(random >> 8u) % 3u,
                                               (uint8_t)(random >> 12u) % 32u, (uint16_t)random);
        break;
    case 1u:
        accepted = kinetis_k22_set_cmp_input(device, (uint8_t)(random >> 8u) % 4u,
                                             (uint8_t)(random >> 12u) % 9u, (uint8_t)random);
        break;
    case 2u:
        accepted =
            kinetis_k22_set_lptmr_input(device, (uint8_t)(random >> 8u) % 3u, (random & 1u) != 0u);
        break;
    case 3u:
        accepted =
            kinetis_k22_set_llwu_pin(device, (uint8_t)(random >> 8u) % 18u, (random & 1u) != 0u);
        break;
    case 4u:
        accepted = kinetis_k22_trigger_llwu_module(device, (uint8_t)(random >> 8u) % 10u);
        break;
    case 5u:
        accepted = kinetis_k22_set_ftm_input(device, (uint8_t)(random >> 8u) % 6u,
                                             (uint8_t)(random >> 12u) % 10u, (random & 1u) != 0u);
        break;
    case 6u:
        accepted = kinetis_k22_set_ftm_fault(device, (uint8_t)(random >> 8u) % 6u,
                                             (uint8_t)(random >> 12u) % 6u, (random & 1u) != 0u);
        break;
    case 7u:
        accepted = kinetis_k22_trigger_ftm_hardware(device, (uint8_t)(random >> 8u) % 6u,
                                                    (uint8_t)(random >> 12u) % 5u);
        break;
    case 8u:
        accepted = kinetis_k22_get_ftm_output(device, (uint8_t)(random >> 8u) % 6u,
                                              (uint8_t)(random >> 12u) % 10u, &high);
        mix(census, high);
        break;
    case 9u:
        accepted = kinetis_k22_get_dac_output(device, (uint8_t)(random >> 8u) % 3u, &output);
        mix(census, output);
        break;
    case 10u:
        accepted = kinetis_k22_gpio_drive(device, (uint8_t)(random >> 8u) % 7u,
                                          (uint8_t)(random >> 12u) % 34u, (random & 1u) != 0u);
        break;
    case 11u:
        accepted = kinetis_k22_gpio_release(device, (uint8_t)(random >> 8u) % 7u,
                                            (uint8_t)(random >> 12u) % 34u);
        break;
    case 12u:
        accepted = kinetis_k22_serial_receive(device, (KinetisK22SerialEndpoint)(random % 15u),
                                              (uint16_t)random, (uint8_t)(random >> 16u));
        break;
    case 13u:
        accepted = kinetis_k22_set_usb_charger(device, (KinetisK22UsbCharger)(random % 6u));
        break;
    case 14u:
        accepted = kinetis_k22_set_usb_pullup(device, (random & 1u) != 0u);
        break;
    default:
        accepted = kinetis_k22_set_ewm_input(device, (random & 1u) != 0u);
        break;
    }
    census->signals += accepted;
    mix(census, accepted);
}

static void exercise_profile(TestState* state, StatefulCensus* census, KinetisK22Profile profile) {
    static const uint8_t sizes[] = {0u, 1u, 2u, 3u, 4u, 5u};
    KinetisK22Configuration configuration = kinetis_k22_configuration(profile);
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(state, device != NULL, "create stateful census simulator");
    if (device == NULL)
        return;
    for (uint32_t iteration = 0u; iteration < 30000u; iteration++) {
        const uint32_t random = next_random(census);
        const K22RegisterDescriptor* descriptor =
            &device->manifest->registers[random % device->manifest->register_count];
        const uint32_t address = descriptor->address + ((random >> 25u) == 0u ? 1u : 0u);
        const uint8_t size = sizes[(random >> 16u) % (sizeof(sizes) / sizeof(sizes[0]))];
        const CortexM4Access access = (CortexM4Access)((random >> 20u) % 5u);
        const bool written =
            kinetis_k22_memory_write(device, address, size, access, random ^ UINT32_C(0xa5a55a5a));
        uint32_t value = UINT32_MAX;
        const bool read = kinetis_k22_memory_read(device, address, size, access, &value);
        census->writes += written;
        census->reads += read;
        mix(census, address);
        mix(census, size);
        mix(census, written);
        mix(census, read);
        mix(census, value);
        exercise_signal(device, census, random);
        if ((iteration & 15u) == 0u)
            kinetis_k22_advance(device, (random & 31u) + 1u);
        KinetisK22Event event;
        for (uint8_t index = 0u; index < 4u && kinetis_k22_next_event(device, &event); index++)
            mix(census, event.type ^ event.source ^ event.value);
    }
    kinetis_k22_destroy(device);
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    StatefulCensus census = {UINT32_C(0x6d2b79f5), 0u, 0u, 0u, UINT64_C(14695981039346656037)};
    for (KinetisK22Profile profile = KINETIS_K22_PROFILE_MK22F12810;
         profile < KINETIS_K22_PROFILE_COUNT; profile++)
        exercise_profile(&state, &census, profile);
    expect(&state,
           census.reads == 35164u && census.writes == 35910u && census.signals == 89803u &&
               census.fingerprint == UINT64_C(1308970153178113273),
           "stateful census matches");
    return test_finish(&state);
}
