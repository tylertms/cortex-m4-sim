#include "cortex_m4_internal.h"

enum {
  EXCEPTION_RETURN_HANDLER_MSP = 0xfffffff1u,
  EXCEPTION_RETURN_THREAD_MSP = 0xfffffff9u,
  EXCEPTION_RETURN_THREAD_PSP = 0xfffffffdu,
};

static bool write_stack_word(CortexM4 *cpu, uint32_t address, uint32_t value) {
  return cortex_m4_bus_write(cpu, address, 4, CORTEX_M4_ACCESS_DATA, value);
}

static bool read_stack_word(CortexM4 *cpu, uint32_t address, uint32_t *value) {
  return cortex_m4_bus_read(cpu, address, 4, CORTEX_M4_ACCESS_DATA, value);
}

static bool exception_is_masked(const CortexM4 *cpu, uint8_t priority) {
  if (cpu->faultmask != 0 || cpu->primask != 0) {
    return true;
  }
  return cpu->basepri != 0 && priority >= cpu->basepri;
}

static uint8_t current_priority(const CortexM4 *cpu) {
  const uint16_t exception = (uint16_t)(cpu->xpsr & 0x1ffu);
  if (exception < 16) {
    return exception == 0 ? 0xffu : 0;
  }
  return cpu->irq_priority[exception - 16];
}

static bool enter_exception(CortexM4 *cpu, uint16_t exception) {
  const bool was_thread = (cpu->xpsr & 0x1ffu) == 0;
  const bool used_psp =
      was_thread && (cpu->control & CORTEX_M4_CONTROL_SPSEL) != 0;
  uint32_t stack_pointer = used_psp ? cpu->psp : cpu->msp;
  const bool align_padding =
      (cpu->ccr & (1u << 9)) != 0 && (stack_pointer & 7u) != 0;
  if (align_padding) {
    stack_pointer -= 4;
  }
  stack_pointer -= 32;
  uint32_t stacked_xpsr = cpu->xpsr;
  if (align_padding) {
    stacked_xpsr |= 1u << 9;
  }
  const uint32_t frame[8] = {
      cpu->registers[0],  cpu->registers[1],  cpu->registers[2],
      cpu->registers[3],  cpu->registers[12], cpu->registers[14],
      cpu->registers[15], stacked_xpsr,
  };
  for (uint8_t index = 0; index < 8; index++) {
    if (!write_stack_word(cpu, stack_pointer + index * 4u, frame[index])) {
      cpu->stop = CORTEX_M4_STOP_LOCKUP;
      return false;
    }
  }
  if (used_psp) {
    cpu->psp = stack_pointer;
  } else {
    cpu->msp = stack_pointer;
  }
  uint32_t vector = 0;
  if (!cortex_m4_bus_read(cpu, cpu->vtor + exception * 4u, 4,
                          CORTEX_M4_ACCESS_INSTRUCTION, &vector) ||
      (vector & 1u) == 0) {
    cpu->stop = CORTEX_M4_STOP_LOCKUP;
    return false;
  }
  cpu->registers[14] = !was_thread ? EXCEPTION_RETURN_HANDLER_MSP
                       : used_psp  ? EXCEPTION_RETURN_THREAD_PSP
                                   : EXCEPTION_RETURN_THREAD_MSP;
  cpu->registers[15] = vector & ~1u;
  cpu->xpsr = (cpu->xpsr & ~0x1ffu) | exception | CORTEX_M4_XPSR_T;
  cpu->control &= ~CORTEX_M4_CONTROL_FPCA;
  cpu->sleeping = false;
  cpu->exclusive_valid = false;
  cpu->exception_depth++;
  if (exception >= 16) {
    const uint16_t irq = exception - 16;
    const uint32_t mask = 1u << (irq & 31u);
    cpu->irq_pending[irq / 32] &= ~mask;
    cpu->irq_active[irq / 32] |= mask;
  } else {
    cpu->system_pending &= ~(1u << exception);
  }
  return true;
}

bool cortex_m4_take_pending_exception(CortexM4 *cpu) {
  uint16_t selected_exception = 0;
  uint8_t selected_priority = current_priority(cpu);
  for (uint16_t irq = 0; irq < CORTEX_M4_IRQ_COUNT; irq++) {
    const uint32_t mask = 1u << (irq & 31u);
    if ((cpu->irq_pending[irq / 32] & cpu->irq_enabled[irq / 32] & mask) == 0) {
      continue;
    }
    const uint8_t priority = cpu->irq_priority[irq];
    if (!exception_is_masked(cpu, priority) && priority < selected_priority) {
      selected_exception = irq + 16;
      selected_priority = priority;
    }
  }
  if (selected_exception == 0) {
    return false;
  }
  return enter_exception(cpu, selected_exception);
}

bool cortex_m4_exception_return(CortexM4 *cpu, uint32_t value) {
  if (value != EXCEPTION_RETURN_HANDLER_MSP &&
      value != EXCEPTION_RETURN_THREAD_MSP &&
      value != EXCEPTION_RETURN_THREAD_PSP) {
    return false;
  }
  const uint16_t current_exception = (uint16_t)(cpu->xpsr & 0x1ffu);
  const bool use_psp = (value & 4u) != 0;
  uint32_t stack_pointer = use_psp ? cpu->psp : cpu->msp;
  uint32_t frame[8];
  for (uint8_t index = 0; index < 8; index++) {
    if (!read_stack_word(cpu, stack_pointer + index * 4u, &frame[index])) {
      cpu->stop = CORTEX_M4_STOP_LOCKUP;
      return true;
    }
  }
  stack_pointer += 32;
  if ((frame[7] & (1u << 9)) != 0) {
    stack_pointer += 4;
  }
  if (use_psp) {
    cpu->psp = stack_pointer;
  } else {
    cpu->msp = stack_pointer;
  }
  cpu->registers[0] = frame[0];
  cpu->registers[1] = frame[1];
  cpu->registers[2] = frame[2];
  cpu->registers[3] = frame[3];
  cpu->registers[12] = frame[4];
  cpu->registers[14] = frame[5];
  cpu->registers[15] = frame[6] & ~1u;
  cpu->xpsr = frame[7] | CORTEX_M4_XPSR_T;
  if (current_exception >= 16) {
    const uint16_t irq = current_exception - 16;
    const uint32_t mask = 1u << (irq & 31u);
    cpu->irq_active[irq / 32] &= ~mask;
    if ((cpu->irq_level[irq / 32] & mask) != 0) {
      cpu->irq_pending[irq / 32] |= mask;
    }
  }
  if (cpu->exception_depth != 0) {
    cpu->exception_depth--;
  }
  return true;
}

void cortex_m4_set_irq(CortexM4 *cpu, uint16_t irq, bool pending) {
  if (cpu == NULL || irq >= CORTEX_M4_IRQ_COUNT) {
    return;
  }
  const uint32_t mask = 1u << (irq & 31u);
  if (pending) {
    cpu->irq_pending[irq / 32] |= mask;
    cpu->event_register = true;
    cpu->sleeping = false;
  } else {
    cpu->irq_pending[irq / 32] &= ~mask;
  }
}

void cortex_m4_set_irq_level(CortexM4 *cpu, uint16_t irq, bool asserted) {
  if (cpu == NULL || irq >= CORTEX_M4_IRQ_COUNT) {
    return;
  }
  const uint32_t mask = 1u << (irq & 31u);
  if (asserted) {
    cpu->irq_level[irq / 32] |= mask;
    cortex_m4_set_irq(cpu, irq, true);
  } else {
    cpu->irq_level[irq / 32] &= ~mask;
  }
}

bool cortex_m4_get_irq_pending(const CortexM4 *cpu, uint16_t irq) {
  if (cpu == NULL || irq >= CORTEX_M4_IRQ_COUNT) {
    return false;
  }
  return (cpu->irq_pending[irq / 32] & (1u << (irq & 31u))) != 0;
}

bool cortex_m4_get_irq_active(const CortexM4 *cpu, uint16_t irq) {
  if (cpu == NULL || irq >= CORTEX_M4_IRQ_COUNT) {
    return false;
  }
  return (cpu->irq_active[irq / 32] & (1u << (irq & 31u))) != 0;
}
