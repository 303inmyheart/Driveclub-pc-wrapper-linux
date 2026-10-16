# Driveclub-PC runtime fork

## Where it is

| | |
|---|---|
| Source | `runtime/`: a git **worktree** of `references/GR2fork` |
| Branch | `driveclub-pc` |
| Base | GR2fork `107d660` (GR2fork-EXODUS, 2026-10-10): the commit of the user's known-good GR2Launcher build |
| Upstream history | kept (`git -C runtime log`). All fork changes: `git -C runtime diff 107d660` |
| Change markers | every edit carries a `driveclub-pc:` comment |
| Submodules | initialized in the worktree (54, shallow) |
| License | GPL-2.0-or-later (unchanged upstream headers) |

Build (clang-cl), output `build\gr2fork-<name>\shadps4.exe`:

```powershell
scripts\build_reference.ps1 -Source "$PWD\runtime" -Config RelWithDebInfo -Lto -Name dcpc-relwithdebinfo-lto   # with symbols
scripts\build_reference.ps1 -Source "$PWD\runtime" -Config Release -Lto -Name dcpc-release                    # like CI
```

Run: `scripts\run_reference.ps1 -BuildName <name> ...`, the same isolated harness as the reference builds.

The executable keeps its upstream name `shadps4.exe` for now. Renaming it, and folding in the validation front end (`Driveclub-PC.exe`), is a later Phase 2 step.

## Changes so far (Phase 2, step 1, 2026-10-15)

### 1. Red-zone protection is always on (`src/emulator.cpp`, `src/core/module.cpp`)

**Problem** (`docs/implementation-plan.md`, "Baseline findings"): Driveclub's SysV code keeps live locals in the 128-byte red zone below RSP. When one of its memory accesses faults into the emulator's vectored handler (write tracking, GPU readback), Windows writes the exception frame below RSP and destroys those locals. Example: eboot+0x64D980 lost its pointer at `[rsp-0x38]`, which led to a null read at +0x64DB13. Upstream GR2fork has a mitigation (`redzone_patches`), but it is **off by default and off in the user's config**. Release builds survived by luck.

**Change**: `WindowsGuestRedZoneProtectionMode::StaticPatching` is set unconditionally. `redzone_patches=false` in a config is ignored with an Info log line. The per-module coverage summary is logged at Info instead of Debug.

**How the mitigation works** (upstream `core/cpu_patches.cpp`, unchanged): at load, functions that use the red zone are found by eh_frame boundaries plus a liveness analysis. Each memory instruction that executes while red-zone data is live is relocated into a trampoline that runs it with `RSP-128`. If it faults, the kernel's exception frame lands below the red zone.

**Verification**: fork built as RelWithDebInfo **without** LTO, the configuration in which the unmodified emulator crashed 4/4 at eboot+0x64DB13 (`docs/implementation-plan.md`). Same harness, config with `redzone_patches=false`: **no crash in 2/2 runs** (150 s and 130 s). Both passed the former crash point (7× and 9× `scePlayGoSetInstallSpeed`; the crash always came after the 6th). Logs: `logs/reference-CUSA00093-20261015-231129.log` and `logs/reference-CUSA00093-20261015-232042.log`. The second run was after the revision fix and records `107d660`, branch `driveclub-pc`, `107d660-dirty`.

**Coverage reported by the patcher for this run:**

| Module | Red-zone functions | Memory instructions protected | Not protected |
|---|---:|---:|---|
| eboot.bin | 171 | 6,304 / 6,462 | **158 unrelocatable**; also 28 short CPU patches left to the exception handler |
| libSceLibcInternal.sprx | 28 | 93 / 93 | 0 |
| libSceFios2.prx | 4 | 186 / 186 | 0 |
| libc.prx | 35 | 87 / 87 | 0 |
| libScePngDec, libSceJpegDec, libSceFont, libSceFreeTypeOt, libSceAvPlayer | 2 / 1 / 3 / 4 / 1 | all | 0 |
| libtbb, logiWheel, oskGNMm, iksemelGNMm, libSceRtc, libSceFontFt, libSceAudiodec | 0 | – | – |

**Residual risk**: 158 eboot memory instructions inside red-zone functions could not be relocated. Step 2 analysed them (`scripts/redzone_sites.py`, `docs/data/redzone_unprotected_sites.json`):

- They sit in exactly **6 functions**, the "6 indirect red-zone functions" the patcher reports. These functions contain indirect branches (jump tables), and relocating code inside them is unsafe:

  | Function | Unprotected sites |
  |---|---:|
  | eboot+0xA34550 | 96 |
  | eboot+0xA99060 | 23 |
  | eboot+0x9EE130 | 14 |
  | eboot+0xA8D9B0 | 14 |
  | eboot+0xA8DFC0 | 6 |
  | eboot+0xA937A0 | 5 |

- **All 158** access memory through a general register (e.g. `mov ebp, [rdi+0x48]`, `mov ecx, [rcx+rax*4]`). None is stack- or RIP-relative, so in principle any of them can reach tracked or GPU memory and fault.
- The crash function eboot+0x64D980 is **not** among them. It is fully protected.
- Once such a fault happens, the kernel has already written its frame below RSP, so it cannot be repaired afterwards. The fork therefore records the sites and **reports any fault at one of them**: a `Fault at unprotected red-zone site` warning, at most 64 per run (`core/signals.cpp`).
  - Menu, 130 s: **0** such faults.
  - Race driven by the user with `Driveclub-PC.exe` (RelWithDebInfo, no LTO; 142 s run, 85 s marked race segment): **0** such faults, no critical errors, no strict-stub stops (`logs/reference-CUSA00093-20261015-234045.log`). The segment ran at 48.5 FPS; this unoptimized symbol build is not comparable with `docs/benchmarks.md`.
  - Limitation: the telemetry sees faults only. It cannot tell whether the 6 functions ran without faulting or did not run at all. Either way no red-zone data was lost at those sites in these runs.

**Cost**: race benchmark −1.2 FPS on average, not significant at n=2 (`docs/benchmarks.md`).

### 2. Strict import policy (`src/core/aerolib/stubs.cpp`)

**Before**: an import with no implementation resolved to a generated stub that logged an error and **returned 0**. The game continued with an invented result.

**Now**: calling such a stub **stops the runtime** (`UNREACHABLE_MSG`) and names the function, its NID and the return address. Exceptions are an explicit `AllowedStubs` list where every entry has a defined result and a reason. It holds only what Driveclub 1.28 was observed to call in every run on 2026-10-15:

| Function | Result | Reason |
|---|---|---|
| `sceNpInGameMessageInitialize` / `CreateHandle` / `Prepare` / `SendData` / `DeleteHandle` / `Terminate` | 0 | PSN in-game messaging; servers offline. 0 is what every measured baseline run returned, so behaviour is unchanged |

Verified in the same run: the four called functions logged as allowed, and nothing else reached a stub.

Note: functions that upstream implements as "(STUBBED)" HLE functions are still called normally. For example `sceNpSnsFacebookDialogUpdateStatus` is called every frame and logs at error level. They are real registered functions, not unresolved imports. Each needs its own decision later; they are listed in `docs/compatibility-gaps.md`.

### 3. Build revision for worktree builds (`CMakeLists.txt`)

`GetGitRevisionDescription` cannot resolve a worktree whose `.git` file holds an absolute `gitdir`, so the log showed an empty `Revision`. The fork falls back to `git rev-parse` / `git describe`, so builds record `107d660…`, branch `driveclub-pc`, `-dirty` while changes are uncommitted.

Step 1 is committed as `b196a76` on `driveclub-pc` (local branch only, not pushed anywhere).

## Phase 2, step 2 (2026-10-15)

### 4. Gravity Rush 2 modules removed

`src/core/libraries/gr2_online/` (private-server online play: Http, Secure, NpWebApi, NpManager, NpAuth overrides) and `gr2_photo/` (photo mode, JPEG enc/dec, gallery), 32 files, plus their registration in `libs.cpp` and their CMake entries.

Both registered **only for Gravity Rush 2 serials**, so Driveclub never used them. Removing them changes nothing for Driveclub and drops network code it must not carry. The `PhotoReadback` hooks in the texture cache stay (inert unless armed) to keep `video_core` mergeable with upstream.

Correction to `runtime-comparison.md`: GR2fork's extra JpegDec HLE was gated to GR2. For Driveclub, GR2fork's effective HLE coverage equals shadPS4's (696/739), and JpegDec runs as the firmware LLE module.

### 5. `Driveclub-PC.exe` front end (`src/driveclub/launch_check.*`, `src/main.cpp`, CMake `OUTPUT_NAME`)

- The runtime binary is now **`Driveclub-PC.exe`**.
- Start: `Driveclub-PC.exe "<Driveclub base folder>"`. The update folder, an eboot.bin path, or the update folder itself are also accepted; the update must sit next to the base as `<folder>-UPDATE` or `<folder>-patch`.
- Checks before the core starts, each reported as a plain message box, on stderr and in `user\log\shadps4.log`:
  - folder exists;
  - `eboot.bin` present;
  - `param.sfo` TITLE_ID = CUSA00093;
  - update version: a warning if it is not 01.28;
  - firmware modules: `libSceLibcInternal.sprx` in the configured `sys_modules`, with an explanation that they come from the user's own console.
- Without arguments the program shows its usage.
- Verified:

  | Input | Result |
  |---|---|
  | missing folder | "Game folder not found" |
  | Uncharted (CUSA02343) | "This folder holds CUSA02343, not Driveclub (CUSA00093)" |
  | Driveclub base folder / eboot | game starts and runs |

### 6. User folder next to the executable (`src/common/path_util.cpp`)

On Windows the `user` folder (settings, logs, saves, caches) is always `<exe dir>\user`, whatever the working directory. Upstream used `<cwd>\user` or `%APPDATA%\shadPS4`, which could mix with a real shadPS4 install. Verified: starting from another directory created no `user` folder there.

### 7. Driveclub default settings

If `user\custom_configs\CUSA00093.json` does not exist, the front end writes `readbacks_mode=1` (Relaxed, owner-selected on 16.10.2026) and `readback_offload=true`. Existing explicit settings are preserved. Earlier measurements used Precise (`2`).

### 8. Red-zone fault telemetry (`core/cpu_patches.*`, `core/signals.cpp`)

See the residual-risk paragraph above.

## Not done yet (rest of Phase 2)

- Keep the telemetry on. If the 6 unprotected functions ever fault in play, protect them, for example by relocating whole functions with jump-table fix-ups. One race and one menu run showed no faults (2026-10-15).
- Decide every "(STUBBED)" HLE function Driveclub calls, e.g. `sceNpSnsFacebookDialogUpdateStatus` every frame.
- Driveclub defaults for resolution, FPS cap and controller.

## Wave 1 (16.10.2026)

Runtime commit: `c1f913a` on `driveclub-pc` (local only).
Final Release + LTO distribution was rebuilt from that clean commit. Packaging verification
hashed all 2,474 files under `dist/Driveclub-PC/user` before and after: every file was unchanged.
The packaged executable matches the build and the SMAA licence matches upstream source.
Evidence: `logs/wave1-dist-verification.json` and `dist/Driveclub-PC/BUILD_INFO.txt`.

- Launcher adds AF Off/2x/4x/8x/16x, AA Off/FXAA/SMAA 1x High, and P-core preference
  (on by default). INI keys: `af`, `aa`, `pcore_pin`. Environment overrides:
  `DCPC_AF`, `DCPC_AA` (0/1/2), `DCPC_PCORE_PIN` (0/1). Missing INI keys now default to AF 16x / FXAA, as requested after the first race.
  Explicit existing settings are still honoured. Precise readbacks remain the default.
  AF Off preserves the game's samplers. Overrides apply only to linear, mipmapped,
  normalized-coordinate colour samplers, clamped to the device limit; depth/data samplers
  retain their settings. AF is fixed for the process lifetime.
- `host_passes/aa_pass.*` runs at the actual guest image size before FSR. FXAA is a spatial
  directional luma filter. SMAA 1x High uses reference searches, diagonal/corner handling
  and original AreaTex/SearchTex lookups, with gamma-encoded luma edge detection.
  No temporal accumulation; Off records no AA dispatches.
- SMAA source and textures: iryoku/smaa commit `71c806a838bdd7d517df19192a20f0c61b3ca29d`.
  Original MIT notice: `runtime/src/video_core/host_shaders/smaa/LICENSE.txt`, copied to dist.
- `GetSystemCpuSetInformation` / `EfficiencyClass` finds the highest available Windows CPU
  performance class. The mask respects process affinity and the reserved GPU core; Game:Main
  stays off that core. Homogeneous CPUs retain the existing policy. GPU core reservation is
  still a separate setting; disabling P-core preference does not disable reservation.
- The frame profiler adds count / inclusive total ms / command-processor-only ms for
  `submit`, `readback_record`, `download`, `readback_flush`, `protect`. Submit counts scheduler
  submits across runtime schedulers, not guest submissions. Timings overlap: readback_record
  contains readback_flush, which contains submit. Do not sum them. Offloaded GPU waits are
  outside readback_record. Protect counts requests at the page-manager API boundary.
- `DCPC_AA_CYCLE=1`: Off/FXAA/SMAA every ten seconds, mode in profiler `phase`. Do not combine
  with readback A/B, which also writes that column. `DCPC_AA_COMPARE_SHOTS=1`: autopilot `shot`
  additionally saves FXAA and SMAA outputs of the **same guest frame**, before FSR. The normal
  game screenshot is the AA-Off reference. Additional comparison readbacks are SDR-only;
  AA rendering preserves the existing input encoding. Screenshot work must be excluded from
  small-effect comparisons. `scripts/wave1_report.py` summarises the race and new counters.

Measured results and validation: see the Wave 1 section in `docs/benchmarks.md`.


### Performance baseline for Wave 2 (16.10.2026)

Commit `1242a2f`: the owner chose FXAA + AF 16x + Precise readbacks, with P-core
preference enabled. These are the process/launcher defaults and current distribution settings.
Future performance comparisons must keep image quality fixed, alternate only the tested
optimisation within one run, and use unique guest-frame rate plus CP timings as primary
metrics; host present rate is reported separately.

Wave 2 priorities: profile the command processor under this baseline; reduce repeated
per-draw texture/buffer binding, image/program lookups and shader-specialisation work;
then reduce avoidable Precise readback submissions while preserving GPU/CPU ordering
and data correctness. Finally, batch redundant page-protection updates where safe.
The measured 0.543 ms readback flush and 0.433 ms CP protection costs overlap other
scopes and do not explain the whole historical Precise penalty. No FPS gain is promised.

### Wave 2 implementation (16.10.2026)

Local runtime commit: `2eb396c` on `driveclub-pc`; no push.
Final Release + LTO build succeeded from the clean commit and was packaged to
`dist/Driveclub-PC`. All 2,474 user files matched their pre-packaging SHA-256 hashes;
FXAA / AF16 / Precise settings were verified. Evidence: `logs/wave2-dist-verification.json`.

**Final policy: opt-in only, default `DCPC_PERF=0`.** Corrected race comparisons found no
confirmed FPS gain (+0.33% / -1.17%; pooled -0.65%). The preliminary +6% result was confounded
by pipeline compilation, not a demonstrated improvement. `docs/benchmarks.md` records the
correction and raw evidence. Readback scope time falling after moving its flush outside the
scope must not be presented as saved work. Remaining per-draw and Precise costs are unresolved.
The disabled production path bypasses the large template helper before entering its stack frame.

- `driveclub/performance.*` provides `DCPC_PERF=0` (baseline), `1` (enabled), `2` (ten-second
  in-run A/B). `DCPC_PERF_AB_REVERSE=1` reverses phase order. This owns profiler `phase`;
  AA/readback A/B must be off. Feature switches: `DCPC_PERF_TEMPLATES`,
  `DCPC_PERF_SAMPLER_HINTS`, `DCPC_PERF_READBACK_BATCH` (each defaults to 1).
- Descriptor update templates replace repeated write-header interpretation for exact flat
  binding plans, including partial pushes and descriptor-heap updates. The current payload is
  copied in full from the rasterizer's live arrays; no descriptor contents are cached. Shape,
  pointer and extent guards fail to the original path. A pipeline holds at most 16 shapes,
  without eviction; creation failure also falls back. `template_updates` counts actual calls.
  The [Vulkan template contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorUpdateTemplateCreateInfo.html)
  binds each template to its layout/bind point. Raw dispatcher calls avoid Vulkan-Hpp's
  enhanced overload interpreting a typed payload pointer as pointer-to-pointer data.
- Sampler hints probe the last slot used by a pipeline binding before hashing. Both masked
  key words and a live handle must match; normal sampler GC clears the memo. Hints do not
  bypass identity checks or lifetime accounting and only run in the existing fast-cache modes.
- Readbacks queued concurrently may share one scheduler tick and submit. Pending copies are
  always submitted at command-drain exit, after at most 16 callbacks, and **before any
  non-readback callback**. This prevents a callback waiting for a faulting guest's lock from
  delaying that guest's GPU completion. Existing timeline waits, pending-range ownership,
  write-sequence validation and CPU write-back are retained. An intervening ordinary submit
  also satisfies the batch; no extra empty submit is issued for an already submitted tick.
- Page-manager code was reviewed: it already batches contiguous permission transitions and
  skips unchanged permissions. No broader deferred protection scheme was introduced; retaining
  precise CPU/GPU ordering is required. Program-specialisation and image identity guards remain.

Profiling baseline: `logs/reference-CUSA00093-20261016-114540.log.meta.txt` and
`logs/sample-CUSA00093-20261016-114540/shadPS4_GpuCommandProcessor/report.txt`.
The initial template prototype crashed before the race (`…-115347`); this run is invalid for
performance. A debugger smoke run (`…-115502`) timed out during startup without reaching the
new path. The payload-overload bug was found in the Vulkan-Hpp headers and corrected before
the measured control races. Results and the final default policy are recorded in benchmarks.

### Readback mode follow-up (16.10.2026)

The owner's requested Relaxed then Off races used clean `2eb396c`, fixed FXAA / AF16,
P-core preference and `DCPC_PERF=0`: **74.07 / 85.95 unique guest FPS**, compared with
68.57 pooled baseline phases in the earlier Precise controls. Cross-run drift and divergent
collision paths limit attribution. Data: `docs/data/readbacks-20261016.json`; reproduction
and visual findings: `docs/benchmarks.md`, readback controls section.

Off removes measured readback flushes entirely; Relaxed still has 5.29/frame. Both retain
~10.1 ms CP work proxy/frame, with substantially less CP idle in Off. The limiting domain
is CPU command translation/submission plus synchronization; neither one dominant function
nor the full Precise penalty has been identified. GPU utilisation was ~40.5/44.5% in these
races. Async queue wait timers overlap graphics work, and CP busy time is not exclusive CPU
time. Do not subtract all wait columns or interpret them as additive stalls.

Off's 110 s capture showed a nearly black world with visible HUD. A separate visual-only
follow-up reproduced this in all eight 102–121 s shots, spanning 19 s and a track reset;
95 s was normal. Off's measured FPS includes this visually broken state and is not a
quality-preserving gain. The exact cause is still undiagnosed. Evidence and original PNGs
are linked in benchmarks. At the time of those tests production defaults remained Precise;
the owner subsequently selected Relaxed for manual validation, as recorded below.

### Provisional Relaxed default and next CPU targets (16.10.2026)

Local commit `93b4885` changes the launcher's missing-setting default and fresh per-game JSON
to Relaxed (`1`), with Precise available as compatibility fallback. Fresh distribution INIs
and the current distribution's explicit INI also use `1`. Existing users' explicit values
are not migrated automatically. FXAA / AF16 / P-core preference remain unchanged. This is
the owner's provisional choice for manual visual testing, not a claim of full correctness.

The original next-step proposal below led to the combined experiment in the following section;
its descriptor-range and queue-handoff implementations have now been tested together:

1. Measure host Vulkan descriptor-update, barrier, draw/dispatch and submit calls with sampled
   timings under the new Relaxed baseline. The prior 21.28% driver sample share is aggregate
   and cannot identify the costly entry point or be treated as fully removable overhead.
2. Target partial descriptor updates and payload preparation, rather than repeating the
   unsuccessful descriptor-template trial. Relaxed telemetry already shows near-total
   BINDPLAN hits, very few complete DESCDELTA hits, but frequent partial changes (e.g.
   613,660 probes / 283 full hits / 422,222 partial in one 300-frame interval). The existing
   delta path already suppresses many descriptors. Investigate whether remaining update
   ranges/calls or CPU payload reconstruction can be reduced, retaining resource identity,
   command-buffer resets, generation/lifetime and barrier checks. Whole-binding reuse is
   unlikely to hit often based on these counters.
3. Trace guest submission -> CP wake -> readback request -> submit -> completion to locate
   work-arrival gaps (Relaxed CP idle ~3.4 ms/frame). Only then alter a proven wakeup or
   redundant-submit path. Previous generic CP backoff and callback readback batching did
   not improve FPS; do not simply re-enable them or remove correctness waits.

Use one meaningful batched experiment with in-run A/B and reversed phases, constant Relaxed
and image settings, excluding pipeline compilation and screenshot stalls. No gain estimate
is established. Source profiles collected with Precise need confirmation under Relaxed.

### Combined CPU-path experiment (16.10.2026)

Two new opt-in features share the existing `DCPC_PERF` A/B phase control. Set
`DCPC_PERF_DESCRIPTOR_RANGES=1` and `DCPC_PERF_SUBMIT_HANDOFF=1`; disable the earlier
three feature flags when testing this bundle. Both new flags default to 0 and `DCPC_PERF`
still defaults to 0. `scripts/run_cpu_path_ab.ps1` reproduces the forward run; `-Reverse`
reverses phases. It reuses the isolated shader cache and recopies source saves read-only,
fixes Relaxed / FXAA / AF16 and captures game-only images at 95/110 s.

- Descriptor range merging joins changed runs separated by at most two unchanged descriptors
  within the same original buffer/sampler write. It uses fresh payloads, preserving every
  descriptor value; layout/tick/generation/identity guards and full-write fallbacks remain.
  It changes the number of write headers, not the number of push API calls. The non-flat
  path and image-array handling are unchanged. This is not the prior template experiment.
- Submit handoff unlocks the producer's submission mutex before notify for graphics/compute
  submissions, callbacks and submit-done. The consumer still checks the condition under that
  mutex; no work order or completion wait is removed. The parser also skips empty mapped
  queues using the already-published atomic pending count, independently of the disabled
  CP backoff experiment. Queue skips are counted locally and published once per drained
  work interval so measurement does not add atomic traffic to each skipped queue.
- Profiler columns appended after `template_updates`: `descriptor_pushes`, `descriptor_writes`,
  `descriptor_elements`, `descriptor_gap_fills`, `empty_queue_skips`. Push counters cover the
  ordinary Pipeline PushSet path, not host AA/FSR or heap descriptor updates. Gap fills count
  unchanged elements actually included in accepted compacted payloads. These are inclusive
  per-guest-frame counts, not timing estimates.

Local commit `e253a89`, clean Release + LTO rebuild. Two Relaxed A/B races found +1.10% /
+5.40% unique guest FPS (pooled 71.08 -> 73.33, +3.16%), with zero race pipeline creations.
This is a small positive signal, not a demonstrated general gain. CP idle falls more than
per-draw work; p99 frame intervals slightly worsen in both runs. All four captured images
render normally. Keep this bundle opt-in; normal defaults stay Relaxed / FXAA / AF16.
Full evidence and limits: `docs/benchmarks.md`, `docs/data/cpu-path-ab-20261016.json`.
