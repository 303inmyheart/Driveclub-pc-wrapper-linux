<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Driveclub-PC

A personal research fork of the [shadPS4](https://github.com/shadps4-emu/shadPS4) PlayStation 4
emulator. It is tuned to run **Driveclub (CUSA00093, v1.28)** on Windows. It is built on top of
[GR2fork](https://github.com/junminlee2004/GR2fork), a shadPS4 fork by junminlee2004.

> **This repository contains no game data, no PS4 firmware and no copyrighted Sony code.**
> To play you need your own legally dumped copy of Driveclub (base game + update) and the
> PS4 system modules dumped from your own console. Please don't ask for them; requests will be ignored.

This is an unofficial hobby project. It is not affiliated with or endorsed by Sony Interactive
Entertainment, Evolution Studios, the shadPS4 team or the GR2fork author. "PlayStation", "PS4" and
"Driveclub" are trademarks of their respective owners.

## Credits

- **[shadPS4](https://github.com/shadps4-emu/shadPS4)**: the emulator this project is built on, by the
  shadPS4 Emulator Project and its contributors. Nearly all of the code here is theirs.
- **[GR2fork](https://github.com/junminlee2004/GR2fork)** by junminlee2004: the shadPS4 fork this branch
  starts from. Its original README is kept as [README-GR2fork.md](README-GR2fork.md).
- **[SMAA](https://github.com/iryoku/smaa)** by Jorge Jimenez et al. (MIT), used for the optional
  SMAA anti-aliasing. See [driveclub-pc/SMAA-LICENSE.txt](driveclub-pc/SMAA-LICENSE.txt).
- **FXAA** (NVIDIA, Timothy Lottes) and **AMD FidelityFX FSR 1**, as included in the code they came with.
- All third-party libraries under `externals/` (git submodules), each under its own licence.

## What this fork changes

All Driveclub-specific changes are on branch `driveclub-pc` and marked with `driveclub-pc:`
comments. The full list is in [driveclub-pc/docs/runtime-fork.md](driveclub-pc/docs/runtime-fork.md).
Highlights:

- Frame-rate targets of 30 / 60 / 120 FPS, with a stable ~60 FPS on the test machine.
- Command-processor speedups: a fast guest mutex path, queue submits on a separate thread, and P-core pinning.
- GPU readback modes: Precise (default), Relaxed and Off.
- Post-process AA (Off / FXAA / SMAA) applied before FSR 1, plus an anisotropic filtering override.
- A small launcher / settings window, and diagnostic tools (frame profiler, Vulkan and HLE call statistics, autopilot).
- Gravity Rush 2 photo mode and online features from GR2fork are removed.

Measurements, including changes that did **not** help, are in
[driveclub-pc/docs/benchmarks.md](driveclub-pc/docs/benchmarks.md). Results come from a single
laptop (RTX 5070 Ti Laptop GPU). Runs vary by up to ~15 % with temperature.

## Building (Windows)

You need Visual Studio 2022 Build Tools with **clang-cl**, CMake, Ninja and Git.

```powershell
git clone --recursive https://github.com/ondrejeder/Driveclub-pc-wrapper.git
cd Driveclub-pc-wrapper
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=ON
cmake --build build --parallel
```

Run the configure step from a "x64 Native Tools" developer prompt. Use Release (optionally with LTO).
RelWithDebInfo **without** LTO is known to crash in Driveclub.

General shadPS4 build documentation is in [documents/](documents/).

## Licence

This project is licensed under the **GNU General Public License v2.0 or later**
([LICENSE](LICENSE), SPDX `GPL-2.0-or-later`), the same as shadPS4 and GR2fork. Some files are
under other compatible licences, stated in their SPDX headers; the licence texts are in
[LICENSES/](LICENSES/). Submodules under `externals/` keep their own licences.

If you distribute binaries built from this repository, the GPL requires you to also offer the
corresponding source code.
