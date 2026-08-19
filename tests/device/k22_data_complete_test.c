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
        return true;
    }
    return false;
}

static void bus_interrupt(void* context, K22DataInterrupt interrupt, bool asserted) {
    TestBus* bus = context;
    bus->interrupt[interrupt] = asserted;
}

static K22Data* create(TestState* state, TestBus* bus, K22ProfileId profile) {
    K22DataBus callbacks = {bus, bus_read, bus_write, bus_write, bus_interrupt};
    K22Data* data = k22_data_create(k22_profile_get(profile), callbacks);
    TEST_EXPECT(state, data != NULL);
    bus->data = data;
    return data;
}

static K22Data* create_without_program(TestState* state, TestBus* bus,
                                       K22ProfileId profile) {
    K22DataBus callbacks = {bus, bus_read, bus_write, NULL, bus_interrupt};
    K22Data* data = k22_data_create(k22_profile_get(profile), callbacks);
    TEST_EXPECT(state, data != NULL);
    bus->data = data;
    return data;
}

static uint32_t read_value(TestState* state, K22Data* data, uint32_t address,
                           uint8_t size) {
    uint32_t value = 0;
    TEST_EXPECT(state, k22_data_read(data, address, size, &value));
    return value;
}

static void write_value(TestState* state, K22Data* data, uint32_t address, uint8_t size,
                        uint32_t value) {
    TEST_EXPECT(state, k22_data_write(data, address, size, value));
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
    TEST_EXPECT(state, !k22_data_read(small, RNG, 4, &value));
    TEST_EXPECT(state, !k22_data_read(small, DAC1, 1, &value));
    TEST_EXPECT(state, !k22_data_read(small, DMAMUX + 4, 1, &value));
    TEST_EXPECT(state, k22_data_read(small, DMAMUX + 3, 1, &value));
    TEST_EXPECT(state, !k22_data_set_adc_input(small, 2, 0, 0));
    TEST_EXPECT(state, !k22_data_set_cmp_input(small, 2, 0, 0));
    k22_data_destroy(small);
    K22Data* large = create(state, &bus, K22_PROFILE_MK22FN51212);
    TEST_EXPECT(state, k22_data_read(large, RNG, 4, &value));
    TEST_EXPECT(state, k22_data_read(large, DAC1, 1, &value));
    k22_data_destroy(large);
}

static void test_dma(TestState* state) {
    TestBus bus = {0};
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    for (uint8_t index = 0; index < 8; index++)
        bus.ram[index] = (uint8_t)(0x80u + index);
    write_tcd(state, data, TCD0, RAM_BASE, 2, 0x0101u, 4, -8, RAM_BASE + 0x100, 2, 2, -8,
              0x0eu);
    write_value(state, data, DMAMUX, 1, 0x80u | 17u);
    write_value(state, data, DMA + 0x1b, 1, 0);
    k22_data_dma_request(data, 17);
    k22_data_advance(data, 1);
    TEST_EXPECT(state, load(bus.ram, 0x100, 4) == 0x83828180u);
    TEST_EXPECT(state, bus.interrupt[K22_DATA_INTERRUPT_DMA0]);
    write_value(state, data, DMA + 0x1f, 1, 0);
    TEST_EXPECT(state, !bus.interrupt[K22_DATA_INTERRUPT_DMA0]);
    k22_data_dma_request(data, 17);
    k22_data_advance(data, 1);
    TEST_EXPECT(state, load(bus.ram, 0x104, 4) == 0x87868584u);
    TEST_EXPECT(state, (read_value(state, data, TCD0 + 0x1c, 2) & 0x80u) != 0);
    TEST_EXPECT(state, (read_value(state, data, DMA + 0x0c, 2) & 1u) == 0);
    TEST_EXPECT(state, read_value(state, data, TCD0, 4) == RAM_BASE);
    TEST_EXPECT(state, read_value(state, data, TCD0 + 0x10, 4) == RAM_BASE + 0x100);

    write_tcd(state, data, TCD1, 0xffff0000u, 1, 0, 1, 0, RAM_BASE, 1, 1, 0, 0x02u);
    write_value(state, data, DMA + 0x19, 1, 1);
    write_value(state, data, DMA + 0x1b, 1, 1);
    write_value(state, data, DMA + 0x1d, 1, 1);
    k22_data_advance(data, 1);
    TEST_EXPECT(state, (read_value(state, data, DMA + 0x2c, 2) & 2u) != 0);
    TEST_EXPECT(state, (read_value(state, data, DMA + 4, 4) & 0x80000000u) != 0);
    TEST_EXPECT(state, bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR]);
    write_value(state, data, DMA + 0x19, 1, 1);
    write_value(state, data, DMA + 0x1e, 1, 1);
    TEST_EXPECT(state, !bus.interrupt[K22_DATA_INTERRUPT_DMA_ERROR]);
    k22_data_destroy(data);
}

static void test_dma_advanced(TestState* state) {
    TestBus bus = {0};
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    bus.ram[0x10] = 0x5au;
    bus.ram[0x20] = 0xa5u;
    write_tcd(state, data, TCD0, RAM_BASE + 0x10, 1, 0, 1, 0, RAM_BASE + 0x110, 1, 0x8201u,
              0, 0);
    write_tcd(state, data, TCD1, RAM_BASE + 0x20, 1, 0, 1, 0, RAM_BASE + 0x120, 1, 1, 0, 0);
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    TEST_EXPECT(state, bus.ram[0x110] == 0x5au);
    TEST_EXPECT(state, bus.ram[0x120] == 0xa5u);

    const uint32_t next = RAM_BASE + 0x300;
    bus.ram[0x30] = 0x11u;
    bus.ram[0x31] = 0x22u;
    store_tcd(&bus, next, RAM_BASE + 0x31, 1, 0, 1, 0, RAM_BASE + 0x131, 1, 1, 0, 0);
    write_tcd(state, data, TCD0, RAM_BASE + 0x30, 1, 0, 1, 0, RAM_BASE + 0x130, 1, 1,
              (int32_t)next, 0x10u);
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    TEST_EXPECT(state, bus.ram[0x130] == 0x11u);
    TEST_EXPECT(state, read_value(state, data, TCD0, 4) == RAM_BASE + 0x31);
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    TEST_EXPECT(state, bus.ram[0x131] == 0x22u);

    bus.ram[0x603] = 3;
    bus.ram[0x600] = 0;
    bus.ram[0x601] = 1;
    bus.ram[0x602] = 2;
    write_tcd(state, data, TCD0, RAM_BASE + 0x603, 1, 2u << 11, 2, 0, RAM_BASE + 0x700, 1,
              2, 0, 0);
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    TEST_EXPECT(state, load(bus.ram, 0x700, 4) == 0x02010003u);

    for (uint8_t index = 0; index < 8; index++)
        bus.ram[0x800 + index] = index;
    write_value(state, data, DMA, 4, 0x80u);
    write_tcd(state, data, TCD0, RAM_BASE + 0x800, 1, 0, 0x80000000u | (2u << 10) | 2u, 0,
              RAM_BASE + 0x900, 1, 2, 0, 0);
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    write_value(state, data, DMA + 0x1d, 1, 0);
    k22_data_advance(data, 1);
    TEST_EXPECT(state, bus.ram[0x900] == 0);
    TEST_EXPECT(state, bus.ram[0x901] == 1);
    TEST_EXPECT(state, bus.ram[0x902] == 4);
    TEST_EXPECT(state, bus.ram[0x903] == 5);

    write_tcd(state, data, TCD0, RAM_BASE, 1, 0, 0, 0, RAM_BASE + 0x100u, 1, 1, 0, 0);
    write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    TEST_EXPECT(state, (read_value(state, data, DMA + 0x2cu, 2) & 1u) != 0u);
    write_value(state, data, DMA + 0x1eu, 1, 0u);

    write_tcd(state, data, TCD0, RAM_BASE, 1, 0, 1, 0, RAM_BASE + 0x100u, 1, 1,
              (int32_t)0xffff0000u, 0x10u);
    write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    TEST_EXPECT(state, (read_value(state, data, DMA + 4u, 4) & (1u << 2u)) != 0u);
    write_value(state, data, DMA + 0x1eu, 1, 0u);

    bus.ram[0x40] = 0x3cu;
    write_tcd(state, data, TCD0, RAM_BASE + 0x40u, 1, 0, 1, 0, RAM_BASE + 0x140u, 1, 1, 0,
              0x0120u);
    bus.observe_dma_active = true;
    write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    k22_data_advance(data, 1u);
    TEST_EXPECT(state, bus.ram[0x140] == 0x3cu);
    TEST_EXPECT(state, (bus.dma_active & 1u) != 0u);
    bus.observe_dma_active = false;
    write_value(state, data, TCD0 + 0x1du, 1, 0x12u);
    TEST_EXPECT(state, read_value(state, data, TCD0 + 0x1du, 1) == 0x12u);

    bus.ram[0x50u] = 0x7eu;
    write_value(state, data, DMA, 4, 0x80u);
    write_tcd(state, data, TCD0, RAM_BASE + 0x50u, 1, 0,
              0xc0000000u | (0xfffffu << 10u) | 1u, 0, RAM_BASE + 0x150u, 1, 2, 0, 0);
    write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    write_value(state, data, DMA + 0x1du, 1, 0u);
    k22_data_advance(data, 1u);
    TEST_EXPECT(state, bus.ram[0x150u] == 0x7eu);
    TEST_EXPECT(state, read_value(state, data, TCD0, 4) == RAM_BASE + 0x50u);
    TEST_EXPECT(state, read_value(state, data, TCD0 + 0x10u, 4) == RAM_BASE + 0x150u);
    k22_data_destroy(data);
}

static void test_adc(TestState* state) {
    TestBus bus = {0};
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    TEST_EXPECT(state, read_value(state, data, ADC0, 1) == 0x1fu);
    write_value(state, data, ADC0 + 8, 1, 0x0cu);
    TEST_EXPECT(state, k22_data_set_adc_input(data, 0, 7, 0x0abcu));
    write_value(state, data, ADC0, 1, 7u | 0x40u);
    k22_data_advance(data, 5);
    TEST_EXPECT(state, (read_value(state, data, ADC0, 1) & 0x80u) == 0);
    k22_data_advance(data, 100);
    TEST_EXPECT(state, (read_value(state, data, ADC0, 1) & 0x80u) != 0);
    TEST_EXPECT(state, bus.interrupt[K22_DATA_INTERRUPT_ADC0]);
    TEST_EXPECT(state, read_value(state, data, ADC0 + 0x10, 2) == 0x0abcu);
    TEST_EXPECT(state, !bus.interrupt[K22_DATA_INTERRUPT_ADC0]);
    TEST_EXPECT(state, k22_data_set_adc_input(data, 1, 3, 0x0555u));
    write_value(state, data, ADC1 + 8, 1, 0x0cu);
    write_value(state, data, ADC1 + 0x20, 1, 0x40u);
    write_value(state, data, ADC1, 1, 3u);
    k22_data_advance(data, 100);
    TEST_EXPECT(state, (read_value(state, data, ADC1, 1) & 0x80u) == 0);
    k22_data_adc_trigger(data, 1);
    k22_data_advance(data, 100);
    TEST_EXPECT(state, read_value(state, data, ADC1 + 0x10, 2) == 0x0555u);
    write_value(state, data, ADC0 + 0x24, 1, 0x80u);
    TEST_EXPECT(state, (read_value(state, data, ADC0 + 0x24, 1) & 0xc0u) == 0);
    TEST_EXPECT(state, (read_value(state, data, ADC0, 1) & 0x80u) != 0);
    k22_data_destroy(data);
}

static void test_adc_compare_dma_and_continuous(TestState* state) {
    TestBus bus = {0};
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    TEST_EXPECT(state, k22_data_set_adc_input(data, 0, 1, 100u));
    write_value(state, data, ADC0 + 0x18u, 2, 90u);
    write_value(state, data, ADC0 + 0x1cu, 2, 110u);
    write_value(state, data, ADC0 + 0x20u, 1, 0x28u);
    write_value(state, data, ADC0, 1, 1u);
    k22_data_advance(data, 100u);
    TEST_EXPECT(state, (read_value(state, data, ADC0, 1) & 0x80u) != 0u);
    TEST_EXPECT(state, read_value(state, data, ADC0 + 0x10u, 2) == 100u);

    write_value(state, data, ADC0 + 0x20u, 1, 0x38u);
    write_value(state, data, ADC0, 1, 1u);
    k22_data_advance(data, 100u);
    TEST_EXPECT(state, (read_value(state, data, ADC0, 1) & 0x80u) == 0u);
    write_value(state, data, ADC0 + 0x20u, 1, 0x30u);
    write_value(state, data, ADC0 + 0x18u, 2, 99u);
    write_value(state, data, ADC0, 1, 1u);
    k22_data_advance(data, 100u);
    TEST_EXPECT(state, (read_value(state, data, ADC0, 1) & 0x80u) != 0u);
    TEST_EXPECT(state, read_value(state, data, ADC0 + 0x10u, 2) == 100u);

    store(bus.ram, 0xb00u, 2u, 100u);
    write_tcd(state, data, TCD0, RAM_BASE + 0xb00u, 0, 0, 2u, 0, RAM_BASE + 0xa00u, 0, 1u,
              0, 0u);
    write_value(state, data, DMAMUX, 1, 0x80u | 40u);
    write_value(state, data, DMA + 0x1bu, 1, 0u);
    write_value(state, data, ADC0 + 0x20u, 1, 0x04u);
    write_value(state, data, ADC0 + 8u, 1, 0x10u);
    write_value(state, data, ADC0 + 9u, 1, 1u);
    write_value(state, data, ADC0 + 0x24u, 1, 0x0fu);
    write_value(state, data, ADC0, 1, 1u);
    k22_data_advance(data, 1000u);
    k22_data_advance(data, 1u);
    TEST_EXPECT(state, load(bus.ram, 0xa00u, 2) == 100u);
    TEST_EXPECT(state, (read_value(state, data, ADC0, 1) & 0x80u) != 0u);
    k22_data_advance(data, 1000u);
    TEST_EXPECT(state, (read_value(state, data, ADC0, 1) & 0x80u) != 0u);

    bus.ram[0xb20u] = 0x4du;
    write_tcd(state, data, TCD1, RAM_BASE + 0xb20u, 0, 0, 1u, 0, RAM_BASE + 0xa20u, 0, 1u,
              0, 0u);
    write_value(state, data, DMAMUX + 1u, 1, 0x80u | 41u);
    write_value(state, data, DMA + 0x1bu, 1, 1u);
    write_value(state, data, ADC1 + 0x20u, 1, 0x04u);
    write_value(state, data, ADC1, 1, 3u);
    k22_data_advance(data, 100u);
    k22_data_advance(data, 1u);
    TEST_EXPECT(state, bus.ram[0xa20u] == 0x4du);
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
    TEST_EXPECT(state, k22_data_get_dac_output(data, 0, &output));
    TEST_EXPECT(state, output == 0x0123u);
    write_value(state, data, DAC0 + 0x22, 1, 0x80u);
    k22_data_dac_trigger(data, 0);
    TEST_EXPECT(state, k22_data_get_dac_output(data, 0, &output));
    TEST_EXPECT(state, output == 0x0456u);
    k22_data_dac_trigger(data, 0);
    TEST_EXPECT(state, (read_value(state, data, DAC0 + 0x20, 1) & 1u) != 0);
    write_value(state, data, DAC0 + 0x20, 1, 7u);
    TEST_EXPECT(state, read_value(state, data, DAC0 + 0x20, 1) == 0);
    write_value(state, data, DAC0 + 0x23, 1, 2u);
    TEST_EXPECT(state, read_value(state, data, DAC0 + 0x23, 1) == 2u);
    write_value(state, data, DAC0 + 0x22, 1, 0x81u);
    k22_data_dac_trigger(data, 0);
    TEST_EXPECT(state, read_value(state, data, DAC0 + 0x23, 1) == 0x22u);
    k22_data_dac_trigger(data, 0);
    TEST_EXPECT(state, read_value(state, data, DAC0 + 0x23, 1) == 0x12u);

    TEST_EXPECT(state, k22_data_set_cmp_input(data, 0, 1, 20));
    TEST_EXPECT(state, k22_data_set_cmp_input(data, 0, 2, 10));
    write_value(state, data, CMP0 + 5, 1, (1u << 3) | 2u);
    write_value(state, data, CMP0 + 3, 1, 0x08u);
    write_value(state, data, CMP0 + 1, 1, 1u);
    TEST_EXPECT(state, (read_value(state, data, CMP0 + 3, 1) & 5u) == 5u);
    TEST_EXPECT(state, bus.interrupt[K22_DATA_INTERRUPT_CMP0]);
    write_value(state, data, CMP0 + 3, 1, 0x0cu);
    TEST_EXPECT(state, !bus.interrupt[K22_DATA_INTERRUPT_CMP0]);
    TEST_EXPECT(state, k22_data_set_cmp_input(data, 0, 1, 0));
    TEST_EXPECT(state, (read_value(state, data, CMP0 + 3, 1) & 2u) != 0);
    write_value(state, data, CMP0 + 4u, 1, 0xa0u);
    TEST_EXPECT(state, k22_data_set_cmp_input(data, 0, 7, 20));
    write_value(state, data, CMP0 + 3u, 1, 0x48u);
    write_value(state, data, CMP0 + 5u, 1, (7u << 3u) | 2u);
    TEST_EXPECT(state, (read_value(state, data, CMP0 + 3u, 1) & 1u) != 0u);

    write_value(state, data, VREF + 1, 1, 0x80u);
    TEST_EXPECT(state, (read_value(state, data, VREF + 1, 1) & 4u) == 0);
    k22_data_advance(data, 99);
    TEST_EXPECT(state, (read_value(state, data, VREF + 1, 1) & 4u) == 0);
    k22_data_advance(data, 1);
    TEST_EXPECT(state, (read_value(state, data, VREF + 1, 1) & 4u) != 0);
    k22_data_destroy(data);
}

static void test_rng_crc(TestState* state) {
    TestBus bus = {0};
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN51212);
    TEST_EXPECT(state, read_value(state, data, CRC, 4) == UINT32_MAX);
    k22_data_rng_seed(data, 1);
    write_value(state, data, RNG, 4, 3u);
    k22_data_advance(data, 63);
    TEST_EXPECT(state, (read_value(state, data, RNG + 4, 4) & 1u) == 0);
    k22_data_advance(data, 1);
    TEST_EXPECT(state, bus.interrupt[K22_DATA_INTERRUPT_RNG]);
    TEST_EXPECT(state, read_value(state, data, RNG + 0x0c, 4) == 0x00042021u);
    TEST_EXPECT(state, !bus.interrupt[K22_DATA_INTERRUPT_RNG]);
    write_value(state, data, RNG, 4, 0x10u);
    TEST_EXPECT(state, read_value(state, data, RNG + 4, 4) == 0);

    static const uint8_t message[] = "123456789";
    for (size_t index = 0; index < sizeof(message) - 1; index++)
        write_value(state, data, CRC, 1, message[index]);
    TEST_EXPECT(state, read_value(state, data, CRC, 4) == 0x29b1u);
    write_value(state, data, CRC + 8, 4, 0x02000000u);
    write_value(state, data, CRC, 4, 0x1234u);
    TEST_EXPECT(state, read_value(state, data, CRC, 4) == 0x1234u);
    write_value(state, data, CRC + 8, 4, 0x12000000u);
    write_value(state, data, CRC, 4, 0x01234567u);
    TEST_EXPECT(state, read_value(state, data, CRC, 4) == 0x0123a2e6u);
    write_value(state, data, CRC + 8, 4, 0x22000000u);
    write_value(state, data, CRC, 4, 0x01234567u);
    TEST_EXPECT(state, read_value(state, data, CRC, 4) == 0x0123e6a2u);
    write_value(state, data, CRC + 8, 4, 0x32000000u);
    write_value(state, data, CRC, 4, 0x01234567u);
    TEST_EXPECT(state, read_value(state, data, CRC, 4) == 0x01236745u);
    write_value(state, data, CRC + 8, 4, 0x16000000u);
    write_value(state, data, CRC, 4, 0x01234567u);
    TEST_EXPECT(state, read_value(state, data, CRC, 4) == 0x01235d19u);
    write_value(state, data, CRC + 8, 4, 0x40000000u);
    write_value(state, data, CRC, 2, 0x1234u);
    TEST_EXPECT(state, read_value(state, data, CRC, 4) != UINT32_MAX);
    TEST_EXPECT(state, read_value(state, data, RNG + 8u, 4) == 0u);
    k22_data_destroy(data);
}

static void test_flash_flex_copy(TestState* state) {
    TestBus bus = {0};
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FX51212);
    TEST_EXPECT(state, read_value(state, data, FTFA, 1) == 0x80u);
    write_value(state, data, FTFA + 4, 1, 0x07u);
    write_value(state, data, FTFA + 5, 1, 0x00u);
    write_value(state, data, FTFA + 6, 1, 0x10u);
    write_value(state, data, FTFA + 7, 1, 0x00u);
    write_value(state, data, FTFA + 8, 4, 0x78563412u);
    write_value(state, data, FTFA + 12, 4, 0xf0debc9au);
    write_value(state, data, FTFA, 1, 0x80u);
    TEST_EXPECT(state, read_value(state, data, FTFA, 1) == 0);
    TEST_EXPECT(state, load(bus.flash, 0x1000, 4) == 0x12345678u);
    TEST_EXPECT(state, load(bus.flash, 0x1004, 4) == 0x9abcdef0u);
    k22_data_advance(data, 40);
    TEST_EXPECT(state, read_value(state, data, FTFA, 1) == 0x80u);
    write_value(state, data, FTFA + 4, 1, 0xffu);
    write_value(state, data, FTFA, 1, 0x80u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 0x20u) != 0);
    write_value(state, data, FTFA, 1, 0x20u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 0x20u) == 0);

    uint8_t configuration[16];
    memset(configuration, 0xff, sizeof(configuration));
    configuration[8] = 0xfeu;
    configuration[0x0c] = 0xfeu;
    TEST_EXPECT(state, k22_data_set_flash_configuration(data, configuration,
                                                        sizeof(configuration)));
    TEST_EXPECT(state, read_value(state, data, 0x408u, 1) == 0xfeu);
    k22_data_reset(data);
    TEST_EXPECT(state, read_value(state, data, FTFA + 0x10, 1) == 0xfeu);
    write_value(state, data, FTFA + 4, 1, 0x07u);
    write_value(state, data, FTFA + 5, 1, 0x00u);
    write_value(state, data, FTFA + 6, 1, 0x10u);
    write_value(state, data, FTFA + 7, 1, 0x00u);
    write_value(state, data, FTFA + 8, 4, 0xffffffffu);
    write_value(state, data, FTFA + 12, 4, 0xffffffffu);
    write_value(state, data, FTFA, 1, 0x80u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 0x10u) != 0);

    TEST_EXPECT(state, read_value(state, data, 0x10000000u, 4) == UINT32_MAX);
    write_value(state, data, 0x10000000u, 4, 0x55aa55aau);
    write_value(state, data, 0x10000000u, 4, 0xffff0000u);
    TEST_EXPECT(state, read_value(state, data, 0x10000000u, 4) == 0x55aa0000u);
    write_value(state, data, 0x14000000u, 4, 0xdeadbeefu);
    TEST_EXPECT(state, read_value(state, data, 0x14000000u, 4) == 0xdeadbeefu);

    K22Data* copy = create(state, &bus, K22_PROFILE_MK22FX51212);
    TEST_EXPECT(state, k22_data_copy(copy, data));
    TEST_EXPECT(state, read_value(state, copy, 0x10000000u, 4) == 0x55aa0000u);
    TEST_EXPECT(state, read_value(state, copy, 0x14000000u, 4) == 0xdeadbeefu);
    k22_data_reset(copy);
    TEST_EXPECT(state, read_value(state, copy, 0x10000000u, 4) == 0x55aa0000u);
    TEST_EXPECT(state, read_value(state, copy, 0x14000000u, 4) == 0);
    k22_data_destroy(copy);
    k22_data_destroy(data);
}

static void set_flash_address(TestState* state, K22Data* data, uint32_t address) {
    write_value(state, data, FTFA + 5u, 1, address >> 16u);
    write_value(state, data, FTFA + 6u, 1, address >> 8u);
    write_value(state, data, FTFA + 7u, 1, address);
}

static void launch_flash(TestState* state, K22Data* data, uint32_t cycles) {
    write_value(state, data, FTFA, 1, 0x80u);
    k22_data_advance(data, cycles);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 0x80u) != 0u);
}

static void test_flash_controller_geometry(TestState* state) {
    TestBus bus;
    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    K22Data* data = create(state, &bus, K22_PROFILE_MK22FN1M012);
    bus.flash[0x1000] = 0;
    bus.flash[0x1ffc] = 0;
    bus.flash[0x2000] = 0;
    write_value(state, data, FTFA + 4u, 1, 0x09u);
    set_flash_address(state, data, 0x1000u);
    launch_flash(state, data, 2000u);
    TEST_EXPECT(state, load(bus.flash, 0x1000u, 4) == UINT32_MAX);
    TEST_EXPECT(state, load(bus.flash, 0x1ffcu, 4) == UINT32_MAX);
    TEST_EXPECT(state, bus.flash[0x2000] == 0);

    write_value(state, data, FTFA + 4u, 1, 0x43u);
    write_value(state, data, FTFA + 5u, 1, 2u);
    write_value(state, data, FTFA + 8u, 4, 0x78563412u);
    write_value(state, data, FTFA + 12u, 4, 0xf0debc9au);
    launch_flash(state, data, 40u);
    write_value(state, data, FTFA + 4u, 1, 0x41u);
    write_value(state, data, FTFA + 5u, 1, 2u);
    launch_flash(state, data, 40u);
    TEST_EXPECT(state, read_value(state, data, FTFA + 8u, 4) == 0x78563412u);
    TEST_EXPECT(state, read_value(state, data, FTFA + 12u, 4) == 0xf0debc9au);
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create(state, &bus, K22_PROFILE_MK22FN51212);
    bus.flash[0x1000] = 0;
    bus.flash[0x17fc] = 0;
    bus.flash[0x1800] = 0;
    write_value(state, data, FTFA + 4u, 1, 0x09u);
    set_flash_address(state, data, 0x1000u);
    launch_flash(state, data, 2000u);
    TEST_EXPECT(state, load(bus.flash, 0x1000u, 4) == UINT32_MAX);
    TEST_EXPECT(state, load(bus.flash, 0x17fcu, 4) == UINT32_MAX);
    TEST_EXPECT(state, bus.flash[0x1800] == 0);
    write_value(state, data, FTFA + 4u, 1, 0x07u);
    set_flash_address(state, data, 0x2000u);
    write_value(state, data, FTFA, 1, 0x80u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 0x20u) != 0u);
    k22_data_destroy(data);
}

static void flash_command(TestState* state, K22Data* data, uint8_t command,
                          uint32_t address, uint32_t cycles) {
    write_value(state, data, FTFA + 4u, 1, command);
    set_flash_address(state, data, address);
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
    TEST_EXPECT(state, bus.flash[0x100u] == 0xffu);
    TEST_EXPECT(state, bus.flash[0x7fffcu] == 0xffu);
    TEST_EXPECT(state, bus.flash[0x80000u] == 0u);

    bus.flash[0u] = 0u;
    bus.flash[0xfffffu] = 0u;
    flash_command(state, data, 0x44u, 0u, 2000u);
    TEST_EXPECT(state, bus.flash[0u] == 0xffu);
    TEST_EXPECT(state, bus.flash[0xfffffu] == 0xffu);

    flash_command(state, data, 0x01u, 0x1000u, 40u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 1u) == 0u);
    bus.flash[0x1000u] = 0u;
    flash_command(state, data, 0x01u, 0x1000u, 40u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 1u) != 0u);
    write_value(state, data, FTFA, 1, 1u);
    memset(bus.flash, 0xff, sizeof(bus.flash));
    flash_command(state, data, 0x40u, 0u, 40u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 1u) == 0u);

    store(bus.flash, 0x2000u, 4, 0x12345678u);
    write_value(state, data, FTFA + 8u, 4, 0x78563412u);
    flash_command(state, data, 0x02u, 0x2000u, 40u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 1u) == 0u);
    write_value(state, data, FTFA + 8u, 4, 0x21436587u);
    flash_command(state, data, 0x02u, 0x2000u, 40u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 1u) != 0u);
    write_value(state, data, FTFA, 1, 1u);

    bus.fail_read = true;
    write_value(state, data, FTFA + 8u, 4, 0x78563412u);
    write_value(state, data, FTFA + 12u, 4, 0xf0debc9au);
    flash_command(state, data, 0x07u, 0x3000u, 40u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 0x20u) != 0u);
    bus.fail_read = false;
    write_value(state, data, FTFA, 1, 0x20u);
    bus.fail_write = true;
    flash_command(state, data, 0x09u, 0x4000u, 2000u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 0x20u) != 0u);
    bus.fail_write = false;
    write_value(state, data, FTFA, 1, 0x20u);

    flash_command(state, data, 0x03u, 0u, 40u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 0x30u) == 0u);
    flash_command(state, data, 0x45u, 0u, 40u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 0x30u) == 0u);
    write_value(state, data, FTFA + 4u, 1, 0x43u);
    write_value(state, data, FTFA + 5u, 1, 1u);
    write_value(state, data, FTFA + 8u, 4, 0x78563412u);
    write_value(state, data, FTFA + 12u, 4, 0xf0debc9au);
    launch_flash(state, data, 40u);
    launch_flash(state, data, 40u);
    TEST_EXPECT(state, (read_value(state, data, FTFA, 1) & 0x20u) != 0u);
    write_value(state, data, FTFA, 1, 0x20u);
    write_value(state, data, FTFA + 1u, 1, 0x80u);
    flash_command(state, data, 0x03u, 0u, 40u);
    TEST_EXPECT(state, bus.interrupt[K22_DATA_INTERRUPT_FTFA]);
    k22_data_destroy(data);

    memset(&bus, 0, sizeof(bus));
    memset(bus.flash, 0xff, sizeof(bus.flash));
    data = create_without_program(state, &bus, K22_PROFILE_MK22FN51212);
    write_value(state, data, FTFA + 4u, 1, 0x06u);
    set_flash_address(state, data, 0x1000u);
    write_value(state, data, FTFA + 8u, 4, 0x78563412u);
    launch_flash(state, data, 40u);
    TEST_EXPECT(state, load(bus.flash, 0x1000u, 4) == 0x12345678u);
    k22_data_destroy(data);
}

int main(void) {
    TestState state = {0};
    k22_data_reset(NULL);
    test_profile_boundaries(&state);
    test_dma(&state);
    test_dma_advanced(&state);
    test_adc(&state);
    test_adc_compare_dma_and_continuous(&state);
    test_dac_cmp_vref(&state);
    test_rng_crc(&state);
    test_flash_flex_copy(&state);
    test_flash_controller_geometry(&state);
    test_flash_commands_and_failures(&state);
    return test_finish(&state);
}
