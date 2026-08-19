#ifndef CORTEX_M4_K22_DATA_H
#define CORTEX_M4_K22_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "k22_profile.h"

typedef struct K22Data K22Data;

typedef enum {
    K22_DATA_INTERRUPT_DMA0,
    K22_DATA_INTERRUPT_DMA1,
    K22_DATA_INTERRUPT_DMA2,
    K22_DATA_INTERRUPT_DMA3,
    K22_DATA_INTERRUPT_DMA4,
    K22_DATA_INTERRUPT_DMA5,
    K22_DATA_INTERRUPT_DMA6,
    K22_DATA_INTERRUPT_DMA7,
    K22_DATA_INTERRUPT_DMA8,
    K22_DATA_INTERRUPT_DMA9,
    K22_DATA_INTERRUPT_DMA10,
    K22_DATA_INTERRUPT_DMA11,
    K22_DATA_INTERRUPT_DMA12,
    K22_DATA_INTERRUPT_DMA13,
    K22_DATA_INTERRUPT_DMA14,
    K22_DATA_INTERRUPT_DMA15,
    K22_DATA_INTERRUPT_DMA_ERROR,
    K22_DATA_INTERRUPT_FTFA,
    K22_DATA_INTERRUPT_ADC0,
    K22_DATA_INTERRUPT_ADC1,
    K22_DATA_INTERRUPT_DAC0,
    K22_DATA_INTERRUPT_DAC1,
    K22_DATA_INTERRUPT_CMP0,
    K22_DATA_INTERRUPT_CMP1,
    K22_DATA_INTERRUPT_CMP2,
    K22_DATA_INTERRUPT_RNG,
    K22_DATA_INTERRUPT_COUNT,
} K22DataInterrupt;

typedef struct {
    void* context;
    bool (*read)(void* context, uint32_t address, uint8_t size, uint32_t* value);
    bool (*write)(void* context, uint32_t address, uint8_t size, uint32_t value);
    bool (*program)(void* context, uint32_t address, uint8_t size, uint32_t value);
    void (*interrupt)(void* context, K22DataInterrupt interrupt, bool asserted);
} K22DataBus;

K22Data* k22_data_create(const K22Profile* profile, K22DataBus bus);
void k22_data_destroy(K22Data* data);
void k22_data_reset(K22Data* data);
bool k22_data_copy(K22Data* destination, const K22Data* source);
bool k22_data_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value);
bool k22_data_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value);
void k22_data_advance(K22Data* data, uint32_t cycles);
void k22_data_dma_request(K22Data* data, uint8_t source);
void k22_data_adc_trigger(K22Data* data, uint8_t instance);
void k22_data_adc_pretrigger(K22Data* data, uint8_t instance, uint8_t pretrigger);
bool k22_data_set_adc_input(K22Data* data, uint8_t instance, uint8_t channel,
                            uint16_t value);
bool k22_data_set_cmp_input(K22Data* data, uint8_t instance, uint8_t input, uint8_t value);
bool k22_data_get_cmp_output(const K22Data* data, uint8_t instance, bool* high);
bool k22_data_get_dac_output(const K22Data* data, uint8_t instance, uint16_t* value);
void k22_data_dac_trigger(K22Data* data, uint8_t instance);
void k22_data_rng_seed(K22Data* data, uint32_t seed);
bool k22_data_set_flash_configuration(K22Data* data, const uint8_t* bytes, size_t size);

#endif
