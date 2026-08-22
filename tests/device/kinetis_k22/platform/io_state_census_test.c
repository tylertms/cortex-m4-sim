#include "device/kinetis_k22/gpio/io.h"

#include "test.h"

enum {
    PORTA = 0x40049000u,
    GPIOA = 0x400ff000u,
    USB0 = 0x40072000u,
    CAN0 = 0x40024000u,
    I2S0 = 0x4002f000u,
    FLEXBUS = 0x4000c000u,
    SYSMPU = 0x4000d000u,
};

#define MCM UINT32_C(0xe0080008)

typedef struct {
    uint32_t random;
    uint32_t reads;
    uint32_t writes;
    uint32_t signals;
    uint64_t fingerprint;
} IoCensus;

static uint32_t next_random(IoCensus* census) {
    uint32_t value = census->random;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    census->random = value;
    return value;
}

static void mix(IoCensus* census, uint32_t value) {
    census->fingerprint = (census->fingerprint ^ value) * UINT64_C(1099511628211);
}

static void record_event(void* context, const K22IoEvent* event) {
    IoCensus* census = context;
    census->signals++;
    mix(census, event->type ^ event->source ^ event->value ^ event->auxiliary);
}

static void randomize_state(K22Io* io, IoCensus* census, uint32_t random) {
    const uint8_t port = (uint8_t)(random % K22_IO_PORT_COUNT);
    const uint8_t pin = (uint8_t)((random >> 8u) % K22_IO_PIN_COUNT);
    const uint32_t second = next_random(census);
    io->port_pcr[port][pin] = random;
    io->port_isfr[port] = second;
    io->port_dfer[port] = random ^ second;
    io->port_dfwr[port] = (uint8_t)random;
    io->gpio_pdor[port] = random;
    io->gpio_pddr[port] = second;
    io->gpio_external[port] = random >> 1u;
    io->gpio_external_drive[port] = second >> 1u;
    io->gpio_filtered[port] = next_random(census);
    io->gpio_pending[port] = next_random(census);
    io->gpio_filter_age[port][pin] = (uint8_t)second;
    io->usb[(random >> 16u) % sizeof(io->usb)] = (uint8_t)second;
    io->usb[0x84u] = (uint8_t)random;
    io->usb[0x94u] = (uint8_t)(second | 1u);
    io->usb_cycle_remainder = second % 1000u;
    io->can[(random >> 16u) % (sizeof(io->can) / sizeof(io->can[0]))] = second;
    io->can[0] = random & ~(1u << 31u);
    io->can[0x28u / 4u] = second;
    io->can[0x30u / 4u] = random;
    io->i2s[(random >> 16u) % (sizeof(io->i2s) / sizeof(io->i2s[0]))] = second;
    io->i2s[0] = random | UINT32_C(0x80000000);
    io->i2s[0x80u / 4u] = second | UINT32_C(0x80000000);
    io->i2s_receive_read = (uint8_t)(random % K22_IO_FIFO_CAPACITY);
    io->i2s_receive_write = (uint8_t)(second % K22_IO_FIFO_CAPACITY);
    io->i2s_receive_count = (uint8_t)(random % (K22_IO_FIFO_CAPACITY + 1u));
    io->i2s_transmit_read = (uint8_t)(second % K22_IO_FIFO_CAPACITY);
    io->i2s_transmit_write = (uint8_t)(random % K22_IO_FIFO_CAPACITY);
    io->i2s_transmit_count = (uint8_t)(second % (K22_IO_FIFO_CAPACITY + 1u));
    io->flexbus[(random >> 16u) % (sizeof(io->flexbus) / sizeof(io->flexbus[0]))] = second;
    io->mcm[(random >> 20u) % (sizeof(io->mcm) / sizeof(io->mcm[0]))] = second;
    io->sysmpu[(random >> 12u) % (sizeof(io->sysmpu) / sizeof(io->sysmpu[0]))] = second;
    const uint8_t region = (uint8_t)((random >> 24u) % 12u);
    const uint32_t descriptor = 0x400u / 4u + (uint32_t)region * 4u;
    io->sysmpu[0] = random | 1u;
    io->sysmpu[descriptor] = second & 0xffff0000u;
    io->sysmpu[descriptor + 1u] = io->sysmpu[descriptor] + (random & 0xffffu);
    io->sysmpu[descriptor + 2u] = random;
    io->sysmpu[descriptor + 3u] = 1u;
}

static uint32_t address_for(uint32_t random) {
    static const uint32_t bases[] = {PORTA, GPIOA, USB0, CAN0, I2S0, FLEXBUS, MCM, SYSMPU};
    static const uint32_t spans[] = {0xd0u, 0x18u, 0x160u, 0x8c0u, 0x108u, 0x64u, 0x3cu, 0x830u};
    const uint8_t region = (uint8_t)(random % (sizeof(bases) / sizeof(bases[0])));
    return bases[region] + ((random >> 8u) % spans[region]);
}

int main(void) {
    TestState state = {0u, 0u, 0u};
    IoCensus census = {UINT32_C(0x243f6a88), 0u, 0u, 0u, UINT64_C(14695981039346656037)};
    K22IoConfiguration configuration =
        k22_io_default_configuration(k22_profile_get(K22_PROFILE_MK22FN1M012));
    for (uint8_t port = 0u; port < K22_IO_PORT_COUNT; port++)
        configuration.package_pin_mask[port] = UINT32_MAX;
    configuration.event_handler = record_event;
    configuration.event_context = &census;
    K22Io io;
    expect(&state, k22_io_init(&io, configuration), "initialize I/O state census");
    for (K22PeripheralId peripheral = 0; peripheral < K22_PERIPHERAL_COUNT; peripheral++)
        k22_io_set_clock(&io, peripheral, true);
    for (uint32_t iteration = 0u; iteration < 100000u; iteration++) {
        const uint32_t random = next_random(&census);
        randomize_state(&io, &census, random);
        const uint32_t address = address_for(random);
        const uint8_t size = (uint8_t)(random % 6u);
        uint32_t value = UINT32_MAX;
        const bool read = k22_io_read(&io, address, size, &value);
        const bool written = k22_io_write(&io, address, size, random ^ UINT32_C(0xa5a55a5a));
        const uint8_t port = (uint8_t)((random >> 8u) % K22_IO_PORT_COUNT);
        const uint8_t pin = (uint8_t)((random >> 16u) % K22_IO_PIN_COUNT);
        const bool driven = k22_io_drive_pin(&io, port, pin, (random & 1u) != 0u);
        const bool released = k22_io_release_pin(&io, port, pin);
        K22CanFrame frame = {
            random, (uint8_t)(random % 10u), {0u}, (random & 1u) != 0u, (random & 2u) != 0u};
        frame.data[0] = (uint8_t)random;
        const bool received = k22_io_can_receive(&io, &frame);
        const bool i2s = k22_io_i2s_receive(&io, random);
        uint32_t sample = 0u;
        const bool transmitted = k22_io_i2s_transmit(&io, &sample);
        const bool protected =
            k22_io_sysmpu_access(&io, random, (uint8_t)((random >> 20u) % 9u), (random & 4u) != 0u,
                                 (K22SysMpuAccess)((random >> 24u) % 4u));
        const bool flexbus = k22_io_flexbus_transfer(&io, random, size, (random & 8u) != 0u,
                                                     random ^ UINT32_C(0x5a5aa5a5));
        k22_io_advance(&io, (random & 0x7ffu) + 1u);
        census.reads += read;
        census.writes += written;
        mix(&census, value);
        mix(&census, sample);
        mix(&census, read | (written << 1u) | (driven << 2u) | (released << 3u) | (received << 4u) |
                         (i2s << 5u) | (transmitted << 6u) | (protected << 7u) | (flexbus << 8u));
        mix(&census, k22_io_pin_input(&io, port));
        mix(&census, k22_io_irq_asserted(&io, (uint8_t)(random >> 24u)));
    }
    expect(&state,
           census.reads == 7833u && census.writes == 6818u && census.signals == 744342u &&
               census.fingerprint == UINT64_C(8816404939693753862),
           "I/O state census matches");
    return test_finish(&state);
}
