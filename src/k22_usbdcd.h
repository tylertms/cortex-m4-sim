#ifndef CORTEX_M4_K22_USBDCD_H
#define CORTEX_M4_K22_USBDCD_H

#include <stdbool.h>
#include <stdint.h>

#include "kinetis_k22.h"

typedef enum {
    K22_USBDCD_IDLE,
    K22_USBDCD_DATA_CONTACT,
    K22_USBDCD_PRIMARY_DELAY,
    K22_USBDCD_PRIMARY_DETECTION,
    K22_USBDCD_WAIT_PULLUP,
    K22_USBDCD_SECONDARY_DELAY,
    K22_USBDCD_SECONDARY_DETECTION,
} K22UsbDcdPhase;

typedef struct {
    uint32_t control;
    uint32_t clock;
    uint32_t status;
    uint32_t timer0;
    uint32_t timer1;
    uint32_t timer2;
    uint64_t clock_cycles;
    uint16_t phase_elapsed;
    K22UsbDcdPhase phase;
    KinetisK22UsbCharger charger;
    bool pullup;
} K22UsbDcd;

void k22_usbdcd_reset(K22UsbDcd* usbdcd);
bool k22_usbdcd_copy(K22UsbDcd* destination, const K22UsbDcd* source);
bool k22_usbdcd_read(K22UsbDcd* usbdcd, uint32_t address, uint8_t size, uint32_t* value);
bool k22_usbdcd_write(K22UsbDcd* usbdcd, uint32_t address, uint8_t size, uint32_t value);
void k22_usbdcd_advance(K22UsbDcd* usbdcd, uint64_t cycles);
bool k22_usbdcd_set_charger(K22UsbDcd* usbdcd, KinetisK22UsbCharger charger);
bool k22_usbdcd_set_pullup(K22UsbDcd* usbdcd, bool enabled);
bool k22_usbdcd_irq(const K22UsbDcd* usbdcd);

#endif
