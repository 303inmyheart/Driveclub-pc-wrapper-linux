# Linux fork changelog

This changelog covers the Linux port created by Codex (OpenAI) at
303inmyheart's request. The direct upstream is
[Driveclub-PC by ondrejeder](https://github.com/ondrejeder/Driveclub-pc-wrapper),
based on [GR2fork by junminlee2004](https://github.com/junminlee2004/GR2fork) and
[shadPS4](https://github.com/shadps4-emu/shadPS4). Original emulator, game timing,
rendering and Driveclub optimisation credits remain with their authors.
The original Windows history is preserved in
[driveclub-pc/CHANGES.md](driveclub-pc/CHANGES.md).

## Unreleased

- Exclude `libvulkan.so*` from AppImages and open `libvulkan.so.1` explicitly
  on Linux, matching the linked FSR SDK/SDL loader SONAME. Avoid bundling a
  second loader alongside the host's graphics stack. Experimental fix candidate
  for issue #3: Deck logs showed two loaders and a repeatable host-loader fault.
- Record the resolved loader path in startup diagnostics. Other platforms retain
  their existing loader selection. Real Steam Deck confirmation is pending.

## 0.2.6-experimental.3 — 2026-10-24

Follow-up diagnostic pre-release for issue #3. Built locally.

- Add per-library HLE initialization markers (begin, callback entry, completion,
  or skipped regional LLE override) to the startup diagnostics. The next Deck
  test can identify the library whose initialization fails.
- Capture current process mappings in the fatal-signal record as well, including
  host libraries loaded after the earlier startup checkpoints. No crash fix is
  claimed; the reported fault remains under investigation.

## 0.2.6-experimental.2 — 2026-10-23

Diagnostic pre-release for the Steam Deck startup crash reported in issue #3.
No crash fix or performance improvement is claimed.

- Enable a separate per-process `log/startup-<PID>.log` in the diagnostic AppImage,
  including direct-write startup checkpoints and process mappings for address
  interpretation. Normal builds keep tracing off unless explicitly enabled.
- Record fatal signal number, thread ID, fault address and x86-64 instruction/stack
  pointers without using the normal logger or unwinding in signal handlers.
  Preserve signal termination before emulator handler installation; after
  installation record only unhandled fatal paths, preserving guest fault handling.
- Mark hardware-info, memory/linker/controller initialization, SDL game window,
  HLE/module loading and guest execution boundaries. These markers bypass the
  buffered main log that ended midway through the reporter's CPU model line.
- This is not a full stack trace and cannot intercept SIGKILL, OOM kills, power
  loss or every third-party signal handler. Logs can include local paths.


## 0.2.6-experimental.1 — 2026-10-23

Pre-release for issues #2 and #3. Built locally; real-game confirmation of the
new 240p/360p scales and the reported CUSA00003 failure is still pending.

- Add experimental 240p and 360p scene rendering and independent Linux window
  sizes 426×240 / 640×360 following Steam Deck feedback in issue #3. Lower
  the game window minimum from 640×360 to 426×240 so 240p output is usable. Menus/HUD
  retain the native framebuffer. Lower scene scales require real-game testing;
  no FPS target is guaranteed.
- Follow directory symlinks during bounded game discovery, deduplicating canonical
  targets to prevent loops and duplicate installations. This fixes discovery of
  linked game libraries, a possible cause of the folder error in issue #2.
- Explain when an accepted CUSA title is rejected because its metadata describes
  an update or DLC. The specific CUSA00003 report remains unconfirmed without
  its folder layout and complete error details.

## 0.2.5 — 2026-10-23

Regular Linux release, based on original Driveclub-PC 0.2.5. Restores the Linux
fork changes after syncing the GitHub fork to upstream. Built locally; no GitHub
Actions build minutes are used.

The maintainer reports successful testing of the preceding 0.2.5 Linux preview
and confirms the Driveclub cover is visible in Discord with release candidate 1.
Image availability and exact RPC payload serialization are checked as well.
These are local/user checks, not broad hardware or all-race compatibility claims.

- Replace the broken PlayStation cover endpoint (HTTP 404 for CUSA00093) with an
  attributed, unchanged Driveclub cover hosted at an immutable GitHub asset commit.
  Cover art is game-identification artwork with its own rights notice, not GPL
  code. The upstream shadPS4 Discord application identity remains unchanged.

- Integrate original Driveclub-PC 0.2.5 at upstream commit d9701fc1, preserving
  the Linux port's recursive discovery, experimental version patch checks,
  sub-native scene resolutions, DLC selection, CPU affinity fixes, profiling,
  local profile name and Discord switch. Keep the original 0.2.5 graphics/audio
  fixes and expose its optional frame-generation mode and custom save root in
  the native Linux launcher. Frame generation stays off by default and needs
  Linux gameplay validation; it adds presentations, not simulation/input FPS.
- Fix Discord Flatpak IPC discovery: search both the native runtime directory
  and app/com.discordapp.Discord (also Vesktop's standard Flatpak directory).
  Use a fresh descriptor for each attempt, retain close-on-exec and nonblocking
  operation, and never create socket symlinks or change Discord permissions.
- Pump RPC callbacks and log client readiness, errors and disconnects. Stop the
  callback worker before clearing/shutting down RPC. Fake native/Flatpak socket
  tests and a full SDK handshake/SET_ACTIVITY contract test contact no real client.
- Preserve upstream's detailed firmware checks, including required JPEG,
  FreeType and audio-decoder modules and optional HLE fallbacks. Validate custom
  save roots without moving existing saves; invalid roots fail explicitly.
- Initialize Discord RPC's pinned RapidJSON dependency in the local build setup.
- Add a saved launcher switch for Discord Rich Presence, off by default. Local
  AppImage builds now include RPC support; enabling it requires a running Discord
  client. Apply the launcher choice after per-game overrides and clear presence
  on shutdown. Builds compiled without RPC show the control as unavailable.
- Add a local PS4 username field backed by the existing player-1 profile in
  users.json. Preserve the profile ID, other users, account fields and save paths;
  validate the 16-byte UTF-8 user-service limit and report persistence failures.
  This changes a local display name, not a PSN/ShadNet account identity.
- On Linux hosts with at most four available physical CPU cores, keep the graphics
  command processor and guest/driver workers on shared cores instead of isolating
  a quarter of a 4c/8t host. Larger hosts retain their reservation policy. This
  addresses the Steam Deck topology observed in issue #1; FPS gains require
  testing on the device and are not guaranteed.
- Add experimental 480p and 576p scene scaling while preserving native 1080p
  menu/HUD output, plus independent 854x480 and 1024x576 Linux window sizes.
  Window size does not change the internal menu framebuffer. New scene scales
  require gameplay validation; no lower-resolution framebuffer patches are forced.
- Clarify that CUSA00093 / 01.28 is the patch reference, not a launch whitelist.
  Rename the reference constants and test CUSA00003 discovery and matching updates.
  Missing/malformed game metadata now identifies rejected executable directories
  and explains that unpacked base-game metadata is required. Issue #2's real-game
  failure remains unconfirmed without its reporter's error message/log.

## 0.2.3 — 2026-10-22

Locally built Linux release. Original upstream credits and Windows history are
preserved; changes below belong to this Linux fork.

### Fixed

- Fix periodic affinity rewalks stripping the reserved P-core from the graphics
  command processor after it widened to all P-cores. Preserve the registered
  owner's mask while still narrowing other threads without widening/stranding them.
- Add 720p internal rendering to the launcher, settings persistence and engine
  scaling. Sub-native 720p scales the 3D scene while preserving native 1080p
  menu/HUD output. Correct the initial experiment that also reduced the output
  and cropped menus. The user reported that the revised scene-only path works;
  broader menu, race and hardware coverage remains unverified.

### Experimental game versions

- Remove the CUSA00093-only launch restriction. Other valid CUSA IDs and game
  versions are allowed with an explicit experimental warning. CUSA00093 / 01.28
  remains the tested reference; this does not establish support for other games.
- Use the selected title ID for per-game settings and external DLC discovery,
  and reject adjacent updates with mismatching or unreadable title metadata.
- Attempt FPS, render-size, heap and ultrawide patches on experimental builds
  too, checking the expected bytes before each patch group. Resolution changes
  are validated as a whole before writing, preventing partial instruction-group
  patches. Mismatching groups are skipped, not relocated or forced onto unknown
  bytes; unsupported executable layouts can therefore remain unpatched.
- Add profile/scaling/patch-transaction regression checks and headless launch
  fixtures for alternate IDs, old builds and matching/mismatching updates.

### Local diagnostics and validation

- Add optional read-only GPU/CPU telemetry to local profiling runs to distinguish
  scheduler contention from clock, thermal, memory and workload differences.
- Record NVIDIA power/thermal limiting reasons, CPU thermal throttle counters
  and separate swapchain acquisition / queue-lock wait timings. These measurements
  do not change queue synchronisation or power settings.
- Extend local profiling with frame percentiles, long-frame exports and isolated
  queue/parallel-recording comparisons. These experiments remain off by default.
- The latest battery-powered diagnostic run measured approximately 37.8 FPS,
  with GPU power/thermal limiting flags and rising CPU thermal throttle counters.
  The user reported approximately 60 FPS again after connecting AC power.
  This is a limited comparison on one laptop, not a new universal FPS fix.
- Steam Deck performance and compatibility remain unverified. An issue reports
  approximately 20 FPS at 1080p; no Deck-specific fix or 60 FPS claim is made.

## 0.2.2 — 2026-10-21

First regular Linux release, based on upstream Driveclub-PC 0.2.1.

### Fixed

- Detect Intel hybrid P-/E-core topology through the Linux `cpu_core` and
  `cpu_atom` PMU CPU lists when `topology/core_type` is unavailable.
- Prevent graphics-command-processor reservation on a low-power E-core. On the
  tested Core Ultra 7 155H, the previous code classified every core as equally
  fast and reserved CPU 21; the corrected code identifies CPUs 0–11 as P-cores.
  CPU numbers are discovered at runtime, not hard-coded for this machine.
- Implement Linux critical-thread P-core affinity and the command processor's
  wide-affinity policy. Preserve inherited CPU restrictions. Correct the log
  that previously claimed a Linux scheduling change despite a Windows-only
  implementation. No privileged priority changes are required.
- Handle an empty physical-core enumeration safely instead of indexing an
  empty vector.
- Replace fixed 100-microsecond polling sleeps in the optional Linux Vulkan
  recorder with producer notifications. Include stop handling so an idle
  recorder can terminate without a lost wakeup.

### Launcher and defaults

- Add a persistent Presentation selector: Immediate, Mailbox and FIFO.
  Immediate is the Linux default and matches the local race-test configuration;
  it can cause tearing. Unsupported modes fall back to FIFO with a log message.
- Apply presentation settings to both runtime settings and the per-game config.
- Keep precise GPU readbacks and the existing 30/60/120 FPS settings.
- Keep readback prefetch, parallel Vulkan recording and empty-command-queue
  skipping off by default. The CPU affinity correction is active normally.
- Retain optional empty-queue skipping with rollback and alternating diagnostic
  modes; it is not presented as a verified performance improvement.

### Diagnostics and validation

- Add an isolated profiling runner that copies settings, saves and caches while
  referencing the user's system modules. Tests do not overwrite original saves.
- Support presentation comparisons, diagnostic 720p rendering, alternating
  Vulkan recording/readback tests, and separate-process prefetch on/off runs.
- Report frame timing and inclusive readback waits; exclude the first second
  after alternating-mode changes from phase comparisons.
- Add regression tests for Intel hybrid CPU-list discovery, restricted CPU
  masks, malformed topology, recorder wakeups and shutdown races. Run these
  alongside existing content-path tests in the GitHub workflow.
- Local CPU-fix tests on an Intel Core Ultra 7 155H / NVIDIA RTX 4050 Laptop GPU
  reached approximately 60 FPS at 1080p in the tested races. The two separate
  prefetch-off/on starts measured 59.75 / 59.90 FPS over their final minute.
  Prefetch therefore has no demonstrated meaningful FPS advantage in those
  runs and stays disabled. These are user-driven samples, not a guarantee for
  all tracks, weather, graphics settings, GPUs or distributions.
- No graphics errors were reported in the earlier readback comparison. Full
  rendering correctness, long-session stability and DLC compatibility are not
  established by these limited tests.

### Packaging and documentation

- Publish the release as a locally built x86_64 AppImage with SHA-256 checksum,
  source/build information and this changelog; GitHub Actions is not required
  to produce or upload the release.
- Document the complete local build and packaging process in
  [driveclub-pc/BUILD-LINUX.md](driveclub-pc/BUILD-LINUX.md).
- Update Linux requirements, distribution compatibility, usage and attribution.
- Preserve original upstream documentation and third-party licence notices.

### Requirements and remaining limits

- Published AppImage: x86_64-v3 / AVX2 CPU, glibc 2.39 or newer, graphical Linux
  desktop and Vulkan-capable graphics drivers. Ubuntu is the build baseline,
  not a requirement to use an Ubuntu-based distribution. Other distributions
  have not yet been independently tested; musl systems are unsupported.
- Target game: the user's Driveclub CUSA00093 dump with update 1.28 and their own
  PS4 system modules. No game, DLC, firmware or Sony code is distributed.
- FUSE is used for normal AppImage mounting; extraction mode remains available.
- Linux port creator: Codex (OpenAI). Fork owner/maintainer: 303inmyheart.
  Original Driveclub-PC creator: ondrejeder. See the README for full credits.

## 0.2.1-linux-preview.1 — 2026-10-20

First published Linux AppImage preview.

- Port the settings launcher to native SDL3/ImGui on Linux; Wine/Proton is not
  required. Preserve upstream graphics, display and frame-rate options.
- Let users select their own game and firmware directories; import console
  modules into the Linux application data directory.
- Find base games recursively inside containing folders, ignore update/mod and
  payload folders as base candidates, avoid symlink loops, bound traversal and
  report ambiguous installations. Save the resolved base-game path.
- Recognise adjacent `<base>-patch` / `<base>-UPDATE` folders for update 1.28.
- Add selectable external DLC containers, supporting an `addcont` root with
  `CUSA00093/<DLC>/sce_sys/param.sfo` or a direct `<DLC>/sce_sys/param.sfo` layout.
  Keep DLC in the chosen location and filter mismatching title IDs; PKG files
  must be unpacked first.
- Bound metadata string reads and reject malformed/truncated game metadata.
- Store settings, saves, imported firmware, caches and logs under
  `$XDG_DATA_HOME/driveclub-pc` (normally `~/.local/share/driveclub-pc`), with a
  `DCPC_USER_DIR` override. Reopen settings with `--launcher`.
- Adapt compiler headers/logging for Linux and avoid the initial save-migration
  dialog for fresh installations.
- Add targeted submodule initialisation, a local AppImage builder, desktop
  metadata, bundled runtime libraries and SHA-256 checksums. Verify packaging
  tools against pinned hashes. Leave glibc and graphics drivers host-provided.
- Add an Ubuntu 24.04 GitHub Actions build with content-path regression tests,
  desktop validation, packaged startup/checksum checks and build information.
  Add version-tag release publishing to the workflow after the initial build.
- Establish clear Linux-port attribution, preserve the original README and
  explain distribution requirements. The initial preview's automated checks
  did not establish real-game compatibility.
