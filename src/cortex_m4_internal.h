#ifndef CORTEX_M4_INTERNAL_H
#define CORTEX_M4_INTERNAL_H

#include "cortex_m4_sim/cortex_m4.h"

enum {
    CORTEX_M4_REGISTER_COUNT = 16,
    CORTEX_M4_FP_REGISTER_COUNT = 32,
    CORTEX_M4_IRQ_COUNT = 240,
    CORTEX_M4_IRQ_WORD_COUNT = (CORTEX_M4_IRQ_COUNT + 31) / 32,
    CORTEX_M4_XPSR_N = 1u << 31,
    CORTEX_M4_XPSR_Z = 1u << 30,
    CORTEX_M4_XPSR_C = 1u << 29,
    CORTEX_M4_XPSR_V = 1u << 28,
    CORTEX_M4_XPSR_Q = 1u << 27,
    CORTEX_M4_XPSR_T = 1u << 24,
    CORTEX_M4_CONTROL_NPRIV = 1u << 0,
    CORTEX_M4_CONTROL_SPSEL = 1u << 1,
    CORTEX_M4_CONTROL_FPCA = 1u << 2,
};

struct CortexM4 {
    CortexM4Bus bus;
    CortexM4Trace trace;
    void* trace_context;
    uint32_t registers[CORTEX_M4_REGISTER_COUNT];
    uint32_t msp;
    uint32_t psp;
    uint32_t xpsr;
    uint32_t control;
    uint32_t primask;
    uint32_t basepri;
    uint32_t faultmask;
    uint32_t fpscr;
    uint32_t fp_registers[CORTEX_M4_FP_REGISTER_COUNT];
    uint32_t breakpoints[8];
    uint32_t vtor;
    uint32_t aircr;
    uint32_t scr;
    uint32_t ccr;
    uint32_t shcsr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t cpacr;
    uint32_t systick_control;
    uint32_t systick_reload;
    uint32_t systick_current;
    uint32_t systick_calibration;
    uint32_t irq_enabled[CORTEX_M4_IRQ_WORD_COUNT];
    uint32_t irq_pending[CORTEX_M4_IRQ_WORD_COUNT];
    uint32_t irq_active[CORTEX_M4_IRQ_WORD_COUNT];
    uint32_t irq_level[CORTEX_M4_IRQ_WORD_COUNT];
    uint32_t system_pending;
    uint8_t irq_priority[CORTEX_M4_IRQ_COUNT];
    uint8_t system_priority[12];
    uint32_t exclusive_address;
    uint8_t exclusive_size;
    uint8_t exception_depth;
    uint8_t it_state;
    uint8_t breakpoint_enabled;
    bool exclusive_valid;
    bool event_register;
    bool sleeping;
    bool reset_requested;
    bool stop_requested;
    CortexM4Stop stop;
    uint64_t instructions;
    uint64_t cycles;
    uint32_t current_opcode;
};

uint32_t cortex_m4_read_register_internal(const CortexM4* cpu, uint8_t index);
void cortex_m4_write_register_internal(CortexM4* cpu, uint8_t index, uint32_t value);
bool cortex_m4_bus_read(CortexM4* cpu, uint32_t address, uint8_t size,
                        CortexM4Access access, uint32_t* value);
bool cortex_m4_bus_write(CortexM4* cpu, uint32_t address, uint8_t size,
                         CortexM4Access access, uint32_t value);
bool cortex_m4_core_read(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t* value);
bool cortex_m4_core_write(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t value);
void cortex_m4_advance(CortexM4* cpu, uint32_t cycles);
bool cortex_m4_take_pending_exception(CortexM4* cpu);
bool cortex_m4_exception_return(CortexM4* cpu, uint32_t value);
void cortex_m4_raise_fault(CortexM4* cpu, uint8_t exception);
bool cortex_m4_execute_thumb16(CortexM4* cpu, uint16_t opcode);
bool cortex_m4_execute_thumb32(CortexM4* cpu, uint16_t first, uint16_t second);
bool cortex_m4_execute_fpu(CortexM4* cpu, uint16_t first, uint16_t second);
void cortex_m4_set_nz(CortexM4* cpu, uint32_t value);
void cortex_m4_set_nzcv(CortexM4* cpu, uint32_t value, bool carry, bool overflow);
bool cortex_m4_condition_passed(const CortexM4* cpu, uint8_t condition);
uint32_t cortex_m4_xpsr_value(const CortexM4* cpu);
void cortex_m4_load_xpsr(CortexM4* cpu, uint32_t value);
uint32_t cortex_m4_add_with_carry(uint32_t left, uint32_t right, bool carry,
                                  bool* carry_out, bool* overflow_out);
uint32_t cortex_m4_shift(uint32_t value, uint8_t type, uint32_t amount, bool carry_in,
                         bool* carry_out);

#endif
