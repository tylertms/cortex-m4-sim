#ifndef CORTEX_M4_K22_SDHC_H
#define CORTEX_M4_K22_SDHC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*K22SdhcBusRead)(void* context, uint32_t address, uint8_t size,
                               uint32_t* value);
typedef bool (*K22SdhcBusWrite)(void* context, uint32_t address, uint8_t size,
                                uint32_t value);

typedef struct {
    void* context;
    K22SdhcBusRead read;
    K22SdhcBusWrite write;
} K22SdhcBus;

typedef struct {
    K22SdhcBus bus;
    uint32_t registers[64];
    uint8_t* card;
    size_t card_size;
    size_t transfer_address;
    size_t transfer_remaining;
    uint32_t block_length;
    uint32_t relative_address;
    bool present;
    bool write_protected;
    bool clock_enabled;
    bool application_command;
    bool selected;
    bool transfer_read;
    bool transfer_multiple;
} K22Sdhc;

bool k22_sdhc_init(K22Sdhc* sdhc, K22SdhcBus bus);
void k22_sdhc_destroy(K22Sdhc* sdhc);
void k22_sdhc_reset(K22Sdhc* sdhc);
bool k22_sdhc_copy(K22Sdhc* destination, const K22Sdhc* source, K22SdhcBus bus);
void k22_sdhc_set_clock(K22Sdhc* sdhc, bool enabled);
bool k22_sdhc_insert(K22Sdhc* sdhc, const void* data, size_t size, bool write_protected);
void k22_sdhc_eject(K22Sdhc* sdhc);
bool k22_sdhc_read_card(const K22Sdhc* sdhc, size_t offset, void* data, size_t size);
bool k22_sdhc_read(K22Sdhc* sdhc, uint32_t address, uint8_t size, uint32_t* value);
bool k22_sdhc_write(K22Sdhc* sdhc, uint32_t address, uint8_t size, uint32_t value);
bool k22_sdhc_irq(const K22Sdhc* sdhc);

#endif
