#include "firmware_image.h"

#include <stdint.h>
#include <string.h>

#include "test.h"

enum {
    ELF_HEADER_SIZE = 52,
    ELF_PROGRAM_HEADER_SIZE = 32,
    IMAGE_SIZE = ELF_HEADER_SIZE + 2 * ELF_PROGRAM_HEADER_SIZE + 8,
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

int main(void) {
    TestState state = {0};
    KinetisK22* device = kinetis_k22_create(kinetis_k22_default_configuration());
    TEST_EXPECT(&state, device != NULL);
    uint8_t image[IMAGE_SIZE];
    initialize_image(image);

    uint32_t entry = 0;
    TEST_EXPECT(&state, cortex_m4_load_elf_data(device, image, sizeof(image), &entry));
    TEST_EXPECT(&state, entry == 0x101u);
    uint32_t value = 0;
    TEST_EXPECT(&state, kinetis_k22_read(device, 0x100u, &value, sizeof(value)));
    TEST_EXPECT(&state, value == 0xbe00bf00u);
    TEST_EXPECT(&state, kinetis_k22_read(device, 0x20000000u, &value, sizeof(value)));
    TEST_EXPECT(&state, value == 0x12345678u);
    TEST_EXPECT(&state, kinetis_k22_read(device, 0x20000004u, &value, sizeof(value)));
    TEST_EXPECT(&state, value == 0);

    image[0] = 0;
    TEST_EXPECT(&state, !cortex_m4_load_elf_data(device, image, sizeof(image), NULL));
    initialize_image(image);
    write16(image, 18, 3);
    TEST_EXPECT(&state, !cortex_m4_load_elf_data(device, image, sizeof(image), NULL));
    initialize_image(image);
    write32(image, ELF_HEADER_SIZE + 16, 5);
    write32(image, ELF_HEADER_SIZE + 20, 4);
    TEST_EXPECT(&state, !cortex_m4_load_elf_data(device, image, sizeof(image), NULL));
    initialize_image(image);
    write32(image, 28, UINT32_MAX);
    TEST_EXPECT(&state, !cortex_m4_load_elf_data(device, image, sizeof(image), NULL));

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
