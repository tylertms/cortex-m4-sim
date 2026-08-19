#ifndef CORTEX_M4_SIM_KINETIS_K22_H
#define CORTEX_M4_SIM_KINETIS_K22_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cortex_m4_sim/cortex_m4.h"

typedef struct KinetisK22 KinetisK22;

typedef enum {
    KINETIS_K22_PROFILE_MK22F12810,
    KINETIS_K22_PROFILE_MK22FN12812,
    KINETIS_K22_PROFILE_MK22FN25612,
    KINETIS_K22_PROFILE_MK22FN51212,
    KINETIS_K22_PROFILE_MK22FN1M012,
    KINETIS_K22_PROFILE_MK22FX51212,
    KINETIS_K22_PROFILE_COUNT,
} KinetisK22Profile;

typedef enum {
    KINETIS_K22_PACKAGE_DEFAULT = -1,
    KINETIS_K22_PACKAGE_LH_64_LQFP,
    KINETIS_K22_PACKAGE_MP_64_MAPBGA,
    KINETIS_K22_PACKAGE_AH_64_WLCSP,
    KINETIS_K22_PACKAGE_LK_80_LQFP,
    KINETIS_K22_PACKAGE_AP_80_WLCSP,
    KINETIS_K22_PACKAGE_BP_80_WLCSP,
    KINETIS_K22_PACKAGE_FX_88_HVQFN,
    KINETIS_K22_PACKAGE_LL_100_LQFP,
    KINETIS_K22_PACKAGE_DC_121_XFBGA,
    KINETIS_K22_PACKAGE_MC_121_MAPBGA,
    KINETIS_K22_PACKAGE_LQ_144_LQFP,
    KINETIS_K22_PACKAGE_MD_144_MAPBGA,
    KINETIS_K22_PACKAGE_COUNT,
} KinetisK22Package;

typedef struct {
    KinetisK22Profile profile;
    KinetisK22Package package;
    size_t flash_size;
    size_t sram_size;
    uint32_t vector_table_address;
    uint32_t external_oscillator_hz;
    uint32_t rtc_oscillator_hz;
} KinetisK22Configuration;

typedef enum {
    KINETIS_K22_SERIAL_LPUART0,
    KINETIS_K22_SERIAL_SPI0,
    KINETIS_K22_SERIAL_SPI1,
    KINETIS_K22_SERIAL_SPI2,
    KINETIS_K22_SERIAL_I2C0,
    KINETIS_K22_SERIAL_I2C1,
    KINETIS_K22_SERIAL_I2C2,
    KINETIS_K22_SERIAL_UART0,
    KINETIS_K22_SERIAL_UART1,
    KINETIS_K22_SERIAL_UART2,
    KINETIS_K22_SERIAL_UART3,
    KINETIS_K22_SERIAL_UART4,
    KINETIS_K22_SERIAL_UART5,
    KINETIS_K22_SERIAL_ENDPOINT_COUNT,
} KinetisK22SerialEndpoint;

typedef struct {
    uint16_t data;
    uint8_t chip_selects;
    uint8_t clock_and_transfer_attributes;
    bool continuous_chip_select;
    bool end_of_queue;
} KinetisK22SpiTransfer;

typedef struct {
    uint32_t identifier;
    uint8_t length;
    uint8_t data[8];
    bool extended;
    bool remote;
} KinetisK22CanFrame;

typedef enum {
    KINETIS_K22_EVENT_IRQ,
    KINETIS_K22_EVENT_DMA,
    KINETIS_K22_EVENT_GPIO_OUTPUT,
    KINETIS_K22_EVENT_USB_TOKEN,
    KINETIS_K22_EVENT_CAN_TRANSMIT,
    KINETIS_K22_EVENT_I2S_TRANSMIT,
    KINETIS_K22_EVENT_FLEXBUS_TRANSFER,
    KINETIS_K22_EVENT_ACCESS_ERROR,
} KinetisK22EventType;

typedef struct {
    KinetisK22EventType type;
    uint32_t source;
    uint32_t value;
    uint32_t auxiliary;
    uint8_t data[8];
    uint8_t length;
    bool extended;
    bool remote;
} KinetisK22Event;

typedef enum {
    KINETIS_K22_I2C_START,
    KINETIS_K22_I2C_REPEATED_START,
    KINETIS_K22_I2C_WRITE,
    KINETIS_K22_I2C_READ,
    KINETIS_K22_I2C_STOP,
} KinetisK22I2cTransferType;

typedef enum {
    KINETIS_K22_USB_CHARGER_NONE,
    KINETIS_K22_USB_CHARGER_STANDARD_HOST,
    KINETIS_K22_USB_CHARGER_CHARGING_PORT,
    KINETIS_K22_USB_CHARGER_DEDICATED,
} KinetisK22UsbCharger;

typedef struct {
    KinetisK22I2cTransferType type;
    uint8_t value;
} KinetisK22I2cTransfer;

KinetisK22Configuration kinetis_k22_default_configuration(void);
KinetisK22* kinetis_k22_create(KinetisK22Configuration configuration);
void kinetis_k22_destroy(KinetisK22* device);
CortexM4* kinetis_k22_cpu(KinetisK22* device);
const CortexM4* kinetis_k22_cpu_const(const KinetisK22* device);
bool kinetis_k22_reset(KinetisK22* device);
bool kinetis_k22_load(KinetisK22* device, uint32_t address, const void* data, size_t size);
bool kinetis_k22_read(const KinetisK22* device, uint32_t address, void* data, size_t size);
bool kinetis_k22_write(KinetisK22* device, uint32_t address, const void* data, size_t size);
bool kinetis_k22_copy(KinetisK22* destination, const KinetisK22* source);
void kinetis_k22_advance(KinetisK22* device, uint32_t cycles);
void kinetis_k22_watchdog_advance(KinetisK22* device, uint32_t ticks);
uint32_t kinetis_k22_core_clock_hz(const KinetisK22* device);
uint32_t kinetis_k22_bus_clock_hz(const KinetisK22* device);
bool kinetis_k22_next_event(KinetisK22* device, KinetisK22Event* event);
bool kinetis_k22_set_adc_channel(KinetisK22* device, uint8_t instance, uint8_t channel,
                                 uint16_t value);
void kinetis_k22_set_adc0_channel(KinetisK22* device, uint8_t channel, uint16_t value);
bool kinetis_k22_set_cmp_input(KinetisK22* device, uint8_t instance, uint8_t input,
                               uint8_t value);
bool kinetis_k22_set_lptmr_input(KinetisK22* device, uint8_t input, bool high);
bool kinetis_k22_trigger_low_voltage_warning(KinetisK22* device);
bool kinetis_k22_trigger_low_voltage_detect(KinetisK22* device);
bool kinetis_k22_set_llwu_pin(KinetisK22* device, uint8_t pin, bool high);
bool kinetis_k22_trigger_llwu_module(KinetisK22* device, uint8_t module);
bool kinetis_k22_set_ewm_input(KinetisK22* device, bool high);
bool kinetis_k22_ewm_output(const KinetisK22* device);
bool kinetis_k22_get_cmt_output(const KinetisK22* device, bool* driven, bool* high);
bool kinetis_k22_set_ftm_input(KinetisK22* device, uint8_t instance, uint8_t channel,
                               bool high);
bool kinetis_k22_set_ftm_fault(KinetisK22* device, uint8_t instance, uint8_t input,
                               bool high);
bool kinetis_k22_trigger_ftm_hardware(KinetisK22* device, uint8_t instance,
                                      uint8_t trigger);
bool kinetis_k22_get_ftm_output(const KinetisK22* device, uint8_t instance, uint8_t channel,
                                bool* high);
bool kinetis_k22_get_dac_output(const KinetisK22* device, uint8_t instance,
                                uint16_t* value);
void kinetis_k22_rng_seed(KinetisK22* device, uint32_t seed);
void kinetis_k22_gpio_drive(KinetisK22* device, uint8_t port, uint8_t pin, bool high);
void kinetis_k22_gpio_release(KinetisK22* device, uint8_t port, uint8_t pin);
bool kinetis_k22_serial_receive(KinetisK22* device, KinetisK22SerialEndpoint endpoint,
                                uint16_t value, uint8_t status);
bool kinetis_k22_serial_transmit(KinetisK22* device, KinetisK22SerialEndpoint endpoint,
                                 uint16_t* value);
bool kinetis_k22_spi_transfer(KinetisK22* device, KinetisK22SerialEndpoint endpoint,
                              KinetisK22SpiTransfer* transfer);
bool kinetis_k22_i2c_transfer(KinetisK22* device, KinetisK22SerialEndpoint endpoint,
                              KinetisK22I2cTransfer* transfer);
bool kinetis_k22_i2c_acknowledge(KinetisK22* device, KinetisK22SerialEndpoint endpoint,
                                 bool acknowledge);
bool kinetis_k22_i2c_lose_arbitration(KinetisK22* device,
                                      KinetisK22SerialEndpoint endpoint);
bool kinetis_k22_i2c_receive(KinetisK22* device, KinetisK22SerialEndpoint endpoint,
                             uint8_t value);
bool kinetis_k22_usb_token(KinetisK22* device, uint8_t endpoint, uint8_t token,
                           bool transmit);
bool kinetis_k22_can_receive(KinetisK22* device, const KinetisK22CanFrame* frame);
bool kinetis_k22_i2s_receive(KinetisK22* device, uint32_t sample);
bool kinetis_k22_i2s_transmit(KinetisK22* device, uint32_t* sample);
bool kinetis_k22_sdhc_insert(KinetisK22* device, const void* data, size_t size,
                             bool write_protected);
void kinetis_k22_sdhc_eject(KinetisK22* device);
bool kinetis_k22_sdhc_read_card(const KinetisK22* device, size_t offset, void* data,
                                size_t size);
bool kinetis_k22_flexbus_attach(KinetisK22* device, uint32_t address, const void* data,
                                size_t size, bool read_only);
void kinetis_k22_flexbus_detach(KinetisK22* device);
bool kinetis_k22_flexbus_read(const KinetisK22* device, size_t offset, void* data,
                              size_t size);
bool kinetis_k22_set_usb_charger(KinetisK22* device, KinetisK22UsbCharger charger);
bool kinetis_k22_uart1_receive(KinetisK22* device, uint8_t value, uint8_t status);
bool kinetis_k22_uart1_transmit(KinetisK22* device, uint8_t* value);
bool kinetis_k22_spi0_receive(KinetisK22* device, uint16_t value);
bool kinetis_k22_spi0_transmit(KinetisK22* device, uint16_t* value);
bool kinetis_k22_i2c0_transfer(KinetisK22* device, KinetisK22I2cTransfer* transfer);
void kinetis_k22_i2c0_acknowledge(KinetisK22* device, bool acknowledge);
bool kinetis_k22_i2c0_lose_arbitration(KinetisK22* device);
bool kinetis_k22_i2c0_receive(KinetisK22* device, uint8_t value);

#endif
