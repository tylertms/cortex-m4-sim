#include "k22_sdhc.h"

#include <stdint.h>
#include <string.h>

#include "test.h"

enum {
    SDHC_BASE = 0x400b1000u,
    SDHC_DSADDR = SDHC_BASE + 0x00u,
    SDHC_BLKATTR = SDHC_BASE + 0x04u,
    SDHC_CMDARG = SDHC_BASE + 0x08u,
    SDHC_XFERTYP = SDHC_BASE + 0x0cu,
    SDHC_CMDRSP0 = SDHC_BASE + 0x10u,
    SDHC_CMDRSP1 = SDHC_BASE + 0x14u,
    SDHC_CMDRSP2 = SDHC_BASE + 0x18u,
    SDHC_CMDRSP3 = SDHC_BASE + 0x1cu,
    SDHC_DATPORT = SDHC_BASE + 0x20u,
    SDHC_PRSSTAT = SDHC_BASE + 0x24u,
    SDHC_SYSCTL = SDHC_BASE + 0x2cu,
    SDHC_IRQSTAT = SDHC_BASE + 0x30u,
    SDHC_IRQSTATEN = SDHC_BASE + 0x34u,
    SDHC_IRQSIGEN = SDHC_BASE + 0x38u,
    SDHC_FEVT = SDHC_BASE + 0x50u,
    SDHC_HOSTVER = SDHC_BASE + 0xfcu,
};

typedef struct {
    uint8_t data[2048];
    bool fail_read;
    bool fail_write;
} BusMemory;

static bool bus_read(void* context, uint32_t address, uint8_t size, uint32_t* value) {
    BusMemory* memory = context;
    if (value == NULL || memory->fail_read || address > sizeof(memory->data) ||
        size > sizeof(memory->data) - address)
        return false;
    *value = 0u;
    memcpy(value, memory->data + address, size);
    return true;
}

static bool bus_write(void* context, uint32_t address, uint8_t size, uint32_t value) {
    BusMemory* memory = context;
    if (memory->fail_write || address > sizeof(memory->data) ||
        size > sizeof(memory->data) - address)
        return false;
    memcpy(memory->data + address, &value, size);
    return true;
}

static uint32_t read_register(TestState* state, K22Sdhc* sdhc, uint32_t address) {
    uint32_t value = 0u;
    TEST_EXPECT(state, k22_sdhc_read(sdhc, address, 4u, &value));
    return value;
}

static void write_register(TestState* state, K22Sdhc* sdhc, uint32_t address,
                           uint32_t value) {
    TEST_EXPECT(state, k22_sdhc_write(sdhc, address, 4u, value));
}

static void command(TestState* state, K22Sdhc* sdhc, uint8_t index, uint32_t argument,
                    uint32_t options) {
    write_register(state, sdhc, SDHC_CMDARG, argument);
    write_register(state, sdhc, SDHC_XFERTYP, (uint32_t)index << 24u | options);
}

static K22Sdhc create_sdhc(TestState* state, BusMemory* memory) {
    K22Sdhc sdhc;
    const K22SdhcBus bus = {memory, bus_read, bus_write};
    TEST_EXPECT(state, k22_sdhc_init(&sdhc, bus));
    k22_sdhc_set_clock(&sdhc, true);
    write_register(state, &sdhc, SDHC_IRQSTATEN, UINT32_MAX);
    write_register(state, &sdhc, SDHC_IRQSIGEN, UINT32_MAX);
    return sdhc;
}

static void select_card(TestState* state, K22Sdhc* sdhc) {
    command(state, sdhc, 3u, 0u, 0u);
    TEST_EXPECT(state, read_register(state, sdhc, SDHC_CMDRSP0) == 0x00010000u);
    command(state, sdhc, 7u, 0x00010000u, 0u);
}

static void expect_initialization_and_access(TestState* state) {
    BusMemory memory = {0};
    K22Sdhc sdhc = create_sdhc(state, &memory);
    uint8_t card[1024];
    for (size_t index = 0u; index < sizeof(card); index++)
        card[index] = (uint8_t)index;

    TEST_EXPECT(state, k22_sdhc_insert(&sdhc, card, sizeof(card), false));
    TEST_EXPECT(state, k22_sdhc_irq(&sdhc));
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_PRSSTAT) & (1u << 16u)) != 0u);
    write_register(state, &sdhc, SDHC_IRQSTAT, UINT32_MAX);
    TEST_EXPECT(state, !k22_sdhc_irq(&sdhc));

    command(state, &sdhc, 8u, 0x1aau, 0u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_CMDRSP0) == 0x1aau);
    command(state, &sdhc, 55u, 0u, 0u);
    command(state, &sdhc, 41u, 0x40ff8000u, 0u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_CMDRSP0) == 0xc0ff8000u);
    select_card(state, &sdhc);

    write_register(state, &sdhc, SDHC_BLKATTR, 16u | (1u << 16u));
    command(state, &sdhc, 17u, 1u, 0u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_DATPORT) == 0x03020100u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_DATPORT) == 0x07060504u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_DATPORT) == 0x0b0a0908u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_DATPORT) == 0x0f0e0d0cu);
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_IRQSTAT) & 2u) != 0u);

    command(state, &sdhc, 24u, 0u, 0u);
    write_register(state, &sdhc, SDHC_DATPORT, 0x11223344u);
    write_register(state, &sdhc, SDHC_DATPORT, 0x55667788u);
    write_register(state, &sdhc, SDHC_DATPORT, 0x99aabbccu);
    write_register(state, &sdhc, SDHC_DATPORT, 0xddeeff00u);
    uint32_t stored[4] = {0};
    TEST_EXPECT(state, k22_sdhc_read_card(&sdhc, 0u, stored, sizeof(stored)));
    TEST_EXPECT(state, stored[0] == 0x11223344u);
    TEST_EXPECT(state, stored[3] == 0xddeeff00u);

    write_register(state, &sdhc, SDHC_DSADDR, 0x100u);
    command(state, &sdhc, 17u, 1u, 1u);
    TEST_EXPECT(state, memcmp(memory.data + 0x100u, card + 512u, 16u) == 0);
    for (size_t index = 0u; index < 16u; index++)
        memory.data[0x200u + index] = (uint8_t)(0xf0u + index);
    write_register(state, &sdhc, SDHC_DSADDR, 0x200u);
    command(state, &sdhc, 24u, 1u, 1u);
    uint8_t written[16] = {0};
    TEST_EXPECT(state, k22_sdhc_read_card(&sdhc, 512u, written, sizeof(written)));
    TEST_EXPECT(state, memcmp(written, memory.data + 0x200u, sizeof(written)) == 0);

    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_HOSTVER) == 0x00001201u);
    TEST_EXPECT(state, !k22_sdhc_read(&sdhc, SDHC_BASE + 0x48u, 4u, stored));
    TEST_EXPECT(state, !k22_sdhc_read(&sdhc, SDHC_BASE, 1u, stored));
    k22_sdhc_destroy(&sdhc);
}

static void expect_errors_reset_and_copy(TestState* state) {
    BusMemory memory = {0};
    K22Sdhc sdhc = create_sdhc(state, &memory);
    command(state, &sdhc, 8u, 0x1aau, 0u);
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_IRQSTAT) & (1u << 16u)) != 0u);

    uint8_t card[512];
    memset(card, 0xa5, sizeof(card));
    TEST_EXPECT(state, k22_sdhc_insert(&sdhc, card, sizeof(card), true));
    select_card(state, &sdhc);
    write_register(state, &sdhc, SDHC_BLKATTR, 4u | (1u << 16u));
    command(state, &sdhc, 24u, 0u, 0u);
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_IRQSTAT) & (1u << 21u)) != 0u);
    command(state, &sdhc, 17u, 2u, 0u);
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_IRQSTAT) & (1u << 20u)) != 0u);

    K22Sdhc copy = create_sdhc(state, &memory);
    const K22SdhcBus bus = {&memory, bus_read, bus_write};
    TEST_EXPECT(state, k22_sdhc_copy(&copy, &sdhc, bus));
    k22_sdhc_eject(&sdhc);
    uint8_t value = 0u;
    TEST_EXPECT(state, k22_sdhc_read_card(&copy, 0u, &value, 1u));
    TEST_EXPECT(state, value == 0xa5u);
    k22_sdhc_reset(&copy);
    k22_sdhc_set_clock(&copy, true);
    TEST_EXPECT(state, (read_register(state, &copy, SDHC_PRSSTAT) & (1u << 16u)) != 0u);
    write_register(state, &copy, SDHC_SYSCTL, 1u << 24u);
    TEST_EXPECT(state, (read_register(state, &copy, SDHC_SYSCTL) & 0x07000000u) == 0u);
    k22_sdhc_destroy(&copy);
    k22_sdhc_destroy(&sdhc);
}

static void expect_command_and_transfer_lifecycle(TestState* state) {
    BusMemory memory = {0};
    K22Sdhc sdhc = create_sdhc(state, &memory);
    uint8_t card[1024];
    for (size_t index = 0u; index < sizeof(card); index++)
        card[index] = (uint8_t)(index ^ 0x5au);
    TEST_EXPECT(state, k22_sdhc_insert(&sdhc, card, sizeof(card), false));

    command(state, &sdhc, 2u, 0u, 0u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_CMDRSP0) == 0x03534453u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_CMDRSP1) == 0x44303447u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_CMDRSP2) == 0x80123456u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_CMDRSP3) == 0x78010001u);
    command(state, &sdhc, 9u, 0u, 0u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_CMDRSP0) == 0x400e0032u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_CMDRSP1) == 2u);

    select_card(state, &sdhc);
    command(state, &sdhc, 13u, 0u, 0u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_CMDRSP0) == (4u << 9u));
    command(state, &sdhc, 0u, 0u, 0u);
    command(state, &sdhc, 13u, 0u, 0u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_CMDRSP0) == (3u << 9u));
    select_card(state, &sdhc);

    command(state, &sdhc, 16u, 0u, 0u);
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_IRQSTAT) & (1u << 16u)) != 0u);
    write_register(state, &sdhc, SDHC_IRQSTAT, UINT32_MAX);
    command(state, &sdhc, 16u, 4097u, 0u);
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_IRQSTAT) & (1u << 16u)) != 0u);
    write_register(state, &sdhc, SDHC_IRQSTAT, UINT32_MAX);
    command(state, &sdhc, 16u, 8u, 0u);

    write_register(state, &sdhc, SDHC_BLKATTR, 2u << 16u);
    command(state, &sdhc, 18u, 0u, 0u);
    for (size_t offset = 0u; offset < 16u; offset += 4u) {
        uint32_t expected = 0u;
        memcpy(&expected, card + offset, sizeof(expected));
        TEST_EXPECT(state, read_register(state, &sdhc, SDHC_DATPORT) == expected);
    }
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_IRQSTAT) & 2u) != 0u);

    write_register(state, &sdhc, SDHC_BLKATTR, 8u | (2u << 16u));
    command(state, &sdhc, 25u, 1u, 0u);
    const uint32_t words[] = {0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f000u};
    for (size_t index = 0u; index < sizeof(words) / sizeof(words[0]); index++)
        write_register(state, &sdhc, SDHC_DATPORT, words[index]);
    uint32_t stored[4] = {0};
    TEST_EXPECT(state, k22_sdhc_read_card(&sdhc, 512u, stored, sizeof(stored)));
    TEST_EXPECT(state, memcmp(stored, words, sizeof(words)) == 0);

    write_register(state, &sdhc, SDHC_BLKATTR, 4u);
    command(state, &sdhc, 18u, 0u, 0u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_DATPORT) == 0x59585b5au);
    write_register(state, &sdhc, SDHC_BLKATTR, 8u | (2u << 16u));
    command(state, &sdhc, 18u, 0u, 0u);
    TEST_EXPECT(state, read_register(state, &sdhc, SDHC_DATPORT) == 0x59585b5au);
    command(state, &sdhc, 12u, 0u, 0u);
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_PRSSTAT) & (1u << 2u)) == 0u);

    write_register(state, &sdhc, SDHC_IRQSTAT, UINT32_MAX);
    command(state, &sdhc, 63u, 0u, 0u);
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_IRQSTAT) & (1u << 16u)) != 0u);
    write_register(state, &sdhc, SDHC_IRQSTAT, UINT32_MAX);
    write_register(state, &sdhc, SDHC_FEVT, (1u << 16u) | (1u << 21u));
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_IRQSTAT) &
                        ((1u << 16u) | (1u << 21u))) ==
                           ((1u << 16u) | (1u << 21u)));
    k22_sdhc_destroy(&sdhc);
}

static void expect_dma_failures(TestState* state) {
    BusMemory memory = {0};
    K22Sdhc sdhc = create_sdhc(state, &memory);
    uint8_t card[512];
    memset(card, 0x3c, sizeof(card));
    TEST_EXPECT(state, k22_sdhc_insert(&sdhc, card, sizeof(card), false));
    select_card(state, &sdhc);
    write_register(state, &sdhc, SDHC_BLKATTR, 4u | (1u << 16u));

    memory.fail_write = true;
    command(state, &sdhc, 17u, 0u, 1u);
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_IRQSTAT) & (1u << 20u)) != 0u);
    memory.fail_write = false;
    write_register(state, &sdhc, SDHC_IRQSTAT, UINT32_MAX);
    memory.fail_read = true;
    command(state, &sdhc, 24u, 0u, 1u);
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_IRQSTAT) & (1u << 20u)) != 0u);
    memory.fail_read = false;
    write_register(state, &sdhc, SDHC_IRQSTAT, UINT32_MAX);
    write_register(state, &sdhc, SDHC_DSADDR, sizeof(memory.data));
    command(state, &sdhc, 17u, 0u, 1u);
    TEST_EXPECT(state, (read_register(state, &sdhc, SDHC_IRQSTAT) & (1u << 20u)) != 0u);
    k22_sdhc_destroy(&sdhc);
}

static void expect_invalid_inputs(TestState* state) {
    BusMemory memory = {0};
    K22Sdhc sdhc;
    const K22SdhcBus bus = {&memory, bus_read, bus_write};
    TEST_EXPECT(state, !k22_sdhc_init(NULL, bus));
    TEST_EXPECT(state, !k22_sdhc_init(&sdhc, (K22SdhcBus){0}));
    TEST_EXPECT(state, k22_sdhc_init(&sdhc, bus));
    TEST_EXPECT(state, !k22_sdhc_insert(&sdhc, NULL, 512u, false));
    TEST_EXPECT(state, !k22_sdhc_insert(&sdhc, memory.data, 0u, false));
    TEST_EXPECT(state, !k22_sdhc_insert(&sdhc, memory.data, 513u, false));
    TEST_EXPECT(state, !k22_sdhc_read_card(&sdhc, 0u, memory.data, 1u));
    TEST_EXPECT(state, !k22_sdhc_irq(NULL));
    uint32_t value = 0u;
    TEST_EXPECT(state, !k22_sdhc_read(NULL, SDHC_BASE, 4u, &value));
    TEST_EXPECT(state, !k22_sdhc_read(&sdhc, SDHC_BASE, 4u, NULL));
    TEST_EXPECT(state, !k22_sdhc_write(NULL, SDHC_BASE, 4u, 0u));
    TEST_EXPECT(state, !k22_sdhc_read(&sdhc, SDHC_BASE, 4u, &value));
    TEST_EXPECT(state, !k22_sdhc_write(&sdhc, SDHC_BASE, 4u, 0u));
    k22_sdhc_set_clock(&sdhc, true);
    TEST_EXPECT(state, !k22_sdhc_read(&sdhc, SDHC_BASE - 4u, 4u, &value));
    TEST_EXPECT(state, !k22_sdhc_read(&sdhc, SDHC_BASE + 2u, 4u, &value));
    TEST_EXPECT(state, !k22_sdhc_read(&sdhc, SDHC_HOSTVER + 4u, 4u, &value));
    TEST_EXPECT(state, !k22_sdhc_write(&sdhc, SDHC_HOSTVER, 4u, 0u));
    k22_sdhc_destroy(&sdhc);
    k22_sdhc_destroy(NULL);
}

int main(void) {
    TestState state = {0};
    expect_initialization_and_access(&state);
    expect_errors_reset_and_copy(&state);
    expect_command_and_transfer_lifecycle(&state);
    expect_dma_failures(&state);
    expect_invalid_inputs(&state);
    return test_finish(&state);
}
