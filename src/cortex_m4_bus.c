#include "cortex_m4_internal.h"

enum {
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
};

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

bool cortex_m4_core_read(CortexM4 *cpu, uint32_t address, uint8_t size,
                         uint32_t *value) {
  if (address < 0xe000e000u || address >= 0xe0100000u) {
    return false;
  }
  const uint32_t aligned = address & ~3u;
  uint32_t data = 0;
  if (aligned == SYSTICK_CONTROL) {
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
  } else if (aligned >= NVIC_CLEAR_PENDING &&
             aligned < NVIC_CLEAR_PENDING + 32) {
    data = cpu->irq_pending[(aligned - NVIC_CLEAR_PENDING) / 4];
  } else if (aligned >= NVIC_ACTIVE && aligned < NVIC_ACTIVE + 32) {
    data = cpu->irq_active[(aligned - NVIC_ACTIVE) / 4];
  } else if (address >= NVIC_PRIORITY &&
             address < NVIC_PRIORITY + CORTEX_M4_IRQ_COUNT) {
    data = cpu->irq_priority[address - NVIC_PRIORITY];
    *value = data;
    return true;
  } else if (aligned == SCB_CPUID) {
    data = 0x410fc241u;
  } else if (aligned == SCB_ICSR) {
    data = cpu->xpsr & 0x1ffu;
    if ((cpu->irq_pending[0] | cpu->irq_pending[1] | cpu->irq_pending[2] |
         cpu->irq_pending[3] | cpu->irq_pending[4] | cpu->irq_pending[5] |
         cpu->irq_pending[6] | cpu->irq_pending[7]) != 0) {
      data |= 1u << 22;
    }
  } else if (aligned == SCB_VTOR) {
    data = cpu->vtor;
  } else if (aligned == SCB_AIRCR) {
    data = cpu->aircr;
  } else if (aligned == SCB_SCR) {
    data = cpu->scr;
  } else if (aligned == SCB_CCR) {
    data = cpu->ccr;
  } else if (address >= SCB_SHPR && address < SCB_SHPR + 12) {
    data = cpu->system_priority[address - SCB_SHPR];
    *value = data;
    return true;
  } else if (aligned == SCB_SHCSR) {
    data = cpu->shcsr;
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

bool cortex_m4_core_write(CortexM4 *cpu, uint32_t address, uint8_t size,
                          uint32_t value) {
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
    cpu->irq_enabled[(aligned - NVIC_ENABLE) / 4] |= value;
  } else if (aligned >= NVIC_CLEAR_ENABLE && aligned < NVIC_CLEAR_ENABLE + 32) {
    cpu->irq_enabled[(aligned - NVIC_CLEAR_ENABLE) / 4] &= ~value;
  } else if (aligned >= NVIC_PENDING && aligned < NVIC_PENDING + 32) {
    cpu->irq_pending[(aligned - NVIC_PENDING) / 4] |= value;
  } else if (aligned >= NVIC_CLEAR_PENDING &&
             aligned < NVIC_CLEAR_PENDING + 32) {
    cpu->irq_pending[(aligned - NVIC_CLEAR_PENDING) / 4] &= ~value;
  } else if (address >= NVIC_PRIORITY &&
             address < NVIC_PRIORITY + CORTEX_M4_IRQ_COUNT) {
    cpu->irq_priority[address - NVIC_PRIORITY] = (uint8_t)value;
  } else if (aligned == SCB_ICSR) {
    if ((value & (1u << 28)) != 0) {
      cpu->event_register = true;
    }
  } else if (aligned == SCB_VTOR) {
    cpu->vtor = value & 0xffffff80u;
  } else if (aligned == SCB_AIRCR) {
    if ((value >> 16) == 0x05fau) {
      cpu->aircr = value & 0x00000700u;
    }
  } else if (aligned == SCB_SCR) {
    cpu->scr = write_partial(cpu->scr, address, size, value) & 0x1eu;
  } else if (aligned == SCB_CCR) {
    cpu->ccr = write_partial(cpu->ccr, address, size, value) & 0x0007031bu;
  } else if (address >= SCB_SHPR && address < SCB_SHPR + 12) {
    cpu->system_priority[address - SCB_SHPR] = (uint8_t)value;
  } else if (aligned == SCB_SHCSR) {
    cpu->shcsr = write_partial(cpu->shcsr, address, size, value);
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
  }
  return true;
}

bool cortex_m4_bus_read(CortexM4 *cpu, uint32_t address, uint8_t size,
                        CortexM4Access access, uint32_t *value) {
  if (value == NULL || (size != 1 && size != 2 && size != 4)) {
    return false;
  }
  if (cortex_m4_core_read(cpu, address, size, value)) {
    return true;
  }
  return cpu->bus.read(cpu->bus.context, address, size, access, value);
}

bool cortex_m4_bus_write(CortexM4 *cpu, uint32_t address, uint8_t size,
                         CortexM4Access access, uint32_t value) {
  if (size != 1 && size != 2 && size != 4) {
    return false;
  }
  if (cortex_m4_core_write(cpu, address, size, value)) {
    cpu->exclusive_valid = false;
    return true;
  }
  const bool written =
      cpu->bus.write(cpu->bus.context, address, size, access, value);
  if (written && cpu->exclusive_valid &&
      address < cpu->exclusive_address + cpu->exclusive_size &&
      cpu->exclusive_address < address + size) {
    cpu->exclusive_valid = false;
  }
  return written;
}

void cortex_m4_advance(CortexM4 *cpu, uint32_t cycles) {
  for (uint32_t index = 0; index < cycles; index++) {
    if ((cpu->systick_control & 1u) != 0) {
      if (cpu->systick_current == 0) {
        cpu->systick_current = cpu->systick_reload;
        cpu->systick_control |= 1u << 16;
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
