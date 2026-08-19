#include "cortex_m4_sim/kinetis_k22.h"

#include <stdint.h>

#include "kinetis_k22_internal.h"
#include "test.h"

enum {
    PFAPR = 0x4001f000u,
    PFB0CR = 0x4001f004u,
    PFB1CR = 0x4001f008u,
    FTFE = 0x40020000u,
    FLEXNVM = 0x10000000u,
    FLEXRAM = 0x14000000u,
    TAG_W0_S0 = 0x4001f100u,
    TAG_W1_S0 = 0x4001f110u,
    TAG_W2_S0 = 0x4001f120u,
    TAG_W3_S0 = 0x4001f130u,
    DATA_W0_S0_UM = 0x4001f200u,
    DATA_W0_S0_LM = 0x4001f20cu,
    INVALIDATE_ALL = 0x00f00000u,
};

static KinetisK22* create_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.package = KINETIS_K22_PACKAGE_DC_121_XFBGA;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(state, device != NULL);
    TEST_EXPECT(state, kinetis_k22_reset(device));
    return device;
}

static KinetisK22* create_flexnvm_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.profile = KINETIS_K22_PROFILE_MK22FX51212;
    configuration.package = KINETIS_K22_PACKAGE_MC_121_MAPBGA;
    KinetisK22* device = kinetis_k22_create(configuration);
    TEST_EXPECT(state, device != NULL);
    TEST_EXPECT(state, kinetis_k22_reset(device));
    return device;
}

static uint32_t read_register(TestState* state, KinetisK22* device, uint32_t address) {
    uint32_t value = UINT32_MAX;
    TEST_EXPECT(state, kinetis_k22_peripheral_read(device, address, 4u,
                                                   CORTEX_M4_ACCESS_DATA, &value));
    return value;
}

static void write_register(TestState* state, KinetisK22* device, uint32_t address,
                           uint32_t value) {
    TEST_EXPECT(state, kinetis_k22_peripheral_write(device, address, 4u,
                                                    CORTEX_M4_ACCESS_DATA, value));
}

static void write_register_byte(TestState* state, KinetisK22* device, uint32_t address,
                                uint8_t value) {
    TEST_EXPECT(state, kinetis_k22_peripheral_write(device, address, 1u,
                                                    CORTEX_M4_ACCESS_DATA, value));
}

static void write_flash(TestState* state, KinetisK22* device, uint32_t address,
                        uint8_t value) {
    TEST_EXPECT(state, kinetis_k22_write(device, address, &value, sizeof(value)));
}

static uint8_t read_flash(TestState* state, KinetisK22* device, uint32_t address,
                          CortexM4Access access) {
    uint32_t value = UINT32_MAX;
    TEST_EXPECT(state, kinetis_k22_memory_read(device, address, 1u, access, &value));
    return (uint8_t)value;
}

static void configure_cache(TestState* state, KinetisK22* device, uint32_t fields) {
    const uint32_t control = read_register(state, device, PFB0CR);
    write_register(state, device, PFB0CR,
                   (control & ~(0x0f0000e0u | 0x0f000000u)) | INVALIDATE_ALL | fields);
}

static uint32_t fccob_address(uint8_t index) {
    static const uint8_t offsets[12] = {7u, 6u, 5u,  4u,  11u, 10u,
                                        9u, 8u, 15u, 14u, 13u, 12u};
    return FTFE + offsets[index];
}

static void launch_flexnvm_phrase(TestState* state, KinetisK22* device, uint32_t offset,
                                  uint8_t value) {
    write_register_byte(state, device, fccob_address(0u), 0x07u);
    write_register_byte(state, device, fccob_address(1u),
                        (uint8_t)((0x800000u + offset) >> 16u));
    write_register_byte(state, device, fccob_address(2u), (uint8_t)(offset >> 8u));
    write_register_byte(state, device, fccob_address(3u), (uint8_t)offset);
    for (uint8_t index = 4u; index < 12u; index++)
        write_register_byte(state, device, fccob_address(index), value);
    write_register_byte(state, device, FTFE, 0x80u);
}

static void program_flexnvm_phrase(TestState* state, KinetisK22* device, uint32_t offset,
                                   uint8_t value) {
    launch_flexnvm_phrase(state, device, offset, value);
    kinetis_k22_advance(device, 40u);
}

static void test_visible_cache_and_invalidation(TestState* state) {
    KinetisK22* device = create_device(state);
    configure_cache(state, device, 0x0e000000u);
    write_flash(state, device, 0x100u, 0x5au);
    TEST_EXPECT(state, read_flash(state, device, 0x100u, CORTEX_M4_ACCESS_DATA) == 0x5au);
    TEST_EXPECT(state, read_register(state, device, TAG_W0_S0) == 0x101u);
    TEST_EXPECT(state, (read_register(state, device, DATA_W0_S0_LM) & 0xffu) == 0x5au);
    TEST_EXPECT(state, kinetis_k22_flash_controller_write(device, 0x100u, 1u, 0xa5u));
    TEST_EXPECT(state, read_flash(state, device, 0x100u, CORTEX_M4_ACCESS_DATA) == 0x5au);
    const uint32_t control = read_register(state, device, PFB0CR);
    write_register(state, device, PFB0CR, control | (1u << 20u));
    TEST_EXPECT(state, read_register(state, device, TAG_W0_S0) == 0u);
    TEST_EXPECT(state, read_register(state, device, DATA_W0_S0_UM) == 0u);
    TEST_EXPECT(state, read_register(state, device, DATA_W0_S0_LM) == 0u);
    TEST_EXPECT(state, read_flash(state, device, 0x100u, CORTEX_M4_ACCESS_DATA) == 0xa5u);
    kinetis_k22_destroy(device);
}

static void test_lru_and_lock(TestState* state) {
    KinetisK22* device = create_device(state);
    configure_cache(state, device, 0u);
    const uint32_t addresses[] = {0x100u, 0x140u, 0x180u, 0x1c0u, 0x200u};
    for (uint8_t index = 0u; index < 5u; index++) {
        write_flash(state, device, addresses[index], (uint8_t)(index + 1u));
        if (index < 4u)
            TEST_EXPECT(state, read_flash(state, device, addresses[index],
                                          CORTEX_M4_ACCESS_DATA) == index + 1u);
    }
    TEST_EXPECT(state,
                read_flash(state, device, addresses[0], CORTEX_M4_ACCESS_DATA) == 1u);
    TEST_EXPECT(state, kinetis_k22_flash_controller_write(device, addresses[1], 1u, 0x22u));
    TEST_EXPECT(state,
                read_flash(state, device, addresses[4], CORTEX_M4_ACCESS_DATA) == 5u);
    TEST_EXPECT(state,
                read_flash(state, device, addresses[1], CORTEX_M4_ACCESS_DATA) == 0x22u);

    configure_cache(state, device, 0u);
    write_flash(state, device, addresses[0], 0x31u);
    TEST_EXPECT(state,
                read_flash(state, device, addresses[0], CORTEX_M4_ACCESS_DATA) == 0x31u);
    const uint32_t control = read_register(state, device, PFB0CR);
    write_register(state, device, PFB0CR, control | (1u << 24u));
    TEST_EXPECT(state, kinetis_k22_flash_controller_write(device, addresses[0], 1u, 0x32u));
    for (uint8_t index = 1u; index < 5u; index++)
        (void)read_flash(state, device, addresses[index], CORTEX_M4_ACCESS_DATA);
    TEST_EXPECT(state,
                read_flash(state, device, addresses[0], CORTEX_M4_ACCESS_DATA) == 0x31u);
    kinetis_k22_destroy(device);
}

static void test_partitioned_replacement(TestState* state) {
    KinetisK22* device = create_device(state);
    write_flash(state, device, 0x240u, 0x24u);
    write_flash(state, device, 0x280u, 0x28u);
    configure_cache(state, device, 2u << 5u);
    TEST_EXPECT(state,
                read_flash(state, device, 0x240u, CORTEX_M4_ACCESS_INSTRUCTION) == 0x24u);
    TEST_EXPECT(state, read_flash(state, device, 0x280u, CORTEX_M4_ACCESS_DATA) == 0x28u);
    TEST_EXPECT(state, read_register(state, device, TAG_W0_S0) == 0x241u);
    TEST_EXPECT(state, read_register(state, device, TAG_W2_S0) == 0x281u);

    write_flash(state, device, 0x2c0u, 0x2cu);
    write_flash(state, device, 0x300u, 0x30u);
    configure_cache(state, device, 3u << 5u);
    TEST_EXPECT(state,
                read_flash(state, device, 0x2c0u, CORTEX_M4_ACCESS_INSTRUCTION) == 0x2cu);
    TEST_EXPECT(state, read_flash(state, device, 0x300u, CORTEX_M4_ACCESS_DATA) == 0x30u);
    TEST_EXPECT(state, read_register(state, device, TAG_W0_S0) == 0x2c1u);
    TEST_EXPECT(state, read_register(state, device, TAG_W3_S0) == 0x301u);
    kinetis_k22_destroy(device);
}

static void test_master_permissions(TestState* state) {
    KinetisK22* device = create_device(state);
    write_flash(state, device, 0x100u, 0x5au);
    uint32_t value = 0u;
    TEST_EXPECT(state, !kinetis_k22_peripheral_write(
                           device, PFAPR, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, 0u));
    write_register(state, device, PFAPR, 1u);
    TEST_EXPECT(state,
                read_flash(state, device, 0x100u, CORTEX_M4_ACCESS_INSTRUCTION) == 0x5au);
    TEST_EXPECT(
        state, !kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value));
    TEST_EXPECT(state, !kinetis_k22_dma_read(device, 0x100u, 1u, &value));
    write_register(state, device, PFAPR, 4u);
    TEST_EXPECT(state, read_flash(state, device, 0x100u, CORTEX_M4_ACCESS_DATA) == 0x5au);
    TEST_EXPECT(state, !kinetis_k22_memory_read(device, 0x100u, 1u,
                                                CORTEX_M4_ACCESS_INSTRUCTION, &value));
    TEST_EXPECT(state, !kinetis_k22_dma_read(device, 0x100u, 1u, &value));
    write_register(state, device, PFAPR, 0x10u);
    TEST_EXPECT(state, kinetis_k22_dma_read(device, 0x100u, 1u, &value));
    TEST_EXPECT(state, value == 0x5au);
    TEST_EXPECT(
        state, !kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DATA, &value));
    TEST_EXPECT(
        state, kinetis_k22_memory_read(device, 0x100u, 1u, CORTEX_M4_ACCESS_DEBUG, &value));
    TEST_EXPECT(state, value == 0x5au);
    kinetis_k22_destroy(device);
}

static void test_copy(TestState* state) {
    KinetisK22* source = create_device(state);
    KinetisK22* destination = create_device(state);
    configure_cache(state, source, 0x0e000000u);
    write_flash(state, source, 0x100u, 0x11u);
    TEST_EXPECT(state, read_flash(state, source, 0x100u, CORTEX_M4_ACCESS_DATA) == 0x11u);
    TEST_EXPECT(state, kinetis_k22_flash_controller_write(source, 0x100u, 1u, 0x22u));
    TEST_EXPECT(state, kinetis_k22_copy(destination, source));
    TEST_EXPECT(state,
                read_flash(state, destination, 0x100u, CORTEX_M4_ACCESS_DATA) == 0x11u);
    kinetis_k22_destroy(source);
    kinetis_k22_destroy(destination);
}

static void test_bank_one_cache(TestState* state) {
    KinetisK22* device = create_flexnvm_device(state);
    uint8_t program = 0x11u;
    uint8_t data = 0x22u;
    TEST_EXPECT(state, kinetis_k22_write(device, 0x100u, &program, sizeof(program)));
    TEST_EXPECT(state, kinetis_k22_write(device, FLEXNVM + 0x100u, &data, sizeof(data)));
    TEST_EXPECT(state, read_flash(state, device, 0x100u, CORTEX_M4_ACCESS_DATA) == program);
    TEST_EXPECT(state,
                read_flash(state, device, FLEXNVM + 0x100u, CORTEX_M4_ACCESS_DATA) == data);
    TEST_EXPECT(state, read_register(state, device, TAG_W0_S0) == 0x101u);
    TEST_EXPECT(state, read_register(state, device, TAG_W1_S0) == 0x101u);
    TEST_EXPECT(state, (read_register(state, device, DATA_W0_S0_LM) & 0xffu) == program);
    TEST_EXPECT(state,
                (read_register(state, device, DATA_W0_S0_LM + 0x40u) & 0xffu) == data);

    write_register(state, device, PFAPR, 1u);
    uint32_t value = 0u;
    TEST_EXPECT(state, !kinetis_k22_memory_read(device, FLEXNVM + 0x100u, 1u,
                                                CORTEX_M4_ACCESS_DATA, &value));
    TEST_EXPECT(state, !kinetis_k22_dma_read(device, FLEXNVM + 0x100u, 1u, &value));
    TEST_EXPECT(state, !kinetis_k22_memory_read(device, FLEXRAM, 1u, CORTEX_M4_ACCESS_DATA,
                                                &value));
    TEST_EXPECT(state, !kinetis_k22_memory_write(device, FLEXRAM, 1u, CORTEX_M4_ACCESS_DATA,
                                                 0x5au));
    TEST_EXPECT(state, !kinetis_k22_dma_write(device, FLEXRAM, 1u, 0x5au));
    write_register(state, device, PFAPR, 0x3fu);
    TEST_EXPECT(state, !kinetis_k22_memory_write(device, FLEXNVM + 0x100u, 1u,
                                                 CORTEX_M4_ACCESS_DATA, 0u));
    TEST_EXPECT(
        state, kinetis_k22_memory_write(device, FLEXRAM, 1u, CORTEX_M4_ACCESS_DATA, 0x5au));
    TEST_EXPECT(
        state, kinetis_k22_memory_read(device, FLEXRAM, 1u, CORTEX_M4_ACCESS_DATA, &value));
    TEST_EXPECT(state, value == 0x5au);
    kinetis_k22_destroy(device);
}

static void test_bank_one_coherence_and_copy(TestState* state) {
    KinetisK22* source = create_flexnvm_device(state);
    KinetisK22* destination = create_flexnvm_device(state);
    TEST_EXPECT(state, read_flash(state, source, FLEXNVM + 0x140u, CORTEX_M4_ACCESS_DATA) ==
                           0xffu);
    program_flexnvm_phrase(state, source, 0x140u, 0xa5u);
    TEST_EXPECT(state, read_flash(state, source, FLEXNVM + 0x140u, CORTEX_M4_ACCESS_DATA) ==
                           0xffu);
    TEST_EXPECT(state, kinetis_k22_copy(destination, source));
    TEST_EXPECT(state, read_flash(state, destination, FLEXNVM + 0x140u,
                                  CORTEX_M4_ACCESS_DATA) == 0xffu);
    const uint32_t control = read_register(state, source, PFB0CR);
    write_register(state, source, PFB0CR, control | (1u << 20u));
    TEST_EXPECT(state, read_flash(state, source, FLEXNVM + 0x140u, CORTEX_M4_ACCESS_DATA) ==
                           0xa5u);

    const uint32_t bank_one_control = read_register(state, source, PFB1CR);
    TEST_EXPECT(state, read_flash(state, source, FLEXNVM + 0x180u, CORTEX_M4_ACCESS_DATA) ==
                           0xffu);
    write_register(state, source, PFB1CR, bank_one_control & ~0x10u);
    program_flexnvm_phrase(state, source, 0x180u, 0x5au);
    TEST_EXPECT(state, read_flash(state, source, FLEXNVM + 0x180u, CORTEX_M4_ACCESS_DATA) ==
                           0x5au);
    kinetis_k22_destroy(source);
    kinetis_k22_destroy(destination);
}

static void test_flash_collision_irq(TestState* state) {
    KinetisK22* device = create_flexnvm_device(state);
    write_register_byte(state, device, FTFE + 1u, 0xc0u);
    launch_flexnvm_phrase(state, device, 0x200u, 0x3cu);
    uint32_t value = 0u;
    TEST_EXPECT(state, kinetis_k22_memory_read(device, FLEXNVM + 0x200u, 1u,
                                               CORTEX_M4_ACCESS_DATA, &value));
    TEST_EXPECT(state, value == 0xffu);
    TEST_EXPECT(state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), 19u));
    write_register_byte(state, device, FTFE, 0x40u);
    cortex_m4_set_irq(kinetis_k22_cpu(device), 19u, false);
    TEST_EXPECT(state, !cortex_m4_get_irq_pending(kinetis_k22_cpu(device), 19u));
    kinetis_k22_advance(device, 40u);
    TEST_EXPECT(state, cortex_m4_get_irq_pending(kinetis_k22_cpu(device), 18u));
    TEST_EXPECT(state, kinetis_k22_memory_read(device, FLEXNVM + 0x200u, 1u,
                                               CORTEX_M4_ACCESS_DATA, &value));
    TEST_EXPECT(state, value == 0x3cu);
    kinetis_k22_destroy(device);
}

int main(void) {
    TestState state = {0};
    test_visible_cache_and_invalidation(&state);
    test_lru_and_lock(&state);
    test_partitioned_replacement(&state);
    test_master_permissions(&state);
    test_copy(&state);
    test_bank_one_cache(&state);
    test_bank_one_coherence_and_copy(&state);
    test_flash_collision_irq(&state);
    return test_finish(&state);
}
