# Cortex-M4 Simulator

This repository contains a native C simulator for the Arm Cortex-M4F processor.
It also contains a Kinetis K22 device model.

The CPU model targets the Armv7E-M architecture and FPv4-SP-D16 extension.
The device model targets the K22 peripherals that the firmware uses.

The implementation is in progress. The current tests cover the implemented
CPU instructions, exception paths, memory map, and device peripherals. A green
test result does not mean that every Arm or K22 feature is complete.

The exact K22 package and silicon revision are not known.
The simulator does not claim behavior that depends on that missing identification.

## Current support

The CPU currently supports the Thumb and floating-point instructions used by
the target firmware. It also supports basic exceptions, NVIC, SCB, SysTick,
peripheral bit-band access, and bounded execution.

The K22 model currently supports flash, SRAM, GPIO, PORT interrupts, PIT, ADC0,
UART1, SPI0, I2C0, DMA, and selected clock and power registers.

The remaining work includes less common Armv7E-M instructions, complete fault
and priority behavior, advanced floating-point state, MPU behavior, watchdog
reset timing, and electrical timing that requires hardware tests.

## Build

Configure a Release build:

```
cmake -S . -B build/simulator -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Build the simulator:

```
cmake --build build/simulator --parallel
```

The build provides the `cortex_m4::simulator` static library target.

## Use from CMake

Add this repository as a Git submodule.

Add the submodule to the parent build:

```cmake
add_subdirectory(third_party/cortex-m4-sim EXCLUDE_FROM_ALL)
target_link_libraries(your_target PRIVATE cortex_m4::simulator)
```

The parent build does not build the standalone tests.

## Tests

CTest runs isolated native C executables.
The tests do not require an external test framework.

Run all tests:

```
ctest --test-dir build/simulator --parallel --output-on-failure
```

The `tests` directory separates core, device, runner, and system tests.

## Coverage

GCC and gcov measure source coverage.

Configure a coverage build:

```
cmake -S . -B build/coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCORTEX_M4_ENABLE_COVERAGE=ON
```

Build and run the tests:

```
cmake --build build/coverage --parallel
ctest --test-dir build/coverage --output-on-failure
```

Create the gcov reports:

```
cmake --build build/coverage --target cortex_m4_coverage_report
```

Coverage measures the native model.
Hardware tests must measure electrical timing, analog tolerances, and silicon-specific behavior.
