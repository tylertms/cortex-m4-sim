#include "kinetis_k22_internal.h"

#include <string.h>

enum {
    K22_SMC_PMCTRL = 0x4007e001u,
    K22_SMC_PMSTAT = 0x4007e003u,
    K22_MCG_C1 = 0x40064000u,
    K22_MCG_C6 = 0x40064005u,
    K22_MCG_S = 0x40064006u,
    K22_LPTMR0_CSR = 0x40040000u,
    K22_WDOG_BASE = 0x40052000u,
    K22_WDOG_STCTRLH = K22_WDOG_BASE,
    K22_WDOG_TOVALH = K22_WDOG_BASE + 4,
    K22_WDOG_TOVALL = K22_WDOG_BASE + 6,
    K22_WDOG_REFRESH = K22_WDOG_BASE + 0x0c,
    K22_WDOG_UNLOCK = K22_WDOG_BASE + 0x0e,
    K22_ADC0_SC1A = 0x4003b000u,
    K22_ADC0_RA = 0x4003b010u,
    K22_ADC0_SC3 = 0x4003b024u,
    K22_I2C0_BASE = 0x40066000u,
    K22_I2C0_C1 = K22_I2C0_BASE + 2,
    K22_I2C0_S = K22_I2C0_BASE + 3,
    K22_I2C0_D = K22_I2C0_BASE + 4,
    K22_I2C0_FLT = K22_I2C0_BASE + 6,
    K22_DMA_CERQ = 0x4000801au,
    K22_DMA_SERQ = 0x4000801bu,
    K22_DMA_CDNE = 0x4000801cu,
    K22_DMA_CINT = 0x4000801fu,
    K22_DMA_TCD_BASE = 0x40009000u,
    K22_DMAMUX_BASE = 0x40021000u,
    K22_PIT_BASE = 0x40037000u,
    K22_PIT_CHANNEL_BASE = 0x40037100u,
    K22_UART1_BASE = 0x4006b000u,
    K22_UART1_C2 = K22_UART1_BASE + 3,
    K22_UART1_S1 = K22_UART1_BASE + 4,
    K22_UART1_S2 = K22_UART1_BASE + 5,
    K22_UART1_C3 = K22_UART1_BASE + 6,
    K22_UART1_D = K22_UART1_BASE + 7,
    K22_UART1_C5 = K22_UART1_BASE + 11,
    K22_SPI0_BASE = 0x4002c000u,
    K22_SPI0_MCR = K22_SPI0_BASE,
    K22_SPI0_SR = K22_SPI0_BASE + 0x2c,
    K22_SPI0_RSER = K22_SPI0_BASE + 0x30,
    K22_SPI0_PUSHR = K22_SPI0_BASE + 0x34,
    K22_SPI0_POPR = K22_SPI0_BASE + 0x38,
    K22_GPIO_BASE = 0x400ff000u,
    K22_GPIO_STRIDE = 0x40u,
    K22_PORT_BASE = 0x40049000u,
    K22_PORT_STRIDE = 0x1000u,
    K22_UART1_IRQ = 33,
    K22_UART1_ERROR_IRQ = 34,
    K22_ADC0_IRQ = 39,
    K22_PIT0_IRQ = 48,
    K22_PORTA_IRQ = 59,
    K22_I2C0_IRQ = 24,
    K22_WDOG_CLOCK_DIVIDER = 120000,
};

static uint32_t raw_load(const KinetisK22* device, uint32_t address, uint8_t size) {
    const uint8_t* data = device->peripheral + address - K22_PERIPHERAL_BASE;
    uint32_t value = 0;
    for (uint8_t index = 0; index < size; index++) {
        value |= (uint32_t)data[index] << (index * 8u);
    }
    return value;
}

static void raw_store(KinetisK22* device, uint32_t address, uint8_t size, uint32_t value) {
    uint8_t* data = device->peripheral + address - K22_PERIPHERAL_BASE;
    for (uint8_t index = 0; index < size; index++) {
        data[index] = (uint8_t)(value >> (index * 8u));
    }
}

static bool fifo_push(KinetisK22Fifo* fifo, uint16_t value) {
    if (fifo->count == K22_FIFO_CAPACITY) {
        return false;
    }
    fifo->values[fifo->write_index] = value;
    fifo->write_index = (uint16_t)((fifo->write_index + 1u) % K22_FIFO_CAPACITY);
    fifo->count++;
    return true;
}

static bool fifo_pop(KinetisK22Fifo* fifo, uint16_t* value) {
    if (fifo->count == 0) {
        return false;
    }
    *value = fifo->values[fifo->read_index];
    fifo->read_index = (uint16_t)((fifo->read_index + 1u) % K22_FIFO_CAPACITY);
    fifo->count--;
    return true;
}

static bool i2c_transfer_push(KinetisK22* device, KinetisK22I2cTransferType type,
                              uint8_t value) {
    return fifo_push(&device->i2c0_transfer, (uint16_t)((uint16_t)type << 8) | value);
}

static uint32_t gpio_input(const KinetisK22* device, uint8_t port) {
    const uint32_t base = K22_GPIO_BASE + (uint32_t)port * K22_GPIO_STRIDE;
    const uint32_t output = raw_load(device, base, 4);
    const uint32_t direction = raw_load(device, base + 0x14, 4);
    uint32_t input = output & direction;
    input |= device->gpio_external[port] & device->gpio_driven[port] & ~direction;
    for (uint8_t pin = 0; pin < 32; pin++) {
        const uint32_t mask = 1u << pin;
        if ((direction & mask) != 0 || (device->gpio_driven[port] & mask) != 0) {
            continue;
        }
        const uint32_t control = raw_load(
            device, K22_PORT_BASE + (uint32_t)port * K22_PORT_STRIDE + (uint32_t)pin * 4u,
            4);
        if ((control & 3u) == 3u) {
            input |= mask;
        }
    }
    return input;
}

static void update_uart_status(KinetisK22* device) {
    uint8_t status = (uint8_t)raw_load(device, K22_UART1_S1, 1);
    status |= 0xc0u;
    if (device->uart1_receive.count != 0) {
        status |= 0x20u;
    } else {
        status &= (uint8_t)~0x20u;
    }
    raw_store(device, K22_UART1_S1, 1, status);
}

static void update_spi_status(KinetisK22* device) {
    uint32_t status = raw_load(device, K22_SPI0_SR, 4);
    status &= ~(0x0fu << 4);
    const uint32_t receive_count =
        device->spi0_receive.count > 15 ? 15 : device->spi0_receive.count;
    status |= receive_count << 4;
    if (receive_count != 0) {
        status |= 1u << 17;
    } else {
        status &= ~(1u << 17);
    }
    status |= (1u << 25) | (1u << 31);
    raw_store(device, K22_SPI0_SR, 4, status);
}

static int16_t raw_i16(const KinetisK22* device, uint32_t address) {
    return (int16_t)raw_load(device, address, 2);
}

static int32_t raw_i32(const KinetisK22* device, uint32_t address) {
    return (int32_t)raw_load(device, address, 4);
}

static uint32_t watchdog_timeout(const KinetisK22* device) {
    return (raw_load(device, K22_WDOG_TOVALH, 2) << 16) |
           raw_load(device, K22_WDOG_TOVALL, 2);
}

void kinetis_k22_watchdog_advance(KinetisK22* device, uint32_t ticks) {
    if (device == NULL || ticks == 0 || (raw_load(device, K22_WDOG_STCTRLH, 2) & 1u) == 0) {
        return;
    }
    const uint32_t timeout = watchdog_timeout(device);
    if (timeout == 0 || ticks >= timeout - device->watchdog_ticks) {
        kinetis_k22_warm_reset(device, 0x20u);
        return;
    }
    device->watchdog_ticks += ticks;
}

static void dma_request(KinetisK22* device, uint8_t source) {
    for (uint8_t channel = 0; channel < 16; channel++) {
        const uint8_t mux = (uint8_t)raw_load(device, K22_DMAMUX_BASE + channel, 1);
        if ((device->dma_enabled & (1u << channel)) == 0 ||
            (device->dma_active & (1u << channel)) != 0 || (mux & 0xc0u) != 0x80u ||
            (mux & 0x3fu) != source) {
            continue;
        }
        const uint32_t descriptor = K22_DMA_TCD_BASE + (uint32_t)channel * 32u;
        uint32_t source_address = raw_load(device, descriptor, 4);
        uint32_t destination_address = raw_load(device, descriptor + 0x10, 4);
        const int16_t source_offset = raw_i16(device, descriptor + 4);
        const int16_t destination_offset = raw_i16(device, descriptor + 0x14);
        const uint32_t minor_count = raw_load(device, descriptor + 8, 4);
        uint16_t iteration = (uint16_t)raw_load(device, descriptor + 0x16, 2);
        if (iteration == 0 || minor_count == 0) {
            continue;
        }
        bool transferred = true;
        device->dma_active |= (uint16_t)(1u << channel);
        for (uint32_t index = 0; index < minor_count; index++) {
            uint32_t value = 0;
            if (!kinetis_k22_memory_read(device, source_address + index, 1, &value) ||
                !kinetis_k22_memory_write(device, destination_address + index, 1,
                                          CORTEX_M4_ACCESS_DATA, value)) {
                transferred = false;
                break;
            }
        }
        device->dma_active &= (uint16_t)~(1u << channel);
        if (!transferred) {
            continue;
        }
        source_address = (uint32_t)((int64_t)source_address + source_offset);
        destination_address = (uint32_t)((int64_t)destination_address + destination_offset);
        iteration--;
        raw_store(device, descriptor, 4, source_address);
        raw_store(device, descriptor + 0x10, 4, destination_address);
        raw_store(device, descriptor + 0x16, 2, iteration);
        if (iteration != 0) {
            continue;
        }
        raw_store(device, descriptor, 4,
                  source_address + (uint32_t)raw_i32(device, descriptor + 0x0c));
        raw_store(device, descriptor + 0x10, 4,
                  destination_address + (uint32_t)raw_i32(device, descriptor + 0x18));
        uint16_t control = (uint16_t)raw_load(device, descriptor + 0x1c, 2);
        control |= 1u << 7;
        raw_store(device, descriptor + 0x1c, 2, control);
        if ((control & (1u << 3)) != 0) {
            device->dma_enabled &= (uint16_t)~(1u << channel);
        }
        if ((control & (1u << 1)) != 0) {
            device->dma_interrupts |= (uint16_t)(1u << channel);
            cortex_m4_set_irq(device->cpu, channel, true);
        }
    }
}

bool kinetis_k22_peripheral_read(KinetisK22* device, uint32_t address, uint8_t size,
                                 uint32_t* value) {
    if (address >= K22_GPIO_BASE && address < K22_GPIO_BASE + 5u * K22_GPIO_STRIDE &&
        ((address - K22_GPIO_BASE) % K22_GPIO_STRIDE) == 0x10u && size == 4) {
        const uint8_t port = (uint8_t)((address - K22_GPIO_BASE) / K22_GPIO_STRIDE);
        *value = gpio_input(device, port);
        return true;
    }
    if (address >= K22_PIT_CHANNEL_BASE && address < K22_PIT_CHANNEL_BASE + 4u * 0x10u &&
        ((address - K22_PIT_CHANNEL_BASE) & 15u) == 4u && size == 4) {
        const uint8_t channel = (uint8_t)((address - K22_PIT_CHANNEL_BASE) / 0x10u);
        *value = device->pit_current[channel];
        return true;
    }
    if (address == K22_UART1_D && size == 1) {
        uint16_t data = 0;
        if (!fifo_pop(&device->uart1_receive, &data)) {
            data = raw_load(device, address, 1);
        }
        update_uart_status(device);
        *value = data;
        return true;
    }
    if (address == K22_SPI0_POPR && size == 4) {
        uint16_t data = 0;
        if (!fifo_pop(&device->spi0_receive, &data)) {
            data = (uint16_t)raw_load(device, address, 4);
        }
        update_spi_status(device);
        *value = data;
        return true;
    }
    if (address == K22_I2C0_D && size == 1) {
        uint16_t data = 0;
        if (!fifo_pop(&device->i2c0_receive, &data)) {
            data = (uint16_t)raw_load(device, address, 1);
        }
        const uint8_t control = (uint8_t)raw_load(device, K22_I2C0_C1, 1);
        if ((control & 0x10u) == 0) {
            i2c_transfer_push(device, KINETIS_K22_I2C_READ, 0);
        }
        *value = data;
        return true;
    }
    *value = raw_load(device, address, size);
    return true;
}

bool kinetis_k22_peripheral_write(KinetisK22* device, uint32_t address, uint8_t size,
                                  uint32_t value) {
    if (address >= K22_GPIO_BASE && address < K22_GPIO_BASE + 5u * K22_GPIO_STRIDE &&
        size == 4) {
        const uint32_t offset = (address - K22_GPIO_BASE) % K22_GPIO_STRIDE;
        const uint32_t base = address - offset;
        const uint32_t output = raw_load(device, base, 4);
        if (offset == 4) {
            raw_store(device, base, 4, output | value);
            return true;
        }
        if (offset == 8) {
            raw_store(device, base, 4, output & ~value);
            return true;
        }
        if (offset == 12) {
            raw_store(device, base, 4, output ^ value);
            return true;
        }
    }
    if (address >= K22_PORT_BASE && address < K22_PORT_BASE + 5u * K22_PORT_STRIDE &&
        ((address - K22_PORT_BASE) % K22_PORT_STRIDE) < 0x80u && size == 4) {
        const uint32_t previous = raw_load(device, address, 4);
        raw_store(device, address, 4,
                  (value & ~(1u << 24)) |
                      (((previous & (1u << 24)) != 0 && (value & (1u << 24)) == 0)
                           ? 1u << 24
                           : 0));
        return true;
    }
    if (address == K22_WDOG_UNLOCK && size == 2) {
        if (value == 0xc520u) {
            device->watchdog_unlock_stage = 1;
        } else if (value == 0xd928u && device->watchdog_unlock_stage == 1) {
            device->watchdog_unlock_stage = 2;
        } else {
            device->watchdog_unlock_stage = 0;
        }
        return true;
    }
    if (address == K22_WDOG_REFRESH && size == 2) {
        if (value == 0xa602u) {
            device->watchdog_refresh_stage = 1;
        } else if (value == 0xb480u && device->watchdog_refresh_stage == 1) {
            device->watchdog_ticks = 0;
            device->watchdog_refresh_stage = 0;
        } else {
            device->watchdog_refresh_stage = 0;
        }
        return true;
    }
    if (address >= K22_WDOG_BASE && address < K22_WDOG_BASE + 0x18u && size == 2) {
        if (device->watchdog_unlock_stage == 2) {
            raw_store(device, address, size, value);
        }
        return true;
    }
    if (address == K22_SMC_PMCTRL && size == 1) {
        raw_store(device, address, size, value);
        raw_store(device, K22_SMC_PMSTAT, 1, (value & 0x60u) == 0x60u ? 0x80u : 1u);
        return true;
    }
    if ((address == K22_MCG_C1 || address == K22_MCG_C6) && size == 1) {
        raw_store(device, address, size, value);
        raw_store(device, K22_MCG_S, 1, raw_load(device, K22_MCG_S, 1) & (uint8_t)~0x1cu);
        return true;
    }
    if (address == K22_LPTMR0_CSR && size == 4) {
        raw_store(device, address, size, (value & 1u) != 0 ? value | 0x80u : value);
        return true;
    }
    if (address == K22_ADC0_SC3 && size == 4 && (value & 0x80u) != 0) {
        raw_store(device, address, size, value & ~0x80u);
        raw_store(device, K22_ADC0_SC1A, 4, raw_load(device, K22_ADC0_SC1A, 4) | 0x80u);
        return true;
    }
    if (address == K22_I2C0_C1 && size == 1) {
        const uint8_t previous = (uint8_t)raw_load(device, address, 1);
        uint8_t control = (uint8_t)value;
        if ((control & 0x04u) != 0) {
            i2c_transfer_push(device, KINETIS_K22_I2C_REPEATED_START, 0);
            control &= (uint8_t)~0x04u;
        }
        if ((previous & 0x20u) == 0 && (control & 0x20u) != 0) {
            i2c_transfer_push(device, KINETIS_K22_I2C_START, 0);
            raw_store(device, K22_I2C0_S, 1, raw_load(device, K22_I2C0_S, 1) | 0x20u);
        } else if ((previous & 0x20u) != 0 && (control & 0x20u) == 0) {
            i2c_transfer_push(device, KINETIS_K22_I2C_STOP, 0);
            raw_store(device, K22_I2C0_S, 1,
                      raw_load(device, K22_I2C0_S, 1) & (uint8_t)~0x20u);
        }
        raw_store(device, address, 1, control);
        return true;
    }
    if (address == K22_I2C0_S && size == 1) {
        raw_store(device, address, 1, raw_load(device, address, 1) & ~(value & 0x12u));
        if ((value & 0x02u) != 0) {
            cortex_m4_set_irq(device->cpu, K22_I2C0_IRQ, false);
        }
        return true;
    }
    if (address == K22_I2C0_FLT && size == 1) {
        raw_store(device, address, 1,
                  (value & 0x2fu) |
                      (raw_load(device, address, 1) & ~(value & 0x50u) & 0x50u));
        return true;
    }
    if (address == K22_I2C0_D && size == 1) {
        raw_store(device, address, 1, value);
        i2c_transfer_push(device, KINETIS_K22_I2C_WRITE, (uint8_t)value);
        return true;
    }
    if ((address == K22_ADC0_SC1A || address == K22_ADC0_SC1A + 4) && size == 4) {
        const uint8_t channel = (uint8_t)(value & 31u);
        raw_store(device, address, size, value & ~0x80u);
        if (channel != 31) {
            raw_store(device, K22_ADC0_RA + (address - K22_ADC0_SC1A), 4,
                      device->adc_channels[channel]);
            raw_store(device, address, 4, value | 0x80u);
            if ((value & 0x40u) != 0) {
                cortex_m4_set_irq(device->cpu, K22_ADC0_IRQ, true);
            }
        }
        return true;
    }
    if (address == K22_DMA_CERQ && size == 1) {
        const uint8_t channel = (uint8_t)value;
        if ((channel & 0x40u) != 0) {
            device->dma_enabled = 0;
        } else if ((channel & 0x0fu) < 16) {
            device->dma_enabled &= (uint16_t)~(1u << (channel & 15u));
        }
        return true;
    }
    if (address == K22_DMA_SERQ && size == 1) {
        const uint8_t channel = (uint8_t)value;
        if ((channel & 0x40u) != 0) {
            device->dma_enabled = 0xffffu;
        } else {
            device->dma_enabled |= (uint16_t)(1u << (channel & 15u));
        }
        return true;
    }
    if (address == K22_DMA_CINT && size == 1) {
        const uint8_t channel = (uint8_t)value & 15u;
        device->dma_interrupts &= (uint16_t)~(1u << channel);
        cortex_m4_set_irq(device->cpu, channel, false);
        return true;
    }
    if (address == K22_DMA_CDNE && size == 1) {
        const uint8_t channel = (uint8_t)value & 15u;
        const uint32_t descriptor = K22_DMA_TCD_BASE + (uint32_t)channel * 32u;
        raw_store(device, descriptor + 0x1c, 2,
                  raw_load(device, descriptor + 0x1c, 2) & ~(1u << 7));
        return true;
    }
    if (address >= K22_PIT_CHANNEL_BASE && address < K22_PIT_CHANNEL_BASE + 4u * 0x10u &&
        size == 4) {
        const uint8_t channel = (uint8_t)((address - K22_PIT_CHANNEL_BASE) / 0x10u);
        const uint32_t offset = (address - K22_PIT_CHANNEL_BASE) & 15u;
        if (offset == 0) {
            raw_store(device, address, size, value);
            device->pit_current[channel] = value;
            return true;
        }
        if (offset == 12) {
            raw_store(device, address, size, raw_load(device, address, 4) & ~(value & 1u));
            if ((value & 1u) != 0) {
                cortex_m4_set_irq(device->cpu, K22_PIT0_IRQ + channel, false);
            }
            return true;
        }
    }
    if (address == K22_UART1_D && size == 1) {
        fifo_push(&device->uart1_transmit, (uint8_t)value);
        raw_store(device, address, size, value);
        update_uart_status(device);
        dma_request(device, 5);
        return true;
    }
    if (address == K22_UART1_S1 && size == 1) {
        raw_store(device, address, size, raw_load(device, address, 1) & ~(value & 0x1fu));
        return true;
    }
    if (address == K22_SPI0_PUSHR && size == 4) {
        fifo_push(&device->spi0_transmit, (uint16_t)value);
        raw_store(device, address, size, value);
        update_spi_status(device);
        dma_request(device, 15);
        return true;
    }
    if (address == K22_SPI0_SR && size == 4) {
        raw_store(device, address, size, raw_load(device, address, 4) & ~value);
        update_spi_status(device);
        return true;
    }
    raw_store(device, address, size, value);
    return true;
}

void kinetis_k22_peripheral_advance(KinetisK22* device, uint32_t cycles) {
    const uint64_t watchdog_cycles = (uint64_t)device->watchdog_cycle_remainder + cycles;
    device->watchdog_cycle_remainder = (uint32_t)(watchdog_cycles % K22_WDOG_CLOCK_DIVIDER);
    kinetis_k22_watchdog_advance(device,
                                 (uint32_t)(watchdog_cycles / K22_WDOG_CLOCK_DIVIDER));
    const uint32_t total = device->pit_cycle_remainder + cycles;
    const uint32_t pit_ticks = total / 4u;
    device->pit_cycle_remainder = (uint8_t)(total % 4u);
    for (uint8_t channel = 0; channel < 4; channel++) {
        const uint32_t base = K22_PIT_CHANNEL_BASE + (uint32_t)channel * 0x10u;
        const uint32_t control = raw_load(device, base + 8, 4);
        if ((control & 1u) == 0 || pit_ticks == 0) {
            continue;
        }
        uint32_t remaining = pit_ticks;
        while (remaining != 0) {
            if (device->pit_current[channel] >= remaining) {
                device->pit_current[channel] -= remaining;
                remaining = 0;
            } else {
                remaining -= device->pit_current[channel] + 1u;
                device->pit_current[channel] = raw_load(device, base, 4);
                raw_store(device, base + 12, 4, 1);
                if ((control & 2u) != 0) {
                    cortex_m4_set_irq(device->cpu, K22_PIT0_IRQ + channel, true);
                }
            }
        }
    }
    if ((raw_load(device, K22_UART1_C2, 1) & 0xa0u) != 0) {
        update_uart_status(device);
        const uint8_t status = (uint8_t)raw_load(device, K22_UART1_S1, 1);
        if (((status & 0x20u) != 0 && (raw_load(device, K22_UART1_C2, 1) & 0x20u) != 0) ||
            ((status & 0x80u) != 0 && (raw_load(device, K22_UART1_C2, 1) & 0x80u) != 0)) {
            cortex_m4_set_irq(device->cpu, K22_UART1_IRQ, true);
        }
    }
}

void kinetis_k22_peripheral_reset(KinetisK22* device) {
    memset(device->pit_current, 0, sizeof(device->pit_current));
    memset(&device->uart1_receive, 0, sizeof(device->uart1_receive));
    memset(&device->uart1_transmit, 0, sizeof(device->uart1_transmit));
    memset(&device->spi0_receive, 0, sizeof(device->spi0_receive));
    memset(&device->spi0_transmit, 0, sizeof(device->spi0_transmit));
    memset(&device->i2c0_receive, 0, sizeof(device->i2c0_receive));
    memset(&device->i2c0_transfer, 0, sizeof(device->i2c0_transfer));
    device->pit_cycle_remainder = 0;
    device->dma_enabled = 0;
    device->dma_interrupts = 0;
    device->dma_active = 0;
    device->watchdog_unlock_stage = 0;
    device->watchdog_refresh_stage = 0;
    device->watchdog_ticks = 0;
    device->watchdog_cycle_remainder = 0;
    raw_store(device, K22_WDOG_STCTRLH, 2, 0x01d3u);
    raw_store(device, K22_WDOG_TOVALH, 2, 0x004cu);
    raw_store(device, K22_WDOG_TOVALL, 2, 0x4b4cu);
    raw_store(device, K22_SMC_PMSTAT, 1, 1);
    raw_store(device, K22_UART1_S1, 1, 0xc0u);
    raw_store(device, K22_SPI0_MCR, 4, 1u);
    update_spi_status(device);
}

void kinetis_k22_set_adc0_channel(KinetisK22* device, uint8_t channel, uint16_t value) {
    if (device != NULL && channel < 32) {
        device->adc_channels[channel] = value;
    }
}

static void update_port_interrupt(KinetisK22* device, uint8_t port, uint8_t pin,
                                  bool previous, bool current) {
    const uint32_t address =
        K22_PORT_BASE + (uint32_t)port * K22_PORT_STRIDE + (uint32_t)pin * 4u;
    const uint32_t control = raw_load(device, address, 4);
    const uint8_t interrupt = (uint8_t)((control >> 16) & 15u);
    const bool trigger =
        (interrupt == 8 && !current) || (interrupt == 9 && !previous && current) ||
        (interrupt == 10 && previous && !current) ||
        (interrupt == 11 && previous != current) || (interrupt == 12 && current);
    if (trigger) {
        raw_store(device, address, 4, control | (1u << 24));
        cortex_m4_set_irq(device->cpu, K22_PORTA_IRQ + port, true);
    }
}

void kinetis_k22_gpio_drive(KinetisK22* device, uint8_t port, uint8_t pin, bool high) {
    if (device == NULL || port >= 5 || pin >= 32) {
        return;
    }
    const uint32_t mask = 1u << pin;
    const bool previous = (gpio_input(device, port) & mask) != 0;
    device->gpio_driven[port] |= mask;
    if (high) {
        device->gpio_external[port] |= mask;
    } else {
        device->gpio_external[port] &= ~mask;
    }
    update_port_interrupt(device, port, pin, previous, high);
}

void kinetis_k22_gpio_release(KinetisK22* device, uint8_t port, uint8_t pin) {
    if (device == NULL || port >= 5 || pin >= 32) {
        return;
    }
    const uint32_t mask = 1u << pin;
    const bool previous = (gpio_input(device, port) & mask) != 0;
    device->gpio_driven[port] &= ~mask;
    const bool current = (gpio_input(device, port) & mask) != 0;
    update_port_interrupt(device, port, pin, previous, current);
}

bool kinetis_k22_uart1_receive(KinetisK22* device, uint8_t value, uint8_t status) {
    if (device == NULL || !fifo_push(&device->uart1_receive, value)) {
        return false;
    }
    raw_store(device, K22_UART1_S1, 1,
              raw_load(device, K22_UART1_S1, 1) | 0x20u | (status & 0x0fu));
    update_uart_status(device);
    if ((raw_load(device, K22_UART1_C5, 1) & 0x20u) != 0) {
        dma_request(device, 4);
    }
    if ((raw_load(device, K22_UART1_C2, 1) & 0x20u) != 0) {
        cortex_m4_set_irq(device->cpu, K22_UART1_IRQ, true);
    }
    if ((status & raw_load(device, K22_UART1_C3, 1) & 0x0fu) != 0) {
        cortex_m4_set_irq(device->cpu, K22_UART1_ERROR_IRQ, true);
    }
    return true;
}

bool kinetis_k22_uart1_transmit(KinetisK22* device, uint8_t* value) {
    uint16_t data = 0;
    if (device == NULL || value == NULL || !fifo_pop(&device->uart1_transmit, &data)) {
        return false;
    }
    *value = (uint8_t)data;
    return true;
}

bool kinetis_k22_spi0_receive(KinetisK22* device, uint16_t value) {
    if (device == NULL || !fifo_push(&device->spi0_receive, value)) {
        return false;
    }
    update_spi_status(device);
    if ((raw_load(device, K22_SPI0_RSER, 4) & (1u << 16)) != 0) {
        dma_request(device, 14);
    } else if ((raw_load(device, K22_SPI0_RSER, 4) & (1u << 17)) != 0) {
        cortex_m4_set_irq(device->cpu, 26, true);
    }
    return true;
}

bool kinetis_k22_spi0_transmit(KinetisK22* device, uint16_t* value) {
    return device != NULL && value != NULL && fifo_pop(&device->spi0_transmit, value);
}

bool kinetis_k22_i2c0_transfer(KinetisK22* device, KinetisK22I2cTransfer* transfer) {
    uint16_t encoded = 0;
    if (device == NULL || transfer == NULL || !fifo_pop(&device->i2c0_transfer, &encoded)) {
        return false;
    }
    transfer->type = (KinetisK22I2cTransferType)(encoded >> 8);
    transfer->value = (uint8_t)encoded;
    return true;
}

static void raise_i2c_interrupt(KinetisK22* device) {
    raw_store(device, K22_I2C0_S, 1, raw_load(device, K22_I2C0_S, 1) | 0x82u);
    if ((raw_load(device, K22_I2C0_C1, 1) & 0x40u) != 0) {
        cortex_m4_set_irq(device->cpu, K22_I2C0_IRQ, true);
    }
}

void kinetis_k22_i2c0_acknowledge(KinetisK22* device, bool acknowledge) {
    if (device == NULL) {
        return;
    }
    uint8_t status = (uint8_t)raw_load(device, K22_I2C0_S, 1);
    if (acknowledge) {
        status &= (uint8_t)~1u;
    } else {
        status |= 1u;
    }
    raw_store(device, K22_I2C0_S, 1, status);
    raise_i2c_interrupt(device);
}

bool kinetis_k22_i2c0_receive(KinetisK22* device, uint8_t value) {
    if (device == NULL || !fifo_push(&device->i2c0_receive, value)) {
        return false;
    }
    raw_store(device, K22_I2C0_D, 1, value);
    raise_i2c_interrupt(device);
    return true;
}
