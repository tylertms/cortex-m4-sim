#include "cortex_m4_sim/kinetis_k22.h"

#include <stdlib.h>
#include <string.h>

#include "kinetis_k22_internal.h"

static bool valid_range(uint32_t address, uint8_t size, uint32_t base, size_t length) {
    return size != 0 && address >= base &&
           (uint64_t)address + size <= (uint64_t)base + length;
}

static uint32_t load_little_endian(const uint8_t* bytes, uint8_t size) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; index++) {
        value |= (uint32_t)bytes[index] << (index * 8u);
    }
    return value;
}

static void store_little_endian(uint8_t* bytes, uint8_t size, uint32_t value) {
    for (uint8_t index = 0; index < size; index++) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

static bool flash_access_allowed(const KinetisK22* device, CortexM4Access access,
                                 bool write) {
    if (access == CORTEX_M4_ACCESS_DEBUG) {
        return true;
    }
    const uint32_t offset = 0x4001f000u - K22_PERIPHERAL_BASE;
    const uint32_t protection = load_little_endian(device->peripheral + offset, 4u);
    const uint8_t master = access == CORTEX_M4_ACCESS_INSTRUCTION ? 0u : 1u;
    const uint8_t permission = (uint8_t)((protection >> (master * 2u)) & 3u);
    return write ? (permission & 2u) != 0u : (permission & 1u) != 0u;
}

bool kinetis_k22_memory_read(KinetisK22* device, uint32_t address, uint8_t size,
                             CortexM4Access access, uint32_t* value) {
    if (device == NULL || value == NULL || (size != 1 && size != 2 && size != 4)) {
        return false;
    }
    if (valid_range(address, size, K22_FLASH_BASE, device->configuration.flash_size)) {
        if (!flash_access_allowed(device, access, false)) {
            return false;
        }
        *value = load_little_endian(device->flash + address, size);
        return true;
    }
    if (valid_range(address, size, device->sram_base, device->configuration.sram_size)) {
        *value = load_little_endian(device->sram + address - device->sram_base, size);
        return true;
    }
    if (((device->profile->flexnvm_size != 0 &&
          valid_range(address, size, device->profile->flexnvm_address,
                      device->profile->flexnvm_size)) ||
         (device->profile->flexram_size != 0 &&
          valid_range(address, size, device->profile->flexram_address,
                      device->profile->flexram_size))) &&
        k22_data_read(device->data, address, size, value)) {
        return true;
    }
    return kinetis_k22_peripheral_read(device, address, size, access, value);
}

bool kinetis_k22_memory_write(KinetisK22* device, uint32_t address, uint8_t size,
                              CortexM4Access access, uint32_t value) {
    if (device == NULL || (size != 1 && size != 2 && size != 4)) {
        return false;
    }
    if (valid_range(address, size, K22_FLASH_BASE, device->configuration.flash_size)) {
        if (access != CORTEX_M4_ACCESS_DEBUG) {
            return false;
        }
        store_little_endian(device->flash + address, size, value);
        return true;
    }
    if (valid_range(address, size, device->sram_base, device->configuration.sram_size)) {
        store_little_endian(device->sram + address - device->sram_base, size, value);
        return true;
    }
    if (((device->profile->flexnvm_size != 0 &&
          valid_range(address, size, device->profile->flexnvm_address,
                      device->profile->flexnvm_size)) ||
         (device->profile->flexram_size != 0 &&
          valid_range(address, size, device->profile->flexram_address,
                      device->profile->flexram_size))) &&
        k22_data_write(device->data, address, size, value)) {
        return true;
    }
    return kinetis_k22_peripheral_write(device, address, size, access, value);
}

static const K22RegisterDescriptor* bit_band_descriptor(const KinetisK22* device,
                                                        uint32_t byte_address) {
    for (size_t index = 0; index < device->manifest->register_count; index++) {
        const K22RegisterDescriptor* descriptor = &device->manifest->registers[index];
        const uint8_t size = (uint8_t)(descriptor->width / 8u);
        if (byte_address >= descriptor->address &&
            byte_address - descriptor->address < size) {
            return descriptor;
        }
    }
    return NULL;
}

static bool bit_band_read(KinetisK22* device, uint32_t address, CortexM4Access access,
                          uint32_t* value) {
    if ((address & 3u) != 0) {
        return false;
    }
    const uint32_t alias = address - K22_BIT_BAND_BASE;
    const uint32_t byte_address = K22_PERIPHERAL_BASE + alias / 32u;
    const uint8_t bit_in_byte = (uint8_t)((alias / 4u) & 7u);
    const K22RegisterDescriptor* descriptor = bit_band_descriptor(device, byte_address);
    if (descriptor == NULL || (descriptor->access & K22_REGISTER_ACCESS_READ) == 0) {
        return false;
    }
    const uint8_t bit = (uint8_t)((byte_address - descriptor->address) * 8u + bit_in_byte);
    const uint32_t mask = 1u << bit;
    if ((descriptor->implemented_mask & descriptor->read_mask & mask) == 0) {
        return false;
    }
    uint32_t register_value = 0;
    if (!kinetis_k22_peripheral_read(device, descriptor->address,
                                     (uint8_t)(descriptor->width / 8u), access,
                                     &register_value)) {
        return false;
    }
    *value = (register_value & mask) != 0;
    return true;
}

static bool bit_band_write(KinetisK22* device, uint32_t address, CortexM4Access access,
                           uint32_t value) {
    if ((address & 3u) != 0) {
        return false;
    }
    const uint32_t alias = address - K22_BIT_BAND_BASE;
    const uint32_t byte_address = K22_PERIPHERAL_BASE + alias / 32u;
    const uint8_t bit_in_byte = (uint8_t)((alias / 4u) & 7u);
    const K22RegisterDescriptor* descriptor = bit_band_descriptor(device, byte_address);
    if (descriptor == NULL || (descriptor->access & K22_REGISTER_ACCESS_WRITE) == 0) {
        return false;
    }
    const uint8_t bit = (uint8_t)((byte_address - descriptor->address) * 8u + bit_in_byte);
    const uint32_t mask = 1u << bit;
    if ((descriptor->implemented_mask & descriptor->write_mask & mask) == 0) {
        return false;
    }
    uint32_t register_value = 0;
    if (!kinetis_k22_peripheral_read(device, descriptor->address,
                                     (uint8_t)(descriptor->width / 8u), access,
                                     &register_value)) {
        return false;
    }
    register_value &= ~descriptor->w1c_mask;
    if ((descriptor->w1c_mask & mask) != 0) {
        if ((value & 1u) != 0) {
            register_value |= mask;
        }
    } else {
        register_value = (value & 1u) != 0 ? register_value | mask : register_value & ~mask;
    }
    return kinetis_k22_peripheral_write(device, descriptor->address,
                                        (uint8_t)(descriptor->width / 8u), access,
                                        register_value);
}

static bool k22_read_bus(void* context, uint32_t address, uint8_t size,
                         CortexM4Access access, uint32_t* value) {
    KinetisK22* device = context;
    if (address >= K22_BIT_BAND_BASE && address < K22_BIT_BAND_BASE + K22_BIT_BAND_SIZE) {
        return (size == 1 || size == 2 || size == 4) &&
               bit_band_read(device, address, access, value);
    }
    return kinetis_k22_memory_read(device, address, size, access, value);
}

static bool k22_write_bus(void* context, uint32_t address, uint8_t size,
                          CortexM4Access access, uint32_t value) {
    KinetisK22* device = context;
    if (address >= K22_BIT_BAND_BASE && address < K22_BIT_BAND_BASE + K22_BIT_BAND_SIZE) {
        return (size == 1 || size == 2 || size == 4) &&
               bit_band_write(device, address, access, value);
    }
    return kinetis_k22_memory_write(device, address, size, access, value);
}

static void k22_advance_bus(void* context, uint32_t cycles) {
    KinetisK22* device = context;
    device->cycles += cycles;
    kinetis_k22_peripheral_advance(device, cycles);
}

static void k22_reset_bus(void* context) { kinetis_k22_warm_reset(context, 0, 0x04u); }

KinetisK22Configuration kinetis_k22_default_configuration(void) {
    KinetisK22Configuration configuration;
    memset(&configuration, 0, sizeof(configuration));
    configuration.profile = KINETIS_K22_PROFILE_MK22FN51212;
    configuration.package = KINETIS_K22_PACKAGE_DEFAULT;
    configuration.flash_size = 512u * 1024u;
    configuration.sram_size = 128u * 1024u;
    configuration.external_oscillator_hz = 8000000u;
    configuration.rtc_oscillator_hz = 32768u;
    return configuration;
}

static void destroy_partial(KinetisK22* device) {
    if (device == NULL) {
        return;
    }
    cortex_m4_destroy(device->cpu);
    k22_data_destroy(device->data);
    free(device->flash);
    free(device->sram);
    free(device->peripheral);
    free(device);
}

KinetisK22* kinetis_k22_create(KinetisK22Configuration configuration) {
    const K22Profile* profile = k22_profile_get(configuration.profile);
    const K22RegisterManifest* manifest = k22_register_manifest_get(configuration.profile);
    if (profile == NULL || manifest == NULL) {
        return NULL;
    }
    const K22PackageSelection* package =
        configuration.package == KINETIS_K22_PACKAGE_DEFAULT
            ? k22_package_default(profile)
            : k22_package_select(profile, (K22PackageId)configuration.package);
    if (package == NULL || configuration.flash_size == 0 ||
        configuration.flash_size > profile->program_flash_size ||
        configuration.sram_size == 0 || configuration.sram_size > 0x40000000u) {
        return NULL;
    }
    KinetisK22* device = calloc(1, sizeof(*device));
    if (device == NULL) {
        return NULL;
    }
    device->configuration = configuration;
    device->profile = profile;
    device->package = package;
    device->manifest = manifest;
    const size_t profile_sram_size =
        (size_t)profile->sram_lower_size + profile->sram_upper_size;
    device->sram_base = configuration.sram_size == profile_sram_size
                            ? profile->sram_lower_address
                            : K22_SRAM_CENTER - (uint32_t)(configuration.sram_size / 2u);
    device->flash = malloc(configuration.flash_size);
    device->sram = calloc(1, configuration.sram_size);
    device->peripheral = calloc(1, K22_PERIPHERAL_SIZE);
    if (device->flash == NULL || device->sram == NULL || device->peripheral == NULL) {
        destroy_partial(device);
        return NULL;
    }
    memset(device->flash, 0xff, configuration.flash_size);
    if (!k22_serial_init(&device->serial, profile) ||
        !k22_timing_init(&device->timing, profile, configuration.external_oscillator_hz,
                         configuration.rtc_oscillator_hz,
                         kinetis_k22_timing_signals(device)) ||
        !k22_io_init(&device->io, kinetis_k22_io_configuration(device))) {
        destroy_partial(device);
        return NULL;
    }
    device->data = k22_data_create(profile, kinetis_k22_data_bus(device));
    if (device->data == NULL) {
        destroy_partial(device);
        return NULL;
    }
    kinetis_k22_sync_clock_gates(device);
    CortexM4Bus bus = {device, k22_read_bus, k22_write_bus, k22_advance_bus, k22_reset_bus};
    device->cpu = cortex_m4_create(bus);
    if (device->cpu == NULL ||
        !cortex_m4_configure_implementation(device->cpu, profile->cpu.external_irq_count,
                                            profile->cpu.nvic_priority_bits,
                                            profile->cpu.has_mpu ? 8u : 0u)) {
        destroy_partial(device);
        return NULL;
    }
    return device;
}

void kinetis_k22_destroy(KinetisK22* device) { destroy_partial(device); }

CortexM4* kinetis_k22_cpu(KinetisK22* device) {
    return device == NULL ? NULL : device->cpu;
}

const CortexM4* kinetis_k22_cpu_const(const KinetisK22* device) {
    return device == NULL ? NULL : device->cpu;
}

static void sync_flash_configuration(KinetisK22* device) {
    K22PeripheralBlock block;
    if (!k22_profile_peripheral_block(device->profile, K22_PERIPHERAL_FLASH_CONFIG,
                                      &block) ||
        block.address + block.size > device->configuration.flash_size) {
        return;
    }
    (void)k22_data_set_flash_configuration(device->data, device->flash + block.address,
                                           block.size);
    memcpy(device->io.configuration.flash_configuration, device->flash + block.address,
           block.size);
}

bool kinetis_k22_reset(KinetisK22* device) {
    if (device == NULL) {
        return false;
    }
    memset(device->sram, 0, device->configuration.sram_size);
    device->cycles = 0;
    device->timing.elapsed_core_cycles = 0;
    sync_flash_configuration(device);
    kinetis_k22_peripheral_reset(device);
    k22_timing_reset(&device->timing, 0x82u, 0);
    kinetis_k22_sync_clock_gates(device);
    return cortex_m4_reset(device->cpu, device->configuration.vector_table_address);
}

void kinetis_k22_warm_reset(KinetisK22* device, uint8_t cause_0, uint8_t cause_1) {
    if (device == NULL) {
        return;
    }
    uint8_t retained_vbat[0x20];
    memcpy(retained_vbat, device->peripheral + 0x3e000u, sizeof(retained_vbat));
    sync_flash_configuration(device);
    kinetis_k22_peripheral_reset(device);
    memcpy(device->peripheral + 0x3e000u, retained_vbat, sizeof(retained_vbat));
    k22_timing_reset(&device->timing, cause_0, cause_1);
    kinetis_k22_sync_clock_gates(device);
    (void)cortex_m4_reset(device->cpu, device->configuration.vector_table_address);
}

bool kinetis_k22_load(KinetisK22* device, uint32_t address, const void* data, size_t size) {
    if (device == NULL || data == NULL) {
        return false;
    }
    if (address < device->configuration.flash_size &&
        (uint64_t)address + size <= device->configuration.flash_size) {
        memcpy(device->flash + address, data, size);
        sync_flash_configuration(device);
        return true;
    }
    if (address >= device->sram_base &&
        (uint64_t)address + size <=
            (uint64_t)device->sram_base + device->configuration.sram_size) {
        memcpy(device->sram + address - device->sram_base, data, size);
        return true;
    }
    return false;
}

bool kinetis_k22_read(const KinetisK22* device, uint32_t address, void* data, size_t size) {
    if (device == NULL || data == NULL) {
        return false;
    }
    uint8_t* output = data;
    size_t offset = 0;
    while (offset < size) {
        const size_t remaining = size - offset;
        const uint8_t width = remaining >= 4 && ((address + offset) & 3u) == 0   ? 4
                              : remaining >= 2 && ((address + offset) & 1u) == 0 ? 2
                                                                                 : 1;
        uint32_t value = 0;
        if (!k22_read_bus((KinetisK22*)device, address + (uint32_t)offset, width,
                          CORTEX_M4_ACCESS_DEBUG, &value)) {
            return false;
        }
        store_little_endian(output + offset, width, value);
        offset += width;
    }
    return true;
}

bool kinetis_k22_write(KinetisK22* device, uint32_t address, const void* data,
                       size_t size) {
    if (device == NULL || data == NULL) {
        return false;
    }
    const uint8_t* input = data;
    size_t offset = 0;
    while (offset < size) {
        const size_t remaining = size - offset;
        const uint8_t width = remaining >= 4 && ((address + offset) & 3u) == 0   ? 4
                              : remaining >= 2 && ((address + offset) & 1u) == 0 ? 2
                                                                                 : 1;
        const uint32_t value = load_little_endian(input + offset, width);
        if (!k22_write_bus(device, address + (uint32_t)offset, width,
                           CORTEX_M4_ACCESS_DEBUG, value)) {
            return false;
        }
        offset += width;
    }
    return true;
}

bool kinetis_k22_copy(KinetisK22* destination, const KinetisK22* source) {
    if (destination == NULL || source == NULL ||
        destination->profile->id != source->profile->id ||
        destination->package != source->package ||
        destination->configuration.flash_size != source->configuration.flash_size ||
        destination->configuration.sram_size != source->configuration.sram_size) {
        return false;
    }
    memcpy(destination->flash, source->flash, source->configuration.flash_size);
    memcpy(destination->sram, source->sram, source->configuration.sram_size);
    memcpy(destination->peripheral, source->peripheral, K22_PERIPHERAL_SIZE);
    destination->configuration = source->configuration;
    destination->cycles = source->cycles;
    destination->cmt_cycles = source->cmt_cycles;
    destination->usbdcd_cycles = source->usbdcd_cycles;
    destination->cmt_eoc_read = source->cmt_eoc_read;
    destination->usb_charger = source->usb_charger;
    memcpy(destination->events, source->events, sizeof(destination->events));
    destination->event_read_index = source->event_read_index;
    destination->event_write_index = source->event_write_index;
    destination->event_count = source->event_count;
    if (!k22_data_copy(destination->data, source->data) ||
        !k22_serial_copy(&destination->serial, &source->serial) ||
        !k22_io_copy(&destination->io, &source->io) ||
        !k22_timing_copy(&destination->timing, &source->timing,
                         kinetis_k22_timing_signals(destination))) {
        return false;
    }
    destination->io.configuration.event_handler =
        kinetis_k22_io_configuration(destination).event_handler;
    destination->io.configuration.event_context = destination;
    return cortex_m4_copy(destination->cpu, source->cpu);
}

void kinetis_k22_advance(KinetisK22* device, uint32_t cycles) {
    if (device != NULL && cycles != 0) {
        k22_advance_bus(device, cycles);
    }
}

void kinetis_k22_watchdog_advance(KinetisK22* device, uint32_t ticks) {
    if (device == NULL || ticks == 0 || (device->timing.wdog[0] & 1u) == 0) {
        return;
    }
    const uint32_t timeout =
        ((uint32_t)device->timing.wdog[2] << 16u) | device->timing.wdog[3];
    if (timeout == 0 || device->timing.wdog_counter >= timeout ||
        ticks >= timeout - device->timing.wdog_counter) {
        if ((device->timing.wdog[0] & 4u) != 0) {
            cortex_m4_set_irq(device->cpu, 22, true);
            device->timing.wdog_counter = 0;
        } else {
            kinetis_k22_warm_reset(device, 0x20u, 0);
        }
    } else {
        device->timing.wdog_counter += ticks;
    }
}

uint32_t kinetis_k22_core_clock_hz(const KinetisK22* device) {
    return device == NULL ? 0 : k22_timing_core_clock_hz(&device->timing);
}

uint32_t kinetis_k22_bus_clock_hz(const KinetisK22* device) {
    return device == NULL ? 0 : k22_timing_bus_clock_hz(&device->timing);
}
