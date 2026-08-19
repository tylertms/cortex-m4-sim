#include "kinetis_k22_internal.h"

#include <string.h>

enum {
    K22_SIM_SCGC1 = 0x40048028u,
    K22_SIM_SCGC2 = 0x4004802cu,
    K22_AIPS0 = 0x40000000u,
    K22_AIPS1 = 0x40080000u,
    K22_AXBS = 0x40004000u,
    K22_FMC = 0x4001f000u,
    K22_USBDCD = 0x40035000u,
    K22_CMT = 0x40062000u,
};

static uint32_t width_mask(uint8_t size) {
    return size == 1 ? 0xffu : size == 2 ? 0xffffu : UINT32_MAX;
}

static bool pop_serial_event(K22Serial* serial, K22SerialEndpoint endpoint,
                             K22SerialEvent* event) {
    for (uint8_t offset = 0; offset < serial->event_count; offset++) {
        const uint8_t index =
            (uint8_t)((serial->event_read_index + offset) % K22_SERIAL_EVENT_CAPACITY);
        if (serial->events[index].endpoint != endpoint) {
            continue;
        }
        *event = serial->events[index];
        for (uint8_t current = offset; current + 1u < serial->event_count; current++) {
            const uint8_t destination =
                (uint8_t)((serial->event_read_index + current) % K22_SERIAL_EVENT_CAPACITY);
            const uint8_t source = (uint8_t)((serial->event_read_index + current + 1u) %
                                             K22_SERIAL_EVENT_CAPACITY);
            serial->events[destination] = serial->events[source];
        }
        serial->event_write_index =
            (uint8_t)((serial->event_write_index + K22_SERIAL_EVENT_CAPACITY - 1u) %
                      K22_SERIAL_EVENT_CAPACITY);
        serial->event_count--;
        return true;
    }
    return false;
}

static uint32_t raw_load(const KinetisK22* device, uint32_t address, uint8_t size) {
    if (address < K22_PERIPHERAL_BASE ||
        address - K22_PERIPHERAL_BASE > (uint32_t)K22_PERIPHERAL_SIZE - size) {
        return 0;
    }
    const uint8_t* bytes = device->peripheral + address - K22_PERIPHERAL_BASE;
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; index++) {
        value |= (uint32_t)bytes[index] << (index * 8u);
    }
    return value;
}

static void raw_store(KinetisK22* device, uint32_t address, uint8_t size, uint32_t value) {
    if (address < K22_PERIPHERAL_BASE ||
        address - K22_PERIPHERAL_BASE > (uint32_t)K22_PERIPHERAL_SIZE - size) {
        return;
    }
    uint8_t* bytes = device->peripheral + address - K22_PERIPHERAL_BASE;
    for (uint8_t index = 0; index < size; index++) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

static bool aips_access_allowed(const KinetisK22* device, uint32_t address,
                                CortexM4Access access, bool write) {
    if (device->profile->id < K22_PROFILE_MK22FN1M012 || access == CORTEX_M4_ACCESS_DEBUG ||
        address < K22_PERIPHERAL_BASE ||
        address >= K22_PERIPHERAL_BASE + K22_PERIPHERAL_SIZE) {
        return true;
    }
    const uint32_t aperture = address < K22_AIPS1 ? K22_AIPS0 : K22_AIPS1;
    const uint8_t slot = (uint8_t)((address - aperture) >> 12u);
    const uint32_t control =
        raw_load(device, aperture + 0x20u + (uint32_t)(slot / 8u) * 4u, 4u);
    const uint8_t shift = (uint8_t)((7u - slot % 8u) * 4u);
    const uint8_t permission = (uint8_t)(control >> shift) & 7u;
    if (write && (permission & 2u) != 0u) {
        return false;
    }
    return !cortex_m4_access_is_unprivileged_data(device->cpu, access) ||
           (permission & 4u) == 0u;
}

static bool axbs_write_allowed(const KinetisK22* device, uint32_t address) {
    if (address < K22_AXBS || address >= K22_AXBS + 0x500u) {
        return true;
    }
    const uint32_t offset = address - K22_AXBS;
    const uint32_t register_offset = offset & 0xffu;
    if (register_offset != 0u && register_offset != 0x10u) {
        return true;
    }
    const uint32_t control = raw_load(device, address - register_offset + 0x10u, 4u);
    return (control & 0x80000000u) == 0u;
}

static bool cmt_read(KinetisK22* device, uint32_t address, uint8_t size, uint32_t* value) {
    if (address < K22_CMT || address >= K22_CMT + 0x0cu || size != 1u) {
        return false;
    }
    *value = raw_load(device, address, 1u);
    if (address == K22_CMT + 5u && (*value & 0x80u) != 0u) {
        device->cmt_eoc_read = true;
    }
    return true;
}

static void cmt_raise_cycle(KinetisK22* device, uint8_t control) {
    raw_store(device, K22_CMT + 5u, 1u, control | 0x80u);
    if ((control & 2u) != 0u && device->cpu != NULL) {
        cortex_m4_set_irq_level(device->cpu, 45u, true);
    }
    if ((raw_load(device, K22_CMT + 0x0bu, 1u) & 1u) != 0u) {
        k22_data_dma_request(device->data, 47u);
    }
}

static bool cmt_write(KinetisK22* device, uint32_t address, uint8_t size, uint32_t value) {
    if (address < K22_CMT || address >= K22_CMT + 0x0cu || size != 1u) {
        return false;
    }
    const uint32_t offset = address - K22_CMT;
    if (offset == 5u) {
        const uint8_t previous = (uint8_t)raw_load(device, address, 1u);
        const uint8_t control = (uint8_t)(value & 0x7fu);
        raw_store(device, address, 1u, (previous & 0x80u) | control);
        if ((value & 1u) == 0u) {
            device->cmt_cycles = 0u;
        } else if ((previous & 1u) == 0u) {
            device->cmt_cycles = 0u;
            cmt_raise_cycle(device, control);
        }
        return true;
    }
    const K22RegisterDescriptor* descriptor =
        k22_register_manifest_lookup(device->profile->id, address, 8u);
    if (descriptor == NULL || (descriptor->access & K22_REGISTER_ACCESS_WRITE) == 0u) {
        return false;
    }
    raw_store(device, address, 1u, value & descriptor->write_mask);
    if ((offset == 7u || offset == 9u) && device->cmt_eoc_read) {
        raw_store(device, K22_CMT + 5u, 1u, raw_load(device, K22_CMT + 5u, 1u) & 0x7fu);
        device->cmt_eoc_read = false;
        if (device->cpu != NULL) {
            cortex_m4_set_irq_level(device->cpu, 45u, false);
        }
    }
    return true;
}

static uint64_t cmt_period(const KinetisK22* device) {
    const uint32_t mark =
        (raw_load(device, K22_CMT + 6u, 1u) << 8u) | raw_load(device, K22_CMT + 7u, 1u);
    const uint32_t space =
        (raw_load(device, K22_CMT + 8u, 1u) << 8u) | raw_load(device, K22_CMT + 9u, 1u);
    const uint32_t primary = raw_load(device, K22_CMT + 0x0au, 1u) + 1u;
    const uint32_t secondary = 1u << ((raw_load(device, K22_CMT + 5u, 1u) >> 5u) & 3u);
    return (uint64_t)(mark + space + 1u) * 8u * primary * secondary;
}

static void cmt_advance(KinetisK22* device, uint32_t cycles) {
    const uint8_t control = (uint8_t)raw_load(device, K22_CMT + 5u, 1u);
    if ((control & 1u) == 0u) {
        return;
    }
    device->cmt_cycles += cycles;
    const uint64_t period = cmt_period(device);
    if (device->cmt_cycles < period) {
        return;
    }
    device->cmt_cycles %= period;
    cmt_raise_cycle(device, control);
}

static bool usbdcd_read(KinetisK22* device, uint32_t address, uint8_t size,
                        uint32_t* value) {
    if (address < K22_USBDCD || address >= K22_USBDCD + 0x1cu || size != 4u ||
        (address & 3u) != 0u) {
        return false;
    }
    *value = raw_load(device, address, 4u);
    return true;
}

static void usbdcd_reset(KinetisK22* device) {
    static const uint8_t offsets[] = {0u, 4u, 8u, 0x10u, 0x14u, 0x18u};
    for (size_t index = 0u; index < sizeof(offsets); index++) {
        const uint32_t address = K22_USBDCD + offsets[index];
        const K22RegisterDescriptor* descriptor =
            k22_register_manifest_lookup(device->profile->id, address, 32u);
        if (descriptor != NULL) {
            raw_store(device, address, 4u,
                      descriptor->reset_value & descriptor->implemented_mask);
        }
    }
    device->usbdcd_cycles = 0u;
    if (device->cpu != NULL) {
        cortex_m4_set_irq_level(device->cpu, 54u, false);
    }
}

static bool usbdcd_write(KinetisK22* device, uint32_t address, uint8_t size,
                         uint32_t value) {
    if (address != K22_USBDCD || size != 4u) {
        return false;
    }
    if ((value & (1u << 25u)) != 0u) {
        usbdcd_reset(device);
        return true;
    }
    uint32_t control = raw_load(device, K22_USBDCD, 4u);
    if ((value & 1u) != 0u) {
        control &= ~(1u << 8u);
        if (device->cpu != NULL) {
            cortex_m4_set_irq_level(device->cpu, 54u, false);
        }
    }
    control = (control & (1u << 8u)) | (value & 0x00030000u);
    if ((value & (1u << 24u)) != 0u &&
        (raw_load(device, K22_USBDCD + 8u, 4u) & (1u << 22u)) == 0u) {
        raw_store(device, K22_USBDCD + 8u, 4u, 1u << 22u);
        device->usbdcd_cycles = 0u;
    }
    raw_store(device, K22_USBDCD, 4u, control);
    return true;
}

static uint64_t usbdcd_period(const KinetisK22* device) {
    const uint32_t timer0 = raw_load(device, K22_USBDCD + 0x10u, 4u);
    const uint32_t timer1 = raw_load(device, K22_USBDCD + 0x14u, 4u);
    const uint32_t timer2 = raw_load(device, K22_USBDCD + 0x18u, 4u);
    uint64_t ticks = (timer0 & 0xfffu) + ((timer0 >> 16u) & 0x3ffu) + 1u;
    if (device->usb_charger != KINETIS_K22_USB_CHARGER_STANDARD_HOST) {
        ticks += (timer1 & 0x3ffu) + ((timer1 >> 16u) & 0x3ffu) + (timer2 & 0x3ffu) +
                 ((timer2 >> 16u) & 0x3ffu) + 1u;
    }
    const uint32_t clock = raw_load(device, K22_USBDCD + 4u, 4u);
    const uint32_t speed = (clock >> 2u) & 0x3ffu;
    const uint64_t hz =
        (uint64_t)(speed == 0u ? 1u : speed) * ((clock & 1u) != 0u ? 1000000u : 1000u);
    const uint64_t core_hz = k22_timing_core_clock_hz(&device->timing);
    const uint64_t cycles_per_tick = core_hz > hz ? core_hz / hz : 1u;
    return ticks * cycles_per_tick;
}

static void usbdcd_advance(KinetisK22* device, uint32_t cycles) {
    if ((raw_load(device, K22_USBDCD + 8u, 4u) & (1u << 22u)) == 0u) {
        return;
    }
    device->usbdcd_cycles += cycles;
    if (device->usbdcd_cycles < usbdcd_period(device)) {
        return;
    }
    uint32_t status = 0u;
    if (device->usb_charger == KINETIS_K22_USB_CHARGER_NONE) {
        status = (1u << 20u) | (1u << 21u);
    } else {
        status = ((uint32_t)device->usb_charger << 16u) | (3u << 18u);
    }
    raw_store(device, K22_USBDCD + 8u, 4u, status);
    uint32_t control = raw_load(device, K22_USBDCD, 4u) | (1u << 8u);
    raw_store(device, K22_USBDCD, 4u, control);
    if ((control & (1u << 16u)) != 0u && device->cpu != NULL) {
        cortex_m4_set_irq_level(device->cpu, 54u, true);
    }
}

static bool serial_peripheral(K22PeripheralId id) {
    return id == K22_PERIPHERAL_LPUART0 || id == K22_PERIPHERAL_SPI0 ||
           id == K22_PERIPHERAL_SPI1 || id == K22_PERIPHERAL_SPI2 ||
           id == K22_PERIPHERAL_I2C0 || id == K22_PERIPHERAL_I2C1 ||
           id == K22_PERIPHERAL_I2C2 || id == K22_PERIPHERAL_UART0 ||
           id == K22_PERIPHERAL_UART1 || id == K22_PERIPHERAL_UART2 ||
           id == K22_PERIPHERAL_UART3 || id == K22_PERIPHERAL_UART4 ||
           id == K22_PERIPHERAL_UART5;
}

static bool package_serial_extension(K22PeripheralId id) {
    return id == K22_PERIPHERAL_SPI1 || id == K22_PERIPHERAL_SPI2 ||
           id == K22_PERIPHERAL_UART3 || id == K22_PERIPHERAL_UART4 ||
           id == K22_PERIPHERAL_UART5;
}

static bool manifest_extension(K22PeripheralId id) {
    return package_serial_extension(id) || id == K22_PERIPHERAL_SDHC ||
           id == K22_PERIPHERAL_DAC1;
}

static K22PeripheralId serial_endpoint_peripheral(KinetisK22SerialEndpoint endpoint) {
    static const K22PeripheralId peripherals[KINETIS_K22_SERIAL_ENDPOINT_COUNT] = {
        K22_PERIPHERAL_LPUART0, K22_PERIPHERAL_SPI0,  K22_PERIPHERAL_SPI1,
        K22_PERIPHERAL_SPI2,    K22_PERIPHERAL_I2C0,  K22_PERIPHERAL_I2C1,
        K22_PERIPHERAL_I2C2,    K22_PERIPHERAL_UART0, K22_PERIPHERAL_UART1,
        K22_PERIPHERAL_UART2,   K22_PERIPHERAL_UART3, K22_PERIPHERAL_UART4,
        K22_PERIPHERAL_UART5,
    };
    return endpoint < KINETIS_K22_SERIAL_ENDPOINT_COUNT ? peripherals[endpoint]
                                                        : K22_PERIPHERAL_COUNT;
}

static bool serial_endpoint_available(const KinetisK22* device,
                                      KinetisK22SerialEndpoint endpoint) {
    const K22PeripheralId peripheral = serial_endpoint_peripheral(endpoint);
    return device != NULL && peripheral < K22_PERIPHERAL_COUNT &&
           k22_package_has_peripheral(device->package, peripheral);
}

static bool data_peripheral(K22PeripheralId id) {
    return id == K22_PERIPHERAL_FLASH_CONFIG || id == K22_PERIPHERAL_DMA ||
           id == K22_PERIPHERAL_FTFA || id == K22_PERIPHERAL_FTFE ||
           id == K22_PERIPHERAL_DMAMUX || id == K22_PERIPHERAL_ADC0 ||
           id == K22_PERIPHERAL_ADC1 || id == K22_PERIPHERAL_DAC0 ||
           id == K22_PERIPHERAL_DAC1 || id == K22_PERIPHERAL_RNG ||
           id == K22_PERIPHERAL_CRC || id == K22_PERIPHERAL_CMP0 ||
           id == K22_PERIPHERAL_CMP1 || id == K22_PERIPHERAL_CMP2 ||
           id == K22_PERIPHERAL_VREF;
}

static bool timing_peripheral(K22PeripheralId id) {
    return id == K22_PERIPHERAL_FTM0 || id == K22_PERIPHERAL_FTM1 ||
           id == K22_PERIPHERAL_FTM2 || id == K22_PERIPHERAL_FTM3 ||
           id == K22_PERIPHERAL_PDB0 || id == K22_PERIPHERAL_PIT ||
           id == K22_PERIPHERAL_RTC || id == K22_PERIPHERAL_LPTMR0 ||
           id == K22_PERIPHERAL_SIM || id == K22_PERIPHERAL_WDOG ||
           id == K22_PERIPHERAL_EWM || id == K22_PERIPHERAL_MCG ||
           id == K22_PERIPHERAL_OSC || id == K22_PERIPHERAL_LLWU ||
           id == K22_PERIPHERAL_PMC || id == K22_PERIPHERAL_SMC || id == K22_PERIPHERAL_RCM;
}

static bool io_peripheral(K22PeripheralId id) {
    return id == K22_PERIPHERAL_FB || id == K22_PERIPHERAL_SYSMPU ||
           id == K22_PERIPHERAL_CAN0 || id == K22_PERIPHERAL_I2S0 ||
           id == K22_PERIPHERAL_USB0 || id == K22_PERIPHERAL_PORTA ||
           id == K22_PERIPHERAL_PORTB || id == K22_PERIPHERAL_PORTC ||
           id == K22_PERIPHERAL_PORTD || id == K22_PERIPHERAL_PORTE ||
           id == K22_PERIPHERAL_GPIOA || id == K22_PERIPHERAL_GPIOB ||
           id == K22_PERIPHERAL_GPIOC || id == K22_PERIPHERAL_GPIOD ||
           id == K22_PERIPHERAL_GPIOE || id == K22_PERIPHERAL_MCM;
}

static uint8_t data_irq(K22DataInterrupt interrupt) {
    static const uint8_t irqs[K22_DATA_INTERRUPT_COUNT] = {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
        13, 14, 15, 16, 18, 39, 73, 56, 72, 40, 41, 70, 23,
    };
    return irqs[interrupt];
}

static void adc_alternate_trigger(KinetisK22* device, uint8_t source) {
    for (uint8_t instance = 0u; instance < 2u; instance++) {
        const uint8_t selection = (uint8_t)(device->timing.sim_sopt7 >> (instance * 8u));
        if ((selection & 0x80u) != 0u && (selection & 15u) == source)
            k22_data_adc_pretrigger(device->data, instance,
                                    (uint8_t)((selection >> 4u) & 1u));
    }
}

static void data_interrupt(void* context, K22DataInterrupt interrupt, bool asserted) {
    KinetisK22* device = context;
    if (interrupt >= K22_DATA_INTERRUPT_CMP0 &&
        interrupt <= K22_DATA_INTERRUPT_CMP2 && device->data != NULL) {
        const uint8_t instance = (uint8_t)(interrupt - K22_DATA_INTERRUPT_CMP0);
        bool high = false;
        if (k22_data_get_cmp_output(device->data, instance, &high)) {
            if (instance == 0u)
                k22_timing_set_lptmr_input(&device->timing, 0u, high);
            if (high && !device->comparator_output[instance])
                adc_alternate_trigger(device, (uint8_t)(1u + instance));
            device->comparator_output[instance] = high;
        }
    }
    if (device->cpu != NULL && interrupt < K22_DATA_INTERRUPT_COUNT) {
        cortex_m4_set_irq_level(device->cpu, data_irq(interrupt), asserted);
    }
}

static bool data_bus_read(void* context, uint32_t address, uint8_t size, uint32_t* value) {
    return kinetis_k22_dma_read(context, address, size, value);
}

static bool data_bus_write(void* context, uint32_t address, uint8_t size, uint32_t value) {
    return kinetis_k22_dma_write(context, address, size, value);
}

static bool flash_bus_write(void* context, uint32_t address, uint8_t size, uint32_t value) {
    return kinetis_k22_flash_controller_write(context, address, size, value);
}

static void timing_irq(void* context, uint8_t irq, bool asserted) {
    KinetisK22* device = context;
    if (device->cpu != NULL) {
        cortex_m4_set_irq_level(device->cpu, irq, asserted);
    }
}

static void timing_dma(void* context, uint8_t source) {
    KinetisK22* device = context;
    k22_data_dma_request(device->data, source);
}

static void timing_reset(void* context, uint8_t cause_0, uint8_t cause_1) {
    kinetis_k22_warm_reset(context, cause_0, cause_1);
}

static void timing_trigger(void* context, K22TimingTrigger type, uint8_t instance,
                           uint8_t channel) {
    KinetisK22* device = context;
    if (type == K22_TIMING_TRIGGER_PDB_ADC) {
        const uint8_t selection = (uint8_t)(device->timing.sim_sopt7 >> (instance * 8u));
        if ((selection & 0x80u) == 0u)
            k22_data_adc_pretrigger(device->data, instance, channel);
    } else if (type == K22_TIMING_TRIGGER_PDB_DAC) {
        k22_data_dac_trigger(device->data, instance);
    } else {
        adc_alternate_trigger(device, instance);
    }
}

static void queue_event(KinetisK22* device, const K22IoEvent* source) {
    if (device->event_count == K22_EVENT_CAPACITY) {
        device->event_read_index =
            (uint8_t)((device->event_read_index + 1u) % K22_EVENT_CAPACITY);
        device->event_count--;
    }
    KinetisK22Event* event = &device->events[device->event_write_index];
    event->type = (KinetisK22EventType)source->type;
    event->source = source->source;
    event->value = source->value;
    event->auxiliary = source->auxiliary;
    memcpy(event->data, source->data, sizeof(event->data));
    event->length = source->length;
    event->extended = source->extended;
    event->remote = source->remote;
    device->event_write_index =
        (uint8_t)((device->event_write_index + 1u) % K22_EVENT_CAPACITY);
    device->event_count++;
}

static void io_event(void* context, const K22IoEvent* event) {
    KinetisK22* device = context;
    queue_event(device, event);
    if (event->type == K22_IO_EVENT_DMA) {
        uint8_t source = event->auxiliary == 0 ? (event->source == 0 ? 13u : 12u)
                                               : (uint8_t)(49u + event->source / 32u);
        k22_data_dma_request(device->data, source);
    }
}

static bool gate(uint32_t value, uint8_t bit) { return (value & (1u << bit)) != 0; }

static bool peripheral_clock_enabled(const KinetisK22* device, K22PeripheralId id) {
    const uint32_t scgc1 = raw_load(device, K22_SIM_SCGC1, 4);
    const uint32_t scgc3 = device->timing.sim_scgc3;
    const uint32_t scgc4 = device->timing.sim_scgc4;
    const uint32_t scgc5 = device->timing.sim_scgc5;
    const uint32_t scgc6 = device->timing.sim_scgc6;
    const uint32_t scgc7 = device->timing.sim_scgc7;
    if (id >= K22_PERIPHERAL_PORTA && id <= K22_PERIPHERAL_PORTE) {
        return gate(scgc5, (uint8_t)(9u + id - K22_PERIPHERAL_PORTA));
    }
    if (id >= K22_PERIPHERAL_GPIOA && id <= K22_PERIPHERAL_GPIOE) {
        return gate(scgc5, (uint8_t)(9u + id - K22_PERIPHERAL_GPIOA));
    }
    switch (id) {
    case K22_PERIPHERAL_DMA:
        return gate(scgc7, 1);
    case K22_PERIPHERAL_FB:
        return gate(scgc7, 0);
    case K22_PERIPHERAL_SYSMPU:
        return gate(scgc7, 2);
    case K22_PERIPHERAL_FTFA:
    case K22_PERIPHERAL_FTFE:
        return gate(scgc6, 0);
    case K22_PERIPHERAL_DMAMUX:
        return gate(scgc6, 1);
    case K22_PERIPHERAL_CAN0:
        return gate(scgc6, 4);
    case K22_PERIPHERAL_FTM0:
    case K22_PERIPHERAL_FTM1:
        return gate(scgc6, (uint8_t)(24u + id - K22_PERIPHERAL_FTM0));
    case K22_PERIPHERAL_FTM2:
        return device->profile->id >= K22_PROFILE_MK22FN1M012 ? gate(scgc3, 24)
                                                              : gate(scgc6, 26);
    case K22_PERIPHERAL_FTM3:
        return device->profile->id >= K22_PROFILE_MK22FN1M012 ? gate(scgc3, 25)
                                                              : gate(scgc6, 6);
    case K22_PERIPHERAL_ADC0:
        return gate(scgc6, 27);
    case K22_PERIPHERAL_ADC1:
        return device->profile->id >= K22_PROFILE_MK22FN1M012 ? gate(scgc3, 27)
                                                              : gate(scgc6, 7);
    case K22_PERIPHERAL_DAC0:
        return device->profile->id >= K22_PROFILE_MK22FN1M012
                   ? gate(raw_load(device, K22_SIM_SCGC2, 4), 12)
                   : gate(scgc6, 31);
    case K22_PERIPHERAL_DAC1:
        return device->profile->id >= K22_PROFILE_MK22FN1M012
                   ? gate(raw_load(device, K22_SIM_SCGC2, 4), 13)
                   : gate(scgc6, 8);
    case K22_PERIPHERAL_RNG:
        return gate(scgc6, 9);
    case K22_PERIPHERAL_LPUART0:
        return gate(scgc6, 10);
    case K22_PERIPHERAL_SPI0:
        return gate(scgc6, 12);
    case K22_PERIPHERAL_SPI1:
        return gate(scgc6, 13);
    case K22_PERIPHERAL_SPI2:
        return gate(scgc3, 12);
    case K22_PERIPHERAL_SDHC:
        return gate(scgc3, 17);
    case K22_PERIPHERAL_I2S0:
        return gate(scgc6, 15);
    case K22_PERIPHERAL_CRC:
        return gate(scgc6, 18);
    case K22_PERIPHERAL_USBDCD:
        return gate(scgc6, 21);
    case K22_PERIPHERAL_PDB0:
        return gate(scgc6, 22);
    case K22_PERIPHERAL_PIT:
        return gate(scgc6, 23);
    case K22_PERIPHERAL_RTC:
        return gate(scgc6, 29);
    case K22_PERIPHERAL_LPTMR0:
        return gate(scgc5, 0);
    case K22_PERIPHERAL_CMT:
        return gate(scgc4, 2);
    case K22_PERIPHERAL_I2C0:
        return gate(scgc4, 6);
    case K22_PERIPHERAL_I2C1:
        return gate(scgc4, 7);
    case K22_PERIPHERAL_I2C2:
        return gate(scgc1, 6);
    case K22_PERIPHERAL_UART0:
        return gate(scgc4, 10);
    case K22_PERIPHERAL_UART1:
        return gate(scgc4, 11);
    case K22_PERIPHERAL_UART2:
        return gate(scgc4, 12);
    case K22_PERIPHERAL_UART3:
        return gate(scgc4, 13);
    case K22_PERIPHERAL_UART4:
        return gate(scgc1, 10);
    case K22_PERIPHERAL_UART5:
        return gate(scgc1, 11);
    case K22_PERIPHERAL_USB0:
        return gate(scgc4, 18);
    case K22_PERIPHERAL_CMP0:
    case K22_PERIPHERAL_CMP1:
    case K22_PERIPHERAL_CMP2:
        return gate(scgc4, 19);
    case K22_PERIPHERAL_VREF:
        return gate(scgc4, 20);
    default:
        return true;
    }
}

void kinetis_k22_sync_clock_gates(KinetisK22* device) {
    const uint32_t scgc1 = raw_load(device, K22_SIM_SCGC1, 4);
    const uint32_t scgc3 = device->timing.sim_scgc3;
    const uint32_t scgc4 = device->timing.sim_scgc4;
    const uint32_t scgc5 = device->timing.sim_scgc5;
    const uint32_t scgc6 = device->timing.sim_scgc6;
    const uint32_t scgc7 = device->timing.sim_scgc7;
    k22_serial_set_clocks(&device->serial, k22_timing_core_clock_hz(&device->timing),
                          k22_timing_bus_clock_hz(&device->timing));
    k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_LPUART0, gate(scgc6, 10));
    k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_SPI0, gate(scgc6, 12));
    k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_SPI1, gate(scgc6, 13));
    k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_SPI2, gate(scgc3, 12));
    k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_I2C0, gate(scgc4, 6));
    k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_I2C1, gate(scgc4, 7));
    k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_I2C2, gate(scgc1, 6));
    k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_UART0, gate(scgc4, 10));
    k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_UART1, gate(scgc4, 11));
    k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_UART2, gate(scgc4, 12));
    k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_UART3, gate(scgc4, 13));
    k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_UART4, gate(scgc1, 10));
    k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_UART5, gate(scgc1, 11));
    k22_sdhc_set_clock(&device->sdhc, gate(scgc3, 17));
    for (uint8_t port = 0; port < 5; port++) {
        k22_io_set_clock(&device->io, (K22PeripheralId)(K22_PERIPHERAL_PORTA + port),
                         gate(scgc5, (uint8_t)(9u + port)));
    }
    k22_io_set_clock(&device->io, K22_PERIPHERAL_USB0, gate(scgc4, 18));
    k22_io_set_clock(&device->io, K22_PERIPHERAL_I2S0, gate(scgc6, 15));
    k22_io_set_clock(&device->io, K22_PERIPHERAL_FB, gate(scgc7, 0));
    k22_io_set_clock(&device->io, K22_PERIPHERAL_CAN0, gate(scgc6, 4));
    k22_io_set_clock(&device->io, K22_PERIPHERAL_SYSMPU, gate(scgc7, 2));
}

static void refresh_serial_signals(KinetisK22* device) {
    static const uint8_t irqs[K22_SERIAL_IRQ_COUNT] = {
        30, 26, 27, 65, 24, 25, 74, 31, 32, 33, 34, 35, 36, 37, 38, 66, 67, 68, 69,
    };
    static const uint8_t dma_sources[K22_SERIAL_DMA_COUNT] = {
        58, 59, 14, 15, 16, 16, 17, 17, 18, 19, 19, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 11, 11,
    };
    if (device->cpu != NULL) {
        for (uint8_t index = 0; index < K22_SERIAL_IRQ_COUNT; index++) {
            cortex_m4_set_irq_level(device->cpu, irqs[index],
                                    k22_serial_irq(&device->serial, (K22SerialIrq)index));
        }
    }
    for (uint8_t index = 0; index < K22_SERIAL_DMA_COUNT; index++) {
        if (k22_serial_dma_request(&device->serial, (K22SerialDmaRequest)index)) {
            k22_data_dma_request(device->data, dma_sources[index]);
        }
    }
}

void kinetis_k22_refresh_signals(KinetisK22* device) {
    if (device != NULL) {
        refresh_serial_signals(device);
        if (device->cpu != NULL) {
            static const uint8_t irqs[] = {28u, 29u, 53u, 59u, 60u, 61u, 62u, 63u, 75u};
            for (size_t index = 0; index < sizeof(irqs); index++) {
                cortex_m4_set_irq_level(device->cpu, irqs[index],
                                        k22_io_irq_asserted(&device->io, irqs[index]));
            }
            cortex_m4_set_irq_level(device->cpu, 81u, k22_sdhc_irq(&device->sdhc));
        }
    }
}

static bool enable_debug_clock(KinetisK22* device, K22PeripheralId id) {
    if (serial_peripheral(id)) {
        return k22_serial_set_clock_gate(&device->serial, id, true);
    }
    if (id == K22_PERIPHERAL_SDHC) {
        k22_sdhc_set_clock(&device->sdhc, true);
        return true;
    }
    if (io_peripheral(id)) {
        k22_io_set_clock(&device->io, id, true);
        return true;
    }
    return false;
}

static bool semantic_read(KinetisK22* device, K22PeripheralId id, uint32_t address,
                          uint8_t size, uint32_t* value) {
    if (id == K22_PERIPHERAL_CMT) {
        return cmt_read(device, address, size, value);
    }
    if (id == K22_PERIPHERAL_USBDCD) {
        return usbdcd_read(device, address, size, value);
    }
    if (timing_peripheral(id) && k22_timing_read(&device->timing, address, size, value)) {
        return true;
    }
    if (data_peripheral(id) && k22_data_read(device->data, address, size, value)) {
        return true;
    }
    if (serial_peripheral(id) && k22_serial_read(&device->serial, address, size, value)) {
        return true;
    }
    if (id == K22_PERIPHERAL_SDHC)
        return k22_sdhc_read(&device->sdhc, address, size, value);
    return io_peripheral(id) && k22_io_read(&device->io, address, size, value);
}

static bool semantic_write(KinetisK22* device, K22PeripheralId id, uint32_t address,
                           uint8_t size, uint32_t value) {
    if (id == K22_PERIPHERAL_CMT) {
        return cmt_write(device, address, size, value);
    }
    if (id == K22_PERIPHERAL_USBDCD) {
        return usbdcd_write(device, address, size, value);
    }
    if (timing_peripheral(id) && k22_timing_write(&device->timing, address, size, value)) {
        kinetis_k22_sync_clock_gates(device);
        return true;
    }
    if (data_peripheral(id) && k22_data_write(device->data, address, size, value)) {
        return true;
    }
    if (serial_peripheral(id) && k22_serial_write(&device->serial, address, size, value)) {
        return true;
    }
    if (id == K22_PERIPHERAL_SDHC)
        return k22_sdhc_write(&device->sdhc, address, size, value);
    return io_peripheral(id) && k22_io_write(&device->io, address, size, value);
}

static const K22RegisterDescriptor*
manifest_descriptor_for_access(const KinetisK22* device, uint32_t address, uint8_t size) {
    const K22RegisterDescriptor* exact =
        k22_register_manifest_lookup(device->profile->id, address, (uint8_t)(size * 8u));
    if (exact != NULL) {
        return exact;
    }
    const K22RegisterManifest* manifest = device->manifest;
    const uint32_t end = address + size;
    for (size_t index = 0u; index < manifest->register_count; index++) {
        const K22RegisterDescriptor* candidate = &manifest->registers[index];
        const uint32_t candidate_end = candidate->address + candidate->width / 8u;
        if (candidate->address <= address && candidate_end >= end)
            return candidate;
    }
    return NULL;
}

static uint32_t manifest_access_mask(const K22RegisterDescriptor* descriptor,
                                     uint32_t address, uint32_t mask) {
    return mask >> ((address - descriptor->address) * 8u);
}

static bool manifest_read(KinetisK22* device, uint32_t address, uint8_t size,
                          uint32_t* value) {
    const K22RegisterDescriptor* descriptor =
        manifest_descriptor_for_access(device, address, size);
    if (descriptor == NULL || (descriptor->access & K22_REGISTER_ACCESS_READ) == 0 ||
        address < K22_PERIPHERAL_BASE) {
        return false;
    }
    *value = raw_load(device, address, size) &
             manifest_access_mask(descriptor, address, descriptor->read_mask) &
             manifest_access_mask(descriptor, address, descriptor->implemented_mask) &
             width_mask(size);
    return true;
}

static bool manifest_write(KinetisK22* device, uint32_t address, uint8_t size,
                           uint32_t value) {
    const K22RegisterDescriptor* descriptor =
        manifest_descriptor_for_access(device, address, size);
    if (descriptor == NULL || address < K22_PERIPHERAL_BASE) {
        return false;
    }
    if ((descriptor->access & K22_REGISTER_ACCESS_WRITE) == 0) {
        return true;
    }
    const uint32_t mask =
        manifest_access_mask(descriptor, address, descriptor->implemented_mask) &
        width_mask(size);
    const uint32_t write_mask =
        manifest_access_mask(descriptor, address, descriptor->write_mask);
    const uint32_t w1c_mask =
        manifest_access_mask(descriptor, address, descriptor->w1c_mask);
    const uint32_t writable = write_mask & ~w1c_mask & mask;
    const uint32_t clear = w1c_mask & value & mask;
    uint32_t current = raw_load(device, address, size);
    current = (current & ~writable) | (value & writable);
    current &= ~clear;
    raw_store(device, address, size, current & mask);
    return true;
}

static void apply_fmc_control(KinetisK22* device, uint32_t address, uint8_t size,
                              uint32_t value) {
    if (address != K22_FMC + 4u || size != 4u) {
        return;
    }
    const uint8_t ways = (uint8_t)((value >> 20u) & 0x0fu);
    const uint8_t sets = device->profile->id >= K22_PROFILE_MK22FN1M012 ? 4u : 8u;
    for (uint8_t way = 0u; way < 4u; way++) {
        if ((ways & (1u << way)) == 0u) {
            continue;
        }
        for (uint8_t set = 0u; set < sets; set++) {
            const uint32_t tag_address =
                K22_FMC + 0x100u + ((uint32_t)way * sets + set) * 4u;
            if (k22_register_manifest_lookup(device->profile->id, tag_address, 32u) !=
                NULL) {
                raw_store(device, tag_address, 4u, raw_load(device, tag_address, 4u) & ~1u);
            }
        }
    }
    const uint32_t control = raw_load(device, address, 4u) & ~0x00f80000u;
    raw_store(device, address, 4u, control);
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
    descriptor = manifest_descriptor_for_access(device, address, size);
    if (location.id != K22_PERIPHERAL_MCM && !manifest_extension(location.id) &&
        (descriptor == NULL || (descriptor->access & K22_REGISTER_ACCESS_READ) == 0u)) {
        return false;
    }
    if (!aips_access_allowed(device, address, access, false)) {
        return false;
    }
    if (access != CORTEX_M4_ACCESS_DEBUG &&
        !peripheral_clock_enabled(device, location.id)) {
        return false;
    }
    const bool debug_clock =
        access == CORTEX_M4_ACCESS_DEBUG && enable_debug_clock(device, location.id);
    bool handled = semantic_read(device, location.id, address, size, value);
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
    descriptor = manifest_descriptor_for_access(device, address, size);
    if (location.id != K22_PERIPHERAL_MCM && !manifest_extension(location.id) &&
        descriptor == NULL) {
        return false;
    }
    if (descriptor != NULL && (descriptor->access & K22_REGISTER_ACCESS_WRITE) == 0u) {
        return true;
    }
    if (!aips_access_allowed(device, address, access, true) ||
        (location.id == K22_PERIPHERAL_AXBS && !axbs_write_allowed(device, address))) {
        return false;
    }
    if (access != CORTEX_M4_ACCESS_DEBUG &&
        !peripheral_clock_enabled(device, location.id)) {
        return false;
    }
    const bool debug_clock =
        access == CORTEX_M4_ACCESS_DEBUG && enable_debug_clock(device, location.id);
    bool handled = semantic_write(device, location.id, address, size, value);
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
            descriptor->address - K22_PERIPHERAL_BASE <=
                (uint32_t)K22_PERIPHERAL_SIZE - size) {
            raw_store(device, descriptor->address, size,
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
    device->usbdcd_cycles = 0u;
    device->cmt_eoc_read = false;
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
    k22_timing_set_debug_halted(&device->timing,
                                device->cpu != NULL && device->cpu->debug.halted);
    k22_timing_advance(&device->timing, cycles);
    k22_data_advance(device->data, cycles);
    k22_serial_advance(&device->serial, cycles);
    k22_io_advance(&device->io, cycles);
    if (k22_profile_has_peripheral(device->profile, K22_PERIPHERAL_CMT) &&
        peripheral_clock_enabled(device, K22_PERIPHERAL_CMT)) {
        cmt_advance(device, cycles);
    }
    if (k22_profile_has_peripheral(device->profile, K22_PERIPHERAL_USBDCD) &&
        peripheral_clock_enabled(device, K22_PERIPHERAL_USBDCD)) {
        usbdcd_advance(device, cycles);
    }
    kinetis_k22_refresh_signals(device);
}

bool kinetis_k22_next_event(KinetisK22* device, KinetisK22Event* event) {
    if (device == NULL || event == NULL || device->event_count == 0) {
        return false;
    }
    *event = device->events[device->event_read_index];
    device->event_read_index =
        (uint8_t)((device->event_read_index + 1u) % K22_EVENT_CAPACITY);
    device->event_count--;
    return true;
}

bool kinetis_k22_set_adc_channel(KinetisK22* device, uint8_t instance, uint8_t channel,
                                 uint16_t value) {
    return device != NULL && k22_data_set_adc_input(device->data, instance, channel, value);
}

void kinetis_k22_set_adc0_channel(KinetisK22* device, uint8_t channel, uint16_t value) {
    (void)kinetis_k22_set_adc_channel(device, 0, channel, value);
}

bool kinetis_k22_set_cmp_input(KinetisK22* device, uint8_t instance, uint8_t input,
                               uint8_t value) {
    return device != NULL && k22_data_set_cmp_input(device->data, instance, input, value);
}

bool kinetis_k22_set_lptmr_input(KinetisK22* device, uint8_t input, bool high) {
    return device != NULL && k22_timing_set_lptmr_input(&device->timing, input, high);
}

bool kinetis_k22_get_dac_output(const KinetisK22* device, uint8_t instance,
                                uint16_t* value) {
    return device != NULL && instance < 2u &&
           k22_package_has_peripheral(device->package,
                                      (K22PeripheralId)(K22_PERIPHERAL_DAC0 + instance)) &&
           k22_data_get_dac_output(device->data, instance, value);
}

bool kinetis_k22_set_usb_charger(KinetisK22* device, KinetisK22UsbCharger charger) {
    if (device == NULL || charger > KINETIS_K22_USB_CHARGER_DEDICATED ||
        !k22_profile_has_peripheral(device->profile, K22_PERIPHERAL_USBDCD)) {
        return false;
    }
    device->usb_charger = charger;
    return true;
}

void kinetis_k22_rng_seed(KinetisK22* device, uint32_t seed) {
    if (device != NULL) {
        k22_data_rng_seed(device->data, seed);
    }
}

void kinetis_k22_gpio_drive(KinetisK22* device, uint8_t port, uint8_t pin, bool high) {
    if (device != NULL) {
        (void)k22_io_drive_pin(&device->io, port, pin, high);
        kinetis_k22_refresh_signals(device);
    }
}

void kinetis_k22_gpio_release(KinetisK22* device, uint8_t port, uint8_t pin) {
    if (device != NULL) {
        (void)k22_io_release_pin(&device->io, port, pin);
        kinetis_k22_refresh_signals(device);
    }
}

bool kinetis_k22_serial_receive(KinetisK22* device, KinetisK22SerialEndpoint endpoint,
                                uint16_t value, uint8_t status) {
    if (!serial_endpoint_available(device, endpoint)) {
        return false;
    }
    const K22PeripheralId id = serial_endpoint_peripheral(endpoint);
    (void)k22_serial_set_clock_gate(&device->serial, id, true);
    const bool result = k22_serial_push_receive(&device->serial,
                                                (K22SerialEndpoint)endpoint, value, status);
    k22_serial_advance(&device->serial, UINT32_MAX);
    refresh_serial_signals(device);
    kinetis_k22_sync_clock_gates(device);
    return result;
}

bool kinetis_k22_serial_transmit(KinetisK22* device, KinetisK22SerialEndpoint endpoint,
                                 uint16_t* value) {
    return serial_endpoint_available(device, endpoint) &&
           k22_serial_pop_transmit(&device->serial, (K22SerialEndpoint)endpoint, value);
}

bool kinetis_k22_spi_transfer(KinetisK22* device, KinetisK22SerialEndpoint endpoint,
                              KinetisK22SpiTransfer* transfer) {
    if (!serial_endpoint_available(device, endpoint) || transfer == NULL ||
        endpoint < KINETIS_K22_SERIAL_SPI0 || endpoint > KINETIS_K22_SERIAL_SPI2) {
        return false;
    }
    K22SerialSpiTransfer internal;
    if (!k22_serial_pop_spi_transfer(&device->serial, (K22SerialEndpoint)endpoint,
                                     &internal)) {
        return false;
    }
    transfer->data = internal.data;
    transfer->chip_selects = internal.chip_selects;
    transfer->clock_and_transfer_attributes = internal.clock_and_transfer_attributes;
    transfer->continuous_chip_select = internal.continuous_chip_select;
    transfer->end_of_queue = internal.end_of_queue;
    return true;
}

bool kinetis_k22_i2c_transfer(KinetisK22* device, KinetisK22SerialEndpoint endpoint,
                              KinetisK22I2cTransfer* transfer) {
    if (!serial_endpoint_available(device, endpoint) || transfer == NULL ||
        endpoint < KINETIS_K22_SERIAL_I2C0 || endpoint > KINETIS_K22_SERIAL_I2C2) {
        return false;
    }
    K22SerialEvent event;
    if (!pop_serial_event(&device->serial, (K22SerialEndpoint)endpoint, &event)) {
        return false;
    }
    static const KinetisK22I2cTransferType types[] = {
        KINETIS_K22_I2C_START, KINETIS_K22_I2C_REPEATED_START, KINETIS_K22_I2C_STOP,
        KINETIS_K22_I2C_WRITE, KINETIS_K22_I2C_READ,
    };
    if ((unsigned)event.type >= sizeof(types) / sizeof(types[0])) {
        return false;
    }
    transfer->type = types[event.type];
    transfer->value = (uint8_t)event.value;
    return true;
}

bool kinetis_k22_i2c_acknowledge(KinetisK22* device, KinetisK22SerialEndpoint endpoint,
                                 bool acknowledge) {
    if (!serial_endpoint_available(device, endpoint) ||
        endpoint < KINETIS_K22_SERIAL_I2C0 || endpoint > KINETIS_K22_SERIAL_I2C2) {
        return false;
    }
    const bool result = k22_serial_i2c_set_acknowledge(
        &device->serial, (K22SerialEndpoint)endpoint, acknowledge);
    k22_serial_advance(&device->serial, UINT32_MAX);
    refresh_serial_signals(device);
    return result;
}

bool kinetis_k22_i2c_lose_arbitration(KinetisK22* device,
                                      KinetisK22SerialEndpoint endpoint) {
    if (!serial_endpoint_available(device, endpoint) ||
        endpoint < KINETIS_K22_SERIAL_I2C0 || endpoint > KINETIS_K22_SERIAL_I2C2) {
        return false;
    }
    const bool result =
        k22_serial_i2c_lose_arbitration(&device->serial, (K22SerialEndpoint)endpoint);
    refresh_serial_signals(device);
    return result;
}

bool kinetis_k22_i2c_receive(KinetisK22* device, KinetisK22SerialEndpoint endpoint,
                             uint8_t value) {
    return endpoint >= KINETIS_K22_SERIAL_I2C0 && endpoint <= KINETIS_K22_SERIAL_I2C2 &&
           kinetis_k22_serial_receive(device, endpoint, value, 0);
}

bool kinetis_k22_usb_token(KinetisK22* device, uint8_t endpoint, uint8_t token,
                           bool transmit) {
    if (device == NULL)
        return false;
    const bool result = k22_io_usb_token(&device->io, endpoint, token, transmit);
    kinetis_k22_refresh_signals(device);
    return result;
}

bool kinetis_k22_can_receive(KinetisK22* device, const KinetisK22CanFrame* frame) {
    if (device == NULL || frame == NULL) {
        return false;
    }
    K22CanFrame internal;
    internal.identifier = frame->identifier;
    internal.length = frame->length;
    memcpy(internal.data, frame->data, sizeof(internal.data));
    internal.extended = frame->extended;
    internal.remote = frame->remote;
    const bool result = k22_io_can_receive(&device->io, &internal);
    kinetis_k22_refresh_signals(device);
    return result;
}

bool kinetis_k22_i2s_receive(KinetisK22* device, uint32_t sample) {
    if (device == NULL)
        return false;
    const bool result = k22_io_i2s_receive(&device->io, sample);
    kinetis_k22_refresh_signals(device);
    return result;
}

bool kinetis_k22_i2s_transmit(KinetisK22* device, uint32_t* sample) {
    if (device == NULL)
        return false;
    const bool result = k22_io_i2s_transmit(&device->io, sample);
    kinetis_k22_refresh_signals(device);
    return result;
}

bool kinetis_k22_sdhc_insert(KinetisK22* device, const void* data, size_t size,
                             bool write_protected) {
    if (device == NULL || !k22_package_has_peripheral(device->package, K22_PERIPHERAL_SDHC))
        return false;
    const bool result = k22_sdhc_insert(&device->sdhc, data, size, write_protected);
    kinetis_k22_refresh_signals(device);
    return result;
}

void kinetis_k22_sdhc_eject(KinetisK22* device) {
    if (device != NULL) {
        k22_sdhc_eject(&device->sdhc);
        kinetis_k22_refresh_signals(device);
    }
}

bool kinetis_k22_sdhc_read_card(const KinetisK22* device, size_t offset, void* data,
                                size_t size) {
    return device != NULL && k22_sdhc_read_card(&device->sdhc, offset, data, size);
}

bool kinetis_k22_uart1_receive(KinetisK22* device, uint8_t value, uint8_t status) {
    if (device == NULL) {
        return false;
    }
    K22SerialUart* uart = &device->serial.uart[1];
    if (!uart->present || uart->receive.count == K22_SERIAL_FIFO_CAPACITY) {
        return false;
    }
    uart->receive.values[uart->receive.write_index] = value;
    uart->receive.metadata[uart->receive.write_index] = status & 0x0fu;
    uart->receive.write_index =
        (uint16_t)((uart->receive.write_index + 1u) % K22_SERIAL_FIFO_CAPACITY);
    uart->receive.count++;
    (void)k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_UART1, true);
    (void)k22_serial_read(&device->serial, uart->base + 4u, 1, &(uint32_t){0});
    refresh_serial_signals(device);
    kinetis_k22_sync_clock_gates(device);
    return true;
}

bool kinetis_k22_uart1_transmit(KinetisK22* device, uint8_t* value) {
    uint16_t wide = 0;
    if (device == NULL || value == NULL) {
        return false;
    }
    K22SerialFifo* fifo = &device->serial.uart[1].transmit;
    if (fifo->count == 0) {
        if (!kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_UART1, &wide)) {
            return false;
        }
    } else {
        wide = fifo->values[fifo->read_index];
        fifo->read_index = (uint16_t)((fifo->read_index + 1u) % K22_SERIAL_FIFO_CAPACITY);
        fifo->count--;
    }
    *value = (uint8_t)wide;
    return true;
}

bool kinetis_k22_spi0_receive(KinetisK22* device, uint16_t value) {
    if (device == NULL) {
        return false;
    }
    K22SerialSpi* spi = &device->serial.spi[0];
    if (!spi->present || spi->receive.count == K22_SERIAL_FIFO_CAPACITY) {
        return false;
    }
    spi->receive.values[spi->receive.write_index] = value;
    spi->receive.metadata[spi->receive.write_index] = 0;
    spi->receive.write_index =
        (uint16_t)((spi->receive.write_index + 1u) % K22_SERIAL_FIFO_CAPACITY);
    spi->receive.count++;
    (void)k22_serial_read(&device->serial, spi->base, 4, &(uint32_t){0});
    refresh_serial_signals(device);
    return true;
}

bool kinetis_k22_spi0_transmit(KinetisK22* device, uint16_t* value) {
    if (device == NULL || value == NULL) {
        return false;
    }
    K22SerialFifo* fifo = &device->serial.spi[0].transmit;
    if (fifo->count == 0) {
        return kinetis_k22_serial_transmit(device, KINETIS_K22_SERIAL_SPI0, value);
    }
    *value = fifo->values[fifo->read_index];
    fifo->read_index = (uint16_t)((fifo->read_index + 1u) % K22_SERIAL_FIFO_CAPACITY);
    fifo->count--;
    return true;
}

bool kinetis_k22_i2c0_transfer(KinetisK22* device, KinetisK22I2cTransfer* transfer) {
    return kinetis_k22_i2c_transfer(device, KINETIS_K22_SERIAL_I2C0, transfer);
}

void kinetis_k22_i2c0_acknowledge(KinetisK22* device, bool acknowledge) {
    (void)kinetis_k22_i2c_acknowledge(device, KINETIS_K22_SERIAL_I2C0, acknowledge);
}

bool kinetis_k22_i2c0_lose_arbitration(KinetisK22* device) {
    return kinetis_k22_i2c_lose_arbitration(device, KINETIS_K22_SERIAL_I2C0);
}

bool kinetis_k22_i2c0_receive(KinetisK22* device, uint8_t value) {
    if (device == NULL || !device->serial.i2c[0].present) {
        return false;
    }
    device->serial.i2c[0].registers[4] = value;
    device->serial.i2c[0].registers[3] |= 0x82u;
    (void)k22_serial_set_clock_gate(&device->serial, K22_PERIPHERAL_I2C0, true);
    refresh_serial_signals(device);
    kinetis_k22_sync_clock_gates(device);
    return true;
}

K22DataBus kinetis_k22_data_bus(KinetisK22* device) {
    const K22DataBus bus = {device, data_bus_read, data_bus_write, flash_bus_write,
                            data_interrupt};
    return bus;
}

K22SdhcBus kinetis_k22_sdhc_bus(KinetisK22* device) {
    const K22SdhcBus bus = {device, data_bus_read, data_bus_write};
    return bus;
}

K22TimingSignals kinetis_k22_timing_signals(KinetisK22* device) {
    const K22TimingSignals signals = {device, timing_irq, timing_dma, timing_reset,
                                      timing_trigger};
    return signals;
}

K22IoConfiguration kinetis_k22_io_configuration(KinetisK22* device) {
    K22IoConfiguration configuration = k22_io_default_configuration(device->profile);
    for (uint8_t port = 0; port < K22_PACKAGE_PORT_COUNT; port++) {
        configuration.package_pin_mask[port] =
            k22_package_port_pin_mask(device->package, port);
    }
    configuration.event_handler = io_event;
    configuration.event_context = device;
    return configuration;
}
