#include "k22_package.h"

#include <stddef.h>
#include <string.h>

#define MASKS(a, b, c, d, e) {a, b, c, d, e}
#define SELECTION(profile_id, package_id, a, b, c, d, e)                                           \
    {profile_id, package_id, MASKS(a, b, c, d, e)}

struct K22PackageSelection {
    K22ProfileId profile;
    K22PackageId package;
    uint32_t port_pin_mask[K22_PACKAGE_PORT_COUNT];
};

static const K22Package packages[K22_PACKAGE_COUNT] = {
    {K22_PACKAGE_LH_64_LQFP, "LH", "64 LQFP", 64},
    {K22_PACKAGE_MP_64_MAPBGA, "MP", "64 MAPBGA", 64},
    {K22_PACKAGE_AH_64_WLCSP, "AH", "64 WLCSP", 64},
    {K22_PACKAGE_LK_80_LQFP, "LK", "80 LQFP", 80},
    {K22_PACKAGE_AP_80_WLCSP, "AP", "80 WLCSP 0.564 mm", 80},
    {K22_PACKAGE_BP_80_WLCSP, "BP", "80 WLCSP 0.321 mm", 80},
    {K22_PACKAGE_FX_88_HVQFN, "FX", "88 HVQFN", 88},
    {K22_PACKAGE_LL_100_LQFP, "LL", "100 LQFP", 100},
    {K22_PACKAGE_DC_121_XFBGA, "DC", "121 XFBGA", 121},
    {K22_PACKAGE_MC_121_MAPBGA, "MC", "121 MAPBGA", 121},
    {K22_PACKAGE_LQ_144_LQFP, "LQ", "144 LQFP", 144},
    {K22_PACKAGE_MD_144_MAPBGA, "MD", "144 MAPBGA", 144},
};

static const K22PackageSelection selections[] = {
    SELECTION(K22_PROFILE_MK22F12810, K22_PACKAGE_LH_64_LQFP, 0x000c303fu, 0x000f000fu, 0x00000fffu,
              0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22F12810, K22_PACKAGE_MP_64_MAPBGA, 0x000c303fu, 0x000f000fu,
              0x00000fffu, 0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22F12810, K22_PACKAGE_LL_100_LQFP, 0x000ff03fu, 0x00ff0e0fu,
              0x0007ffffu, 0x000000ffu, 0x07000000u),
    SELECTION(K22_PROFILE_MK22F12810, K22_PACKAGE_DC_121_XFBGA, 0x000ff03fu, 0x00ff0f0fu,
              0x0007ffffu, 0x000000ffu, 0x07000000u),
    SELECTION(K22_PROFILE_MK22FN12812, K22_PACKAGE_AH_64_WLCSP, 0x000c303fu, 0x000f000fu,
              0x00000fffu, 0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22FN25612, K22_PACKAGE_LH_64_LQFP, 0x000c303fu, 0x000f000fu,
              0x00000fffu, 0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22FN25612, K22_PACKAGE_MP_64_MAPBGA, 0x000c303fu, 0x000f000fu,
              0x00000fffu, 0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22FN25612, K22_PACKAGE_AH_64_WLCSP, 0x000c303fu, 0x000f000fu,
              0x00000fffu, 0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22FN25612, K22_PACKAGE_AP_80_WLCSP, 0x000ff03fu, 0x000f0c0fu,
              0x00030fffu, 0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22FN25612, K22_PACKAGE_LL_100_LQFP, 0x000ff03fu, 0x00ff0e0fu,
              0x0007ffffu, 0x000000ffu, 0x07000000u),
    SELECTION(K22_PROFILE_MK22FN25612, K22_PACKAGE_DC_121_XFBGA, 0x000ff03fu, 0x00ff0fcfu,
              0x000fffffu, 0x000000ffu, 0x07000000u),
    SELECTION(K22_PROFILE_MK22FN51212, K22_PACKAGE_LH_64_LQFP, 0x000c303fu, 0x000f000fu,
              0x00000fffu, 0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22FN51212, K22_PACKAGE_MP_64_MAPBGA, 0x000c303fu, 0x000f000fu,
              0x00000fffu, 0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22FN51212, K22_PACKAGE_AP_80_WLCSP, 0x000ff03fu, 0x000f0c0fu,
              0x00030fffu, 0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22FN51212, K22_PACKAGE_BP_80_WLCSP, 0x000ff03fu, 0x000f0c0fu,
              0x00030fffu, 0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22FN51212, K22_PACKAGE_FX_88_HVQFN, 0x000ff03fu, 0x000f0fcfu,
              0x000ff1ffu, 0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22FN51212, K22_PACKAGE_LL_100_LQFP, 0x000ff03fu, 0x00ff0e0fu,
              0x0007ffffu, 0x000000ffu, 0x07000000u),
    SELECTION(K22_PROFILE_MK22FN51212, K22_PACKAGE_DC_121_XFBGA, 0x200ffc3fu, 0x00ff0fcfu,
              0x000fffffu, 0x0000ffffu, 0x07000000u),
    SELECTION(K22_PROFILE_MK22FN1M012, K22_PACKAGE_LH_64_LQFP, 0x000c303fu, 0x000f000fu,
              0x00000fffu, 0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22FN1M012, K22_PACKAGE_LK_80_LQFP, 0x000ff03fu, 0x000f0c0fu,
              0x00030fffu, 0x000000ffu, 0x00000038u),
    SELECTION(K22_PROFILE_MK22FN1M012, K22_PACKAGE_LL_100_LQFP, 0x000ff03fu, 0x00ff0e0fu,
              0x0007ffffu, 0x000000ffu, 0x07000078u),
    SELECTION(K22_PROFILE_MK22FN1M012, K22_PACKAGE_MC_121_MAPBGA, 0x200ffc3fu, 0x00ff3fcfu,
              0x000fffffu, 0x0000ffffu, 0x07000078u),
    SELECTION(K22_PROFILE_MK22FN1M012, K22_PACKAGE_LQ_144_LQFP, 0x3f0fffffu, 0x00ff0fffu,
              0x000fffffu, 0x0000ffffu, 0x1f001ff8u),
    SELECTION(K22_PROFILE_MK22FN1M012, K22_PACKAGE_MD_144_MAPBGA, 0x3f0fffffu, 0x00ff0fffu,
              0x000fffffu, 0x0000ffffu, 0x1f001ff8u),
    SELECTION(K22_PROFILE_MK22FX51212, K22_PACKAGE_LH_64_LQFP, 0x000c303fu, 0x000f000fu,
              0x00000fffu, 0x000000ffu, 0x00000000u),
    SELECTION(K22_PROFILE_MK22FX51212, K22_PACKAGE_LK_80_LQFP, 0x000ff03fu, 0x000f0c0fu,
              0x00030fffu, 0x000000ffu, 0x00000038u),
    SELECTION(K22_PROFILE_MK22FX51212, K22_PACKAGE_LL_100_LQFP, 0x000ff03fu, 0x00ff0e0fu,
              0x0007ffffu, 0x000000ffu, 0x07000078u),
    SELECTION(K22_PROFILE_MK22FX51212, K22_PACKAGE_MC_121_MAPBGA, 0x200ffc3fu, 0x00ff3fcfu,
              0x000fffffu, 0x0000ffffu, 0x07000078u),
    SELECTION(K22_PROFILE_MK22FX51212, K22_PACKAGE_LQ_144_LQFP, 0x3f0fffffu, 0x00ff0fffu,
              0x000fffffu, 0x0000ffffu, 0x1f001ff8u),
    SELECTION(K22_PROFILE_MK22FX51212, K22_PACKAGE_MD_144_MAPBGA, 0x3f0fffffu, 0x00ff0fffu,
              0x000fffffu, 0x0000ffffu, 0x1f001ff8u),
};

static size_t selection_count(void) { return sizeof(selections) / sizeof(selections[0]); }

const K22Package* k22_package_get(K22PackageId id) {
    if ((unsigned)id >= K22_PACKAGE_COUNT)
        return NULL;
    return &packages[id];
}

const K22Package* k22_package_find(const char* code) {
    if (code == NULL)
        return NULL;
    for (size_t index = 0; index < K22_PACKAGE_COUNT; index++) {
        if (strcmp(packages[index].code, code) == 0)
            return &packages[index];
    }
    return NULL;
}

const K22PackageSelection* k22_package_select(const K22Profile* profile, K22PackageId id) {
    if (profile == NULL || (unsigned)profile->id >= K22_PROFILE_COUNT ||
        (unsigned)id >= K22_PACKAGE_COUNT)
        return NULL;
    for (size_t index = 0; index < selection_count(); index++) {
        if (selections[index].profile == profile->id && selections[index].package == id)
            return &selections[index];
    }
    return NULL;
}

const K22PackageSelection* k22_package_default(const K22Profile* profile) {
    if (profile == NULL)
        return NULL;
    if (profile->id == K22_PROFILE_MK22FN12812)
        return k22_package_select(profile, K22_PACKAGE_AH_64_WLCSP);
    return k22_package_select(profile, K22_PACKAGE_LH_64_LQFP);
}

K22ProfileId k22_package_selection_profile(const K22PackageSelection* selected) {
    return selected == NULL ? K22_PROFILE_COUNT : selected->profile;
}

const K22Package* k22_package_selection_package(const K22PackageSelection* selected) {
    return selected == NULL ? NULL : k22_package_get(selected->package);
}

uint32_t k22_package_port_pin_mask(const K22PackageSelection* selected, uint8_t port) {
    if (selected == NULL || port >= K22_PACKAGE_PORT_COUNT)
        return 0;
    return selected->port_pin_mask[port];
}

bool k22_package_pin_exists(const K22PackageSelection* selected, uint8_t port, uint8_t pin) {
    return selected != NULL && port < K22_PACKAGE_PORT_COUNT && pin < K22_PACKAGE_PIN_COUNT &&
           (selected->port_pin_mask[port] & (UINT32_C(1) << pin)) != 0;
}

bool k22_package_has_peripheral(const K22PackageSelection* selected, K22PeripheralId peripheral) {
    if (selected == NULL || (unsigned)peripheral >= K22_PERIPHERAL_COUNT)
        return false;
    const K22Profile* profile = k22_profile_get(selected->profile);
    if (!k22_profile_has_peripheral(profile, peripheral))
        return false;
    if (selected->profile == K22_PROFILE_MK22FN51212 && peripheral == K22_PERIPHERAL_DAC1)
        return selected->package != K22_PACKAGE_FX_88_HVQFN;
    if (selected->profile == K22_PROFILE_MK22FN51212 && peripheral == K22_PERIPHERAL_FB)
        return selected->package != K22_PACKAGE_FX_88_HVQFN;
    if (selected->profile == K22_PROFILE_MK22FN1M012 ||
        selected->profile == K22_PROFILE_MK22FX51212) {
        const bool at_least_80 = selected->package != K22_PACKAGE_LH_64_LQFP;
        const bool at_least_100 = selected->package == K22_PACKAGE_LL_100_LQFP ||
                                  selected->package == K22_PACKAGE_MC_121_MAPBGA ||
                                  selected->package == K22_PACKAGE_LQ_144_LQFP ||
                                  selected->package == K22_PACKAGE_MD_144_MAPBGA;
        const bool at_least_121 = selected->package == K22_PACKAGE_MC_121_MAPBGA ||
                                  selected->package == K22_PACKAGE_LQ_144_LQFP ||
                                  selected->package == K22_PACKAGE_MD_144_MAPBGA;
        if (peripheral == K22_PERIPHERAL_SPI1 || peripheral == K22_PERIPHERAL_UART3 ||
            peripheral == K22_PERIPHERAL_SDHC)
            return at_least_80;
        if (peripheral == K22_PERIPHERAL_SPI2 || peripheral == K22_PERIPHERAL_UART4)
            return at_least_100;
        if (peripheral == K22_PERIPHERAL_UART5 || peripheral == K22_PERIPHERAL_DAC1)
            return at_least_121;
    }
    return true;
}
