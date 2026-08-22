#include "kinetis_k22.h"

#include <stdint.h>

#include "test.h"

typedef struct {
    uint32_t addresses[8];
    uint32_t opcodes[8];
    bool executed[8];
    uint8_t count;
} TraceState;

static void capture_trace(void* context, uint32_t address, uint32_t opcode, bool executed) {
    TraceState* trace = context;
    if (trace->count < 8) {
        trace->addresses[trace->count] = address;
        trace->opcodes[trace->count] = opcode;
        trace->executed[trace->count] = executed;
        trace->count++;
    }
}

int main(void) {
    TestState state = {0};
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096;
    configuration.sram_size = 4096;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(&state, device != NULL, "device != NULL");

    const uint32_t vectors[2] = {0x20000100u, 0x00000101u};
    const uint16_t program[] = {0x2000u, 0x2801u, 0xbf08u, 0x3001u, 0xf240u, 0x0102u, 0xbe00u};
    expect(&state, kinetis_k22_load(device, 0, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0, vectors, sizeof(vectors))");
    expect(&state, kinetis_k22_load(device, 0x100, program, sizeof(program)),
           "kinetis_k22_load(device, 0x100, program, sizeof(program))");
    expect(&state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    test_connect_debugger(&state, kinetis_k22_cpu(device));

    TraceState trace = {0};
    cortex_m4_set_trace(kinetis_k22_cpu(device), capture_trace, &trace);
    const CortexM4Result result =
        cortex_m4_run(kinetis_k22_cpu(device), (CortexM4RunLimits){16, 32});
    expect(&state, result.stop == CORTEX_M4_STOP_BREAKPOINT,
           "result.stop == CORTEX_M4_STOP_BREAKPOINT");
    expect(&state, trace.count == 6, "trace.count == 6");
    expect(&state, trace.addresses[0] == 0x100u, "trace.addresses[0] == 0x100u");
    expect(&state, trace.addresses[3] == 0x106u, "trace.addresses[3] == 0x106u");
    expect(&state, trace.addresses[4] == 0x108u, "trace.addresses[4] == 0x108u");
    expect(&state, trace.opcodes[4] == 0xf2400102u, "trace.opcodes[4] == 0xf2400102u");
    expect(&state, !trace.executed[3], "!trace.executed[3]");
    expect(&state, trace.executed[4], "trace.executed[4]");
    expect(&state, cortex_m4_get_register(kinetis_k22_cpu(device), 0) == 0,
           "cortex_m4_get_register(kinetis_k22_cpu(device), 0) == 0");
    expect(&state, cortex_m4_get_register(kinetis_k22_cpu(device), 1) == 2,
           "cortex_m4_get_register(kinetis_k22_cpu(device), 1) == 2");

    cortex_m4_set_trace(kinetis_k22_cpu(device), NULL, NULL);
    expect(&state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    expect(&state, trace.count == 6, "trace.count == 6");

    kinetis_k22_destroy(device);
    return test_finish(&state);
}
