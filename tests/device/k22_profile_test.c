#include "k22_profile.h"

#include <stdint.h>
#include <string.h>

#include "k22_profile_expectations.h"
#include "test.h"

static void expect_cpu(TestState* state, const K22Profile* profile,
                       const K22ExpectedProfile* expected) {
    TEST_EXPECT(state, profile->cpu.architecture == K22_CPU_ARCHITECTURE_ARMV7E_M);
    TEST_EXPECT(state, profile->cpu.core_revision_major == 0);
    TEST_EXPECT(state, profile->cpu.core_revision_minor == 1);
    TEST_EXPECT(state, profile->cpu.nvic_priority_bits == 4);
    TEST_EXPECT(state, profile->cpu.little_endian);
    TEST_EXPECT(state, profile->cpu.has_fpu);
    TEST_EXPECT(state, !profile->cpu.has_mpu);
    TEST_EXPECT(state, profile->cpu.has_vtor);
    TEST_EXPECT(state, profile->cpu.has_systick);
    TEST_EXPECT(state,
                profile->cpu.maximum_core_clock_hz == expected->maximum_core_clock_hz);
}

static void expect_memory(TestState* state, const K22Profile* profile,
                          const K22ExpectedProfile* expected) {
    TEST_EXPECT(state, profile->program_flash_size == expected->program_flash_size);
    TEST_EXPECT(state, profile->sram_lower_address == expected->sram_lower_address);
    TEST_EXPECT(state, profile->sram_lower_size == expected->sram_lower_size);
    TEST_EXPECT(state, profile->sram_upper_address == expected->sram_upper_address);
    TEST_EXPECT(state, profile->sram_upper_size == expected->sram_upper_size);
    TEST_EXPECT(state, profile->flexnvm_address == expected->flexnvm_address);
    TEST_EXPECT(state, profile->flexnvm_size == expected->flexnvm_size);
    TEST_EXPECT(state, profile->flexram_address == expected->flexram_address);
    TEST_EXPECT(state, profile->flexram_size == expected->flexram_size);
}

static void expect_block(TestState* state, const K22Profile* profile,
                         const K22ExpectedBlock* expected) {
    K22PeripheralBlock block = {0};
    TEST_EXPECT(state, k22_profile_has_peripheral(profile, expected->id));
    TEST_EXPECT(state, k22_profile_peripheral_block(profile, expected->id, &block));
    TEST_EXPECT(state, block.id == expected->id);
    TEST_EXPECT(state, block.address == expected->address);
    TEST_EXPECT(state, block.size == expected->size);

    K22PeripheralLocation location = {0};
    TEST_EXPECT(state,
                k22_profile_resolve_peripheral(profile, expected->address, 1, &location));
    TEST_EXPECT(state, location.id == expected->id);
    TEST_EXPECT(state, location.block_address == expected->address);
    TEST_EXPECT(state, location.block_size == expected->size);
    TEST_EXPECT(state, location.offset == 0);

    uint32_t last = expected->address + expected->size - 1;
    TEST_EXPECT(state, k22_profile_resolve_peripheral(profile, last, 1, &location));
    TEST_EXPECT(state, location.offset == expected->size - 1);
    TEST_EXPECT(state, !k22_profile_resolve_peripheral(profile, last, 2, NULL));
    if (expected->size >= 4) {
        uint32_t final_word = expected->address + expected->size - 4;
        TEST_EXPECT(state, k22_profile_resolve_peripheral(profile, final_word, 4, NULL));
    }
}

static void expect_profile(TestState* state, const K22ExpectedProfile* expected) {
    const K22Profile* profile = k22_profile_get(expected->id);
    TEST_EXPECT(state, profile != NULL);
    TEST_EXPECT(state, profile->id == expected->id);
    TEST_EXPECT(state, strcmp(profile->name, expected->name) == 0);
    TEST_EXPECT(state, k22_profile_find(expected->name) == profile);
    TEST_EXPECT(state, profile->sim_sdid_reset == expected->sim_sdid_reset);
    TEST_EXPECT(state, profile->sim_sdid_mask == expected->sim_sdid_mask);
    TEST_EXPECT(state, profile->peripheral_block_count == expected->block_count);
    expect_cpu(state, profile, expected);
    expect_memory(state, profile, expected);

    bool expected_ids[K22_PERIPHERAL_COUNT] = {false};
    for (size_t index = 0; index < expected->block_count; index++) {
        TEST_EXPECT(state, !expected_ids[expected->blocks[index].id]);
        expected_ids[expected->blocks[index].id] = true;
        expect_block(state, profile, &expected->blocks[index]);
    }
    for (int id = 0; id < K22_PERIPHERAL_COUNT; id++) {
        TEST_EXPECT(state, k22_profile_has_peripheral(profile, (K22PeripheralId)id) ==
                               expected_ids[id]);
    }
}

static void expect_fail_closed(TestState* state) {
    const K22Profile* small = k22_profile_get(K22_PROFILE_MK22F12810);
    const K22Profile* large = k22_profile_get(K22_PROFILE_MK22FN1M012);
    TEST_EXPECT(state, !k22_profile_resolve_peripheral(NULL, 0x40000000u, 4, NULL));
    TEST_EXPECT(state, !k22_profile_resolve_peripheral(small, 0x40000000u, 4, NULL));
    TEST_EXPECT(state, k22_profile_resolve_peripheral(large, 0x40000000u, 4, NULL));
    TEST_EXPECT(state, !k22_profile_resolve_peripheral(small, 0x40003000u, 4, NULL));
    TEST_EXPECT(state, !k22_profile_resolve_peripheral(small, 0xe000e000u, 4, NULL));
    TEST_EXPECT(state, !k22_profile_resolve_peripheral(small, UINT32_MAX, 4, NULL));
    TEST_EXPECT(state, !k22_profile_resolve_peripheral(small, 0x40008000u, 0, NULL));
    TEST_EXPECT(state, !k22_profile_resolve_peripheral(small, 0x40008000u, 3, NULL));
    TEST_EXPECT(state, !k22_profile_resolve_peripheral(small, 0x40008000u, 8, NULL));
    TEST_EXPECT(state, !k22_profile_peripheral_block(NULL, K22_PERIPHERAL_DMA, NULL));
    TEST_EXPECT(state, !k22_profile_peripheral_block(small, (K22PeripheralId)-1, NULL));
    TEST_EXPECT(state, !k22_profile_peripheral_block(small, K22_PERIPHERAL_COUNT, NULL));
}

int main(void) {
    TestState state = {0};
    for (size_t index = 0; index < EXPECTED_COUNT(expected_k22_profiles); index++)
        expect_profile(&state, &expected_k22_profiles[index]);
    TEST_EXPECT(&state, k22_profile_get((K22ProfileId)-1) == NULL);
    TEST_EXPECT(&state, k22_profile_get(K22_PROFILE_COUNT) == NULL);
    TEST_EXPECT(&state, k22_profile_find(NULL) == NULL);
    TEST_EXPECT(&state, k22_profile_find("") == NULL);
    TEST_EXPECT(&state, k22_profile_find("MK22FN512VLL12") == NULL);
    expect_fail_closed(&state);
    return test_finish(&state);
}
