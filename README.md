# Cortex-M4 Simulator

This repository provides a native C simulator for the Arm Cortex-M4F processor.
It also provides a Kinetis K22 device model.

The CPU model implements Armv7E-M and FPv4-SP-D16 behavior. The K22 model
provides six device profiles and package-specific features.

The exact target package and silicon revision are not known. Thus, the
simulator does not claim behavior that depends on this identification.

## Supported behavior

The CPU supports Thumb, Thumb-2, DSP, and floating-point instructions. It also
supports exceptions, interrupts, faults, sleep states, the MPU, and debug units.

The system model includes the NVIC, SCB, SysTick, bit-band access, reset,
breakpoints, instruction tracing, and bounded execution.

The K22 model includes these main interfaces:

- Program flash, SRAM, FlexNVM, FlexRAM, and FlexBus memory
- FTFA, FTFE, FMC, eDMA, DMAMUX, AIPS, AXBS, and SYSMPU
- SIM, MCG, OSC, SMC, PMC, LLWU, RCM, RTC, and RFVBAT
- PIT, LPTMR, PDB, FTM, CMT, watchdog, and EWM timers
- ADC, DAC, CMP, VREF, RNG, and CRC data units
- UART, LPUART, SPI, I2C, SDHC, USB, CAN, and I2S interfaces
- PORT, GPIO, interrupt, DMA-request, reset, and clock routing

The register model rejects unknown addresses and unsupported access widths. The
profile and package models reject invalid device combinations.

Hardware tests must check electrical timing, analog tolerances, clock accuracy,
and silicon-specific behavior. The model does not replace these hardware tests.

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

CTest runs 55 isolated native C executables. No external test framework is
necessary.

Run all tests:

```
ctest --test-dir build/simulator --parallel --output-on-failure
```

The `tests` directory has this structure:

- `core` contains instruction, exception, fault, power, and trace tests.
- `device` contains memory, register, interrupt, and peripheral tests.
- `system` contains firmware image tests.
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

Create the gcov reports and apply the coverage limits:

```
cmake --build build/coverage --target cortex_m4_coverage_check
```

The target writes the reports to `build/coverage/coverage`.
The gate requires 99.80% line coverage and 99.00% branch-site coverage.
Coverage measures the native model. It does not measure the target hardware.

## Mutation checks

The mutation checks change seven critical behaviors in temporary source copies.
The related tests must reject each change.

Run the mutation checks:

```
cmake --build build/simulator --target cortex_m4_mutation_check
```

The checks cover these behaviors:

- IT conditions
- Flash reset state
- Flash command register layout
- FlexNVM partition codes
- Flash protection ranges
- SYSMPU access control
