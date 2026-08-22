cmake_minimum_required(VERSION 3.21)

project(kinetis_k22_sim LANGUAGES C)

if(PROJECT_IS_TOP_LEVEL)
  option(BUILD_TESTING "Build the simulator tests" ON)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      Release
      CACHE STRING "Build type" FORCE)
endif()

set(kinetis_k22_simulator_sources
    src/architecture/cortex_m4/core.c
    src/architecture/cortex_m4/bus.c
    src/architecture/cortex_m4/constraints.c
    src/architecture/cortex_m4/debug.c
    src/architecture/cortex_m4/instructions/dsp.c
    src/architecture/cortex_m4/exceptions/base.c
    src/architecture/cortex_m4/exceptions/advanced.c
    src/architecture/cortex_m4/instructions/fpu.c
    src/architecture/cortex_m4/instructions/it.c
    src/architecture/cortex_m4/mpu.c
    src/architecture/cortex_m4/instructions/misc.c
    src/architecture/cortex_m4/system.c
    src/architecture/cortex_m4/instructions/thumb/thumb16.c
    src/architecture/cortex_m4/instructions/thumb/thumb32.c
    src/architecture/cortex_m4/instructions/thumb/extended.c
    src/architecture/cortex_m4/timing.c
    src/device/kinetis_k22/memory/api.c
    src/device/kinetis_k22/memory/analog.c
    src/device/kinetis_k22/memory/dma.c
    src/device/kinetis_k22/memory/flash.c
    src/device/kinetis_k22/memory/integrity.c
    src/device/kinetis_k22/memory/runtime.c
    src/device/kinetis_k22/gpio/api.c
    src/device/kinetis_k22/gpio/core.c
    src/device/kinetis_k22/gpio/peripherals.c
    src/device/kinetis_k22/variants/package.c
    src/device/kinetis_k22/variants/profile.c
    src/device/kinetis_k22/variants/manifest.c
    src/device/kinetis_k22/variants/registers/mk22f12.c
    src/device/kinetis_k22/variants/registers/mk22f12810.c
    src/device/kinetis_k22/variants/registers/mk22f25612.c
    src/device/kinetis_k22/variants/registers/mk22f51212.c
    src/device/kinetis_k22/communication/sdhc.c
    src/device/kinetis_k22/communication/serial/registers.c
    src/device/kinetis_k22/communication/serial/runtime.c
    src/device/kinetis_k22/timing/api.c
    src/device/kinetis_k22/timing/clocks.c
    src/device/kinetis_k22/timing/counters.c
    src/device/kinetis_k22/timing/ftm.c
    src/device/kinetis_k22/timing/registers.c
    src/device/kinetis_k22/timing/watchdogs.c
    src/device/kinetis_k22/communication/usb_dcd.c
    src/device/kinetis_k22/device.c
    src/device/kinetis_k22/host.c
    src/device/kinetis_k22/platform/peripherals.c
    src/device/kinetis_k22/platform/registers.c)

add_library(kinetis_k22_simulator STATIC ${kinetis_k22_simulator_sources})
add_library(kinetis_k22::simulator ALIAS kinetis_k22_simulator)

add_library(kinetis_k22_firmware_image STATIC
            src/image/cortex_m4_firmware_image.c)
add_library(kinetis_k22::firmware_image ALIAS kinetis_k22_firmware_image)

add_executable(kinetis_k22_firmware_runner src/runner/main.c)
add_executable(kinetis_k22::firmware_runner ALIAS kinetis_k22_firmware_runner)

target_compile_features(kinetis_k22_simulator PUBLIC c_std_11)
target_include_directories(
  kinetis_k22_simulator
  PUBLIC include
  PRIVATE src)
target_compile_features(kinetis_k22_firmware_runner PRIVATE c_std_11)
target_compile_features(kinetis_k22_firmware_image PUBLIC c_std_11)
target_include_directories(kinetis_k22_firmware_image PUBLIC include)
target_link_libraries(kinetis_k22_firmware_image PUBLIC kinetis_k22_simulator)
target_link_libraries(kinetis_k22_firmware_runner
                      PRIVATE kinetis_k22_firmware_image)

if(MSVC)
  target_compile_options(kinetis_k22_simulator PRIVATE /W4 /WX)
  target_compile_options(kinetis_k22_firmware_runner PRIVATE /W4 /WX)
  target_compile_options(kinetis_k22_firmware_image PRIVATE /W4 /WX)
else()
  target_compile_options(kinetis_k22_simulator PRIVATE -Wall -Wextra -Wpedantic
                                                       -Werror)
  target_compile_options(kinetis_k22_firmware_runner PRIVATE -Wall -Wextra
                                                             -Wpedantic -Werror)
  target_compile_options(kinetis_k22_firmware_image PRIVATE -Wall -Wextra
                                                            -Wpedantic -Werror)
endif()

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  enable_testing()

  function(kinetis_k22_add_test name source labels)
    set(target "kinetis_k22_${name}_test")
    add_executable(${target} ${source} ${ARGN})
    target_compile_features(${target} PRIVATE c_std_11)
    target_include_directories(${target} PRIVATE tests/support tests src)
    target_link_libraries(${target} PRIVATE kinetis_k22_simulator)
    if(MSVC)
      target_compile_options(${target} PRIVATE /W4 /WX)
    else()
      target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "${labels}")
  endfunction()

  function(kinetis_k22_enable_allocation_failure target)
    target_sources(${target} PRIVATE tests/support/allocation_failure.c)
    if(NOT MSVC)
      target_compile_definitions(${target} PRIVATE K22_TEST_ALLOCATION_FAILURE=1)
      target_link_options(
        ${target} PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc
        -Wl,--wrap=realloc)
    endif()
  endfunction()

  kinetis_k22_add_test(
    core_reset tests/architecture/cortex_m4/system/reset_test.c "core;unit")
  kinetis_k22_add_test(core_api tests/architecture/cortex_m4/api_test.c
                       "core;unit;integration")
  kinetis_k22_add_test(
    core_thumb tests/architecture/cortex_m4/execution/thumb_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_thumb16_arithmetic
    tests/architecture/cortex_m4/execution/thumb16_arithmetic_test.c
    "core;unit")
  kinetis_k22_add_test(
    core_thumb16_memory
    tests/architecture/cortex_m4/execution/thumb16_memory_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_thumb16_complete tests/architecture/cortex_m4/execution/thumb16_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_thumb32 tests/architecture/cortex_m4/execution/thumb32_smoke_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_thumb32_extended
    tests/architecture/cortex_m4/execution/thumb32_extended_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_thumb32_complete tests/architecture/cortex_m4/execution/thumb32_test.c
    "core;unit;integration")
  kinetis_k22_add_test(
    core_thumb32_memory_complete
    tests/architecture/cortex_m4/execution/thumb32_memory_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_power tests/architecture/cortex_m4/system/power_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_processor_state tests/architecture/cortex_m4/system/state_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_constraints_complete
    tests/architecture/cortex_m4/system/constraints_test.c
    "core;unit;integration")
  kinetis_k22_add_test(
    core_debug_complete tests/architecture/cortex_m4/system/debug_test.c
    "core;unit;integration")
  kinetis_k22_add_test(
    core_dsp tests/architecture/cortex_m4/execution/dsp_test.c "core;unit")
  kinetis_k22_add_test(
    core_fpu tests/architecture/cortex_m4/floating_point/fpu_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_fpu_complete
    tests/architecture/cortex_m4/floating_point/complete_test.c
    "core;unit;integration")
  kinetis_k22_add_test(
    core_fpu_edge_complete
    tests/architecture/cortex_m4/floating_point/edge_test.c
    "core;unit;integration")
  kinetis_k22_add_test(
    core_mpu_complete tests/architecture/cortex_m4/system/mpu_test.c
    "core;unit;integration")
  kinetis_k22_add_test(
    core_instruction_census
    tests/architecture/cortex_m4/execution/instruction_census_test.c
    "core;unit;integration")
  kinetis_k22_add_test(
    core_encoding_census
    tests/architecture/cortex_m4/execution/encoding_census_test.c
    "core;unit;integration")
  kinetis_k22_add_test(
    core_it_complete tests/architecture/cortex_m4/execution/it_test.c
    "core;unit;integration")
  kinetis_k22_add_test(
    core_exception tests/architecture/cortex_m4/exceptions/exception_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_exception_advanced
    tests/architecture/cortex_m4/exceptions/advanced_test.c
    "core;unit;integration")
  kinetis_k22_add_test(
    core_exception_integration_complete
    tests/architecture/cortex_m4/exceptions/integration_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_fault tests/architecture/cortex_m4/exceptions/fault_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_system_exception tests/architecture/cortex_m4/exceptions/system_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_system_register
    tests/architecture/cortex_m4/system/register_access_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_system_complete tests/architecture/cortex_m4/system/registers_test.c
    "core;integration")
  kinetis_k22_add_test(
    core_system_boundary tests/architecture/cortex_m4/system/boundary_test.c
    "core;unit")
  kinetis_k22_add_test(
    core_timing_complete tests/architecture/cortex_m4/system/timing_test.c
    "core;unit;integration")
  kinetis_k22_add_test(
    core_trace tests/architecture/cortex_m4/system/trace_test.c
    "core;integration")
  kinetis_k22_add_test(
    device_memory tests/device/kinetis_k22/memory/memory_test.c "device;unit")
  kinetis_k22_add_test(
    device_profile tests/device/kinetis_k22/variants/profile_test.c
    "device;unit")
  kinetis_k22_add_test(
    device_register_manifest tests/device/kinetis_k22/variants/manifest_test.c
    "device;unit")
  kinetis_k22_add_test(
    device_register_access
    tests/device/kinetis_k22/variants/register_access_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_peripheral_census
    tests/device/kinetis_k22/variants/peripheral_census_test.c
    "device;unit;integration")
  kinetis_k22_add_test(
    device_data_complete tests/device/kinetis_k22/memory/data/core.c
    "device;integration" tests/device/kinetis_k22/memory/data/flash.c
    tests/device/kinetis_k22/memory/data/swap.c
    tests/device/kinetis_k22/memory/data/boundary.c)
  kinetis_k22_add_test(
    device_fmc_complete tests/device/kinetis_k22/memory/fmc_test.c
    "device;unit;integration")
  kinetis_k22_add_test(
    device_io_complete tests/device/kinetis_k22/platform/io_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_package tests/device/kinetis_k22/variants/package_test.c
    "device;unit")
  kinetis_k22_add_test(
    device_serial_complete tests/device/kinetis_k22/communication/serial_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_sdhc_complete tests/device/kinetis_k22/communication/sdhc_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_timing_complete tests/device/kinetis_k22/timing/core.c
    "device;integration" tests/device/kinetis_k22/timing/ftm.c
    tests/device/kinetis_k22/timing/surface.c)
  kinetis_k22_add_test(
    device_watchdog_complete
    tests/device/kinetis_k22/timing/watchdog_api_test.c "device;integration")
  kinetis_k22_add_test(
    device_cmt_complete tests/device/kinetis_k22/timing/cmt_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_usbdcd_complete
    tests/device/kinetis_k22/communication/usb_dcd_test.c
    "device;unit;integration")
  kinetis_k22_add_test(
    device_ftm_synchronization
    tests/device/kinetis_k22/control/ftm_synchronization_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_ftm_combine tests/device/kinetis_k22/control/ftm_combine_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_ftm_deadtime tests/device/kinetis_k22/control/ftm_deadtime_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_ftm_fault tests/device/kinetis_k22/control/ftm_fault_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_ftm_quadrature
    tests/device/kinetis_k22/control/ftm_quadrature_test.c "device;integration")
  kinetis_k22_add_test(
    device_integration_complete
    tests/device/kinetis_k22/platform/integration/platform.c
    "device;integration"
    tests/device/kinetis_k22/platform/integration/peripherals.c)
  kinetis_k22_add_test(
    device_host_api_complete tests/device/kinetis_k22/platform/host_api_test.c
    "device;integration")
  kinetis_k22_enable_allocation_failure(kinetis_k22_device_host_api_complete_test)
  kinetis_k22_add_test(
    device_clock_gate_complete
    tests/device/kinetis_k22/platform/clock_gate_test.c "device;integration")
  kinetis_k22_add_test(
    device_sysmpu_integration tests/device/kinetis_k22/platform/sysmpu_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_infrastructure
    tests/device/kinetis_k22/platform/infrastructure_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_gpio tests/device/kinetis_k22/platform/gpio_test.c
    "device;integration")
  kinetis_k22_add_test(device_pit tests/device/kinetis_k22/timing/pit_test.c
                       "device;integration")
  kinetis_k22_add_test(device_adc tests/device/kinetis_k22/analog/adc_test.c
                       "device;integration")
  kinetis_k22_add_test(
    device_uart tests/device/kinetis_k22/communication/uart_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_spi tests/device/kinetis_k22/communication/spi_test.c
    "device;integration")
  kinetis_k22_add_test(device_dma tests/device/kinetis_k22/memory/dma_test.c
                       "device;integration")
  kinetis_k22_add_test(
    device_i2c tests/device/kinetis_k22/communication/i2c_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_watchdog tests/device/kinetis_k22/timing/watchdog_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_clock_power tests/device/kinetis_k22/timing/clock_power_test.c
    "device;integration")
  kinetis_k22_add_test(firmware_image tests/system/firmware_image_test.c
                       "runner;system")
  target_link_libraries(kinetis_k22_firmware_image_test
                        PRIVATE kinetis_k22_firmware_image)

endif()
