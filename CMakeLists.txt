cmake_minimum_required(VERSION 3.21)

project(cortex_m4_sim LANGUAGES C)

if(PROJECT_IS_TOP_LEVEL)
  option(BUILD_TESTING "Build the simulator tests" ON)
  option(CORTEX_M4_ENABLE_COVERAGE "Enable GCC coverage instrumentation" OFF)
  if(CORTEX_M4_ENABLE_COVERAGE)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
      message(FATAL_ERROR "Coverage requires GCC")
    endif()
    add_compile_options(-O1 -g --coverage)
    add_link_options(--coverage)
    find_program(GCOV_EXECUTABLE gcov REQUIRED)
  endif()
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      Release
      CACHE STRING "Build type" FORCE)
endif()

set(cortex_m4_simulator_sources
    src/cortex_m4.c
    src/cortex_m4_bus.c
    src/cortex_m4_exception.c
    src/cortex_m4_fpu.c
    src/cortex_m4_thumb.c
    src/kinetis_k22.c
    src/kinetis_k22_peripherals.c)

add_library(cortex_m4_simulator STATIC ${cortex_m4_simulator_sources})
add_library(cortex_m4::simulator ALIAS cortex_m4_simulator)

add_executable(cortex_m4_firmware_runner src/firmware_image.c src/main.c)
add_executable(cortex_m4::firmware_runner ALIAS cortex_m4_firmware_runner)

target_compile_features(cortex_m4_simulator PUBLIC c_std_11)
target_include_directories(
  cortex_m4_simulator
  PUBLIC include
  PRIVATE src)
target_compile_features(cortex_m4_firmware_runner PRIVATE c_std_11)
target_include_directories(cortex_m4_firmware_runner PRIVATE src)
target_link_libraries(cortex_m4_firmware_runner PRIVATE cortex_m4_simulator)

if(MSVC)
  target_compile_options(cortex_m4_simulator PRIVATE /W4 /WX)
  target_compile_options(cortex_m4_firmware_runner PRIVATE /W4 /WX)
else()
  target_compile_options(cortex_m4_simulator PRIVATE -Wall -Wextra -Wpedantic
                                                     -Werror)
  target_compile_options(cortex_m4_firmware_runner PRIVATE -Wall -Wextra
                                                           -Wpedantic -Werror)
endif()

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  enable_testing()

  function(cortex_m4_add_test name source labels)
    set(target "cortex_m4_${name}_test")
    add_executable(${target} ${source} ${ARGN})
    target_compile_features(${target} PRIVATE c_std_11)
    target_include_directories(${target} PRIVATE tests/support src)
    target_link_libraries(${target} PRIVATE cortex_m4_simulator)
    if(MSVC)
      target_compile_options(${target} PRIVATE /W4 /WX)
    else()
      target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "${labels}" TIMEOUT 60)
  endfunction()

  cortex_m4_add_test(core_reset tests/core/reset_test.c "core;unit")
  cortex_m4_add_test(core_thumb tests/core/thumb_test.c "core;integration")
  cortex_m4_add_test(core_thumb16_arithmetic
                     tests/core/thumb16_arithmetic_test.c "core;unit")
  cortex_m4_add_test(core_thumb16_memory tests/core/thumb16_memory_test.c
                     "core;integration")
  cortex_m4_add_test(core_thumb32 tests/core/thumb32_test.c "core;integration")
  cortex_m4_add_test(core_thumb32_extended tests/core/thumb32_extended_test.c
                     "core;integration")
  cortex_m4_add_test(core_power tests/core/power_test.c "core;integration")
  cortex_m4_add_test(core_fpu tests/core/fpu_test.c "core;integration")
  cortex_m4_add_test(core_exception tests/core/exception_test.c
                     "core;integration")
  cortex_m4_add_test(core_fault tests/core/fault_test.c "core;integration")
  cortex_m4_add_test(core_system_exception tests/core/system_exception_test.c
                     "core;integration")
  cortex_m4_add_test(core_system_register tests/core/system_register_test.c
                     "core;integration")
  cortex_m4_add_test(core_trace tests/core/trace_test.c "core;integration")
  cortex_m4_add_test(device_memory tests/device/memory_test.c "device;unit")
  cortex_m4_add_test(device_gpio tests/device/gpio_test.c "device;integration")
  cortex_m4_add_test(device_pit tests/device/pit_test.c "device;integration")
  cortex_m4_add_test(device_adc tests/device/adc_test.c "device;integration")
  cortex_m4_add_test(device_uart tests/device/uart_test.c "device;integration")
  cortex_m4_add_test(device_spi tests/device/spi_test.c "device;integration")
  cortex_m4_add_test(device_dma tests/device/dma_test.c "device;integration")
  cortex_m4_add_test(device_i2c tests/device/i2c_test.c "device;integration")
  cortex_m4_add_test(device_watchdog tests/device/watchdog_test.c
                     "device;integration")
  cortex_m4_add_test(device_clock_power tests/device/clock_power_test.c
                     "device;integration")
  cortex_m4_add_test(firmware_image tests/system/firmware_image_test.c
                     "runner;system" src/firmware_image.c)

  if(CORTEX_M4_ENABLE_COVERAGE)
    set(coverage_directory "${CMAKE_CURRENT_BINARY_DIR}/coverage")
    file(MAKE_DIRECTORY "${coverage_directory}")
    set(coverage_notes ${cortex_m4_simulator_sources})
    list(
      TRANSFORM coverage_notes
      PREPEND "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/cortex_m4_simulator.dir/")
    list(TRANSFORM coverage_notes APPEND ".gcno")
    add_custom_target(
      cortex_m4_coverage_report
      COMMAND ${GCOV_EXECUTABLE} -b ${coverage_notes}
      WORKING_DIRECTORY "${coverage_directory}"
      DEPENDS cortex_m4_simulator
      VERBATIM)
  endif()
endif()
