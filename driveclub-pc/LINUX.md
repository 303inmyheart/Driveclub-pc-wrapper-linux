# Native Linux build and AppImage

This port targets **x86_64 Linux** with an AVX2-capable CPU (the existing
`x86-64-v3` target) and a Vulkan-capable graphics driver. It runs the PS4 game
through the bundled emulator; Wine and Proton are not required.

## Run the AppImage

```sh
chmod +x Driveclub-PC-*-x86_64.AppImage
./Driveclub-PC-*-x86_64.AppImage
```

Choose your own **CUSA00093** base game folder or a containing folder such as
`Drive Club`. Subfolders are searched recursively; update/mod folders are
excluded as base-game candidates. If multiple installations are found, choose
a more specific folder. The resolved base folder is saved automatically.
Keep update **1.28** next to it
as `<base folder>-UPDATE` or `<base folder>-patch`. Use **Import firmware...**
to copy `.sprx` modules from your own console dump. Existing modules are kept.
No game or firmware files are included.

**DLC folder (optional)** lets you select an external folder without copying its
contents. Both `addcont/CUSA00093/<DLC>/sce_sys/param.sfo` and a directly chosen
folder containing `<DLC>/sce_sys/param.sfo` are supported. The launcher counts
matching unpacked DLC folders; PKG files must be unpacked first. The selected
path is saved in `config.json` as `General.addon_install_dir`.

If the machine has no FUSE support, run with
`APPIMAGE_EXTRACT_AND_RUN=1 ./Driveclub-PC-…-x86_64.AppImage`.

The launcher supports keyboard navigation, folder selection, 30/60/120 FPS,
windowed/borderless display, render resolution, ultrawide, FSR, sharpening,
FXAA/SMAA, anisotropic filtering and shader compilation options.
Use `./Driveclub-PC-…-x86_64.AppImage --launcher` to reopen settings after
disabling the startup window.

## Settings, saves and caches

Data lives in `$XDG_DATA_HOME/driveclub-pc`, defaulting to
`~/.local/share/driveclub-pc`. It is separate from other shadPS4 installations
and survives replacing or moving the AppImage. The launcher displays the
active data directory. To choose another location:

```sh
DCPC_USER_DIR="/path/to/driveclub-data" ./Driveclub-PC-…-x86_64.AppImage
```

The directory contains `driveclub_pc.ini`, `config.json`, per-game settings,
`sys_modules`, saves, shader caches and logs. Do not put it inside an extracted
AppImage's `usr` directory. Existing Windows settings can be copied manually,
but game and firmware paths must be adjusted for Linux.

## Build

### GitHub Actions

The [Linux AppImage workflow](https://github.com/303inmyheart/Driveclub-pc-wrapper-linux/actions/workflows/linux-appimage.yml)
runs for pushes to `main` / `linux-appimage`, pull requests to `main`, and manual
**Run workflow** requests. It builds on Ubuntu 24.04, runs the content-path
regression tests, validates desktop metadata, and checks the packaged `--help`
command and checksum. It does not run the game.

Open a successful run and download **Driveclub-PC-Linux-x86_64-…** under
**Artifacts**. The download contains the AppImage, its SHA-256 checksum and
`BUILD-INFO.txt` linking the exact source commit. Extract it and make the
AppImage executable with `chmod +x Driveclub-PC-*.AppImage`. Artifacts are
retained for 30 days; a new build can be started manually.

### Local build

On Ubuntu 24.04 or a compatible distribution, install the compiler and headers:

```sh
sudo apt-get install build-essential clang-19 lld-19 ninja-build cmake git curl \
  pkg-config libasound2-dev libpulse-dev libudev-dev libevdev-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
  libxkbcommon-dev libwayland-dev libdecor-0-dev libdrm-dev libgbm-dev \
  libegl1-mesa-dev libgl1-mesa-dev libusb-1.0-0-dev uuid-dev libssl-dev \
  libxtst-dev wayland-protocols patchelf squashfs-tools
./tools/init-linux-submodules.sh
./tools/build-appimage.sh
```

Use **CMake 3.30 or newer** for the supplied presets. The build script configures
CMake directly. `BUILD_JOBS` defaults to 4 to limit peak memory use.
`CC`, `CXX`, `BUILD_DIR`, `OUTPUT_DIR`, `TOOLS_DIR` and `VERSION` can be overridden;
additional script arguments are passed to CMake. The packaging tools download
into `.tools` and are verified against pinned SHA-256 hashes. If upstream moves
its `continuous` assets, update the URLs/hashes together after verification.

Output is `dist/Driveclub-PC-<version>-x86_64.AppImage` plus a checksum. Packaging
requires a fresh `<BUILD_DIR>/AppDir`; move previous staging trees aside before
repackaging. Nothing from the user's game or data directories is packaged.

The AppImage bundles application dependencies and the build's C++ runtime;
glibc, the display server and graphics drivers are supplied by the host.
Build on the oldest supported distribution. An AppImage built on Ubuntu 24.04
does not promise support for older glibc versions.

## Validation scope

A successful build, help command and settings-window test do **not** establish
gameplay compatibility. Test real races, timing, audio, controller input,
readbacks and shader compilation on the intended GPU/driver. Windows-only
P-core pinning and experimental readback redirection are not enabled on Linux;
the existing Linux thread and memory implementations remain in use.
