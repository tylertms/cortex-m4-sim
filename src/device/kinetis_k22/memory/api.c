#include "internal.h"

#include <stdlib.h>
#include <string.h>

uint32_t k22_data_internal_load_bytes(const uint8_t* bytes, uint32_t offset, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; index++)
        value |= (uint32_t)bytes[offset + index] << (8u * index);
    return value;
}

void k22_data_internal_store_bytes(uint8_t* bytes, uint32_t offset, uint8_t size, uint32_t value) {
    for (uint8_t index = 0; index < size; index++)
        bytes[offset + index] = (uint8_t)(value >> (8u * index));
}

void k22_data_internal_adc_reset_registers(K22Adc* adc) {
    adc->registers[0] = 0x1fu;
    adc->registers[4] = 0x1fu;
    k22_data_internal_store_bytes(adc->registers, 0x28u, 4u, 0x0004u);
    k22_data_internal_store_bytes(adc->registers, 0x2cu, 4u, 0x8200u);
    k22_data_internal_store_bytes(adc->registers, 0x30u, 4u, 0x8200u);
    k22_data_internal_store_bytes(adc->registers, 0x34u, 4u, 0x000au);
    k22_data_internal_store_bytes(adc->registers, 0x38u, 4u, 0x0020u);
    k22_data_internal_store_bytes(adc->registers, 0x3cu, 4u, 0x0200u);
    k22_data_internal_store_bytes(adc->registers, 0x40u, 4u, 0x0100u);
    k22_data_internal_store_bytes(adc->registers, 0x44u, 4u, 0x0080u);
    k22_data_internal_store_bytes(adc->registers, 0x48u, 4u, 0x0040u);
    k22_data_internal_store_bytes(adc->registers, 0x4cu, 4u, 0x0020u);
    k22_data_internal_store_bytes(adc->registers, 0x54u, 4u, 0x000au);
    k22_data_internal_store_bytes(adc->registers, 0x58u, 4u, 0x0020u);
    k22_data_internal_store_bytes(adc->registers, 0x5cu, 4u, 0x0200u);
    k22_data_internal_store_bytes(adc->registers, 0x60u, 4u, 0x0100u);
    k22_data_internal_store_bytes(adc->registers, 0x64u, 4u, 0x0080u);
    k22_data_internal_store_bytes(adc->registers, 0x68u, 4u, 0x0040u);
    k22_data_internal_store_bytes(adc->registers, 0x6cu, 4u, 0x0020u);
}

bool k22_data_internal_valid_access(uint32_t offset, uint8_t size, uint32_t length) {
    return (size == 1 || size == 2 || size == 4) && offset < length && size <= length - offset;
}

void k22_data_internal_interrupt(K22Data* data, K22DataInterrupt line, bool asserted) {
    if (data->bus.interrupt != NULL)
        data->bus.interrupt(data->bus.context, line, asserted);
}

bool k22_data_internal_profile_block(const K22Data* data, K22PeripheralId id, uint32_t* base,
                                     uint32_t* size) {
    K22PeripheralBlock block;
    if (!k22_profile_peripheral_block(data->profile, id, &block))
        return false;
    if (base != NULL)
        *base = block.address;
    if (size != NULL)
        *size = block.size;
    return true;
}
