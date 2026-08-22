#include <stdint.h>
#include <stdio.h>

#include "test.h"

int main(void) {
    const uint8_t image[] = {0x00u, 0x10u, 0x00u, 0x20u, 0x09u, 0x00u,
                             0x00u, 0x00u, 0x00u, 0xbfu, 0x00u, 0xbfu};
    TestState state = {0};
    FILE* file = fopen(KINETIS_K22_RUNNER_SMOKE_IMAGE, "wb");
    expect(&state, file != NULL, "open runner smoke image");
    if (file != NULL) {
        expect(&state, fwrite(image, 1u, sizeof(image), file) == sizeof(image),
               "write runner smoke image");
        expect(&state, fclose(file) == 0, "close runner smoke image");
    }
    return test_finish(&state);
}
