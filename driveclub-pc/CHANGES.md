# Changes compared with GR2fork

## v0.2

- **Render resolution** setting (1920x1080 / 2560x1440 / 3840x2160). Driveclub's engine has its
  own `-resolutionscale` switch; the launcher passes it and enlarges the game's video-memory
  heaps (VideoPrivate and VRAM_RENDERING) with two small in-memory stubs, so the larger render
  targets fit. The display buffers follow the chosen size. Output stays 16:9.
- **Background shader compilation** (on by default, launcher option): vertex/fragment shader
  translation and graphics pipeline creation run on worker threads; draws wait for them instead
  of the whole frame. Cold-cache race start: about half the stutters, worst frame ~150 ms instead
  of ~700 ms.
- **Texture garbage collector** no longer stalls on images it cannot free (memory kept growing).
- "Resolution" is now "Window size" and only applies in windowed mode.
- New experiments that showed no gain stay off: write-fault window, hybrid readbacks.
- Release build: PGO again, from a new recorded race.

## v0.1

Base: GR2fork commit `107d660` (GR2fork-EXODUS, 2026-10-10). Every edit in the source is marked
with a `driveclub-pc:` comment; `git diff 107d660` shows all of them.

## Stability and correctness

- **Red-zone protection always on.** Driveclub keeps live data below the stack pointer. Without
  this, a memory fault handled by the emulator overwrote it and crashed the game. GR2fork's
  `redzone_patches` mitigation is now forced on. The few sites it cannot protect are logged if they fault.
- **Strict import policy.** Calls to unimplemented imports stop the emulator with a clear message
  instead of silently returning 0. A short allow-list covers the offline PSN messaging calls.
- **Gravity Rush 2 modules removed** (online private-server code and photo mode). They only ever
  activated for Gravity Rush 2.

## Launcher

- The executable is `Driveclub-PC.exe`. It opens a settings window: game folder, firmware folder,
  frame rate (30 / 60 / 120), GPU readbacks (Precise / Relaxed / Off), anti-aliasing,
  anisotropic filtering and P-core preference.
- Before starting, it checks the game folder, title ID (CUSA00093), update version and firmware.
- The `user` folder (settings, saves, caches, logs) always sits next to the executable.
- Defaults: 60 FPS, Precise readbacks, FXAA, AF 16x, P-core preference on.

## Image quality

- **Anisotropic filtering override** (Off / 2x / 4x / 8x / 16x) for colour texture samplers.
- **Post-process anti-aliasing** (Off / FXAA / SMAA 1x High), applied to the game's
  internal image before FSR 1 upscaling. SMAA uses the reference implementation (MIT).

## Performance

- **Fast guest mutexes:** uncontended guest pthread mutex locks no longer make a system call.
- **Separate submit thread:** the GPU command processor hands queue submits to its own thread.
- **P-core pinning:** the command processor and game threads are kept on performance cores
  (detected with `GetSystemCpuSetInformation`).
- **Frame pacing** for the 30 / 60 / 120 FPS targets.
- **PGO builds:** release builds use profile-guided optimisation from a recorded race.

Several other experiments (descriptor templates, readback batching, deferred recording and more)
showed no reliable gain. They stay in the code but are **off by default** (`DCPC_PERF=0`).

## Diagnostics

- Frame profiler, Vulkan call statistics and guest HLE call statistics (`DCPC_*` environment variables).
- An autopilot for unattended test runs (scripted button presses and in-game screenshots).
