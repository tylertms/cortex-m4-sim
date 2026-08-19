# K22 register test data

This directory contains the frozen register data for the device tests.
The tests do not read register data from the simulator source.

The primary sources are these NXP device-family packs:

| Device family | NXP pack | SHA-256 |
|---|---|---|
| MK22F12810 | [NXP.MK22F12810_DFP.25.06.00.pack](https://mcuxpresso.nxp.com/cmsis_pack/repo/NXP.MK22F12810_DFP.25.06.00.pack) | `56c3b7b8ab7cc9ba540355ebdd5d9cebbeaa16b86961a64e5ffa4cac4e21441a` |
| MK22F25612 | [NXP.MK22F25612_DFP.25.06.00.pack](https://mcuxpresso.nxp.com/cmsis_pack/repo/NXP.MK22F25612_DFP.25.06.00.pack) | `c7a9e4da2c61f0a3f9514db635171695478476e51f567f28eadd990619adaaa5` |
| MK22F51212 | [NXP.MK22F51212_DFP.25.06.00.pack](https://mcuxpresso.nxp.com/cmsis_pack/repo/NXP.MK22F51212_DFP.25.06.00.pack) | `8de23acd0902c5562d10c286a23b31def7c86d5c71c55bd32619fc421ff65497` |

The MK22F12 data also uses the official `MK22F12.h` device header.
The header comes from [NXP legacy MCUX SDK commit `8a289764d763ad06e0c3a05c885644ed98b970af`](https://github.com/nxp-mcuxpresso/mcux-sdk/tree/8a289764d763ad06e0c3a05c885644ed98b970af).
Its SHA-256 value is `0f8289e0c2caecb08cb5e792af7118183a331951063977909e6d5aad24b46697`.

The files are an independent test oracle.
Do not make them from `src/k22_register_manifest.c`.
Update them only after a change to an official NXP source.
Record the new source version and SHA-256 value in this file.
