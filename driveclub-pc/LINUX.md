# Native Linux build and AppImage

This port targets **x86_64 Linux** with an AVX2-capable CPU (the existing
`x86-64-v3` target) and a Vulkan-capable graphics driver. It runs the PS4 game
through the bundled emulator; Wine and Proton are not required.

## Profile and Discord settings (development builds)

Development builds after 0.2.4 Experimental 1 add a **PS4 username** field for the
existing player-1 local profile and a **Discord Rich Presence** switch. The name
is stored in `users.json`; renaming preserves the user ID, other profiles and save
directories. Names must be nonblank valid UTF-8 of at most 16 bytes, matching the
emulated user-service buffer. This does not change a PSN/ShadNet login; an online
session may expose its online ID instead of the local display name.

Discord RPC is compiled into new AppImages but stays off by default. Enable it
in the launcher and use a running Discord client to publish the current game.
The saved launcher choice also applies after loading per-game settings. Builds
explicitly compiled with `-DENABLE_DISCORD_RPC=OFF` show the switch as unavailable.
Profile/RPC edits are applied with **Save and close** or **Play** and take effect
on the next game start. Current public pre-releases do not gain these options
until an updated build is published.

## Distribution requirements

The AppImage is **not restricted to Ubuntu or Ubuntu-based distributions**.
Published builds are built on Ubuntu 24.04 and require **glibc 2.39 or newer**,
an **x86_64-v3 / AVX2-capable CPU**, a working X11 or Wayland desktop, and
Vulkan graphics drivers supplied by the host. Other distributions can work
when these requirements are met, but cross-distribution operation has not yet
been verified. Systems using musl instead of glibc are not supported by this build.

Check the installed glibc version with `getconf GNU_LIBC_VERSION`.
The AppImage bundles most application libraries and the C++ runtime; it does
not bundle glibc or graphics drivers. FUSE is needed for normal mounting, or
use `APPIMAGE_EXTRACT_AND_RUN=1` as shown below. Extraction mode does not remove
the other system requirements.

## Run the AppImage

```sh
chmod +x Driveclub-PC-*-x86_64.AppImage
./Driveclub-PC-*-x86_64.AppImage
```

Choose your own Driveclub base game folder or a containing folder such as
`Drive Club`. Subfolders are searched recursively; update/mod folders are
excluded as base-game candidates. If multiple installations are found, choose
a more specific folder. The resolved base folder is saved automatically.
Keep the matching update next to it as `<base folder>-UPDATE` or `<base folder>-patch`. Use **Import firmware...**
to copy `.sprx` modules from your own console dump. Existing modules are kept.
No game or firmware files are included.

**DLC folder (optional)** lets you select an external folder without copying its
contents. Both `addcont/<selected title ID>/<DLC>/sce_sys/param.sfo` and a directly chosen
folder containing `<DLC>/sce_sys/param.sfo` are supported. The launcher counts
matching unpacked DLC folders; PKG files must be unpacked first. The selected
path is saved in `config.json` as `General.addon_install_dir`.

If the machine has no FUSE support, run with
`APPIMAGE_EXTRACT_AND_RUN=1 ./Driveclub-PC-…-x86_64.AppImage`.

**CUSA00093 / update 1.28 is the tested reference. Other CUSA IDs and versions are
EXPERIMENTAL**, with a persistent highlighted launcher warning. The selected ID is
used for per-game settings and DLC filtering. Updates must carry the same title ID
as the base game. FPS, render-size and ultrawide patches are attempted for other
versions too after expected-byte checks. A mismatching resolution patch group is
skipped without partial writes; unsupported offsets are not searched/relocated.
Correct timing and compatibility on other versions are not established.

The launcher supports keyboard navigation, folder selection, 30/60/120 FPS,
windowed/borderless display, 720p/1080p/1440p/2160p render resolution, ultrawide, FSR, sharpening,
FXAA/SMAA, anisotropic filtering and shader compilation options.
Use `./Driveclub-PC-…-x86_64.AppImage --launcher` to reopen settings after
disabling the startup window.

The Linux launcher exposes **Presentation**: Immediate (the mode used in local
race tests; may show tearing), Mailbox (VSync), or FIFO (VSync). Unsupported
modes fall back to FIFO and are reported in the log. Settings default to
Immediate in this Linux port. Readback prefetch, parallel Vulkan recording and
empty-queue skipping remain off by default; normal use needs no profiling script.

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

See [BUILD-LINUX.md](BUILD-LINUX.md) for the complete tested-baseline setup,
compiler flags, checks, packaging and rebuild instructions.

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
readbacks and shader compilation on the intended GPU/driver. Experimental readback redirection remains Windows-only. On Linux Intel hybrid
CPUs, the port uses the kernel's `cpu_core` / `cpu_atom` PMU CPU lists to identify
P-cores even when `topology/core_type` is absent. Graphics core reservation and
critical-thread affinity respect the inherited CPU affinity. This prevents the
command processor from being reserved on a low-power E-core. Linux affinity
changes require no privileged scheduler settings.

## Local performance investigation

`tools/profile-linux.py` runs an existing AppImage against a private copy of your
settings, caches and saves. Console modules are referenced from their original
folder. Results are written under `~/Downloads/Driveclub-FPS-Tests/`.

```sh
python3 tools/profile-linux.py immediate --appimage /path/to/Driveclub-PC.AppImage
```

`--low-resolution` requests diagnostic 720p rendering. With a build containing
the Linux queue/recorder changes, `--experiment` alternates direct and parallel Vulkan recording every ten seconds while
queue skipping and prefetch remain off. Run the same race for at least 120
seconds and close normally. `SUMMARY.txt` reports the last 120 seconds and
separates recording phases, excluding the first second
after each switch. This is a diagnostic comparison, not a promised speedup.

The optional command-queue optimisation skips queues whose release-published
pending count is zero. It is off by default. `DCPC_CP_EMPTY_SKIP=0` restores the original polling path; `1` enables
skipping; `2` alternates it every ten seconds (profiler phases 0/1). Do not
combine multiple alternating experiments: they share the profiler phase field.
The optional Vulkan recorder uses producer notifications on Linux instead of
100-microsecond polling sleeps. It remains disabled by default until race tests
establish a benefit and correct rendering.

`--readback-experiment` instead tests the existing precise-readback prefetch path:
CPU affinity stays fixed, parallel Vulkan recording and empty-queue skipping are
off, and prefetch alternates every ten seconds. The launcher uses 1080p and
Precise readbacks in the private test data. Run the same race for 120 seconds;
the report separates prefetch-on/off FPS and inclusive readback waiting times.
This mode does not require another AppImage build: the CPU-fix AppImage already
contains the opt-in prefetch implementation. Do not enable it by default until
both performance and rendering have been checked on real races.

For independent-process readback comparisons, use `--prefetch off` and
`--prefetch on` in separate runs against the same saved source snapshot.
Each run fixes 1080p, Precise readbacks and disables alternating experiments.
The local comparison reached 59.75 / 59.90 FPS respectively, so prefetch has
not demonstrated a meaningful FPS gain and is not enabled in normal builds.

### Frame-time investigation

`python3 tools/frame_times.py /path/to/frame_profile.csv` reports nearest-rank
p50/p95/p99/p99.9 frame durations and counts above 25, 33.333 and 50 ms. The
normal window is the last 60 seconds; change it with `--seconds`. Partial or
non-finite frame records are excluded. Inclusive CPU/GPU timing scopes overlap;
these columns do not measure hardware utilisation.

`tools/profile-linux.py immediate --queue-experiment --appimage /path/to/image`
alternates empty-command-queue skipping every ten seconds, holding prefetch and
parallel recording off. Test a demanding race for at least two minutes and
close directly from gameplay. This comparison uses the final 120 seconds,
excludes the first second after each phase change and writes per-phase frame
percentiles plus `LONGEST-FRAMES.csv`. Scene progression and rare spikes can
still affect a single A/B run; check repeatability before enabling a default.

720p uses `-resolutionscale 0.666667` for the 3D scene only. Menu/HUD output
stays at native 1920x1080 at 16:9; ultrawide follows the display aspect at native
1080 output lines. The original experiment that also shrank 19 output/viewport
sites to 1280x720 cropped menus and was reported unstable; that path is no longer
used for the 720p launcher setting. Changing only window size does not lower
internal rendering resolution. The tester reported that the corrected scene-only
path works; wider menu/race coverage and other versions remain experimental.
Tests: `./tools/test-game-profile.sh` and
`python3 tools/test-launch-validation.py --binary /path/to/Driveclub-PC`.
The launch fixtures intentionally stop at missing firmware and execute no game.

`--telemetry` additionally writes `SYSTEM-TELEMETRY.jsonl`: emulator thread CPU
load/affinity, CPU frequencies/temperature and optional NVIDIA GPU clocks,
utilisation, VRAM and power data. Sampling is read-only and follows only the
launched process tree. CPU percent is per logical core (100% means one core),
not total-machine utilisation. Missing tools or unreadable sensors are recorded
without changing power settings. Monotonic timestamps align approximately with
the frame profiler. The affinity walker preserves its registered GPU owner even
when that owner uses a wide P-core mask.

Development builds after 0.2.3 leave all CPU cores shared on Linux hosts with four
or fewer available physical cores, including 4c/8t handhelds. Counting SMT threads
as spare cores previously caused the Steam Deck to lose one physical core from
guest/driver worker masks. Larger hosts keep their existing reservation policy.
This scheduling change has regression coverage but still needs a Deck race test.

Development builds also offer experimental 480p (`-resolutionscale 0.444444`) and
576p (`-resolutionscale 0.533333`) scenes. Like 720p they retain native menu/HUD
buffers. The Linux window-size selector independently offers 854x480 and 1024x576
when borderless fullscreen is off. Smaller window size is a presentation setting,
not a patch to the menu coordinate system; fullscreen follows the desktop.
These scene scales need gameplay/graphics validation on actual devices.

CUSA00003 and other syntactically valid CUSA IDs are accepted experimentally;
CUSA00093 / 01.28 is a patch reference, not a whitelist. Folder-discovery errors
now include missing/unreadable/invalid metadata for rejected executables. This
does not repair a damaged dump or guarantee compatible binary patches. Include
the exact error and start log when reporting a different-region launch failure.

Telemetry also includes NVIDIA power/thermal clock-limiting reasons and cumulative
CPU thermal throttle counters where supported. Compare counter changes during
the run, not their absolute values. Optional NVIDIA fields are queried separately
so an unsupported driver query does not suppress basic clock/utilisation samples.
The frame CSV and summary distinguish swapchain image acquisition,
presentation-lock acquisition, direct/async submission-lock acquisition and the
time spent holding the lock during presentation. Columns ending in `_max_ms`
are per-frame maxima, not sums or pure GPU execution times.

On the tested laptop, a battery-powered run measured about 37.8 FPS with GPU
power/thermal limits active and increasing CPU throttle counters. The user
reported about 60 FPS again on AC power. Compare the same race and power/cooling
conditions before attributing a regression to the renderer. No automatic power
profile changes or clock overrides are applied.
