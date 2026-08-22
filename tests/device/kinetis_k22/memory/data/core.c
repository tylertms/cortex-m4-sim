#include "device/kinetis_k22/memory/data/internal.h"

uint32_t k22_data_test_load(const uint8_t* bytes, uint32_t offset, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; index++)
        value |= (uint32_t)bytes[offset + index] << (index * 8u);
    return value;
}

void k22_data_test_store(uint8_t* bytes, uint32_t offset, uint8_t size, uint32_t value) {
    for (uint8_t index = 0; index < size; index++)
        bytes[offset + index] = (uint8_t)(value >> (index * 8u));
}

static bool bus_read(void* context, uint32_t address, uint8_t size, uint32_t* value) {
    TestBus* bus = context;
    if (bus->fail_read)
        return false;
    if (address <= sizeof(bus->flash) - size) {
        *value = k22_data_test_load(bus->flash, address, size);
        return true;
    }
    if (address >= RAM_BASE && address - RAM_BASE <= sizeof(bus->ram) - size) {
        *value = k22_data_test_load(bus->ram, address - RAM_BASE, size);
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
        k22_data_test_store(bus->flash, address, size, value);
        return true;
    }
    if (address >= RAM_BASE && address - RAM_BASE <= sizeof(bus->ram) - size) {
        k22_data_test_store(bus->ram, address - RAM_BASE, size, value);
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

K22Data* k22_data_test_create(TestState* state, TestBus* bus, K22ProfileId profile) {
    K22DataBus callbacks = {bus, bus_read, bus_write, bus_write, bus_interrupt, NULL};
    K22Data* data = k22_data_create(k22_profile_get(profile), callbacks);
    expect(state, data != NULL, "data != NULL");
    bus->data = data;
    return data;
}

K22Data* k22_data_test_create_without_program(TestState* state, TestBus* bus,
                                              K22ProfileId profile) {
    K22DataBus callbacks = {bus, bus_read, bus_write, NULL, bus_interrupt, NULL};
    K22Data* data = k22_data_create(k22_profile_get(profile), callbacks);
    expect(state, data != NULL, "data != NULL");
    bus->data = data;
    return data;
}

uint32_t k22_data_test_read_value(TestState* state, K22Data* data, uint32_t address, uint8_t size) {
    uint32_t value = 0;
    expect(state, k22_data_read(data, address, size, &value),
           "k22_data_read(data, address, size, &value)");
    return value;
}

void k22_data_test_write_value(TestState* state, K22Data* data, uint32_t address, uint8_t size,
                               uint32_t value) {
    expect(state, k22_data_write(data, address, size, value),
           "k22_data_write(data, address, size, value)");
}

static uint32_t flash_fccob_address(uint8_t index) {
    static const uint8_t offsets[12] = {7u, 6u, 5u, 4u, 11u, 10u, 9u, 8u, 15u, 14u, 13u, 12u};
    return FTFA + offsets[index];
}

void k22_data_test_write_fccob(TestState* state, K22Data* data, uint8_t index, uint8_t value) {
    k22_data_test_write_value(state, data, flash_fccob_address(index), 1u, value);
}

uint8_t k22_data_test_read_fccob(TestState* state, K22Data* data, uint8_t index) {
    return (uint8_t)k22_data_test_read_value(state, data, flash_fccob_address(index), 1u);
}

static void write_tcd(TestState* state, K22Data* data, uint32_t base, uint32_t source,
                      int16_t source_offset, uint16_t attributes, uint32_t bytes,
                      int32_t source_last, uint32_t destination, int16_t destination_offset,
                      uint16_t iterations, int32_t destination_last, uint16_t control) {
    k22_data_test_write_value(state, data, base, 4, source);
    k22_data_test_write_value(state, data, base + 4, 2, (uint16_t)source_offset);
    k22_data_test_write_value(state, data, base + 6, 2, attributes);
    k22_data_test_write_value(state, data, base + 8, 4, bytes);
    k22_data_test_write_value(state, data, base + 0x0c, 4, (uint32_t)source_last);
    k22_data_test_write_value(state, data, base + 0x10, 4, destination);
    k22_data_test_write_value(state, data, base + 0x14, 2, (uint16_t)destination_offset);
    k22_data_test_write_value(state, data, base + 0x16, 2, iterations);
    k22_data_test_write_value(state, data, base + 0x18, 4, (uint32_t)destination_last);
    k22_data_test_write_value(state, data, base + 0x1c, 2, control);
    k22_data_test_write_value(state, data, base + 0x1e, 2, iterations);
}

static void store_tcd(TestBus* bus, uint32_t base, uint32_t source, int16_t source_offset,
                      uint16_t attributes, uint32_t bytes, int32_t source_last,
                      uint32_t destination, int16_t destination_offset, uint16_t iterations,
                      int32_t destination_last, uint16_t control) {
    const uint32_t offset = base - RAM_BASE;
    k22_data_test_store(bus->ram, offset, 4, source);
    k22_data_test_store(bus->ram, offset + 4, 2, (uint16_t)source_offset);
    k22_data_test_store(bus->ram, offset + 6, 2, attributes);
    k22_data_test_store(bus->ram, offset + 8, 4, bytes);
    k22_data_test_store(bus->ram, offset + 0x0c, 4, (uint32_t)source_last);
    k22_data_test_store(bus->ram, offset + 0x10, 4, destination);
    k22_data_test_store(bus->ram, offset + 0x14, 2, (uint16_t)destination_offset);
    k22_data_test_store(bus->ram, offset + 0x16, 2, iterations);
    k22_data_test_store(bus->ram, offset + 0x18, 4, (uint32_t)destination_last);
    k22_data_test_store(bus->ram, offset + 0x1c, 2, control);
    k22_data_test_store(bus->ram, offset + 0x1e, 2, iterations);
}

void k22_data_test_test_profile_boundaries(TestState* state) {
    TestBus bus = {0};
    K22Data* small = k22_data_test_create(state, &bus, K22_PROFILE_MK22F12810);
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
    K22Data* large = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    expect(state, k22_data_read(large, RNG, 4, &value), "k22_data_read(large, RNG, 4, &value)");
    expect(state, k22_data_read(large, DAC1, 1, &value), "k22_data_read(large, DAC1, 1, &value)");
    k22_data_destroy(large);
}

void k22_data_test_test_dma(TestState* state) {
    TestBus bus = {0};
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    for (uint8_t index = 0; index < 8; index++)
        bus.ram[index] = (uint8_t)(0x80u + index);
    write_tcd(state, data, TCD0, RAM_BASE, 2, 0x0101u, 4, -8, RAM_BASE + 0x100, 2, 2, -8, 0x0eu);
    k22_data_test_write_value(state, data, DMAMUX, 1, 0x80u | 16u);
    k22_data_test_write_value(state, data, DMA + 0x1b, 1, 0);
    k22_data_dma_request(data, 16u);
    k22_data_advance(data, 1);
    expect(state, k22_data_test_load(bus.ram, 0x100, 4) == 0x83828180u,
           "k22_data_test_load(bus.ram, 0x100, 4) == 0x83828180u");
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_DMA0], "bus.interrupt[K22_DATA_INTERRUPT_DMA0]");
    k22_data_test_write_value(state, data, DMA + 0x1f, 1, 0);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_DMA0],
           "!bus.interrupt[K22_DATA_INTERRUPT_DMA0]");
    k22_data_dma_request(data, 16u);
    k22_data_advance(data, 1);
    expect(state, k22_data_test_load(bus.ram, 0x104, 4) == 0x87868584u,
           "k22_data_test_load(bus.ram, 0x104, 4) == 0x87868584u");
    expect(state, (k22_data_test_read_value(state, data, TCD0 + 0x1c, 2) & 0x80u) != 0,
           "(k22_data_test_read_value(state, data, TCD0 + 0x1c, 2) & 0x80u) != 0");
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x0c, 2) & 1u) == 0,
           "(k22_data_test_read_value(state, data, DMA + 0x0c, 2) & 1u) == 0");
    expect(state, k22_data_test_read_value(state, data, TCD0, 4) == RAM_BASE,
           "k22_data_test_read_value(state, data, TCD0, 4) == RAM_BASE");
    expect(state, k22_data_test_read_value(state, data, TCD0 + 0x10, 4) == RAM_BASE + 0x100,
           "k22_data_test_read_value(state, data, TCD0 + 0x10, 4) == RAM_BASE + 0x100");

    write_tcd(state, data, TCD1, 0xffff0000u, 1, 0, 1, 0, RAM_BASE, 1, 1, 0, 0x02u);
    k22_data_test_write_value(state, data, DMA + 0x19, 1, 1);
    k22_data_test_write_value(state, data, DMA + 0x1b, 1, 1);
    k22_data_test_write_value(state, data, DMA + 0x1d, 1, 1);
    k22_data_advance(data, 1);
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x2c, 2) & 2u) != 0,
           "(k22_data_test_read_value(state, data, DMA + 0x2c, 2) & 2u) != 0");
    expect(state, (k22_data_test_read_value(state, data, DMA + 4, 4) & 0x80000000u) != 0,
           "(k22_data_test_read_value(state, data, DMA + 4, 4) & 0x80000000u) != 0");
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR],
           "bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR]");
    k22_data_test_write_value(state, data, DMA + 0x19, 1, 1);
    k22_data_test_write_value(state, data, DMA + 0x1e, 1, 1);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR],
           "!bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR]");
    k22_data_destroy(data);
}

void k22_data_test_test_dma_advanced(TestState* state) {
    TestBus bus = {0};
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    bus.ram[0x10] = 0x5au;
    bus.ram[0x20] = 0xa5u;
    write_tcd(state, data, TCD0, RAM_BASE + 0x10, 1, 0, 1, 0, RAM_BASE + 0x110, 1, 0x8201u, 0, 0);
    write_tcd(state, data, TCD1, RAM_BASE + 0x20, 1, 0, 1, 0, RAM_BASE + 0x120, 1, 1, 0, 0);
    k22_data_test_write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    expect(state, bus.ram[0x110] == 0x5au, "bus.ram[0x110] == 0x5au");
    expect(state, bus.ram[0x120] == 0xa5u, "bus.ram[0x120] == 0xa5u");

    const uint32_t next = RAM_BASE + 0x300;
    bus.ram[0x30] = 0x11u;
    bus.ram[0x31] = 0x22u;
    store_tcd(&bus, next, RAM_BASE + 0x31, 1, 0, 1, 0, RAM_BASE + 0x131, 1, 1, 0, 0);
    write_tcd(state, data, TCD0, RAM_BASE + 0x30, 1, 0, 1, 0, RAM_BASE + 0x130, 1, 1, (int32_t)next,
              0x10u);
    k22_data_test_write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    expect(state, bus.ram[0x130] == 0x11u, "bus.ram[0x130] == 0x11u");
    expect(state, k22_data_test_read_value(state, data, TCD0, 4) == RAM_BASE + 0x31,
           "k22_data_test_read_value(state, data, TCD0, 4) == RAM_BASE + 0x31");
    k22_data_test_write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    expect(state, bus.ram[0x131] == 0x22u, "bus.ram[0x131] == 0x22u");

    bus.ram[0x603] = 3;
    bus.ram[0x600] = 0;
    bus.ram[0x601] = 1;
    bus.ram[0x602] = 2;
    write_tcd(state, data, TCD0, RAM_BASE + 0x603, 1, 2u << 11, 2, 0, RAM_BASE + 0x700, 1, 2, 0, 0);
    k22_data_test_write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    k22_data_test_write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    expect(state, k22_data_test_load(bus.ram, 0x700, 4) == 0x02010003u,
           "k22_data_test_load(bus.ram, 0x700, 4) == 0x02010003u");

    for (uint8_t index = 0; index < 8; index++)
        bus.ram[0x800 + index] = index;
    k22_data_test_write_value(state, data, DMA, 4, 0x80u);
    write_tcd(state, data, TCD0, RAM_BASE + 0x800, 1, 0, 0x80000000u | (2u << 10) | 2u, 0,
              RAM_BASE + 0x900, 1, 2, 0, 0);
    k22_data_test_write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    k22_data_test_write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    expect(state, bus.ram[0x900] == 0, "bus.ram[0x900] == 0");
    expect(state, bus.ram[0x901] == 1, "bus.ram[0x901] == 1");
    expect(state, bus.ram[0x902] == 4, "bus.ram[0x902] == 4");
    expect(state, bus.ram[0x903] == 5, "bus.ram[0x903] == 5");

    write_tcd(state, data, TCD0, RAM_BASE, 1, 0, 0, 0, RAM_BASE + 0x100u, 1, 1, 0, 0);
    k22_data_test_write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x2cu, 2) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, DMA + 0x2cu, 2) & 1u) != 0u");
    k22_data_test_write_value(state, data, DMA + 0x1eu, 1, 0u);

    write_tcd(state, data, TCD0, RAM_BASE, 1, 0, 1, 0, RAM_BASE + 0x100u, 1, 1,
              (int32_t)0xffff0000u, 0x10u);
    k22_data_test_write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    expect(state, (k22_data_test_read_value(state, data, DMA + 4u, 4) & (1u << 2u)) != 0u,
           "(k22_data_test_read_value(state, data, DMA + 4u, 4) & (1u << 2u)) != 0u");
    k22_data_test_write_value(state, data, DMA + 0x1eu, 1, 0u);

    bus.ram[0x40] = 0x3cu;
    write_tcd(state, data, TCD0, RAM_BASE + 0x40u, 1, 0, 1, 0, RAM_BASE + 0x140u, 1, 1, 0, 0x0120u);
    bus.observe_dma_active = true;
    k22_data_test_write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x140] == 0x3cu, "bus.ram[0x140] == 0x3cu");
    expect(state, (bus.dma_active & 1u) != 0u, "(bus.dma_active & 1u) != 0u");
    bus.observe_dma_active = false;
    k22_data_test_write_value(state, data, TCD0 + 0x1du, 1, 0x12u);
    expect(state, k22_data_test_read_value(state, data, TCD0 + 0x1du, 1) == 0x12u,
           "k22_data_test_read_value(state, data, TCD0 + 0x1du, 1) == 0x12u");

    bus.ram[0x50u] = 0x7eu;
    k22_data_test_write_value(state, data, DMA, 4, 0x80u);
    write_tcd(state, data, TCD0, RAM_BASE + 0x50u, 1, 0, 0xc0000000u | (0xfffffu << 10u) | 1u, 0,
              RAM_BASE + 0x150u, 1, 2, 0, 0);
    k22_data_test_write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    k22_data_test_write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x150u] == 0x7eu, "bus.ram[0x150u] == 0x7eu");
    expect(state, k22_data_test_read_value(state, data, TCD0, 4) == RAM_BASE + 0x50u,
           "k22_data_test_read_value(state, data, TCD0, 4) == RAM_BASE + 0x50u");
    expect(state, k22_data_test_read_value(state, data, TCD0 + 0x10u, 4) == RAM_BASE + 0x150u,
           "k22_data_test_read_value(state, data, TCD0 + 0x10u, 4) == RAM_BASE + 0x150u");
    k22_data_destroy(data);
}

static void prepare_single_byte_dma(TestState* state, K22Data* data, uint32_t tcd, uint32_t source,
                                    uint32_t destination);

void k22_data_test_test_dmamux_triggers(TestState* state) {
    TestBus bus = {0};
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    bus.ram[0x10u] = 0x5au;
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    k22_data_test_write_value(state, data, DMAMUX, 1u, 0xc0u | 16u);
    k22_data_test_write_value(state, data, DMA + 0x1bu, 1u, 0u);
    expect(state, !k22_data_dma_trigger(data, 0u), "!k22_data_dma_trigger(data, 0u)");
    expect(state, k22_data_dma_request(data, 16u), "k22_data_dma_request(data, 16u)");
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u,
           "(k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u");
    expect(state, k22_data_dma_trigger(data, 0u), "k22_data_dma_trigger(data, 0u)");
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u");
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x200u] == 0x5au, "bus.ram[0x200u] == 0x5au");
    expect(state, !k22_data_dma_trigger(data, 4u), "!k22_data_dma_trigger(data, 4u)");

    k22_data_reset(data);
    bus.ram[0x11u] = 0xa5u;
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x11u, RAM_BASE + 0x201u);
    k22_data_test_write_value(state, data, DMAMUX, 1u, 0x80u | 60u);
    k22_data_test_write_value(state, data, DMA + 0x1bu, 1u, 0u);
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u");
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x201u] == 0xa5u, "bus.ram[0x201u] == 0xa5u");
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u");

    k22_data_reset(data);
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x11u, RAM_BASE + 0x202u);
    k22_data_test_write_value(state, data, DMAMUX, 1u, 0xc0u | 60u);
    k22_data_test_write_value(state, data, DMA + 0x1bu, 1u, 0u);
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u,
           "(k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u");
    expect(state, k22_data_dma_trigger(data, 0u), "k22_data_dma_trigger(data, 0u)");
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x202u] == 0xa5u, "bus.ram[0x202u] == 0xa5u");
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u,
           "(k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u");

    k22_data_reset(data);
    prepare_single_byte_dma(state, data, TCD0 + 4u * 32u, RAM_BASE + 0x11u, RAM_BASE + 0x203u);
    k22_data_test_write_value(state, data, DMAMUX + 4u, 1u, 0xc0u | 16u);
    k22_data_test_write_value(state, data, DMA + 0x1bu, 1u, 4u);
    expect(state, k22_data_dma_request(data, 16u), "k22_data_dma_request(data, 16u)");
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & (1u << 4u)) != 0u,
           "(k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & (1u << 4u)) != 0u");
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x203u] == 0xa5u, "bus.ram[0x203u] == 0xa5u");

    k22_data_reset(data);
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x11u, RAM_BASE + 0x204u);
    k22_data_test_write_value(state, data, DMAMUX, 1u, 0x80u | 54u);
    k22_data_test_write_value(state, data, DMA + 0x1bu, 1u, 0u);
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u,
           "(k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u");
    k22_data_destroy(data);

    data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN1M012);
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x11u, RAM_BASE + 0x205u);
    k22_data_test_write_value(state, data, DMAMUX, 1u, 0x80u | 54u);
    k22_data_test_write_value(state, data, DMA + 0x1bu, 1u, 0u);
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u");
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0x205u] == 0xa5u, "bus.ram[0x205u] == 0xa5u");
    k22_data_destroy(data);
}

void k22_data_test_test_dmamux_source_matrix(TestState* state) {
    static const uint64_t expected[K22_PROFILE_COUNT] = {
        UINT64_C(0xfc3f2f00fffdf0fc), UINT64_C(0xfc3f2f00fffdf0fc), UINT64_C(0xfc3f2f00fffdf0fc),
        UINT64_C(0xfc3f6ffffffdf0fc), UINT64_C(0xfffffffffffffffc), UINT64_C(0xfffffffffffffffc),
    };
    TestBus bus = {0};
    for (uint8_t profile = 0u; profile < K22_PROFILE_COUNT; profile++) {
        K22Data* data = k22_data_test_create(state, &bus, (K22ProfileId)profile);
        for (uint8_t source = 0u; source < 64u; source++) {
            k22_data_reset(data);
            k22_data_test_write_value(state, data, DMAMUX, 1u, 0x80u | source);
            k22_data_test_write_value(state, data, DMA + 0x1bu, 1u, 0u);
            const bool valid = (expected[profile] & (UINT64_C(1) << source)) != 0u;
            expect(state, k22_data_dma_request(data, source) == valid,
                   "k22_data_dma_request(data, source) == valid");
            const bool requested =
                (k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u;
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

void k22_data_test_test_dma_arbitration_and_control(TestState* state) {
    TestBus bus = {0};
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    expect(state, k22_data_test_read_value(state, data, DMA + 0x103u, 1u) == 0u,
           "k22_data_test_read_value(state, data, DMA + 0x103u, 1u) == 0u");
    expect(state, k22_data_test_read_value(state, data, DMA + 0x102u, 1u) == 1u,
           "k22_data_test_read_value(state, data, DMA + 0x102u, 1u) == 1u");
    expect(state, k22_data_test_read_value(state, data, DMA + 0x100u, 1u) == 3u,
           "k22_data_test_read_value(state, data, DMA + 0x100u, 1u) == 3u");
    bus.ram[0x10u] = 0xa0u;
    bus.ram[0x20u] = 0xb1u;
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    prepare_single_byte_dma(state, data, TCD1, RAM_BASE + 0x20u, RAM_BASE + 0x201u);
    k22_data_test_write_value(state, data, DMA + 0x1du, 1u, 0u);
    k22_data_test_write_value(state, data, DMA + 0x1du, 1u, 1u);
    k22_data_advance(data, 1u);
    expect(state, bus.dma_write_count == 2u, "bus.dma_write_count == 2u");
    expect(state, bus.dma_write_values[0] == 0xb1u, "bus.dma_write_values[0] == 0xb1u");
    expect(state, bus.dma_write_values[1] == 0xa0u, "bus.dma_write_values[1] == 0xa0u");

    k22_data_reset(data);
    bus.dma_write_count = 0u;
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    prepare_single_byte_dma(state, data, TCD0 + 15u * 32u, RAM_BASE + 0x20u, RAM_BASE + 0x201u);
    k22_data_test_write_value(state, data, DMA, 4u, 4u);
    k22_data_test_write_value(state, data, DMA + 0x1du, 1u, 0u);
    k22_data_test_write_value(state, data, DMA + 0x1du, 1u, 15u);
    k22_data_advance(data, 1u);
    expect(state, bus.dma_write_values[0] == 0xa0u, "bus.dma_write_values[0] == 0xa0u");
    expect(state, bus.dma_write_values[1] == 0xb1u, "bus.dma_write_values[1] == 0xb1u");

    k22_data_reset(data);
    bus.dma_write_count = 0u;
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    k22_data_test_write_value(state, data, DMA, 4u, 0x20u);
    k22_data_test_write_value(state, data, DMA + 0x1du, 1u, 0u);
    k22_data_advance(data, 1u);
    expect(state, bus.dma_write_count == 0u, "bus.dma_write_count == 0u");
    k22_data_test_write_value(state, data, DMA, 4u, 0u);
    k22_data_advance(data, 1u);
    expect(state, bus.dma_write_count == 1u, "bus.dma_write_count == 1u");

    k22_data_reset(data);
    bus.dma_write_count = 0u;
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    k22_data_test_write_value(state, data, DMA, 4u, 2u);
    k22_data_test_write_value(state, data, DMA + 0x1du, 1u, 0u);
    k22_data_set_debug_halted(data, true);
    k22_data_advance(data, 1u);
    expect(state, bus.dma_write_count == 0u, "bus.dma_write_count == 0u");
    k22_data_set_debug_halted(data, false);
    k22_data_advance(data, 1u);
    expect(state, bus.dma_write_count == 1u, "bus.dma_write_count == 1u");

    k22_data_reset(data);
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    k22_data_test_write_value(state, data, DMAMUX, 1u, 0x80u | 16u);
    k22_data_test_write_value(state, data, DMA + 0x1bu, 1u, 0u);
    k22_data_dma_request(data, 16u);
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) != 0u");
    k22_data_advance(data, 1u);
    expect(state, (k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u,
           "(k22_data_test_read_value(state, data, DMA + 0x34u, 2u) & 1u) == 0u");

    k22_data_reset(data);
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    prepare_single_byte_dma(state, data, TCD1, RAM_BASE + 0x20u, RAM_BASE + 0x201u);
    k22_data_test_write_value(state, data, DMA + 0x102u, 1u, 0u);
    k22_data_test_write_value(state, data, DMA + 0x19u, 1u, 0u);
    k22_data_test_write_value(state, data, DMA + 0x1du, 1u, 0u);
    k22_data_test_write_value(state, data, DMA + 0x1du, 1u, 1u);
    k22_data_advance(data, 1u);
    expect(state, (k22_data_test_read_value(state, data, DMA + 4u, 4u) & (1u << 14u)) != 0u,
           "(k22_data_test_read_value(state, data, DMA + 4u, 4u) & (1u << 14u)) != 0u");
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR],
           "bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR]");

    k22_data_reset(data);
    k22_data_test_write_value(state, data, DMA, 4u, 0x10u);
    k22_data_test_write_value(state, data, DMA + 0x19u, 1u, 0u);
    prepare_single_byte_dma(state, data, TCD0, RAM_BASE + 0x10u, RAM_BASE + 0x200u);
    k22_data_test_write_value(state, data, TCD0 + 8u, 4u, 0u);
    k22_data_test_write_value(state, data, DMA + 0x1du, 1u, 0u);
    k22_data_advance(data, 1u);
    expect(state, (k22_data_test_read_value(state, data, DMA, 4u) & 0x20u) != 0u,
           "(k22_data_test_read_value(state, data, DMA, 4u) & 0x20u) != 0u");
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    K22Data* small = k22_data_test_create(state, &bus, K22_PROFILE_MK22F12810);
    uint32_t value = 0u;
    expect(state, !k22_data_read(small, DMA + 0x104u, 1u, &value),
           "!k22_data_read(small, DMA + 0x104u, 1u, &value)");
    expect(state, !k22_data_write(small, DMA + 0x104u, 1u, 4u),
           "!k22_data_write(small, DMA + 0x104u, 1u, 4u)");
    expect(state, !k22_data_read(small, TCD0 + 4u * 32u, 1u, &value),
           "!k22_data_read(small, TCD0 + 4u * 32u, 1u, &value)");
    k22_data_destroy(small);
}

void k22_data_test_test_adc(TestState* state) {
    TestBus bus = {0};
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    expect(state, k22_data_test_read_value(state, data, ADC0, 1) == 0x1fu,
           "k22_data_test_read_value(state, data, ADC0, 1) == 0x1fu");
    k22_data_test_write_value(state, data, ADC0 + 8, 1, 0x0cu);
    expect(state, k22_data_set_adc_input(data, 0, 7, 0x0abcu),
           "k22_data_set_adc_input(data, 0, 7, 0x0abcu)");
    k22_data_test_write_value(state, data, ADC0, 1, 7u | 0x40u);
    k22_data_advance(data, 5);
    expect(state, (k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) == 0,
           "(k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) == 0");
    k22_data_advance(data, 100);
    expect(state, (k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) != 0,
           "(k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) != 0");
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_ADC0], "bus.interrupt[K22_DATA_INTERRUPT_ADC0]");
    expect(state, k22_data_test_read_value(state, data, ADC0 + 0x10, 2) == 0x0abcu,
           "k22_data_test_read_value(state, data, ADC0 + 0x10, 2) == 0x0abcu");
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_ADC0],
           "!bus.interrupt[K22_DATA_INTERRUPT_ADC0]");
    expect(state, k22_data_set_adc_input(data, 1, 3, 0x0555u),
           "k22_data_set_adc_input(data, 1, 3, 0x0555u)");
    k22_data_test_write_value(state, data, ADC1 + 8, 1, 0x0cu);
    k22_data_test_write_value(state, data, ADC1 + 0x20, 1, 0x40u);
    k22_data_test_write_value(state, data, ADC1, 1, 3u);
    k22_data_advance(data, 100);
    expect(state, (k22_data_test_read_value(state, data, ADC1, 1) & 0x80u) == 0,
           "(k22_data_test_read_value(state, data, ADC1, 1) & 0x80u) == 0");
    k22_data_adc_trigger(data, 1);
    k22_data_advance(data, 100);
    expect(state, k22_data_test_read_value(state, data, ADC1 + 0x10, 2) == 0x0555u,
           "k22_data_test_read_value(state, data, ADC1 + 0x10, 2) == 0x0555u");
    k22_data_test_write_value(state, data, ADC0 + 0x24, 1, 0x80u);
    expect(state, (k22_data_test_read_value(state, data, ADC0 + 0x24, 1) & 0xc0u) == 0,
           "(k22_data_test_read_value(state, data, ADC0 + 0x24, 1) & 0xc0u) == 0");
    expect(state, (k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) != 0,
           "(k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) != 0");
    k22_data_destroy(data);
}

void k22_data_test_test_adc_compare_dma_and_continuous(TestState* state) {
    TestBus bus = {0};
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    expect(state, k22_data_set_adc_input(data, 0, 1, 100u),
           "k22_data_set_adc_input(data, 0, 1, 100u)");
    k22_data_test_write_value(state, data, ADC0 + 0x18u, 2, 90u);
    k22_data_test_write_value(state, data, ADC0 + 0x1cu, 2, 110u);
    k22_data_test_write_value(state, data, ADC0 + 0x20u, 1, 0x28u);
    k22_data_test_write_value(state, data, ADC0, 1, 1u);
    k22_data_advance(data, 100u);
    expect(state, (k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) != 0u,
           "(k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) != 0u");
    expect(state, k22_data_test_read_value(state, data, ADC0 + 0x10u, 2) == 100u,
           "k22_data_test_read_value(state, data, ADC0 + 0x10u, 2) == 100u");

    k22_data_test_write_value(state, data, ADC0 + 0x20u, 1, 0x38u);
    k22_data_test_write_value(state, data, ADC0, 1, 1u);
    k22_data_advance(data, 100u);
    expect(state, (k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) == 0u,
           "(k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) == 0u");
    k22_data_test_write_value(state, data, ADC0 + 0x20u, 1, 0x30u);
    k22_data_test_write_value(state, data, ADC0 + 0x18u, 2, 99u);
    k22_data_test_write_value(state, data, ADC0, 1, 1u);
    k22_data_advance(data, 100u);
    expect(state, (k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) != 0u,
           "(k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) != 0u");
    expect(state, k22_data_test_read_value(state, data, ADC0 + 0x10u, 2) == 100u,
           "k22_data_test_read_value(state, data, ADC0 + 0x10u, 2) == 100u");

    k22_data_test_store(bus.ram, 0xb00u, 2u, 100u);
    write_tcd(state, data, TCD0, RAM_BASE + 0xb00u, 0, 0, 2u, 0, RAM_BASE + 0xa00u, 0, 1u, 0, 0u);
    k22_data_test_write_value(state, data, DMAMUX, 1, 0x80u | 40u);
    k22_data_test_write_value(state, data, DMA + 0x1bu, 1, 0u);
    k22_data_test_write_value(state, data, ADC0 + 0x20u, 1, 0x04u);
    k22_data_test_write_value(state, data, ADC0 + 8u, 1, 0x10u);
    k22_data_test_write_value(state, data, ADC0 + 9u, 1, 1u);
    k22_data_test_write_value(state, data, ADC0 + 0x24u, 1, 0x0fu);
    k22_data_test_write_value(state, data, ADC0, 1, 1u);
    k22_data_advance(data, 1000u);
    k22_data_advance(data, 1u);
    expect(state, k22_data_test_load(bus.ram, 0xa00u, 2) == 100u,
           "k22_data_test_load(bus.ram, 0xa00u, 2) == 100u");
    expect(state, (k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) != 0u,
           "(k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) != 0u");
    k22_data_advance(data, 1000u);
    expect(state, (k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) != 0u,
           "(k22_data_test_read_value(state, data, ADC0, 1) & 0x80u) != 0u");

    bus.ram[0xb20u] = 0x4du;
    write_tcd(state, data, TCD1, RAM_BASE + 0xb20u, 0, 0, 1u, 0, RAM_BASE + 0xa20u, 0, 1u, 0, 0u);
    k22_data_test_write_value(state, data, DMAMUX + 1u, 1, 0x80u | 41u);
    k22_data_test_write_value(state, data, DMA + 0x1bu, 1, 1u);
    k22_data_test_write_value(state, data, ADC1 + 0x20u, 1, 0x04u);
    k22_data_test_write_value(state, data, ADC1, 1, 3u);
    k22_data_advance(data, 100u);
    k22_data_advance(data, 1u);
    expect(state, bus.ram[0xa20u] == 0x4du, "bus.ram[0xa20u] == 0x4du");
    k22_data_destroy(data);
}

void k22_data_test_test_dac_cmp_vref(TestState* state) {
    TestBus bus = {0};
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    k22_data_test_write_value(state, data, DAC0, 2, 0x0123u);
    k22_data_test_write_value(state, data, DAC0 + 2, 2, 0x0456u);
    k22_data_test_write_value(state, data, DAC0 + 0x23, 1, 1u);
    k22_data_test_write_value(state, data, DAC0 + 0x21, 1, 0x80u);
    uint16_t output = 0;
    expect(state, k22_data_get_dac_output(data, 0, &output),
           "k22_data_get_dac_output(data, 0, &output)");
    expect(state, output == 0x0123u, "output == 0x0123u");
    k22_data_test_write_value(state, data, DAC0 + 0x22, 1, 0x80u);
    k22_data_dac_trigger(data, 0);
    expect(state, k22_data_get_dac_output(data, 0, &output),
           "k22_data_get_dac_output(data, 0, &output)");
    expect(state, output == 0x0456u, "output == 0x0456u");
    k22_data_dac_trigger(data, 0);
    expect(state, (k22_data_test_read_value(state, data, DAC0 + 0x20, 1) & 1u) != 0,
           "(k22_data_test_read_value(state, data, DAC0 + 0x20, 1) & 1u) != 0");
    k22_data_test_write_value(state, data, DAC0 + 0x20, 1, 7u);
    expect(state, k22_data_test_read_value(state, data, DAC0 + 0x20, 1) == 0,
           "k22_data_test_read_value(state, data, DAC0 + 0x20, 1) == 0");
    k22_data_test_write_value(state, data, DAC0 + 0x23, 1, 2u);
    expect(state, k22_data_test_read_value(state, data, DAC0 + 0x23, 1) == 2u,
           "k22_data_test_read_value(state, data, DAC0 + 0x23, 1) == 2u");
    k22_data_test_write_value(state, data, DAC0 + 0x22, 1, 0x81u);
    k22_data_dac_trigger(data, 0);
    expect(state, k22_data_test_read_value(state, data, DAC0 + 0x23, 1) == 0x22u,
           "k22_data_test_read_value(state, data, DAC0 + 0x23, 1) == 0x22u");
    k22_data_dac_trigger(data, 0);
    expect(state, k22_data_test_read_value(state, data, DAC0 + 0x23, 1) == 0x12u,
           "k22_data_test_read_value(state, data, DAC0 + 0x23, 1) == 0x12u");

    expect(state, k22_data_set_cmp_input(data, 0, 1, 20), "k22_data_set_cmp_input(data, 0, 1, 20)");
    expect(state, k22_data_set_cmp_input(data, 0, 2, 10), "k22_data_set_cmp_input(data, 0, 2, 10)");
    bool comparator_high = false;
    expect(state, k22_data_get_cmp_output(data, 0, &comparator_high),
           "k22_data_get_cmp_output(data, 0, &comparator_high)");
    expect(state, !comparator_high, "!comparator_high");
    k22_data_test_write_value(state, data, CMP0 + 5, 1, (1u << 3) | 2u);
    k22_data_test_write_value(state, data, CMP0 + 3, 1, 0x08u);
    k22_data_test_write_value(state, data, CMP0 + 1, 1, 1u);
    expect(state, k22_data_get_cmp_output(data, 0, &comparator_high),
           "k22_data_get_cmp_output(data, 0, &comparator_high)");
    expect(state, comparator_high, "comparator_high");
    expect(state, (k22_data_test_read_value(state, data, CMP0 + 3, 1) & 5u) == 5u,
           "(k22_data_test_read_value(state, data, CMP0 + 3, 1) & 5u) == 5u");
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_CMP0], "bus.interrupt[K22_DATA_INTERRUPT_CMP0]");
    k22_data_test_write_value(state, data, CMP0 + 3, 1, 0x0cu);
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_CMP0],
           "!bus.interrupt[K22_DATA_INTERRUPT_CMP0]");
    expect(state, k22_data_set_cmp_input(data, 0, 1, 0), "k22_data_set_cmp_input(data, 0, 1, 0)");
    expect(state, (k22_data_test_read_value(state, data, CMP0 + 3, 1) & 2u) != 0,
           "(k22_data_test_read_value(state, data, CMP0 + 3, 1) & 2u) != 0");
    k22_data_test_write_value(state, data, CMP0 + 4u, 1, 0xa0u);
    expect(state, k22_data_set_cmp_input(data, 0, 7, 20), "k22_data_set_cmp_input(data, 0, 7, 20)");
    k22_data_test_write_value(state, data, CMP0 + 3u, 1, 0x48u);
    k22_data_test_write_value(state, data, CMP0 + 5u, 1, (7u << 3u) | 2u);
    expect(state, (k22_data_test_read_value(state, data, CMP0 + 3u, 1) & 1u) != 0u,
           "(k22_data_test_read_value(state, data, CMP0 + 3u, 1) & 1u) != 0u");
    expect(state, !k22_data_get_cmp_output(NULL, 0, &comparator_high),
           "!k22_data_get_cmp_output(NULL, 0, &comparator_high)");
    expect(state, !k22_data_get_cmp_output(data, 3, &comparator_high),
           "!k22_data_get_cmp_output(data, 3, &comparator_high)");
    expect(state, !k22_data_get_cmp_output(data, 0, NULL),
           "!k22_data_get_cmp_output(data, 0, NULL)");

    k22_data_test_write_value(state, data, VREF + 1, 1, 0x80u);
    expect(state, (k22_data_test_read_value(state, data, VREF + 1, 1) & 4u) == 0,
           "(k22_data_test_read_value(state, data, VREF + 1, 1) & 4u) == 0");
    k22_data_advance(data, 99);
    expect(state, (k22_data_test_read_value(state, data, VREF + 1, 1) & 4u) == 0,
           "(k22_data_test_read_value(state, data, VREF + 1, 1) & 4u) == 0");
    k22_data_advance(data, 1);
    expect(state, (k22_data_test_read_value(state, data, VREF + 1, 1) & 4u) != 0,
           "(k22_data_test_read_value(state, data, VREF + 1, 1) & 4u) != 0");
    k22_data_destroy(data);
}

void k22_data_test_test_rng_crc(TestState* state) {
    TestBus bus = {0};
    K22Data* data = k22_data_test_create(state, &bus, K22_PROFILE_MK22FN51212);
    expect(state, k22_data_test_read_value(state, data, CRC, 4) == UINT32_MAX,
           "k22_data_test_read_value(state, data, CRC, 4) == UINT32_MAX");
    k22_data_rng_seed(data, 1);
    k22_data_test_write_value(state, data, RNG, 4, 3u);
    k22_data_advance(data, 63);
    expect(state, (k22_data_test_read_value(state, data, RNG + 4, 4) & 1u) == 0,
           "(k22_data_test_read_value(state, data, RNG + 4, 4) & 1u) == 0");
    k22_data_advance(data, 1);
    expect(state, bus.interrupt[K22_DATA_INTERRUPT_RNG], "bus.interrupt[K22_DATA_INTERRUPT_RNG]");
    expect(state, k22_data_test_read_value(state, data, RNG + 0x0c, 4) == 0x00042021u,
           "k22_data_test_read_value(state, data, RNG + 0x0c, 4) == 0x00042021u");
    expect(state, !bus.interrupt[K22_DATA_INTERRUPT_RNG], "!bus.interrupt[K22_DATA_INTERRUPT_RNG]");
    k22_data_test_write_value(state, data, RNG, 4, 0x10u);
    expect(state, k22_data_test_read_value(state, data, RNG + 4, 4) == 0,
           "k22_data_test_read_value(state, data, RNG + 4, 4) == 0");

    static const uint8_t message[] = "123456789";
    for (size_t index = 0; index < sizeof(message) - 1; index++)
        k22_data_test_write_value(state, data, CRC, 1, message[index]);
    expect(state, k22_data_test_read_value(state, data, CRC, 4) == 0x29b1u,
           "k22_data_test_read_value(state, data, CRC, 4) == 0x29b1u");
    k22_data_test_write_value(state, data, CRC + 8, 4, 0x02000000u);
    k22_data_test_write_value(state, data, CRC, 4, 0x1234u);
    expect(state, k22_data_test_read_value(state, data, CRC, 4) == 0x1234u,
           "k22_data_test_read_value(state, data, CRC, 4) == 0x1234u");
    k22_data_test_write_value(state, data, CRC + 8, 4, 0x12000000u);
    k22_data_test_write_value(state, data, CRC, 4, 0x01234567u);
    expect(state, k22_data_test_read_value(state, data, CRC, 4) == 0x0123a2e6u,
           "k22_data_test_read_value(state, data, CRC, 4) == 0x0123a2e6u");
    k22_data_test_write_value(state, data, CRC + 8, 4, 0x22000000u);
    k22_data_test_write_value(state, data, CRC, 4, 0x01234567u);
    expect(state, k22_data_test_read_value(state, data, CRC, 4) == 0x0123e6a2u,
           "k22_data_test_read_value(state, data, CRC, 4) == 0x0123e6a2u");
    k22_data_test_write_value(state, data, CRC + 8, 4, 0x32000000u);
    k22_data_test_write_value(state, data, CRC, 4, 0x01234567u);
    expect(state, k22_data_test_read_value(state, data, CRC, 4) == 0x01236745u,
           "k22_data_test_read_value(state, data, CRC, 4) == 0x01236745u");
    k22_data_test_write_value(state, data, CRC + 8, 4, 0x16000000u);
    k22_data_test_write_value(state, data, CRC, 4, 0x01234567u);
    expect(state, k22_data_test_read_value(state, data, CRC, 4) == 0x01235d19u,
           "k22_data_test_read_value(state, data, CRC, 4) == 0x01235d19u");
    k22_data_test_write_value(state, data, CRC + 8, 4, 0x40000000u);
    k22_data_test_write_value(state, data, CRC, 2, 0x1234u);
    expect(state, k22_data_test_read_value(state, data, CRC, 4) != UINT32_MAX,
           "k22_data_test_read_value(state, data, CRC, 4) != UINT32_MAX");
    expect(state, k22_data_test_read_value(state, data, RNG + 8u, 4) == 0u,
           "k22_data_test_read_value(state, data, RNG + 8u, 4) == 0u");
    k22_data_destroy(data);
}

void k22_data_test_set_flash_address(TestState* state, K22Data* data, uint32_t address);
