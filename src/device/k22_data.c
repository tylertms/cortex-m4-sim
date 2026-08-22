#include "k22_data.h"

#include <stdlib.h>
#include <string.h>

enum {
    DMA_BASE = 0x40008000u,
    DMA_TCD_BASE = 0x40009000u,
    DMAMUX_BASE = 0x40021000u,
    ADC0_BASE = 0x4003b000u,
    ADC1_BASE_STANDARD = 0x40027000u,
    ADC1_BASE_LARGE = 0x400bb000u,
    DAC0_BASE_STANDARD = 0x4003f000u,
    DAC0_BASE_LARGE = 0x400cc000u,
    DAC1_BASE = 0x40028000u,
    RNG_BASE = 0x40029000u,
    CRC_BASE = 0x40032000u,
    FLASH_BASE = 0x40020000u,
    CMP_BASE = 0x40073000u,
    VREF_BASE = 0x40074000u,
    DMA_CHANNEL_COUNT = 16,
    DMA_TCD_SIZE = 32,
    DMA_REGISTER_SIZE = 0x1200,
    ADC_REGISTER_SIZE = 0x70,
    DAC_REGISTER_SIZE = 0x24,
    CMP_REGISTER_SIZE = 6,
};

typedef struct {
    uint8_t registers[ADC_REGISTER_SIZE];
    uint16_t inputs[32];
    uint32_t remaining_cycles;
    uint8_t active_slot;
    bool converting;
} K22Adc;

typedef struct {
    uint8_t registers[DAC_REGISTER_SIZE];
    uint16_t output;
} K22Dac;

typedef struct {
    uint8_t registers[CMP_REGISTER_SIZE];
    uint8_t inputs[8];
} K22Cmp;

struct K22Data {
    const K22Profile* profile;
    K22DataBus bus;
    uint8_t dma[DMA_REGISTER_SIZE];
    uint16_t dma_requests;
    uint16_t dma_hardware_requests;
    uint16_t dma_trigger_waiting;
    uint16_t dma_active;
    uint16_t dma_half;
    uint8_t dma_request_source[DMA_CHANNEL_COUNT];
    uint8_t dma_channel_count;
    uint8_t dma_last_channel;
    bool debug_halted;
    uint8_t dmamux[DMA_CHANNEL_COUNT];
    uint8_t dmamux_count;
    K22Adc adc[2];
    uint8_t adc_count;
    uint32_t adc_base[2];
    K22Dac dac[2];
    uint8_t dac_count;
    uint32_t dac_base[2];
    K22Cmp cmp[3];
    uint8_t cmp_count;
    uint8_t vref[2];
    uint32_t vref_cycles;
    uint32_t rng_control;
    uint32_t rng_status;
    uint32_t rng_error;
    uint32_t rng_output;
    uint32_t rng_state;
    uint32_t rng_cycles;
    uint32_t crc_value;
    uint32_t crc_polynomial;
    uint32_t crc_control;
    uint8_t flash[0x2c];
    uint8_t flash_config[0x10];
    uint8_t flash_program_ifr[1024];
    uint8_t flash_data_ifr[1024];
    uint32_t flash_cycles;
    uint8_t flash_busy_banks;
    uint32_t flash_busy_start;
    uint32_t flash_busy_length;
    uint32_t flash_swap_address;
    uint8_t flash_swap_mode;
    uint8_t flash_swap_current_block;
    uint8_t flash_swap_next_block;
    bool flash_key_blocked;
    bool flash_partitioned;
    bool flexram_eeprom;
    uint8_t* flexnvm;
    uint8_t* flexram;
};

static uint32_t load_bytes(const uint8_t* bytes, uint32_t offset, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; index++)
        value |= (uint32_t)bytes[offset + index] << (8u * index);
    return value;
}

static void store_bytes(uint8_t* bytes, uint32_t offset, uint8_t size, uint32_t value) {
    for (uint8_t index = 0; index < size; index++)
        bytes[offset + index] = (uint8_t)(value >> (8u * index));
}

static void adc_reset_registers(K22Adc* adc) {
    adc->registers[0] = 0x1fu;
    adc->registers[4] = 0x1fu;
    store_bytes(adc->registers, 0x28u, 4u, 0x0004u);
    store_bytes(adc->registers, 0x2cu, 4u, 0x8200u);
    store_bytes(adc->registers, 0x30u, 4u, 0x8200u);
    store_bytes(adc->registers, 0x34u, 4u, 0x000au);
    store_bytes(adc->registers, 0x38u, 4u, 0x0020u);
    store_bytes(adc->registers, 0x3cu, 4u, 0x0200u);
    store_bytes(adc->registers, 0x40u, 4u, 0x0100u);
    store_bytes(adc->registers, 0x44u, 4u, 0x0080u);
    store_bytes(adc->registers, 0x48u, 4u, 0x0040u);
    store_bytes(adc->registers, 0x4cu, 4u, 0x0020u);
    store_bytes(adc->registers, 0x54u, 4u, 0x000au);
    store_bytes(adc->registers, 0x58u, 4u, 0x0020u);
    store_bytes(adc->registers, 0x5cu, 4u, 0x0200u);
    store_bytes(adc->registers, 0x60u, 4u, 0x0100u);
    store_bytes(adc->registers, 0x64u, 4u, 0x0080u);
    store_bytes(adc->registers, 0x68u, 4u, 0x0040u);
    store_bytes(adc->registers, 0x6cu, 4u, 0x0020u);
}

static bool valid_access(uint32_t offset, uint8_t size, uint32_t length) {
    return (size == 1 || size == 2 || size == 4) && offset < length && size <= length - offset;
}

static void interrupt(K22Data* data, K22DataInterrupt line, bool asserted) {
    if (data->bus.interrupt != NULL)
        data->bus.interrupt(data->bus.context, line, asserted);
}

static bool profile_block(const K22Data* data, K22PeripheralId id, uint32_t* base, uint32_t* size) {
    K22PeripheralBlock block;
    if (!k22_profile_peripheral_block(data->profile, id, &block))
        return false;
    if (base != NULL)
        *base = block.address;
    if (size != NULL)
        *size = block.size;
    return true;
}

#include "data/dma.inc"
#include "data/analog.inc"
#include "data/integrity.inc"
#include "data/flash.inc"
#include "data/runtime.inc"
