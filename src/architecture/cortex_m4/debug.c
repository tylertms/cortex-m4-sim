#include "architecture/cortex_m4/internal.h"

#include <limits.h>
#include <string.h>

#define ITM_BASE 0xe0000000u
#define DWT_BASE 0xe0001000u
#define FPB_BASE 0xe0002000u
#define TPIU_BASE 0xe0040000u
#define DHCSR 0xe000edf0u
#define DCRSR 0xe000edf4u
#define DCRDR 0xe000edf8u
#define DEMCR 0xe000edfcu
#define CORESIGHT_LAR 0xfb0u
#define CORESIGHT_LSR 0xfb4u
#define CORESIGHT_UNLOCK 0xc5acce55u
#define DHCSR_DEBUG_KEY 0xa05f0000u
#define DHCSR_CONTROL_MASK 0x0000002fu
#define DEMCR_CONTROL_MASK 0x010f07f1u
#define DEMCR_MONITOR_ENABLE (1u << 16)
#define DEMCR_MONITOR_PENDING (1u << 17)
#define DEMCR_TRACE_ENABLE (1u << 24)
#define DWT_CYCLE_COUNT_ENABLE 1u
#define DWT_CPI_COUNT_ENABLE (1u << 17)
#define DWT_EXCEPTION_COUNT_ENABLE (1u << 18)
#define DWT_SLEEP_COUNT_ENABLE (1u << 19)
#define DWT_LSU_COUNT_ENABLE (1u << 20)
#define DWT_FUNCTION_MATCHED (1u << 24)
#define FPB_ENABLE 1u
#define ITM_ENABLE 1u

static bool valid_access(uint32_t address, uint8_t size) {
    return (size == 1 || size == 2 || size == 4) && (address & (uint32_t)(size - 1u)) == 0 &&
           (address & 3u) + size <= 4u;
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

static uint32_t write_partial(uint32_t previous, uint32_t address, uint8_t size, uint32_t value) {
    const uint32_t shift = (address & 3u) * 8u;
    uint32_t mask = UINT32_MAX;
    if (size == 1) {
        mask = 0xffu << shift;
    } else if (size == 2) {
        mask = 0xffffu << shift;
    }
    return (previous & ~mask) | ((value << shift) & mask);
}

static CortexM4SystemAccess accepted_read(uint32_t data, uint32_t address, uint8_t size,
                                          uint32_t* value) {
    *value = read_partial(data, address, size);
    return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
}

static bool word_access(uint32_t address, uint8_t size) { return size == 4 && (address & 3u) == 0; }

bool cortex_m4_debug_address(uint32_t address) {
    return (address >= ITM_BASE && address < ITM_BASE + 0x1000u) ||
           (address >= DWT_BASE && address < DWT_BASE + 0x1000u) ||
           (address >= FPB_BASE && address < FPB_BASE + 0x1000u) ||
           (address >= TPIU_BASE && address < TPIU_BASE + 0x1000u) ||
           (address >= DHCSR && address <= DEMCR + 3u);
}

static void pend_exception(CortexM4* cpu, uint8_t exception) {
    cpu->system_pending |= 1u << exception;
    cpu->event_register = true;
    cpu->sleeping = false;
}

static void debug_event(CortexM4* cpu, uint32_t cause) {
    cpu->dfsr |= cause & 0x1fu;
    if ((cpu->debug.dhcsr_control & 1u) != 0) {
        cpu->debug.halted = true;
        cpu->debug.step_armed = false;
        cpu->sleeping = false;
        return;
    }
    if ((cpu->debug.demcr & DEMCR_MONITOR_ENABLE) != 0 && (cpu->xpsr & 0x1ffu) != 12u) {
        cpu->debug.demcr |= DEMCR_MONITOR_PENDING;
        pend_exception(cpu, 12);
        return;
    }
    cpu->hfsr |= 1u << 31;
    pend_exception(cpu, 3);
}

static uint32_t debug_xpsr_value(const CortexM4* cpu) {
    const uint32_t it_state =
        ((uint32_t)(cpu->it_state & 3u) << 25) | ((uint32_t)(cpu->it_state & 0xfcu) << 8);
    return cpu->xpsr | it_state;
}

static void debug_load_xpsr(CortexM4* cpu, uint32_t value) {
    cpu->it_state = (uint8_t)(((value >> 25) & 3u) | ((value >> 8) & 0xfcu));
    cpu->xpsr = (value & ~0x0600fc00u) | CORTEX_M4_XPSR_T;
}

static uint32_t core_register_read(const CortexM4* cpu, uint8_t selector) {
    if (selector < 16u) {
        if (selector == 13u) {
            const bool thread = (cpu->xpsr & 0x1ffu) == 0;
            return thread && (cpu->control & CORTEX_M4_CONTROL_SPSEL) != 0 ? cpu->psp : cpu->msp;
        }
        return cpu->registers[selector];
    }
    if (selector == 16u) {
        return debug_xpsr_value(cpu);
    }
    if (selector == 17u) {
        return cpu->msp;
    }
    if (selector == 18u) {
        return cpu->psp;
    }
    if (selector == 20u) {
        return ((cpu->control & 0xffu) << 24) | ((cpu->faultmask & 1u) << 16) |
               ((cpu->basepri & 0xffu) << 8) | (cpu->primask & 1u);
    }
    if (selector == 33u) {
        return cpu->fpscr;
    }
    if (selector >= 64u && selector < 96u) {
        return cpu->fp_registers[selector - 64u];
    }
    return 0;
}

static void core_register_write(CortexM4* cpu, uint8_t selector, uint32_t value) {
    if (selector < 16u) {
        if (selector == 13u) {
            const bool thread = (cpu->xpsr & 0x1ffu) == 0;
            value &= ~3u;
            if (thread && (cpu->control & CORTEX_M4_CONTROL_SPSEL) != 0) {
                cpu->psp = value;
            } else {
                cpu->msp = value;
            }
            cpu->registers[13] = value;
        } else {
            cpu->registers[selector] = selector == 15u ? value & ~1u : value;
        }
        return;
    }
    if (selector == 16u) {
        debug_load_xpsr(cpu, value);
    } else if (selector == 17u) {
        cpu->msp = value & ~3u;
    } else if (selector == 18u) {
        cpu->psp = value & ~3u;
    } else if (selector == 20u) {
        cpu->control = (value >> 24) & 7u;
        cpu->faultmask = (value >> 16) & 1u;
        cpu->basepri = (value >> 8) & 0xf0u;
        cpu->primask = value & 1u;
    } else if (selector == 33u) {
        cpu->fpscr = value;
    } else if (selector >= 64u && selector < 96u) {
        cpu->fp_registers[selector - 64u] = value;
    }
}

static void transfer_core_register(CortexM4* cpu) {
    const uint8_t selector = (uint8_t)(cpu->debug.dcrsr & 0x7fu);
    if ((cpu->debug.dcrsr & (1u << 16)) != 0) {
        core_register_write(cpu, selector, cpu->debug.dcrdr);
    } else {
        cpu->debug.dcrdr = core_register_read(cpu, selector);
    }
}

static uint32_t dhcsr_value(const CortexM4* cpu) {
    uint32_t value = cpu->debug.dhcsr_control | (1u << 16);
    if (cpu->debug.halted) {
        value |= 1u << 17;
    }
    if (cpu->sleeping) {
        value |= 1u << 18;
    }
    if (cpu->stop == CORTEX_M4_STOP_LOCKUP) {
        value |= 1u << 19;
    }
    if (cpu->debug.retire_sticky) {
        value |= 1u << 24;
    }
    if (cpu->debug.reset_sticky) {
        value |= 1u << 25;
    }
    return value;
}

static CortexM4SystemAccess debug_register_read(CortexM4* cpu, uint32_t address, uint8_t size,
                                                uint32_t* value) {
    if (!word_access(address, size)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    if (address == DHCSR) {
        const uint32_t data = dhcsr_value(cpu);
        cpu->debug.retire_sticky = false;
        cpu->debug.reset_sticky = false;
        return accepted_read(data, address, size, value);
    }
    if (address == DCRSR) {
        return accepted_read(cpu->debug.dcrsr, address, size, value);
    }
    if (address == DCRDR) {
        return accepted_read(cpu->debug.dcrdr, address, size, value);
    }
    if (address == DEMCR) {
        return accepted_read(cpu->debug.demcr, address, size, value);
    }
    return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
}

static CortexM4SystemAccess debug_register_write(CortexM4* cpu, uint32_t address, uint8_t size,
                                                 uint32_t value) {
    if (!word_access(address, size)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    if (address == DHCSR) {
        if ((value & 0xffff0000u) != DHCSR_DEBUG_KEY) {
            return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
        }
        cpu->debug.dhcsr_control = value & DHCSR_CONTROL_MASK;
        if ((cpu->debug.dhcsr_control & 1u) == 0) {
            cpu->debug.halted = false;
            cpu->debug.step_armed = false;
        } else if ((cpu->debug.dhcsr_control & 2u) != 0) {
            cpu->debug.halted = true;
            cpu->debug.step_armed = false;
        } else if ((cpu->debug.dhcsr_control & 4u) != 0) {
            cpu->debug.halted = false;
            cpu->debug.step_armed = true;
        } else {
            cpu->debug.halted = false;
        }
        if (!cpu->debug.halted && cpu->stop == CORTEX_M4_STOP_BREAKPOINT) {
            cpu->stop = CORTEX_M4_STOP_RUNNING;
        }
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (address == DCRSR) {
        cpu->debug.dcrsr = value & 0x0001007fu;
        transfer_core_register(cpu);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (address == DCRDR) {
        cpu->debug.dcrdr = value;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (address == DEMCR) {
        cpu->debug.demcr = value & DEMCR_CONTROL_MASK;
        if ((cpu->debug.demcr & DEMCR_MONITOR_PENDING) != 0) {
            pend_exception(cpu, 12);
        } else {
            cpu->system_pending &= ~(1u << 12);
        }
        if ((cpu->debug.demcr & (1u << 19)) != 0) {
            debug_event(cpu, 1u << 4);
        }
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
}

static CortexM4SystemAccess dwt_read(CortexM4* cpu, uint32_t address, uint8_t size,
                                     uint32_t* value) {
    if (!word_access(address, size)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    const uint32_t offset = address - DWT_BASE;
    if (offset == 0) {
        return accepted_read(cpu->debug.dwt_control | 0x40000000u, address, size, value);
    }
    if (offset == 4) {
        return accepted_read(cpu->debug.dwt_cycle_count, address, size, value);
    }
    if (offset >= 8 && offset <= 0x18u && (offset & 3u) == 0) {
        return accepted_read(cpu->debug.dwt_counters[(offset - 8u) / 4u], address, size, value);
    }
    if (offset == 0x1cu) {
        return accepted_read(cpu->registers[15], address, size, value);
    }
    if (offset >= 0x20u && offset < 0x60u) {
        const uint8_t index = (uint8_t)((offset - 0x20u) / 0x10u);
        const uint32_t field = offset & 0x0fu;
        if (field == 0) {
            return accepted_read(cpu->debug.dwt_comparators[index].comparator, address, size,
                                 value);
        }
        if (field == 4) {
            return accepted_read(cpu->debug.dwt_comparators[index].mask, address, size, value);
        }
        if (field == 8) {
            const uint32_t function = cpu->debug.dwt_comparators[index].function;
            cpu->debug.dwt_comparators[index].function &= ~DWT_FUNCTION_MATCHED;
            return accepted_read(function, address, size, value);
        }
    }
    if (offset == CORESIGHT_LSR) {
        return accepted_read(cpu->debug.dwt_locked ? 3u : 1u, address, size, value);
    }
    return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
}

static CortexM4SystemAccess dwt_write(CortexM4* cpu, uint32_t address, uint8_t size,
                                      uint32_t value) {
    if (!word_access(address, size)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    const uint32_t offset = address - DWT_BASE;
    if (offset == CORESIGHT_LAR) {
        cpu->debug.dwt_locked = value != CORESIGHT_UNLOCK;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == CORESIGHT_LSR) {
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (cpu->debug.dwt_locked) {
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == 0) {
        cpu->debug.dwt_control = value & 0x007f1fffu;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == 4) {
        cpu->debug.dwt_cycle_count = value;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset >= 8 && offset <= 0x18u && (offset & 3u) == 0) {
        cpu->debug.dwt_counters[(offset - 8u) / 4u] = (uint8_t)value;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset >= 0x20u && offset < 0x60u) {
        const uint8_t index = (uint8_t)((offset - 0x20u) / 0x10u);
        const uint32_t field = offset & 0x0fu;
        if (field == 0) {
            cpu->debug.dwt_comparators[index].comparator = value;
            return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
        }
        if (field == 4) {
            cpu->debug.dwt_comparators[index].mask = value > 31u ? 31u : value;
            return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
        }
        if (field == 8) {
            cpu->debug.dwt_comparators[index].function = value & 0x000fffffu;
            return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
        }
    }
    return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
}

static CortexM4SystemAccess fpb_read(CortexM4* cpu, uint32_t address, uint8_t size,
                                     uint32_t* value) {
    if (!word_access(address, size)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    const uint32_t offset = address - FPB_BASE;
    if (offset == 0) {
        return accepted_read(0x00000260u | cpu->debug.fpb_control, address, size, value);
    }
    if (offset == 4) {
        return accepted_read(cpu->debug.fpb_remap, address, size, value);
    }
    if (offset >= 8 && offset < 0x28u) {
        return accepted_read(cpu->debug.fpb_comparators[(offset - 8u) / 4u], address, size, value);
    }
    if (offset == CORESIGHT_LSR) {
        return accepted_read(cpu->debug.fpb_locked ? 3u : 1u, address, size, value);
    }
    return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
}

static CortexM4SystemAccess fpb_write(CortexM4* cpu, uint32_t address, uint8_t size,
                                      uint32_t value) {
    if (!word_access(address, size)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    const uint32_t offset = address - FPB_BASE;
    if (offset == CORESIGHT_LAR) {
        cpu->debug.fpb_locked = value != CORESIGHT_UNLOCK;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == CORESIGHT_LSR) {
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (cpu->debug.fpb_locked) {
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == 0) {
        if ((value & 2u) != 0) {
            cpu->debug.fpb_control = value & FPB_ENABLE;
        }
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == 4) {
        cpu->debug.fpb_remap = value & 0x1fffffe0u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset >= 8 && offset < 0x28u) {
        const uint8_t index = (uint8_t)((offset - 8u) / 4u);
        const uint32_t address_mask = index < 6u ? 0x1ffffffcu : 0x1fffffffu;
        cpu->debug.fpb_comparators[index] = value & (address_mask | 0xc0000001u);
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
}

static CortexM4SystemAccess itm_read(CortexM4* cpu, uint32_t address, uint8_t size,
                                     uint32_t* value) {
    const uint32_t offset = address - ITM_BASE;
    if (offset < 0x80u && (offset & 3u) == 0) {
        const uint8_t port = (uint8_t)(offset / 4u);
        const bool ready = (cpu->debug.demcr & DEMCR_TRACE_ENABLE) != 0 &&
                           (cpu->debug.itm_trace_control & ITM_ENABLE) != 0 &&
                           (cpu->debug.itm_trace_enable & (1u << port)) != 0;
        return accepted_read(ready ? 1u : 0u, address, size, value);
    }
    if (!word_access(address, size)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    if (offset == 0xe00u) {
        return accepted_read(cpu->debug.itm_trace_enable, address, size, value);
    }
    if (offset == 0xe40u) {
        return accepted_read(cpu->debug.itm_trace_privilege, address, size, value);
    }
    if (offset == 0xe80u) {
        return accepted_read(cpu->debug.itm_trace_control, address, size, value);
    }
    if (offset == 0xef8u) {
        return accepted_read(cpu->debug.itm_integration_write, address, size, value);
    }
    if (offset == 0xefcu) {
        return accepted_read(cpu->debug.itm_integration_read, address, size, value);
    }
    if (offset == 0xf00u) {
        return accepted_read(cpu->debug.itm_integration_mode, address, size, value);
    }
    if (offset == CORESIGHT_LSR) {
        return accepted_read(cpu->debug.itm_locked ? 3u : 1u, address, size, value);
    }
    return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
}

static CortexM4SystemAccess itm_write(CortexM4* cpu, uint32_t address, uint8_t size,
                                      uint32_t value) {
    const uint32_t offset = address - ITM_BASE;
    if (offset == CORESIGHT_LAR && word_access(address, size)) {
        cpu->debug.itm_locked = value != CORESIGHT_UNLOCK;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == CORESIGHT_LSR && word_access(address, size)) {
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (cpu->debug.itm_locked) {
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset < 0x80u && (offset & 3u) + size <= 4u) {
        const uint8_t port = (uint8_t)(offset / 4u);
        const bool enabled = (cpu->debug.demcr & DEMCR_TRACE_ENABLE) != 0 &&
                             (cpu->debug.itm_trace_control & ITM_ENABLE) != 0 &&
                             (cpu->debug.itm_trace_enable & (1u << port)) != 0;
        if (enabled) {
            cpu->debug.itm_stimulus[port] =
                write_partial(cpu->debug.itm_stimulus[port], address, size, value);
        }
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (!word_access(address, size)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    if (offset == 0xe00u) {
        cpu->debug.itm_trace_enable = value;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == 0xe40u) {
        cpu->debug.itm_trace_privilege = value & 0x0fu;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == 0xe80u) {
        cpu->debug.itm_trace_control = value & 0x007f0f1fu;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == 0xef8u) {
        cpu->debug.itm_integration_write = value & 1u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == 0xefcu) {
        cpu->debug.itm_integration_read = value & 1u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == 0xf00u) {
        cpu->debug.itm_integration_mode = value & 1u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
}

static CortexM4SystemAccess tpiu_read(CortexM4* cpu, uint32_t address, uint8_t size,
                                      uint32_t* value) {
    if (!word_access(address, size)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    const uint32_t offset = address - TPIU_BASE;
    if (offset == 0) {
        return accepted_read(1u, address, size, value);
    }
    if (offset == 4u) {
        return accepted_read(cpu->debug.tpiu_current_port_size, address, size, value);
    }
    if (offset == 0x10u) {
        return accepted_read(cpu->debug.tpiu_async_prescaler, address, size, value);
    }
    if (offset == 0xf0u) {
        return accepted_read(cpu->debug.tpiu_selected_protocol, address, size, value);
    }
    if (offset == 0x300u) {
        return accepted_read(8u, address, size, value);
    }
    if (offset == 0x304u) {
        return accepted_read(cpu->debug.tpiu_formatter_control, address, size, value);
    }
    if (offset == 0xfc8u) {
        return accepted_read(0x000000ca, address, size, value);
    }
    if (offset == 0xfccu) {
        return accepted_read(0x00000011, address, size, value);
    }
    if (offset == CORESIGHT_LSR) {
        return accepted_read(cpu->debug.tpiu_locked ? 3u : 1u, address, size, value);
    }
    return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
}

static CortexM4SystemAccess tpiu_write(CortexM4* cpu, uint32_t address, uint8_t size,
                                       uint32_t value) {
    if (!word_access(address, size)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    const uint32_t offset = address - TPIU_BASE;
    if (offset == CORESIGHT_LAR) {
        cpu->debug.tpiu_locked = value != CORESIGHT_UNLOCK;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == CORESIGHT_LSR) {
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (cpu->debug.tpiu_locked) {
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == 4u) {
        cpu->debug.tpiu_current_port_size = value & 1u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == 0x10u) {
        cpu->debug.tpiu_async_prescaler = value & 0x1fffu;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == 0xf0u) {
        cpu->debug.tpiu_selected_protocol = value & 3u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    if (offset == 0x304u) {
        cpu->debug.tpiu_formatter_control = value & 0x00000103u;
        return CORTEX_M4_SYSTEM_ACCESS_ACCEPTED;
    }
    return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
}

void cortex_m4_debug_reset(CortexM4* cpu) {
    if (cpu == NULL) {
        return;
    }
    memset(&cpu->debug, 0, sizeof(cpu->debug));
    cpu->debug.reset_sticky = true;
    cpu->debug.itm_locked = true;
    cpu->debug.dwt_locked = false;
    cpu->debug.fpb_locked = false;
    cpu->debug.tpiu_locked = false;
    cpu->debug.tpiu_current_port_size = 1u;
    cpu->debug.tpiu_selected_protocol = 1u;
}

CortexM4SystemAccess cortex_m4_debug_read(CortexM4* cpu, uint32_t address, uint8_t size,
                                          uint32_t* value) {
    if (!cortex_m4_debug_address(address)) {
        return CORTEX_M4_SYSTEM_ACCESS_OUTSIDE;
    }
    if (cpu == NULL || value == NULL || !valid_access(address, size)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    if (address >= DHCSR && address <= DEMCR + 3u) {
        return debug_register_read(cpu, address, size, value);
    }
    if (address < DWT_BASE) {
        return itm_read(cpu, address, size, value);
    }
    if (address < FPB_BASE) {
        return dwt_read(cpu, address, size, value);
    }
    if (address < FPB_BASE + 0x1000u) {
        return fpb_read(cpu, address, size, value);
    }
    return tpiu_read(cpu, address, size, value);
}

CortexM4SystemAccess cortex_m4_debug_write(CortexM4* cpu, uint32_t address, uint8_t size,
                                           uint32_t value) {
    if (!cortex_m4_debug_address(address)) {
        return CORTEX_M4_SYSTEM_ACCESS_OUTSIDE;
    }
    if (cpu == NULL || !valid_access(address, size)) {
        return CORTEX_M4_SYSTEM_ACCESS_REJECTED;
    }
    if (address >= DHCSR && address <= DEMCR + 3u) {
        return debug_register_write(cpu, address, size, value);
    }
    if (address < DWT_BASE) {
        return itm_write(cpu, address, size, value);
    }
    if (address < FPB_BASE) {
        return dwt_write(cpu, address, size, value);
    }
    if (address < FPB_BASE + 0x1000u) {
        return fpb_write(cpu, address, size, value);
    }
    return tpiu_write(cpu, address, size, value);
}

static void add_counter(uint8_t* counter, uint32_t amount) {
    *counter = (uint8_t)(*counter + amount);
}

static bool cycle_match(uint32_t before, uint32_t after, uint32_t target) {
    if (after >= before) {
        return target > before && target <= after;
    }
    return target > before || target <= after;
}

void cortex_m4_debug_advance(CortexM4* cpu, uint32_t cycles, bool sleeping) {
    if (cpu == NULL || cycles == 0 || (cpu->debug.demcr & DEMCR_TRACE_ENABLE) == 0) {
        return;
    }
    if ((cpu->debug.dwt_control & DWT_CYCLE_COUNT_ENABLE) != 0) {
        const uint32_t before = cpu->debug.dwt_cycle_count;
        cpu->debug.dwt_cycle_count += cycles;
        CortexM4DwtComparator* const comparator = &cpu->debug.dwt_comparators[0];
        if ((comparator->function & 0x8fu) == 0x84u &&
            cycle_match(before, cpu->debug.dwt_cycle_count, comparator->comparator)) {
            comparator->function |= DWT_FUNCTION_MATCHED;
            debug_event(cpu, 1u << 2);
        }
    }
    if (sleeping && (cpu->debug.dwt_control & DWT_SLEEP_COUNT_ENABLE) != 0) {
        add_counter(&cpu->debug.dwt_counters[2], cycles);
    }
}

void cortex_m4_debug_cpi_cycles(CortexM4* cpu, uint32_t cycles) {
    if (cpu != NULL && (cpu->debug.demcr & DEMCR_TRACE_ENABLE) != 0 &&
        (cpu->debug.dwt_control & DWT_CPI_COUNT_ENABLE) != 0) {
        add_counter(&cpu->debug.dwt_counters[0], cycles);
    }
}

void cortex_m4_debug_exception_cycles(CortexM4* cpu, uint32_t cycles) {
    if (cpu != NULL && (cpu->debug.demcr & DEMCR_TRACE_ENABLE) != 0 &&
        (cpu->debug.dwt_control & DWT_EXCEPTION_COUNT_ENABLE) != 0) {
        add_counter(&cpu->debug.dwt_counters[1], cycles);
    }
}

void cortex_m4_debug_lsu_cycles(CortexM4* cpu, uint32_t cycles) {
    if (cpu != NULL && (cpu->debug.demcr & DEMCR_TRACE_ENABLE) != 0 &&
        (cpu->debug.dwt_control & DWT_LSU_COUNT_ENABLE) != 0) {
        add_counter(&cpu->debug.dwt_counters[3], cycles);
    }
}

void cortex_m4_debug_folded_instruction(CortexM4* cpu) {
    if (cpu != NULL && (cpu->debug.demcr & DEMCR_TRACE_ENABLE) != 0 &&
        (cpu->debug.dwt_control & (1u << 21)) != 0) {
        add_counter(&cpu->debug.dwt_counters[4], 1u);
    }
}

bool cortex_m4_debug_execution_allowed(CortexM4* cpu) {
    if (cpu == NULL) {
        return false;
    }
    return !cpu->debug.halted || cpu->debug.step_armed;
}

void cortex_m4_debug_instruction_retired(CortexM4* cpu) {
    if (cpu == NULL) {
        return;
    }
    cpu->debug.retire_sticky = true;
    if (cpu->debug.step_armed) {
        cpu->debug.step_armed = false;
        cpu->debug.halted = true;
        cpu->dfsr |= 1u;
    } else if ((cpu->debug.demcr & (DEMCR_MONITOR_ENABLE | (1u << 18))) ==
               (DEMCR_MONITOR_ENABLE | (1u << 18))) {
        debug_event(cpu, 1u);
    }
}

void cortex_m4_debug_breakpoint(CortexM4* cpu) {
    if (cpu != NULL) {
        debug_event(cpu, 1u << 1);
    }
}

void cortex_m4_debug_exception(CortexM4* cpu, uint16_t exception) {
    if (cpu == NULL) {
        return;
    }
    uint32_t vector_catch = 0;
    if (exception == 1u) {
        vector_catch = 1u;
    } else if (exception == 3u) {
        vector_catch = 1u << 10;
    } else if (exception == 4u) {
        vector_catch = 1u << 4;
    } else if (exception == 5u) {
        vector_catch = 1u << 8;
    } else if (exception == 6u) {
        vector_catch = 7u << 5;
    } else if (exception >= 7u && exception <= 15u) {
        vector_catch = 1u << 9;
    }
    if ((cpu->debug.demcr & vector_catch) != 0) {
        debug_event(cpu, 1u << 3);
    }
}

static bool dwt_address_match(CortexM4DwtComparator* comparator, uint32_t address, uint8_t size,
                              bool check_size) {
    const uint32_t mask = comparator->mask;
    const bool address_match = mask == 31u || (address >> mask) == (comparator->comparator >> mask);
    const uint8_t configured_size = (uint8_t)((comparator->function >> 10) & 3u);
    const bool size_match = !check_size || configured_size == 3u || size == (1u << configured_size);
    if (address_match && size_match) {
        comparator->function |= DWT_FUNCTION_MATCHED;
        return true;
    }
    return false;
}

void cortex_m4_debug_instruction_access(CortexM4* cpu, uint32_t address) {
    if (cpu == NULL || (cpu->debug.demcr & DEMCR_TRACE_ENABLE) == 0) {
        return;
    }
    for (uint8_t index = 0; index < 4u; index++) {
        CortexM4DwtComparator* const comparator = &cpu->debug.dwt_comparators[index];
        if ((comparator->function & 0x0fu) == 4u &&
            dwt_address_match(comparator, address, 2u, false)) {
            debug_event(cpu, 1u << 2);
        }
    }
}

static uint32_t access_mask(uint8_t size) {
    if (size == 1u) {
        return 0xffu;
    }
    if (size == 2u) {
        return 0xffffu;
    }
    return UINT32_MAX;
}

static bool dwt_data_match(CortexM4* cpu, uint8_t index, uint32_t address, uint8_t size,
                           uint32_t value) {
    CortexM4DwtComparator* const comparator = &cpu->debug.dwt_comparators[index];
    if ((comparator->function & (1u << 8)) == 0) {
        return dwt_address_match(comparator, address, size, true);
    }
    const uint8_t address_index = (uint8_t)((comparator->function >> 12) & 0x0fu);
    if (address_index >= 4u ||
        !dwt_address_match(&cpu->debug.dwt_comparators[address_index], address, size, true)) {
        return false;
    }
    if ((value & access_mask(size)) != (comparator->comparator & access_mask(size))) {
        return false;
    }
    comparator->function |= DWT_FUNCTION_MATCHED;
    return true;
}

void cortex_m4_debug_memory_access(CortexM4* cpu, uint32_t address, uint8_t size, bool write,
                                   uint32_t value) {
    if (cpu == NULL || (cpu->debug.demcr & DEMCR_TRACE_ENABLE) == 0) {
        return;
    }
    for (uint8_t index = 0; index < 4u; index++) {
        CortexM4DwtComparator* const comparator = &cpu->debug.dwt_comparators[index];
        const uint8_t function = (uint8_t)(comparator->function & 0x0fu);
        if ((function == 7u || function == (write ? 6u : 5u)) &&
            dwt_data_match(cpu, index, address, size, value)) {
            debug_event(cpu, 1u << 2);
        }
    }
}

bool cortex_m4_debug_remap_instruction(const CortexM4* cpu, uint32_t address,
                                       uint32_t* remapped_address) {
    if (cpu == NULL || remapped_address == NULL || (cpu->debug.fpb_control & FPB_ENABLE) == 0 ||
        address > 0x1fffffffu) {
        return false;
    }
    for (uint8_t index = 0; index < 6u; index++) {
        const uint32_t comparator = cpu->debug.fpb_comparators[index];
        if ((comparator & 1u) == 0 || (address & 0x1ffffffcu) != (comparator & 0x1ffffffcu)) {
            continue;
        }
        const uint8_t replacement = (uint8_t)(comparator >> 30);
        if (replacement == 3u || replacement == ((address & 2u) != 0 ? 2u : 1u)) {
            *remapped_address = cpu->debug.fpb_remap + index * 4u + (address & 2u);
            return true;
        }
    }
    return false;
}

bool cortex_m4_debug_remap_literal(const CortexM4* cpu, uint32_t address,
                                   uint32_t* remapped_address) {
    if (cpu == NULL || remapped_address == NULL || (cpu->debug.fpb_control & FPB_ENABLE) == 0 ||
        address > 0x1fffffffu) {
        return false;
    }
    for (uint8_t index = 6u; index < 8u; index++) {
        const uint32_t comparator = cpu->debug.fpb_comparators[index];
        if ((comparator & 1u) != 0 && (address & 0x1ffffffcu) == (comparator & 0x1ffffffcu)) {
            *remapped_address = cpu->debug.fpb_remap + index * 4u;
            return true;
        }
    }
    return false;
}
