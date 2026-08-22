
static uint32_t reverse_bits(uint32_t value, uint8_t count) {
    uint32_t result = 0;
    for (uint8_t bit = 0; bit < count; bit++) {
        result <<= 1;
        result |= (value >> bit) & 1u;
    }
    return result;
}

static void crc_accumulate(K22Data* data, uint8_t byte) {
    const bool width32 = (data->crc_control & 0x01000000u) != 0;
    const uint8_t bits = width32 ? 32u : 16u;
    const uint32_t mask = width32 ? UINT32_MAX : 0xffffu;
    uint32_t input = byte;
    data->crc_value &= mask;
    data->crc_value ^= input << (bits - 8u);
    for (uint8_t bit = 0; bit < 8; bit++) {
        const bool top = (data->crc_value & (1u << (bits - 1u))) != 0;
        data->crc_value = (data->crc_value << 1) & mask;
        if (top)
            data->crc_value ^= data->crc_polynomial & mask;
    }
}

static uint32_t crc_result(const K22Data* data) {
    const bool width32 = (data->crc_control & 0x01000000u) != 0;
    const uint8_t bits = width32 ? 32u : 16u;
    const uint32_t stored_high = width32 ? 0u : data->crc_value & 0xffff0000u;
    uint32_t value = data->crc_value;
    const uint8_t transpose = (uint8_t)((data->crc_control >> 28) & 3u);
    uint32_t transformed = 0;
    const uint8_t byte_count = (uint8_t)(bits / 8u);
    for (uint8_t index = 0; index < byte_count; index++) {
        uint8_t byte = (uint8_t)(value >> (index * 8u));
        if (transpose == 1 || transpose == 2)
            byte = (uint8_t)reverse_bits(byte, 8);
        const uint8_t target = transpose >= 2 ? (uint8_t)(byte_count - 1u - index) : index;
        transformed |= (uint32_t)byte << (target * 8u);
    }
    value = transformed;
    if ((data->crc_control & 0x04000000u) != 0)
        value = ~value;
    return width32 ? value : stored_high | (value & 0xffffu);
}

static bool crc_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    const uint32_t offset = address - CRC_BASE;
    if (!valid_access(offset, size, 12))
        return false;
    if (offset < 4) {
        *value = crc_result(data) >> (offset * 8u);
        if (size < 4)
            *value &= (1u << (size * 8u)) - 1u;
    } else if (offset < 8)
        *value = data->crc_polynomial >> ((offset - 4u) * 8u);
    else
        *value = data->crc_control >> ((offset - 8u) * 8u);
    return true;
}

static bool crc_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    const uint32_t offset = address - CRC_BASE;
    if (!valid_access(offset, size, 12))
        return false;
    if (offset < 4) {
        if ((data->crc_control & 0x02000000u) != 0) {
            uint32_t mask = size == 4 ? UINT32_MAX : (1u << (size * 8u)) - 1u;
            data->crc_value =
                (data->crc_value & ~(mask << (offset * 8u))) | ((value & mask) << (offset * 8u));
            return true;
        }
        const uint8_t transpose = (uint8_t)((data->crc_control >> 30) & 3u);
        for (uint8_t index = 0; index < size; index++) {
            const uint8_t source = transpose >= 2 ? (uint8_t)(size - 1u - index) : index;
            uint8_t byte = (uint8_t)(value >> (source * 8u));
            if (transpose == 1 || transpose == 2)
                byte = (uint8_t)reverse_bits(byte, 8);
            crc_accumulate(data, byte);
        }
        return true;
    }
    if (offset < 8) {
        uint8_t bytes[4];
        store_bytes(bytes, 0, 4, data->crc_polynomial);
        store_bytes(bytes, offset - 4u, size, value);
        data->crc_polynomial = load_bytes(bytes, 0, 4);
    } else {
        uint8_t bytes[4];
        store_bytes(bytes, 0, 4, data->crc_control);
        store_bytes(bytes, offset - 8u, size, value);
        data->crc_control = load_bytes(bytes, 0, 4) & 0xf7000000u;
    }
    return true;
}

static uint32_t rng_next(uint32_t value) {
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value == 0 ? 0x6d2b79f5u : value;
}

static bool rng_read(K22Data* data, uint32_t address, uint8_t size, uint32_t* value) {
    const uint32_t offset = address - RNG_BASE;
    if (!valid_access(offset, size, 16) || size != 4)
        return false;
    if (offset == 0)
        *value = data->rng_control;
    else if (offset == 4)
        *value = data->rng_status;
    else if (offset == 8)
        *value = data->rng_error;
    else if (offset == 12) {
        *value = data->rng_output;
        data->rng_status &= ~1u;
        interrupt(data, K22_DATA_INTERRUPT_RNG, false);
    } else
        return false;
    return true;
}

static bool rng_write(K22Data* data, uint32_t address, uint8_t size, uint32_t value) {
    const uint32_t offset = address - RNG_BASE;
    if (size != 4 || !valid_access(offset, size, 16))
        return false;
    if (offset != 0)
        return false;
    data->rng_control = value & 0x1fu;
    if ((value & 0x10u) != 0) {
        data->rng_status = 0;
        data->rng_error = 0;
        interrupt(data, K22_DATA_INTERRUPT_RNG, false);
    }
    if ((value & 1u) != 0)
        data->rng_cycles = 64;
    return true;
}
