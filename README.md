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

## Tested version

Only **Driveclub CUSA00093 with update 1.28** has been tested. Other regions or versions may not work.

## How to use

1. Download the release zip and extract it into its own folder.
2. Get your dumps ready:
   - the base game folder (`CUSA00093`), and next to it the 1.28 update folder named
     `<base folder>-patch` or `<base folder>-UPDATE`;
   - the PS4 firmware modules (`sys_modules`) from your own console.
3. Run `Driveclub-PC.exe`. A settings window opens:

| Setting | What to choose |
|---|---|
| Game folder | the base game folder (`CUSA00093`) |
| Firmware | your `sys_modules` folder. **Import...** copies it into `user\sys_modules`, so the original can move later |
| Frame rate | 30 / **60 (recommended)** / 120 (experimental, depends on scene and CPU temperature) |
| Display / Window size | borderless fullscreen or window; the window size only applies in windowed mode (fullscreen always covers the display) |
| Render resolution | **1920x1080 (native)** / 2560x1440 / 3840x2160. The game renders its scene at this size (the engine's own scaling; extra video memory is set up automatically). The picture stays 16:9, with black bars on other aspect ratios. 1440p costs about 10–15 % FPS; 4K is very demanding |
| Upscaling / Sharpening | AMD FSR 1 or bilinear; RCAS sharpening off / normal / strong |
| Anisotropic filter | 16x by default; Off keeps the game's own setting |
| Anti-aliasing | Off / **FXAA (default)** / SMAA 1x High |
| Prefer performance cores | on by default; keeps the busiest threads on P-cores of hybrid Intel CPUs |
| Compile shaders in the background | on by default: new shaders and pipelines are built off the main thread, so first-time content does not freeze the game (an object may appear a few frames late). Turn off if you see graphics errors |
| GPU readbacks | **Precise (default)**. Relaxed is faster but causes shadow and vegetation glitches; Off makes the 3D world nearly black |
| Classic PS4 frame pacing | fallback: only turn it on if game speed feels wrong |
| Show this window at start | if turned off, the game starts directly; hold **Shift** while launching to open the window again |

4. Press **Play**. Before the game boots, the launcher checks the folder, the title ID, the update
   version and the firmware, and explains any problem it finds.

The game's own clock runs in real time: race times are correct at 120 FPS as well, and physics don't speed up.

**Known issue:** with an unmodified 1.28 executable the game can run too fast at the 60 FPS target.
A fix is planned for the next release. Ultrawide (21:9) output is not supported yet.

**Data:** settings, saves, shader cache and logs are all kept in the `user` folder next to the
executable. Keep that folder when you update to a new release. The log of the last run is
`user\log\shad_log.txt`.

**Controls:** gamepad through SDL, the same as shadPS4/GR2fork.

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
| 120 FPS target, 2560x1440 render (v0.2, played by hand) | – | 65–80 FPS |

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

Release builds also use profile-guided optimisation, which adds about 10 %.

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
