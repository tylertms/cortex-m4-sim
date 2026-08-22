#ifndef KINETIS_K22_DATA_TEST_INTERNAL_H
#define KINETIS_K22_DATA_TEST_INTERNAL_H

#include "device/kinetis_k22/memory/api.h"

#include <stdint.h>
#include <string.h>

#include "test.h"

enum {
    DMA = 0x40008000u,
    TCD0 = 0x40009000u,
    TCD1 = 0x40009020u,
    DMAMUX = 0x40021000u,
    ADC0 = 0x4003b000u,
    ADC1 = 0x40027000u,
    DAC0 = 0x4003f000u,
    DAC1 = 0x40028000u,
    RNG = 0x40029000u,
    CRC = 0x40032000u,
    FTFA = 0x40020000u,
    CMP0 = 0x40073000u,
    VREF = 0x40074000u,
    RAM_BASE = 0x20000000u,
};

typedef struct {
    uint8_t flash[1024 * 1024];
    uint8_t ram[64 * 1024];
    bool interrupt[K22_DATA_INTERRUPT_COUNT];
    bool fail_read;
    bool fail_write;
    bool observe_dma_active;
    uint16_t dma_active;
    uint32_t dma_write_values[32];
    uint8_t dma_write_count;
    K22Data* data;
} TestBus;

K22Data* k22_data_test_create_without_program(TestState* state, TestBus* bus, K22ProfileId profile);
K22Data* k22_data_test_create(TestState* state, TestBus* bus, K22ProfileId profile);
uint32_t k22_data_test_load(const uint8_t* bytes, uint32_t offset, uint8_t size);
uint32_t k22_data_test_read_value(TestState* state, K22Data* data, uint32_t address, uint8_t size);
uint8_t k22_data_test_read_fccob(TestState* state, K22Data* data, uint8_t index);
void k22_data_test_clear_flash_status(TestState* state, K22Data* data);
void k22_data_test_flash_command_without_address(TestState* state, K22Data* data, uint8_t command,
                                                 uint32_t cycles);
void k22_data_test_flash_command(TestState* state, K22Data* data, uint8_t command, uint32_t address,
                                 uint32_t cycles);
void k22_data_test_set_flash_address(TestState* state, K22Data* data, uint32_t address);
void k22_data_test_set_flash_data(TestState* state, K22Data* data, const uint8_t* bytes,
                                  uint8_t length);
void k22_data_test_store(uint8_t* bytes, uint32_t offset, uint8_t size, uint32_t value);
void k22_data_test_test_adc_compare_dma_and_continuous(TestState* state);
void k22_data_test_test_adc(TestState* state);
void k22_data_test_test_dac_cmp_vref(TestState* state);
void k22_data_test_test_dma_advanced(TestState* state);
void k22_data_test_test_dma_arbitration_and_control(TestState* state);
void k22_data_test_test_dma(TestState* state);
void k22_data_test_test_dmamux_source_matrix(TestState* state);
void k22_data_test_test_dmamux_triggers(TestState* state);
void k22_data_test_test_flash_collision_lifecycle(TestState* state);
void k22_data_test_test_flash_command_semantics(TestState* state);
void k22_data_test_test_flash_commands_and_failures(TestState* state);
void k22_data_test_test_flash_controller_geometry(TestState* state);
void k22_data_test_test_flash_flex_copy(TestState* state);
void k22_data_test_test_profile_boundaries(TestState* state);
void k22_data_test_test_rng_crc(TestState* state);
void k22_data_test_write_fccob(TestState* state, K22Data* data, uint8_t index, uint8_t value);
void k22_data_test_write_value(TestState* state, K22Data* data, uint32_t address, uint8_t size,
                               uint32_t value);

#endif
