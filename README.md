# Cortex-M4 Simulator

This repository provides a native C simulator for the Arm Cortex-M4F processor.
It also provides a Kinetis K22 device model.

The CPU implements the Armv7E-M and FPv4-SP-D16 features used by the target
firmware. The device implements the K22 interfaces used by that firmware.

The exact K22 package and silicon revision are not known. The simulator does
not claim behavior that depends on that missing identification.

## Supported behavior

The CPU supports the required Thumb and Thumb-2 instructions. It also supports
the required floating-point, exception, interrupt, fault, and sleep behavior.

The system model includes the NVIC, SCB, SysTick, bit-band access, reset,
breakpoints, instruction tracing, and bounded execution.

The K22 model includes these interfaces:

- Flash and SRAM
- SIM, MCG, SMC, and low-power timer registers used during startup
- GPIO and PORT interrupts
- PIT channels
- ADC0
- UART1
- SPI0
- I2C0
- DMA and DMAMUX
- Watchdog reset and refresh behavior

The simulator does not implement every optional Cortex-M4 or K22 feature. The
current target does not use the MPU, USB, or the omitted instruction groups.

Hardware tests must verify electrical timing, analog tolerances, clock accuracy,
and silicon-specific behavior.

## Build

Configure a Release build:

```
cmake -S . -B build/simulator -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Build the simulator:

```
cmake --build build/simulator --parallel
```

The build provides these targets:

- `cortex_m4::simulator` is the static simulator library.
- `cortex_m4::firmware_image` loads ELF and raw binary images.
- `cortex_m4::firmware_runner` loads and runs a firmware image.

The runner accepts an Arm ELF file or a raw binary file. It requires a vector
table address and uses finite instruction and cycle limits by default.

## Use from CMake

Add this repository as a Git submodule. Add the submodule to the parent build:

```cmake
add_subdirectory(third_party/cortex-m4-sim EXCLUDE_FROM_ALL)
target_link_libraries(your_target PRIVATE cortex_m4::simulator)
```

The parent build does not build the standalone tests.

## Tests

CTest runs isolated native C executables. No external test framework is needed.

Run all tests:

```
ctest --test-dir build/simulator --parallel --output-on-failure
```

The `tests` directory has this structure:

- `core` contains instruction, exception, fault, power, and trace tests.
- `device` contains memory, register, interrupt, and peripheral tests.
- `system` contains firmware image and runner tests.
- `support` contains the small assertion helper.

Each ordinary test has a 60-second deadline.

Run one test group:

```
ctest --test-dir build/simulator -L core --parallel --output-on-failure
ctest --test-dir build/simulator -L device --parallel --output-on-failure
ctest --test-dir build/simulator -L system --parallel --output-on-failure
```

## Coverage

GCC and gcov measure source coverage. Configure a coverage build:

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

The target writes the reports to `build/coverage/coverage`.
Coverage measures the native model, not the target hardware.
