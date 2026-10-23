# Build the Linux AppImage locally

A local build uses your own computer. GitHub Actions, build minutes and a paid
GitHub plan are not required. The commands below follow the Ubuntu 24.04 build
baseline; on other distributions install equivalent development packages.

## Requirements

- An x86_64 Linux build host, Git and an Internet connection for dependencies
  and pinned AppImage packaging tools.
- Clang 19, GCC 14 C++ headers/runtime, LLD 19, Ninja and CMake 3.31.10.
- The development libraries listed below. Use two parallel jobs initially to
  limit memory pressure; a first build compiles the bundled dependencies too.
- Build outputs target x86-64-v3 / AVX2. The published release requires glibc
  2.39 or newer; building on a newer distribution can raise that requirement.

No game dump, firmware, DLC, Vulkan GPU or graphical session is needed to
compile/package. Running the game requires your own content and suitable GPU.

## 1. Install build dependencies

On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install build-essential g++-14 clang-19 lld-19 ninja-build git curl \
  pkg-config python3-venv libasound2-dev libpulse-dev libudev-dev libevdev-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
  libxtst-dev libxkbcommon-dev libxkbcommon-x11-dev libwayland-dev \
  wayland-protocols libdecor-0-dev libdrm-dev libgbm-dev libdbus-1-dev \
  libegl1-mesa-dev libgl1-mesa-dev libusb-1.0-0-dev uuid-dev libssl-dev \
  patchelf squashfs-tools desktop-file-utils appstream
```

## 2. Get the source and CMake

```sh
git clone https://github.com/303inmyheart/Driveclub-pc-wrapper-linux.git
cd Driveclub-pc-wrapper-linux
git checkout v0.2.3
mkdir -p .tools
python3 -m venv .tools/build-env
.tools/build-env/bin/pip install cmake==3.31.10
export PATH="$PWD/.tools/build-env/bin:$PATH"
./tools/init-linux-submodules.sh
```

Use the targeted submodule script rather than recursively cloning every nested
upstream dependency. To build development code, omit `git checkout v0.2.3`.

## 3. Check and build

```sh
CXX=g++-14 ./tools/test-content-paths.sh
CXX=g++-14 ./tools/test-linux-cpu-topology.sh
CXX=g++-14 ./tools/test-recorder-wake.sh
desktop-file-validate packaging/linux/io.github.driveclub_pc.desktop
appstreamcli validate --no-net packaging/linux/io.github.driveclub_pc.metainfo.xml

CC=clang-19 CXX=clang++-19 BUILD_JOBS=2 VERSION=0.2.3 \
  ./tools/build-appimage.sh \
  -DCMAKE_C_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/14 \
  -DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/14 \
  -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld
```

The script builds Release code with Discord RPC support compiled in (the launcher
switch defaults to off) and the updater disabled. It
fetches linuxdeploy/appimagetool into `.tools`, verifies pinned hashes, stages
libraries and licence notices, and packages only application files.

Outputs:

```text
dist/Driveclub-PC-0.2.3-x86_64.AppImage
dist/Driveclub-PC-0.2.3-x86_64.AppImage.sha256
```

Build intermediates live in `build/linux-release/`. `BUILD_JOBS`, `VERSION`,
`BUILD_DIR`, `OUTPUT_DIR`, `TOOLS_DIR`, `CC` and `CXX` can be overridden. Other
arguments to the build script are passed to CMake. When changing compiler or
base distribution, choose a fresh build directory.

## 4. Verify and run

```sh
cd dist
sha256sum --check Driveclub-PC-0.2.3-x86_64.AppImage.sha256
chmod +x Driveclub-PC-0.2.3-x86_64.AppImage
APPIMAGE_EXTRACT_AND_RUN=1 ./Driveclub-PC-0.2.3-x86_64.AppImage --help
./Driveclub-PC-0.2.3-x86_64.AppImage --launcher
```

If FUSE is unavailable, prefix the last command with
`APPIMAGE_EXTRACT_AND_RUN=1` too. The launcher lets you choose game and DLC
paths and import firmware. Runtime data is separate from the build directory;
see [LINUX.md](LINUX.md).

## Rebuild or package again

Incremental compilation reuses `build/linux-release`. Packaging deliberately
requires a fresh `build/linux-release/AppDir`. If it already exists, move it
aside before repeating step 3:

```sh
mv build/linux-release/AppDir build/linux-release/AppDir.previous
```

Choose another unused archive name if `.previous` already exists. Do not move
or delete your runtime data directory to rebuild. A successful build and help
check do not replace gameplay tests.

## Upload a locally built release

An authenticated maintainer can upload the AppImage/checksum directly with
`gh release create`; GitHub does not compile uploaded assets. This repository
also has an automatic version-tag build workflow. To avoid spending Actions
minutes when publishing locally, pause that workflow for the source/tag push,
publish and verify the local assets, then restore its previous state. Include
source commit, compiler, glibc baseline and validation scope in build information.
