#include "architecture/cortex_m4/internal.h"

#include "test.h"

enum {
    ITM_PORT = 0xe0000000u,
    SYSTICK_CONTROL = 0xe000e010u,
    INTERRUPT_PRIORITY = 0xe000e400u,
    SYSTEM_PRIORITY = 0xe000ed18u,
    CONFIGURABLE_FAULT_STATUS = 0xe000ed28u,
    SOFTWARE_INTERRUPT = 0xe000ef00u,
};

typedef struct {
    uint32_t accepted;
    uint32_t rejected;
    uint32_t outside;
    uint64_t fingerprint;
} AccessCensus;

static bool bus_read(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                     uint32_t* value) {
    (void)context;
    (void)address;
    (void)size;
    (void)access;
    *value = 0u;
    return true;
}

static bool bus_write(void* context, uint32_t address, uint8_t size, CortexM4Access access,
                      uint32_t value) {
    (void)context;
    (void)address;
    (void)size;
    (void)access;
    (void)value;
    return true;
}

static void access_shape_cases(TestState* state, CortexM4* cpu) {
    uint32_t value = 0u;
    expect(state,
           cortex_m4_system_read(cpu, SYSTICK_CONTROL, 0u, CORTEX_M4_ACCESS_DEBUG, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "system access rejects a zero width");
    expect(state,
           cortex_m4_system_read(cpu, SYSTICK_CONTROL, 3u, CORTEX_M4_ACCESS_DEBUG, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "system access rejects a non-power-of-two width");
    expect(state,
           cortex_m4_system_read(cpu, SYSTICK_CONTROL + 1u, 2u, CORTEX_M4_ACCESS_DEBUG, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "system access rejects a misaligned halfword");
    expect(state,
           cortex_m4_system_read(cpu, SYSTICK_CONTROL + 1u, 4u, CORTEX_M4_ACCESS_DEBUG, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "system access rejects a misaligned word");
    expect(state,
           cortex_m4_system_write(cpu, SYSTEM_PRIORITY + 3u, 2u, CORTEX_M4_ACCESS_DEBUG, 0u) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "system access rejects a value crossing its register word");
    expect(state,
           cortex_m4_system_write(cpu, CONFIGURABLE_FAULT_STATUS + 2u, 2u, CORTEX_M4_ACCESS_DEBUG,
                                  UINT16_MAX) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "configurable fault status accepts an aligned halfword");
    expect(state,
           cortex_m4_system_write(cpu, INTERRUPT_PRIORITY + 1u, 1u, CORTEX_M4_ACCESS_DEBUG,
                                  UINT8_MAX) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "interrupt priority accepts a byte write");
}

static void privilege_cases(TestState* state, CortexM4* cpu) {
    uint32_t value = 0u;
    cpu->xpsr = CORTEX_M4_XPSR_T;
    cpu->control = CORTEX_M4_CONTROL_NPRIV;
    expect(state,
           cortex_m4_system_read(cpu, SYSTICK_CONTROL, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA,
                                 &value) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "unprivileged thread access rejects system control");

    cpu->xpsr |= 3u;
    expect(state,
           cortex_m4_system_read(cpu, SYSTICK_CONTROL, 4u, CORTEX_M4_ACCESS_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "handler data access is privileged");

    cpu->xpsr = CORTEX_M4_XPSR_T;
    cpu->ccr = 1u << 1u;
    expect(state,
           cortex_m4_system_write(cpu, SOFTWARE_INTERRUPT, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA,
                                  0u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "configured user software interrupt is accepted");

    cpu->debug.itm_trace_privilege = 1u;
    expect(state,
           cortex_m4_system_read(cpu, ITM_PORT + 0x20u, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA,
                                 &value) == CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "unprivileged trace access respects its port group");
}

static void record_access(AccessCensus* census, CortexM4SystemAccess result, uint32_t value) {
    census->accepted += result == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    census->rejected += result == CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    census->outside += result == CORTEX_M4_SYSTEM_ACCESS_OUTSIDE;
    census->fingerprint = (census->fingerprint ^ result) * UINT64_C(1099511628211);
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static void access_census(TestState* state, CortexM4* cpu) {
    static const uint32_t addresses[] = {
        0xdffffffcu, 0xe0000000u, 0xe0000080u, 0xe000e004u, 0xe000e010u, 0xe000e014u, 0xe000e018u,
        0xe000e01cu, 0xe000e100u, 0xe000e180u, 0xe000e200u, 0xe000e280u, 0xe000e300u, 0xe000e400u,
        0xe000ed00u, 0xe000ed04u, 0xe000ed08u, 0xe000ed0cu, 0xe000ed10u, 0xe000ed14u, 0xe000ed18u,
        0xe000ed24u, 0xe000ed28u, 0xe000ed2cu, 0xe000ed30u, 0xe000ed34u, 0xe000ed38u, 0xe000ed3cu,
        0xe000ed40u, 0xe000ed74u, 0xe000ed88u, 0xe000ef00u, 0xe000ef34u, 0xe000ef38u, 0xe000ef3cu,
        0xe000ef40u, 0xe000ef44u, 0xe000ef48u, 0xe0080000u, 0xe0100000u,
    };
    static const uint8_t sizes[] = {0u, 1u, 2u, 3u, 4u, 5u};
    AccessCensus census = {0u, 0u, 0u, UINT64_C(14695981039346656037)};

    cpu->debug.itm_trace_privilege = UINT8_MAX;
    cpu->ccr = 1u << 1u;
    for (size_t address_index = 0u; address_index < sizeof(addresses) / sizeof(addresses[0]);
         address_index++) {
        for (uint32_t offset = 0u; offset < 4u; offset++) {
            for (size_t size_index = 0u; size_index < sizeof(sizes) / sizeof(sizes[0]);
                 size_index++) {
                for (uint8_t access = CORTEX_M4_ACCESS_INSTRUCTION;
                     access <= CORTEX_M4_ACCESS_DEBUG; access++) {
                    uint32_t value = UINT32_C(0xa5a55a5a);
                    record_access(&census,
                                  cortex_m4_system_read(cpu, addresses[address_index] + offset,
                                                        sizes[size_index], (CortexM4Access)access,
                                                        &value),
                                  value);
                    record_access(&census,
                                  cortex_m4_system_write(cpu, addresses[address_index] + offset,
                                                         sizes[size_index], (CortexM4Access)access,
                                                         UINT32_C(0x5aa5a55a)),
                                  0u);
                }
            }
        }
    }
    expect(state,
           census.accepted == 134u && census.rejected == 6970u && census.outside == 576u &&
               census.fingerprint == UINT64_C(17579825366673452793),
           "system access census matches");
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    CortexM4Bus bus = {NULL, bus_read, bus_write, NULL, NULL};
    CortexM4* cpu = cortex_m4_create(bus);
    expect(&state, cpu != NULL, "create system access boundary processor");
    if (cpu != NULL) {
        expect(&state, cortex_m4_configure_implementation(cpu, 64u, 4u, 8u),
               "configure system access boundary processor");
        access_shape_cases(&state, cpu);
        privilege_cases(&state, cpu);
        access_census(&state, cpu);
        cortex_m4_destroy(cpu);
    }
    return test_finish(&state);
}
