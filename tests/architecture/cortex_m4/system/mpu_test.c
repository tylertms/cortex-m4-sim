#include "architecture/cortex_m4/internal.h"

#include <string.h>

#include "test.h"

#define MPU_TYPE 0xe000ed90u
#define MPU_CTRL 0xe000ed94u
#define MPU_RNR 0xe000ed98u
#define MPU_RBAR 0xe000ed9cu
#define MPU_RASR 0xe000eda0u
#define MPU_RBAR_A1 0xe000eda4u
#define MPU_RASR_A1 0xe000eda8u
#define MPU_RBAR_A2 0xe000edacu
#define MPU_RASR_A2 0xe000edb0u
#define MPU_RBAR_A3 0xe000edb4u
#define MPU_RASR_A3 0xe000edb8u

#define MPU_ENABLE 0x1u
#define MPU_HFNMIENA 0x2u
#define MPU_PRIVDEFENA 0x4u

static uint32_t raised_faults;
static uint8_t last_fault;

void cortex_m4_raise_fault(CortexM4* cpu, uint8_t exception) {
    raised_faults++;
    last_fault = exception;
    if (cpu != NULL) {
        cpu->system_pending |= 1u << exception;
    }
}

static CortexM4 create_cpu(void) {
    CortexM4 cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.external_irq_count = CORTEX_M4_IRQ_COUNT;
    cpu.priority_bits = 8u;
    cpu.mpu_region_count = CORTEX_M4_MPU_REGION_COUNT;
    cpu.xpsr = CORTEX_M4_XPSR_T;
    cortex_m4_mpu_reset(&cpu);
    raised_faults = 0u;
    last_fault = 0u;
    return cpu;
}

static uint32_t rasr(uint8_t size_encoding, uint8_t permission, bool execute_never,
                     uint8_t subregions) {
    return (execute_never ? 1u << 28 : 0u) | ((uint32_t)permission << 24u) |
           ((uint32_t)subregions << 8u) | ((uint32_t)size_encoding << 1u) | 1u;
}

static void set_region(CortexM4* cpu, uint8_t region, uint32_t base, uint32_t attributes) {
    cpu->mpu_region_base[region] = base & 0xffffffe0u;
    cpu->mpu_region_attributes[region] = attributes;
}

static uint32_t read_register(TestState* state, CortexM4* cpu, uint32_t address,
                              CortexM4Access access) {
    uint32_t value = 0xdeadbeefu;
    expect(state,
           cortex_m4_mpu_read(cpu, address, 4u, access, &value) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_mpu_read(cpu, address, 4u, access, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    return value;
}

static void test_reset_and_copy(TestState* state) {
    cortex_m4_mpu_reset(NULL);
    CortexM4 cpu;
    memset(&cpu, 0xff, sizeof(cpu));
    cpu.mpu_region_count = CORTEX_M4_MPU_REGION_COUNT;
    cortex_m4_mpu_reset(&cpu);
    expect(state, cpu.mpu_control == 0u, "cpu.mpu_control == 0u");
    expect(state, cpu.mpu_region_number == 0u, "cpu.mpu_region_number == 0u");
    for (uint8_t region = 0u; region < CORTEX_M4_MPU_REGION_COUNT; region++) {
        expect(state, cpu.mpu_region_base[region] == 0u, "cpu.mpu_region_base[region] == 0u");
        expect(state, cpu.mpu_region_attributes[region] == 0u,
               "cpu.mpu_region_attributes[region] == 0u");
    }
    cpu.mpu_control = 7u;
    cpu.mpu_region_number = 6u;
    set_region(&cpu, 6u, 0x456789a0u, rasr(11u, 3u, true, 0xa5u));
    CortexM4 copy = cpu;
    expect(state, copy.mpu_control == cpu.mpu_control, "copy.mpu_control == cpu.mpu_control");
    expect(state, copy.mpu_region_number == cpu.mpu_region_number,
           "copy.mpu_region_number == cpu.mpu_region_number");
    expect(state,
           memcmp(copy.mpu_region_base, cpu.mpu_region_base, sizeof(cpu.mpu_region_base)) == 0,
           "memcmp(copy.mpu_region_base, cpu.mpu_region_base, sizeof(cpu.mpu_region_base)) "
           "== 0");
    expect(state,
           memcmp(copy.mpu_region_attributes, cpu.mpu_region_attributes,
                  sizeof(cpu.mpu_region_attributes)) == 0,
           "memcmp(copy.mpu_region_attributes, cpu.mpu_region_attributes, "
           "sizeof(cpu.mpu_region_attributes)) == 0");
}

static void test_register_access(TestState* state) {
    CortexM4 cpu = create_cpu();
    expect(state, read_register(state, &cpu, MPU_TYPE, CORTEX_M4_ACCESS_DATA) == 0x00000800u,
           "read_register(state, &cpu, MPU_TYPE, CORTEX_M4_ACCESS_DATA) == 0x00000800u");
    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_TYPE, 4u, CORTEX_M4_ACCESS_DATA, 0xffffffffu) ==
               CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_mpu_write(&cpu, MPU_TYPE, 4u, CORTEX_M4_ACCESS_DATA, 0xffffffffu) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, read_register(state, &cpu, MPU_TYPE, CORTEX_M4_ACCESS_DATA) == 0x00000800u,
           "read_register(state, &cpu, MPU_TYPE, CORTEX_M4_ACCESS_DATA) == 0x00000800u");
    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_CTRL, 4u, CORTEX_M4_ACCESS_DATA, 0xffffffffu) ==
               CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_mpu_write(&cpu, MPU_CTRL, 4u, CORTEX_M4_ACCESS_DATA, 0xffffffffu) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, read_register(state, &cpu, MPU_CTRL, CORTEX_M4_ACCESS_DATA) == 7u,
           "read_register(state, &cpu, MPU_CTRL, CORTEX_M4_ACCESS_DATA) == 7u");
    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_RNR, 4u, CORTEX_M4_ACCESS_DATA, 0xffffffffu) ==
               CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_mpu_write(&cpu, MPU_RNR, 4u, CORTEX_M4_ACCESS_DATA, 0xffffffffu) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, read_register(state, &cpu, MPU_RNR, CORTEX_M4_ACCESS_DATA) == 7u,
           "read_register(state, &cpu, MPU_RNR, CORTEX_M4_ACCESS_DATA) == 7u");

    uint32_t value = 0u;
    expect(state,
           cortex_m4_mpu_read(&cpu, MPU_TYPE - 4u, 4u, CORTEX_M4_ACCESS_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_OUTSIDE,
           "cortex_m4_mpu_read(&cpu, MPU_TYPE - 4u, 4u, CORTEX_M4_ACCESS_DATA, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_OUTSIDE");
    expect(state,
           cortex_m4_mpu_read(&cpu, MPU_RASR_A3 + 4u, 4u, CORTEX_M4_ACCESS_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_OUTSIDE,
           "cortex_m4_mpu_read(&cpu, MPU_RASR_A3 + 4u, 4u, CORTEX_M4_ACCESS_DATA, &value) "
           "== CORTEX_M4_SYSTEM_ACCESS_OUTSIDE");
    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_TYPE - 4u, 4u, CORTEX_M4_ACCESS_DATA, 0u) ==
               CORTEX_M4_SYSTEM_ACCESS_OUTSIDE,
           "cortex_m4_mpu_write(&cpu, MPU_TYPE - 4u, 4u, CORTEX_M4_ACCESS_DATA, 0u) == "
           "CORTEX_M4_SYSTEM_ACCESS_OUTSIDE");
    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_RASR_A3 + 4u, 4u, CORTEX_M4_ACCESS_DATA, 0u) ==
               CORTEX_M4_SYSTEM_ACCESS_OUTSIDE,
           "cortex_m4_mpu_write(&cpu, MPU_RASR_A3 + 4u, 4u, CORTEX_M4_ACCESS_DATA, 0u) == "
           "CORTEX_M4_SYSTEM_ACCESS_OUTSIDE");
    expect(state,
           cortex_m4_mpu_read(NULL, MPU_TYPE, 4u, CORTEX_M4_ACCESS_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_mpu_read(NULL, MPU_TYPE, 4u, CORTEX_M4_ACCESS_DATA, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_mpu_read(&cpu, MPU_TYPE, 4u, CORTEX_M4_ACCESS_DATA, NULL) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_mpu_read(&cpu, MPU_TYPE, 4u, CORTEX_M4_ACCESS_DATA, NULL) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_mpu_write(NULL, MPU_TYPE, 4u, CORTEX_M4_ACCESS_DATA, 0u) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_mpu_write(NULL, MPU_TYPE, 4u, CORTEX_M4_ACCESS_DATA, 0u) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_mpu_read(&cpu, MPU_TYPE, 1u, CORTEX_M4_ACCESS_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_mpu_read(&cpu, MPU_TYPE, 1u, CORTEX_M4_ACCESS_DATA, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_TYPE, 1u, CORTEX_M4_ACCESS_DATA, 0u) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_mpu_write(&cpu, MPU_TYPE, 1u, CORTEX_M4_ACCESS_DATA, 0u) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_mpu_read(&cpu, MPU_TYPE + 2u, 4u, CORTEX_M4_ACCESS_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_mpu_read(&cpu, MPU_TYPE + 2u, 4u, CORTEX_M4_ACCESS_DATA, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_mpu_read(&cpu, MPU_TYPE, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_mpu_read(&cpu, MPU_TYPE, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, "
           "&value) == CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    cpu.control = CORTEX_M4_CONTROL_NPRIV;
    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_CTRL, 4u, CORTEX_M4_ACCESS_DATA, 0u) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_mpu_write(&cpu, MPU_CTRL, 4u, CORTEX_M4_ACCESS_DATA, 0u) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_mpu_read(&cpu, MPU_TYPE, 4u, CORTEX_M4_ACCESS_DEBUG, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_mpu_read(&cpu, MPU_TYPE, 4u, CORTEX_M4_ACCESS_DEBUG, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    cpu.xpsr = CORTEX_M4_XPSR_T | 11u;
    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_CTRL, 4u, CORTEX_M4_ACCESS_DATA, 5u) ==
               CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_mpu_write(&cpu, MPU_CTRL, 4u, CORTEX_M4_ACCESS_DATA, 5u) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
}

static void test_absent_mpu(TestState* state) {
    CortexM4 cpu = create_cpu();
    cpu.mpu_region_count = 0u;
    cortex_m4_mpu_reset(&cpu);
    uint32_t value = UINT32_MAX;
    expect(state,
           cortex_m4_mpu_read(&cpu, MPU_TYPE, 4u, CORTEX_M4_ACCESS_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_mpu_read(&cpu, MPU_TYPE, 4u, CORTEX_M4_ACCESS_DATA, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, value == 0u, "value == 0u");
    expect(state,
           cortex_m4_mpu_read(&cpu, MPU_CTRL, 4u, CORTEX_M4_ACCESS_DATA, &value) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_mpu_read(&cpu, MPU_CTRL, 4u, CORTEX_M4_ACCESS_DATA, &value) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_CTRL, 4u, CORTEX_M4_ACCESS_DATA, 1u) ==
               CORTEX_M4_SYSTEM_ACCESS_REJECTED,
           "cortex_m4_mpu_write(&cpu, MPU_CTRL, 4u, CORTEX_M4_ACCESS_DATA, 1u) == "
           "CORTEX_M4_SYSTEM_ACCESS_REJECTED");
}

static void test_region_registers(TestState* state) {
    CortexM4 cpu = create_cpu();
    for (uint8_t region = 0u; region < CORTEX_M4_MPU_REGION_COUNT; region++) {
        const uint32_t base = 0x10000000u + (uint32_t)region * 0x1000u;
        const uint32_t attributes =
            rasr((uint8_t)(7u + region), region & 7u, (region & 1u) != 0u, (uint8_t)(region * 17u));
        expect(state,
               cortex_m4_mpu_write(&cpu, MPU_RBAR, 4u, CORTEX_M4_ACCESS_DATA,
                                   base | 0x10u | region) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
               "cortex_m4_mpu_write(&cpu, MPU_RBAR, 4u, CORTEX_M4_ACCESS_DATA, base | "
               "0x10u | region) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
        expect(state, cpu.mpu_region_number == region, "cpu.mpu_region_number == region");
        expect(state,
               cortex_m4_mpu_write(&cpu, MPU_RASR, 4u, CORTEX_M4_ACCESS_DATA, attributes) ==
                   CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
               "cortex_m4_mpu_write(&cpu, MPU_RASR, 4u, CORTEX_M4_ACCESS_DATA, attributes) "
               "== CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    }
    for (uint8_t region = 0u; region < CORTEX_M4_MPU_REGION_COUNT; region++) {
        expect(state,
               cortex_m4_mpu_write(&cpu, MPU_RNR, 4u, CORTEX_M4_ACCESS_DATA, region) ==
                   CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
               "cortex_m4_mpu_write(&cpu, MPU_RNR, 4u, CORTEX_M4_ACCESS_DATA, region) == "
               "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
        expect(state, (read_register(state, &cpu, MPU_RBAR, CORTEX_M4_ACCESS_DATA) & 7u) == region,
               "(read_register(state, &cpu, MPU_RBAR, CORTEX_M4_ACCESS_DATA) & 7u) == region");
        expect(state,
               (read_register(state, &cpu, MPU_RBAR, CORTEX_M4_ACCESS_DATA) & 0xffffffe0u) ==
                   cpu.mpu_region_base[region],
               "(read_register(state, &cpu, MPU_RBAR, CORTEX_M4_ACCESS_DATA) & "
               "0xffffffe0u) == cpu.mpu_region_base[region]");
        expect(state,
               read_register(state, &cpu, MPU_RASR, CORTEX_M4_ACCESS_DATA) ==
                   cpu.mpu_region_attributes[region],
               "read_register(state, &cpu, MPU_RASR, CORTEX_M4_ACCESS_DATA) == "
               "cpu.mpu_region_attributes[region]");
    }

    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_RNR, 4u, CORTEX_M4_ACCESS_DATA, 4u) ==
               CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_mpu_write(&cpu, MPU_RNR, 4u, CORTEX_M4_ACCESS_DATA, 4u) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    const uint32_t rbar_aliases[] = {MPU_RBAR_A1, MPU_RBAR_A2, MPU_RBAR_A3};
    const uint32_t rasr_aliases[] = {MPU_RASR_A1, MPU_RASR_A2, MPU_RASR_A3};
    for (uint8_t alias = 1u; alias <= 3u; alias++) {
        const uint8_t region = (uint8_t)(4u | alias);
        const uint32_t base = 0x30000000u + (uint32_t)alias * 0x2000u;
        const uint32_t attributes = rasr(12u, alias, false, 0u);
        expect(state,
               cortex_m4_mpu_write(&cpu, rbar_aliases[alias - 1u], 4u, CORTEX_M4_ACCESS_DATA,
                                   base | 0x0fu) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
               "cortex_m4_mpu_write(&cpu, rbar_aliases[alias - 1u], 4u, "
               "CORTEX_M4_ACCESS_DATA, base | 0x0fu) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
        expect(state,
               cortex_m4_mpu_write(&cpu, rasr_aliases[alias - 1u], 4u, CORTEX_M4_ACCESS_DATA,
                                   attributes) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
               "cortex_m4_mpu_write(&cpu, rasr_aliases[alias - 1u], 4u, "
               "CORTEX_M4_ACCESS_DATA, attributes) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
        expect(state, cpu.mpu_region_base[region] == (base & 0xffffffe0u),
               "cpu.mpu_region_base[region] == (base & 0xffffffe0u)");
        expect(state, cpu.mpu_region_attributes[region] == attributes,
               "cpu.mpu_region_attributes[region] == attributes");
        expect(state,
               (read_register(state, &cpu, rbar_aliases[alias - 1u], CORTEX_M4_ACCESS_DATA) & 7u) ==
                   region,
               "(read_register(state, &cpu, rbar_aliases[alias - 1u], CORTEX_M4_ACCESS_DATA) "
               "& 7u) == region");
        expect(state,
               read_register(state, &cpu, rasr_aliases[alias - 1u], CORTEX_M4_ACCESS_DATA) ==
                   attributes,
               "read_register(state, &cpu, rasr_aliases[alias - 1u], "
               "CORTEX_M4_ACCESS_DATA) == attributes");
    }
    expect(state, cpu.mpu_region_number == 4u, "cpu.mpu_region_number == 4u");

    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_RBAR_A1, 4u, CORTEX_M4_ACCESS_DATA,
                               0x44444000u | 0x10u | 2u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_mpu_write(&cpu, MPU_RBAR_A1, 4u, CORTEX_M4_ACCESS_DATA, 0x44444000u "
           "| 0x10u | 2u) == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, cpu.mpu_region_number == 2u, "cpu.mpu_region_number == 2u");
    expect(state, cpu.mpu_region_base[2] == 0x44444000u, "cpu.mpu_region_base[2] == 0x44444000u");

    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_RNR, 4u, CORTEX_M4_ACCESS_DATA, 3u) ==
               CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_mpu_write(&cpu, MPU_RNR, 4u, CORTEX_M4_ACCESS_DATA, 3u) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_RBAR_A1, 4u, CORTEX_M4_ACCESS_DATA, 0x5555554fu) ==
               CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_mpu_write(&cpu, MPU_RBAR_A1, 4u, CORTEX_M4_ACCESS_DATA, 0x5555554fu) "
           "== CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, cpu.mpu_region_base[1] == 0x55555540u, "cpu.mpu_region_base[1] == 0x55555540u");
    expect(state, cpu.mpu_region_number == 3u, "cpu.mpu_region_number == 3u");

    const uint32_t unmasked = 0xffffffffu;
    expect(state,
           cortex_m4_mpu_write(&cpu, MPU_RASR, 4u, CORTEX_M4_ACCESS_DATA, unmasked) ==
               CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
           "cortex_m4_mpu_write(&cpu, MPU_RASR, 4u, CORTEX_M4_ACCESS_DATA, unmasked) == "
           "CORTEX_M4_SYSTEM_ACCESS_ACCEPTED");
    expect(state, cpu.mpu_region_attributes[3] == 0x173fff3fu,
           "cpu.mpu_region_attributes[3] == 0x173fff3fu");
}

static void test_region_sizes_and_subregions(TestState* state) {
    CortexM4 cpu = create_cpu();
    cpu.mpu_control = MPU_ENABLE;
    set_region(&cpu, 0u, 0x20000000u, rasr(3u, 3u, false, 0u));
    for (uint32_t offset = 0u; offset < 64u; offset++) {
        expect(state,
               !cortex_m4_mpu_access_permitted(&cpu, 0x20000000u + offset, 1u,
                                               CORTEX_M4_ACCESS_DATA, false),
               "!cortex_m4_mpu_access_permitted(&cpu, 0x20000000u + offset, 1u, "
               "CORTEX_M4_ACCESS_DATA, false)");
    }
    set_region(&cpu, 0u, 0x2000001fu, rasr(4u, 3u, false, 0xffu));
    for (uint32_t offset = 0u; offset < 32u; offset++) {
        expect(state,
               cortex_m4_mpu_access_permitted(&cpu, 0x20000000u + offset, 1u, CORTEX_M4_ACCESS_DATA,
                                              false),
               "cortex_m4_mpu_access_permitted(&cpu, 0x20000000u + offset, 1u, "
               "CORTEX_M4_ACCESS_DATA, false)");
    }
    expect(state,
           !cortex_m4_mpu_access_permitted(&cpu, 0x20000020u, 1u, CORTEX_M4_ACCESS_DATA, false),
           "!cortex_m4_mpu_access_permitted(&cpu, 0x20000020u, 1u, CORTEX_M4_ACCESS_DATA, "
           "false)");

    set_region(&cpu, 0u, 0x200001e0u, rasr(7u, 3u, false, 0x24u));
    for (uint8_t subregion = 0u; subregion < 8u; subregion++) {
        for (uint8_t offset = 0u; offset < 32u; offset++) {
            const bool enabled = subregion != 2u && subregion != 5u;
            expect(state,
                   cortex_m4_mpu_access_permitted(&cpu,
                                                  0x20000100u + (uint32_t)subregion * 32u + offset,
                                                  1u, CORTEX_M4_ACCESS_DATA, false) == enabled,
                   "cortex_m4_mpu_access_permitted( &cpu, 0x20000100u + (uint32_t)subregion * "
                   "32u + offset, 1u, CORTEX_M4_ACCESS_DATA, false) == enabled");
        }
    }
    expect(state,
           !cortex_m4_mpu_access_permitted(&cpu, 0x2000013fu, 2u, CORTEX_M4_ACCESS_DATA, false),
           "!cortex_m4_mpu_access_permitted(&cpu, 0x2000013fu, 2u, CORTEX_M4_ACCESS_DATA, "
           "false)");
    expect(state,
           cortex_m4_mpu_access_permitted(&cpu, 0x2000011fu, 2u, CORTEX_M4_ACCESS_DATA, false),
           "cortex_m4_mpu_access_permitted(&cpu, 0x2000011fu, 2u, CORTEX_M4_ACCESS_DATA, "
           "false)");

    set_region(&cpu, 7u, 0xffffffe0u, rasr(31u, 3u, false, 0u));
    expect(state,
           cortex_m4_mpu_access_permitted(&cpu, 0x00000000u, 4u, CORTEX_M4_ACCESS_DATA, true),
           "cortex_m4_mpu_access_permitted(&cpu, 0x00000000u, 4u, CORTEX_M4_ACCESS_DATA, "
           "true)");
    expect(state,
           cortex_m4_mpu_access_permitted(&cpu, 0xfffffffcu, 4u, CORTEX_M4_ACCESS_DATA, true),
           "cortex_m4_mpu_access_permitted(&cpu, 0xfffffffcu, 4u, CORTEX_M4_ACCESS_DATA, "
           "true)");
    expect(state,
           !cortex_m4_mpu_access_permitted(&cpu, 0xfffffffeu, 4u, CORTEX_M4_ACCESS_DATA, true),
           "!cortex_m4_mpu_access_permitted(&cpu, 0xfffffffeu, 4u, CORTEX_M4_ACCESS_DATA, "
           "true)");
}

static void test_all_region_sizes(TestState* state) {
    CortexM4 cpu = create_cpu();
    cpu.mpu_control = MPU_ENABLE;
    for (uint8_t encoding = 4u; encoding <= 30u; encoding++) {
        const uint64_t size = UINT64_C(1) << (encoding + 1u);
        const uint64_t mask = size - 1u;
        const uint32_t written_base = 0xfedcba80u;
        const uint32_t base = (uint32_t)((uint64_t)written_base & ~mask);
        const uint32_t end = (uint32_t)((uint64_t)base + size - 1u);
        set_region(&cpu, 0u, written_base, rasr(encoding, 3u, false, 0u));
        for (uint8_t access_size = 1u; access_size <= 4u; access_size *= 2u) {
            expect(state,
                   cortex_m4_mpu_access_permitted(&cpu, base, access_size, CORTEX_M4_ACCESS_DATA,
                                                  true),
                   "cortex_m4_mpu_access_permitted(&cpu, base, access_size, "
                   "CORTEX_M4_ACCESS_DATA, true)");
            expect(state,
                   cortex_m4_mpu_access_permitted(&cpu, end - access_size + 1u, access_size,
                                                  CORTEX_M4_ACCESS_DATA, true),
                   "cortex_m4_mpu_access_permitted(&cpu, end - access_size + 1u, "
                   "access_size, CORTEX_M4_ACCESS_DATA, true)");
        }
        if (base != 0u) {
            expect(
                state,
                !cortex_m4_mpu_access_permitted(&cpu, base - 1u, 1u, CORTEX_M4_ACCESS_DATA, false),
                "!cortex_m4_mpu_access_permitted( &cpu, base - 1u, 1u, "
                "CORTEX_M4_ACCESS_DATA, false)");
        }
        if (end != UINT32_MAX) {
            expect(
                state,
                !cortex_m4_mpu_access_permitted(&cpu, end + 1u, 1u, CORTEX_M4_ACCESS_DATA, false),
                "!cortex_m4_mpu_access_permitted( &cpu, end + 1u, 1u, "
                "CORTEX_M4_ACCESS_DATA, false)");
        }
    }
}

static bool expected_permission(uint8_t permission, bool privileged, bool write) {
    switch (permission) {
    case 1u:
        return privileged;
    case 2u:
        return privileged || !write;
    case 3u:
        return true;
    case 5u:
        return privileged && !write;
    case 6u:
    case 7u:
        return !write;
    default:
        return false;
    }
}

static void test_permissions(TestState* state) {
    CortexM4 cpu = create_cpu();
    cpu.mpu_control = MPU_ENABLE;
    for (uint8_t permission = 0u; permission < 8u; permission++) {
        set_region(&cpu, 0u, 0x20000000u, rasr(7u, permission, false, 0u));
        for (uint8_t privilege = 0u; privilege < 2u; privilege++) {
            cpu.control = privilege == 0u ? CORTEX_M4_CONTROL_NPRIV : 0u;
            const bool privileged = privilege != 0u;
            for (uint8_t write = 0u; write < 2u; write++) {
                const bool expected = expected_permission(permission, privileged, write != 0u);
                expect(state,
                       cortex_m4_mpu_access_permitted(&cpu, 0x20000080u, 4u, CORTEX_M4_ACCESS_DATA,
                                                      write != 0u) == expected,
                       "cortex_m4_mpu_access_permitted(&cpu, 0x20000080u, 4u, "
                       "CORTEX_M4_ACCESS_DATA, write != 0u) == expected");
                expect(state,
                       cortex_m4_mpu_access_permitted(
                           &cpu, 0x20000080u, 4u, CORTEX_M4_ACCESS_UNPRIVILEGED_DATA,
                           write != 0u) == expected_permission(permission, false, write != 0u),
                       "cortex_m4_mpu_access_permitted( &cpu, 0x20000080u, 4u, "
                       "CORTEX_M4_ACCESS_UNPRIVILEGED_DATA, write != 0u) == "
                       "expected_permission(permission, false, write != 0u)");
            }
            expect(state,
                   cortex_m4_mpu_access_permitted(&cpu, 0x20000080u, 2u,
                                                  CORTEX_M4_ACCESS_INSTRUCTION, false) ==
                       expected_permission(permission, privileged, false),
                   "cortex_m4_mpu_access_permitted(&cpu, 0x20000080u, 2u, "
                   "CORTEX_M4_ACCESS_INSTRUCTION, false) == "
                   "expected_permission(permission, privileged, false)");
        }
        cpu.xpsr = CORTEX_M4_XPSR_T | 16u;
        cpu.control = CORTEX_M4_CONTROL_NPRIV;
        expect(state,
               cortex_m4_mpu_access_permitted(&cpu, 0x20000080u, 4u, CORTEX_M4_ACCESS_DATA, true) ==
                   expected_permission(permission, true, true),
               "cortex_m4_mpu_access_permitted(&cpu, 0x20000080u, 4u, CORTEX_M4_ACCESS_DATA, "
               "true) == expected_permission(permission, true, true)");
        cpu.xpsr = CORTEX_M4_XPSR_T;
    }
    set_region(&cpu, 0u, 0x20000000u, rasr(7u, 3u, true, 0u));
    cpu.control = 0u;
    expect(state,
           cortex_m4_mpu_access_permitted(&cpu, 0x20000080u, 4u, CORTEX_M4_ACCESS_DATA, false),
           "cortex_m4_mpu_access_permitted(&cpu, 0x20000080u, 4u, CORTEX_M4_ACCESS_DATA, "
           "false)");
    expect(
        state,
        !cortex_m4_mpu_access_permitted(&cpu, 0x20000080u, 2u, CORTEX_M4_ACCESS_INSTRUCTION, false),
        "!cortex_m4_mpu_access_permitted( &cpu, 0x20000080u, 2u, "
        "CORTEX_M4_ACCESS_INSTRUCTION, false)");
    expect(state,
           cortex_m4_mpu_access_permitted(&cpu, 0x20000080u, 2u, CORTEX_M4_ACCESS_DEBUG, true),
           "cortex_m4_mpu_access_permitted(&cpu, 0x20000080u, 2u, CORTEX_M4_ACCESS_DEBUG, "
           "true)");
}

static void test_priority(TestState* state) {
    CortexM4 cpu = create_cpu();
    cpu.mpu_control = MPU_ENABLE;
    set_region(&cpu, 0u, 0x20000000u, rasr(11u, 3u, false, 0u));
    set_region(&cpu, 3u, 0x20000400u, rasr(9u, 6u, false, 0u));
    expect(state,
           cortex_m4_mpu_access_permitted(&cpu, 0x200003fcu, 4u, CORTEX_M4_ACCESS_DATA, true),
           "cortex_m4_mpu_access_permitted(&cpu, 0x200003fcu, 4u, CORTEX_M4_ACCESS_DATA, "
           "true)");
    expect(state,
           !cortex_m4_mpu_access_permitted(&cpu, 0x20000400u, 4u, CORTEX_M4_ACCESS_DATA, true),
           "!cortex_m4_mpu_access_permitted(&cpu, 0x20000400u, 4u, CORTEX_M4_ACCESS_DATA, "
           "true)");
    expect(state,
           cortex_m4_mpu_access_permitted(&cpu, 0x20000400u, 4u, CORTEX_M4_ACCESS_DATA, false),
           "cortex_m4_mpu_access_permitted(&cpu, 0x20000400u, 4u, CORTEX_M4_ACCESS_DATA, "
           "false)");
    cpu.mpu_region_attributes[3] |= 1u << (8u + 2u);
    expect(state,
           cortex_m4_mpu_access_permitted(&cpu, 0x20000500u, 4u, CORTEX_M4_ACCESS_DATA, true),
           "cortex_m4_mpu_access_permitted(&cpu, 0x20000500u, 4u, CORTEX_M4_ACCESS_DATA, "
           "true)");
}

static void test_background_and_fault_modes(TestState* state) {
    CortexM4 cpu = create_cpu();
    const uint32_t executable_addresses[] = {0x00000000u, 0x20000000u, 0x60000000u, 0x80000000u};
    const uint32_t execute_never_addresses[] = {0x40000000u, 0xa0000000u, 0xc0000000u, 0xe0000000u};
    cpu.mpu_control = MPU_ENABLE | MPU_PRIVDEFENA;
    for (uint8_t index = 0u; index < sizeof(executable_addresses) / sizeof(executable_addresses[0]);
         index++) {
        expect(state,
               cortex_m4_mpu_access_permitted(&cpu, executable_addresses[index], 2u,
                                              CORTEX_M4_ACCESS_INSTRUCTION, false),
               "cortex_m4_mpu_access_permitted(&cpu, executable_addresses[index], 2u, "
               "CORTEX_M4_ACCESS_INSTRUCTION, false)");
        expect(state,
               cortex_m4_mpu_access_permitted(&cpu, executable_addresses[index], 4u,
                                              CORTEX_M4_ACCESS_DATA, true),
               "cortex_m4_mpu_access_permitted(&cpu, executable_addresses[index], 4u, "
               "CORTEX_M4_ACCESS_DATA, true)");
    }
    for (uint8_t index = 0u;
         index < sizeof(execute_never_addresses) / sizeof(execute_never_addresses[0]); index++) {
        expect(state,
               !cortex_m4_mpu_access_permitted(&cpu, execute_never_addresses[index], 2u,
                                               CORTEX_M4_ACCESS_INSTRUCTION, false),
               "!cortex_m4_mpu_access_permitted(&cpu, execute_never_addresses[index], 2u, "
               "CORTEX_M4_ACCESS_INSTRUCTION, false)");
        expect(state,
               cortex_m4_mpu_access_permitted(&cpu, execute_never_addresses[index], 4u,
                                              CORTEX_M4_ACCESS_DATA, true),
               "cortex_m4_mpu_access_permitted(&cpu, execute_never_addresses[index], 4u, "
               "CORTEX_M4_ACCESS_DATA, true)");
    }
    cpu.control = CORTEX_M4_CONTROL_NPRIV;
    expect(state,
           !cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, false),
           "!cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, "
           "false)");
    cpu.control = 0u;
    cpu.mpu_control = MPU_ENABLE;
    expect(state,
           !cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, false),
           "!cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, "
           "false)");
    cpu.mpu_control = 0u;
    cpu.control = CORTEX_M4_CONTROL_NPRIV;
    expect(state,
           cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, true),
           "cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, "
           "true)");

    cpu.mpu_control = MPU_ENABLE;
    cpu.xpsr = CORTEX_M4_XPSR_T | 2u;
    expect(state,
           cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, true),
           "cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, "
           "true)");
    cpu.xpsr = CORTEX_M4_XPSR_T | 3u;
    expect(state,
           cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, true),
           "cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, "
           "true)");
    cpu.mpu_control |= MPU_HFNMIENA;
    expect(state,
           !cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, true),
           "!cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, "
           "true)");
    cpu.xpsr = CORTEX_M4_XPSR_T | 2u;
    expect(state,
           !cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, true),
           "!cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, "
           "true)");
    cpu.xpsr = CORTEX_M4_XPSR_T | 4u;
    cpu.mpu_control = MPU_ENABLE;
    expect(state,
           !cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, true),
           "!cortex_m4_mpu_access_permitted(&cpu, 0x20000000u, 4u, CORTEX_M4_ACCESS_DATA, "
           "true)");
}

static void test_fault_metadata(TestState* state) {
    CortexM4 cpu = create_cpu();
    cpu.mpu_control = MPU_ENABLE;
    cpu.mmfar = 0xaaaaaaaau;
    expect(state, !cortex_m4_mpu_check(&cpu, 0x12345678u, 2u, CORTEX_M4_ACCESS_INSTRUCTION, false),
           "!cortex_m4_mpu_check(&cpu, 0x12345678u, 2u, CORTEX_M4_ACCESS_INSTRUCTION, false)");
    expect(state, (cpu.cfsr & 1u) != 0u, "(cpu.cfsr & 1u) != 0u");
    expect(state, (cpu.cfsr & 0x80u) == 0u, "(cpu.cfsr & 0x80u) == 0u");
    expect(state, cpu.mmfar == 0xaaaaaaaau, "cpu.mmfar == 0xaaaaaaaau");
    expect(state, raised_faults == 1u, "raised_faults == 1u");
    expect(state, last_fault == 4u, "last_fault == 4u");
    expect(state, (cpu.system_pending & (1u << 4u)) != 0u,
           "(cpu.system_pending & (1u << 4u)) != 0u");

    cpu.cfsr = 0u;
    cpu.system_pending = 0u;
    expect(state, !cortex_m4_mpu_check(&cpu, 0x89abcdefu, 1u, CORTEX_M4_ACCESS_DATA, true),
           "!cortex_m4_mpu_check(&cpu, 0x89abcdefu, 1u, CORTEX_M4_ACCESS_DATA, true)");
    expect(state, cpu.cfsr == 0x82u, "cpu.cfsr == 0x82u");
    expect(state, cpu.mmfar == 0x89abcdefu, "cpu.mmfar == 0x89abcdefu");
    expect(state, raised_faults == 2u, "raised_faults == 2u");

    set_region(&cpu, 0u, 0x89abcde0u, rasr(4u, 3u, false, 0u));
    expect(state, cortex_m4_mpu_check(&cpu, 0x89abcdefu, 1u, CORTEX_M4_ACCESS_DATA, true),
           "cortex_m4_mpu_check(&cpu, 0x89abcdefu, 1u, CORTEX_M4_ACCESS_DATA, true)");
    expect(state, raised_faults == 2u, "raised_faults == 2u");
    expect(state, !cortex_m4_mpu_access_permitted(NULL, 0u, 1u, CORTEX_M4_ACCESS_DATA, false),
           "!cortex_m4_mpu_access_permitted(NULL, 0u, 1u, CORTEX_M4_ACCESS_DATA, false)");
    expect(state, !cortex_m4_mpu_check(NULL, 0u, 1u, CORTEX_M4_ACCESS_DATA, false),
           "!cortex_m4_mpu_check(NULL, 0u, 1u, CORTEX_M4_ACCESS_DATA, false)");
    expect(state, !cortex_m4_mpu_access_permitted(&cpu, 0u, 3u, CORTEX_M4_ACCESS_DATA, false),
           "!cortex_m4_mpu_access_permitted(&cpu, 0u, 3u, CORTEX_M4_ACCESS_DATA, false)");
}

int main(void) {
    TestState state = {0u};
    test_reset_and_copy(&state);
    test_register_access(&state);
    test_absent_mpu(&state);
    test_region_registers(&state);
    test_region_sizes_and_subregions(&state);
    test_all_region_sizes(&state);
    test_permissions(&state);
    test_priority(&state);
    test_background_and_fault_modes(&state);
    test_fault_metadata(&state);
    return test_finish(&state);
}
