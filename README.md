<!--
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Driveclub-PC — Linux AppImage

[![Linux AppImage](https://github.com/303inmyheart/Driveclub-pc-wrapper-linux/actions/workflows/linux-appimage.yml/badge.svg)](https://github.com/303inmyheart/Driveclub-pc-wrapper-linux/actions/workflows/linux-appimage.yml)

**Linux port created by Codex (OpenAI), at the request of 303inmyheart.**
This repository is maintained by [303inmyheart](https://github.com/303inmyheart).
Codex's creator credit applies specifically to the Linux port, native Linux
launcher adaptation, AppImage packaging and Linux build workflow.

**This is a Linux-port fork of [Driveclub-PC by ondrejeder](https://github.com/ondrejeder/Driveclub-pc-wrapper).**
The original Driveclub wrapper, its game-specific improvements and the underlying
emulator are the work of their respective upstream authors. This fork focuses on
bringing that existing work to Linux; it does not claim authorship of the original
project. Full credit for Driveclub-PC goes to **ondrejeder**, with foundational
credit to **junminlee2004 / GR2fork** and the **shadPS4 team and contributors**.

The application runs the PS4 version of Driveclub through the bundled emulator
natively on Linux. Wine and Proton are not required. This does not convert the
game itself into a native Linux game.

## Download and requirements

Download the `.AppImage` and its `.sha256` file from
[GitHub Releases](https://github.com/303inmyheart/Driveclub-pc-wrapper-linux/releases).
Preview releases are intended for local testing.

- x86_64 Linux with an AVX2-capable CPU (x86-64-v3).
- Ubuntu 24.04 or a compatible distribution with glibc 2.39 or newer for the published builds.
- A Vulkan-capable GPU and working graphics drivers.
- Your own **Driveclub CUSA00093** base-game dump with **update 1.28**, and PS4 system modules from your own console.

**No game data, DLC, firmware or Sony code is included.**
This is an unofficial project, unaffiliated with Sony Interactive Entertainment,
Evolution Studios or the upstream emulator developers. Names and trademarks
belong to their owners.

## Run on Linux

In the download directory, verify the checksum, make the AppImage executable,
and run it (replace `<build>` with the downloaded filename's version):

```sh
sha256sum --check Driveclub-PC-<build>-x86_64.AppImage.sha256
chmod +x Driveclub-PC-<build>-x86_64.AppImage
./Driveclub-PC-<build>-x86_64.AppImage
```

In the launcher:

1. Choose the **game folder**. You can select the base-game folder directly or a
   containing folder such as `Drive Club`; subfolders are searched recursively.
   If several installations are found, select a more specific folder.
2. Keep update **1.28** next to the resolved base-game folder, named
   `<base folder>-patch` or `<base folder>-UPDATE`.
3. Use **Import firmware...** to import `.sprx` system modules from your console dump.
4. Optionally select your own **DLC folder**. Both an `addcont` root containing
   `CUSA00093/<DLC>/sce_sys/param.sfo` and a folder directly containing
   `<DLC>/sce_sys/param.sfo` are supported. DLC stays in the chosen location;
   PKG files must be unpacked first.
5. Choose graphics, display and frame-rate settings, then press **Play**.

Example game layout:

```text
Drive Club/
├── CUSA00093/
│   ├── eboot.bin
│   └── sce_sys/param.sfo
└── CUSA00093-patch/
    ├── eboot.bin
    └── sce_sys/param.sfo
```

To reopen settings after disabling the startup window, launch with `--launcher`.
If FUSE is unavailable, use `APPIMAGE_EXTRACT_AND_RUN=1` before the AppImage command.
Gamepad input uses SDL, inherited from shadPS4/GR2fork.

## Settings and saved data

Linux settings, saves, shader caches, imported firmware and logs are stored in
`$XDG_DATA_HOME/driveclub-pc`, normally `~/.local/share/driveclub-pc`.
They survive replacing the AppImage. The launcher shows the active data directory.
To choose another location:

```sh
DCPC_USER_DIR="/path/to/driveclub-data" ./Driveclub-PC-<build>-x86_64.AppImage
```

Game and DLC directories are selected independently in the launcher.
The last-run log is inside the data directory at `log/shad_log.txt`.

## Scope and validation

This fork adds the Linux launcher adaptation, recursive game discovery,
configurable external DLC paths, Linux data-directory handling, AppImage packaging
and GitHub Actions builds. The Driveclub-specific emulation, rendering and timing
improvements originate upstream; see the
[original README](README-UPSTREAM.md) and [upstream change list](driveclub-pc/CHANGES.md).
The original README is preserved as historical upstream documentation and describes
the Windows version, including its Windows performance results.

The Linux build targets **CUSA00093 with update 1.28**. Successful CI checks cover
content-path regression tests, desktop metadata, packaged `--help` startup and
SHA-256 checksums. **Actual Linux gameplay, DLC gameplay and GPU rendering have
not yet been verified.** Upstream Windows benchmarks are not Linux performance claims.

## Build and technical documentation

See [Native Linux build and AppImage](driveclub-pc/LINUX.md) for dependencies,
local build commands, configuration and validation details.

The [Linux AppImage workflow](https://github.com/303inmyheart/Driveclub-pc-wrapper-linux/actions/workflows/linux-appimage.yml)
builds on Ubuntu 24.04. Successful runs provide an AppImage, checksum and
`BUILD-INFO.txt` identifying the exact source commit. Version tags also trigger
release builds. Development artifacts are available for 30 days; published release
assets are available from the Releases page.

## Credits and project lineage

- **[ondrejeder — Driveclub-PC](https://github.com/ondrejeder/Driveclub-pc-wrapper): original creator of the Driveclub wrapper and its Driveclub-specific launcher, optimisations and fixes. This is the direct upstream project.**
- **[junminlee2004 — GR2fork](https://github.com/junminlee2004/GR2fork):** the emulator fork on which Driveclub-PC is built; see [README-GR2fork.md](README-GR2fork.md).
- **[shadPS4 Emulator Project and contributors](https://github.com/shadps4-emu/shadPS4):** the underlying PS4 emulator and the vast majority of the codebase.
- **Codex (OpenAI):** creator of this fork's Linux port, Linux launcher adaptation, AppImage packaging and Linux CI workflow, working at 303inmyheart's request.
- **[303inmyheart](https://github.com/303inmyheart):** Linux fork owner and maintainer.
- [rafidwayne — Driveclub_Widescreen](https://github.com/rafidwayne/Driveclub_Widescreen), GPL-2.0-or-later: the aspect-ratio, video-out width and FOV values used as the starting point for upstream ultrawide support.
- [SMAA](https://github.com/iryoku/smaa) by Jorge Jimenez et al., MIT licence ([licence text](driveclub-pc/SMAA-LICENSE.txt)).
- AMD FidelityFX FSR 1, already part of shadPS4.
- The libraries in `externals/`, each under its own licence.

## Licence

**GPL-2.0-or-later** ([LICENSE](LICENSE)), inherited from shadPS4, GR2fork and
Driveclub-PC. Files with other compatible SPDX licences retain those licences;
see [LICENSES/](LICENSES/). Linux-port attribution does not replace upstream
copyright notices or licence terms.
