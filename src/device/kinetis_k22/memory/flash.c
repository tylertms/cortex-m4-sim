#include "internal.h"

#include <string.h>

static uint8_t flash_fccob_offset(uint8_t index) {
    static const uint8_t offsets[12] = {7u, 6u, 5u, 4u, 11u, 10u, 9u, 8u, 15u, 14u, 13u, 12u};
    return offsets[index];
}

static uint8_t flash_fccob(const K22Data* data, uint8_t index) {
    return data->flash[flash_fccob_offset(index)];
}

static void flash_set_fccob(K22Data* data, uint8_t index, uint8_t value) {
    data->flash[flash_fccob_offset(index)] = value;
}

static uint32_t flash_address(const K22Data* data) {
    return ((uint32_t)flash_fccob(data, 1u) << 16u) | ((uint32_t)flash_fccob(data, 2u) << 8u) |
           flash_fccob(data, 3u);
}

uint32_t k22_data_program_flash_address(const K22Data* data, uint32_t address) {
    if (data == NULL || data->flash_swap_current_block == 0u ||
        address >= data->profile->program_flash_size)
        return address;
    return address ^ (data->profile->program_flash_size / 2u);
}

static bool flash_store(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    bool stored = false;
    const uint32_t physical_address = k22_data_program_flash_address(data, address);
    if (data->bus.program != NULL)
        stored = data->bus.program(data->bus.context, physical_address, size, value);
    else if (data->bus.write != NULL)
        stored = data->bus.write(data->bus.context, physical_address, size, value);
    if (!stored)
        return false;
    if (address < 0x410u && address + size > 0x400u) {
        for (uint8_t index = 0; index < size; index++) {
            const uint32_t byte_address = address + index;
            if (byte_address >= 0x400u && byte_address < 0x410u)
                data->flash_config[byte_address - 0x400u] = (uint8_t)(value >> (index * 8u));
        }
    }
    return true;
}

static bool flash_load(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    if (address >= 0x400u && address <= 0x410u - size) {
        *value = k22_data_internal_load_bytes(data->flash_config, address - 0x400u, size);
        return true;
    }
    if (data->bus.read == NULL)
        return false;
    return data->bus.read(data->bus.context, address, size, value);
}

static uint32_t flash_data_size(const K22Data* data) {
    if (!data->flash_partitioned)
        return data->profile->flexnvm_size;
    switch (data->flash_data_ifr[0x3fcu]) {
    case 0x00u:
    case 0x0du:
    case 0x0fu:
        return 0x20000u;
    case 0x03u:
        return 0x18000u;
    case 0x04u:
    case 0x0cu:
        return 0x10000u;
    case 0x0bu:
        return 0x8000u;
    case 0x05u:
    case 0x08u:
        return 0u;
    default:
        return 0u;
    }
}

static bool flash_memory_range(const K22Data* data, uint32_t address, uint32_t length,
                               bool* data_flash, uint32_t* offset) {
    *data_flash = (address & 0x800000u) != 0u;
    *offset = address & 0x7fffffu;
    const uint32_t available =
        *data_flash ? flash_data_size(data) : data->profile->program_flash_size;
    return length <= available && *offset <= available - length;
}

static bool flash_memory_load(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    const bool data_flash = (address & 0x800000u) != 0u;
    const uint32_t offset = address & 0x7fffffu;
    if (!data_flash)
        return flash_load(data, offset, size, value);
    *value = k22_data_internal_load_bytes(data->flexnvm, offset, size);
    return true;
}

static bool flash_memory_store(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    const bool data_flash = (address & 0x800000u) != 0u;
    const uint32_t offset = address & 0x7fffffu;
    if (!data_flash)
        return flash_store(data, offset, size, value);
    k22_data_internal_store_bytes(data->flexnvm, offset, size, value);
    return true;
}

static bool flash_memory_range_protected(const K22Data* data, uint32_t address, uint32_t length) {
    const bool data_flash = (address & 0x800000u) != 0u;
    const uint32_t offset = address & 0x7fffffu;
    const uint32_t region_count = data_flash ? 8u : 32u;
    const uint32_t memory_size =
        data_flash ? flash_data_size(data) : data->profile->program_flash_size;
    const uint32_t first = ((uint64_t)offset * region_count) / memory_size;
    const uint32_t last = ((uint64_t)(offset + length - 1u) * region_count) / memory_size;
    for (uint32_t region = first; region <= last; region++) {
        const uint8_t protection = data->flash[data_flash ? 0x17u : 0x10u + region / 8u];
        if ((protection & (1u << (region & 7u))) == 0u)
            return true;
    }
    return false;
}

static bool flash_program_words(K22Data* data, uint32_t address, uint8_t words,
                                bool* verify_failure) {
    for (uint8_t word = 0; word < words; word++) {
        const uint8_t offset = (uint8_t)(4u + word * 4u);
        const uint32_t value = (uint32_t)flash_fccob(data, offset) |
                               ((uint32_t)flash_fccob(data, offset + 1u) << 8u) |
                               ((uint32_t)flash_fccob(data, offset + 2u) << 16u) |
                               ((uint32_t)flash_fccob(data, offset + 3u) << 24u);
        uint32_t previous = 0;
        if (!flash_memory_load(data, address + (uint32_t)word * 4u, 4u, &previous))
            return false;
        if (previous != UINT32_MAX) {
            *verify_failure = true;
            return true;
        }
        if (!flash_memory_store(data, address + (uint32_t)word * 4u, 4u, value))
            return false;
    }
    return true;
}

static bool flash_program_buffer(K22Data* data, uint32_t address, uint32_t length,
                                 bool* verify_failure) {
    for (uint32_t offset = 0; offset < length; offset += 4u) {
        uint32_t previous = 0;
        if (!flash_memory_load(data, address + offset, 4u, &previous))
            return false;
        if (previous != UINT32_MAX) {
            *verify_failure = true;
            return true;
        }
        const uint32_t value = k22_data_internal_load_bytes(data->flexram, offset, 4u);
        if (!flash_memory_store(data, address + offset, 4u, value))
            return false;
    }
    return true;
}

static bool flash_erase(K22Data* data, uint32_t start, uint32_t length) {
    for (uint32_t offset = 0; offset < length; offset += 4u) {
        if (!flash_memory_store(data, start + offset, 4u, UINT32_MAX))
            return false;
    }
    return true;
}

static bool flash_range_erased(K22Data* data, uint32_t start, uint32_t length) {
    for (uint32_t offset = 0; offset < length; offset += 4u) {
        uint32_t word = 0;
        if (!flash_memory_load(data, start + offset, 4u, &word) || word != UINT32_MAX)
            return false;
    }
    return true;
}

static uint32_t flash_block_size(const K22Data* data) {
    if (data->profile->program_flash_size >= 0x80000u)
        return data->profile->program_flash_size == 0x100000u ? 0x80000u : 0x40000u;
    return data->profile->program_flash_size;
}

static bool flash_block_range(const K22Data* data, uint32_t address, uint32_t* start,
                              uint32_t* length, bool* data_flash) {
    uint32_t offset = 0u;
    if (!flash_memory_range(data, address, 1u, data_flash, &offset))
        return false;
    *length = *data_flash ? flash_data_size(data) : flash_block_size(data);
    *start = *data_flash ? 0x800000u : address & ~(*length - 1u);
    return true;
}

static bool flash_read_resource(K22Data* data) {
    const bool ftfe = k22_profile_has_peripheral(data->profile, K22_PERIPHERAL_FTFE);
    const uint32_t address = flash_address(data);
    const uint8_t option = flash_fccob(data, ftfe ? 4u : 8u);
    const uint8_t length = ftfe ? 8u : 4u;
    const uint8_t* source = NULL;
    uint32_t offset = address;
    if (option == 0u && address < (ftfe ? 1024u : 256u))
        source = data->flash_program_ifr;
    else if (option == 0u && ftfe && data->profile->flexnvm_size != 0u && address >= 0x800000u &&
             address - 0x800000u <= 1024u - length) {
        source = data->flash_data_ifr;
        offset = address - 0x800000u;
    } else if (option == 1u && address == (ftfe ? 8u : 0u)) {
        static const uint8_t ftfa_version[8] = {0x01u, 0x00u, 0x46u, 0x54u,
                                                0x46u, 0x41u, 0x00u, 0x00u};
        static const uint8_t ftfe_version[8] = {0x01u, 0x00u, 0x46u, 0x54u,
                                                0x46u, 0x45u, 0x00u, 0x00u};
        source = ftfe ? ftfe_version : ftfa_version;
        offset = 0;
    }
    if (source == NULL || offset > 1024u - length || (address & (length - 1u)) != 0u)
        return false;
    for (uint8_t index = 0; index < length; index++)
        flash_set_fccob(data, (uint8_t)(4u + index), source[offset + index]);
    return true;
}

static bool flash_once_record(const K22Data* data, uint8_t index, uint32_t* offset,
                              uint8_t* length) {
    if (k22_profile_has_peripheral(data->profile, K22_PERIPHERAL_FTFE)) {
        if (index > 7u)
            return false;
        *offset = 0x3c0u + (uint32_t)index * 8u;
        *length = 8u;
        return true;
    }
    if (index <= 7u) {
        *offset = 0xc0u + (uint32_t)index * 4u;
        *length = 4u;
        return true;
    }
    if (index >= 0x10u && index <= 0x13u) {
        *offset = 0xe0u + (uint32_t)(index - 0x10u) * 8u;
        *length = 8u;
        return true;
    }
    return false;
}

static bool flash_read_once(K22Data* data) {
    uint32_t offset = 0;
    uint8_t length = 0;
    if (!flash_once_record(data, flash_fccob(data, 1u), &offset, &length))
        return false;
    for (uint8_t index = 0; index < length; index++)
        flash_set_fccob(data, (uint8_t)(4u + index), data->flash_program_ifr[offset + index]);
    return true;
}

static bool flash_program_once(K22Data* data, bool* verify_failure) {
    uint32_t offset = 0;
    uint8_t length = 0;
    if (!flash_once_record(data, flash_fccob(data, 1u), &offset, &length))
        return false;
    for (uint8_t index = 0; index < length; index++) {
        if (data->flash_program_ifr[offset + index] != 0xffu)
            return false;
    }
    for (uint8_t index = 0; index < length; index++)
        data->flash_program_ifr[offset + index] = flash_fccob(data, (uint8_t)(4u + index));
    for (uint8_t index = 0; index < length; index++)
        *verify_failure |=
            data->flash_program_ifr[offset + index] != flash_fccob(data, (uint8_t)(4u + index));
    return true;
}

static bool flash_verify_key(K22Data* data) {
    if (data->flash_key_blocked || (data->flash[2] & 0xc0u) != 0x80u)
        return false;
    uint8_t aggregate_and = 0xffu;
    uint8_t aggregate_or = 0u;
    bool matches = true;
    for (uint8_t index = 0; index < 8u; index++) {
        const uint8_t key = flash_fccob(data, (uint8_t)(4u + index));
        aggregate_and &= key;
        aggregate_or |= key;
        matches &= key == data->flash_config[index];
    }
    if (aggregate_or == 0u || aggregate_and == 0xffu)
        matches = false;
    if (matches)
        data->flash[2] = (uint8_t)((data->flash[2] & 0xfcu) | 0x02u);
    else
        data->flash_key_blocked = true;
    return matches;
}

static bool flash_program_partition(K22Data* data) {
    static const uint8_t valid_depart[] = {0x00u, 0x03u, 0x04u, 0x05u, 0x08u,
                                           0x0bu, 0x0cu, 0x0du, 0x0fu};
    if (data->profile->flexnvm_size == 0u || data->flash_partitioned)
        return false;
    const uint8_t load = flash_fccob(data, 3u);
    const uint8_t eeesize = flash_fccob(data, 4u);
    const uint8_t depart = flash_fccob(data, 5u);
    bool valid_depart_code = false;
    for (size_t index = 0; index < sizeof(valid_depart); index++)
        valid_depart_code |= depart == valid_depart[index];
    const bool no_eeprom = depart == 0x00u || depart == 0x0du || depart == 0x0fu;
    const bool eeprom_disabled = eeesize == 0x0fu;
    if (load > 1u || (eeesize & 0xc0u) != 0u || (depart & 0xf0u) != 0u || !valid_depart_code ||
        (eeesize < 2u && !eeprom_disabled) || (eeesize > 9u && !eeprom_disabled) ||
        no_eeprom != eeprom_disabled)
        return false;
    memset(data->flexnvm, 0xff, data->profile->flexnvm_size);
    data->flash_data_ifr[0x3fcu] = depart;
    data->flash_data_ifr[0x3fdu] = eeesize;
    data->flash_partitioned = true;
    data->flexram_eeprom = !no_eeprom;
    memset(data->flexram, 0xff, data->profile->flexram_size);
    data->flash[1] = (uint8_t)((data->flash[1] & 0xfcu) | (data->flexram_eeprom ? 0x01u : 0x02u));
    return true;
}

static bool flash_set_flexram(K22Data* data) {
    if (data->flexram == NULL)
        return false;
    const uint8_t control = flash_fccob(data, 1u);
    if (control != 0u && control != 0xffu)
        return false;
    if (control == 0u && !data->flash_partitioned)
        return false;
    memset(data->flexram, 0xff, data->profile->flexram_size);
    data->flexram_eeprom = control == 0u;
    data->flash[1] = (uint8_t)((data->flash[1] & 0xfcu) | (data->flexram_eeprom ? 0x01u : 0x02u));
    return true;
}

static bool flash_swap_address_valid(const K22Data* data, uint32_t address) {
    const uint32_t block_size = data->profile->program_flash_size / 2u;
    return (address & 0x0fu) == 0u && address < block_size &&
           (address < 0x400u || address >= 0x410u);
}

static bool flash_swap_program_indicator(K22Data* data, uint8_t block, uint16_t value) {
    const uint32_t logical_address =
        data->flash_swap_address + ((uint32_t)(block ^ data->flash_swap_current_block) *
                                    (data->profile->program_flash_size / 2u));
    return flash_memory_store(data, logical_address, 2u, value);
}

static bool flash_swap_control(K22Data* data, uint32_t address, bool* verify_failure) {
    const uint8_t control = flash_fccob(data, 4u);
    if (!flash_swap_address_valid(data, address))
        return false;
    if (control == 0x08u) {
        flash_set_fccob(data, 5u, data->flash_swap_mode);
        flash_set_fccob(data, 6u, data->flash_swap_current_block);
        flash_set_fccob(data, 7u, data->flash_swap_next_block);
        return true;
    }
    if (control == 0x01u) {
        if (data->flash_swap_mode != 0u)
            return false;
        data->flash_swap_address = address;
        if (!flash_swap_program_indicator(data, 0u, 0xff00u)) {
            *verify_failure = true;
            return true;
        }
        data->flash_swap_mode = 3u;
        data->flash_swap_current_block = 0u;
        data->flash_swap_next_block = 0u;
        return true;
    }
    if (address != data->flash_swap_address)
        return false;
    if (control == 0x02u) {
        if (data->flash_swap_mode != 1u)
            return false;
        if (!flash_swap_program_indicator(data, data->flash_swap_current_block, 0xff00u)) {
            *verify_failure = true;
            return true;
        }
        data->flash_swap_mode = 2u;
        return true;
    }
    if (control == 0x04u) {
        if (data->flash_swap_mode != 3u)
            return false;
        if (!flash_swap_program_indicator(data, data->flash_swap_current_block, 0u)) {
            *verify_failure = true;
            return true;
        }
        data->flash_swap_mode = 4u;
        data->flash_swap_next_block = data->flash_swap_current_block ^ 1u;
        return true;
    }
    return false;
}

static void flash_swap_erased(K22Data* data, uint32_t start, uint32_t length) {
    if (data->flash_swap_mode != 2u)
        return;
    const uint32_t block_size = data->profile->program_flash_size / 2u;
    const uint32_t nonactive = data->flash_swap_address + block_size;
    if (start <= nonactive && nonactive - start < length)
        data->flash_swap_mode = 3u;
}

static bool flash_swap_range_protected(const K22Data* data, uint32_t start, uint32_t length,
                                       bool erase) {
    if (data->flash_swap_mode == 0u || length == 0u)
        return false;
    const uint32_t block_size = data->profile->program_flash_size / 2u;
    const uint32_t active = data->flash_swap_address;
    const uint32_t nonactive = active + block_size;
    const bool active_overlap = start <= active && active - start < length;
    const bool nonactive_overlap = start <= nonactive && nonactive - start < length;
    if (active_overlap)
        return true;
    return nonactive_overlap &&
           (!erase || (data->flash_swap_mode != 2u && data->flash_swap_mode != 3u));
}

static uint8_t flash_busy_banks(uint8_t command, uint32_t address) {
    switch (command) {
    case 0x00u:
    case 0x01u:
    case 0x02u:
    case 0x03u:
    case 0x06u:
    case 0x07u:
    case 0x08u:
    case 0x09u:
    case 0x0bu:
        return (address & 0x800000u) != 0u ? 2u : 1u;
    case 0x40u:
    case 0x44u:
        return 3u;
    case 0x41u:
    case 0x43u:
    case 0x45u:
    case 0x46u:
        return 1u;
    default:
        return 2u;
    }
}

void k22_data_internal_flash_update_interrupts(K22Data* data) {
    k22_data_internal_interrupt(data, K22_DATA_INTERRUPT_FTFA,
                                (data->flash[1] & 0x80u) != 0u && (data->flash[0] & 0x80u) != 0u);
    k22_data_internal_interrupt(data, K22_DATA_INTERRUPT_FLASH_COLLISION,
                                (data->flash[1] & 0x40u) != 0u && (data->flash[0] & 0x40u) != 0u);
}

static void flash_execute(K22Data* data) {
    data->flash[0] &= 0x70u;
    const uint8_t command = flash_fccob(data, 0u);
    const uint32_t address = flash_address(data);
    const bool ftfe = k22_profile_has_peripheral(data->profile, K22_PERIPHERAL_FTFE);
    const uint32_t sector_size = ftfe ? 4096u : 2048u;
    const uint8_t program_command = ftfe ? 0x07u : 0x06u;
    const uint8_t program_words = ftfe ? 2u : 1u;
    bool valid = true;
    bool protection_failure = false;
    bool verify_failure = false;
    if (command == program_command) {
        bool data_flash = false;
        uint32_t offset = 0;
        valid = address % (program_words * 4u) == 0u &&
                flash_memory_range(data, address, program_words * 4u, &data_flash, &offset);
        protection_failure =
            valid && (flash_memory_range_protected(data, address, program_words * 4u) ||
                      flash_swap_range_protected(data, address, program_words * 4u, false));
        if (valid && !protection_failure)
            valid = flash_program_words(data, address, program_words, &verify_failure);
    } else if (command == 0x09u) {
        bool data_flash = false;
        uint32_t offset = 0;
        const uint32_t start = address & ~(sector_size - 1u);
        valid = (address & 0x0fu) == 0u &&
                flash_memory_range(data, start, sector_size, &data_flash, &offset);
        protection_failure = valid && (flash_memory_range_protected(data, start, sector_size) ||
                                       flash_swap_range_protected(data, start, sector_size, true));
        if (valid && !protection_failure)
            valid = flash_erase(data, start, sector_size);
        if (valid && !protection_failure)
            flash_swap_erased(data, start, sector_size);
    } else if (command == 0x08u) {
        bool data_flash = false;
        uint32_t block_size = 0u;
        uint32_t start = 0u;
        valid = (address & 0x0fu) == 0u &&
                flash_block_range(data, address, &start, &block_size, &data_flash) &&
                (ftfe || data->profile->program_flash_size == 0x80000u) &&
                !(data_flash && data->flexram_eeprom);
        protection_failure = valid && (flash_memory_range_protected(data, start, block_size) ||
                                       flash_swap_range_protected(data, start, block_size, true));
        if (valid && !protection_failure)
            valid = flash_erase(data, start, block_size);
        if (valid && !protection_failure)
            flash_swap_erased(data, start, block_size);
    } else if (command == 0x44u) {
        protection_failure =
            flash_memory_range_protected(data, 0, data->profile->program_flash_size);
        if (!protection_failure && data->profile->flexnvm_size != 0u)
            protection_failure = data->flash[0x17u] != 0xffu;
        if (!protection_failure) {
            valid = flash_erase(data, 0, data->profile->program_flash_size);
            if (valid && data->profile->flexnvm_size != 0u)
                memset(data->flexnvm, 0xff, data->profile->flexnvm_size);
            if (valid && data->flexram != NULL)
                memset(data->flexram, 0xff, data->profile->flexram_size);
            if (valid) {
                memset(data->flash_config, 0xff, sizeof(data->flash_config));
                memset(data->flash_data_ifr, 0xff, sizeof(data->flash_data_ifr));
                data->flash_partitioned = false;
                data->flexram_eeprom = false;
                data->flash[1] =
                    (uint8_t)((data->flash[1] & 0xfcu) | (data->flexram != NULL ? 0x02u : 0u));
                data->flash[2] = 0xfeu;
                data->flash_swap_address = 0u;
                data->flash_swap_mode = 0u;
                data->flash_swap_current_block = 0u;
                data->flash_swap_next_block = 0u;
            }
        }
    } else if (command == 0x00u) {
        bool data_flash = false;
        const uint8_t margin = flash_fccob(data, 4u);
        uint32_t block_size = 0u;
        uint32_t start = 0u;
        valid = margin <= 2u && (address & 0x0fu) == 0u &&
                flash_block_range(data, address, &start, &block_size, &data_flash) &&
                (ftfe || data->profile->program_flash_size == 0x80000u) &&
                !(data_flash && data->flexram_eeprom);
        verify_failure = valid && !flash_range_erased(data, start, block_size);
    } else if (command == 0x01u) {
        const uint32_t count = ((uint32_t)flash_fccob(data, 4u) << 8u) | flash_fccob(data, 5u);
        const uint8_t margin = flash_fccob(data, 6u);
        const uint32_t length = count * 16u;
        bool data_flash = false;
        uint32_t offset = 0;
        valid = margin <= 2u && count != 0u && (address & 0x0fu) == 0u &&
                length <= sector_size - (address & (sector_size - 1u)) &&
                flash_memory_range(data, address, length, &data_flash, &offset);
        verify_failure = valid && !flash_range_erased(data, address, length);
    } else if (command == 0x40u) {
        const uint8_t margin = flash_fccob(data, 1u);
        valid = margin <= 2u;
        verify_failure = valid && !flash_range_erased(data, 0, data->profile->program_flash_size);
        if (valid && !verify_failure && data->profile->flexnvm_size != 0u)
            verify_failure = !flash_range_erased(data, 0x800000u, flash_data_size(data));
        if (valid && !verify_failure)
            data->flash[2] = (uint8_t)((data->flash[2] & 0xfcu) | 0x02u);
    } else if (command == 0x02u) {
        uint32_t actual = 0;
        const uint8_t margin = flash_fccob(data, 4u);
        const uint32_t expected =
            (uint32_t)flash_fccob(data, 8u) | ((uint32_t)flash_fccob(data, 9u) << 8u) |
            ((uint32_t)flash_fccob(data, 10u) << 16u) | ((uint32_t)flash_fccob(data, 11u) << 24u);
        bool data_flash = false;
        uint32_t offset = 0;
        valid = margin >= 1u && margin <= 2u && (address & 3u) == 0u &&
                flash_memory_range(data, address, 4u, &data_flash, &offset);
        verify_failure =
            valid && (!flash_memory_load(data, address, 4u, &actual) || actual != expected);
    } else if (command == 0x03u) {
        valid = flash_read_resource(data);
    } else if (command == 0x41u) {
        valid = flash_read_once(data);
    } else if (command == 0x43u) {
        valid = flash_program_once(data, &verify_failure);
    } else if (command == 0x0bu) {
        const uint32_t count = ((uint32_t)flash_fccob(data, 4u) << 8u) | flash_fccob(data, 5u);
        const uint32_t length = count * 16u;
        bool data_flash = false;
        uint32_t offset = 0;
        valid = ftfe && data->flexram != NULL && (data->flash[1] & 0x02u) != 0u && count != 0u &&
                length <= 1024u && (address & 0x0fu) == 0u &&
                length <= sector_size - (address & (sector_size - 1u)) &&
                flash_memory_range(data, address, length, &data_flash, &offset);
        protection_failure = valid && (flash_memory_range_protected(data, address, length) ||
                                       flash_swap_range_protected(data, address, length, false));
        if (valid && !protection_failure)
            valid = flash_program_buffer(data, address, length, &verify_failure);
    } else if (command == 0x45u) {
        valid = flash_verify_key(data);
    } else if (command == 0x80u) {
        valid = ftfe && flash_program_partition(data);
    } else if (command == 0x81u) {
        valid = ftfe && flash_set_flexram(data);
    } else if (command == 0x46u) {
        valid = ftfe && data->profile->flexnvm_size == 0u;
        if (valid)
            valid = flash_swap_control(data, address, &verify_failure);
    } else {
        valid = false;
    }
    if (protection_failure)
        data->flash[0] |= 0x10u;
    else if (!valid)
        data->flash[0] |= 0x20u;
    else if (verify_failure)
        data->flash[0] |= 1u;
    data->flash_cycles =
        command == 0x08u || command == 0x09u || command == 0x44u || command == 0x80u ? 2000u : 40u;
    data->flash_busy_banks = valid && !protection_failure ? flash_busy_banks(command, address) : 0u;
    data->flash_busy_start = 0u;
    data->flash_busy_length = 0u;
    if (data->flash_busy_banks == 1u) {
        data->flash_busy_length = flash_block_size(data);
        data->flash_busy_start = address & ~(data->flash_busy_length - 1u);
    } else if (data->flash_busy_banks == 2u) {
        data->flash_busy_length = flash_data_size(data);
    } else if (data->flash_busy_banks == 3u) {
        data->flash_busy_length = UINT32_MAX;
    }
    k22_data_internal_flash_update_interrupts(data);
}

bool k22_data_internal_flash_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    const uint32_t offset = address - FLASH_BASE;
    if (!k22_data_internal_valid_access(offset, size, sizeof(data->flash)))
        return false;
    const bool ftfe = k22_profile_has_peripheral(data->profile, K22_PERIPHERAL_FTFE);
    for (uint8_t index = 0; index < size; index++) {
        const uint32_t current = offset + index;
        const bool common = current <= 0x13u;
        const bool extension = ftfe ? current >= 0x16u && current <= 0x17u
                                    : (current >= 0x18u && current <= 0x28u) || current == 0x2bu;
        if (!common && !extension)
            return false;
    }
    *value = k22_data_internal_load_bytes(data->flash, offset, size);
    return true;
}

bool k22_data_internal_flash_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    const uint32_t offset = address - FLASH_BASE;
    if (!k22_data_internal_valid_access(offset, size, sizeof(data->flash)))
        return false;
    if (offset == 0 && size == 1) {
        data->flash[0] &= (uint8_t)~(value & 0x70u);
        if ((value & 0x80u) != 0 && (data->flash[0] & 0xb0u) == 0x80u)
            flash_execute(data);
        else
            k22_data_internal_flash_update_interrupts(data);
        return true;
    }
    if (offset == 1u && size == 1u) {
        data->flash[1] = (uint8_t)((data->flash[1] & 0x2fu) | (value & 0xd0u));
        k22_data_internal_flash_update_interrupts(data);
        return true;
    }
    if (offset >= 4u && offset <= 0x10u - size) {
        if ((data->flash[0] & 0x80u) != 0u)
            k22_data_internal_store_bytes(data->flash, offset, size, value);
        return true;
    }
    if (offset >= 0x10u && offset <= 0x14u - size) {
        if ((data->flash[0] & 0x80u) == 0u)
            return true;
        for (uint8_t index = 0; index < size; index++)
            data->flash[offset + index] &= (uint8_t)(value >> (index * 8u));
        return true;
    }
    if (k22_profile_has_peripheral(data->profile, K22_PERIPHERAL_FTFE) &&
        (offset == 0x16u || offset == 0x17u) && size == 1u) {
        if ((data->flash[0] & 0x80u) != 0u)
            data->flash[offset] &= (uint8_t)value;
        return true;
    }
    return false;
}
