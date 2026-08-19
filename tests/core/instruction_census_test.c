#include "cortex_m4_sim/cortex_m4.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "test.h"

enum { FLASH_SIZE = 4096, SRAM_SIZE = 65536 };

typedef struct {
    uint8_t flash[FLASH_SIZE];
    uint8_t sram[SRAM_SIZE];
} TestMemory;

bool cortex_m4_execute_remaining(CortexM4* cpu, uint16_t first,
                                 uint16_t second);

static bool memory_region(uint32_t address, uint8_t size, uint32_t base,
                          uint32_t length, uint32_t* offset) {
    if ((size != 1u && size != 2u && size != 4u) || address < base ||
        address - base > length - size) {
        return false;
    }
    *offset = address - base;
    return true;
}

static bool memory_read(void* context, uint32_t address, uint8_t size,
                        CortexM4Access access, uint32_t* value) {
    TestMemory* memory = context;
    uint32_t offset = 0;
    const uint8_t* source = NULL;
    (void)access;
    if (memory_region(address, size, 0, FLASH_SIZE, &offset)) {
        source = &memory->flash[offset];
    } else if (memory_region(address, size, 0x20000000u, SRAM_SIZE, &offset)) {
        source = &memory->sram[offset];
    } else {
        return false;
    }
    *value = 0;
    for (uint8_t index = 0; index < size; index++) {
        *value |= (uint32_t)source[index] << (index * 8u);
    }
    return true;
}

static bool memory_write(void* context, uint32_t address, uint8_t size,
                         CortexM4Access access, uint32_t value) {
    TestMemory* memory = context;
    uint32_t offset = 0;
    uint8_t* destination = NULL;
    (void)access;
    if (memory_region(address, size, 0, FLASH_SIZE, &offset)) {
        destination = &memory->flash[offset];
    } else if (memory_region(address, size, 0x20000000u, SRAM_SIZE, &offset)) {
        destination = &memory->sram[offset];
    } else {
        return false;
    }
    for (uint8_t index = 0; index < size; index++) {
        destination[index] = (uint8_t)(value >> (index * 8u));
    }
    return true;
}

static void store_word(uint8_t* memory, uint32_t value) {
    for (uint8_t index = 0; index < 4; index++) {
        memory[index] = (uint8_t)(value >> (index * 8u));
    }
}

static CortexM4* create_cpu(TestState* state, TestMemory* memory) {
    memset(memory, 0, sizeof(*memory));
    store_word(memory->flash, 0x20001000u);
    store_word(&memory->flash[4], 0x00000101u);
    const CortexM4Bus bus = {memory, memory_read, memory_write, NULL, NULL};
    CortexM4* cpu = cortex_m4_create(bus);
    TEST_EXPECT(state, cpu != NULL);
    return cpu;
}

static void reset_cpu(TestState* state, CortexM4* cpu, TestMemory* memory) {
    memory->flash[0x100] = 0x00u;
    memory->flash[0x101] = 0xbfu;
    TEST_EXPECT(state, cortex_m4_reset(cpu, 0));
}

static void expect_reverse_forms(TestState* state, CortexM4* cpu,
                                 TestMemory* memory) {
    reset_cpu(state, cpu, memory);
    cortex_m4_set_register(cpu, 1, 0x11223344u);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xfa91u, 0xf081u));
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == 0x44332211u);

    reset_cpu(state, cpu, memory);
    cortex_m4_set_register(cpu, 1, 0x11223344u);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xfa91u, 0xf091u));
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == 0x22114433u);

    reset_cpu(state, cpu, memory);
    cortex_m4_set_register(cpu, 1, 0x00000180u);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xfa91u, 0xf0b1u));
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == 0xffff8001u);

    reset_cpu(state, cpu, memory);
    TEST_EXPECT(state, !cortex_m4_execute_remaining(cpu, 0xfa91u, 0xf0a1u));

    reset_cpu(state, cpu, memory);
    TEST_EXPECT(state, !cortex_m4_execute_remaining(cpu, 0xfa9du, 0xf08du));
    TEST_EXPECT(state, !cortex_m4_execute_remaining(cpu, 0xfa91u, 0xff81u));
    TEST_EXPECT(state, !cortex_m4_execute_remaining(cpu, 0xfa91u, 0xf0c1u));
}

static void expect_hint_forms(TestState* state, CortexM4* cpu,
                              TestMemory* memory) {
    reset_cpu(state, cpu, memory);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf3afu, 0x8000u));
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf3afu, 0x8001u));
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf3afu, 0x80f9u));
    TEST_EXPECT(state, !cortex_m4_execute_remaining(cpu, 0xf3afu, 0x8005u));
    TEST_EXPECT(state, !cortex_m4_execute_remaining(cpu, 0xf3afu, 0x8100u));

    reset_cpu(state, cpu, memory);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf3afu, 0x8002u));
    const CortexM4Result sleeping_result = cortex_m4_step(cpu);
    TEST_EXPECT(state, sleeping_result.stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, sleeping_result.instructions == 0);

    reset_cpu(state, cpu, memory);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf3afu, 0x8004u));
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf3afu, 0x8002u));
    const CortexM4Result event_result = cortex_m4_step(cpu);
    TEST_EXPECT(state, event_result.stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, event_result.instructions == 1);

    reset_cpu(state, cpu, memory);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf3afu, 0x8004u));
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf3afu, 0x8002u));
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf3afu, 0x8002u));
    const CortexM4Result consumed_event_result = cortex_m4_step(cpu);
    TEST_EXPECT(state, consumed_event_result.stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, consumed_event_result.instructions == 0);

    reset_cpu(state, cpu, memory);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf3afu, 0x8003u));
    const CortexM4Result interrupt_result = cortex_m4_step(cpu);
    TEST_EXPECT(state, interrupt_result.stop == CORTEX_M4_STOP_RUNNING);
    TEST_EXPECT(state, interrupt_result.instructions == 0);
}

static void expect_preload_forms(TestState* state, CortexM4* cpu,
                                 TestMemory* memory) {
    static const uint16_t forms[][2] = {
        {0xf890u, 0xf004u}, {0xf810u, 0xfc04u}, {0xf810u, 0xf021u},
        {0xf990u, 0xf004u}, {0xf910u, 0xfc04u}, {0xf910u, 0xf021u},
    };
    reset_cpu(state, cpu, memory);
    cortex_m4_set_register(cpu, 0, 0x60000000u);
    cortex_m4_set_register(cpu, 1, 1u);
    const uint32_t status = cortex_m4_get_fault_status(cpu);
    for (uint8_t index = 0; index < sizeof(forms) / sizeof(forms[0]); index++) {
        TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, forms[index][0],
                                                       forms[index][1]));
        TEST_EXPECT(state, cortex_m4_get_fault_status(cpu) == status);
        TEST_EXPECT(state, cortex_m4_get_stop(cpu) == CORTEX_M4_STOP_RUNNING);
    }
    TEST_EXPECT(state, !cortex_m4_execute_remaining(cpu, 0xf810u, 0xfd04u));
    TEST_EXPECT(state, !cortex_m4_execute_remaining(cpu, 0xf800u, 0xf004u));
}

static void write_value(TestState* state, CortexM4* cpu, uint32_t address,
                        uint8_t size, uint32_t value) {
    TEST_EXPECT(state, cortex_m4_write_memory(cpu, address, size, value));
}

static void expect_negative_literal_forms(TestState* state, CortexM4* cpu,
                                          TestMemory* memory) {
    const uint32_t address = 0x20000000u;

    reset_cpu(state, cpu, memory);
    const uint32_t word = 0x89abcdefu;
    write_value(state, cpu, address, 4, word);
    cortex_m4_set_register(cpu, 15, address + 6u);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf85fu, 0x0004u));
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 0) == word);

    reset_cpu(state, cpu, memory);
    write_value(state, cpu, address, 1, 0x80u);
    cortex_m4_set_register(cpu, 15, address + 6u);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf91fu, 0x1004u));
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 1) == 0xffffff80u);

    reset_cpu(state, cpu, memory);
    write_value(state, cpu, address, 2, 0x8001u);
    cortex_m4_set_register(cpu, 15, address + 6u);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf93fu, 0x2004u));
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 2) == 0xffff8001u);

    reset_cpu(state, cpu, memory);
    write_value(state, cpu, address, 1, 0x80u);
    cortex_m4_set_register(cpu, 15, address + 6u);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf81fu, 0x3004u));
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 3) == 0x80u);

    reset_cpu(state, cpu, memory);
    write_value(state, cpu, address, 2, 0x8001u);
    cortex_m4_set_register(cpu, 15, address + 6u);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf83fu, 0x4004u));
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 4) == 0x8001u);

    reset_cpu(state, cpu, memory);
    write_value(state, cpu, address, 4, 0x00000101u);
    cortex_m4_set_register(cpu, 15, address + 6u);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf85fu, 0xf004u));
    TEST_EXPECT(state, cortex_m4_get_register(cpu, 15) == 0x100u);

    reset_cpu(state, cpu, memory);
    write_value(state, cpu, address, 4, 0x00000100u);
    cortex_m4_set_register(cpu, 15, address + 6u);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf85fu, 0xf004u));
    TEST_EXPECT(state, (cortex_m4_get_fault_status(cpu) & (1u << 17)) != 0);

    reset_cpu(state, cpu, memory);
    cortex_m4_set_register(cpu, 15, address + 6u);
    TEST_EXPECT(state, !cortex_m4_execute_remaining(cpu, 0xf93fu, 0xf004u));
    TEST_EXPECT(state, !cortex_m4_execute_remaining(cpu, 0xf80fu, 0x0004u));

    reset_cpu(state, cpu, memory);
    cortex_m4_set_register(cpu, 15, 0x60000006u);
    TEST_EXPECT(state, cortex_m4_execute_remaining(cpu, 0xf85fu, 0x0004u));
    TEST_EXPECT(state, cortex_m4_get_fault_status(cpu) != 0);
}

int main(void) {
    TestState state = {0};
    TestMemory memory;
    CortexM4* cpu = create_cpu(&state, &memory);
    expect_reverse_forms(&state, cpu, &memory);
    expect_hint_forms(&state, cpu, &memory);
    expect_preload_forms(&state, cpu, &memory);
    expect_negative_literal_forms(&state, cpu, &memory);
    cortex_m4_destroy(cpu);
    return test_finish(&state);
}
