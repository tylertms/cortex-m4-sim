#include "cortex_m4_internal.h"

static const uint32_t xpsr_nzcv = UINT32_C(0xf0000000);

static CortexM4FlagWrite narrow_flag_write(uint16_t opcode) {
    if ((opcode & 0xe000u) == 0x0000u) {
        return CORTEX_M4_FLAGS_IMPLICIT;
    }
    if ((opcode & 0xe000u) == 0x2000u) {
        return (opcode & 0x1800u) == 0x0800u ? CORTEX_M4_FLAGS_EXPLICIT
                                             : CORTEX_M4_FLAGS_IMPLICIT;
    }
    if ((opcode & 0xfc00u) == 0x4000u) {
        const uint8_t operation = (uint8_t)((opcode >> 6) & 15u);
        return operation == 8u || operation == 10u || operation == 11u
                   ? CORTEX_M4_FLAGS_EXPLICIT
                   : CORTEX_M4_FLAGS_IMPLICIT;
    }
    if ((opcode & 0xfc00u) == 0x4400u && (opcode & 0x0300u) == 0x0100u) {
        return CORTEX_M4_FLAGS_EXPLICIT;
    }
    return CORTEX_M4_FLAGS_UNCHANGED;
}

static bool wide_data_processing(uint16_t first, uint16_t second) {
    return ((first & 0xfa00u) == 0xf000u ||
            (first & 0xfe00u) == 0xea00u) &&
           (second & 0x8000u) == 0;
}

static bool writes_apsr(uint16_t first, uint16_t second) {
    const bool msr = (first & 0xfff0u) == 0xf380u &&
                     (second & 0xff00u) == 0x8800u &&
                     (second & 0x00ffu) <= 3u;
    const bool vmrs = first == 0xeef1u && second == 0xfa10u;
    return msr || vmrs;
}

CortexM4FlagWrite cortex_m4_it_flag_write(uint16_t first, uint16_t second,
                                           bool wide) {
    if (!wide) {
        return narrow_flag_write(first);
    }
    if (writes_apsr(first, second)) {
        return CORTEX_M4_FLAGS_EXPLICIT;
    }
    if (wide_data_processing(first, second) && (first & 0x0010u) != 0) {
        return CORTEX_M4_FLAGS_EXPLICIT;
    }
    return CORTEX_M4_FLAGS_UNCHANGED;
}

bool cortex_m4_it_condition_passed(const CortexM4* cpu) {
    return cpu != NULL &&
           (cpu->it_state == 0 ||
            cortex_m4_condition_passed(cpu, (uint8_t)(cpu->it_state >> 4)));
}

void cortex_m4_it_advance(CortexM4* cpu) {
    if (cpu == NULL || cpu->it_state == 0) {
        return;
    }
    if ((cpu->it_state & 7u) == 0) {
        cpu->it_state = 0;
        return;
    }
    cpu->it_state =
        (uint8_t)((cpu->it_state & 0xe0u) | ((cpu->it_state << 1) & 0x1fu));
}

void cortex_m4_it_preserve_flags(CortexM4* cpu, uint16_t first, uint16_t second,
                                 bool wide, bool in_it_block,
                                 uint32_t previous_xpsr) {
    if (cpu == NULL || !in_it_block ||
        cortex_m4_it_flag_write(first, second, wide) !=
            CORTEX_M4_FLAGS_IMPLICIT) {
        return;
    }
    cpu->xpsr = (cpu->xpsr & ~xpsr_nzcv) | (previous_xpsr & xpsr_nzcv);
}
