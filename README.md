# Kinetis K22 Simulator

C simulator for the 100 MHz and 120 MHz NXP Kinetis K22F microcontroller families and their Arm Cortex-M4 cores with FPv4-SP-D16 floating-point units.

## Features

- CPU Core: Armv7E-M instruction set (Thumb, Thumb-2, DSP, FPv4-SP-D16 floating-point unit).
- Core Peripherals: NVIC, SCB, SysTick, bit-band operations, MPU, breakpoints, and cycle-accurate execution.
- Kinetis K22 Peripherals: Flash/SRAM, eDMA, DMAMUX, SIM, MCG, WDOG, PIT, LPTMR, ADC, UART, SPI, I2C, USB, GPIO.
- Device Profiles: MK22F12810, MK22FN12812, MK22FN25612, MK22FN51212, MK22FN1M012, and MK22FX51212.
- Device Packages: AK, LH, MP, AH, LK, AP, BP, FX, LL, DC, MC, LQ, and MD variants supported by each profile.

The 50 MHz K22D5 devices use Cortex-M4 cores without floating-point units and a different peripheral map. They are outside this K22F simulator's scope.

## Layout

- `include/` contains the public Cortex-M4F, K22, and firmware-image APIs.
- `src/core/` contains the Armv7E-M processor implementation.
- `src/device/` contains K22 profiles, register manifests, and peripheral domains.
- `src/image/` and `src/runner/` contain firmware loading and the command-line runner.
- `tests/` mirrors the core, device, and system boundaries.

## Build

```
cmake -S . -B build/simulator -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/simulator --parallel
```

### Build Targets

| Target | Type | Description |
| :--- | :--- | :--- |
| `kinetis_k22::simulator` | Static Library | K22 device and Cortex-M4F core simulator. |
| `kinetis_k22::firmware_image` | Static Library | ELF and raw binary image loader. |
| `kinetis_k22::firmware_runner` | Executable | CLI tool to load and run firmware images. |

## Run Firmware

```
kinetis_k22_firmware_runner <IMAGE> --reset-address <ADDRESS> [OPTIONS]
```

### Runner Options

| Option | Description |
| :--- | :--- |
| `--reset-address <ADDR>` | Entry point / reset address (required). |
| `--profile <DEVICE>` | K22 device profile. Defaults to `MK22FN51212`. |
| `--package <CODE>` | Package code such as `LH`, `LL`, or `LQ`. |
| `--binary-address <ADDR>` | Base address for raw binary images. |
| `--stop-address <ADDR>` | Execution stop address. |
| `--max-instructions <N>` | Maximum instruction count. |
| `--max-cycles <N>` | Maximum clock cycle limit. |

## Use in CMake Projects

```cmake
add_subdirectory(sim/kinetis-k22-sim EXCLUDE_FROM_ALL)
target_link_libraries(your_target PRIVATE kinetis_k22::simulator)
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
