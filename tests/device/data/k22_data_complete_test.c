#include "k22_data.h"

#include <stdint.h>
#include <string.h>

#include "test.h"

enum {
    DMA = 0x40008000u,
    TCD0 = 0x40009000u,
    TCD1 = 0x40009020u,
    DMAMUX = 0x40021000u,
    ADC0 = 0x4003b000u,
    ADC1 = 0x40027000u,
    DAC0 = 0x4003f000u,
    DAC1 = 0x40028000u,
    RNG = 0x40029000u,
    CRC = 0x40032000u,
    FTFA = 0x40020000u,
    CMP0 = 0x40073000u,
    VREF = 0x40074000u,
    RAM_BASE = 0x20000000u,
};

typedef struct {
    uint8_t flash[1024 * 1024];
    uint8_t ram[64 * 1024];
    bool interrupt[K22_DATA_INTERRUPT_COUNT];
    bool fail_read;
    bool fail_write;
    bool observe_dma_active;
    uint16_t dma_active;
    uint32_t dma_write_values[32];
    uint8_t dma_write_count;
    K22Data* data;
} TestBus;

static uint32_t load(const uint8_t* bytes, uint32_t offset, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; index++)
        value |= (uint32_t)bytes[offset + index] << (index * 8u);
    return value;
}

static void store(uint8_t* bytes, uint32_t offset, uint8_t size, uint32_t value) {
    for (uint8_t index = 0; index < size; index++)
        bytes[offset + index] = (uint8_t)(value >> (index * 8u));
}

static bool bus_read(void* context, uint32_t address, uint8_t size, uint32_t* value) {
    TestBus* bus = context;
    if (bus->fail_read)
        return false;
    if (address <= sizeof(bus->flash) - size) {
        *value = load(bus->flash, address, size);
        return true;
    }
    if (address >= RAM_BASE && address - RAM_BASE <= sizeof(bus->ram) - size) {
        *value = load(bus->ram, address - RAM_BASE, size);
        return true;
    }
    return false;
}

static bool bus_write(void* context, uint32_t address, uint8_t size, uint32_t value) {
    TestBus* bus = context;
    if (bus->fail_write)
        return false;
    if (bus->observe_dma_active && bus->data != NULL) {
        uint32_t active = 0;
        if (k22_data_read(bus->data, DMA + 0x30u, 2u, &active))
            bus->dma_active |= (uint16_t)active;
    }
    if (address <= sizeof(bus->flash) - size) {
        store(bus->flash, address, size, value);
        return true;
    }
    if (address >= RAM_BASE && address - RAM_BASE <= sizeof(bus->ram) - size) {
        store(bus->ram, address - RAM_BASE, size, value);
        if (bus->dma_write_count < sizeof(bus->dma_write_values) / sizeof(bus->dma_write_values[0]))
            bus->dma_write_values[bus->dma_write_count++] = value;
        return true;
    }
    return false;
}

static void bus_interrupt(void* context, K22DataInterrupt interrupt, bool asserted) {
    TestBus* bus = context;
    bus->interrupt[interrupt] = asserted;
}

static K22Data* create(TestState* state, TestBus* bus, K22ProfileId profile) {
    K22DataBus callbacks = {bus, bus_read, bus_write, bus_write, bus_interrupt, NULL};
    K22Data* data = k22_data_create(k22_profile_get(profile), callbacks);
    expect(state, data != NULL, "data != NULL");
    bus->data = data;
    return data;
}

static K22Data* create_without_program(TestState* state, TestBus* bus, K22ProfileId profile) {
    K22DataBus callbacks = {bus, bus_read, bus_write, NULL, bus_interrupt, NULL};
    K22Data* data = k22_data_create(k22_profile_get(profile), callbacks);
    expect(state, data != NULL, "data != NULL");
    bus->data = data;
    return data;
}

static uint32_t read_value(TestState* state, K22Data* data, uint32_t address, uint8_t size) {
    uint32_t value = 0;
    expect(state, k22_data_read(data, address, size, &value),
           "k22_data_read(data, address, size, &value)");
    return value;
}

static void write_value(TestState* state, K22Data* data, uint32_t address, uint8_t size,
                        uint32_t value) {
    expect(state, k22_data_write(data, address, size, value),
           "k22_data_write(data, address, size, value)");
}

static uint32_t flash_fccob_address(uint8_t index) {
    static const uint8_t offsets[12] = {7u, 6u, 5u, 4u, 11u, 10u, 9u, 8u, 15u, 14u, 13u, 12u};
    return FTFA + offsets[index];
}

static void write_fccob(TestState* state, K22Data* data, uint8_t index, uint8_t value) {
    write_value(state, data, flash_fccob_address(index), 1u, value);
}

static uint8_t read_fccob(TestState* state, K22Data* data, uint8_t index) {
    return (uint8_t)read_value(state, data, flash_fccob_address(index), 1u);
}

static void write_tcd(TestState* state, K22Data* data, uint32_t base, uint32_t source,
                      int16_t source_offset, uint16_t attributes, uint32_t bytes,
                      int32_t source_last, uint32_t destination, int16_t destination_offset,
                      uint16_t iterations, int32_t destination_last, uint16_t control) {
    write_value(state, data, base, 4, source);
    write_value(state, data, base + 4, 2, (uint16_t)source_offset);
    write_value(state, data, base + 6, 2, attributes);
    write_value(state, data, base + 8, 4, bytes);
    write_value(state, data, base + 0x0c, 4, (uint32_t)source_last);
    write_value(state, data, base + 0x10, 4, destination);
    write_value(state, data, base + 0x14, 2, (uint16_t)destination_offset);
    write_value(state, data, base + 0x16, 2, iterations);
    write_value(state, data, base + 0x18, 4, (uint32_t)destination_last);
    write_value(state, data, base + 0x1c, 2, control);
    write_value(state, data, base + 0x1e, 2, iterations);
}

static void store_tcd(TestBus* bus, uint32_t base, uint32_t source, int16_t source_offset,
                      uint16_t attributes, uint32_t bytes, int32_t source_last,
                      uint32_t destination, int16_t destination_offset, uint16_t iterations,
                      int32_t destination_last, uint16_t control) {
    const uint32_t offset = base - RAM_BASE;
    store(bus->ram, offset, 4, source);
    store(bus->ram, offset + 4, 2, (uint16_t)source_offset);
    store(bus->ram, offset + 6, 2, attributes);
    store(bus->ram, offset + 8, 4, bytes);
    store(bus->ram, offset + 0x0c, 4, (uint32_t)source_last);
    store(bus->ram, offset + 0x10, 4, destination);
    store(bus->ram, offset + 0x14, 2, (uint16_t)destination_offset);
    store(bus->ram, offset + 0x16, 2, iterations);
    store(bus->ram, offset + 0x18, 4, (uint32_t)destination_last);
    store(bus->ram, offset + 0x1c, 2, control);
    store(bus->ram, offset + 0x1e, 2, iterations);
}

static void test_profile_boundaries(TestState* state) {
    TestBus bus = {0};
    K22Data* small = create(state, &bus, K22_PROFILE_MK22F12810);
    uint32_t value = 0;
    expect(state, !k22_data_read(small, RNG, 4, &value), "!k22_data_read(small, RNG, 4, &value)");
    expect(state, !k22_data_read(small, DAC1, 1, &value), "!k22_data_read(small, DAC1, 1, &value)");
    expect(state, !k22_data_read(small, DMAMUX + 4, 1, &value),
           "!k22_data_read(small, DMAMUX + 4, 1, &value)");
    expect(state, k22_data_read(small, DMAMUX + 3, 1, &value),
           "k22_data_read(small, DMAMUX + 3, 1, &value)");
    expect(state, !k22_data_set_adc_input(small, 2, 0, 0),
           "!k22_data_set_adc_input(small, 2, 0, 0)");
    expect(state, !k22_data_set_cmp_input(small, 2, 0, 0),
           "!k22_data_set_cmp_input(small, 2, 0, 0)");
    k22_data_destroy(small);
    K22Data* large = create(state, &bus, K22_PROFILE_MK22FN51212);
    expect(state, k22_data_read(large, RNG, 4, &value), "k22_data_read(large, RNG, 4, &value)");
    expect(state, k22_data_read(large, DAC1, 1, &value), "k22_data_read(large, DAC1, 1, &value)");
    k22_data_destroy(large);
}

static void test_dma(TestState* state) {
    TestBus bus = {0};
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    for (uint8_t index = 0; index < 8; index++)
        bus.ram[index] = (uint8_t)(0x80u + index);
    write_tcd(state, data, TCD0, RAM_BASE, 2, 0x0101u, 4, -8, RAM_BASE + 0x100, 2, 2, -8, 0x0eu);
    write_value(state, data, DMAMUX, 1, 0x80u | 16u);
    write_value(state, data, DMA + 0x1b, 1, 0);
    k22_data_dma_request(data, 16u);
    k22_data_advance(data, 1);
    expect(state, load(bus.ram, 0x100, 4) == 0x83828180u, "load(bus.ram, 0x100, 4) == 0x83828180u");
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_DMA0], "bus.interrupt[K22_DATA_INTERRUPT_DMA0]");
    write_value(state, data, DMA + 0x1f, 1, 0);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_DMA0],
           "!bus.interrupt[K22_DATA_INTERRUPT_DMA0]");
    k22_data_dma_request(data, 16u);
    k22_data_advance(data, 1);
    expect(state, load(bus.ram, 0x104, 4) == 0x87868584u, "load(bus.ram, 0x104, 4) == 0x87868584u");
    expect(state, (read_value(state, data, TCD0 + 0x1c, 2) & 0x80u) != 0,
           "(read_value(state, data, TCD0 + 0x1c, 2) & 0x80u) != 0");
    expect(state, (read_value(state, data, DMA + 0x0c, 2) & 1u) == 0,
           "(read_value(state, data, DMA + 0x0c, 2) & 1u) == 0");
    expect(state, read_value(state, data, TCD0, 4) == RAM_BASE,
           "read_value(state, data, TCD0, 4) == RAM_BASE");
    expect(state, read_value(state, data, TCD0 + 0x10, 4) == RAM_BASE + 0x100,
           "read_value(state, data, TCD0 + 0x10, 4) == RAM_BASE + 0x100");

    write_tcd(state, data, TCD1, 0xffff0000u, 1, 0, 1, 0, RAM_BASE, 1, 1, 0, 0x02u);
    write_value(state, data, DMA + 0x19, 1, 1);
    write_value(state, data, DMA + 0x1b, 1, 1);
    write_value(state, data, DMA + 0x1d, 1, 1);
    k22_data_advance(data, 1);
    expect(state, (read_value(state, data, DMA + 0x2c, 2) & 2u) != 0,
           "(read_value(state, data, DMA + 0x2c, 2) & 2u) != 0");
    expect(state, (read_value(state, data, DMA + 4, 4) & 0x80000000u) != 0,
           "(read_value(state, data, DMA + 4, 4) & 0x80000000u) != 0");
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR],
           "bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR]");
    write_value(state, data, DMA + 0x19, 1, 1);
    write_value(state, data, DMA + 0x1e, 1, 1);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR],
           "!bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR]");
    k22_data_destroy(data);
}

static void test_dma_advanced(TestState* state) {
    TestBus bus = {0};
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    bus.ram[0x10] = 0x5au;
    bus.ram[0x20] = 0xa5u;
    write_tcd(state, data, TCD0, RAM_BASE + 0x10, 1, 0, 1, 0, RAM_BASE + 0x110, 1, 0x8201u, 0, 0);
    write_tcd(state, data, TCD1, RAM_BASE + 0x20, 1, 0, 1, 0, RAM_BASE + 0x120, 1, 1, 0, 0);
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    expect(state, bus.ram[0x110] == 0x5au, "bus.ram[0x110] == 0x5au");
    expect(state, bus.ram[0x120] == 0xa5u, "bus.ram[0x120] == 0xa5u");

    const uint32_t next = RAM_BASE + 0x300;
    bus.ram[0x30] = 0x11u;
    bus.ram[0x31] = 0x22u;
    store_tcd(&bus, next, RAM_BASE + 0x31, 1, 0, 1, 0, RAM_BASE + 0x131, 1, 1, 0, 0);
    write_tcd(state, data, TCD0, RAM_BASE + 0x30, 1, 0, 1, 0, RAM_BASE + 0x130, 1, 1, (int32_t)next,
              0x10u);
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    expect(state, bus.ram[0x130] == 0x11u, "bus.ram[0x130] == 0x11u");
    expect(state, read_value(state, data, TCD0, 4) == RAM_BASE + 0x31,
           "read_value(state, data, TCD0, 4) == RAM_BASE + 0x31");
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    expect(state, bus.ram[0x131] == 0x22u, "bus.ram[0x131] == 0x22u");

    bus.ram[0x603] = 3;
    bus.ram[0x600] = 0;
    bus.ram[0x601] = 1;
    bus.ram[0x602] = 2;
    write_tcd(state, data, TCD0, RAM_BASE + 0x603, 1, 2u << 11, 2, 0, RAM_BASE + 0x700, 1, 2, 0, 0);
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    expect(state, load(bus.ram, 0x700, 4) == 0x02010003u, "load(bus.ram, 0x700, 4) == 0x02010003u");

    for (uint8_t index = 0; index < 8; index++)
        bus.ram[0x800 + index] = index;
    write_value(state, data, DMA, 4, 0x80u);
    write_tcd(state, data, TCD0, RAM_BASE + 0x800, 1, 0, 0x80000000u | (2u << 10) | 2u, 0,
              RAM_BASE + 0x900, 1, 2, 0, 0);
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    expect(state, bus.ram[0x900] == 0, "bus.ram[0x900] == 0");
    expect(state, bus.ram[0x901] == 1, "bus.ram[0x901] == 1");
    expect(state, bus.ram[0x902] == 4, "bus.ram[0x902] == 4");
    expect(state, bus.ram[0x903] == 5, "bus.ram[0x903] == 5");

    write_tcd(state, data, TCD0, RAM_BASE, 1, 0, 0, 0, RAM_BASE + 0x100u, 1, 1, 0, 0);
    write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    expect(state, (read_value(state, data, DMA + 0x2cu, 2) & 1u) != 0u,
           "(read_value(state, data, DMA + 0x2cu, 2) & 1u) != 0u");
    write_value(state, data, DMA + 0x1eu, 1, 0u);

    write_tcd(state, data, TCD0, RAM_BASE, 1, 0, 1, 0, RAM_BASE + 0x100u, 1, 1,
              (int32_t)0xffff0000u, 0x10u);
    write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    expect(state, (read_value(state, data, DMA + 4u, 4) & (1u << 2u)) != 0u,
           "(read_value(state, data, DMA + 4u, 4) & (1u << 2u)) != 0u");
    write_value(state, data, DMA + 0x1eu, 1, 0u);

    bus.ram[0x40] = 0x3cu;
    write_tcd(state, data, TCD0, RAM_BASE + 0x40u, 1, 0, 1, 0, RAM_BASE + 0x140u, 1, 1, 0, 0x0120u);
    bus.observe_dma_active = true;
    write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x140] == 0x3cu, "bus.ram[0x140] == 0x3cu");
    expect(state, (bus.dma_active & 1u) != 0u, "(bus.dma_active & 1u) != 0u");
    bus.observe_dma_active = false;
    write_value(state, data, TCD0 + 0x1du, 1, 0x12u);
    expect(state, read_value(state, data, TCD0 + 0x1du, 1) == 0x12u,
           "read_value(state, data, TCD0 + 0x1du, 1) == 0x12u");

    bus.ram[0x50u] = 0x7eu;
    write_value(state, data, DMA, 4, 0x80u);
    write_tcd(state, data, TCD0, RAM_BASE + 0x50u, 1, 0, 0xc0000000u | (0xfffffu << 10u) | 1u, 0,
              RAM_BASE + 0x150u, 1, 2, 0, 0);
    write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x150u] == 0x7eu, "bus.ram[0x150u] == 0x7eu");
    expect(state, read_value(state, data, TCD0, 4) == RAM_BASE + 0x50u,
           "read_value(state, data, TCD0, 4) == RAM_BASE + 0x50u");
    expect(state, read_value(state, data, TCD0 + 0x10u, 4) == RAM_BASE + 0x150u,
           "read_value(state, data, TCD0 + 0x10u, 4) == RAM_BASE + 0x150u");
    k22_data_destroy(data);
}

static void prepare_single_byte_dma(TestState* state, K22Data* data, uint32_t tcd, uint32_t source,
                                    uint32_t destination);

static void test_dmamux_triggers(TestState* state) {
    TestBus bus = {0};
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    bus.ram[0x10u] = 0x5au;
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    write_value(state, data, DMAMUX, 1u, 0xc0u | 16u);
    write_value(state, data, DMA + 0x1bu, 1u, 0u);
    expect(state, !k22_data_dma_trigger(data, 0u), "!k22_data_dma_trigger(data, 0u)");
    expect(state, k22_data_dma_request(data, 16u), "k22_data_dma_request(data, 16u)");
    expect(state, (read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u,
           "(read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u");
    expect(state, k22_data_dma_trigger(data, 0u), "k22_data_dma_trigger(data, 0u)");
    expect(state, (read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u,
           "(read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u");
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x200u] == 0x5au, "bus.ram[0x200u] == 0x5au");
    expect(state, !k22_data_dma_trigger(data, 4u), "!k22_data_dma_trigger(data, 4u)");

    k22_data_reset(data);
    bus.ram[0x11u] = 0xa5u;
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x11u, RAM_BASE + 0x201u);
    write_value(state, data, DMAMUX, 1u, 0x80u | 60u);
    write_value(state, data, DMA + 0x1bu, 1u, 0u);
    expect(state, (read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u,
           "(read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u");
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x201u] == 0xa5u, "bus.ram[0x201u] == 0xa5u");
    expect(state, (read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u,
           "(read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u");

    k22_data_reset(data);
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x11u, RAM_BASE + 0x202u);
    write_value(state, data, DMAMUX, 1u, 0xc0u | 60u);
    write_value(state, data, DMA + 0x1bu, 1u, 0u);
    expect(state, (read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u,
           "(read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u");
    expect(state, k22_data_dma_trigger(data, 0u), "k22_data_dma_trigger(data, 0u)");
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x202u] == 0xa5u, "bus.ram[0x202u] == 0xa5u");
    expect(state, (read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u,
           "(read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u");

    k22_data_reset(data);
    prepare_single_byte_dma(state, data, TCD0 + 4u * 32u, RAM_BASE + 0x11u, RAM_BASE + 0x203u);
    write_value(state, data, DMAMUX + 4u, 1u, 0xc0u | 16u);
    write_value(state, data, DMA + 0x1bu, 1u, 4u);
    expect(state, k22_data_dma_request(data, 16u), "k22_data_dma_request(data, 16u)");
    expect(state, (read_value(state, data, DMA + 0x34u, 2u) & (1u << 4u)) != 0u,
           "(read_value(state, data, DMA + 0x34u, 2u) & (1u << 4u)) != 0u");
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x203u] == 0xa5u, "bus.ram[0x203u] == 0xa5u");

    k22_data_reset(data);
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x11u, RAM_BASE + 0x204u);
    write_value(state, data, DMAMUX, 1u, 0x80u | 54u);
    write_value(state, data, DMA + 0x1bu, 1u, 0u);
    expect(state, (read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u,
           "(read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u");
    k22_data_destroy(data);

    data = create(state, &bus, K22_PROFILE_MK22FN1M012);
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x11u, RAM_BASE + 0x205u);
    write_value(state, data, DMAMUX, 1u, 0x80u | 54u);
    write_value(state, data, DMA + 0x1bu, 1u, 0u);
    expect(state, (read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u,
           "(read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u");
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x205u] == 0xa5u, "bus.ram[0x205u] == 0xa5u");
    k22_data_destroy(data);
}

static void test_dmamux_source_matrix(TestState* state) {
    static const uint64_t expected[K22_PROFILE_COUNT] = {
        UINT64_C(0xfc3f2f00fffdf0fc), UINT64_C(0xfc3f2f00fffdf0fc), UINT64_C(0xfc3f2f00fffdf0fc),
        UINT64_C(0xfc3f6ffffffdf0fc), UINT64_C(0xfffffffffffffffc), UINT64_C(0xfffffffffffffffc),
    };
    TestBus bus = {0};
    for (uint8_t profile = 0u; profile < K22_PROFILE_COUNT; profile++) {
        K22Data* data = create(state, &bus, (K22ProfileId)profile);
        for (uint8_t source = 0u; source < 64u; source++) {
            k22_data_reset(data);
            write_value(state, data, DMAMUX, 1u, 0x80u | source);
            write_value(state, data, DMA + 0x1bu, 1u, 0u);
            const bool valid = (expected[profile] & (UINT64_C(1) << source)) != 0u;
            expect(state, k22_data_dma_request(data, source) == valid,
                   "k22_data_dma_request(data, source) == valid");
            const bool requested = (read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u;
            expect(state, requested == valid, "requested == valid");
        }
        expect(state, !k22_data_dma_request(data, 64u), "!k22_data_dma_request(data, 64u)");
        expect(state, !k22_data_dma_request(data, UINT8_MAX),
               "!k22_data_dma_request(data, UINT8_MAX)");
        k22_data_destroy(data);
    }
}

static void prepare_single_byte_dma(TestState* state, K22Data* data, uint32_t tcd, uint32_t source,
                                    uint32_t destination) {
    write_tcd(state, data, tcd, source, 0, 0, 1, 0, destination, 0, 1, 0, 0);
}

static void test_dma_arbitration_and_control(TestState* state) {
    TestBus bus = {0};
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    expect(state, read_value(state, data, DMA + 0x103u, 1u) == 0u,
           "read_value(state, data, DMA + 0x103u, 1u) == 0u");
    expect(state, read_value(state, data, DMA + 0x102u, 1u) == 1u,
           "read_value(state, data, DMA + 0x102u, 1u) == 1u");
    expect(state, read_value(state, data, DMA + 0x100u, 1u) == 3u,
           "read_value(state, data, DMA + 0x100u, 1u) == 3u");
    bus.ram[0x10u] = 0xa0u;
    bus.ram[0x20u] = 0xb1u;
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    prepare_single_byte_dma(state, data, TCD1, RAM_BASE + 0x20u, RAM_BASE + 0x201u);
    write_value(state, data, DMA + 0x1du, 1u, 0u);
    write_value(state, data, DMA + 0x1du, 1u, 1u);
    k22_data_advance(data, 1u);
    expect(state, bus.dma_write_count == 2u, "bus.dma_write_count == 2u");
    expect(state, bus.dma_write_values[0] == 0xb1u, "bus.dma_write_values[0] == 0xb1u");
    expect(state, bus.dma_write_values[1] == 0xa0u, "bus.dma_write_values[1] == 0xa0u");

    k22_data_reset(data);
    bus.dma_write_count = 0u;
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    prepare_single_byte_dma(state, data, TCD0 + 15u * 32u, RAM_BASE + 0x20u, RAM_BASE + 0x201u);
    write_value(state, data, DMA, 4u, 4u);
    write_value(state, data, DMA + 0x1du, 1u, 0u);
    write_value(state, data, DMA + 0x1du, 1u, 15u);
    k22_data_advance(data, 1u);
    expect(state, bus.dma_write_values[0] == 0xa0u, "bus.dma_write_values[0] == 0xa0u");
    expect(state, bus.dma_write_values[1] == 0xb1u, "bus.dma_write_values[1] == 0xb1u");

    k22_data_reset(data);
    bus.dma_write_count = 0u;
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    write_value(state, data, DMA, 4u, 0x20u);
    write_value(state, data, DMA + 0x1du, 1u, 0u);
    k22_data_advance(data, 1u);
    expect(state, bus.dma_write_count == 0u, "bus.dma_write_count == 0u");
    write_value(state, data, DMA, 4u, 0u);
    k22_data_advance(data, 1u);
    expect(state, bus.dma_write_count == 1u, "bus.dma_write_count == 1u");

    k22_data_reset(data);
    bus.dma_write_count = 0u;
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    write_value(state, data, DMA, 4u, 2u);
    write_value(state, data, DMA + 0x1du, 1u, 0u);
    k22_data_set_debug_halted(data, true);
    k22_data_advance(data, 1u);
    expect(state, bus.dma_write_count == 0u, "bus.dma_write_count == 0u");
    k22_data_set_debug_halted(data, false);
    k22_data_advance(data, 1u);
    expect(state, bus.dma_write_count == 1u, "bus.dma_write_count == 1u");

    k22_data_reset(data);
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    write_value(state, data, DMAMUX, 1u, 0x80u | 16u);
    write_value(state, data, DMA + 0x1bu, 1u, 0u);
    k22_data_dma_request(data, 16u);
    expect(state, (read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u,
           "(read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u");
    k22_data_advance(data, 1u);
    expect(state, (read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u,
           "(read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u");

    k22_data_reset(data);
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    prepare_single_byte_dma(state, data, TCD1, RAM_BASE + 0x20u, RAM_BASE + 0x201u);
    write_value(state, data, DMA + 0x102u, 1u, 0u);
    write_value(state, data, DMA + 0x19u, 1u, 0u);
    write_value(state, data, DMA + 0x1du, 1u, 0u);
    write_value(state, data, DMA + 0x1du, 1u, 1u);
    k22_data_advance(data, 1u);
    expect(state, (read_value(state, data, DMA + 4u, 4u) & (1u << 14u)) != 0u,
           "(read_value(state, data, DMA + 4u, 4u) & (1u << 14u)) != 0u");
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR],
           "bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR]");

    k22_data_reset(data);
    write_value(state, data, DMA, 4u, 0x10u);
    write_value(state, data, DMA + 0x19u, 1u, 0u);
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    write_value(state, data, TCD0 + 8u, 4u, 0u);
    write_value(state, data, DMA + 0x1du, 1u, 0u);
    k22_data_advance(data, 1u);
    expect(state, (read_value(state, data, DMA, 4u) & 0x20u) != 0u,
           "(read_value(state, data, DMA, 4u) & 0x20u) != 0u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    K22Data* small = create(state, &bus, K22_PROFILE_MK22F12810);
    uint32_t value = 0u;
    expect(state, !k22_data_read(small, DMA + 0x104u, 1u, &value),
           "!k22_data_read(small, DMA + 0x104u, 1u, &value)");
    expect(state, !k22_data_write(small, DMA + 0x104u, 1u, 4u),
           "!k22_data_write(small, DMA + 0x104u, 1u, 4u)");
    expect(state, !k22_data_read(small, TCD0 + 4u * 32u, 1u, &value),
           "!k22_data_read(small, TCD0 + 4u * 32u, 1u, &value)");
    k22_data_destroy(small);
}

static void test_adc(TestState* state) {
    TestBus bus = {0};
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    expect(state, read_value(state, data, ADC0, 1) == 0x1fu,
           "read_value(state, data, ADC0, 1) == 0x1fu");
    write_value(state, data, ADC0 + 8, 1, 0x0cu);
    expect(state, k22_data_set_adc_input(data, 0, 7, 0x0abcu),
           "k22_data_set_adc_input(data, 0, 7, 0x0abcu)");
    write_value(state, data, ADC0, 1, 7u | 0x40u);
    k22_data_advance(data, 5);
    expect(state, (read_value(state, data, ADC0, 1) & 0x80u) == 0,
           "(read_value(state, data, ADC0, 1) & 0x80u) == 0");
    k22_data_advance(data, 100);
    expect(state, (read_value(state, data, ADC0, 1) & 0x80u) != 0,
           "(read_value(state, data, ADC0, 1) & 0x80u) != 0");
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_ADC0], "bus.interrupt[K22_DATA_INTERRUPT_ADC0]");
    expect(state, read_value(state, data, ADC0 + 0x10, 2) == 0x0abcu,
           "read_value(state, data, ADC0 + 0x10, 2) == 0x0abcu");
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_ADC0],
           "!bus.interrupt[K22_DATA_INTERRUPT_ADC0]");
    expect(state, k22_data_set_adc_input(data, 1, 3, 0x0555u),
           "k22_data_set_adc_input(data, 1, 3, 0x0555u)");
    write_value(state, data, ADC1 + 8, 1, 0x0cu);
    write_value(state, data, ADC1 + 0x20, 1, 0x40u);
    write_value(state, data, ADC1, 1, 3u);
    k22_data_advance(data, 100);
    expect(state, (read_value(state, data, ADC1, 1) & 0x80u) == 0,
           "(read_value(state, data, ADC1, 1) & 0x80u) == 0");
    k22_data_adc_trigger(data, 1);
    k22_data_advance(data, 100);
    expect(state, read_value(state, data, ADC1 + 0x10, 2) == 0x0555u,
           "read_value(state, data, ADC1 + 0x10, 2) == 0x0555u");
    write_value(state, data, ADC0 + 0x24, 1, 0x80u);
    expect(state, (read_value(state, data, ADC0 + 0x24, 1) & 0xc0u) == 0,
           "(read_value(state, data, ADC0 + 0x24, 1) & 0xc0u) == 0");
    expect(state, (read_value(state, data, ADC0, 1) & 0x80u) != 0,
           "(read_value(state, data, ADC0, 1) & 0x80u) != 0");
    k22_data_destroy(data);
}

static void test_adc_compare_dma_and_continuous(TestState* state) {
    TestBus bus = {0};
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    expect(state, k22_data_set_adc_input(data, 0, 1, 100u),
           "k22_data_set_adc_input(data, 0, 1, 100u)");
    write_value(state, data, ADC0 + 0x18u, 2, 90u);
    write_value(state, data, ADC0 + 0x1cu, 2, 110u);
    write_value(state, data, ADC0 + 0x20u, 1, 0x28u);
    write_value(state, data, ADC0, 1, 1u);
    k22_data_advance(data, 100u);
    expect(state, (read_value(state, data, ADC0, 1) & 0x80u) != 0u,
           "(read_value(state, data, ADC0, 1) & 0x80u) != 0u");
    expect(state, read_value(state, data, ADC0 + 0x10u, 2) == 100u,
           "read_value(state, data, ADC0 + 0x10u, 2) == 100u");

    write_value(state, data, ADC0 + 0x20u, 1, 0x38u);
    write_value(state, data, ADC0, 1, 1u);
    k22_data_advance(data, 100u);
    expect(state, (read_value(state, data, ADC0, 1) & 0x80u) == 0u,
           "(read_value(state, data, ADC0, 1) & 0x80u) == 0u");
    write_value(state, data, ADC0 + 0x20u, 1, 0x30u);
    write_value(state, data, ADC0 + 0x18u, 2, 99u);
    write_value(state, data, ADC0, 1, 1u);
    k22_data_advance(data, 100u);
    expect(state, (read_value(state, data, ADC0, 1) & 0x80u) != 0u,
           "(read_value(state, data, ADC0, 1) & 0x80u) != 0u");
    expect(state, read_value(state, data, ADC0 + 0x10u, 2) == 100u,
           "read_value(state, data, ADC0 + 0x10u, 2) == 100u");

    store(bus.ram, 0xb00u, 2u, 100u);
    write_tcd(state, data, TCD0, RAM_BASE + 0xb00u, 0, 0, 2u, 0, RAM_BASE + 0xa00u, 0, 1u, 0, 0u);
    write_value(state, data, DMAMUX, 1, 0x80u | 40u);
    write_value(state, data, DMA + 0x1bu, 1, 0u);
    write_value(state, data, ADC0 + 0x20u, 1, 0x04u);
    write_value(state, data, ADC0 + 8u, 1, 0x10u);
    write_value(state, data, ADC0 + 9u, 1, 1u);
    write_value(state, data, ADC0 + 0x24u, 1, 0x0fu);
    write_value(state, data, ADC0, 1, 1u);
    k22_data_advance(data, 1000u);
    k22_data_advance(data, 1u);
    expect(state, load(bus.ram, 0xa00u, 2) == 100u, "load(bus.ram, 0xa00u, 2) == 100u");
    expect(state, (read_value(state, data, ADC0, 1) & 0x80u) != 0u,
           "(read_value(state, data, ADC0, 1) & 0x80u) != 0u");
    k22_data_advance(data, 1000u);
    expect(state, (read_value(state, data, ADC0, 1) & 0x80u) != 0u,
           "(read_value(state, data, ADC0, 1) & 0x80u) != 0u");

    bus.ram[0xb20u] = 0x4du;
    write_tcd(state, data, TCD1, RAM_BASE + 0xb20u, 0, 0, 1u, 0, RAM_BASE + 0xa20u, 0, 1u, 0, 0u);
    write_value(state, data, DMAMUX + 1u, 1, 0x80u | 41u);
    write_value(state, data, DMA + 0x1bu, 1, 1u);
    write_value(state, data, ADC1 + 0x20u, 1, 0x04u);
    write_value(state, data, ADC1, 1, 3u);
    k22_data_advance(data, 100u);
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0xa20u] == 0x4du, "bus.ram[0xa20u] == 0x4du");
    k22_data_destroy(data);
}

static void test_dac_cmp_vref(TestState* state) {
    TestBus bus = {0};
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    write_value(state, data, DAC0, 2, 0x0123u);
    write_value(state, data, DAC0 + 2, 2, 0x0456u);
    write_value(state, data, DAC0 + 0x23, 1, 1u);
    write_value(state, data, DAC0 + 0x21, 1, 0x80u);
    uint16_t output = 0;
    expect(state, k22_data_get_dac_output(data, 0, &output),
           "k22_data_get_dac_output(data, 0, &output)");
    expect(state, output == 0x0123u, "output == 0x0123u");
    write_value(state, data, DAC0 + 0x22, 1, 0x80u);
    k22_data_dac_trigger(data, 0);
    expect(state, k22_data_get_dac_output(data, 0, &output),
           "k22_data_get_dac_output(data, 0, &output)");
    expect(state, output == 0x0456u, "output == 0x0456u");
    k22_data_dac_trigger(data, 0);
    expect(state, (read_value(state, data, DAC0 + 0x20, 1) & 1u) != 0,
           "(read_value(state, data, DAC0 + 0x20, 1) & 1u) != 0");
    write_value(state, data, DAC0 + 0x20, 1, 7u);
    expect(state, read_value(state, data, DAC0 + 0x20, 1) == 0,
           "read_value(state, data, DAC0 + 0x20, 1) == 0");
    write_value(state, data, DAC0 + 0x23, 1, 2u);
    expect(state, read_value(state, data, DAC0 + 0x23, 1) == 2u,
           "read_value(state, data, DAC0 + 0x23, 1) == 2u");
    write_value(state, data, DAC0 + 0x22, 1, 0x81u);
    k22_data_dac_trigger(data, 0);
    expect(state, read_value(state, data, DAC0 + 0x23, 1) == 0x22u,
           "read_value(state, data, DAC0 + 0x23, 1) == 0x22u");
    k22_data_dac_trigger(data, 0);
    expect(state, read_value(state, data, DAC0 + 0x23, 1) == 0x12u,
           "read_value(state, data, DAC0 + 0x23, 1) == 0x12u");

    expect(state, k22_data_set_cmp_input(data, 0, 1, 20), "k22_data_set_cmp_input(data, 0, 1, 20)");
    expect(state, k22_data_set_cmp_input(data, 0, 2, 10), "k22_data_set_cmp_input(data, 0, 2, 10)");
    bool comparator_high = false;
    expect(state, k22_data_get_cmp_output(data, 0, &comparator_high),
           "k22_data_get_cmp_output(data, 0, &comparator_high)");
    expect(state, !comparator_high, "!comparator_high");
    write_value(state, data, CMP0 + 5, 1, (1u << 3) | 2u);
    write_value(state, data, CMP0 + 3, 1, 0x08u);
    write_value(state, data, CMP0 + 1, 1, 1u);
    expect(state, k22_data_get_cmp_output(data, 0, &comparator_high),
           "k22_data_get_cmp_output(data, 0, &comparator_high)");
    expect(state, comparator_high, "comparator_high");
    expect(state, (read_value(state, data, CMP0 + 3, 1) & 5u) == 5u,
           "(read_value(state, data, CMP0 + 3, 1) & 5u) == 5u");
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_CMP0], "bus.interrupt[K22_DATA_INTERRUPT_CMP0]");
    write_value(state, data, CMP0 + 3, 1, 0x0cu);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_CMP0],
           "!bus.interrupt[K22_DATA_INTERRUPT_CMP0]");
    expect(state, k22_data_set_cmp_input(data, 0, 1, 0), "k22_data_set_cmp_input(data, 0, 1, 0)");
    expect(state, (read_value(state, data, CMP0 + 3, 1) & 2u) != 0,
           "(read_value(state, data, CMP0 + 3, 1) & 2u) != 0");
    write_value(state, data, CMP0 + 4u, 1, 0xa0u);
    expect(state, k22_data_set_cmp_input(data, 0, 7, 20), "k22_data_set_cmp_input(data, 0, 7, 20)");
    write_value(state, data, CMP0 + 3u, 1, 0x48u);
    write_value(state, data, CMP0 + 5u, 1, (7u << 3u) | 2u);
    expect(state, (read_value(state, data, CMP0 + 3u, 1) & 1u) != 0u,
           "(read_value(state, data, CMP0 + 3u, 1) & 1u) != 0u");
    expect(state, !k22_data_get_cmp_output(NULL, 0, &comparator_high),
           "!k22_data_get_cmp_output(NULL, 0, &comparator_high)");
    expect(state, !k22_data_get_cmp_output(data, 3, &comparator_high),
           "!k22_data_get_cmp_output(data, 3, &comparator_high)");
    expect(state, !k22_data_get_cmp_output(data, 0, NULL),
           "!k22_data_get_cmp_output(data, 0, NULL)");

    write_value(state, data, VREF + 1, 1, 0x80u);
    expect(state, (read_value(state, data, VREF + 1, 1) & 4u) == 0,
           "(read_value(state, data, VREF + 1, 1) & 4u) == 0");
    k22_data_advance(data, 99);
    expect(state, (read_value(state, data, VREF + 1, 1) & 4u) == 0,
           "(read_value(state, data, VREF + 1, 1) & 4u) == 0");
    k22_data_advance(data, 1);
    expect(state, (read_value(state, data, VREF + 1, 1) & 4u) != 0,
           "(read_value(state, data, VREF + 1, 1) & 4u) != 0");
    k22_data_destroy(data);
}

static void test_rng_crc(TestState* state) {
    TestBus bus = {0};
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    expect(state, read_value(state, data, CRC, 4) == UINT32_MAX,
           "read_value(state, data, CRC, 4) == UINT32_MAX");
    k22_data_rng_seed(data, 1);
    write_value(state, data, RNG, 4, 3u);
    k22_data_advance(data, 63);
    expect(state, (read_value(state, data, RNG + 4, 4) & 1u) == 0,
           "(read_value(state, data, RNG + 4, 4) & 1u) == 0");
    k22_data_advance(data, 1);
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_RNG], "bus.interrupt[K22_DATA_INTERRUPT_RNG]");
    expect(state, read_value(state, data, RNG + 0x0c, 4) == 0x00042021u,
           "read_value(state, data, RNG + 0x0c, 4) == 0x00042021u");
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_RNG], "!bus.interrupt[K22_DATA_INTERRUPT_RNG]");
    write_value(state, data, RNG, 4, 0x10u);
    expect(state, read_value(state, data, RNG + 4, 4) == 0,
           "read_value(state, data, RNG + 4, 4) == 0");

    static const uint8_t message[] = "123456789";
    for (size_t index = 0; index < sizeof(message) - 1; index++)
        write_value(state, data, CRC, 1, message[index]);
    expect(state, read_value(state, data, CRC, 4) == 0x29b1u,
           "read_value(state, data, CRC, 4) == 0x29b1u");
    write_value(state, data, CRC + 8, 4, 0x02000000u);
    write_value(state, data, CRC, 4, 0x1234u);
    expect(state, read_value(state, data, CRC, 4) == 0x1234u,
           "read_value(state, data, CRC, 4) == 0x1234u");
    write_value(state, data, CRC + 8, 4, 0x12000000u);
    write_value(state, data, CRC, 4, 0x01234567u);
    expect(state, read_value(state, data, CRC, 4) == 0x0123a2e6u,
           "read_value(state, data, CRC, 4) == 0x0123a2e6u");
    write_value(state, data, CRC + 8, 4, 0x22000000u);
    write_value(state, data, CRC, 4, 0x01234567u);
    expect(state, read_value(state, data, CRC, 4) == 0x0123e6a2u,
           "read_value(state, data, CRC, 4) == 0x0123e6a2u");
    write_value(state, data, CRC + 8, 4, 0x32000000u);
    write_value(state, data, CRC, 4, 0x01234567u);
    expect(state, read_value(state, data, CRC, 4) == 0x01236745u,
           "read_value(state, data, CRC, 4) == 0x01236745u");
    write_value(state, data, CRC + 8, 4, 0x16000000u);
    write_value(state, data, CRC, 4, 0x01234567u);
    expect(state, read_value(state, data, CRC, 4) == 0x01235d19u,
           "read_value(state, data, CRC, 4) == 0x01235d19u");
    write_value(state, data, CRC + 8, 4, 0x40000000u);
    write_value(state, data, CRC, 2, 0x1234u);
    expect(state, read_value(state, data, CRC, 4) != UINT32_MAX,
           "read_value(state, data, CRC, 4) != UINT32_MAX");
    expect(state, read_value(state, data, RNG + 8u, 4) == 0u,
           "read_value(state, data, RNG + 8u, 4) == 0u");
    k22_data_destroy(data);
}

static void set_flash_address(TestState* state, K22Data* data, uint32_t address);

static void test_flash_flex_copy(TestState* state) {
    TestBus bus = {0};
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FX51212);
    expect(state, read_value(state, data, FTFA, 1) == 0x80u,
           "read_value(state, data, FTFA, 1) == 0x80u");
    write_fccob(state, data, 0u, 0x07u);
    write_fccob(state, data, 1u, 0x00u);
    write_fccob(state, data, 2u, 0x10u);
    write_fccob(state, data, 3u, 0x00u);
    write_fccob(state, data, 4u, 0x12u);
    write_fccob(state, data, 5u, 0x34u);
    write_fccob(state, data, 6u, 0x56u);
    write_fccob(state, data, 7u, 0x78u);
    write_fccob(state, data, 8u, 0x9au);
    write_fccob(state, data, 9u, 0xbcu);
    write_fccob(state, data, 10u, 0xdeu);
    write_fccob(state, data, 11u, 0xf0u);
    write_value(state, data, FTFA, 1, 0x80u);
    expect(state, read_value(state, data, FTFA, 1) == 0, "read_value(state, data, FTFA, 1) == 0");
    expect(state, load(bus.flash, 0x1000, 4) == 0x78563412u,
           "load(bus.flash, 0x1000, 4) == 0x78563412u");
    expect(state, load(bus.flash, 0x1004, 4) == 0xf0debc9au,
           "load(bus.flash, 0x1004, 4) == 0xf0debc9au");
    k22_data_advance(data, 40);
    expect(state, read_value(state, data, FTFA, 1) == 0x80u,
           "read_value(state, data, FTFA, 1) == 0x80u");
    write_fccob(state, data, 0u, 0xffu);
    write_value(state, data, FTFA, 1, 0x80u);
    expect(state, (read_value(state, data, FTFA, 1) & 0x20u) != 0,
           "(read_value(state, data, FTFA, 1) & 0x20u) != 0");
    write_value(state, data, FTFA, 1, 0x20u);
    expect(state, (read_value(state, data, FTFA, 1) & 0x20u) == 0,
           "(read_value(state, data, FTFA, 1) & 0x20u) == 0");

    uint8_t configuration[16];
    memset(configuration, 0xff, sizeof(configuration));
    configuration[8] = 0xfeu;
    configuration[0x0c] = 0xfeu;
    expect(state, k22_data_set_flash_configuration(data, configuration, sizeof(configuration)),
           "k22_data_set_flash_configuration(data, configuration, sizeof(configuration))");
    expect(state, read_value(state, data, 0x408u, 1) == 0xfeu,
           "read_value(state, data, 0x408u, 1) == 0xfeu");
    k22_data_reset(data);
    expect(state, read_value(state, data, FTFA + 0x10, 1) == 0xfeu,
           "read_value(state, data, FTFA + 0x10, 1) == 0xfeu");
    write_fccob(state, data, 0u, 0x07u);
    write_fccob(state, data, 1u, 0x00u);
    write_fccob(state, data, 2u, 0x10u);
    write_fccob(state, data, 3u, 0x00u);
    for (uint8_t index = 4u; index < 12u; index++)
        write_fccob(state, data, index, 0xffu);
    write_value(state, data, FTFA, 1, 0x80u);
    expect(state, (read_value(state, data, FTFA, 1) & 0x10u) != 0,
           "(read_value(state, data, FTFA, 1) & 0x10u) != 0");

    expect(state, read_value(state, data, 0x10000000u, 4) == UINT32_MAX,
           "read_value(state, data, 0x10000000u, 4) == UINT32_MAX");
    write_value(state, data, 0x10000000u, 4, 0x55aa55aau);
    write_value(state, data, 0x10000000u, 4, 0xffff0000u);
    expect(state, read_value(state, data, 0x10000000u, 4) == 0x55aa0000u,
           "read_value(state, data, 0x10000000u, 4) == 0x55aa0000u");
    write_value(state, data, 0x14000000u, 4, 0xdeadbeefu);
    expect(state, read_value(state, data, 0x14000000u, 4) == 0xdeadbeefu,
           "read_value(state, data, 0x14000000u, 4) == 0xdeadbeefu");

    K22Data* copy = create(state, &bus, K22_PROFILE_MK22FX51212);
    expect(state, k22_data_copy(copy, data), "k22_data_copy(copy, data)");
    expect(state, read_value(state, copy, 0x10000000u, 4) == 0x55aa0000u,
           "read_value(state, copy, 0x10000000u, 4) == 0x55aa0000u");
    expect(state, read_value(state, copy, 0x14000000u, 4) == 0xdeadbeefu,
           "read_value(state, copy, 0x14000000u, 4) == 0xdeadbeefu");
    k22_data_reset(copy);
    expect(state, read_value(state, copy, 0x10000000u, 4) == 0x55aa0000u,
           "read_value(state, copy, 0x10000000u, 4) == 0x55aa0000u");
    expect(state, read_value(state, copy, 0x14000000u, 4) == 0,
           "read_value(state, copy, 0x14000000u, 4) == 0");
    k22_data_destroy(copy);
    k22_data_destroy(data);
}

static void test_flash_collision_lifecycle(TestState* state) {
    TestBus bus = {0};
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FX51212);
    write_value(state, data, FTFA + 1u, 1u, 0xc0u);
    write_fccob(state, data, 0u, 0x07u);
    set_flash_address(state, data, 0x1000u);
    for (uint8_t index = 4u; index < 12u; index++)
        write_fccob(state, data, index, index);
    write_value(state, data, FTFA, 1u, 0x80u);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_FTFA],
           "!bus.interrupt[K22_DATA_INTERRUPT_FTFA]");
    expect(state, !k22_data_flash_read(data, false, 0x1000u, 1u),
           "!k22_data_flash_read(data, false, 0x1000u, 1u)");
    expect(state, k22_data_flash_read(data, false, 0x41000u, 1u),
           "k22_data_flash_read(data, false, 0x41000u, 1u)");
    expect(state, k22_data_flash_read(data, true, 0u, 1u),
           "k22_data_flash_read(data, true, 0u, 1u)");
    expect(state, (read_value(state, data, FTFA, 1u) & 0x40u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x40u) != 0u");
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_FLASH_COLLISION],
           "bus.interrupt[K22_DATA_INTERRUPT_FLASH_COLLISION]");
    write_value(state, data, FTFA, 1u, 0x40u);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_FLASH_COLLISION],
           "!bus.interrupt[K22_DATA_INTERRUPT_FLASH_COLLISION]");
    k22_data_advance(data, 39u);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_FTFA],
           "!bus.interrupt[K22_DATA_INTERRUPT_FTFA]");
    k22_data_advance(data, 1u);
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_FTFA], "bus.interrupt[K22_DATA_INTERRUPT_FTFA]");
    write_value(state, data, FTFA + 1u, 1u, 0u);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_FTFA],
           "!bus.interrupt[K22_DATA_INTERRUPT_FTFA]");

    write_fccob(state, data, 0u, 0x07u);
    set_flash_address(state, data, 0x800000u);
    for (uint8_t index = 4u; index < 12u; index++)
        write_fccob(state, data, index, (uint8_t)(index + 0x10u));
    write_value(state, data, FTFA, 1u, 0x80u);
    expect(state, k22_data_flash_read(data, false, 0u, 1u),
           "k22_data_flash_read(data, false, 0u, 1u)");
    expect(state, !k22_data_flash_read(data, true, 0u, 1u),
           "!k22_data_flash_read(data, true, 0u, 1u)");
    k22_data_advance(data, 40u);
    expect(state, k22_data_flash_read(data, true, 0u, 1u),
           "k22_data_flash_read(data, true, 0u, 1u)");
    k22_data_destroy(data);
}

static void set_flash_address(TestState* state, K22Data* data, uint32_t address) {
    write_fccob(state, data, 1u, (uint8_t)(address >> 16u));
    write_fccob(state, data, 2u, (uint8_t)(address >> 8u));
    write_fccob(state, data, 3u, (uint8_t)address);
}

static void launch_flash(TestState* state, K22Data* data, uint32_t cycles) {
    write_value(state, data, FTFA, 1, 0x80u);
    k22_data_advance(data, cycles);
    expect(state, (read_value(state, data, FTFA, 1) & 0x80u) != 0u,
           "(read_value(state, data, FTFA, 1) & 0x80u) != 0u");
}

static void test_flash_controller_geometry(TestState* state) {
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN1M012);
    bus.flash[0x1000] = 0;
    bus.flash[0x1ffc] = 0;
    bus.flash[0x2000] = 0;
    write_fccob(state, data, 0u, 0x09u);
    set_flash_address(state, data, 0x1000u);
    launch_flash(state, data, 2000u);
    expect(state, load(bus.flash, 0x1000u, 4) == UINT32_MAX,
           "load(bus.flash, 0x1000u, 4) == UINT32_MAX");
    expect(state, load(bus.flash, 0x1ffcu, 4) == UINT32_MAX,
           "load(bus.flash, 0x1ffcu, 4) == UINT32_MAX");
    expect(state, bus.flash[0x2000] == 0, "bus.flash[0x2000] == 0");

    write_fccob(state, data, 0u, 0x43u);
    write_fccob(state, data, 1u, 2u);
    const uint8_t once_data[8] = {0x12u, 0x34u, 0x56u, 0x78u, 0x9au, 0xbcu, 0xdeu, 0xf0u};
    for (uint8_t index = 0u; index < sizeof(once_data); index++)
        write_fccob(state, data, (uint8_t)(4u + index), once_data[index]);
    launch_flash(state, data, 40u);
    write_fccob(state, data, 0u, 0x41u);
    write_fccob(state, data, 1u, 2u);
    launch_flash(state, data, 40u);
    for (uint8_t index = 0u; index < sizeof(once_data); index++)
        expect(state, read_fccob(state, data, (uint8_t)(4u + index)) == once_data[index],
               "read_fccob(state, data, (uint8_t)(4u + index)) == once_data[index]");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create(state, &bus, K22_PROFILE_MK22FN51212);
    bus.flash[0x1000] = 0;
    bus.flash[0x17fc] = 0;
    bus.flash[0x1800] = 0;
    write_fccob(state, data, 0u, 0x09u);
    set_flash_address(state, data, 0x1000u);
    launch_flash(state, data, 2000u);
    expect(state, load(bus.flash, 0x1000u, 4) == UINT32_MAX,
           "load(bus.flash, 0x1000u, 4) == UINT32_MAX");
    expect(state, load(bus.flash, 0x17fcu, 4) == UINT32_MAX,
           "load(bus.flash, 0x17fcu, 4) == UINT32_MAX");
    expect(state, bus.flash[0x1800] == 0, "bus.flash[0x1800] == 0");
    write_fccob(state, data, 0u, 0x07u);
    set_flash_address(state, data, 0x2000u);
    write_value(state, data, FTFA, 1, 0x80u);
    expect(state, (read_value(state, data, FTFA, 1) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1) & 0x20u) != 0u");
    k22_data_destroy(data);
}

static void flash_command(TestState* state, K22Data* data, uint8_t command, uint32_t address,
                          uint32_t cycles) {
    write_fccob(state, data, 0u, command);
    set_flash_address(state, data, address);
    launch_flash(state, data, cycles);
}

static void flash_command_without_address(TestState* state, K22Data* data, uint8_t command,
                                          uint32_t cycles) {
    write_fccob(state, data, 0u, command);
    launch_flash(state, data, cycles);
}

static void test_flash_commands_and_failures(TestState* state) {
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN1M012);

    bus.flash[0x100u] = 0u;
    bus.flash[0x7fffcu] = 0u;
    bus.flash[0x80000u] = 0u;
    flash_command(state, data, 0x08u, 0x100u, 2000u);
    expect(state, bus.flash[0x100u] == 0xffu, "bus.flash[0x100u] == 0xffu");
    expect(state, bus.flash[0x7fffcu] == 0xffu, "bus.flash[0x7fffcu] == 0xffu");
    expect(state, bus.flash[0x80000u] == 0u, "bus.flash[0x80000u] == 0u");

    bus.flash[0u] = 0u;
    bus.flash[0xfffffu] = 0u;
    flash_command(state, data, 0x44u, 0u, 2000u);
    expect(state, bus.flash[0u] == 0xffu, "bus.flash[0u] == 0xffu");
    expect(state, bus.flash[0xfffffu] == 0xffu, "bus.flash[0xfffffu] == 0xffu");

    write_fccob(state, data, 4u, 0u);
    write_fccob(state, data, 5u, 1u);
    write_fccob(state, data, 6u, 0u);
    flash_command(state, data, 0x01u, 0x1000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1) & 1u) == 0u,
           "(read_value(state, data, FTFA, 1) & 1u) == 0u");
    bus.flash[0x1000u] = 0u;
    flash_command(state, data, 0x01u, 0x1000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1) & 1u) != 0u,
           "(read_value(state, data, FTFA, 1) & 1u) != 0u");
    write_value(state, data, FTFA, 1, 1u);
    memset(bus.flash, 0xff, sizeof(bus.flash));
    write_fccob(state, data, 1u, 0u);
    flash_command(state, data, 0x40u, 0u, 40u);
    expect(state, (read_value(state, data, FTFA, 1) & 1u) == 0u,
           "(read_value(state, data, FTFA, 1) & 1u) == 0u");

    store(bus.flash, 0x2000u, 4, 0x12345678u);
    write_fccob(state, data, 4u, 1u);
    write_fccob(state, data, 8u, 0x78u);
    write_fccob(state, data, 9u, 0x56u);
    write_fccob(state, data, 10u, 0x34u);
    write_fccob(state, data, 11u, 0x12u);
    flash_command(state, data, 0x02u, 0x2000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1) & 1u) == 0u,
           "(read_value(state, data, FTFA, 1) & 1u) == 0u");
    write_fccob(state, data, 8u, 0x87u);
    write_fccob(state, data, 9u, 0x65u);
    write_fccob(state, data, 10u, 0x43u);
    write_fccob(state, data, 11u, 0x21u);
    flash_command(state, data, 0x02u, 0x2000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1) & 1u) != 0u,
           "(read_value(state, data, FTFA, 1) & 1u) != 0u");
    write_value(state, data, FTFA, 1, 1u);

    bus.fail_read = true;
    for (uint8_t index = 4u; index < 12u; index++)
        write_fccob(state, data, index, (uint8_t)(index * 17u));
    flash_command(state, data, 0x07u, 0x3000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1) & 0x20u) != 0u");
    bus.fail_read = false;
    write_value(state, data, FTFA, 1, 0x20u);
    bus.fail_write = true;
    flash_command(state, data, 0x09u, 0x4000u, 2000u);
    expect(state, (read_value(state, data, FTFA, 1) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1) & 0x20u) != 0u");
    bus.fail_write = false;
    write_value(state, data, FTFA, 1, 0x20u);

    write_fccob(state, data, 4u, 1u);
    flash_command(state, data, 0x03u, 8u, 40u);
    expect(state, (read_value(state, data, FTFA, 1) & 0x30u) == 0u,
           "(read_value(state, data, FTFA, 1) & 0x30u) == 0u");
    flash_command(state, data, 0x45u, 0u, 40u);
    expect(state, (read_value(state, data, FTFA, 1) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1) & 0x20u) != 0u");
    write_value(state, data, FTFA, 1, 0x20u);
    write_fccob(state, data, 0u, 0x43u);
    write_fccob(state, data, 1u, 1u);
    for (uint8_t index = 4u; index < 12u; index++)
        write_fccob(state, data, index, (uint8_t)(index * 19u));
    launch_flash(state, data, 40u);
    launch_flash(state, data, 40u);
    expect(state, (read_value(state, data, FTFA, 1) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1) & 0x20u) != 0u");
    write_value(state, data, FTFA, 1, 0x20u);
    write_value(state, data, FTFA + 1u, 1, 0x80u);
    flash_command(state, data, 0x03u, 8u, 40u);
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_FTFA], "bus.interrupt[K22_DATA_INTERRUPT_FTFA]");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create_without_program(state, &bus, K22_PROFILE_MK22FN51212);
    write_fccob(state, data, 0u, 0x06u);
    set_flash_address(state, data, 0x1000u);
    write_fccob(state, data, 4u, 0x78u);
    write_fccob(state, data, 5u, 0x56u);
    write_fccob(state, data, 6u, 0x34u);
    write_fccob(state, data, 7u, 0x12u);
    launch_flash(state, data, 40u);
    expect(state, load(bus.flash, 0x1000u, 4) == 0x12345678u,
           "load(bus.flash, 0x1000u, 4) == 0x12345678u");
    k22_data_destroy(data);
}

static void clear_flash_status(TestState* state, K22Data* data) {
    write_value(state, data, FTFA, 1u, 0x70u);
}

static void set_flash_data(TestState* state, K22Data* data, const uint8_t* bytes, uint8_t length) {
    for (uint8_t index = 0u; index < length; index++)
        write_fccob(state, data, (uint8_t)(4u + index), bytes[index]);
}

static void test_flash_command_semantics(TestState* state) {
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    const uint8_t longword[4] = {0x11u, 0x22u, 0x33u, 0x44u};
    set_flash_data(state, data, longword, sizeof(longword));
    flash_command(state, data, 0x06u, 0x2000u, 40u);
    expect(state, load(bus.flash, 0x2000u, 4u) == 0x44332211u,
           "load(bus.flash, 0x2000u, 4u) == 0x44332211u");
    flash_command(state, data, 0x06u, 0x2000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 1u) != 0u");
    clear_flash_status(state, data);
    flash_command(state, data, 0x06u, 0x2001u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    clear_flash_status(state, data);
    flash_command(state, data, 0x07u, 0x3000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    clear_flash_status(state, data);

    write_fccob(state, data, 1u, 0u);
    set_flash_data(state, data, longword, sizeof(longword));
    flash_command(state, data, 0x43u, 0u, 40u);
    flash_command(state, data, 0x41u, 0u, 40u);
    for (uint8_t index = 0u; index < sizeof(longword); index++)
        expect(state, read_fccob(state, data, (uint8_t)(4u + index)) == longword[index],
               "read_fccob(state, data, (uint8_t)(4u + index)) == longword[index]");
    flash_command(state, data, 0x43u, 0u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    clear_flash_status(state, data);
    write_fccob(state, data, 8u, 1u);
    flash_command(state, data, 0x03u, 0u, 40u);
    expect(state, read_fccob(state, data, 4u) == 0x01u, "read_fccob(state, data, 4u) == 0x01u");
    expect(state, read_fccob(state, data, 6u) == 0x46u, "read_fccob(state, data, 6u) == 0x46u");
    expect(state, read_fccob(state, data, 7u) == 0x54u, "read_fccob(state, data, 7u) == 0x54u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create(state, &bus, K22_PROFILE_MK22FN1M012);
    uint8_t erased_configuration[16];
    memset(erased_configuration, 0xff, sizeof(erased_configuration));
    expect(
        state,
        k22_data_set_flash_configuration(data, erased_configuration, sizeof(erased_configuration)),
        "k22_data_set_flash_configuration(data, erased_configuration, "
        "sizeof(erased_configuration))");
    k22_data_reset(data);
    write_fccob(state, data, 4u, 0u);
    flash_command(state, data, 0x00u, 0x00000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 1u) == 0u,
           "(read_value(state, data, FTFA, 1u) & 1u) == 0u");
    bus.flash[0x7fffcu] = 0u;
    flash_command(state, data, 0x00u, 0x00000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 1u) != 0u");
    clear_flash_status(state, data);
    write_fccob(state, data, 4u, 0u);
    write_fccob(state, data, 5u, 2u);
    write_fccob(state, data, 6u, 0u);
    bus.flash[0x1010u] = 0u;
    flash_command(state, data, 0x01u, 0x1000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 1u) != 0u");
    clear_flash_status(state, data);
    write_fccob(state, data, 4u, 0u);
    write_fccob(state, data, 5u, 0u);
    flash_command(state, data, 0x01u, 0x1000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    clear_flash_status(state, data);

    for (uint8_t index = 0u; index < 16u; index++)
        write_value(state, data, 0x14000000u + index, 1u, (uint8_t)(0x80u + index));
    write_fccob(state, data, 4u, 0u);
    write_fccob(state, data, 5u, 1u);
    flash_command(state, data, 0x0bu, 0x3000u, 40u);
    for (uint8_t index = 0u; index < 16u; index++)
        expect(state, bus.flash[0x3000u + index] == (uint8_t)(0x80u + index),
               "bus.flash[0x3000u + index] == (uint8_t)(0x80u + index)");
    flash_command(state, data, 0x0bu, 0x3000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 1u) != 0u");
    clear_flash_status(state, data);
    write_fccob(state, data, 4u, 0u);
    write_fccob(state, data, 5u, 0u);
    flash_command(state, data, 0x0bu, 0x4000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    clear_flash_status(state, data);
    write_fccob(state, data, 4u, 8u);
    flash_command(state, data, 0x46u, 0x4000u, 40u);
    expect(state, read_fccob(state, data, 5u) == 0u, "read_fccob(state, data, 5u) == 0u");
    expect(state, read_fccob(state, data, 6u) == 0u, "read_fccob(state, data, 6u) == 0u");
    expect(state, read_fccob(state, data, 7u) == 0u, "read_fccob(state, data, 7u) == 0u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create(state, &bus, K22_PROFILE_MK22FX51212);
    uint8_t flex_configuration[16];
    memset(flex_configuration, 0xff, sizeof(flex_configuration));
    expect(state,
           k22_data_set_flash_configuration(data, flex_configuration, sizeof(flex_configuration)),
           "k22_data_set_flash_configuration(data, flex_configuration, "
           "sizeof(flex_configuration))");
    k22_data_reset(data);
    write_fccob(state, data, 3u, 0u);
    write_fccob(state, data, 4u, 2u);
    write_fccob(state, data, 5u, 3u);
    flash_command_without_address(state, data, 0x80u, 2000u);
    expect(state, (read_value(state, data, FTFA + 1u, 1u) & 3u) == 1u,
           "(read_value(state, data, FTFA + 1u, 1u) & 3u) == 1u");
    expect(state, read_value(state, data, 0x10000000u, 4u) == UINT32_MAX,
           "read_value(state, data, 0x10000000u, 4u) == UINT32_MAX");
    k22_data_reset(data);
    expect(state, (read_value(state, data, FTFA + 1u, 1u) & 3u) == 1u,
           "(read_value(state, data, FTFA + 1u, 1u) & 3u) == 1u");
    flash_command_without_address(state, data, 0x80u, 2000u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    clear_flash_status(state, data);
    write_fccob(state, data, 1u, 0xffu);
    flash_command_without_address(state, data, 0x81u, 40u);
    expect(state, (read_value(state, data, FTFA + 1u, 1u) & 3u) == 2u,
           "(read_value(state, data, FTFA + 1u, 1u) & 3u) == 2u");
    expect(state, read_value(state, data, 0x14000000u, 4u) == UINT32_MAX,
           "read_value(state, data, 0x14000000u, 4u) == UINT32_MAX");
    write_fccob(state, data, 1u, 0u);
    flash_command_without_address(state, data, 0x81u, 40u);
    expect(state, (read_value(state, data, FTFA + 1u, 1u) & 3u) == 1u,
           "(read_value(state, data, FTFA + 1u, 1u) & 3u) == 1u");
    write_fccob(state, data, 1u, 1u);
    flash_command_without_address(state, data, 0x81u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    clear_flash_status(state, data);
    const uint8_t phrase[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    set_flash_data(state, data, phrase, sizeof(phrase));
    flash_command(state, data, 0x07u, 0x800000u, 40u);
    expect(state, read_value(state, data, 0x10000000u, 4u) == 0x04030201u,
           "read_value(state, data, 0x10000000u, 4u) == 0x04030201u");
    expect(state, read_value(state, data, 0x10000004u, 4u) == 0x08070605u,
           "read_value(state, data, 0x10000004u, 4u) == 0x08070605u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create(state, &bus, K22_PROFILE_MK22FX51212);
    expect(state,
           k22_data_set_flash_configuration(data, flex_configuration, sizeof(flex_configuration)),
           "k22_data_set_flash_configuration(data, flex_configuration, "
           "sizeof(flex_configuration))");
    k22_data_reset(data);
    write_fccob(state, data, 4u, 0u);
    flash_command(state, data, 0x03u, 0u, 40u);
    expect(state, read_fccob(state, data, 4u) == 0xffu, "read_fccob(state, data, 4u) == 0xffu");
    write_fccob(state, data, 4u, 0u);
    flash_command(state, data, 0x00u, 0x800000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 1u) == 0u,
           "(read_value(state, data, FTFA, 1u) & 1u) == 0u");
    write_fccob(state, data, 4u, 0u);
    flash_command(state, data, 0x03u, 0x800000u, 40u);
    expect(state, read_fccob(state, data, 4u) == 0xffu, "read_fccob(state, data, 4u) == 0xffu");
    write_fccob(state, data, 3u, 0u);
    write_fccob(state, data, 4u, 0x02u);
    write_fccob(state, data, 5u, 0x04u);
    flash_command_without_address(state, data, 0x80u, 2000u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) == 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) == 0u");
    write_fccob(state, data, 4u, 0u);
    flash_command(state, data, 0x00u, 0x800000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    clear_flash_status(state, data);
    write_value(state, data, 0x10000000u, 1u, 0u);
    write_fccob(state, data, 1u, 0u);
    flash_command_without_address(state, data, 0x40u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 1u) != 0u");
    clear_flash_status(state, data);
    flash_command(state, data, 0x08u, 0x800000u, 2000u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    expect(state, read_value(state, data, 0x10000000u, 1u) == 0u,
           "read_value(state, data, 0x10000000u, 1u) == 0u");
    clear_flash_status(state, data);
    bus.flash[0u] = 0u;
    flash_command(state, data, 0x08u, 0u, 2000u);
    expect(state, bus.flash[0u] == 0xffu, "bus.flash[0u] == 0xffu");
    write_value(state, data, 0x10000000u, 1u, 0u);
    flash_command(state, data, 0x44u, 0u, 2000u);
    expect(state, read_value(state, data, 0x10000000u, 1u) == 0xffu,
           "read_value(state, data, 0x10000000u, 1u) == 0xffu");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create(state, &bus, K22_PROFILE_MK22FN51212);
    bus.flash[0u] = 0u;
    flash_command(state, data, 0x08u, 0u, 2000u);
    expect(state, bus.flash[0u] == 0xffu, "bus.flash[0u] == 0xffu");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create(state, &bus, K22_PROFILE_MK22FN51212);
    write_fccob(state, data, 1u, 0x10u);
    const uint8_t once_phrase[8] = {9u, 8u, 7u, 6u, 5u, 4u, 3u, 2u};
    set_flash_data(state, data, once_phrase, sizeof(once_phrase));
    flash_command_without_address(state, data, 0x43u, 40u);
    write_fccob(state, data, 1u, 0x10u);
    flash_command_without_address(state, data, 0x41u, 40u);
    for (uint8_t index = 0u; index < sizeof(once_phrase); index++)
        expect(state, read_fccob(state, data, (uint8_t)(4u + index)) == once_phrase[index],
               "read_fccob(state, data, (uint8_t)(4u + index)) == once_phrase[index]");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create(state, &bus, K22_PROFILE_MK22FN1M012);
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
    flash_command(state, data, 0x08u, 0u, 2000u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x10u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x10u) != 0u");
    expect(state, bus.flash[0u] == 0u, "bus.flash[0u] == 0u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create(state, &bus, K22_PROFILE_MK22FN51212);
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
    set_flash_data(state, data, configuration, 8u);
    flash_command(state, data, 0x45u, 0u, 40u);
    expect(state, (read_value(state, data, FTFA + 2u, 1u) & 3u) == 2u,
           "(read_value(state, data, FTFA + 2u, 1u) & 3u) == 2u");
    k22_data_reset(data);
    configuration[0] ^= 0xffu;
    set_flash_data(state, data, configuration, 8u);
    flash_command(state, data, 0x45u, 0u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    clear_flash_status(state, data);
    configuration[0] ^= 0xffu;
    set_flash_data(state, data, configuration, 8u);
    flash_command(state, data, 0x45u, 0u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_destroy(data);
}

static void test_flash_command_guards(TestState* state) {
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN1M012);
    bus.flash[0u] = 0u;
    flash_command(state, data, 0x08u, 1u, 2000u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    expect(state, bus.flash[0u] == 0u, "bus.flash[0u] == 0u");
    clear_flash_status(state, data);
    flash_command(state, data, 0x09u, 1u, 2000u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    expect(state, bus.flash[0u] == 0u, "bus.flash[0u] == 0u");
    clear_flash_status(state, data);
    write_fccob(state, data, 4u, 0u);
    flash_command(state, data, 0x03u, 1u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create(state, &bus, K22_PROFILE_MK22FN51212);
    write_fccob(state, data, 8u, 0u);
    flash_command(state, data, 0x03u, 1u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create(state, &bus, K22_PROFILE_MK22FX51212);
    uint8_t configuration[16];
    memset(configuration, 0xff, sizeof(configuration));
    expect(state, k22_data_set_flash_configuration(data, configuration, sizeof(configuration)),
           "k22_data_set_flash_configuration(data, configuration, sizeof(configuration))");
    k22_data_reset(data);
    write_fccob(state, data, 3u, 0u);
    write_fccob(state, data, 4u, 2u);
    write_fccob(state, data, 5u, 3u);
    flash_command_without_address(state, data, 0x80u, 2000u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) == 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) == 0u");
    write_fccob(state, data, 4u, 0u);
    flash_command(state, data, 0x00u, 0x800000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    clear_flash_status(state, data);
    write_value(state, data, 0x10000000u, 1u, 0u);
    flash_command(state, data, 0x08u, 0x817ff0u, 2000u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    expect(state, read_value(state, data, 0x10000000u, 1u) == 0u,
           "read_value(state, data, 0x10000000u, 1u) == 0u");
    k22_data_destroy(data);
}

static void test_flash_swap_lifecycle(TestState* state) {
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN1M012);

    write_fccob(state, data, 4u, 8u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, read_fccob(state, data, 5u) == 0u, "read_fccob(state, data, 5u) == 0u");
    expect(state, read_fccob(state, data, 6u) == 0u, "read_fccob(state, data, 6u) == 0u");
    expect(state, read_fccob(state, data, 7u) == 0u, "read_fccob(state, data, 7u) == 0u");

    write_fccob(state, data, 4u, 1u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x21u) == 0u,
           "(read_value(state, data, FTFA, 1u) & 0x21u) == 0u");
    expect(state, bus.flash[0x1000u] == 0u, "bus.flash[0x1000u] == 0u");
    expect(state, bus.flash[0x1001u] == 0xffu, "bus.flash[0x1001u] == 0xffu");
    const uint8_t phrase[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    set_flash_data(state, data, phrase, sizeof(phrase));
    flash_command(state, data, 0x07u, 0x1000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x10u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x10u) != 0u");
    expect(state, bus.flash[0x1001u] == 0xffu, "bus.flash[0x1001u] == 0xffu");
    clear_flash_status(state, data);
    flash_command(state, data, 0x09u, 0x1000u, 2000u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x10u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x10u) != 0u");
    expect(state, bus.flash[0x1001u] == 0xffu, "bus.flash[0x1001u] == 0xffu");
    clear_flash_status(state, data);
    write_fccob(state, data, 4u, 8u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, read_fccob(state, data, 5u) == 3u, "read_fccob(state, data, 5u) == 3u");
    expect(state, read_fccob(state, data, 6u) == 0u, "read_fccob(state, data, 6u) == 0u");
    expect(state, read_fccob(state, data, 7u) == 0u, "read_fccob(state, data, 7u) == 0u");

    write_fccob(state, data, 4u, 4u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, bus.flash[0x1000u] == 0u, "bus.flash[0x1000u] == 0u");
    expect(state, bus.flash[0x1001u] == 0u, "bus.flash[0x1001u] == 0u");
    flash_command(state, data, 0x09u, 0x81000u, 2000u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x10u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x10u) != 0u");
    expect(state, bus.flash[0x81000u] == 0xffu, "bus.flash[0x81000u] == 0xffu");
    clear_flash_status(state, data);
    k22_data_reset(data);
    expect(state, (read_value(state, data, FTFA + 1u, 1u) & 8u) != 0u,
           "(read_value(state, data, FTFA + 1u, 1u) & 8u) != 0u");
    expect(state, k22_data_program_flash_address(data, 0x20u) == 0x80020u,
           "k22_data_program_flash_address(data, 0x20u) == 0x80020u");
    expect(state, k22_data_program_flash_address(data, 0x80020u) == 0x20u,
           "k22_data_program_flash_address(data, 0x80020u) == 0x20u");
    write_fccob(state, data, 4u, 8u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, read_fccob(state, data, 5u) == 1u, "read_fccob(state, data, 5u) == 1u");
    expect(state, read_fccob(state, data, 6u) == 1u, "read_fccob(state, data, 6u) == 1u");
    expect(state, read_fccob(state, data, 7u) == 1u, "read_fccob(state, data, 7u) == 1u");

    write_fccob(state, data, 4u, 2u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, bus.flash[0x81000u] == 0u, "bus.flash[0x81000u] == 0u");
    expect(state, bus.flash[0x81001u] == 0xffu, "bus.flash[0x81001u] == 0xffu");
    flash_command(state, data, 0x09u, 0x81000u, 2000u);
    write_fccob(state, data, 4u, 8u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, read_fccob(state, data, 5u) == 3u, "read_fccob(state, data, 5u) == 3u");
    expect(state, read_fccob(state, data, 6u) == 1u, "read_fccob(state, data, 6u) == 1u");
    expect(state, read_fccob(state, data, 7u) == 1u, "read_fccob(state, data, 7u) == 1u");
    write_fccob(state, data, 4u, 4u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, bus.flash[0x81000u] == 0u, "bus.flash[0x81000u] == 0u");
    expect(state, bus.flash[0x81001u] == 0u, "bus.flash[0x81001u] == 0u");
    k22_data_reset(data);
    expect(state, (read_value(state, data, FTFA + 1u, 1u) & 8u) == 0u,
           "(read_value(state, data, FTFA + 1u, 1u) & 8u) == 0u");
    expect(state, k22_data_program_flash_address(data, 0x20u) == 0x20u,
           "k22_data_program_flash_address(data, 0x20u) == 0x20u");

    write_fccob(state, data, 4u, 1u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    clear_flash_status(state, data);
    write_fccob(state, data, 4u, 8u);
    flash_command(state, data, 0x46u, 0x1001u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    clear_flash_status(state, data);
    flash_command(state, data, 0x46u, 0x400u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
    k22_data_destroy(data);
}

static void test_flash_swap_indicator_failures(TestState* state) {
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN1M012);
    bus.fail_write = true;
    write_fccob(state, data, 4u, 1u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 1u) != 0u");
    bus.fail_write = false;
    write_fccob(state, data, 4u, 8u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, read_fccob(state, data, 5u) == 0u, "read_fccob(state, data, 5u) == 0u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create(state, &bus, K22_PROFILE_MK22FN1M012);
    write_fccob(state, data, 4u, 1u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    bus.fail_write = true;
    write_fccob(state, data, 4u, 4u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 1u) != 0u");
    bus.fail_write = false;
    write_fccob(state, data, 4u, 8u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, read_fccob(state, data, 5u) == 3u, "read_fccob(state, data, 5u) == 3u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create(state, &bus, K22_PROFILE_MK22FN1M012);
    write_fccob(state, data, 4u, 1u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    write_fccob(state, data, 4u, 4u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    k22_data_reset(data);
    bus.fail_write = true;
    write_fccob(state, data, 4u, 2u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, (read_value(state, data, FTFA, 1u) & 1u) != 0u,
           "(read_value(state, data, FTFA, 1u) & 1u) != 0u");
    bus.fail_write = false;
    write_fccob(state, data, 4u, 8u);
    flash_command(state, data, 0x46u, 0x1000u, 40u);
    expect(state, read_fccob(state, data, 5u) == 1u, "read_fccob(state, data, 5u) == 1u");
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
        K22Data* data = create(state, &bus, K22_PROFILE_MK22FX51212);
        write_fccob(state, data, 3u, 0u);
        write_fccob(state, data, 4u, cases[index].data_size == 0x20000u ? 0x0fu : 2u);
        write_fccob(state, data, 5u, cases[index].code);
        flash_command_without_address(state, data, 0x80u, 2000u);
        expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) == 0u,
               "(read_value(state, data, FTFA, 1u) & 0x20u) == 0u");
        set_flash_data(state, data, phrase, sizeof(phrase));
        const uint32_t address = cases[index].data_size == 0u
                                     ? 0x800000u
                                     : 0x800000u + cases[index].data_size - sizeof(phrase);
        flash_command(state, data, 0x07u, address, 40u);
        expect(state,
               ((read_value(state, data, FTFA, 1u) & 0x20u) != 0u) ==
                   (cases[index].data_size == 0u),
               "((read_value(state, data, FTFA, 1u) & 0x20u) != 0u) == "
               "(cases[index].data_size == 0u)");
        if (cases[index].data_size != 0u) {
            expect(state,
                   read_value(state, data, 0x10000000u + cases[index].data_size - 8u, 4u) ==
                       0x07050301u,
                   "read_value(state, data, 0x10000000u + cases[index].data_size - 8u, 4u) "
                   "== 0x07050301u");
            clear_flash_status(state, data);
            flash_command(state, data, 0x07u, 0x800000u + cases[index].data_size, 40u);
            expect(state, (read_value(state, data, FTFA, 1u) & 0x20u) != 0u,
                   "(read_value(state, data, FTFA, 1u) & 0x20u) != 0u");
        }
        k22_data_destroy(data);
    }
}

int main(void) {
    TestState state = {0};
    k22_data_reset(NULL);
    test_profile_boundaries(&state);
    test_dma(&state);
    test_dma_advanced(&state);
    test_dmamux_triggers(&state);
    test_dmamux_source_matrix(&state);
    test_dma_arbitration_and_control(&state);
    test_adc(&state);
    test_adc_compare_dma_and_continuous(&state);
    test_dac_cmp_vref(&state);
    test_rng_crc(&state);
    test_flash_flex_copy(&state);
    test_flash_collision_lifecycle(&state);
    test_flash_controller_geometry(&state);
    test_flash_commands_and_failures(&state);
    test_flash_command_semantics(&state);
    test_flash_command_guards(&state);
    test_flash_swap_lifecycle(&state);
    test_flash_swap_indicator_failures(&state);
    test_flash_partition_codes(&state);
    return test_finish(&state);
}
