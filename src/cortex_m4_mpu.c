#include "cortex_m4_internal.h"

#include <limits.h>

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

#define MPU_CTRL_MASK 0x00000007u
#define MPU_CTRL_ENABLE 0x00000001u
#define MPU_CTRL_HFNMIENA 0x00000002u
#define MPU_CTRL_PRIVDEFENA 0x00000004u
#define MPU_RBAR_VALID 0x00000010u
#define MPU_RBAR_ADDRESS_MASK 0xffffffe0u
#define MPU_RASR_XN 0x10000000u
#define MPU_RASR_AP_MASK 0x07000000u
#define MPU_RASR_SRD_MASK 0x0000ff00u
#define MPU_RASR_SIZE_MASK 0x0000003eu
#define MPU_RASR_ENABLE 0x00000001u
#define MPU_RASR_MASK 0x173fff3fu

#define CFSR_MMARVALID 0x00000080u
#define CFSR_DACCVIOL 0x00000002u
#define CFSR_IACCVIOL 0x00000001u

static bool valid_register_access(uint32_t address, uint8_t size) {
    return size == 4u && (address & 3u) == 0u;
}

static bool privileged_access(const CortexM4* cpu, CortexM4Access access) {
    if (access == CORTEX_M4_ACCESS_DEBUG) {
        return true;
    }
    if (access == CORTEX_M4_ACCESS_UNPRIVILEGED_DATA) {
        return false;
    }
    return (cpu->xpsr & 0x1ffu) != 0u || (cpu->control & CORTEX_M4_CONTROL_NPRIV) == 0u;
}

static uint8_t alias_number(uint32_t address) {
    if (address == MPU_RBAR_A1 || address == MPU_RASR_A1) {
        return 1u;
    }
    if (address == MPU_RBAR_A2 || address == MPU_RASR_A2) {
        return 2u;
    }
    if (address == MPU_RBAR_A3 || address == MPU_RASR_A3) {
        return 3u;
    }
    return 0u;
}

static uint8_t selected_region(const CortexM4* cpu, uint32_t address) {
    const uint8_t alias = alias_number(address);
    if (alias == 0u) {
        return (uint8_t)(cpu->mpu_region_number & 7u);
    }
    return (uint8_t)(((cpu->mpu_region_number & ~3u) | alias) & 7u);
}

static bool is_rbar(uint32_t address) {
    return address == MPU_RBAR || address == MPU_RBAR_A1 || address == MPU_RBAR_A2 ||
           address == MPU_RBAR_A3;
}

void cortex_m4_mpu_reset(CortexM4* cpu) {
    if (cpu == NULL) {
        return;
    }
    cpu->mpu_control = 0u;
    cpu->mpu_region_number = 0u;
    for (uint8_t region = 0u; region < CORTEX_M4_MPU_REGION_COUNT; region++) {
        cpu->mpu_region_base[region] = 0u;
        cpu->mpu_region_attributes[region] = 0u;
    }
}

CortexM4SystemAccess cortex_m4_mpu_read(CortexM4* cpu, uint32_t address, uint8_t size,
                                        CortexM4Access access, uint32_t* value) {
    if (address < MPU_TYPE || address > MPU_RASR_A3) {
        return CORTEX_M4_SYSTEM_ACCESS_OUTSIDE;
    }
    if (cpu == NULL || value == NULL || !valid_register_access(address, size) ||
        !privileged_access(cpu, access)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    if (address == MPU_TYPE) {
        *value = (uint32_t)cpu->mpu_region_count << 8u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (cpu->mpu_region_count == 0u) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    if (address == MPU_CTRL) {
        *value = cpu->mpu_control;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (address == MPU_RNR) {
        *value = cpu->mpu_region_number;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (is_rbar(address)) {
        const uint8_t region = selected_region(cpu, address);
        *value = cpu->mpu_region_base[region] | region;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    *value = cpu->mpu_region_attributes[selected_region(cpu, address)];
    return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
}

CortexM4SystemAccess cortex_m4_mpu_write(CortexM4* cpu, uint32_t address, uint8_t size,
                                         CortexM4Access access, uint32_t value) {
    if (address < MPU_TYPE || address > MPU_RASR_A3) {
        return CORTEX_M4_SYSTEM_ACCESS_OUTSIDE;
    }
    if (cpu == NULL || !valid_register_access(address, size) ||
        !privileged_access(cpu, access)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    if (address == MPU_TYPE) {
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (cpu->mpu_region_count == 0u) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    if (address == MPU_CTRL) {
        cpu->mpu_control = value & MPU_CTRL_MASK;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (address == MPU_RNR) {
        cpu->mpu_region_number = value & 7u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (is_rbar(address)) {
        uint8_t region = selected_region(cpu, address);
        if ((value & MPU_RBAR_VALID) != 0u) {
            region = (uint8_t)(value & 7u);
            cpu->mpu_region_number = region;
        }
        cpu->mpu_region_base[region] = value & MPU_RBAR_ADDRESS_MASK;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    cpu->mpu_region_attributes[selected_region(cpu, address)] = value & MPU_RASR_MASK;
    return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
}

static bool mpu_active(const CortexM4* cpu) {
    if (cpu->mpu_region_count == 0u || (cpu->mpu_control & MPU_CTRL_ENABLE) == 0u) {
        return false;
    }
    const uint16_t exception = (uint16_t)(cpu->xpsr & 0x1ffu);
    return (exception != 2u && exception != 3u) ||
           (cpu->mpu_control & MPU_CTRL_HFNMIENA) != 0u;
}

static bool region_contains(const CortexM4* cpu, uint8_t region, uint32_t address) {
    const uint32_t attributes = cpu->mpu_region_attributes[region];
    if ((attributes & MPU_RASR_ENABLE) == 0u) {
        return false;
    }
    const uint8_t size_encoding = (uint8_t)((attributes & MPU_RASR_SIZE_MASK) >> 1u);
    if (size_encoding < 4u) {
        return false;
    }
    const uint64_t region_size = UINT64_C(1) << (size_encoding + 1u);
    const uint64_t region_mask = region_size - 1u;
    const uint64_t base = (uint64_t)cpu->mpu_region_base[region] & ~region_mask;
    if ((uint64_t)address < base || (uint64_t)address >= base + region_size) {
        return false;
    }
    if (size_encoding < 7u) {
        return true;
    }
    const uint8_t subregion = (uint8_t)(((uint64_t)address - base) * 8u / region_size);
    return (attributes & (UINT32_C(1) << (subregion + 8u))) == 0u;
}

static int8_t matching_region(const CortexM4* cpu, uint32_t address) {
    for (int8_t region = (int8_t)cpu->mpu_region_count - 1; region >= 0; region--) {
        if (region_contains(cpu, (uint8_t)region, address)) {
            return region;
        }
    }
    return -1;
}

static bool region_permission(uint32_t attributes, bool privileged, bool write,
                              bool instruction) {
    if (instruction && (attributes & MPU_RASR_XN) != 0u) {
        return false;
    }
    const uint8_t permission = (uint8_t)((attributes & MPU_RASR_AP_MASK) >> 24u);
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

static bool background_permission(uint32_t address, bool instruction) {
    if (!instruction) {
        return true;
    }
    return address < 0x40000000u || (address >= 0x60000000u && address < 0xa0000000u);
}

static bool byte_access_permitted(const CortexM4* cpu, uint32_t address,
                                  CortexM4Access access, bool write) {
    const bool instruction = access == CORTEX_M4_ACCESS_INSTRUCTION;
    const bool privileged = privileged_access(cpu, access);
    const int8_t region = matching_region(cpu, address);
    if (region >= 0) {
        return region_permission(cpu->mpu_region_attributes[(uint8_t)region], privileged,
                                 write, instruction);
    }
    return privileged && (cpu->mpu_control & MPU_CTRL_PRIVDEFENA) != 0u &&
           background_permission(address, instruction);
}

bool cortex_m4_mpu_access_permitted(const CortexM4* cpu, uint32_t address, uint8_t size,
                                    CortexM4Access access, bool write) {
    if (cpu == NULL || (size != 1u && size != 2u && size != 4u) ||
        address > UINT32_MAX - (uint32_t)(size - 1u)) {
        return false;
    }
    if (access == CORTEX_M4_ACCESS_DEBUG || !mpu_active(cpu)) {
        return true;
    }
    for (uint8_t offset = 0u; offset < size; offset++) {
        if (!byte_access_permitted(cpu, address + offset, access, write)) {
            return false;
        }
    }
    return true;
}

bool cortex_m4_mpu_check(CortexM4* cpu, uint32_t address, uint8_t size,
                         CortexM4Access access, bool write) {
    if (cortex_m4_mpu_access_permitted(cpu, address, size, access, write)) {
        return true;
    }
    if (cpu == NULL) {
        return false;
    }
    if (access == CORTEX_M4_ACCESS_INSTRUCTION) {
        cpu->cfsr |= CFSR_IACCVIOL;
    } else {
        cpu->mmfar = address;
        cpu->cfsr |= CFSR_MMARVALID | CFSR_DACCVIOL;
    }
    cortex_m4_raise_fault(cpu, 4u);
    return false;
}
