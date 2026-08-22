#include "kinetis_k22.h"

#include <stdint.h>

#include "device/kinetis_k22/internal.h"
#include "k22_test.h"
#include "test.h"

enum {
    CMT_CGH1 = 0x40062000u,
    CMT_CGL1 = 0x40062001u,
    CMT_CGH2 = 0x40062002u,
    CMT_CGL2 = 0x40062003u,
    CMT_OC = 0x40062004u,
    CMT_MSC = 0x40062005u,
    CMT_CMD1 = 0x40062006u,
    CMT_CMD2 = 0x40062007u,
    CMT_CMD3 = 0x40062008u,
    CMT_CMD4 = 0x40062009u,
    CMT_PPS = 0x4006200au,
    CMT_DMA = 0x4006200bu,
    DMA_SERQ = 0x4000801bu,
    DMA_TCD0 = 0x40009000u,
    DMAMUX_CHCFG0 = 0x40021000u,
    SIM_SCGC4 = 0x40048034u,
};

static KinetisK22* make_device(TestState* state) {
    KinetisK22Configuration configuration = kinetis_k22_default_configuration();
    configuration.profile = KINETIS_K22_PROFILE_MK22FN1M012;
    configuration.package = KINETIS_K22_PACKAGE_LQ_144_LQFP;
    KinetisK22* device = kinetis_k22_create(configuration);
    expect(state, device != NULL, "device != NULL");
    expect(state, k22_test_disable_watchdog(device), "k22_test_disable_watchdog(device)");
    const uint32_t gate = 1u << 2u;
    expect(state, kinetis_k22_write(device, SIM_SCGC4, &gate, sizeof(gate)),
           "kinetis_k22_write(device, SIM_SCGC4, &gate, sizeof(gate))");
    return device;
}

static void reset_device(TestState* state, KinetisK22* device) {
    kinetis_k22_warm_reset(device, 0u, 4u);
    expect(state, k22_test_disable_watchdog(device), "k22_test_disable_watchdog(device)");
    const uint32_t gate = 1u << 2u;
    expect(state, kinetis_k22_write(device, SIM_SCGC4, &gate, sizeof(gate)),
           "kinetis_k22_write(device, SIM_SCGC4, &gate, sizeof(gate))");
}

static void write8(TestState* state, KinetisK22* device, uint32_t address, uint8_t value) {
    expect(state, kinetis_k22_write(device, address, &value, sizeof(value)),
           "kinetis_k22_write(device, address, &value, sizeof(value))");
}

static uint8_t read8(TestState* state, KinetisK22* device, uint32_t address) {
    uint8_t value = 0u;
    expect(state, kinetis_k22_read(device, address, &value, sizeof(value)),
           "kinetis_k22_read(device, address, &value, sizeof(value))");
    return value;
}

static void write16(TestState* state, KinetisK22* device, uint32_t address, uint16_t value) {
    expect(state, kinetis_k22_write(device, address, &value, sizeof(value)),
           "kinetis_k22_write(device, address, &value, sizeof(value))");
}

static void write32(TestState* state, KinetisK22* device, uint32_t address, uint32_t value) {
    expect(state, kinetis_k22_write(device, address, &value, sizeof(value)),
           "kinetis_k22_write(device, address, &value, sizeof(value))");
}

static void configure_dma(TestState* state, KinetisK22* device) {
    write32(state, device, DMA_TCD0, CMT_MSC);
    write16(state, device, DMA_TCD0 + 4u, 0u);
    write16(state, device, DMA_TCD0 + 6u, 0u);
    write32(state, device, DMA_TCD0 + 8u, 1u);
    write32(state, device, DMA_TCD0 + 0x10u, 0x20000080u);
    write16(state, device, DMA_TCD0 + 0x14u, 0u);
    write16(state, device, DMA_TCD0 + 0x16u, 1u);
    write16(state, device, DMA_TCD0 + 0x1cu, 1u << 3u);
    write16(state, device, DMA_TCD0 + 0x1eu, 1u);
    write8(state, device, DMAMUX_CHCFG0, 0x80u | 47u);
    write8(state, device, DMA_SERQ, 0u);
}

static void advance_bus(KinetisK22* device, uint32_t bus_cycles) {
    kinetis_k22_advance(device, k22_test_core_cycles_for_bus_cycles(device, bus_cycles));
}

static void configure_time(TestState* state, KinetisK22* device) {
    write8(state, device, CMT_CGH1, 2u);
    write8(state, device, CMT_CGL1, 2u);
    write8(state, device, CMT_CMD1, 0u);
    write8(state, device, CMT_CMD2, 3u);
    write8(state, device, CMT_CMD3, 0u);
    write8(state, device, CMT_CMD4, 1u);
    write8(state, device, CMT_PPS, 0u);
}

static void clear_eoc(TestState* state, KinetisK22* device) {
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) != 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) != 0u");
    (void)read8(state, device, CMT_CMD2);
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) == 0u");
}

static void expect_output(TestState* state, KinetisK22* device, bool driven, bool high) {
    bool actual_driven = !driven;
    bool actual_high = !high;
    expect(state, kinetis_k22_get_cmt_output(device, &actual_driven, &actual_high),
           "kinetis_k22_get_cmt_output(device, &actual_driven, &actual_high)");
    expect(state, actual_driven == driven, "actual_driven == driven");
    expect(state, actual_high == high, "actual_high == high");
}

static void test_direct_output(TestState* state, KinetisK22* device) {
    write8(state, device, CMT_OC, 0x20u);
    expect_output(state, device, true, true);
    write8(state, device, CMT_OC, 0xa0u);
    expect_output(state, device, true, false);
    write8(state, device, CMT_OC, 0xe0u);
    expect_output(state, device, true, true);
    write8(state, device, CMT_OC, 0x40u);
    expect_output(state, device, false, false);
}

static void test_time_mode(TestState* state, KinetisK22* device) {
    configure_time(state, device);
    write8(state, device, CMT_OC, 0x60u);
    write8(state, device, CMT_MSC, 3u);
    expect(state, device->cmt_running, "device->cmt_running");
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) != 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) != 0u");
    expect(state, (device->cpu->irq_level[45u / 32u] & (1u << (45u & 31u))) != 0u,
           "(device->cpu->irq_level[45u / 32u] & (1u << (45u & 31u))) != 0u");
    const uint8_t command = read8(state, device, CMT_CMD1);
    write8(state, device, CMT_CMD1, command);
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) != 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) != 0u");
    clear_eoc(state, device);
    expect_output(state, device, true, false);
    advance_bus(device, 1u);
    expect_output(state, device, true, false);
    advance_bus(device, 1u);
    expect_output(state, device, true, true);
    advance_bus(device, 2u);
    expect_output(state, device, true, false);
    advance_bus(device, 2u);
    expect_output(state, device, true, true);
    advance_bus(device, 33u);
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) == 0u");
    advance_bus(device, 1u);
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) != 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) != 0u");
    write8(state, device, CMT_CMD4, 1u);
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) == 0u");

    write8(state, device, CMT_MSC, 2u);
    expect(state, device->cmt_stop_pending, "device->cmt_stop_pending");
    advance_bus(device, 39u);
    expect(state, device->cmt_running, "device->cmt_running");
    write8(state, device, CMT_MSC, 3u);
    expect(state, !device->cmt_stop_pending, "!device->cmt_stop_pending");
    advance_bus(device, 1u);
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) != 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) != 0u");
    clear_eoc(state, device);
    write8(state, device, CMT_MSC, 2u);
    advance_bus(device, 40u);
    expect(state, !device->cmt_running, "!device->cmt_running");
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) == 0u");
}

static void test_baseband_and_extended_space(TestState* state, KinetisK22* device) {
    configure_time(state, device);
    write8(state, device, CMT_OC, 0x60u);
    write8(state, device, CMT_MSC, 0x0bu);
    clear_eoc(state, device);
    advance_bus(device, 2u);
    expect_output(state, device, true, true);
    advance_bus(device, 30u);
    expect_output(state, device, true, false);
    write8(state, device, CMT_MSC, 0x1bu);
    advance_bus(device, 8u);
    clear_eoc(state, device);
    expect(state, device->cmt_extended_space, "device->cmt_extended_space");
    expect_output(state, device, true, false);
    write8(state, device, CMT_MSC, 0x0au);
    advance_bus(device, 40u);
}

static void test_fsk_mode(TestState* state, KinetisK22* device) {
    write8(state, device, CMT_CGH1, 1u);
    write8(state, device, CMT_CGL1, 1u);
    write8(state, device, CMT_CGH2, 2u);
    write8(state, device, CMT_CGL2, 2u);
    write8(state, device, CMT_CMD1, 0u);
    write8(state, device, CMT_CMD2, 0u);
    write8(state, device, CMT_CMD3, 0u);
    write8(state, device, CMT_CMD4, 0u);
    write8(state, device, CMT_PPS, 0u);
    write8(state, device, CMT_MSC, 7u);
    clear_eoc(state, device);
    expect(state, device->cmt_period_ticks == 2u, "device->cmt_period_ticks == 2u");
    advance_bus(device, 1u);
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) == 0u");
    advance_bus(device, 1u);
    expect(state, device->cmt_fsk_secondary, "device->cmt_fsk_secondary");
    expect(state, device->cmt_period_ticks == 4u, "device->cmt_period_ticks == 4u");
    clear_eoc(state, device);
    advance_bus(device, 3u);
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) == 0u");
    advance_bus(device, 1u);
    expect(state, !device->cmt_fsk_secondary, "!device->cmt_fsk_secondary");
    clear_eoc(state, device);
    write8(state, device, CMT_MSC, 6u);
    advance_bus(device, 2u);
}

static void test_power_modes(TestState* state, KinetisK22* device) {
    configure_time(state, device);
    write8(state, device, CMT_MSC, 3u);
    clear_eoc(state, device);
    device->cpu->sleeping = true;
    device->cpu->scr = 0u;
    advance_bus(device, 40u);
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) != 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) != 0u");
    clear_eoc(state, device);

    device->cpu->scr = 4u;
    const uint64_t cycles = device->cmt_cycles;
    advance_bus(device, 40u);
    expect(state, device->cmt_cycles == cycles, "device->cmt_cycles == cycles");
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) == 0u");

    device->cpu->sleeping = false;
    advance_bus(device, 40u);
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) != 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) != 0u");
    clear_eoc(state, device);
    write8(state, device, CMT_MSC, 2u);
    advance_bus(device, 40u);
}

static void test_dividers(TestState* state, KinetisK22* device) {
    configure_time(state, device);
    write8(state, device, CMT_PPS, 3u);
    write8(state, device, CMT_OC, 0x60u);
    write8(state, device, CMT_MSC, 0x23u);
    clear_eoc(state, device);
    expect(state, device->cmt_period_ticks == 320u, "device->cmt_period_ticks == 320u");
    expect_output(state, device, true, false);
    advance_bus(device, 8u);
    expect_output(state, device, true, false);
    advance_bus(device, 1u);
    expect_output(state, device, true, true);
    write8(state, device, CMT_MSC, 0x22u);
    advance_bus(device, 311u);
}

static void test_dma_and_copy(TestState* state, KinetisK22* device) {
    configure_time(state, device);
    write8(state, device, CMT_DMA, 1u);
    write8(state, device, CMT_MSC, 3u);
    expect(state, !device->cmt_dma_pending, "!device->cmt_dma_pending");
    advance_bus(device, 1u);
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) != 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) != 0u");

    configure_dma(state, device);
    write8(state, device, CMT_MSC, 3u);
    expect(state, device->cmt_dma_pending, "device->cmt_dma_pending");
    expect(state, (device->cpu->irq_level[45u / 32u] & (1u << (45u & 31u))) == 0u,
           "(device->cpu->irq_level[45u / 32u] & (1u << (45u & 31u))) == 0u");
    advance_bus(device, 1u);
    expect(state, !device->cmt_dma_pending, "!device->cmt_dma_pending");
    expect(state, (read8(state, device, CMT_MSC) & 0x80u) == 0u,
           "(read8(state, device, CMT_MSC) & 0x80u) == 0u");

    KinetisK22* copy = make_device(state);
    expect(state, kinetis_k22_copy(copy, device), "kinetis_k22_copy(copy, device)");
    expect(state, copy->cmt_running == device->cmt_running,
           "copy->cmt_running == device->cmt_running");
    expect(state, copy->cmt_cycles == device->cmt_cycles, "copy->cmt_cycles == device->cmt_cycles");
    bool driven = false;
    bool high = false;
    expect(state, kinetis_k22_get_cmt_output(copy, &driven, &high),
           "kinetis_k22_get_cmt_output(copy, &driven, &high)");
    kinetis_k22_destroy(copy);

    reset_device(state, device);
    expect(state, !device->cmt_running, "!device->cmt_running");
    expect(state, !device->cmt_dma_pending, "!device->cmt_dma_pending");
    expect_output(state, device, false, true);
}

static void test_api_guards(TestState* state) {
    bool driven = false;
    bool high = false;
    expect(state, !kinetis_k22_get_cmt_output(NULL, &driven, &high),
           "!kinetis_k22_get_cmt_output(NULL, &driven, &high)");
    KinetisK22* device = kinetis_k22_create(kinetis_k22_default_configuration());
    expect(state, device != NULL, "device != NULL");
    expect(state, !kinetis_k22_get_cmt_output(device, &driven, &high),
           "!kinetis_k22_get_cmt_output(device, &driven, &high)");
    kinetis_k22_destroy(device);
}

int main(void) {
    TestState state = {0};
    test_api_guards(&state);

    KinetisK22* device = make_device(&state);
    test_direct_output(&state, device);
    test_time_mode(&state, device);
    reset_device(&state, device);
    test_baseband_and_extended_space(&state, device);
    reset_device(&state, device);
    test_fsk_mode(&state, device);
    reset_device(&state, device);
    test_power_modes(&state, device);
    reset_device(&state, device);
    test_dividers(&state, device);
    reset_device(&state, device);
    test_dma_and_copy(&state, device);
    kinetis_k22_destroy(device);
    return test_finish(&state);
}
