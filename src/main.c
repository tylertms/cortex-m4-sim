#include "cortex_m4.h"
#include "kinetis_k22.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "firmware_image.h"

static bool parse_u64(const char* text, uint64_t* value) {
    char* end = NULL;
    errno = 0;
    const unsigned long long parsed = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *value = (uint64_t)parsed;
    return true;
}

static void print_usage(const char* program) {
    fprintf(stderr,
            "usage: %s IMAGE --vector-address ADDRESS "
            "[--binary-address ADDRESS] [--max-instructions COUNT] "
            "[--max-cycles COUNT] [--stop-address ADDRESS]\n",
            program);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    uint64_t vector_address = 0;
    uint64_t binary_address = 0;
    bool binary = false;
    uint64_t stop_address = 0;
    bool stop_address_set = false;
    CortexM4RunLimits limits = {1000000, 10000000};
    for (int index = 2; index < argc; index += 2) {
        if (index + 1 >= argc) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        uint64_t value = 0;
        if (!parse_u64(argv[index + 1], &value)) {
            fprintf(stderr, "invalid value: %s\n", argv[index + 1]);
            return EXIT_FAILURE;
        }
        if (strcmp(argv[index], "--vector-address") == 0) {
            vector_address = value;
        } else if (strcmp(argv[index], "--binary-address") == 0) {
            binary_address = value;
            binary = true;
        } else if (strcmp(argv[index], "--max-instructions") == 0) {
            limits.instruction_limit = value;
        } else if (strcmp(argv[index], "--max-cycles") == 0) {
            limits.cycle_limit = value;
        } else if (strcmp(argv[index], "--stop-address") == 0) {
            stop_address = value;
            stop_address_set = true;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[index]);
            return EXIT_FAILURE;
        }
    }
    if (vector_address > UINT32_MAX || binary_address > UINT32_MAX ||
        stop_address > UINT32_MAX) {
        fprintf(stderr, "an address is too large\n");
        return EXIT_FAILURE;
    }
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.vector_table_address = (uint32_t)vector_address;
    KinetisK22* device = kinetis_k22_create(configuration);
    if (device == NULL) {
        fprintf(stderr, "failed to create the device\n");
        return EXIT_FAILURE;
    }
    uint32_t entry = 0;
    const bool loaded =
        binary ? cortex_m4_load_binary(device, argv[1], (uint32_t)binary_address)
               : cortex_m4_load_elf(device, argv[1], &entry);
    if (!loaded) {
        fprintf(stderr, "failed to load the firmware image\n");
        kinetis_k22_destroy(device);
        return EXIT_FAILURE;
    }
    if (!kinetis_k22_reset(device)) {
        fprintf(stderr, "failed to reset the device\n");
        kinetis_k22_destroy(device);
        return EXIT_FAILURE;
    }
    if (stop_address_set && !cortex_m4_set_breakpoint(kinetis_k22_cpu(device), 0,
                                                      (uint32_t)stop_address, true)) {
        fprintf(stderr, "the stop address is invalid\n");
        kinetis_k22_destroy(device);
        return EXIT_FAILURE;
    }
    const CortexM4Result result = cortex_m4_run(kinetis_k22_cpu(device), limits);
    printf("stop=%u pc=0x%08" PRIx32 " opcode=0x%08" PRIx32 " instructions=%" PRIu64
           " cycles=%" PRIu64 " entry=0x%08" PRIx32 " cfsr=0x%08" PRIx32
           " bfar=0x%08" PRIx32 "\n",
           result.stop, result.pc, result.opcode, result.instructions, result.cycles, entry,
           cortex_m4_get_fault_status(kinetis_k22_cpu(device)),
           cortex_m4_get_fault_address(kinetis_k22_cpu(device)));
    for (uint8_t index = 0; index < 16; index++) {
        printf("r%u=0x%08" PRIx32 "%c", index,
               cortex_m4_get_register(kinetis_k22_cpu(device), index),
               index == 15 ? '\n' : ' ');
    }
    kinetis_k22_destroy(device);
    return result.stop == CORTEX_M4_STOP_UNSUPPORTED ||
                   result.stop == CORTEX_M4_STOP_BUS_FAULT ||
                   result.stop == CORTEX_M4_STOP_USAGE_FAULT ||
                   result.stop == CORTEX_M4_STOP_LOCKUP
               ? EXIT_FAILURE
               : EXIT_SUCCESS;
}
