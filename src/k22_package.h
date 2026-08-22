#ifndef KINETIS_K22_SIM_K22_PACKAGE_H
#define KINETIS_K22_SIM_K22_PACKAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "k22_profile.h"

enum {
    K22_PACKAGE_PORT_COUNT = 5,
    K22_PACKAGE_PIN_COUNT = 32,
};

typedef enum {
    K22_PACKAGE_LH_64_LQFP,
    K22_PACKAGE_MP_64_MAPBGA,
    K22_PACKAGE_AH_64_WLCSP,
    K22_PACKAGE_LK_80_LQFP,
    K22_PACKAGE_AP_80_WLCSP,
    K22_PACKAGE_BP_80_WLCSP,
    K22_PACKAGE_FX_88_HVQFN,
    K22_PACKAGE_LL_100_LQFP,
    K22_PACKAGE_DC_121_XFBGA,
    K22_PACKAGE_MC_121_MAPBGA,
    K22_PACKAGE_LQ_144_LQFP,
    K22_PACKAGE_MD_144_MAPBGA,
    K22_PACKAGE_AK_49_WLCSP,
    K22_PACKAGE_COUNT,
} K22PackageId;

typedef struct {
    K22PackageId id;
    const char* code;
    const char* name;
    uint16_t terminal_count;
} K22Package;

typedef struct K22PackageSelection K22PackageSelection;

const K22Package* k22_package_get(K22PackageId id);
const K22Package* k22_package_find(const char* code);
const K22PackageSelection* k22_package_select(const K22Profile* profile, K22PackageId id);
const K22PackageSelection* k22_package_default(const K22Profile* profile);
K22ProfileId k22_package_selection_profile(const K22PackageSelection* selection);
const K22Package* k22_package_selection_package(const K22PackageSelection* selection);
uint32_t k22_package_port_pin_mask(const K22PackageSelection* selection, uint8_t port);
bool k22_package_pin_exists(const K22PackageSelection* selection, uint8_t port, uint8_t pin);
bool k22_package_has_peripheral(const K22PackageSelection* selection, K22PeripheralId peripheral);

#endif
