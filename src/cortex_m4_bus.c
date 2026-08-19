#include "cortex_m4_internal.h"

enum {
    SCB_INTERRUPT_CONTROLLER_TYPE = 0xe000e004u,
    SYSTICK_CONTROL = 0xe000e010u,
    SYSTICK_RELOAD = 0xe000e014u,
    SYSTICK_CURRENT = 0xe000e018u,
    SYSTICK_CALIBRATION = 0xe000e01cu,
    NVIC_ENABLE = 0xe000e100u,
    NVIC_CLEAR_ENABLE = 0xe000e180u,
    NVIC_PENDING = 0xe000e200u,
    NVIC_CLEAR_PENDING = 0xe000e280u,
    NVIC_ACTIVE = 0xe000e300u,
    NVIC_PRIORITY = 0xe000e400u,
    SCB_CPUID = 0xe000ed00u,
    SCB_ICSR = 0xe000ed04u,
    SCB_VTOR = 0xe000ed08u,
    SCB_AIRCR = 0xe000ed0cu,
    SCB_SCR = 0xe000ed10u,
    SCB_CCR = 0xe000ed14u,
    SCB_SHPR = 0xe000ed18u,
    SCB_SHCSR = 0xe000ed24u,
    SCB_CFSR = 0xe000ed28u,
    SCB_HFSR = 0xe000ed2cu,
    SCB_MMFAR = 0xe000ed34u,
    SCB_BFAR = 0xe000ed38u,
    SCB_CPACR = 0xe000ed88u,
    NVIC_SOFTWARE_TRIGGER = 0xe000ef00u,
};

static uint32_t write_partial(uint32_t previous, uint32_t address, uint8_t size,
                              uint32_t value);

static uint32_t read_bytes(const uint8_t* bytes, size_t count, uint32_t offset,
                           uint8_t size) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < size && offset + index < count; index++) {
        value |= (uint32_t)bytes[offset + index] << (index * 8u);
    }
    return value;
}

static void write_priority_bytes(uint8_t* bytes, size_t count, uint32_t offset,
                                 uint8_t size, uint32_t value) {
    for (uint8_t index = 0; index < size && offset + index < count; index++) {
        bytes[offset + index] = (uint8_t)(value >> (index * 8u)) & 0xf0u;
    }
}

static uint32_t access_value(uint32_t address, uint8_t size, uint32_t value) {
    return write_partial(0, address, size, value);
}

static bool any_external_pending(const CortexM4* cpu) {
    for (uint8_t index = 0; index < CORTEX_M4_IRQ_WORD_COUNT; index++) {
        if ((cpu->irq_pending[index] & cpu->irq_enabled[index]) != 0) {
            return true;
        }
    }
    return false;
}

static uint16_t pending_vector(const CortexM4* cpu) {
    uint16_t selected = 0;
    uint8_t selected_priority = 0xffu;
    const uint8_t system_exceptions[] = {4, 5, 6, 11, 12, 14, 15};
    for (uint8_t index = 0;
         index < sizeof(system_exceptions) / sizeof(system_exceptions[0]); index++) {
        const uint8_t exception = system_exceptions[index];
        if ((cpu->system_pending & (1u << exception)) != 0 &&
            cpu->system_priority[exception - 4] < selected_priority) {
            selected = exception;
            selected_priority = cpu->system_priority[exception - 4];
        }
    }
    for (uint16_t irq = 0; irq < CORTEX_M4_IRQ_COUNT; irq++) {
        const uint32_t mask = 1u << (irq & 31u);
        if ((cpu->irq_pending[irq / 32] & cpu->irq_enabled[irq / 32] & mask) != 0 &&
            cpu->irq_priority[irq] < selected_priority) {
            selected = irq + 16u;
            selected_priority = cpu->irq_priority[irq];
        }
    }
    return selected;
}

static uint32_t shcsr_value(const CortexM4* cpu) {
    uint32_t value = cpu->shcsr & 0x00070000u;
    const uint16_t current = (uint16_t)(cpu->xpsr & 0x1ffu);
    if (current == 4)
        value |= 1u << 0;
    if (current == 5)
        value |= 1u << 1;
    if (current == 6)
        value |= 1u << 3;
    if (current == 11)
        value |= 1u << 7;
    if (current == 12)
        value |= 1u << 8;
    if (current == 14)
        value |= 1u << 10;
    if (current == 15)
        value |= 1u << 11;
    if ((cpu->system_pending & (1u << 6)) != 0)
        value |= 1u << 12;
    if ((cpu->system_pending & (1u << 4)) != 0)
        value |= 1u << 13;
    if ((cpu->system_pending & (1u << 5)) != 0)
        value |= 1u << 14;
    if ((cpu->system_pending & (1u << 11)) != 0)
        value |= 1u << 15;
    return value;
}

static uint32_t read_partial(uint32_t value, uint32_t address, uint8_t size) {
    const uint32_t shift = (address & 3u) * 8u;
    if (size == 1) {
        return (value >> shift) & 0xffu;
    }
    if (size == 2) {
        return (value >> shift) & 0xffffu;
    }
    return value;
}

static uint32_t write_partial(uint32_t previous, uint32_t address, uint8_t size,
                              uint32_t value) {
    const uint32_t shift = (address & 3u) * 8u;
    if (size == 1) {
        return (previous & ~(0xffu << shift)) | ((value & 0xffu) << shift);
    }
    if (size == 2) {
        return (previous & ~(0xffffu << shift)) | ((value & 0xffffu) << shift);
    }
    return value;
}

bool cortex_m4_core_read(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t* value) {
    if (address < 0xe000e000u || address >= 0xe0100000u) {
        return false;
    }
    const uint32_t aligned = address & ~3u;
    uint32_t data = 0;
    if (aligned == SCB_INTERRUPT_CONTROLLER_TYPE) {
        data = 7u;
    } else if (aligned == SYSTICK_CONTROL) {
        data = cpu->systick_control;
        cpu->systick_control &= ~(1u << 16);
    } else if (aligned == SYSTICK_RELOAD) {
        data = cpu->systick_reload;
    } else if (aligned == SYSTICK_CURRENT) {
        data = cpu->systick_current;
    } else if (aligned == SYSTICK_CALIBRATION) {
        data = cpu->systick_calibration;
    } else if (aligned >= NVIC_ENABLE && aligned < NVIC_ENABLE + 32) {
        data = cpu->irq_enabled[(aligned - NVIC_ENABLE) / 4];
    } else if (aligned >= NVIC_CLEAR_ENABLE && aligned < NVIC_CLEAR_ENABLE + 32) {
        data = cpu->irq_enabled[(aligned - NVIC_CLEAR_ENABLE) / 4];
    } else if (aligned >= NVIC_PENDING && aligned < NVIC_PENDING + 32) {
        data = cpu->irq_pending[(aligned - NVIC_PENDING) / 4];
    } else if (aligned >= NVIC_CLEAR_PENDING && aligned < NVIC_CLEAR_PENDING + 32) {
        data = cpu->irq_pending[(aligned - NVIC_CLEAR_PENDING) / 4];
    } else if (aligned >= NVIC_ACTIVE && aligned < NVIC_ACTIVE + 32) {
        data = cpu->irq_active[(aligned - NVIC_ACTIVE) / 4];
    } else if (address >= NVIC_PRIORITY &&
               address + size <= NVIC_PRIORITY + CORTEX_M4_IRQ_COUNT) {
        data = read_bytes(cpu->irq_priority, CORTEX_M4_IRQ_COUNT, address - NVIC_PRIORITY,
                          size);
        *value = data;
        return true;
    } else if (aligned == SCB_CPUID) {
        data = 0x410fc241u;
    } else if (aligned == SCB_ICSR) {
        const uint16_t pending = pending_vector(cpu);
        data = cpu->xpsr & 0x1ffu;
        data |= (uint32_t)pending << 12;
        if (any_external_pending(cpu)) {
            data |= 1u << 22;
        }
        if ((cpu->system_pending & (1u << 15)) != 0) {
            data |= 1u << 26;
        }
        if ((cpu->system_pending & (1u << 14)) != 0) {
            data |= 1u << 28;
        }
    } else if (aligned == SCB_VTOR) {
        data = cpu->vtor;
    } else if (aligned == SCB_AIRCR) {
        data = 0xfa050000u | cpu->aircr;
    } else if (aligned == SCB_SCR) {
        data = cpu->scr;
    } else if (aligned == SCB_CCR) {
        data = cpu->ccr;
    } else if (address >= SCB_SHPR && address + size <= SCB_SHPR + 12) {
        data = read_bytes(cpu->system_priority, 12, address - SCB_SHPR, size);
        *value = data;
        return true;
    } else if (aligned == SCB_SHCSR) {
        data = shcsr_value(cpu);
    } else if (aligned == SCB_CFSR) {
        data = cpu->cfsr;
    } else if (aligned == SCB_HFSR) {
        data = cpu->hfsr;
    } else if (aligned == SCB_MMFAR) {
        data = cpu->mmfar;
    } else if (aligned == SCB_BFAR) {
        data = cpu->bfar;
    } else if (aligned == SCB_CPACR) {
        data = cpu->cpacr;
    } else {
        data = 0;
    }
    *value = read_partial(data, address, size);
    return true;
}

bool cortex_m4_core_write(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t value) {
    if (address < 0xe000e000u || address >= 0xe0100000u) {
        return false;
    }
    const uint32_t aligned = address & ~3u;
    if (aligned == SYSTICK_CONTROL) {
        cpu->systick_control =
            write_partial(cpu->systick_control, address, size, value) & 0x00010007u;
    } else if (aligned == SYSTICK_RELOAD) {
        cpu->systick_reload =
            write_partial(cpu->systick_reload, address, size, value) & 0x00ffffffu;
    } else if (aligned == SYSTICK_CURRENT) {
        cpu->systick_current = 0;
        cpu->systick_control &= ~(1u << 16);
    } else if (aligned >= NVIC_ENABLE && aligned < NVIC_ENABLE + 32) {
        cpu->irq_enabled[(aligned - NVIC_ENABLE) / 4] |= access_value(address, size, value);
    } else if (aligned >= NVIC_CLEAR_ENABLE && aligned < NVIC_CLEAR_ENABLE + 32) {
        cpu->irq_enabled[(aligned - NVIC_CLEAR_ENABLE) / 4] &=
            ~access_value(address, size, value);
    } else if (aligned >= NVIC_PENDING && aligned < NVIC_PENDING + 32) {
        cpu->irq_pending[(aligned - NVIC_PENDING) / 4] |=
            access_value(address, size, value);
    } else if (aligned >= NVIC_CLEAR_PENDING && aligned < NVIC_CLEAR_PENDING + 32) {
        cpu->irq_pending[(aligned - NVIC_CLEAR_PENDING) / 4] &=
            ~access_value(address, size, value);
    } else if (address >= NVIC_PRIORITY &&
               address + size <= NVIC_PRIORITY + CORTEX_M4_IRQ_COUNT) {
        write_priority_bytes(cpu->irq_priority, CORTEX_M4_IRQ_COUNT,
                             address - NVIC_PRIORITY, size, value);
    } else if (aligned == SCB_ICSR) {
        if ((value & (1u << 31)) != 0)
            cpu->system_pending |= 1u << 2;
        if ((value & (1u << 28)) != 0)
            cpu->system_pending |= 1u << 14;
        if ((value & (1u << 27)) != 0)
            cpu->system_pending &= ~(1u << 14);
        if ((value & (1u << 26)) != 0)
            cpu->system_pending |= 1u << 15;
        if ((value & (1u << 25)) != 0)
            cpu->system_pending &= ~(1u << 15);
    } else if (aligned == SCB_VTOR) {
        cpu->vtor = value & 0xffffff80u;
    } else if (aligned == SCB_AIRCR) {
        if (size == 4 && (value >> 16) == 0x05fau) {
            cpu->aircr = value & 0x00000700u;
            if ((value & (1u << 2)) != 0) {
                cpu->reset_requested = true;
            }
        }
    } else if (aligned == SCB_SCR) {
        cpu->scr = write_partial(cpu->scr, address, size, value) & 0x1eu;
    } else if (aligned == SCB_CCR) {
        cpu->ccr = write_partial(cpu->ccr, address, size, value) & 0x0007031bu;
    } else if (address >= SCB_SHPR && address + size <= SCB_SHPR + 12) {
        write_priority_bytes(cpu->system_priority, 12, address - SCB_SHPR, size, value);
    } else if (aligned == SCB_SHCSR) {
        cpu->shcsr = write_partial(cpu->shcsr, address, size, value) & 0x00070000u;
    } else if (aligned == SCB_CFSR) {
        cpu->cfsr &= ~write_partial(0, address, size, value);
    } else if (aligned == SCB_HFSR) {
        cpu->hfsr &= ~write_partial(0, address, size, value);
    } else if (aligned == SCB_MMFAR) {
        cpu->mmfar = write_partial(cpu->mmfar, address, size, value);
    } else if (aligned == SCB_BFAR) {
        cpu->bfar = write_partial(cpu->bfar, address, size, value);
    } else if (aligned == SCB_CPACR) {
        cpu->cpacr = write_partial(cpu->cpacr, address, size, value) & 0x00f00000u;
    } else if (aligned == NVIC_SOFTWARE_TRIGGER && (value & 0x1ffu) < CORTEX_M4_IRQ_COUNT) {
        cortex_m4_set_irq(cpu, (uint16_t)(value & 0x1ffu), true);
    }
    return true;
}

bool cortex_m4_bus_read(CortexM4* cpu, uint32_t address, uint8_t size,
                        CortexM4Access access, uint32_t* value) {
    if (value == NULL || (size != 1 && size != 2 && size != 4)) {
        return false;
    }
    if (cortex_m4_core_read(cpu, address, size, value)) {
        return true;
    }
    return cpu->bus.read(cpu->bus.context, address, size, access, value);
}

bool cortex_m4_bus_write(CortexM4* cpu, uint32_t address, uint8_t size,
                         CortexM4Access access, uint32_t value) {
    if (size != 1 && size != 2 && size != 4) {
        return false;
    }
    if (cortex_m4_core_write(cpu, address, size, value)) {
        cpu->exclusive_valid = false;
        return true;
    }
    const bool written = cpu->bus.write(cpu->bus.context, address, size, access, value);
    if (written && cpu->exclusive_valid &&
        address < cpu->exclusive_address + cpu->exclusive_size &&
        cpu->exclusive_address < address + size) {
        cpu->exclusive_valid = false;
    }
    return written;
}

void cortex_m4_advance(CortexM4* cpu, uint32_t cycles) {
    for (uint32_t index = 0; index < cycles; index++) {
        if ((cpu->systick_control & 1u) != 0) {
            if (cpu->systick_current == 0) {
                cpu->systick_current = cpu->systick_reload;
                cpu->systick_control |= 1u << 16;
                if ((cpu->systick_control & 2u) != 0) {
                    cpu->system_pending |= 1u << 15;
                    cpu->sleeping = false;
                }
            } else {
                cpu->systick_current--;
            }
        }
    }
    cpu->cycles += cycles;
    if (cpu->bus.advance != NULL) {
        cpu->bus.advance(cpu->bus.context, cycles);
    }
}
