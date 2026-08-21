#include "k22_profile.h"

#include <stdint.h>
#include <string.h>

#include "k22_profile_expectations.h"
#include "test.h"

static void expect_cpu(TestState* state, const K22Profile* profile,
                       const K22ExpectedProfile* expected) {
    expect(state, profile->cpu.architecture == K22_CPU_ARCHITECTURE_ARMV7E_M,
           "profile->cpu.architecture == K22_CPU_ARCHITECTURE_ARMV7E_M");
    expect(state, profile->cpu.core_revision_major == 0, "profile->cpu.core_revision_major == 0");
    expect(state, profile->cpu.core_revision_minor == 1, "profile->cpu.core_revision_minor == 1");
    expect(state, profile->cpu.nvic_priority_bits == 4, "profile->cpu.nvic_priority_bits == 4");
    expect(state, profile->cpu.external_irq_count == expected->external_irq_count,
           "profile->cpu.external_irq_count == expected->external_irq_count");
    expect(state, profile->cpu.little_endian, "profile->cpu.little_endian");
    expect(state, profile->cpu.has_fpu, "profile->cpu.has_fpu");
    expect(state, !profile->cpu.has_mpu, "!profile->cpu.has_mpu");
    expect(state, profile->cpu.has_vtor, "profile->cpu.has_vtor");
    expect(state, profile->cpu.has_systick, "profile->cpu.has_systick");
    expect(state, profile->cpu.maximum_core_clock_hz == expected->maximum_core_clock_hz,
           "profile->cpu.maximum_core_clock_hz == expected->maximum_core_clock_hz");
}

static void expect_memory(TestState* state, const K22Profile* profile,
                          const K22ExpectedProfile* expected) {
    expect(state, profile->program_flash_size == expected->program_flash_size,
           "profile->program_flash_size == expected->program_flash_size");
    expect(state, profile->sram_lower_address == expected->sram_lower_address,
           "profile->sram_lower_address == expected->sram_lower_address");
    expect(state, profile->sram_lower_size == expected->sram_lower_size,
           "profile->sram_lower_size == expected->sram_lower_size");
    expect(state, profile->sram_upper_address == expected->sram_upper_address,
           "profile->sram_upper_address == expected->sram_upper_address");
    expect(state, profile->sram_upper_size == expected->sram_upper_size,
           "profile->sram_upper_size == expected->sram_upper_size");
    expect(state, profile->flexnvm_address == expected->flexnvm_address,
           "profile->flexnvm_address == expected->flexnvm_address");
    expect(state, profile->flexnvm_size == expected->flexnvm_size,
           "profile->flexnvm_size == expected->flexnvm_size");
    expect(state, profile->flexram_address == expected->flexram_address,
           "profile->flexram_address == expected->flexram_address");
    expect(state, profile->flexram_size == expected->flexram_size,
           "profile->flexram_size == expected->flexram_size");
}

static void expect_block(TestState* state, const K22Profile* profile,
                         const K22ExpectedBlock* expected) {
    K22PeripheralBlock block = {0};
    expect(state, k22_profile_has_peripheral(profile, expected->id),
           "k22_profile_has_peripheral(profile, expected->id)");
    expect(state, k22_profile_peripheral_block(profile, expected->id, &block),
           "k22_profile_peripheral_block(profile, expected->id, &block)");
    expect(state, block.id == expected->id, "block.id == expected->id");
    expect(state, block.address == expected->address, "block.address == expected->address");
    expect(state, block.size == expected->size, "block.size == expected->size");

    K22PeripheralLocation location = {0};
    expect(state, k22_profile_resolve_peripheral(profile, expected->address, 1, &location),
           "k22_profile_resolve_peripheral(profile, expected->address, 1, &location)");
    expect(state, location.id == expected->id, "location.id == expected->id");
    expect(state, location.block_address == expected->address,
           "location.block_address == expected->address");
    expect(state, location.block_size == expected->size, "location.block_size == expected->size");
    expect(state, location.offset == 0, "location.offset == 0");

    uint32_t last = expected->address + expected->size - 1;
    expect(state, k22_profile_resolve_peripheral(profile, last, 1, &location),
           "k22_profile_resolve_peripheral(profile, last, 1, &location)");
    expect(state, location.offset == expected->size - 1, "location.offset == expected->size - 1");
    expect(state, !k22_profile_resolve_peripheral(profile, last, 2, NULL),
           "!k22_profile_resolve_peripheral(profile, last, 2, NULL)");
    if (expected->size >= 4) {
        uint32_t final_word = expected->address + expected->size - 4;
        expect(state, k22_profile_resolve_peripheral(profile, final_word, 4, NULL),
               "k22_profile_resolve_peripheral(profile, final_word, 4, NULL)");
    }
}

static void expect_profile(TestState* state, const K22ExpectedProfile* expected) {
    const K22Profile* profile = k22_profile_get(expected->id);
    expect(state, profile != NULL, "profile != NULL");
    expect(state, profile->id == expected->id, "profile->id == expected->id");
    expect(state, strcmp(profile->name, expected->name) == 0,
           "strcmp(profile->name, expected->name) == 0");
    expect(state, k22_profile_find(expected->name) == profile,
           "k22_profile_find(expected->name) == profile");
    expect(state, profile->sim_sdid_reset == expected->sim_sdid_reset,
           "profile->sim_sdid_reset == expected->sim_sdid_reset");
    expect(state, profile->sim_sdid_mask == expected->sim_sdid_mask,
           "profile->sim_sdid_mask == expected->sim_sdid_mask");
    expect(state, profile->peripheral_block_count == expected->block_count,
           "profile->peripheral_block_count == expected->block_count");
    expect_cpu(state, profile, expected);
    expect_memory(state, profile, expected);

    bool expected_ids[K22_PERIPHERAL_COUNT] = {false};
    for (size_t index = 0; index < expected->block_count; index++) {
        expect(state, !expected_ids[expected->blocks[index].id],
               "!expected_ids[expected->blocks[index].id]");
        expected_ids[expected->blocks[index].id] = true;
        expect_block(state, profile, &expected->blocks[index]);
    }
    for (int id = 0; id < K22_PERIPHERAL_COUNT; id++) {
        expect(state, k22_profile_has_peripheral(profile, (K22PeripheralId)id) == expected_ids[id],
               "k22_profile_has_peripheral(profile, (K22PeripheralId)id) == expected_ids[id]");
    }
}

static void expect_fail_closed(TestState* state) {
    const K22Profile* small = k22_profile_get(K22_PROFILE_MK22F12810);
    const K22Profile* large = k22_profile_get(K22_PROFILE_MK22FN1M012);
    expect(state, !k22_profile_resolve_peripheral(NULL, 0x40000000u, 4, NULL),
           "!k22_profile_resolve_peripheral(NULL, 0x40000000u, 4, NULL)");
    expect(state, !k22_profile_resolve_peripheral(small, 0x40000000u, 4, NULL),
           "!k22_profile_resolve_peripheral(small, 0x40000000u, 4, NULL)");
    expect(state, k22_profile_resolve_peripheral(large, 0x40000000u, 4, NULL),
           "k22_profile_resolve_peripheral(large, 0x40000000u, 4, NULL)");
    expect(state, !k22_profile_resolve_peripheral(small, 0x40003000u, 4, NULL),
           "!k22_profile_resolve_peripheral(small, 0x40003000u, 4, NULL)");
    expect(state, !k22_profile_resolve_peripheral(small, 0xe000e000u, 4, NULL),
           "!k22_profile_resolve_peripheral(small, 0xe000e000u, 4, NULL)");
    expect(state, !k22_profile_resolve_peripheral(small, UINT32_MAX, 4, NULL),
           "!k22_profile_resolve_peripheral(small, UINT32_MAX, 4, NULL)");
    expect(state, !k22_profile_resolve_peripheral(small, 0x40008000u, 0, NULL),
           "!k22_profile_resolve_peripheral(small, 0x40008000u, 0, NULL)");
    expect(state, !k22_profile_resolve_peripheral(small, 0x40008000u, 3, NULL),
           "!k22_profile_resolve_peripheral(small, 0x40008000u, 3, NULL)");
    expect(state, !k22_profile_resolve_peripheral(small, 0x40008000u, 8, NULL),
           "!k22_profile_resolve_peripheral(small, 0x40008000u, 8, NULL)");
    expect(state, !k22_profile_peripheral_block(NULL, K22_PERIPHERAL_DMA, NULL),
           "!k22_profile_peripheral_block(NULL, K22_PERIPHERAL_DMA, NULL)");
    expect(state, !k22_profile_peripheral_block(small, (K22PeripheralId)-1, NULL),
           "!k22_profile_peripheral_block(small, (K22PeripheralId)-1, NULL)");
    expect(state, !k22_profile_peripheral_block(small, K22_PERIPHERAL_COUNT, NULL),
           "!k22_profile_peripheral_block(small, K22_PERIPHERAL_COUNT, NULL)");
}

int main(void) {
    TestState state = {0};
    for (size_t index = 0; index < EXPECTED_COUNT(expected_k22_profiles); index++)
        expect_profile(&state, &expected_k22_profiles[index]);
    expect(&state, k22_profile_get((K22ProfileId)-1) == NULL,
           "k22_profile_get((K22ProfileId)-1) == NULL");
    expect(&state, k22_profile_get(K22_PROFILE_COUNT) == NULL,
           "k22_profile_get(K22_PROFILE_COUNT) == NULL");
    expect(&state, k22_profile_find(NULL) == NULL, "k22_profile_find(NULL) == NULL");
    expect(&state, k22_profile_find("") == NULL, "k22_profile_find(\"\") == NULL");
    expect(&state, k22_profile_find("MK22FN512VLL12") == NULL,
           "k22_profile_find(\"MK22FN512VLL12\") == NULL");
    expect_fail_closed(&state);
    return test_finish(&state);
}
