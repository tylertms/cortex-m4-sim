#include "cortex_m4_firmware_image.h"

#include <stdint.h>
#include <string.h>

#include "test.h"

enum {
    ELF_HEADER_SIZE = 52,
    ELF_PROGRAM_HEADER_SIZE = 32,
    IMAGE_SIZE = ELF_HEADER_SIZE + 2 * ELF_PROGRAM_HEADER_SIZE + 8,
    ELF_SECTION_SIZE = 40,
    SYMBOL_OFFSET = ELF_HEADER_SIZE + 3 * ELF_SECTION_SIZE,
    STRING_OFFSET = SYMBOL_OFFSET + 16,
    SYMBOL_IMAGE_SIZE = STRING_OFFSET + 6,
};

static void write16(uint8_t* data, size_t offset, uint16_t value) {
    data[offset] = (uint8_t)value;
    data[offset + 1] = (uint8_t)(value >> 8);
}

static void write32(uint8_t* data, size_t offset, uint32_t value) {
    data[offset] = (uint8_t)value;
    data[offset + 1] = (uint8_t)(value >> 8);
    data[offset + 2] = (uint8_t)(value >> 16);
    data[offset + 3] = (uint8_t)(value >> 24);
}

static void initialize_image(uint8_t* image) {
    memset(image, 0, IMAGE_SIZE);
    image[0] = 0x7f;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = 1;
    image[5] = 1;
    write16(image, 18, 40);
    write32(image, 24, 0x101u);
    write32(image, 28, ELF_HEADER_SIZE);
    write16(image, 42, ELF_PROGRAM_HEADER_SIZE);
    write16(image, 44, 2);

    size_t header = ELF_HEADER_SIZE;
    write32(image, header, 1);
    write32(image, header + 4, ELF_HEADER_SIZE + 2 * ELF_PROGRAM_HEADER_SIZE);
    write32(image, header + 8, 0x100u);
    write32(image, header + 16, 4);
    write32(image, header + 20, 4);

    header += ELF_PROGRAM_HEADER_SIZE;
    write32(image, header, 1);
    write32(image, header + 4, ELF_HEADER_SIZE + 2 * ELF_PROGRAM_HEADER_SIZE + 4);
    write32(image, header + 8, 0x20000000u);
    write32(image, header + 16, 4);
    write32(image, header + 20, 8);

    image[ELF_HEADER_SIZE + 2 * ELF_PROGRAM_HEADER_SIZE] = 0x00;
    image[ELF_HEADER_SIZE + 2 * ELF_PROGRAM_HEADER_SIZE + 1] = 0xbf;
    image[ELF_HEADER_SIZE + 2 * ELF_PROGRAM_HEADER_SIZE + 2] = 0x00;
    image[ELF_HEADER_SIZE + 2 * ELF_PROGRAM_HEADER_SIZE + 3] = 0xbe;
    image[ELF_HEADER_SIZE + 2 * ELF_PROGRAM_HEADER_SIZE + 4] = 0x78;
    image[ELF_HEADER_SIZE + 2 * ELF_PROGRAM_HEADER_SIZE + 5] = 0x56;
    image[ELF_HEADER_SIZE + 2 * ELF_PROGRAM_HEADER_SIZE + 6] = 0x34;
    image[ELF_HEADER_SIZE + 2 * ELF_PROGRAM_HEADER_SIZE + 7] = 0x12;
}

static void initialize_symbol_image(uint8_t* image) {
    memset(image, 0, SYMBOL_IMAGE_SIZE);
    image[0] = 0x7f;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = 1;
    image[5] = 1;
    write16(image, 18, 40);
    write32(image, 32, ELF_HEADER_SIZE);
    write16(image, 46, ELF_SECTION_SIZE);
    write16(image, 48, 3);

    size_t section = ELF_HEADER_SIZE + ELF_SECTION_SIZE;
    write32(image, section + 4, 2);
    write32(image, section + 16, SYMBOL_OFFSET);
    write32(image, section + 20, 16);
    write32(image, section + 24, 2);
    write32(image, section + 36, 16);

    section += ELF_SECTION_SIZE;
    write32(image, section + 16, STRING_OFFSET);
    write32(image, section + 20, 6);
    write32(image, SYMBOL_OFFSET, 1);
    write32(image, SYMBOL_OFFSET + 4, 0x12345678u);
    memcpy(image + STRING_OFFSET, "\0test", 6);
}

static void test_binary(TestState* state, KinetisK22* device) {
    const uint8_t image[] = {0x78, 0x56, 0x34, 0x12};
    uint32_t entry = UINT32_MAX;
    expect(state, cortex_m4_load_binary_data(device, image, sizeof(image), 0x20000010u, &entry),
           "cortex_m4_load_binary_data(device, image, sizeof(image), address, &entry)");
    expect(state, entry == 0x20000010u, "entry == 0x20000010u");
    uint32_t value = 0;
    expect(state, kinetis_k22_read(device, 0x20000010u, &value, sizeof(value)),
           "kinetis_k22_read(device, 0x20000010u, &value, sizeof(value))");
    expect(state, value == 0x12345678u, "value == 0x12345678u");
    expect(state, cortex_m4_load_binary_data(device, image, sizeof(image), 0x20000014u, NULL),
           "cortex_m4_load_binary_data(device, image, sizeof(image), address, NULL)");
    expect(state, !cortex_m4_load_binary_data(NULL, image, sizeof(image), 0u, NULL),
           "!cortex_m4_load_binary_data(NULL, image, sizeof(image), 0u, NULL)");
}

static void test_symbol(TestState* state) {
    uint8_t image[SYMBOL_IMAGE_SIZE];
    initialize_symbol_image(image);
    uint32_t address = UINT32_MAX;
    expect(state, cortex_m4_elf_symbol_data(image, sizeof(image), "test", &address),
           "cortex_m4_elf_symbol_data(image, sizeof(image), test, &address)");
    expect(state, address == 0x12345678u, "address == 0x12345678u");
    expect(state, !cortex_m4_elf_symbol_data(image, sizeof(image), "missing", &address),
           "!cortex_m4_elf_symbol_data(image, sizeof(image), missing, &address)");
    expect(state, !cortex_m4_elf_symbol_data(image, sizeof(image), NULL, &address),
           "!cortex_m4_elf_symbol_data(image, sizeof(image), NULL, &address)");
}

int main(void) {
    TestState state = {0};
    KinetisK22* device = kinetis_k22_create(kinetis_k22_default_configuration());
    expect(&state, device != NULL, "device != NULL");
    uint8_t image[IMAGE_SIZE];
    initialize_image(image);
    test_binary(&state, device);
    test_symbol(&state);

    uint32_t entry = 0;
    expect(&state, cortex_m4_load_elf_data(device, image, sizeof(image), &entry),
           "cortex_m4_load_elf_data(device, image, sizeof(image), &entry)");
    expect(&state, entry == 0x101u, "entry == 0x101u");
    uint32_t value = 0;
    expect(&state, kinetis_k22_read(device, 0x100u, &value, sizeof(value)),
           "kinetis_k22_read(device, 0x100u, &value, sizeof(value))");
    expect(&state, value == 0xbe00bf00u, "value == 0xbe00bf00u");
    expect(&state, kinetis_k22_read(device, 0x20000000u, &value, sizeof(value)),
           "kinetis_k22_read(device, 0x20000000u, &value, sizeof(value))");
    expect(&state, value == 0x12345678u, "value == 0x12345678u");
    expect(&state, kinetis_k22_read(device, 0x20000004u, &value, sizeof(value)),
           "kinetis_k22_read(device, 0x20000004u, &value, sizeof(value))");
    expect(&state, value == 0, "value == 0");

    image[0] = 0;
    expect(&state, !cortex_m4_load_elf_data(device, image, sizeof(image), NULL),
           "!cortex_m4_load_elf_data(device, image, sizeof(image), NULL)");
    initialize_image(image);
    write16(image, 18, 3);
    expect(&state, !cortex_m4_load_elf_data(device, image, sizeof(image), NULL),
           "!cortex_m4_load_elf_data(device, image, sizeof(image), NULL)");
    initialize_image(image);
    write32(image, ELF_HEADER_SIZE + 16, 5);
    write32(image, ELF_HEADER_SIZE + 20, 4);
    expect(&state, !cortex_m4_load_elf_data(device, image, sizeof(image), NULL),
           "!cortex_m4_load_elf_data(device, image, sizeof(image), NULL)");
    initialize_image(image);
    write32(image, 28, UINT32_MAX);
    expect(&state, !cortex_m4_load_elf_data(device, image, sizeof(image), NULL),
           "!cortex_m4_load_elf_data(device, image, sizeof(image), NULL)");

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
