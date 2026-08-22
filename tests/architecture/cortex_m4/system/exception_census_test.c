#include "architecture/cortex_m4/internal.h"

#include "test.h"

typedef struct {
    uint32_t samples;
    uint32_t true_results;
    uint64_t fingerprint;
} Census;

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

static void record(Census* census, bool value, uint32_t key) {
    census->samples++;
    census->true_results += value;
    census->fingerprint = (census->fingerprint ^ key) * UINT64_C(1099511628211);
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static void mask_and_preemption_census(CortexM4* cpu, Census* census) {
    static const uint8_t masks[] = {0u, 1u};
    static const uint8_t priorities[] = {0u, 0x20u, 0x80u, 0xe0u};
    static const uint16_t current_exceptions[] = {0u, 2u, 3u, 4u, 11u, 14u, 15u, 16u, 79u};

    for (size_t fault = 0u; fault < sizeof(masks) / sizeof(masks[0]); fault++) {
        for (size_t primary = 0u; primary < sizeof(masks) / sizeof(masks[0]); primary++) {
            for (size_t base = 0u; base < sizeof(priorities) / sizeof(priorities[0]); base++) {
                cpu->faultmask = masks[fault];
                cpu->primask = masks[primary];
                cpu->basepri = priorities[base];
                for (uint16_t exception = 0u; exception < 80u; exception++) {
                    cpu->irq_priority[exception < 16u ? 0u : exception - 16u] =
                        priorities[exception % (sizeof(priorities) / sizeof(priorities[0]))];
                    record(census, cortex_m4_system_exception_masked(cpu, exception), exception);
                    for (size_t current = 0u;
                         current < sizeof(current_exceptions) / sizeof(current_exceptions[0]);
                         current++) {
                        record(census,
                               cortex_m4_system_exception_can_preempt(cpu, exception,
                                                                      current_exceptions[current]),
                               ((uint32_t)exception << 16u) | current_exceptions[current]);
                    }
                }
            }
        }
    }
}

static void pending_and_order_census(CortexM4* cpu, Census* census) {
    for (uint16_t exception = 0u; exception <= 80u; exception++) {
        cortex_m4_system_set_pending(cpu, exception, true);
        bool pending = exception < 16u ? (cpu->system_pending & (1u << exception)) != 0u
                                       : (cpu->irq_pending[(exception - 16u) / 32u] &
                                          (1u << ((exception - 16u) & 31u))) != 0u;
        record(census, pending, exception);
        cortex_m4_system_set_pending(cpu, exception, false);
        pending = exception < 16u ? (cpu->system_pending & (1u << exception)) != 0u
                                  : (cpu->irq_pending[(exception - 16u) / 32u] &
                                     (1u << ((exception - 16u) & 31u))) != 0u;
        record(census, pending, exception | 0x10000u);
        for (uint16_t other = 0u; other <= 80u; other += 8u) {
            record(census, cortex_m4_system_exception_before(cpu, exception, other),
                   ((uint32_t)exception << 16u) | other);
        }
    }
}

static void return_census(CortexM4* cpu, Census* census) {
    static const uint32_t returns[] = {
        0u,          0xffffffe1u, 0xffffffe9u, 0xffffffedu,
        0xfffffff1u, 0xfffffff9u, 0xfffffffdu, UINT32_MAX,
    };
    for (uint8_t depth = 0u; depth < 4u; depth++) {
        cpu->exception_depth = depth;
        for (uint8_t thread = 0u; thread < 2u; thread++) {
            cpu->xpsr = CORTEX_M4_XPSR_T | (thread == 0u ? 3u : 0u);
            for (uint8_t nonbase = 0u; nonbase < 2u; nonbase++) {
                cpu->ccr = nonbase;
                for (size_t index = 0u; index < sizeof(returns) / sizeof(returns[0]); index++) {
                    record(census, cortex_m4_system_valid_exception_return(cpu, returns[index]),
                           returns[index] ^ depth ^ ((uint32_t)thread << 8u) ^
                               ((uint32_t)nonbase << 9u));
                }
            }
        }
    }
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    CortexM4Bus bus = {NULL, bus_read, bus_write, NULL, NULL};
    CortexM4* cpu = cortex_m4_create(bus);
    Census census = {0u, 0u, UINT64_C(14695981039346656037)};
    expect(&state, cpu != NULL, "create exception census processor");
    if (cpu != NULL) {
        expect(&state, cortex_m4_configure_implementation(cpu, 64u, 4u, 8u),
               "configure exception census processor");
        mask_and_preemption_census(cpu, &census);
        pending_and_order_census(cpu, &census);
        return_census(cpu, &census);
        expect(&state,
               census.samples == 13981u && census.true_results == 1995u &&
                   census.fingerprint == UINT64_C(15672790406690341878),
               "exception census matches");
        cortex_m4_destroy(cpu);
    }
    return test_finish(&state);
}
