#include "device/kinetis_k22/communication/sdhc.h"

#include <stdlib.h>
#include <string.h>

enum {
    SDHC_BASE = 0x400b1000u,
    SDHC_DSADDR = 0x00u,
    SDHC_BLKATTR = 0x04u,
    SDHC_CMDARG = 0x08u,
    SDHC_XFERTYP = 0x0cu,
    SDHC_CMDRSP0 = 0x10u,
    SDHC_DATPORT = 0x20u,
    SDHC_PRSSTAT = 0x24u,
    SDHC_PROCTL = 0x28u,
    SDHC_SYSCTL = 0x2cu,
    SDHC_IRQSTAT = 0x30u,
    SDHC_IRQSTATEN = 0x34u,
    SDHC_IRQSIGEN = 0x38u,
    SDHC_AC12ERR = 0x3cu,
    SDHC_HTCAPBLT = 0x40u,
    SDHC_WML = 0x44u,
    SDHC_FEVT = 0x50u,
    SDHC_ADMAES = 0x54u,
    SDHC_ADSADDR = 0x58u,
    SDHC_VENDOR = 0xc0u,
    SDHC_MMCBOOT = 0xc4u,
    SDHC_HOSTVER = 0xfcu,
    SDHC_IRQ_COMMAND_COMPLETE = 1u << 0,
    SDHC_IRQ_TRANSFER_COMPLETE = 1u << 1,
    SDHC_IRQ_DMA = 1u << 3,
    SDHC_IRQ_BUFFER_WRITE_READY = 1u << 4,
    SDHC_IRQ_BUFFER_READ_READY = 1u << 5,
    SDHC_IRQ_CARD_INSERTION = 1u << 6,
    SDHC_IRQ_CARD_REMOVAL = 1u << 7,
    SDHC_IRQ_COMMAND_TIMEOUT = 1u << 16,
    SDHC_IRQ_DATA_TIMEOUT = 1u << 20,
    SDHC_IRQ_DATA_CRC = 1u << 21,
};

static uint32_t* reg(K22Sdhc* sdhc, uint32_t offset) { return &sdhc->registers[offset / 4u]; }

static const uint32_t* const_reg(const K22Sdhc* sdhc, uint32_t offset) {
    return &sdhc->registers[offset / 4u];
}

static void set_status(K22Sdhc* sdhc, uint32_t status) {
    *reg(sdhc, SDHC_IRQSTAT) |= status & *reg(sdhc, SDHC_IRQSTATEN);
}

static void refresh_present_state(K22Sdhc* sdhc) {
    uint32_t value = 1u << 3;
    if (sdhc->present)
        value |= (1u << 16) | (1u << 23) | (0xffu << 24);
    if (sdhc->transfer_remaining != 0u) {
        value |= 1u << 2;
        value |= sdhc->transfer_read ? (1u << 9) | (1u << 11) : (1u << 8) | (1u << 10);
    }
    *reg(sdhc, SDHC_PRSSTAT) = value;
}

static void reset_registers(K22Sdhc* sdhc) {
    memset(sdhc->registers, 0, sizeof(sdhc->registers));
    *reg(sdhc, SDHC_PRSSTAT) = 1u << 3;
    *reg(sdhc, SDHC_HTCAPBLT) = 0x07f30000u;
    *reg(sdhc, SDHC_WML) = 0x00800080u;
    *reg(sdhc, SDHC_HOSTVER) = 0x00001201u;
}

bool k22_sdhc_init(K22Sdhc* sdhc, K22SdhcBus bus) {
    if (sdhc == NULL || bus.read == NULL || bus.write == NULL)
        return false;
    memset(sdhc, 0, sizeof(*sdhc));
    sdhc->bus = bus;
    sdhc->block_length = 512u;
    sdhc->relative_address = 1u << 16;
    reset_registers(sdhc);
    return true;
}

void k22_sdhc_destroy(K22Sdhc* sdhc) {
    if (sdhc == NULL)
        return;
    free(sdhc->card);
    sdhc->card = NULL;
    sdhc->card_size = 0u;
    sdhc->present = false;
}

void k22_sdhc_reset(K22Sdhc* sdhc) {
    if (sdhc == NULL)
        return;
    const bool present = sdhc->present;
    const bool write_protected = sdhc->write_protected;
    sdhc->transfer_address = 0u;
    sdhc->transfer_remaining = 0u;
    sdhc->block_length = 512u;
    sdhc->relative_address = 1u << 16;
    sdhc->clock_enabled = false;
    sdhc->application_command = false;
    sdhc->selected = false;
    sdhc->transfer_read = false;
    sdhc->transfer_multiple = false;
    sdhc->present = present;
    sdhc->write_protected = write_protected;
    reset_registers(sdhc);
    refresh_present_state(sdhc);
}

bool k22_sdhc_copy(K22Sdhc* destination, const K22Sdhc* source, K22SdhcBus bus) {
    if (destination == NULL || source == NULL || bus.read == NULL || bus.write == NULL)
        return false;
    uint8_t* card = NULL;
    if (source->card_size != 0u) {
        card = malloc(source->card_size);
        if (card == NULL)
            return false;
        memcpy(card, source->card, source->card_size);
    }
    free(destination->card);
    *destination = *source;
    destination->bus = bus;
    destination->card = card;
    return true;
}

void k22_sdhc_set_clock(K22Sdhc* sdhc, bool enabled) {
    if (sdhc != NULL)
        sdhc->clock_enabled = enabled;
}

bool k22_sdhc_insert(K22Sdhc* sdhc, const void* data, size_t size, bool write_protected) {
    if (sdhc == NULL || data == NULL || size == 0u || size % 512u != 0u)
        return false;
    uint8_t* card = malloc(size);
    if (card == NULL)
        return false;
    memcpy(card, data, size);
    free(sdhc->card);
    sdhc->card = card;
    sdhc->card_size = size;
    sdhc->present = true;
    sdhc->write_protected = write_protected;
    sdhc->selected = false;
    set_status(sdhc, SDHC_IRQ_CARD_INSERTION);
    refresh_present_state(sdhc);
    return true;
}

void k22_sdhc_eject(K22Sdhc* sdhc) {
    if (sdhc == NULL)
        return;
    free(sdhc->card);
    sdhc->card = NULL;
    sdhc->card_size = 0u;
    sdhc->present = false;
    sdhc->selected = false;
    sdhc->transfer_remaining = 0u;
    set_status(sdhc, SDHC_IRQ_CARD_REMOVAL);
    refresh_present_state(sdhc);
}

bool k22_sdhc_read_card(const K22Sdhc* sdhc, size_t offset, void* data, size_t size) {
    if (sdhc == NULL || data == NULL || !sdhc->present || offset > sdhc->card_size ||
        size > sdhc->card_size - offset)
        return false;
    memcpy(data, sdhc->card + offset, size);
    return true;
}

static uint32_t transfer_block_count(const K22Sdhc* sdhc) {
    const uint32_t count = *const_reg(sdhc, SDHC_BLKATTR) >> 16;
    return count == 0u ? 1u : count;
}

static uint32_t transfer_block_size(const K22Sdhc* sdhc) {
    const uint32_t size = *const_reg(sdhc, SDHC_BLKATTR) & 0x1fffu;
    return size == 0u ? sdhc->block_length : size;
}

static bool transfer_bounds(const K22Sdhc* sdhc, size_t address, size_t size) {
    return address <= sdhc->card_size && size <= sdhc->card_size - address;
}

static void complete_transfer(K22Sdhc* sdhc) {
    sdhc->transfer_remaining = 0u;
    set_status(sdhc, SDHC_IRQ_TRANSFER_COMPLETE);
    refresh_present_state(sdhc);
}

static bool dma_transfer(K22Sdhc* sdhc) {
    uint32_t address = *reg(sdhc, SDHC_DSADDR) & 0xfffffffcu;
    while (sdhc->transfer_remaining != 0u) {
        const uint8_t size = sdhc->transfer_remaining >= 4u ? 4u : 1u;
        uint32_t value = 0u;
        if (sdhc->transfer_read) {
            memcpy(&value, sdhc->card + sdhc->transfer_address, size);
            if (!sdhc->bus.write(sdhc->bus.context, address, size, value))
                return false;
        } else {
            if (!sdhc->bus.read(sdhc->bus.context, address, size, &value))
                return false;
            memcpy(sdhc->card + sdhc->transfer_address, &value, size);
        }
        address += size;
        sdhc->transfer_address += size;
        sdhc->transfer_remaining -= size;
    }
    *reg(sdhc, SDHC_DSADDR) = address;
    set_status(sdhc, SDHC_IRQ_DMA | SDHC_IRQ_TRANSFER_COMPLETE);
    refresh_present_state(sdhc);
    return true;
}

static bool begin_transfer(K22Sdhc* sdhc, bool read, bool multiple) {
    const size_t block_size = transfer_block_size(sdhc);
    const size_t block_count = multiple ? transfer_block_count(sdhc) : 1u;
    const size_t address = (size_t)*reg(sdhc, SDHC_CMDARG) * 512u;
    if (!sdhc->selected || block_size == 0u || block_size > 4096u ||
        block_count > SIZE_MAX / block_size ||
        !transfer_bounds(sdhc, address, block_size * block_count)) {
        set_status(sdhc, SDHC_IRQ_DATA_TIMEOUT);
        return false;
    }
    if (!read && sdhc->write_protected) {
        set_status(sdhc, SDHC_IRQ_DATA_CRC);
        return false;
    }
    sdhc->transfer_address = address;
    sdhc->transfer_remaining = block_size * block_count;
    sdhc->transfer_read = read;
    sdhc->transfer_multiple = multiple;
    refresh_present_state(sdhc);
    if ((*reg(sdhc, SDHC_XFERTYP) & 1u) != 0u) {
        if (!dma_transfer(sdhc)) {
            set_status(sdhc, SDHC_IRQ_DATA_TIMEOUT);
            return false;
        }
    } else {
        set_status(sdhc, read ? SDHC_IRQ_BUFFER_READ_READY : SDHC_IRQ_BUFFER_WRITE_READY);
    }
    return true;
}

static void issue_command(K22Sdhc* sdhc) {
    const uint8_t command = (uint8_t)(*reg(sdhc, SDHC_XFERTYP) >> 24u) & 0x3fu;
    const uint32_t argument = *reg(sdhc, SDHC_CMDARG);
    memset(reg(sdhc, SDHC_CMDRSP0), 0, 4u * sizeof(uint32_t));
    if (command != 0u && !sdhc->present) {
        set_status(sdhc, SDHC_IRQ_COMMAND_TIMEOUT);
        return;
    }
    if (sdhc->application_command && command == 41u) {
        *reg(sdhc, SDHC_CMDRSP0) = 0xc0ff8000u;
        sdhc->application_command = false;
    } else {
        sdhc->application_command = false;
        switch (command) {
        case 0u:
            sdhc->selected = false;
            break;
        case 2u:
            *reg(sdhc, SDHC_CMDRSP0) = 0x03534453u;
            *reg(sdhc, SDHC_CMDRSP0 + 4u) = 0x44303447u;
            *reg(sdhc, SDHC_CMDRSP0 + 8u) = 0x80123456u;
            *reg(sdhc, SDHC_CMDRSP0 + 12u) = 0x78010001u;
            break;
        case 3u:
            *reg(sdhc, SDHC_CMDRSP0) = sdhc->relative_address;
            break;
        case 7u:
            sdhc->selected = argument == sdhc->relative_address;
            break;
        case 8u:
            *reg(sdhc, SDHC_CMDRSP0) = argument;
            break;
        case 9u:
            *reg(sdhc, SDHC_CMDRSP0) = 0x400e0032u;
            *reg(sdhc, SDHC_CMDRSP0 + 4u) = (uint32_t)(sdhc->card_size / 512u);
            break;
        case 12u:
            complete_transfer(sdhc);
            break;
        case 13u:
            *reg(sdhc, SDHC_CMDRSP0) = sdhc->selected ? 4u << 9u : 3u << 9u;
            break;
        case 16u:
            if (argument == 0u || argument > 4096u)
                set_status(sdhc, SDHC_IRQ_COMMAND_TIMEOUT);
            else
                sdhc->block_length = argument;
            break;
        case 17u:
            (void)begin_transfer(sdhc, true, false);
            break;
        case 18u:
            (void)begin_transfer(sdhc, true, true);
            break;
        case 24u:
            (void)begin_transfer(sdhc, false, false);
            break;
        case 25u:
            (void)begin_transfer(sdhc, false, true);
            break;
        case 55u:
            sdhc->application_command = true;
            *reg(sdhc, SDHC_CMDRSP0) = 1u << 5u;
            break;
        default:
            set_status(sdhc, SDHC_IRQ_COMMAND_TIMEOUT);
            break;
        }
    }
    set_status(sdhc, SDHC_IRQ_COMMAND_COMPLETE);
}

static bool read_data(K22Sdhc* sdhc, uint32_t* value) {
    if (!sdhc->transfer_read || sdhc->transfer_remaining == 0u)
        return false;
    const size_t size = sdhc->transfer_remaining >= 4u ? 4u : sdhc->transfer_remaining;
    *value = 0u;
    memcpy(value, sdhc->card + sdhc->transfer_address, size);
    sdhc->transfer_address += size;
    sdhc->transfer_remaining -= size;
    if (sdhc->transfer_remaining == 0u)
        complete_transfer(sdhc);
    return true;
}

static bool write_data(K22Sdhc* sdhc, uint32_t value) {
    if (sdhc->transfer_read || sdhc->transfer_remaining == 0u)
        return false;
    const size_t size = sdhc->transfer_remaining >= 4u ? 4u : sdhc->transfer_remaining;
    memcpy(sdhc->card + sdhc->transfer_address, &value, size);
    sdhc->transfer_address += size;
    sdhc->transfer_remaining -= size;
    if (sdhc->transfer_remaining == 0u)
        complete_transfer(sdhc);
    return true;
}

static bool readable(uint32_t offset) {
    return offset <= SDHC_WML || offset == SDHC_ADMAES || offset == SDHC_ADSADDR ||
           offset == SDHC_VENDOR || offset == SDHC_MMCBOOT || offset == SDHC_HOSTVER;
}

static bool writable(uint32_t offset) {
    return offset == SDHC_DSADDR || offset == SDHC_BLKATTR || offset == SDHC_CMDARG ||
           offset == SDHC_XFERTYP || offset == SDHC_DATPORT || offset == SDHC_PROCTL ||
           offset == SDHC_SYSCTL || offset == SDHC_IRQSTAT || offset == SDHC_IRQSTATEN ||
           offset == SDHC_IRQSIGEN || offset == SDHC_FEVT || offset == SDHC_ADSADDR ||
           offset == SDHC_VENDOR || offset == SDHC_MMCBOOT;
}

bool k22_sdhc_read(K22Sdhc* sdhc, uint32_t address, uint8_t size, uint32_t* value) {
    if (sdhc == NULL || value == NULL || !sdhc->clock_enabled || size != 4u ||
        address < SDHC_BASE || address - SDHC_BASE > SDHC_HOSTVER ||
        ((address - SDHC_BASE) & 3u) != 0u)
        return false;
    const uint32_t offset = address - SDHC_BASE;
    if (offset == SDHC_DATPORT)
        return read_data(sdhc, value);
    if (!readable(offset))
        return false;
    refresh_present_state(sdhc);
    *value = *reg(sdhc, offset);
    return true;
}

bool k22_sdhc_write(K22Sdhc* sdhc, uint32_t address, uint8_t size, uint32_t value) {
    if (sdhc == NULL || !sdhc->clock_enabled || size != 4u || address < SDHC_BASE ||
        address - SDHC_BASE > SDHC_HOSTVER || ((address - SDHC_BASE) & 3u) != 0u)
        return false;
    const uint32_t offset = address - SDHC_BASE;
    if (!writable(offset))
        return false;
    if (offset == SDHC_DATPORT)
        return write_data(sdhc, value);
    if (offset == SDHC_IRQSTAT) {
        *reg(sdhc, offset) &= ~value;
        return true;
    }
    if (offset == SDHC_FEVT) {
        set_status(sdhc, value);
        return true;
    }
    if (offset == SDHC_SYSCTL && (value & 0x07000000u) != 0u) {
        const bool clock = sdhc->clock_enabled;
        k22_sdhc_reset(sdhc);
        sdhc->clock_enabled = clock;
        *reg(sdhc, offset) = value & 0x000fffffu;
        return true;
    }
    *reg(sdhc, offset) = value;
    if (offset == SDHC_XFERTYP)
        issue_command(sdhc);
    return true;
}

bool k22_sdhc_irq(const K22Sdhc* sdhc) {
    if (sdhc == NULL || !sdhc->clock_enabled)
        return false;
    return (*const_reg(sdhc, SDHC_IRQSTAT) & *const_reg(sdhc, SDHC_IRQSIGEN)) != 0u;
}
