#ifndef CORTEX_M4_KINETIS_K22_INTERNAL_H
#define CORTEX_M4_KINETIS_K22_INTERNAL_H

#include "cortex_m4_internal.h"
#include "cortex_m4_sim/kinetis_k22.h"
#include "k22_data.h"
#include "k22_io.h"
#include "k22_package.h"
#include "k22_profile.h"
#include "k22_register_manifest.h"
#include "k22_sdhc.h"
#include "k22_serial.h"
#include "k22_timing.h"

enum {
    K22_FLASH_BASE = 0x00000000u,
    K22_SRAM_CENTER = 0x20000000u,
    K22_PERIPHERAL_BASE = 0x40000000u,
    K22_PERIPHERAL_SIZE = 0x00100000u,
    K22_BIT_BAND_BASE = 0x42000000u,
    K22_BIT_BAND_SIZE = 0x02000000u,
    K22_EVENT_CAPACITY = 64,
};

struct KinetisK22 {
    KinetisK22Configuration configuration;
    const K22Profile* profile;
    const K22PackageSelection* package;
    const K22RegisterManifest* manifest;
    CortexM4* cpu;
    uint8_t* flash;
    uint8_t* sram;
    uint8_t* peripheral;
    uint8_t* flexbus_memory;
    uint32_t flexbus_address;
    size_t flexbus_size;
    bool flexbus_read_only;
    uint32_t sram_base;
    uint64_t cycles;
    uint64_t cmt_cycles;
    uint64_t usbdcd_cycles;
    bool cmt_eoc_read;
    KinetisK22UsbCharger usb_charger;
    K22Data* data;
    K22Io io;
    K22Sdhc sdhc;
    K22Serial serial;
    K22Timing timing;
    bool comparator_output[3];
    KinetisK22Event events[K22_EVENT_CAPACITY];
    uint8_t event_read_index;
    uint8_t event_write_index;
    uint8_t event_count;
};

bool kinetis_k22_memory_read(KinetisK22* device, uint32_t address, uint8_t size,
                             CortexM4Access access, uint32_t* value);
bool kinetis_k22_memory_write(KinetisK22* device, uint32_t address, uint8_t size,
                              CortexM4Access access, uint32_t value);
bool kinetis_k22_dma_read(KinetisK22* device, uint32_t address, uint8_t size,
                          uint32_t* value);
bool kinetis_k22_dma_write(KinetisK22* device, uint32_t address, uint8_t size,
                           uint32_t value);
bool kinetis_k22_flash_controller_write(KinetisK22* device, uint32_t address, uint8_t size,
                                        uint32_t value);
bool kinetis_k22_peripheral_read(KinetisK22* device, uint32_t address, uint8_t size,
                                 CortexM4Access access, uint32_t* value);
bool kinetis_k22_peripheral_write(KinetisK22* device, uint32_t address, uint8_t size,
                                  CortexM4Access access, uint32_t value);
void kinetis_k22_peripheral_advance(KinetisK22* device, uint32_t cycles);
void kinetis_k22_peripheral_reset(KinetisK22* device);
void kinetis_k22_warm_reset(KinetisK22* device, uint8_t cause_0, uint8_t cause_1);
void kinetis_k22_refresh_signals(KinetisK22* device);
void kinetis_k22_sync_clock_gates(KinetisK22* device);
K22DataBus kinetis_k22_data_bus(KinetisK22* device);
K22SdhcBus kinetis_k22_sdhc_bus(KinetisK22* device);
K22TimingSignals kinetis_k22_timing_signals(KinetisK22* device);
K22IoConfiguration kinetis_k22_io_configuration(KinetisK22* device);

#endif
