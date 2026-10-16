<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Driveclub-PC

A personal hobby fork of the [shadPS4](https://github.com/shadps4-emu/shadPS4) PS4 emulator, tuned
to run **Driveclub (CUSA00093, v1.28)** on Windows. It is built on
[GR2fork](https://github.com/junminlee2004/GR2fork) by junminlee2004.

**No game data, firmware or Sony code is included.** You need your own dump of Driveclub
(base game plus the 1.28 update) and the PS4 system modules from your own console.

Unofficial, and not affiliated with Sony Interactive Entertainment, Evolution Studios, the
shadPS4 team or the GR2fork author. Names and trademarks belong to their owners.

## What it changes

Compared with GR2fork, this fork adds a Driveclub launcher with settings, 30/60/120 FPS targets,
FXAA/SMAA and anisotropic filtering, stability fixes, and CPU-side speedups of the emulated GPU
command processor. Full list: [driveclub-pc/CHANGES.md](driveclub-pc/CHANGES.md).

## Performance

Test machine: ASUS ROG Zephyrus G16 (GU605CR), Intel Core Ultra 9 285H, NVIDIA GeForce RTX 5070 Ti
Laptop GPU, 32 GB RAM, Windows 11. Same race, same game settings.

| | GR2fork (shadPS4) | Driveclub-PC |
|---|---|---|
| 60 FPS target, average | ~51 FPS | ~59.6 FPS |
| 60 FPS target, frame pacing | 12–19 % of frames drop to 33 ms (30 FPS) | stable 60 |
| 120 FPS target (played by hand) | dips to ~35 FPS | 75–90 FPS |

The 60 FPS rows come from scripted test races; the 120 FPS row comes from races played by hand.
Results vary by up to ~15 % with laptop temperature. At 120 FPS the limit is the emulator's
CPU-side GPU command processing, not the graphics card (27–35 % busy).

## Building (Windows)

Requires Visual Studio 2022 Build Tools with **clang-cl**, CMake, Ninja and Git. Run from an
"x64 Native Tools" developer prompt:

```powershell
git clone --recursive https://github.com/ondrejeder/Driveclub-pc-wrapper.git
cd Driveclub-pc-wrapper
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=ON
cmake --build build --parallel
```

Release builds of `v0.1` also use profile-guided optimisation, which adds about 10 %.

## Credits

- [shadPS4](https://github.com/shadps4-emu/shadPS4): the shadPS4 Emulator Project and contributors.
  Almost all of the code here is theirs.
- [GR2fork](https://github.com/junminlee2004/GR2fork) by junminlee2004: the fork this project starts
  from (see [README-GR2fork.md](README-GR2fork.md)).
- [SMAA](https://github.com/iryoku/smaa) by Jorge Jimenez et al., MIT licence
  ([driveclub-pc/SMAA-LICENSE.txt](driveclub-pc/SMAA-LICENSE.txt)).
- AMD FidelityFX FSR 1, already part of shadPS4.
- The libraries in `externals/` (git submodules), each under its own licence.

## Licence

**GPL-2.0-or-later** ([LICENSE](LICENSE)), the same as shadPS4 and GR2fork. Some files carry
other compatible licences in their SPDX headers; the texts are in [LICENSES/](LICENSES/).
