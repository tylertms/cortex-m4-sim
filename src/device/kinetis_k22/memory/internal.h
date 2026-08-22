#ifndef KINETIS_K22_SIM_K22_DATA_INTERNAL_H
#define KINETIS_K22_SIM_K22_DATA_INTERNAL_H

#include "device/kinetis_k22/memory/api.h"

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

bool k22_data_internal_adc_read(K22Data* data, uint8_t instance, uint32_t address, uint8_t size,
                                uint32_t* value);
bool k22_data_internal_adc_write(K22Data* data, uint8_t instance, uint32_t address, uint8_t size,
                                 uint32_t value);
bool k22_data_internal_cmp_read(K22Data* data, uint8_t instance, uint32_t address, uint8_t size,
                                uint32_t* value);
bool k22_data_internal_cmp_write(K22Data* data, uint8_t instance, uint32_t address, uint8_t size,
                                 uint32_t value);
bool k22_data_internal_crc_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value);
bool k22_data_internal_crc_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value);
bool k22_data_internal_dac_read(K22Data* data, uint8_t instance, uint32_t address, uint8_t size,
                                uint32_t* value);
bool k22_data_internal_dac_write(K22Data* data, uint8_t instance, uint32_t address, uint8_t size,
                                 uint32_t value);
bool k22_data_internal_dma_priorities_valid(const K22Data* data);
bool k22_data_internal_dma_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value);
bool k22_data_internal_dma_service_channel(K22Data* data, uint8_t channel);
bool k22_data_internal_dma_source_always_enabled(const K22Data* data, uint8_t source);
bool k22_data_internal_dma_source_valid(const K22Data* data, uint8_t source);
bool k22_data_internal_dma_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value);
bool k22_data_internal_dmamux_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value);
bool k22_data_internal_dmamux_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value);
bool k22_data_internal_flash_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value);
bool k22_data_internal_flash_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value);
bool k22_data_internal_profile_block(const K22Data* data, K22PeripheralId id, uint32_t* base,
                                     uint32_t* size);
bool k22_data_internal_rng_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value);
bool k22_data_internal_rng_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value);
bool k22_data_internal_valid_access(uint32_t offset, uint8_t size, uint32_t length);
uint32_t k22_data_internal_dma_priority_offset(uint8_t channel);
uint32_t k22_data_internal_load_bytes(const uint8_t* bytes, uint32_t offset, uint8_t size);
uint32_t k22_data_internal_rng_next(uint32_t value);
uint8_t k22_data_internal_dma_select_channel(const K22Data* data);
void k22_data_internal_adc_complete(K22Data* data, uint8_t instance);
void k22_data_internal_adc_reset_registers(K22Adc* adc);
void k22_data_internal_adc_start(K22Adc* adc, uint8_t slot);
void k22_data_internal_cmp_evaluate(K22Data* data, uint8_t instance);
void k22_data_internal_dac_flags(K22Data* data, uint8_t instance, uint8_t flags);
void k22_data_internal_dac_update_output(K22Data* data, uint8_t instance);
void k22_data_internal_dma_error(K22Data* data, uint8_t channel, uint32_t reason);
void k22_data_internal_dma_queue_always_enabled(K22Data* data, uint8_t channel);
void k22_data_internal_dma_queue_hardware_channel(K22Data* data, uint8_t channel, uint8_t source);
void k22_data_internal_dma_update_interrupts(K22Data* data);
void k22_data_internal_flash_update_interrupts(K22Data* data);
void k22_data_internal_interrupt(K22Data* data, K22DataInterrupt line, bool asserted);
void k22_data_internal_store_bytes(uint8_t* bytes, uint32_t offset, uint8_t size, uint32_t value);

#endif
