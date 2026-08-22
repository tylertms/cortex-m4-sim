#include "architecture/cortex_m4/internal.h"

bool cortex_m4_bus_read(CortexM4* cpu, uint32_t address, uint8_t size, CortexM4Access access,
                        uint32_t* value) {
    if (value == NULL || (size != 1 && size != 2 && size != 4)) {
        return false;
    }
    cortex_m4_timing_access(cpu, address, size, access, false);
    const CortexM4SystemAccess mpu_access = cortex_m4_mpu_read(cpu, address, size, access, value);
    if (mpu_access == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED) {
        return true;
    }
    if (mpu_access == CORTEX_M4_SYSTEM_ACCESS_REJECTED) {
        return false;
    }
    if (!cortex_m4_mpu_check(cpu, address, size, access, false)) {
        return false;
    }
    const CortexM4SystemAccess system_access =
        cortex_m4_system_read(cpu, address, size, access, value);
    if (system_access == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED) {
        return true;
    }
    if (system_access == CORTEX_M4_SYSTEM_ACCESS_REJECTED) {
        return false;
    }
    return cpu->bus.read(cpu->bus.context, address, size, access, value);
}

bool cortex_m4_bus_write(CortexM4* cpu, uint32_t address, uint8_t size, CortexM4Access access,
                         uint32_t value) {
    if (size != 1 && size != 2 && size != 4) {
        return false;
    }
    cortex_m4_timing_access(cpu, address, size, access, true);
    const CortexM4SystemAccess mpu_access = cortex_m4_mpu_write(cpu, address, size, access, value);
    if (mpu_access == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED) {
        cpu->exclusive_valid = false;
        cortex_m4_timing_observe_write(cpu, address, size);
        return true;
    }
    if (mpu_access == CORTEX_M4_SYSTEM_ACCESS_REJECTED) {
        return false;
    }
    if (!cortex_m4_mpu_check(cpu, address, size, access, true)) {
        return false;
    }
    const CortexM4SystemAccess system_access =
        cortex_m4_system_write(cpu, address, size, access, value);
    if (system_access == CORTEX_M4_SYSTEM_ACCESS_ACCEPTED) {
        cortex_m4_timing_observe_write(cpu, address, size);
        return true;
    }
    if (system_access == CORTEX_M4_SYSTEM_ACCESS_REJECTED) {
        return false;
    }
    const bool written = cpu->bus.write(cpu->bus.context, address, size, access, value);
    if (written) {
        cortex_m4_timing_observe_write(cpu, address, size);
    }
    return written;
}

bool cortex_m4_data_read(CortexM4* cpu, uint32_t address, uint8_t size, CortexM4Access access,
                         uint32_t* value) {
    if (size > 1 && (address & (size - 1u)) != 0 && (cpu->ccr & (1u << 3)) != 0) {
        cpu->cfsr |= 1u << 24;
        cortex_m4_raise_fault(cpu, 6);
        return false;
    }
    if (!cortex_m4_mpu_check(cpu, address, size, access, false)) {
        return false;
    }
    if (cortex_m4_bus_read(cpu, address, size, access, value)) {
        cortex_m4_debug_memory_access(cpu, address, size, false, *value);
        return true;
    }
    cpu->bfar = address;
    cpu->cfsr |= (1u << 15) | (1u << 9);
    cortex_m4_raise_fault(cpu, 5);
    return false;
}

bool cortex_m4_data_write(CortexM4* cpu, uint32_t address, uint8_t size, CortexM4Access access,
                          uint32_t value) {
    if (size > 1 && (address & (size - 1u)) != 0 && (cpu->ccr & (1u << 3)) != 0) {
        cpu->cfsr |= 1u << 24;
        cortex_m4_raise_fault(cpu, 6);
        return false;
    }
    if (!cortex_m4_mpu_check(cpu, address, size, access, true)) {
        return false;
    }
    if (cortex_m4_bus_write(cpu, address, size, access, value)) {
        cortex_m4_debug_memory_access(cpu, address, size, true, value);
        return true;
    }
    cpu->bfar = address;
    cpu->cfsr |= (1u << 15) | (1u << 9);
    cortex_m4_raise_fault(cpu, 5);
    return false;
}

bool cortex_m4_require_alignment(CortexM4* cpu, uint32_t address, uint8_t alignment) {
    if ((address & (alignment - 1u)) == 0) {
        return true;
    }
    cpu->cfsr |= 1u << 24;
    cortex_m4_raise_fault(cpu, 6);
    return false;
}

void cortex_m4_advance(CortexM4* cpu, uint32_t cycles) {
    const bool sleeping = cpu->sleeping;
    for (uint32_t index = 0; index < cycles; index++) {
        if ((cpu->systick_control & 1u) != 0) {
            if (cpu->systick_current == 0) {
                cpu->systick_current = cpu->systick_reload;
            } else {
                cpu->systick_current--;
                if (cpu->systick_current == 0) {
                    cpu->systick_control |= 1u << 16;
                    if ((cpu->systick_control & 2u) != 0) {
                        cortex_m4_system_set_pending(cpu, 15, true);
                        cpu->sleeping = false;
                    }
                }
            }
        }
    }
    cpu->cycles += cycles;
    cortex_m4_debug_advance(cpu, cycles, sleeping);
    if (cpu->bus.advance != NULL) {
        cpu->bus.advance(cpu->bus.context, cycles);
    }
}
