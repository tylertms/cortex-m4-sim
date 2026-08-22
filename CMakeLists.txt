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
    src/core/cortex_m4.c
    src/core/cortex_m4_bus.c
    src/core/cortex_m4_constraints.c
    src/core/cortex_m4_debug.c
    src/core/cortex_m4_dsp.c
    src/core/cortex_m4_exception.c
    src/core/cortex_m4_exception_advanced.c
    src/core/cortex_m4_fpu.c
    src/core/cortex_m4_it.c
    src/core/cortex_m4_mpu.c
    src/core/cortex_m4_remaining.c
    src/core/cortex_m4_system.c
    src/core/cortex_m4_thumb.c
    src/core/cortex_m4_thumb32_extra.c
    src/core/cortex_m4_timing.c
    src/device/k22_data.c
    src/device/k22_io.c
    src/device/k22_package.c
    src/device/k22_profile.c
    src/device/k22_register_manifest.c
    src/device/registers/mk22f12.c
    src/device/registers/mk22f12810.c
    src/device/registers/mk22f25612.c
    src/device/registers/mk22f51212.c
    src/device/k22_sdhc.c
    src/device/k22_serial.c
    src/device/k22_timing.c
    src/device/k22_usbdcd.c
    src/device/kinetis_k22.c
    src/device/kinetis_k22_peripherals.c)

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
  PRIVATE src/core src/device)
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
    target_include_directories(${target} PRIVATE tests/support tests/device
                                                 src/core src/device)
    target_link_libraries(${target} PRIVATE kinetis_k22_simulator)
    if(MSVC)
      target_compile_options(${target} PRIVATE /W4 /WX)
    else()
      target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "${labels}")
  endfunction()

  kinetis_k22_add_test(core_reset tests/core/reset_test.c "core;unit")
  kinetis_k22_add_test(core_api tests/core/api_test.c "core;unit;integration")
  kinetis_k22_add_test(core_thumb tests/core/thumb_test.c "core;integration")
  kinetis_k22_add_test(core_thumb16_arithmetic
                       tests/core/thumb16_arithmetic_test.c "core;unit")
  kinetis_k22_add_test(core_thumb16_memory tests/core/thumb16_memory_test.c
                       "core;integration")
  kinetis_k22_add_test(core_thumb16_complete tests/core/thumb16_complete_test.c
                       "core;integration")
  kinetis_k22_add_test(core_thumb32 tests/core/thumb32_test.c
                       "core;integration")
  kinetis_k22_add_test(core_thumb32_extended tests/core/thumb32_extended_test.c
                       "core;integration")
  kinetis_k22_add_test(core_thumb32_complete tests/core/thumb32_complete_test.c
                       "core;unit;integration")
  kinetis_k22_add_test(
    core_thumb32_memory_complete tests/core/thumb32_memory_complete_test.c
    "core;integration")
  kinetis_k22_add_test(core_power tests/core/power_test.c "core;integration")
  kinetis_k22_add_test(core_processor_state tests/core/processor_state_test.c
                       "core;integration")
  kinetis_k22_add_test(
    core_constraints_complete tests/core/constraints_complete_test.c
    "core;unit;integration")
  kinetis_k22_add_test(core_debug_complete tests/core/debug_complete_test.c
                       "core;unit;integration")
  kinetis_k22_add_test(core_dsp tests/core/dsp_test.c "core;unit")
  kinetis_k22_add_test(core_fpu tests/core/fpu_test.c "core;integration")
  kinetis_k22_add_test(core_fpu_complete tests/core/fpu_complete_test.c
                       "core;unit;integration")
  kinetis_k22_add_test(
    core_fpu_edge_complete tests/core/fpu_edge_complete_test.c
    "core;unit;integration")
  kinetis_k22_add_test(core_mpu_complete tests/core/mpu_complete_test.c
                       "core;unit;integration")
  kinetis_k22_add_test(
    core_instruction_census tests/core/instruction_census_test.c
    "core;unit;integration")
  kinetis_k22_add_test(core_it_complete tests/core/it_complete_test.c
                       "core;unit;integration")
  kinetis_k22_add_test(core_exception tests/core/exception_test.c
                       "core;integration")
  kinetis_k22_add_test(
    core_exception_advanced tests/core/exception_advanced_test.c
    "core;unit;integration")
  kinetis_k22_add_test(
    core_exception_integration_complete
    tests/core/exception_integration_complete_test.c "core;integration")
  kinetis_k22_add_test(core_fault tests/core/fault_test.c "core;integration")
  kinetis_k22_add_test(core_system_exception tests/core/system_exception_test.c
                       "core;integration")
  kinetis_k22_add_test(core_system_register tests/core/system_register_test.c
                       "core;integration")
  kinetis_k22_add_test(core_system_complete tests/core/system_complete_test.c
                       "core;integration")
  kinetis_k22_add_test(core_timing_complete tests/core/timing_complete_test.c
                       "core;unit;integration")
  kinetis_k22_add_test(core_trace tests/core/trace_test.c "core;integration")
  kinetis_k22_add_test(device_memory tests/device/model/memory_test.c
                       "device;unit")
  kinetis_k22_add_test(device_profile tests/device/model/k22_profile_test.c
                       "device;unit")
  kinetis_k22_add_test(
    device_register_manifest tests/device/model/k22_register_manifest_test.c
    "device;unit")
  kinetis_k22_add_test(
    device_register_access tests/device/model/k22_register_access_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_data_complete tests/device/data/k22_data_complete_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_fmc_complete tests/device/data/k22_fmc_complete_test.c
    "device;unit;integration")
  kinetis_k22_add_test(
    device_io_complete tests/device/io/k22_io_complete_test.c
    "device;integration")
  kinetis_k22_add_test(device_package tests/device/model/k22_package_test.c
                       "device;unit")
  kinetis_k22_add_test(
    device_serial_complete tests/device/serial/k22_serial_complete_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_sdhc_complete tests/device/serial/k22_sdhc_complete_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_timing_complete tests/device/timing/k22_timing_complete_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_watchdog_complete tests/device/timing/k22_watchdog_complete_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_cmt_complete tests/device/timing/k22_cmt_complete_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_usbdcd_complete tests/device/serial/k22_usbdcd_complete_test.c
    "device;unit;integration")
  kinetis_k22_add_test(
    device_ftm_synchronization
    tests/device/timing/k22_ftm_synchronization_test.c "device;integration")
  kinetis_k22_add_test(
    device_ftm_combine tests/device/timing/k22_ftm_combine_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_ftm_deadtime tests/device/timing/k22_ftm_deadtime_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_ftm_fault tests/device/timing/k22_ftm_fault_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_ftm_quadrature tests/device/timing/k22_ftm_quadrature_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_integration_complete
    tests/device/integration/k22_integration_complete_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_host_api_complete
    tests/device/integration/k22_host_api_complete_test.c "device;integration")
  kinetis_k22_add_test(
    device_clock_gate_complete
    tests/device/integration/k22_clock_gate_complete_test.c
    "device;integration")
  kinetis_k22_add_test(
    device_sysmpu_integration
    tests/device/integration/k22_sysmpu_integration_test.c "device;integration")
  kinetis_k22_add_test(
    device_infrastructure tests/device/integration/k22_infrastructure_test.c
    "device;integration")
  kinetis_k22_add_test(device_gpio tests/device/io/gpio_test.c
                       "device;integration")
  kinetis_k22_add_test(device_pit tests/device/timing/pit_test.c
                       "device;integration")
  kinetis_k22_add_test(device_adc tests/device/data/adc_test.c
                       "device;integration")
  kinetis_k22_add_test(device_uart tests/device/serial/uart_test.c
                       "device;integration")
  kinetis_k22_add_test(device_spi tests/device/serial/spi_test.c
                       "device;integration")
  kinetis_k22_add_test(device_dma tests/device/data/dma_test.c
                       "device;integration")
  kinetis_k22_add_test(device_i2c tests/device/serial/i2c_test.c
                       "device;integration")
  kinetis_k22_add_test(device_watchdog tests/device/timing/watchdog_test.c
                       "device;integration")
  kinetis_k22_add_test(
    device_clock_power tests/device/timing/clock_power_test.c
    "device;integration")
  kinetis_k22_add_test(firmware_image tests/system/firmware_image_test.c
                       "runner;system")
  target_link_libraries(kinetis_k22_firmware_image_test
                        PRIVATE kinetis_k22_firmware_image)

endif()
