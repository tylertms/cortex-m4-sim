#include "k22_package.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "test.h"

#define MASKS(a, b, c, d, e) {a, b, c, d, e}
#define EXPECTED(profile_id, package_id, a, b, c, d, e, dac1_value, flexbus_value)                 \
    {profile_id, package_id, MASKS(a, b, c, d, e), dac1_value, flexbus_value}

typedef struct {
    K22PackageId id;
    const char* code;
    const char* name;
    uint16_t terminal_count;
} ExpectedPackage;

typedef struct {
    K22ProfileId profile;
    K22PackageId package;
    uint32_t port_pin_mask[K22_PACKAGE_PORT_COUNT];
    bool dac1;
    bool flexbus;
} ExpectedSelection;

static const ExpectedPackage expected_packages[] = {
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
    {K22_PACKAGE_AK_49_WLCSP, "AK", "49 WLCSP", 49},
};

static const ExpectedSelection expected_selections[] = {
    EXPECTED(K22_PROFILE_MK22F12810, K22_PACKAGE_AK_49_WLCSP, 0x000c001fu, 0x0003000fu, 0x000000ffu,
             0x000000ffu, 0x0000003fu, false, false),
    EXPECTED(K22_PROFILE_MK22F12810, K22_PACKAGE_LH_64_LQFP, 0x000c303fu, 0x000f000fu, 0x00000fffu,
             0x000000ffu, 0x00000000u, false, false),
    EXPECTED(K22_PROFILE_MK22F12810, K22_PACKAGE_MP_64_MAPBGA, 0x000c303fu, 0x000f000fu,
             0x00000fffu, 0x000000ffu, 0x00000000u, false, false),
    EXPECTED(K22_PROFILE_MK22F12810, K22_PACKAGE_LL_100_LQFP, 0x000ff03fu, 0x00ff0e0fu, 0x0007ffffu,
             0x000000ffu, 0x07000000u, false, false),
    EXPECTED(K22_PROFILE_MK22F12810, K22_PACKAGE_DC_121_XFBGA, 0x000ff03fu, 0x00ff0f0fu,
             0x0007ffffu, 0x000000ffu, 0x07000000u, false, false),
    EXPECTED(K22_PROFILE_MK22FN12812, K22_PACKAGE_AH_64_WLCSP, 0x000c303fu, 0x000f000fu,
             0x00000fffu, 0x000000ffu, 0x00000000u, false, false),
    EXPECTED(K22_PROFILE_MK22FN25612, K22_PACKAGE_LH_64_LQFP, 0x000c303fu, 0x000f000fu, 0x00000fffu,
             0x000000ffu, 0x00000000u, false, false),
    EXPECTED(K22_PROFILE_MK22FN25612, K22_PACKAGE_MP_64_MAPBGA, 0x000c303fu, 0x000f000fu,
             0x00000fffu, 0x000000ffu, 0x00000000u, false, false),
    EXPECTED(K22_PROFILE_MK22FN25612, K22_PACKAGE_AH_64_WLCSP, 0x000c303fu, 0x000f000fu,
             0x00000fffu, 0x000000ffu, 0x00000000u, false, false),
    EXPECTED(K22_PROFILE_MK22FN25612, K22_PACKAGE_AP_80_WLCSP, 0x000ff03fu, 0x000f0c0fu,
             0x00030fffu, 0x000000ffu, 0x00000000u, false, false),
    EXPECTED(K22_PROFILE_MK22FN25612, K22_PACKAGE_LL_100_LQFP, 0x000ff03fu, 0x00ff0e0fu,
             0x0007ffffu, 0x000000ffu, 0x07000000u, false, false),
    EXPECTED(K22_PROFILE_MK22FN25612, K22_PACKAGE_DC_121_XFBGA, 0x000ff03fu, 0x00ff0fcfu,
             0x000fffffu, 0x000000ffu, 0x07000000u, false, false),
    EXPECTED(K22_PROFILE_MK22FN51212, K22_PACKAGE_LH_64_LQFP, 0x000c303fu, 0x000f000fu, 0x00000fffu,
             0x000000ffu, 0x00000000u, true, true),
    EXPECTED(K22_PROFILE_MK22FN51212, K22_PACKAGE_MP_64_MAPBGA, 0x000c303fu, 0x000f000fu,
             0x00000fffu, 0x000000ffu, 0x00000000u, true, true),
    EXPECTED(K22_PROFILE_MK22FN51212, K22_PACKAGE_AP_80_WLCSP, 0x000ff03fu, 0x000f0c0fu,
             0x00030fffu, 0x000000ffu, 0x00000000u, true, true),
    EXPECTED(K22_PROFILE_MK22FN51212, K22_PACKAGE_BP_80_WLCSP, 0x000ff03fu, 0x000f0c0fu,
             0x00030fffu, 0x000000ffu, 0x00000000u, true, true),
    EXPECTED(K22_PROFILE_MK22FN51212, K22_PACKAGE_FX_88_HVQFN, 0x000ff03fu, 0x000f0fcfu,
             0x000ff1ffu, 0x000000ffu, 0x00000000u, false, false),
    EXPECTED(K22_PROFILE_MK22FN51212, K22_PACKAGE_LL_100_LQFP, 0x000ff03fu, 0x00ff0e0fu,
             0x0007ffffu, 0x000000ffu, 0x07000000u, true, true),
    EXPECTED(K22_PROFILE_MK22FN51212, K22_PACKAGE_DC_121_XFBGA, 0x200ffc3fu, 0x00ff0fcfu,
             0x000fffffu, 0x0000ffffu, 0x07000000u, true, true),
    EXPECTED(K22_PROFILE_MK22FN1M012, K22_PACKAGE_LH_64_LQFP, 0x000c303fu, 0x000f000fu, 0x00000fffu,
             0x000000ffu, 0x00000000u, false, true),
    EXPECTED(K22_PROFILE_MK22FN1M012, K22_PACKAGE_LK_80_LQFP, 0x000ff03fu, 0x000f0c0fu, 0x00030fffu,
             0x000000ffu, 0x00000038u, false, true),
    EXPECTED(K22_PROFILE_MK22FN1M012, K22_PACKAGE_LL_100_LQFP, 0x000ff03fu, 0x00ff0e0fu,
             0x0007ffffu, 0x000000ffu, 0x07000078u, false, true),
    EXPECTED(K22_PROFILE_MK22FN1M012, K22_PACKAGE_MC_121_MAPBGA, 0x200ffc3fu, 0x00ff3fcfu,
             0x000fffffu, 0x0000ffffu, 0x07000078u, false, true),
    EXPECTED(K22_PROFILE_MK22FN1M012, K22_PACKAGE_LQ_144_LQFP, 0x3f0fffffu, 0x00ff0fffu,
             0x000fffffu, 0x0000ffffu, 0x1f001ff8u, false, true),
    EXPECTED(K22_PROFILE_MK22FN1M012, K22_PACKAGE_MD_144_MAPBGA, 0x3f0fffffu, 0x00ff0fffu,
             0x000fffffu, 0x0000ffffu, 0x1f001ff8u, false, true),
    EXPECTED(K22_PROFILE_MK22FX51212, K22_PACKAGE_LH_64_LQFP, 0x000c303fu, 0x000f000fu, 0x00000fffu,
             0x000000ffu, 0x00000000u, false, true),
    EXPECTED(K22_PROFILE_MK22FX51212, K22_PACKAGE_LK_80_LQFP, 0x000ff03fu, 0x000f0c0fu, 0x00030fffu,
             0x000000ffu, 0x00000038u, false, true),
    EXPECTED(K22_PROFILE_MK22FX51212, K22_PACKAGE_LL_100_LQFP, 0x000ff03fu, 0x00ff0e0fu,
             0x0007ffffu, 0x000000ffu, 0x07000078u, false, true),
    EXPECTED(K22_PROFILE_MK22FX51212, K22_PACKAGE_MC_121_MAPBGA, 0x200ffc3fu, 0x00ff3fcfu,
             0x000fffffu, 0x0000ffffu, 0x07000078u, false, true),
    EXPECTED(K22_PROFILE_MK22FX51212, K22_PACKAGE_LQ_144_LQFP, 0x3f0fffffu, 0x00ff0fffu,
             0x000fffffu, 0x0000ffffu, 0x1f001ff8u, false, true),
    EXPECTED(K22_PROFILE_MK22FX51212, K22_PACKAGE_MD_144_MAPBGA, 0x3f0fffffu, 0x00ff0fffu,
             0x000fffffu, 0x0000ffffu, 0x1f001ff8u, false, true),
};

static size_t expected_package_count(void) {
    return sizeof(expected_packages) / sizeof(expected_packages[0]);
}

static size_t expected_selection_count(void) {
    return sizeof(expected_selections) / sizeof(expected_selections[0]);
}

static const ExpectedSelection* expected_selection(K22ProfileId profile, K22PackageId package) {
    for (size_t index = 0; index < expected_selection_count(); index++) {
        if (expected_selections[index].profile == profile &&
            expected_selections[index].package == package)
            return &expected_selections[index];
    }
    return NULL;
}

static void expect_package_metadata(TestState* state) {
    expect(state, expected_package_count() == K22_PACKAGE_COUNT,
           "expected_package_count() == K22_PACKAGE_COUNT");
    for (size_t index = 0; index < expected_package_count(); index++) {
        const ExpectedPackage* expected = &expected_packages[index];
        const K22Package* package = k22_package_get(expected->id);
        expect(state, package != NULL, "package != NULL");
        expect(state, package->id == expected->id, "package->id == expected->id");
        expect(state, strcmp(package->code, expected->code) == 0,
               "strcmp(package->code, expected->code) == 0");
        expect(state, strcmp(package->name, expected->name) == 0,
               "strcmp(package->name, expected->name) == 0");
        expect(state, package->terminal_count == expected->terminal_count,
               "package->terminal_count == expected->terminal_count");
        expect(state, k22_package_find(expected->code) == package,
               "k22_package_find(expected->code) == package");
        KinetisK22Package public_package = KINETIS_K22_PACKAGE_DEFAULT;
        expect(state, kinetis_k22_package_from_code(expected->code, &public_package),
               "kinetis_k22_package_from_code(expected->code, &public_package)");
        expect(state, public_package == (KinetisK22Package)expected->id,
               "public_package == expected->id");
        expect(state, strcmp(kinetis_k22_package_code(public_package), expected->code) == 0,
               "strcmp(kinetis_k22_package_code(public_package), expected->code) == 0");
    }
}

static void expect_selection_data(TestState* state, const ExpectedSelection* expected,
                                  const K22PackageSelection* selected) {
    expect(state, selected != NULL, "selected != NULL");
    expect(state, k22_package_selection_profile(selected) == expected->profile,
           "k22_package_selection_profile(selected) == expected->profile");
    expect(state, k22_package_selection_package(selected)->id == expected->package,
           "k22_package_selection_package(selected)->id == expected->package");
    for (uint8_t port = 0; port < K22_PACKAGE_PORT_COUNT; port++) {
        expect(state, k22_package_port_pin_mask(selected, port) == expected->port_pin_mask[port],
               "k22_package_port_pin_mask(selected, port) == expected->port_pin_mask[port]");
        for (uint8_t pin = 0; pin < K22_PACKAGE_PIN_COUNT; pin++) {
            const bool exists = (expected->port_pin_mask[port] & (UINT32_C(1) << pin)) != 0;
            expect(state, k22_package_pin_exists(selected, port, pin) == exists,
                   "k22_package_pin_exists(selected, port, pin) == exists");
        }
    }
    for (int peripheral = 0; peripheral < K22_PERIPHERAL_COUNT; peripheral++) {
        bool exists = k22_profile_has_peripheral(k22_profile_get(expected->profile),
                                                 (K22PeripheralId)peripheral);
        if (peripheral == K22_PERIPHERAL_DAC1)
            exists = expected->dac1;
        if (peripheral == K22_PERIPHERAL_FB)
            exists = expected->flexbus;
        if (expected->profile == K22_PROFILE_MK22FN1M012 ||
            expected->profile == K22_PROFILE_MK22FX51212) {
            const bool at_least_80 = expected->package != K22_PACKAGE_LH_64_LQFP;
            const bool at_least_100 = expected->package == K22_PACKAGE_LL_100_LQFP ||
                                      expected->package == K22_PACKAGE_MC_121_MAPBGA ||
                                      expected->package == K22_PACKAGE_LQ_144_LQFP ||
                                      expected->package == K22_PACKAGE_MD_144_MAPBGA;
            const bool at_least_121 = expected->package == K22_PACKAGE_MC_121_MAPBGA ||
                                      expected->package == K22_PACKAGE_LQ_144_LQFP ||
                                      expected->package == K22_PACKAGE_MD_144_MAPBGA;
            if (peripheral == K22_PERIPHERAL_SPI1 || peripheral == K22_PERIPHERAL_UART3 ||
                peripheral == K22_PERIPHERAL_SDHC)
                exists = at_least_80;
            if (peripheral == K22_PERIPHERAL_SPI2 || peripheral == K22_PERIPHERAL_UART4)
                exists = at_least_100;
            if (peripheral == K22_PERIPHERAL_UART5 || peripheral == K22_PERIPHERAL_DAC1)
                exists = at_least_121;
        }
        expect(state, k22_package_has_peripheral(selected, (K22PeripheralId)peripheral) == exists,
               "k22_package_has_peripheral( selected, (K22PeripheralId)peripheral) == exists");
    }
}

static void expect_all_combinations(TestState* state) {
    for (int profile_id = 0; profile_id < K22_PROFILE_COUNT; profile_id++) {
        const K22Profile* profile = k22_profile_get((K22ProfileId)profile_id);
        expect(state, profile != NULL, "profile != NULL");
        for (int package_id = 0; package_id < K22_PACKAGE_COUNT; package_id++) {
            const ExpectedSelection* expected =
                expected_selection((K22ProfileId)profile_id, (K22PackageId)package_id);
            const K22PackageSelection* selected =
                k22_package_select(profile, (K22PackageId)package_id);
            expect(state, (selected != NULL) == (expected != NULL),
                   "(selected != NULL) == (expected != NULL)");
            if (expected != NULL)
                expect_selection_data(state, expected, selected);
        }
    }
}

static void expect_defaults(TestState* state) {
    for (int profile_id = 0; profile_id < K22_PROFILE_COUNT; profile_id++) {
        const K22Profile* profile = k22_profile_get((K22ProfileId)profile_id);
        const K22PackageSelection* selected = k22_package_default(profile);
        expect(state, selected != NULL, "selected != NULL");
        const K22PackageId expected = profile_id == K22_PROFILE_MK22FN12812
                                          ? K22_PACKAGE_AH_64_WLCSP
                                          : K22_PACKAGE_LH_64_LQFP;
        expect(state, k22_package_selection_package(selected)->id == expected,
               "k22_package_selection_package(selected)->id == expected");
    }
}

static void expect_fail_closed(TestState* state) {
    const K22Profile* profile = k22_profile_get(K22_PROFILE_MK22FN51212);
    const K22PackageSelection* selected = k22_package_select(profile, K22_PACKAGE_LH_64_LQFP);
    K22Profile invalid_profile = *profile;
    invalid_profile.id = K22_PROFILE_COUNT;
    expect(state, k22_package_get((K22PackageId)-1) == NULL,
           "k22_package_get((K22PackageId)-1) == NULL");
    expect(state, k22_package_get(K22_PACKAGE_COUNT) == NULL,
           "k22_package_get(K22_PACKAGE_COUNT) == NULL");
    expect(state, k22_package_find(NULL) == NULL, "k22_package_find(NULL) == NULL");
    expect(state, k22_package_find("") == NULL, "k22_package_find(\"\") == NULL");
    expect(state, k22_package_find("lh") == NULL, "k22_package_find(\"lh\") == NULL");
    KinetisK22Package public_package = KINETIS_K22_PACKAGE_LH_64_LQFP;
    expect(state, !kinetis_k22_package_from_code(NULL, &public_package),
           "!kinetis_k22_package_from_code(NULL, &public_package)");
    expect(state, !kinetis_k22_package_from_code("lh", &public_package),
           "!kinetis_k22_package_from_code(\"lh\", &public_package)");
    expect(state, kinetis_k22_package_code(KINETIS_K22_PACKAGE_DEFAULT) == NULL,
           "kinetis_k22_package_code(KINETIS_K22_PACKAGE_DEFAULT) == NULL");
    expect(state, k22_package_select(NULL, K22_PACKAGE_LH_64_LQFP) == NULL,
           "k22_package_select(NULL, K22_PACKAGE_LH_64_LQFP) == NULL");
    expect(state, k22_package_select(&invalid_profile, K22_PACKAGE_LH_64_LQFP) == NULL,
           "k22_package_select(&invalid_profile, K22_PACKAGE_LH_64_LQFP) == NULL");
    expect(state, k22_package_select(profile, (K22PackageId)-1) == NULL,
           "k22_package_select(profile, (K22PackageId)-1) == NULL");
    expect(state, k22_package_select(profile, K22_PACKAGE_COUNT) == NULL,
           "k22_package_select(profile, K22_PACKAGE_COUNT) == NULL");
    expect(state, k22_package_default(NULL) == NULL, "k22_package_default(NULL) == NULL");
    expect(state, k22_package_default(&invalid_profile) == NULL,
           "k22_package_default(&invalid_profile) == NULL");
    expect(state, k22_package_selection_profile(NULL) == K22_PROFILE_COUNT,
           "k22_package_selection_profile(NULL) == K22_PROFILE_COUNT");
    expect(state, k22_package_selection_package(NULL) == NULL,
           "k22_package_selection_package(NULL) == NULL");
    expect(state, k22_package_port_pin_mask(NULL, 0) == 0,
           "k22_package_port_pin_mask(NULL, 0) == 0");
    expect(state, k22_package_port_pin_mask(selected, K22_PACKAGE_PORT_COUNT) == 0,
           "k22_package_port_pin_mask(selected, K22_PACKAGE_PORT_COUNT) == 0");
    expect(state, !k22_package_pin_exists(NULL, 0, 0), "!k22_package_pin_exists(NULL, 0, 0)");
    expect(state, !k22_package_pin_exists(selected, K22_PACKAGE_PORT_COUNT, 0),
           "!k22_package_pin_exists(selected, K22_PACKAGE_PORT_COUNT, 0)");
    expect(state, !k22_package_pin_exists(selected, 0, K22_PACKAGE_PIN_COUNT),
           "!k22_package_pin_exists(selected, 0, K22_PACKAGE_PIN_COUNT)");
    expect(state, !k22_package_has_peripheral(NULL, K22_PERIPHERAL_DMA),
           "!k22_package_has_peripheral(NULL, K22_PERIPHERAL_DMA)");
    expect(state, !k22_package_has_peripheral(selected, (K22PeripheralId)-1),
           "!k22_package_has_peripheral(selected, (K22PeripheralId)-1)");
    expect(state, !k22_package_has_peripheral(selected, K22_PERIPHERAL_COUNT),
           "!k22_package_has_peripheral(selected, K22_PERIPHERAL_COUNT)");
}

int main(void) {
    TestState state = {0};
    expect_package_metadata(&state);
    expect_all_combinations(&state);
    expect_defaults(&state);
    expect_fail_closed(&state);
    return test_finish(&state);
}
