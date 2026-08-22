#ifndef CORTEX_M4_KINETIS_K22_INTERNAL_H
#define CORTEX_M4_KINETIS_K22_INTERNAL_H

#include "architecture/cortex_m4/internal.h"
#include "device/kinetis_k22/communication/sdhc.h"
#include "device/kinetis_k22/communication/serial/api.h"
#include "device/kinetis_k22/communication/usb_dcd.h"
#include "device/kinetis_k22/gpio/io.h"
#include "device/kinetis_k22/memory/api.h"
#include "device/kinetis_k22/timing/api.h"
#include "device/kinetis_k22/variants/manifest.h"
#include "device/kinetis_k22/variants/package.h"
#include "device/kinetis_k22/variants/profile.h"
#include "kinetis_k22.h"

#include <string.h>

enum {
    K22_FLASH_BASE = 0x00000000u,
    K22_SRAM_CENTER = 0x20000000u,
    K22_PERIPHERAL_BASE = 0x40000000u,
    K22_PERIPHERAL_SIZE = 0x00100000u,
    K22_BIT_BAND_BASE = 0x42000000u,
    K22_BIT_BAND_SIZE = 0x02000000u,
    K22_EVENT_CAPACITY = 64,
    K22_SIM_SCGC1 = 0x40048028u,
    K22_SIM_SCGC2 = 0x4004802cu,
    K22_AIPS0 = 0x40000000u,
    K22_AIPS1 = 0x40080000u,
    K22_AXBS = 0x40004000u,
    K22_FMC = 0x4001f000u,
    K22_USBDCD = 0x40035000u,
    K22_CMT = 0x40062000u,
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
    uint64_t cmt_bus_remainder;
    uint64_t cmt_mark_ticks;
    uint64_t cmt_period_ticks;
    uint64_t cmt_carrier_high_ticks;
    uint64_t cmt_carrier_period_ticks;
    uint64_t cmt_carrier_offset_ticks;
    uint64_t cmt_output_delay_ticks;
    bool cmt_eoc_read;
    bool cmt_running;
    bool cmt_stop_pending;
    bool cmt_fsk_secondary;
    bool cmt_extended_space;
    bool cmt_dma_pending;
    uint8_t fmc_bank[4][4];
    uint64_t fmc_age[4][4];
    uint64_t fmc_access_count;
    K22UsbDcd usbdcd;
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
bool kinetis_k22_dma_read(KinetisK22* device, uint32_t address, uint8_t size, uint32_t* value);
bool kinetis_k22_dma_write(KinetisK22* device, uint32_t address, uint8_t size, uint32_t value);
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

bool kinetis_k22_internal_aips_access_allowed(const KinetisK22* device, uint32_t address,
                                              CortexM4Access access, bool write);
bool kinetis_k22_internal_axbs_write_allowed(const KinetisK22* device, uint32_t address);
bool kinetis_k22_internal_enable_debug_clock(KinetisK22* device, K22PeripheralId id);
bool kinetis_k22_internal_manifest_extension(K22PeripheralId id);
bool kinetis_k22_internal_peripheral_clock_enabled(const KinetisK22* device, K22PeripheralId id);
bool kinetis_k22_internal_pop_serial_event(K22Serial* serial, K22SerialEndpoint endpoint,
                                           K22SerialEvent* event);
bool kinetis_k22_internal_semantic_read(KinetisK22* device, K22PeripheralId id, uint32_t address,
                                        uint8_t size, uint32_t* value);
bool kinetis_k22_internal_semantic_write(KinetisK22* device, K22PeripheralId id, uint32_t address,
                                         uint8_t size, uint32_t value);
bool kinetis_k22_internal_serial_endpoint_available(const KinetisK22* device,
                                                    KinetisK22SerialEndpoint endpoint);
const K22RegisterDescriptor*
kinetis_k22_internal_manifest_descriptor_for_access(const KinetisK22* device, uint32_t address,
                                                    uint8_t size);
K22PeripheralId kinetis_k22_internal_serial_endpoint_peripheral(KinetisK22SerialEndpoint endpoint);
bool kinetis_k22_internal_data_bus_read(void* context, uint32_t address, uint8_t size,
                                        uint32_t* value);
bool kinetis_k22_internal_data_bus_write(void* context, uint32_t address, uint8_t size,
                                         uint32_t value);
bool kinetis_k22_internal_flash_bus_write(void* context, uint32_t address, uint8_t size,
                                          uint32_t value);
void kinetis_k22_internal_data_dma_complete(void* context, uint8_t source);
void kinetis_k22_internal_data_interrupt(void* context, K22DataInterrupt interrupt, bool asserted);
void kinetis_k22_internal_io_event(void* context, const K22IoEvent* event);
void kinetis_k22_internal_timing_dma_trigger(void* context, uint8_t channel);
void kinetis_k22_internal_timing_dma(void* context, uint8_t source);
void kinetis_k22_internal_timing_irq(void* context, uint8_t irq, bool asserted);
void kinetis_k22_internal_timing_reset(void* context, uint8_t cause_0, uint8_t cause_1);
void kinetis_k22_internal_timing_trigger(void* context, K22TimingTrigger type, uint8_t instance,
                                         uint8_t channel);
uint32_t kinetis_k22_internal_manifest_access_mask(const K22RegisterDescriptor* descriptor,
                                                   uint32_t address, uint32_t mask);
uint32_t kinetis_k22_internal_raw_load(const KinetisK22* device, uint32_t address, uint8_t size);
uint32_t kinetis_k22_internal_width_mask(uint8_t size);
void kinetis_k22_internal_cmt_advance(KinetisK22* device, uint32_t cycles);
void kinetis_k22_internal_raw_store(KinetisK22* device, uint32_t address, uint8_t size,
                                    uint32_t value);
void kinetis_k22_internal_refresh_serial_signals(KinetisK22* device);

#endif
