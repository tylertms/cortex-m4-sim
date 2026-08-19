#ifndef CORTEX_M4_SIM_CORTEX_M4_H
#define CORTEX_M4_SIM_CORTEX_M4_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CortexM4 CortexM4;

typedef enum {
    CORTEX_M4_STOP_RUNNING,
    CORTEX_M4_STOP_LIMIT,
    CORTEX_M4_STOP_BREAKPOINT,
    CORTEX_M4_STOP_LOCKUP,
    CORTEX_M4_STOP_UNSUPPORTED,
    CORTEX_M4_STOP_BUS_FAULT,
    CORTEX_M4_STOP_USAGE_FAULT,
} CortexM4Stop;

typedef enum {
    CORTEX_M4_ACCESS_INSTRUCTION,
    CORTEX_M4_ACCESS_DATA,
    CORTEX_M4_ACCESS_DEBUG,
} CortexM4Access;

typedef bool (*CortexM4Read)(void* context, uint32_t address, uint8_t size,
                             CortexM4Access access, uint32_t* value);
typedef bool (*CortexM4Write)(void* context, uint32_t address, uint8_t size,
                              CortexM4Access access, uint32_t value);
typedef void (*CortexM4Advance)(void* context, uint32_t cycles);
typedef void (*CortexM4Reset)(void* context);
typedef void (*CortexM4Trace)(void* context, uint32_t address, uint32_t opcode,
                              bool executed);

typedef struct {
    void* context;
    CortexM4Read read;
    CortexM4Write write;
    CortexM4Advance advance;
    CortexM4Reset reset;
} CortexM4Bus;

typedef struct {
    uint64_t instruction_limit;
    uint64_t cycle_limit;
} CortexM4RunLimits;

typedef struct {
    CortexM4Stop stop;
    uint64_t instructions;
    uint64_t cycles;
    uint32_t pc;
    uint32_t opcode;
} CortexM4Result;

CortexM4* cortex_m4_create(CortexM4Bus bus);
void cortex_m4_destroy(CortexM4* cpu);
bool cortex_m4_copy(CortexM4* destination, const CortexM4* source);
bool cortex_m4_reset(CortexM4* cpu, uint32_t vector_table_address);
CortexM4Result cortex_m4_step(CortexM4* cpu);
CortexM4Result cortex_m4_run(CortexM4* cpu, CortexM4RunLimits limits);
void cortex_m4_request_stop(CortexM4* cpu);
bool cortex_m4_set_breakpoint(CortexM4* cpu, uint8_t index, uint32_t address, bool enabled);
void cortex_m4_set_trace(CortexM4* cpu, CortexM4Trace trace, void* context);

uint32_t cortex_m4_get_register(const CortexM4* cpu, uint8_t index);
void cortex_m4_set_register(CortexM4* cpu, uint8_t index, uint32_t value);
uint32_t cortex_m4_get_xpsr(const CortexM4* cpu);
void cortex_m4_set_xpsr(CortexM4* cpu, uint32_t value);
uint32_t cortex_m4_get_control(const CortexM4* cpu);
void cortex_m4_set_control(CortexM4* cpu, uint32_t value);
uint32_t cortex_m4_get_fault_status(const CortexM4* cpu);
uint32_t cortex_m4_get_fault_address(const CortexM4* cpu);
uint64_t cortex_m4_get_instruction_count(const CortexM4* cpu);
uint64_t cortex_m4_get_cycle_count(const CortexM4* cpu);
CortexM4Stop cortex_m4_get_stop(const CortexM4* cpu);

uint32_t cortex_m4_get_fp_register(const CortexM4* cpu, uint8_t index);
void cortex_m4_set_fp_register(CortexM4* cpu, uint8_t index, uint32_t value);
uint32_t cortex_m4_get_fpscr(const CortexM4* cpu);
void cortex_m4_set_fpscr(CortexM4* cpu, uint32_t value);

void cortex_m4_set_irq(CortexM4* cpu, uint16_t irq, bool pending);
void cortex_m4_set_irq_level(CortexM4* cpu, uint16_t irq, bool asserted);
bool cortex_m4_get_irq_pending(const CortexM4* cpu, uint16_t irq);
bool cortex_m4_get_irq_active(const CortexM4* cpu, uint16_t irq);

bool cortex_m4_read_memory(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t* value);
bool cortex_m4_write_memory(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t value);

#endif
