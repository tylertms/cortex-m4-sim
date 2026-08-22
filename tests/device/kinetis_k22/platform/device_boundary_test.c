#include "device/kinetis_k22/internal.h"

#include "allocation_failure.h"
#include "test.h"

typedef struct {
    uint32_t accepted_reads;
    uint32_t accepted_writes;
    uint64_t fingerprint;
} DeviceCensus;

static KinetisK22* create_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.profile = KINETIS_K22_PROFILE_MK22FN1M012;
    configuration.package = KINETIS_K22_PACKAGE_LQ_144_LQFP;
    configuration.flash_size = 4096u;
    configuration.sram_size = 65536u;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(state, device != NULL, "create device boundary simulator");
    return device;
}

static void mix(DeviceCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static void access_census(KinetisK22* device, DeviceCensus* census) {
    static const uint32_t addresses[] = {
        0u,          0x3ffu,      0x400u,      0xfffu,      0x1fffffffu,
        0x20000000u, 0x20000001u, 0x20007fffu, 0x40000000u, 0x4001f000u,
        0x400fffffu, 0x42000000u, 0x43ffffffu, 0x60000000u, UINT32_MAX,
    };
    for (size_t address_index = 0u; address_index < sizeof(addresses) / sizeof(addresses[0]);
         address_index++) {
        for (uint8_t size = 0u; size <= 5u; size++) {
            for (uint8_t access = CORTEX_M4_ACCESS_INSTRUCTION; access <= CORTEX_M4_ACCESS_DEBUG;
                 access++) {
                uint32_t value = UINT32_C(0xa5a55a5a);
                const bool read = kinetis_k22_memory_read(device, addresses[address_index], size,
                                                          (CortexM4Access)access, &value);
                const bool write =
                    kinetis_k22_memory_write(device, addresses[address_index], size,
                                             (CortexM4Access)access, UINT32_C(0x5aa5a55a));
                census->accepted_reads += read;
                census->accepted_writes += write;
                mix(census, addresses[address_index]);
                mix(census, size);
                mix(census, access);
                mix(census, read);
                mix(census, write);
                mix(census, value);
            }
            uint32_t value = 0u;
            const bool dma_read =
                kinetis_k22_dma_read(device, addresses[address_index], size, &value);
            const bool dma_write =
                kinetis_k22_dma_write(device, addresses[address_index], size, UINT32_C(0x12345678));
            census->accepted_reads += dma_read;
            census->accepted_writes += dma_write;
            mix(census, dma_read);
            mix(census, dma_write);
            mix(census, value);
        }
    }
}

static void copy_guard_cases(TestState* state, KinetisK22* destination, KinetisK22* source) {
    const K22Profile* profile = source->profile;
    const K22PackageSelection* package = source->package;
    const size_t flash_size = source->configuration.flash_size;
    const size_t sram_size = source->configuration.sram_size;

    source->profile = k22_profile_get(K22_PROFILE_MK22FN51212);
    expect(state, !kinetis_k22_copy(destination, source), "copy rejects a different profile");
    source->profile = profile;
    source->package = k22_package_select(source->profile, K22_PACKAGE_DC_121_XFBGA);
    expect(state, !kinetis_k22_copy(destination, source), "copy rejects a different package");
    source->package = package;
    source->configuration.flash_size++;
    expect(state, !kinetis_k22_copy(destination, source), "copy rejects a different flash size");
    source->configuration.flash_size = flash_size;
    source->configuration.sram_size++;
    expect(state, !kinetis_k22_copy(destination, source), "copy rejects a different SRAM size");
    source->configuration.sram_size = sram_size;
    expect(state, kinetis_k22_copy(destination, source), "compatible devices copy");
}

static void flexbus_cases(TestState* state, KinetisK22* device) {
    static const uint8_t memory[] = {1u, 2u, 3u, 4u};
    uint8_t value[4] = {0u};
    expect(state, kinetis_k22_flexbus_attach(device, 0x60000000u, memory, sizeof(memory), true),
           "attach a read-only FlexBus window");
    expect(state, kinetis_k22_flexbus_read(device, 0u, value, sizeof(value)),
           "read the complete FlexBus window");
    expect(state, !kinetis_k22_flexbus_read(device, sizeof(memory) + 1u, value, 0u),
           "FlexBus read rejects an offset beyond the window");
    expect(state, !kinetis_k22_flexbus_read(device, sizeof(memory), value, 1u),
           "FlexBus read rejects a range beyond the window");
    kinetis_k22_flexbus_detach(device);
}

static void copy_allocation_cases(TestState* state, KinetisK22* destination, KinetisK22* source) {
    static const uint8_t memory[] = {1u, 2u, 3u, 4u};
    static const uint8_t card[512] = {0u};
    expect(state, kinetis_k22_flexbus_attach(source, 0x60000000u, memory, sizeof(memory), false),
           "attach source FlexBus window");
    expect(state, k22_sdhc_insert(&source->sdhc, card, sizeof(card), false),
           "insert source SDHC card");
    uint32_t failures = 0u;
    bool copied = false;
    for (size_t accepted = 0u; accepted < 32u && !copied; accepted++) {
        test_fail_allocation_after(accepted);
        copied = kinetis_k22_copy(destination, source);
        test_allow_allocations();
        failures += !copied;
    }
    expect(state, copied && failures != 0u,
           "device copy reports allocation failures before succeeding");
    kinetis_k22_flexbus_detach(source);
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    DeviceCensus census = {0u, 0u, UINT64_C(14695981039346656037)};
    KinetisK22* first = create_device(&state);
    KinetisK22* second = create_device(&state);
    if (first != NULL && second != NULL) {
        access_census(first, &census);
        copy_guard_cases(&state, first, second);
        copy_allocation_cases(&state, first, second);
        flexbus_cases(&state, first);
        expect(&state,
               census.accepted_reads == 90u && census.accepted_writes == 87u &&
                   census.fingerprint == UINT64_C(3273249558290571156),
               "device census matches");
    }
    kinetis_k22_destroy(second);
    kinetis_k22_destroy(first);
    return test_finish(&state);
}
