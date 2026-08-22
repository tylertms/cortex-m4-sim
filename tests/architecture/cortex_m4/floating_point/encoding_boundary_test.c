#include "architecture/cortex_m4/internal.h"

#include <string.h>

#include "test.h"

static CortexM4 create_cpu(void) {
    CortexM4 cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.cpacr = UINT32_C(0x00f00000);
    cpu.xpsr = CORTEX_M4_XPSR_T;
    return cpu;
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    CortexM4 cpu = create_cpu();

    expect(&state, !cortex_m4_execute_fpu(&cpu, UINT16_C(0xed40), UINT16_C(0x0b00)),
           "scalar double register beyond D15 is rejected");
    expect(&state, !cortex_m4_execute_fpu(&cpu, UINT16_C(0xec00), UINT16_C(0x0a00)),
           "empty multiple transfer is rejected");
    expect(&state, !cortex_m4_execute_fpu(&cpu, UINT16_C(0xec00), UINT16_C(0x0b01)),
           "odd double multiple transfer is rejected");
    expect(&state, !cortex_m4_execute_fpu(&cpu, UINT16_C(0xec40), UINT16_C(0x0b30)),
           "core transfer beyond D15 is rejected");
    expect(&state, !cortex_m4_execute_fpu(&cpu, UINT16_C(0xeeb0), UINT16_C(0x0a10)),
           "unsupported FPU encoding is rejected");

    return test_finish(&state);
}
