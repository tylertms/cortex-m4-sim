#include "kinetis_k22.h"

#include <stdint.h>

#include "architecture/cortex_m4/internal.h"
#include "test.h"

typedef struct {
    uint64_t examined;
    uint64_t permitted;
    uint64_t executed;
    uint64_t fingerprint;
} Census;

static uint64_t mix(uint64_t fingerprint, uint32_t value) {
    return (fingerprint ^ value) * UINT64_C(1099511628211);
}

static void prepare_cpu(CortexM4* cpu, uint8_t state) {
    static const uint32_t values[][4] = {
        {0u, 1u, 0x20000100u, 0x7fffffffu},
        {UINT32_MAX, 0x80000000u, 0x20000200u, 0x55555555u},
        {0xaaaaaaaau, 0x12345678u, 0x20000300u, 0x80000001u},
    };
    for (uint8_t index = 0u; index < CORTEX_M4_REGISTER_COUNT; index++) {
        cpu->registers[index] = values[state][index & 3u];
    }
    cpu->registers[13] = 0x20001000u;
    cpu->registers[15] = 0x100u;
    cpu->msp = 0x20001000u;
    cpu->psp = 0x20001800u;
    cpu->xpsr = CORTEX_M4_XPSR_T | (state == 1u ? UINT32_C(0xf0000000) : 0u);
    cpu->it_state = state == 2u ? 0x0cu : 0u;
    cpu->control = state == 2u ? CORTEX_M4_CONTROL_NPRIV : 0u;
    cpu->ccr = state == 1u ? 0x18u : 0u;
    cpu->primask = state == 1u;
    cpu->basepri = state == 2u ? 0x80u : 0u;
    cpu->faultmask = state == 2u;
    cpu->exclusive_valid = state == 2u;
    cpu->exclusive_address = 0x20000200u;
    cpu->exclusive_size = 4u;
    cpu->cpacr = state == 0u ? 0u : 0x00f00000u;
    cpu->fpscr = state == 2u ? 0x03c00000u : 0u;
    for (uint8_t index = 0u; index < CORTEX_M4_FP_REGISTER_COUNT; index++) {
        cpu->fp_registers[index] = values[state][index & 3u];
    }
    cpu->cfsr = 0u;
    cpu->hfsr = 0u;
    cpu->system_pending = 0u;
    cpu->stop = CORTEX_M4_STOP_RUNNING;
}

static void record(Census* census, CortexM4* cpu, bool executed, uint32_t encoding) {
    census->executed += executed;
    census->fingerprint = mix(census->fingerprint, encoding);
    census->fingerprint = mix(census->fingerprint, executed);
    census->fingerprint = mix(census->fingerprint, cpu->registers[0]);
    census->fingerprint = mix(census->fingerprint, cpu->registers[15]);
    census->fingerprint = mix(census->fingerprint, cpu->xpsr);
    census->fingerprint = mix(census->fingerprint, cpu->cfsr);
}

static Census census_thumb16(CortexM4* cpu) {
    Census census = {0u, 0u, 0u, UINT64_C(14695981039346656037)};
    for (uint8_t state = 0u; state < 3u; state++) {
        for (uint32_t opcode = 0u; opcode <= UINT16_MAX; opcode++) {
            prepare_cpu(cpu, state);
            census.examined++;
            if (cortex_m4_check_instruction_constraints(cpu, (uint16_t)opcode, 0u, false) !=
                CORTEX_M4_INSTRUCTION_EXECUTE) {
                continue;
            }
            census.permitted++;
            record(&census, cpu, cortex_m4_execute_thumb16(cpu, (uint16_t)opcode), opcode);
        }
    }
    return census;
}

static Census census_thumb32(CortexM4* cpu) {
    Census census = {0u, 0u, 0u, UINT64_C(14695981039346656037)};
    for (uint8_t state = 0u; state < 3u; state++) {
        for (uint32_t first = 0xe800u; first <= UINT16_MAX; first++) {
            for (uint16_t high = 0u; high <= UINT8_MAX; high++) {
                const uint16_t second = (uint16_t)((high << 8u) | ((first + high) & 0xffu));
                prepare_cpu(cpu, state);
                census.examined++;
                if (cortex_m4_check_instruction_constraints(cpu, (uint16_t)first, second, true) !=
                    CORTEX_M4_INSTRUCTION_EXECUTE) {
                    continue;
                }
                census.permitted++;
                record(&census, cpu, cortex_m4_execute_thumb32(cpu, (uint16_t)first, second),
                       (first << 16u) | second);
            }
        }
    }
    return census;
}

int main(void) {
    TestState state = {0};
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.flash_size = 4096u;
    configuration.sram_size = 65536u;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(&state, device != NULL, "device != NULL");
    const uint32_t vectors[2] = {0x20001000u, 0x101u};
    expect(&state, kinetis_k22_load(device, 0u, vectors, sizeof(vectors)),
           "kinetis_k22_load(device, 0u, vectors, sizeof(vectors))");
    expect(&state, kinetis_k22_reset(device), "kinetis_k22_reset(device)");
    CortexM4* cpu = kinetis_k22_cpu(device);
    const Census thumb16 = census_thumb16(cpu);
    const Census thumb32 = census_thumb32(cpu);
    expect(&state,
           thumb16.examined == 196608u && thumb16.permitted == 185351u &&
               thumb16.executed == 146023u && thumb16.fingerprint == UINT64_C(14300317076329787867),
           "thumb16 census matches");
    expect(&state,
           thumb32.examined == 4718592u && thumb32.permitted == 4147778u &&
               thumb32.executed == 1018875u && thumb32.fingerprint == UINT64_C(6564814519483446493),
           "thumb32 census matches");
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
