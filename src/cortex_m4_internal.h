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
    CORTEX_M4_EXCEPTION_FRAME_LIMIT = 16,
    CORTEX_M4_MPU_REGION_COUNT = 8,
};

typedef enum {
    CORTEX_M4_SYSTEM_ACCESS_OUTSIDE,
    CORTEX_M4_SYSTEM_ACCESS_ACCEPTED,
    CORTEX_M4_SYSTEM_ACCESS_REJECTED,
} CortexM4SystemAccess;

typedef enum {
    CORTEX_M4_INSTRUCTION_EXECUTE,
    CORTEX_M4_INSTRUCTION_UNDEFINED,
    CORTEX_M4_INSTRUCTION_BREAKPOINT,
} CortexM4InstructionDisposition;

typedef enum {
    CORTEX_M4_FLAGS_UNCHANGED,
    CORTEX_M4_FLAGS_IMPLICIT,
    CORTEX_M4_FLAGS_EXPLICIT,
} CortexM4FlagWrite;

typedef struct {
    uint8_t preemption;
    uint8_t subpriority;
} CortexM4Priority;

typedef struct {
    uint32_t address;
    uint32_t ici_address;
    uint32_t return_value;
    uint32_t stacked_xpsr;
    uint8_t it_state;
    uint8_t ici_register;
    bool extended;
    bool lazy;
    bool ici_valid;
} CortexM4ExceptionFrame;

typedef enum {
    CORTEX_M4_TIMING_EXCEPTION_ENTRY,
    CORTEX_M4_TIMING_EXCEPTION_LATE_ARRIVAL,
    CORTEX_M4_TIMING_EXCEPTION_FP_ENTRY,
    CORTEX_M4_TIMING_EXCEPTION_RETURN,
    CORTEX_M4_TIMING_EXCEPTION_FP_RETURN,
    CORTEX_M4_TIMING_EXCEPTION_TAIL_CHAIN,
} CortexM4ExceptionTiming;

typedef enum {
    CORTEX_M4_EXCEPTION_CHAIN_NONE,
    CORTEX_M4_EXCEPTION_CHAIN_TAKEN,
    CORTEX_M4_EXCEPTION_CHAIN_FAULT,
} CortexM4ExceptionChain;

typedef enum {
    CORTEX_M4_FAULT_STACKING,
    CORTEX_M4_FAULT_UNSTACKING,
    CORTEX_M4_FAULT_LAZY_FP,
} CortexM4ExceptionFaultStage;

typedef enum {
    CORTEX_M4_TIMING_BARRIER_DMB,
    CORTEX_M4_TIMING_BARRIER_DSB,
    CORTEX_M4_TIMING_BARRIER_ISB,
} CortexM4Barrier;

typedef enum {
    CORTEX_M4_TIMING_BUS_ICODE,
    CORTEX_M4_TIMING_BUS_DCODE,
    CORTEX_M4_TIMING_BUS_SYSTEM,
    CORTEX_M4_TIMING_BUS_PPB,
} CortexM4TimingBus;

typedef struct {
    uint32_t comparator;
    uint32_t mask;
    uint32_t function;
} CortexM4DwtComparator;

typedef struct {
    uint32_t dhcsr_control;
    uint32_t dcrsr;
    uint32_t dcrdr;
    uint32_t demcr;
    uint32_t dwt_control;
    uint32_t dwt_cycle_count;
    uint8_t dwt_counters[5];
    CortexM4DwtComparator dwt_comparators[4];
    uint32_t fpb_control;
    uint32_t fpb_remap;
    uint32_t fpb_comparators[8];
    uint32_t itm_trace_control;
    uint32_t itm_trace_enable;
    uint32_t itm_trace_privilege;
    uint32_t itm_integration_write;
    uint32_t itm_integration_read;
    uint32_t itm_integration_mode;
    uint32_t itm_stimulus[32];
    uint32_t tpiu_current_port_size;
    uint32_t tpiu_async_prescaler;
    uint32_t tpiu_selected_protocol;
    uint32_t tpiu_formatter_control;
    bool itm_locked;
    bool dwt_locked;
    bool fpb_locked;
    bool tpiu_locked;
    bool halted;
    bool step_armed;
    bool retire_sticky;
    bool reset_sticky;
} CortexM4Debug;

struct CortexM4 {
    CortexM4Bus bus;
    CortexM4Trace trace;
    void* trace_context;
    CortexM4WaitStates wait_states;
    void* wait_state_context;
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
    uint32_t fpccr;
    uint32_t fpcar;
    uint32_t fpdscr;
    uint32_t mpu_control;
    uint32_t mpu_region_number;
    uint32_t mpu_region_base[CORTEX_M4_MPU_REGION_COUNT];
    uint32_t mpu_region_attributes[CORTEX_M4_MPU_REGION_COUNT];
    uint32_t dfsr;
    uint32_t afsr;
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
    uint32_t exclusive_reservation_base;
    uint32_t timing_instruction_wait_cycles;
    uint32_t timing_instruction_store_cycles;
    uint32_t timing_pending_store_cycles;
    uint32_t timing_last_access_address;
    uint32_t timing_memory_epoch;
    uint32_t timing_context_epoch;
    uint32_t timing_prepared_cycles;
    CortexM4Access timing_last_access_type;
    uint8_t exclusive_size;
    CortexM4TimingBus timing_instruction_store_bus;
    CortexM4TimingBus timing_pending_store_bus;
    uint32_t exclusive_granule;
    uint8_t exception_depth;
    uint8_t exception_frame_depth;
    uint8_t it_state;
    uint8_t breakpoint_enabled;
    bool exclusive_valid;
    bool timing_instruction_active;
    bool timing_exception_active;
    bool timing_last_access_valid;
    bool timing_last_access_write;
    bool timing_prepared;
    bool event_register;
    bool sleeping;
    bool reset_requested;
    bool stop_requested;
    CortexM4Stop stop;
    uint64_t instructions;
    uint64_t cycles;
    uint32_t current_opcode;
    CortexM4ExceptionFrame exception_frames[CORTEX_M4_EXCEPTION_FRAME_LIMIT];
    uint16_t active_exceptions[CORTEX_M4_EXCEPTION_FRAME_LIMIT];
    uint32_t ici_address;
    uint8_t ici_register;
    bool ici_valid;
    bool exception_unstack_memory_fault;
    CortexM4Debug debug;
};

uint32_t cortex_m4_read_register_internal(const CortexM4* cpu, uint8_t index);
void cortex_m4_write_register_internal(CortexM4* cpu, uint8_t index, uint32_t value);
bool cortex_m4_bus_read(CortexM4* cpu, uint32_t address, uint8_t size,
                        CortexM4Access access, uint32_t* value);
bool cortex_m4_bus_write(CortexM4* cpu, uint32_t address, uint8_t size,
                         CortexM4Access access, uint32_t value);
bool cortex_m4_data_read(CortexM4* cpu, uint32_t address, uint8_t size,
                         CortexM4Access access, uint32_t* value);
bool cortex_m4_data_write(CortexM4* cpu, uint32_t address, uint8_t size,
                          CortexM4Access access, uint32_t value);
bool cortex_m4_require_alignment(CortexM4* cpu, uint32_t address, uint8_t alignment);
bool cortex_m4_core_read(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t* value);
bool cortex_m4_core_write(CortexM4* cpu, uint32_t address, uint8_t size, uint32_t value);
void cortex_m4_advance(CortexM4* cpu, uint32_t cycles);
bool cortex_m4_take_pending_exception(CortexM4* cpu);
bool cortex_m4_exception_return(CortexM4* cpu, uint32_t value);
void cortex_m4_raise_fault(CortexM4* cpu, uint8_t exception);
void cortex_m4_exception_advanced_reset(CortexM4* cpu);
uint16_t cortex_m4_exception_advanced_late_arrival(CortexM4* cpu,
                                                   uint16_t entering_exception);
CortexM4ExceptionChain cortex_m4_exception_advanced_tail_chain(
    CortexM4* cpu, uint32_t exception_return, uint16_t current_exception);
void cortex_m4_exception_advanced_commit_entry(CortexM4* cpu, uint16_t exception);
void cortex_m4_exception_advanced_commit_return(CortexM4* cpu,
                                                uint16_t current_exception,
                                                bool return_thread);
bool cortex_m4_exception_advanced_active(const CortexM4* cpu, uint16_t exception);
bool cortex_m4_exception_advanced_valid_return(const CortexM4* cpu, uint32_t value);
bool cortex_m4_exception_advanced_valid_stacked_xpsr(const CortexM4* cpu,
                                                     uint32_t value,
                                                     uint32_t exception_return);
void cortex_m4_exception_advanced_fault(CortexM4* cpu,
                                        CortexM4ExceptionFaultStage stage,
                                        bool memory_management);
void cortex_m4_exception_advanced_entry_fault(
    CortexM4* cpu, uint16_t entering_exception,
    CortexM4ExceptionFaultStage stage, bool memory_management);
void cortex_m4_exception_advanced_vector_fault(CortexM4* cpu);
bool cortex_m4_exception_advanced_hardfault_vector(CortexM4* cpu,
                                                   uint32_t* vector);
void cortex_m4_exception_advanced_imprecise_fault(CortexM4* cpu);
uint8_t cortex_m4_exception_advanced_multiple_resume(const CortexM4* cpu);
uint32_t cortex_m4_exception_advanced_multiple_address(const CortexM4* cpu,
                                                       uint32_t initial_address);
bool cortex_m4_exception_advanced_multiple_suspend(CortexM4* cpu,
                                                   uint8_t next_register,
                                                   uint8_t instruction_size,
                                                   uint32_t next_address);
void cortex_m4_exception_advanced_multiple_complete(CortexM4* cpu);
uint32_t cortex_m4_exception_advanced_xpsr(const CortexM4* cpu, uint32_t value);
void cortex_m4_exception_advanced_load_xpsr(CortexM4* cpu, uint32_t value);
bool cortex_m4_execute_thumb16(CortexM4* cpu, uint16_t opcode);
bool cortex_m4_execute_thumb32(CortexM4* cpu, uint16_t first, uint16_t second);
bool cortex_m4_execute_remaining(CortexM4* cpu, uint16_t first, uint16_t second);
bool cortex_m4_execute_dsp(CortexM4* cpu, uint16_t first, uint16_t second);
bool cortex_m4_execute_thumb32_extra(CortexM4* cpu, uint16_t first, uint16_t second);
bool cortex_m4_execute_fpu(CortexM4* cpu, uint16_t first, uint16_t second);
CortexM4InstructionDisposition cortex_m4_check_instruction_constraints(const CortexM4* cpu,
                                                                       uint16_t first,
                                                                       uint16_t second,
                                                                       bool wide);
void cortex_m4_set_nz(CortexM4* cpu, uint32_t value);
void cortex_m4_set_nzcv(CortexM4* cpu, uint32_t value, bool carry, bool overflow);
bool cortex_m4_condition_passed(const CortexM4* cpu, uint8_t condition);
CortexM4FlagWrite cortex_m4_it_flag_write(uint16_t first, uint16_t second,
                                           bool wide);
bool cortex_m4_it_condition_passed(const CortexM4* cpu);
void cortex_m4_it_advance(CortexM4* cpu);
void cortex_m4_it_preserve_flags(CortexM4* cpu, uint16_t first, uint16_t second,
                                 bool wide, bool in_it_block,
                                 uint32_t previous_xpsr);
uint32_t cortex_m4_xpsr_value(const CortexM4* cpu);
void cortex_m4_load_xpsr(CortexM4* cpu, uint32_t value);
uint32_t cortex_m4_add_with_carry(uint32_t left, uint32_t right, bool carry,
                                  bool* carry_out, bool* overflow_out);
uint32_t cortex_m4_shift(uint32_t value, uint8_t type, uint32_t amount, bool carry_in,
                         bool* carry_out);
void cortex_m4_system_reset(CortexM4* cpu);
CortexM4SystemAccess cortex_m4_system_read(CortexM4* cpu, uint32_t address, uint8_t size,
                                           uint32_t* value);
CortexM4SystemAccess cortex_m4_system_write(CortexM4* cpu, uint32_t address, uint8_t size,
                                            uint32_t value);
CortexM4Priority cortex_m4_system_priority(const CortexM4* cpu, uint8_t priority);
bool cortex_m4_system_exception_masked(const CortexM4* cpu, uint16_t exception);
bool cortex_m4_system_exception_can_preempt(const CortexM4* cpu, uint16_t candidate,
                                            uint16_t current);
bool cortex_m4_system_exception_before(const CortexM4* cpu, uint16_t left, uint16_t right);
void cortex_m4_system_set_pending(CortexM4* cpu, uint16_t exception, bool pending);
void cortex_m4_system_wait_for_interrupt(CortexM4* cpu);
bool cortex_m4_system_stack_exception_frame(CortexM4* cpu, uint32_t* stack_pointer,
                                            uint32_t* return_value);
bool cortex_m4_system_materialize_lazy_fp(CortexM4* cpu);
bool cortex_m4_system_valid_exception_return(const CortexM4* cpu, uint32_t value);
bool cortex_m4_system_unstack_exception_frame(CortexM4* cpu, uint32_t* stack_pointer,
                                              uint32_t value, uint16_t current_exception);
void cortex_m4_timing_reset(CortexM4* cpu);
void cortex_m4_timing_begin_instruction(CortexM4* cpu);
void cortex_m4_timing_begin_exception(CortexM4* cpu);
void cortex_m4_timing_abort(CortexM4* cpu);
void cortex_m4_timing_prepare_instruction(CortexM4* cpu, uint16_t first, uint16_t second,
                                          bool wide);
void cortex_m4_timing_access(CortexM4* cpu, uint32_t address, uint8_t size,
                             CortexM4Access access, bool write);
void cortex_m4_timing_complete_instruction(CortexM4* cpu, uint16_t first, uint16_t second,
                                           bool wide, bool executed,
                                           uint32_t sequential_pc);
void cortex_m4_timing_exception(CortexM4* cpu, CortexM4ExceptionTiming transition);
void cortex_m4_timing_barrier(CortexM4* cpu, CortexM4Barrier barrier);
void cortex_m4_timing_sleep(CortexM4* cpu, uint32_t cycles);
void cortex_m4_timing_reserve(CortexM4* cpu, uint32_t address, uint8_t size);
bool cortex_m4_timing_consume_reservation(CortexM4* cpu, uint32_t address, uint8_t size);
void cortex_m4_timing_observe_write(CortexM4* cpu, uint32_t address, uint32_t size);
uint32_t cortex_m4_timing_divide_cycles(uint32_t dividend, uint32_t divisor,
                                        bool signed_divide);
void cortex_m4_mpu_reset(CortexM4* cpu);
CortexM4SystemAccess cortex_m4_mpu_read(CortexM4* cpu, uint32_t address, uint8_t size,
                                        CortexM4Access access, uint32_t* value);
CortexM4SystemAccess cortex_m4_mpu_write(CortexM4* cpu, uint32_t address, uint8_t size,
                                         CortexM4Access access, uint32_t value);
bool cortex_m4_mpu_access_permitted(const CortexM4* cpu, uint32_t address, uint8_t size,
                                    CortexM4Access access, bool write);
bool cortex_m4_mpu_check(CortexM4* cpu, uint32_t address, uint8_t size,
                         CortexM4Access access, bool write);
void cortex_m4_debug_reset(CortexM4* cpu);
CortexM4SystemAccess cortex_m4_debug_read(CortexM4* cpu, uint32_t address, uint8_t size,
                                          uint32_t* value);
CortexM4SystemAccess cortex_m4_debug_write(CortexM4* cpu, uint32_t address, uint8_t size,
                                           uint32_t value);
void cortex_m4_debug_advance(CortexM4* cpu, uint32_t cycles, bool sleeping);
void cortex_m4_debug_cpi_cycles(CortexM4* cpu, uint32_t cycles);
void cortex_m4_debug_exception_cycles(CortexM4* cpu, uint32_t cycles);
void cortex_m4_debug_lsu_cycles(CortexM4* cpu, uint32_t cycles);
void cortex_m4_debug_folded_instruction(CortexM4* cpu);
bool cortex_m4_debug_execution_allowed(CortexM4* cpu);
void cortex_m4_debug_instruction_retired(CortexM4* cpu);
void cortex_m4_debug_breakpoint(CortexM4* cpu);
void cortex_m4_debug_exception(CortexM4* cpu, uint16_t exception);
void cortex_m4_debug_instruction_access(CortexM4* cpu, uint32_t address);
void cortex_m4_debug_memory_access(CortexM4* cpu, uint32_t address, uint8_t size,
                                   bool write, uint32_t value);
bool cortex_m4_debug_remap_instruction(const CortexM4* cpu, uint32_t address,
                                       uint32_t* remapped_address);
bool cortex_m4_debug_remap_literal(const CortexM4* cpu, uint32_t address,
                                   uint32_t* remapped_address);

#endif
