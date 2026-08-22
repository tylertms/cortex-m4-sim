#include "device/kinetis_k22/internal.h"

static bool manifest_read(KinetisK22* device, uint32_t address, uint8_t size, uint32_t* value) {
    const K22RegisterDescriptor* descriptor =
        kinetis_k22_internal_manifest_descriptor_for_access(device, address, size);
    if (descriptor == NULL || (descriptor->access & K22_REGISTER_ACCESS_READ) == 0 ||
        address < K22_PERIPHERAL_BASE) {
        return false;
    }
    *value = kinetis_k22_internal_raw_load(device, address, size) &
             kinetis_k22_internal_manifest_access_mask(descriptor, address, descriptor->read_mask) &
             kinetis_k22_internal_manifest_access_mask(descriptor, address,
                                                       descriptor->implemented_mask) &
             kinetis_k22_internal_width_mask(size);
    return true;
}

static bool manifest_write(KinetisK22* device, uint32_t address, uint8_t size, uint32_t value) {
    const K22RegisterDescriptor* descriptor =
        kinetis_k22_internal_manifest_descriptor_for_access(device, address, size);
    if (descriptor == NULL || address < K22_PERIPHERAL_BASE) {
        return false;
    }
    if ((descriptor->access & K22_REGISTER_ACCESS_WRITE) == 0) {
        return true;
    }
    const uint32_t mask = kinetis_k22_internal_manifest_access_mask(descriptor, address,
                                                                    descriptor->implemented_mask) &
                          kinetis_k22_internal_width_mask(size);
    const uint32_t write_mask =
        kinetis_k22_internal_manifest_access_mask(descriptor, address, descriptor->write_mask);
    const uint32_t w1c_mask =
        kinetis_k22_internal_manifest_access_mask(descriptor, address, descriptor->w1c_mask);
    const uint32_t writable = write_mask & ~w1c_mask & mask;
    const uint32_t clear = w1c_mask & value & mask;
    uint32_t current = kinetis_k22_internal_raw_load(device, address, size);
    current = (current & ~writable) | (value & writable);
    current &= ~clear;
    kinetis_k22_internal_raw_store(device, address, size, current & mask);
    return true;
}

static void apply_fmc_control(KinetisK22* device, uint32_t address, uint8_t size, uint32_t value) {
    if (address != K22_FMC + 4u || size != 4u) {
        return;
    }
    const uint8_t ways = (uint8_t)((value >> 20u) & 0x0fu);
    const uint8_t sets = 4u;
    for (uint8_t way = 0u; way < 4u; way++) {
        if ((ways & (1u << way)) == 0u) {
            continue;
        }
        for (uint8_t set = 0u; set < sets; set++) {
            const uint32_t tag_address = K22_FMC + 0x100u + ((uint32_t)way * sets + set) * 4u;
            if (k22_register_manifest_lookup(device->profile->id, tag_address, 32u) != NULL) {
                kinetis_k22_internal_raw_store(device, tag_address, 4u, 0u);
                device->fmc_bank[way][set] = 0u;
                device->fmc_age[way][set] = 0u;
                for (uint8_t word = 0u; word < 4u; word++) {
                    const uint32_t data_address =
                        K22_FMC + 0x200u + (((uint32_t)way * sets + set) * 4u + word) * 4u;
                    kinetis_k22_internal_raw_store(device, data_address, 4u, 0u);
                }
            }
        }
    }
    const uint32_t control = kinetis_k22_internal_raw_load(device, address, 4u) & ~0x00f80000u;
    kinetis_k22_internal_raw_store(device, address, 4u, control);
}

bool kinetis_k22_peripheral_read(KinetisK22* device, uint32_t address, uint8_t size,
                                 CortexM4Access access, uint32_t* value) {
    K22PeripheralLocation location;
    const K22RegisterDescriptor* descriptor;
    if (device == NULL || value == NULL ||
        !k22_profile_resolve_peripheral(device->profile, address, size, &location) ||
        !k22_package_has_peripheral(device->package, location.id)) {
        return false;
    }
    descriptor = kinetis_k22_internal_manifest_descriptor_for_access(device, address, size);
    if (location.id != K22_PERIPHERAL_MCM &&
        !kinetis_k22_internal_manifest_extension(location.id) &&
        (descriptor == NULL || (descriptor->access & K22_REGISTER_ACCESS_READ) == 0u)) {
        return false;
    }
    if (!kinetis_k22_internal_aips_access_allowed(device, address, access, false)) {
        return false;
    }
    if (access != CORTEX_M4_ACCESS_DEBUG &&
        !kinetis_k22_internal_peripheral_clock_enabled(device, location.id)) {
        return false;
    }
    const bool debug_clock = access == CORTEX_M4_ACCESS_DEBUG &&
                             kinetis_k22_internal_enable_debug_clock(device, location.id);
    bool handled = kinetis_k22_internal_semantic_read(device, location.id, address, size, value);
    if (!handled) {
        handled = manifest_read(device, address, size, value);
    }
    if (debug_clock) {
        kinetis_k22_sync_clock_gates(device);
    }
    kinetis_k22_refresh_signals(device);
    return handled;
}

bool kinetis_k22_peripheral_write(KinetisK22* device, uint32_t address, uint8_t size,
                                  CortexM4Access access, uint32_t value) {
    K22PeripheralLocation location;
    const K22RegisterDescriptor* descriptor;
    if (device == NULL ||
        !k22_profile_resolve_peripheral(device->profile, address, size, &location) ||
        !k22_package_has_peripheral(device->package, location.id)) {
        return false;
    }
    descriptor = kinetis_k22_internal_manifest_descriptor_for_access(device, address, size);
    if (location.id != K22_PERIPHERAL_MCM &&
        !kinetis_k22_internal_manifest_extension(location.id) && descriptor == NULL) {
        return false;
    }
    if (descriptor != NULL && (descriptor->access & K22_REGISTER_ACCESS_WRITE) == 0u) {
        return true;
    }
    if (!kinetis_k22_internal_aips_access_allowed(device, address, access, true) ||
        (location.id == K22_PERIPHERAL_AXBS &&
         !kinetis_k22_internal_axbs_write_allowed(device, address)) ||
        (location.id == K22_PERIPHERAL_FMC && access == CORTEX_M4_ACCESS_UNPRIVILEGED_DATA)) {
        return false;
    }
    if (access != CORTEX_M4_ACCESS_DEBUG &&
        !kinetis_k22_internal_peripheral_clock_enabled(device, location.id)) {
        return false;
    }
    const bool debug_clock = access == CORTEX_M4_ACCESS_DEBUG &&
                             kinetis_k22_internal_enable_debug_clock(device, location.id);
    bool handled = kinetis_k22_internal_semantic_write(device, location.id, address, size, value);
    if (!handled) {
        handled = manifest_write(device, address, size, value);
    }
    if (handled && location.id == K22_PERIPHERAL_FMC) {
        apply_fmc_control(device, address, size, value);
    }
    if (handled && (address == K22_SIM_SCGC1 || address == K22_SIM_SCGC2)) {
        kinetis_k22_sync_clock_gates(device);
    }
    if (debug_clock) {
        kinetis_k22_sync_clock_gates(device);
    }
    kinetis_k22_refresh_signals(device);
    return handled;
}

static void reset_manifest(KinetisK22* device) {
    memset(device->peripheral, 0, K22_PERIPHERAL_SIZE);
    for (size_t index = 0; index < device->manifest->register_count; index++) {
        const K22RegisterDescriptor* descriptor = &device->manifest->registers[index];
        const uint8_t size = (uint8_t)(descriptor->width / 8u);
        if (descriptor->address >= K22_PERIPHERAL_BASE &&
            descriptor->address - K22_PERIPHERAL_BASE <= (uint32_t)K22_PERIPHERAL_SIZE - size) {
            kinetis_k22_internal_raw_store(device, descriptor->address, size,
                                           descriptor->reset_value & descriptor->implemented_mask);
        }
    }
}

void kinetis_k22_peripheral_reset(KinetisK22* device) {
    uint32_t external[5];
    uint32_t driven[5];
    memcpy(external, device->io.gpio_external, sizeof(external));
    memcpy(driven, device->io.gpio_external_drive, sizeof(driven));
    reset_manifest(device);
    device->cmt_cycles = 0u;
    device->cmt_bus_remainder = 0u;
    device->cmt_mark_ticks = 0u;
    device->cmt_period_ticks = 0u;
    device->cmt_carrier_high_ticks = 0u;
    device->cmt_carrier_period_ticks = 0u;
    device->cmt_carrier_offset_ticks = 0u;
    device->cmt_output_delay_ticks = 0u;
    k22_usbdcd_reset(&device->usbdcd);
    memset(device->fmc_bank, 0, sizeof(device->fmc_bank));
    memset(device->fmc_age, 0, sizeof(device->fmc_age));
    device->fmc_access_count = 0u;
    device->cmt_eoc_read = false;
    device->cmt_running = false;
    device->cmt_stop_pending = false;
    device->cmt_fsk_secondary = false;
    device->cmt_extended_space = false;
    device->cmt_dma_pending = false;
    memset(device->comparator_output, 0, sizeof(device->comparator_output));
    k22_data_reset(device->data);
    k22_serial_reset(&device->serial);
    k22_sdhc_reset(&device->sdhc);
    k22_io_reset(&device->io);
    memcpy(device->io.gpio_external, external, sizeof(external));
    memcpy(device->io.gpio_external_drive, driven, sizeof(driven));
    for (uint8_t port = 0; port < 5; port++) {
        device->io.gpio_filtered[port] = k22_io_pin_input(&device->io, port);
        device->io.gpio_pending[port] = device->io.gpio_filtered[port];
    }
    device->event_read_index = 0;
    device->event_write_index = 0;
    device->event_count = 0;
}

void kinetis_k22_peripheral_advance(KinetisK22* device, uint32_t cycles) {
    k22_timing_set_cpu_sleeping(&device->timing, device->cpu != NULL && device->cpu->sleeping,
                                device->cpu != NULL && (device->cpu->scr & 4u) != 0u);
    k22_timing_set_debug_halted(&device->timing, device->cpu != NULL && device->cpu->debug.halted);
    k22_data_set_debug_halted(device->data, device->cpu != NULL && device->cpu->debug.halted);
    k22_timing_advance(&device->timing, cycles);
    k22_data_advance(device->data, cycles);
    k22_serial_advance(&device->serial, cycles);
    k22_io_advance(&device->io, cycles);
    if (k22_profile_has_peripheral(device->profile, K22_PERIPHERAL_CMT) &&
        kinetis_k22_internal_peripheral_clock_enabled(device, K22_PERIPHERAL_CMT)) {
        kinetis_k22_internal_cmt_advance(device, cycles);
    }
    if (k22_profile_has_peripheral(device->profile, K22_PERIPHERAL_USBDCD) &&
        kinetis_k22_internal_peripheral_clock_enabled(device, K22_PERIPHERAL_USBDCD)) {
        k22_usbdcd_advance(&device->usbdcd, cycles);
        if (device->cpu != NULL)
            cortex_m4_set_irq_level(device->cpu, 54u, k22_usbdcd_irq(&device->usbdcd));
    }
    kinetis_k22_refresh_signals(device);
}

bool kinetis_k22_next_event(KinetisK22* device, KinetisK22Event* event) {
    if (device == NULL || event == NULL || device->event_count == 0) {
        return false;
    }
    *event = device->events[device->event_read_index];
    device->event_read_index = (uint8_t)((device->event_read_index + 1u) % K22_EVENT_CAPACITY);
    device->event_count--;
    return true;
}

bool kinetis_k22_set_adc_channel(KinetisK22* device, uint8_t instance, uint8_t channel,
                                 uint16_t value) {
    return device != NULL && k22_data_set_adc_input(device->data, instance, channel, value);
}
