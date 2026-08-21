# Cortex-M4 Simulator

C simulator for the Arm Cortex-M4F processor and NXP Kinetis K22 microcontroller.

## Features

- CPU Core: Armv7E-M instruction set (Thumb, Thumb-2, DSP, FPv4-SP-D16 floating-point unit).
- Core Peripherals: NVIC, SCB, SysTick, bit-band operations, MPU, breakpoints, and cycle-accurate execution.
- Kinetis K22 Peripherals: Flash/SRAM, eDMA, DMAMUX, SIM, MCG, WDOG, PIT, LPTMR, ADC, UART, SPI, I2C, USB, GPIO.

## Build

```
cmake -S . -B build/simulator -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/simulator --parallel
```

### Build Targets

| Target | Type | Description |
| :--- | :--- | :--- |
| `cortex_m4::simulator` | Static Library | Core CPU and peripheral simulator. |
| `cortex_m4::firmware_image` | Static Library | ELF and raw binary image loader. |
| `cortex_m4::firmware_runner` | Executable | CLI tool to load and run firmware images. |

## Run Firmware

```
cortex_m4_firmware_runner <IMAGE> --reset-address <ADDRESS> [OPTIONS]
```

### Runner Options

| Option | Description |
| :--- | :--- |
| `--reset-address <ADDR>` | Entry point / reset address (required). |
| `--binary-address <ADDR>` | Base address for raw binary images. |
| `--stop-address <ADDR>` | Execution stop address. |
| `--max-instructions <N>` | Maximum instruction count. |
| `--max-cycles <N>` | Maximum clock cycle limit. |

## Use in CMake Projects

```cmake
add_subdirectory(sim/cortex-m4-sim EXCLUDE_FROM_ALL)
target_link_libraries(your_target PRIVATE cortex_m4::simulator)
```

## Run Tests

Run all unit and device tests:

```
ctest --test-dir build/simulator --output-on-failure --parallel
```

Run specific test groups:

```
ctest --test-dir build/simulator -L core --output-on-failure --parallel
ctest --test-dir build/simulator -L device --output-on-failure --parallel
ctest --test-dir build/simulator -L system --output-on-failure --parallel
```
