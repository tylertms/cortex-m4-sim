#include "kinetis_k22.h"
#include "kinetis_k22_internal.h"

#include <stdint.h>

#include "test.h"

enum {
    DMA_ES = 0x40008004u,
    DMA_SSRT = 0x4000801du,
    DMA_CERR = 0x4000801eu,
    DMA_ERR = 0x4000802cu,
    DMA_TCD0 = 0x40009000u,
    SYSMPU_CESR = 0x4000d000u,
    SYSMPU_EAR0 = 0x4000d010u,
    SYSMPU_EDR0 = 0x4000d014u,
    SYSMPU_RGD0_WORD0 = 0x4000d400u,
    SYSMPU_RGD0_WORD1 = 0x4000d404u,
    SYSMPU_RGD0_WORD2 = 0x4000d408u,
    SYSMPU_RGD0_WORD3 = 0x4000d40cu,
};

static void write_debug(TestState* state, KinetisK22* device, uint32_t address, const void* value,
                        size_t size) {
    expect(state, kinetis_k22_write(device, address, value, size),
           "kinetis_k22_write(device, address, value, size)");
}

static uint32_t read_debug(TestState* state, KinetisK22* device, uint32_t address) {
    uint32_t value = 0;
    expect(state, kinetis_k22_read(device, address, &value, sizeof(value)),
           "kinetis_k22_read(device, address, &value, sizeof(value))");
    return value;
}

static void configure_region(TestState* state, KinetisK22* device, uint32_t access_control) {
    for (uint8_t region = 0; region < 12u; region++) {
        const uint32_t valid = SYSMPU_RGD0_WORD3 + (uint32_t)region * 16u;
        write_debug(state, device, valid, &(uint32_t){0}, sizeof(uint32_t));
    }
    write_debug(state, device, SYSMPU_RGD0_WORD0, &(uint32_t){0x20000000u}, sizeof(uint32_t));
    write_debug(state, device, SYSMPU_RGD0_WORD1, &(uint32_t){0x200000ffu}, sizeof(uint32_t));
    write_debug(state, device, SYSMPU_RGD0_WORD2, &access_control, sizeof(access_control));
    write_debug(state, device, SYSMPU_RGD0_WORD3, &(uint32_t){1u}, sizeof(uint32_t));
}

static void configure_transfer(TestState* state, KinetisK22* device, uint32_t source,
                               uint32_t destination) {
    const int16_t offset = 1;
    const uint32_t count = 1;
    const uint16_t iterations = 1;
    write_debug(state, device, DMA_TCD0, &source, sizeof(source));
    write_debug(state, device, DMA_TCD0 + 4u, &offset, sizeof(offset));
    write_debug(state, device, DMA_TCD0 + 8u, &count, sizeof(count));
    write_debug(state, device, DMA_TCD0 + 0x10u, &destination, sizeof(destination));
    write_debug(state, device, DMA_TCD0 + 0x14u, &offset, sizeof(offset));
    write_debug(state, device, DMA_TCD0 + 0x16u, &iterations, sizeof(iterations));
    write_debug(state, device, DMA_TCD0 + 0x1eu, &iterations, sizeof(iterations));
}

int main(void) {
    TestState state = {0};
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.profile = KINETIS_K22_PROFILE_MK22FN1M012;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(&state, device != NULL, "device != NULL");
    const uint32_t source = 0x20000010u;
    const uint32_t destination = 0x20000020u;
    const uint8_t source_value = 0x5au;
    const uint8_t destination_value = 0;
    write_debug(&state, device, source, &source_value, sizeof(source_value));
    write_debug(&state, device, destination, &destination_value, sizeof(destination_value));

    configure_region(&state, device, (2u << 3u) | (1u << 9u) | (1u << 15u));
    uint32_t value = 0;
    expect(&state,
           !kinetis_k22_memory_read(device, source, 1u, CORTEX_M4_ACCESS_INSTRUCTION, &value),
           "!kinetis_k22_memory_read(device, source, 1u, CORTEX_M4_ACCESS_INSTRUCTION, "
           "&value)");
    expect(&state, read_debug(&state, device, SYSMPU_EAR0) == source,
           "read_debug(&state, device, SYSMPU_EAR0) == source");
    expect(&state, (read_debug(&state, device, SYSMPU_EDR0) & 0x0ff00000u) == 0x00200000u,
           "(read_debug(&state, device, SYSMPU_EDR0) & 0x0ff00000u) == 0x00200000u");
    write_debug(&state, device, SYSMPU_CESR, &(uint32_t){(1u << 27u) | 1u}, sizeof(uint32_t));

    configure_region(&state, device, (1u << 9u) | (1u << 15u));
    expect(&state, read_debug(&state, device, SYSMPU_RGD0_WORD2) == ((1u << 9u) | (1u << 15u)),
           "read_debug(&state, device, SYSMPU_RGD0_WORD2) == ((1u << 9u) | (1u << 15u))");
    expect(&state, (read_debug(&state, device, SYSMPU_RGD0_WORD3) & 1u) != 0u,
           "(read_debug(&state, device, SYSMPU_RGD0_WORD3) & 1u) != 0u");
    expect(&state, (read_debug(&state, device, SYSMPU_CESR) & 1u) != 0u,
           "(read_debug(&state, device, SYSMPU_CESR) & 1u) != 0u");
    expect(&state, k22_io_clock_enabled(&device->io, K22_PERIPHERAL_SYSMPU),
           "k22_io_clock_enabled(&device->io, K22_PERIPHERAL_SYSMPU)");
    expect(&state, !k22_io_sysmpu_access(&device->io, destination, 2u, true, K22_SYSMPU_WRITE),
           "!k22_io_sysmpu_access(&device->io, destination, 2u, true, K22_SYSMPU_WRITE)");
    write_debug(&state, device, SYSMPU_CESR, &(uint32_t){(1u << 27u) | 1u}, sizeof(uint32_t));
    expect(&state, kinetis_k22_memory_read(device, source, 1u, CORTEX_M4_ACCESS_DATA, &value),
           "kinetis_k22_memory_read(device, source, 1u, CORTEX_M4_ACCESS_DATA, &value)");
    expect(&state, value == source_value, "value == source_value");
    expect(&state, !kinetis_k22_memory_write(device, source, 1u, CORTEX_M4_ACCESS_DATA, 0xa5u),
           "!kinetis_k22_memory_write(device, source, 1u, CORTEX_M4_ACCESS_DATA, 0xa5u)");
    expect(&state, read_debug(&state, device, SYSMPU_EAR0) == source,
           "read_debug(&state, device, SYSMPU_EAR0) == source");
    expect(&state, (read_debug(&state, device, SYSMPU_EDR0) & 0x0ff00000u) == 0x01100000u,
           "(read_debug(&state, device, SYSMPU_EDR0) & 0x0ff00000u) == 0x01100000u");
    write_debug(&state, device, SYSMPU_CESR, &(uint32_t){(1u << 27u) | 1u}, sizeof(uint32_t));
    expect(&state, !kinetis_k22_dma_write(device, destination, 1u, 0xa5u),
           "!kinetis_k22_dma_write(device, destination, 1u, 0xa5u)");
    write_debug(&state, device, SYSMPU_CESR, &(uint32_t){(1u << 27u) | 1u}, sizeof(uint32_t));

    configure_transfer(&state, device, source, destination);
    write_debug(&state, device, DMA_SSRT, &(uint8_t){0}, sizeof(uint8_t));
    expect(&state, (read_debug(&state, device, 0x40008034u) & 1u) == 0u,
           "(read_debug(&state, device, 0x40008034u) & 1u) == 0u");
    kinetis_k22_advance(device, 1u);
    expect(&state, (read_debug(&state, device, DMA_ES) & 0x80000000u) != 0u,
           "(read_debug(&state, device, DMA_ES) & 0x80000000u) != 0u");
    expect(&state, (read_debug(&state, device, DMA_ERR) & 1u) != 0u,
           "(read_debug(&state, device, DMA_ERR) & 1u) != 0u");
    expect(&state, read_debug(&state, device, SYSMPU_EAR0) == destination,
           "read_debug(&state, device, SYSMPU_EAR0) == destination");
    uint8_t byte = 0xffu;
    expect(&state, kinetis_k22_read(device, destination, &byte, sizeof(byte)),
           "kinetis_k22_read(device, destination, &byte, sizeof(byte))");
    expect(&state, byte == 0u, "byte == 0u");

    write_debug(&state, device, DMA_CERR, &(uint8_t){0}, sizeof(uint8_t));
    configure_region(&state, device, 1u << 9u);
    configure_transfer(&state, device, source, destination);
    write_debug(&state, device, DMA_SSRT, &(uint8_t){0}, sizeof(uint8_t));
    kinetis_k22_advance(device, 1u);
    expect(&state, kinetis_k22_read(device, destination, &byte, sizeof(byte)),
           "kinetis_k22_read(device, destination, &byte, sizeof(byte))");
    expect(&state, byte == source_value, "byte == source_value");
    expect(&state, (read_debug(&state, device, DMA_ERR) & 1u) == 0u,
           "(read_debug(&state, device, DMA_ERR) & 1u) == 0u");

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
