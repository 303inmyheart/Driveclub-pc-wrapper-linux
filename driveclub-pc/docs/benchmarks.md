# Benchmarks: reference runtime baseline

All numbers here were measured. Nothing is estimated. Each table names its raw data.

## Method

- **Frame times**: `VK_LAYER_DC_framelog` (`tools/framelog`). It is a Vulkan layer that records a QueryPerformanceCounter timestamp at every `vkQueuePresentKHR` and is enabled per process through environment variables only. The interval between presents is the same quantity PresentMon reports as `MsBetweenPresents`. GPU time is **not** measured yet.
- **Statistics**: `scripts/frame_stats.py`.
  - average FPS = frames / elapsed time;
  - p95/p99/max present interval;
  - "1% low" = FPS equivalent of the mean of the slowest 1% of intervals;
  - "hitches" = intervals > max(2× median, 50 ms);
  - "frames > 25 ms" = share of frames that fell to the 30 FPS cadence or worse.
- **Harness**: `scripts/run_reference.ps1`. Each run starts from a fresh isolated `user` folder that contains:
  - the user's `config.json` and Driveclub per-game config: fullscreen borderless 2560×1440 output, internal 1280×720 (`internal_screen_*` in config), `readbacks_mode=2`, `readback_offload=true`, Mailbox present, vblank 60;
  - a copy of the user's Driveclub save (profile, ghosts, replays);
  - a copy of the user's shader/pipeline cache (21 MB).

  The firmware `sys_modules` are read from the user's install. Nothing in the user's install or the game directories is written.
- **Game**: Driveclub base + 1.28 update.
- **Hardware**: Intel Core Ultra 9 285H, RTX 5070 Ti Laptop (driver 617.42), Windows 11.

## Race benchmark (2026-10-15, user driving)

The user drove the same race start 2× per variant, interleaved, and pressed F1 at the race start and again 65–85 s later. Statistics cover exactly the marked segment.

Variants:
- **user** = the user's prebuilt GR2fork "Pre-release (GR2fork-EXODUS)" `107d660` (official CI build: Release + LTO);
- **self** = `107d660` built here as Release + LTO (`scripts/build_reference.ps1 -MatchCI`, clang 19.1.5);
- **off/on** = `General.redzone_patches`.

| Variant | avg FPS (run 1 / run 2) | mean | 1% low FPS | p99 ms | max ms | hitches | frames > 25 ms |
|---|---|---:|---|---|---|---|---|
| user, redzone off (user's setup) | 52.4 / 50.1 | **51.3** | 21.2 / 18.5 | 35.1 / 50.2 | 83.5 / 133.1 | 17 / 47 | 13.7 % / 18.2 % |
| self, redzone off | 51.5 / 53.2 | **52.3** | 19.0 / 21.9 | 50.1 / 34.9 | 83.9 / 67.1 | 43 / 20 | 15.3 % / 12.1 % |
| user, redzone on | 51.4 / 50.6 | **51.0** | 19.6 / 19.3 | 35.0 / 35.3 | 68.0 / 68.2 | 30 / 34 | 15.7 % / 17.5 % |
| self, redzone on | 49.9 / 50.4 | **50.2** | 19.6 / 19.8 | 35.1 / 35.2 | 68.2 / 68.1 | 27 / 29 | 19.2 % / 18.1 % |

Raw data: `logs/race-bench-20261015-stats.json` and the `logs/reference-CUSA00093-20261015-22*.log.meta.txt` files tagged `race-*` with their `frames-*.csv`. One run (`…-221638`) had no F1 marks because the user did not drive. It is marked `invalid=` and was repeated.

### What this shows

1. **Baseline.** In this race segment the user's setup averages **about 51 FPS** (50.1–52.4 across 2 runs), consistent with the reported 50–60 FPS. The median frame is **16.7–16.8 ms** (60 FPS cadence) in every run, but **12–19 % of frames take 33 ms or longer** (p95 ≈ 34 ms). The average below 60 comes almost entirely from frames that drop to the 30 FPS cadence, not from uniformly slower frames. This frame-pacing pattern is the main target for later work.
2. **The self-built binary matches the official one.** Mean over both red-zone settings: user 51.1 vs self 51.2 FPS. The difference (0.1 FPS) is far below the run-to-run spread (all 8 runs: 49.9–53.2 FPS, standard deviation 1.16 FPS). Our own builds are therefore a valid baseline for future changes.
3. **`redzone_patches` costs at most a little and is not resolved by this data.** Mean off 51.8 vs on 50.6 FPS (−1.2 FPS, −2.3 %). With 2 runs per variant and a spread of about 2 FPS between runs of the same variant this is **not statistically significant**. All four "on" runs had max ≤ 68.2 ms and p99 ≤ 35.3 ms. Two of the four "off" runs had p99 ≈ 50 ms, and one had a 133 ms spike. Fewer worst-case spikes with patches on is possible but **not established** with n = 2.

### Limitations

- Manual driving: route, speed and traffic differ slightly between runs. n = 2 per variant.
- Present intervals only. No GPU timestamps and no CPU per-thread breakdown yet.
- The display refresh, Mailbox present and the emulated 60 Hz vblank cap the result at 60 FPS. Gains above 60 are not visible in this test.

## Profiled race (2026-10-16): where the time goes

Build: `Driveclub-PC.exe` fork `2672379`, Release + LTO, with the runtime frame profiler (`DCPC_PROFILE=1`, `src/driveclub/frame_profiler.*`). Settings: Driveclub-PC defaults (borderless 2560×1600, FSR 1 + RCAS, readbacks_mode 2). GPU sampled with `nvidia-smi` every 100 ms. The user drove; the segment between the F1 marks was 63 s. Report: `scripts/profile_report.py`, data `docs/data/profile-race-20261016.json`, log `logs/reference-CUSA00093-20261016-000702.log`.

| | All frames | On time (≤ 25 ms) | Slow (> 25 ms) |
|---|---:|---:|---:|
| Share of frames | 100 % | 64.5 % | **35.5 %** |
| Frame time (mean) | 22.9 ms | 16.6 ms | 34.4 ms |
| Guest draws per frame | 2,856 | 2,726 | **3,093** |
| Compute dispatches per frame | 211 | 210 | 212 |
| Pipelines created | 0 | 0 | 0 |
| Scheduler waits on host GPU | 0.03 ms | 0.02 ms | 0.06 ms |
| Presenter waits | 0.005 ms | 0.004 ms | 0.005 ms |

- **GPU command processor thread busy: 99.8 %** of the segment (idle 0.04 ms per frame).
- **GPU utilisation: 27 % mean, 35 % p90** (RTX 5070 Ti Laptop at about 2.5 GHz).
- Average 43.7 FPS, median 17.1 ms, p95 34.4 ms.

**Conclusion.**
- The frame rate is limited by **one CPU thread, the emulated GPU command processor** (`shadPS4:GpuCommandProcessor`). That thread decodes the guest's PM4 command stream and turns about 2,900 draws per frame into Vulkan work.
- The GPU is mostly idle. There are no shader compiles, GPU waits or presentation waits in the segment.
- Slow frames are the frames with more draws (+13 %): the cost per frame grows with the draw count on that single thread.
- This is the same bottleneck the Bloodborne port documented before parallelising its command processing (`references/bloodborne_pc_windows_port/docs/parallel_gpu.md`).

**Caveats.**
- This segment had about 2,900 draws per frame. The earlier race runs logged about 1,600 per frame (GR2fork `PACKETS` census), so this was a heavier scene or a different event.
- The 43.7 FPS here is therefore **not** comparable with the 51 FPS baseline. A like-for-like comparison needs the same event.
- Per-frame busy values are approximate (busy time is booked per processed block); the segment totals are exact.

## Waits and the command-processor backoff, A/B (2026-10-16)

Build: fork, RelWithDebInfo + LTO, with wait instrumentation (`DCPC_WAIT`, `NoteGpuWrite` in `liverpool.cpp`; profile columns `wait_*`, `backoffs`). Same race segment driven 4×, alternating `DCPC_CP_BACKOFF=1` / `0`. Data: `logs/reference-CUSA00093-20261016-0037*`, `-0041*`, `-0043*`, `-0045*` (+ `profile-*.csv`, `gpu-*.csv`).

| Run | Backoff | Segment | FPS | Slow frames | Draws/frame | Graphics waiting on compute | GPU util |
|---|---|---:|---:|---:|---:|---:|---:|
| 1 | on | 66 s | 37.8 | 56.9 % | 2,798 | 9.3 ms/frame | 24 % |
| 2 | off | 50 s | 38.9 | 52.4 % | 2,968 | 8.6 ms | 25 % |
| 3 | on | 50 s | 38.8 | 52.9 % | 2,945 | 8.8 ms | 26 % |
| 4 | off | 53 s | 41.2 | 44.3 % | 2,822 | 7.7 ms | 27 % |

- **Backoff does not help.** Mean 38.3 FPS with it, 40.1 FPS without. It is now opt-in (`DCPC_CP_BACKOFF=1`); the default is upstream's loop.
- **Every wait is on a label that an emulated GPU queue writes** (EOP / ReleaseMem / WriteData). None is on a label the game's CPU threads write.
- The compute queues spend most of their time waiting, which is normal for async compute.
- **Graphics waits about 8–9 ms per frame on labels written by compute queues.** That is about a third of a 25 ms frame.
- Since all queues run on the one command-processor thread, a graphics wait that no runnable task can satisfy means the compute work that writes the label **has not been submitted by the game yet**. Pausing only delays noticing the submit, which matches the small loss measured with backoff on.
- FPS here (38–41) is lower than the release race (43.7) because this build has `/Ob1` and the profiler on. Use it only for comparing runs of this build.

## Game/command-processor dependency (2026-10-16)

Race (43 s segment, RelWithDebInfo + LTO build, profiler on): 61 % slow frames, mean frame 27.0 ms.
- Graphics waits 9.8 ms per frame, and **100 % of that wait time ended with a write by the game's CPU code** (0 % by a GPU-queue write, 0 % after a late submit).
- In the same segment `Game:Main` spent **≈45 % of its time spinning on the clock**. This is the wait for the previous frame's completion (eboot+0x745720 / +0x745FC0).
- Details and the resulting plan: `docs/parallel-gpu-design.md` §3b.

## Image recycler (step 2), menu A/B (2026-10-16)

Fork commit `fe36a0b`, RelWithDebInfo + LTO, profiler on, game defaults. The user navigated the menu by hand and closed each run after about a minute. Both runs used the same build; only `DCPC_IMAGE_RECYCLE_MB` differs (0 = off, 512 = on). The segments are 20–40 s and 40–60 s after the first frame, before the sampler attached.

| Variant | Segment | Frames | Mean frame ms | Frames > 25 ms | CP busy ms/frame | Draws/frame |
|---|---|---:|---:|---:|---:|---:|
| off | 20–40 s | 757 | 26.44 | 58.0 % | 26.42 | 3,095 |
| off | 40–60 s | 824 | 24.27 | 44.4 % | 24.25 | 2,837 |
| on | 20–40 s | 1,058 | 18.90 | 12.9 % | 18.78 | 3,031 |
| on | 40–60 s | 751 | 18.73 | 10.9 % | 18.71 | 2,581 |

- With the recycler on, about 430 image creates per second are served about 98 % from the pool. The pool settles at about 93 images / 298 MiB with no evictions.
- The user independently reported one variant running noticeably better.
- **Caveat:** the inputs were manual, so the scenes are not identical. Draw counts are comparable. A race A/B is still to be done.

## Deferred command recording, race A/B in one run (2026-10-16)

Fork commit `3f2e8ff`, RelWithDebInfo + LTO, profiler on, game defaults, image recycler on, `DCPC_DEFERRED_RECORDING=2`: on/off alternating every 10 s, with each frame labelled by phase. The user drove one race. Segment between F1 marks: 81 s, 4,573 frames.

| Phase | Mean frame | FPS | Frames > 25 ms | CP work without waits |
|---|---:|---:|---:|---:|
| direct | 17.82 ms | 56.1 | 6.3 % | 12.51 ms |
| deferred | 17.67 ms | 56.6 | 5.3 % | 11.47 ms |

- The difference in FPS is within noise.
- The command processor's work falls by ~1 ms per frame, but the time is spent waiting for the game's main thread instead. Details: `docs/parallel-gpu-design.md` §3d.

## Unattended Release races: frame pacing and the frame-rate target (2026-10-16)

**Method.**
- Fork Release + LTO, game defaults, `scripts\run_reference.ps1 -CopySaves -FrameLog`.
- `DCPC_AUTOPILOT` presses X at 40/52/60 s and holds R2 from 65 s.
- Window: 70–122 s after start, from the present log (`scripts\race_window_stats.py`). The car's path differs between runs, so single-run FPS differences of up to ±15 % are scene noise.
- The game times gameplay with the real clock: race-timer deltas between two screenshots taken 15 s apart are 15.02 / 14.98 s at 120 Hz and 15.015 s at 480 Hz.

| Run | Pacing | FPS | p50 ms | p99 ms | > 1.5× median |
|---|---|---:|---:|---:|---:|
| 60, deferred recording off (×2) | whole 60 Hz vblanks | 59.5 / 58.7 | 16.68 / 16.63 | 19.7 / 33.5 | 0.8 / 1.4 % |
| 60, deferred recording on (×2) | whole 60 Hz vblanks | 58.6 / 59.0 | 16.66 | 33.5 | 1.5 / 1.3 % |
| 60, fine grid + flip schedule | 480 Hz grid, 1/60 s interval | 59.1 | 16.68 | 20.0 | 0.4 % |
| 120, whole 120 Hz vblanks | | 70.8 | 16.28 | 25.2 | 1.5 % |
| 120, fine grid | 480 Hz grid, 1/120 s interval | 104.4 / 78.6 / 94.3 | 9.9 / 11.9 / 10.6 | 22–25 | 8–10 % |

**Profiled 120 FPS runs** (frame-profiler columns, same window):

| Run | FPS | CP work ms/frame | µs/draw | CP idle (waiting for game submits) | Graphics waits (flip) |
|---|---:|---:|---:|---:|---:|
| base | 77.9 | 10.10 | 5.11 | 2.07 | 0.66 |
| `Log.sync=false` | 82.9 | 10.02 | 5.27 | 0.98 | 1.07 |
| flip handed back before present | 86.1 | 10.60 | 5.42 | 2.30 | 0.28 |
| + own flip thread | 80.0 | 9.97 | 5.11 | 2.63 | 0.00 |

| + deferred recording | 76.5 | 8.76 | 4.54 | 4.31 | 0.00 |
| + `gpu_thread_core_reserve=false` | 78.8 | 11.30 | 5.79 | 1.38 | 0.00 |
| + `Log.sync=false` | 85.7 | 10.04 | 5.14 | 1.63 | 0.00 |
| + `Log.sync=false` + stub-spam filter | 84.6 | 10.07 | 5.14 | 1.75 | 0.00 |
| + `Log.sync=false`, readbacks Relaxed | 89.2 | 9.84 | 5.10 | 1.42 | 0.00 |
| + `Log.sync=false`, readbacks Off | 97.9 | 9.62 | 5.20 | 0.57 | 0.06 |
| + `Log.sync=false`, high-resolution guest sleeps (reverted) | 85.5 | 10.01 | 5.09 | 1.68 | 0.00 |

- With the flip handed back before the host presents, and flips on their own thread, the graphics queue no longer waits for the flip.
- Deferred recording takes ~1.2 ms off the command processor, but the game then becomes the limit; FPS does not change.
- The game thread's time at 120 FPS (dc-sampler, RelWithDebInfo):
  - ~39 % guest code;
  - ~21 % blocking waits (guest mutexes and semaphores);
  - ~12 % sleeps;
  - ~8 % waiting for GPU readbacks (`BufferCache::OffloadReadback`).
- GPU readbacks are the largest game-side cost. Precise stays the default because it is the setting recommended for the game; Relaxed and Off are launcher options.
- Guest write faults: ~8,600/s in a race. That is too few for the Bloodborne-style unprotect window to pay.

**Final configuration** (fork `358cba7`, `Log.sync=false` + stub-spam filter, readbacks Precise):

| Target | FPS | p50 ms | p99 ms | > 1.5× median | Race timer vs wall clock |
|---|---:|---:|---:|---:|---|
| 30 | 29.9 | 33.29 | 35.22 | 0.1 % | 30.035 s over 30.034 s |
| 60 | 59.6 | 16.72 | 20.21 | 0.5 % | (checked at 60 Hz earlier) |
| 120 | ~85 (80–98 by scene and readback mode) | ~10.9 | ~22 | ~7–8 % | 15.015 s over 14.997 s |
- At 120 FPS the frame is the command processor's work (~10 ms for ~1,950 draws) plus about 2–2.6 ms in which it waits for the game's next submission. `Game:Main` builds the next frame while the command processor runs the current one, and at 120 FPS it sometimes takes longer.

## GPU readbacks at 120 FPS: where the wait comes from (2026-10-16)

Fork Release + LTO, game defaults, `Log.sync=false`, target 120, unattended race (`DCPC_AUTOPILOT` as above), window 70–122 s. Readback statistics are logged every 10 s (`Readback faults per second …` in the run log).

**Run-to-run drift is larger than the effects.** Four races with an identical configuration gave 85.9, 73.4, 73.9 and 75.8 FPS (the first on a cool machine). Single runs are therefore compared only for large effects. Feature effects below come from **in-run A/B**: `DCPC_READBACK_AB=1` switches the features on and off every 10 s, the frame profiler's `phase` column records which half each frame ran in, and the two halves are compared within one race.

**Precise vs Off is a real cost** (alternating runs, settled machine):

| Readbacks | FPS | CP work ms/frame |
|---|---:|---:|
| Off | 85.6 / 89.1 | 10.36 / 10.12 |
| Precise | 71.3 / 73.2 | 10.80 / 10.91 |

**What the game reads back.**
- About 500 readback faults per second (≈ 6 per frame), ~100 MB/s downloaded. Each costs ~0.5 ms of the faulting thread. `Game:Main` spends ~140 ms/s in them, the TBB workers ~150 ms/s.
- When the copy is recorded, the window's last GPU write is in a batch the GPU has finished in ~50 % of cases, in a submitted batch in ~20 %, and in the batch still being recorded in ~30 %. The copy still waits for everything queued before it.
- **No faulting access touches a byte the GPU wrote** (0 of ~450/s measured). The GPU writes only 4–16 bytes into these pages: ReleaseMem GDS→memory counter copies (4 B, ~620/s) and small RW buffer bindings (12–16 B). The game then reads its own C++ objects on the same 4 KiB page, e.g. a vtable load before a virtual call (eboot+0x6057fb) or a flag test (eboot+0x87f640). The hot page is `0x3000404000`. The cost is page-granularity false sharing, not real GPU→CPU traffic.

**What was tried** (in-run A/B, FPS feature off → on):

| Change | FPS off → on | Effect on the faulting threads |
|---|---|---|
| Second graphics-family queue for readback copies, waiting only for the batch that wrote the window | 89.0 → 85.2 | — |
| Same on the transfer-only queue family (buffers shared concurrently) | 89.6 → 88.7 | copy wait for finished data 0.29 → 0.11 ms |
| Prefetch: windows read in the last second are downloaded with every batch that writes them, finished by a separate thread | 88.6 → 88.9, 66.1 → 67.1 | `Game:Main` readback time ~120 → ~70–95 ms/s, faults halved |
| + submit the batch early after a write to such a window | 77.3 → 77.6 | — |
| Read redirect alone: a read fault that misses every GPU-written byte runs one instruction single-stepped against the unprotected backing view | 87.9 → 87.6 | reads gone, but write readbacks rise (pages stay protected) |
| Prefetch + read redirect (final set), 3 races | 74.4 → 72.8, 73.1 → 71.7, 71.5 → 71.4 | `Game:Main` readback time ~118 → ~50 ms/s; reads needing GPU bytes ≤ 2/s; 9–14k redirected reads/s |

**Why the frame rate does not follow.** dc-sampler on `Game:Main` (RelWithDebInfo + LTO):
- Precise: 7.6 % in the Vulkan semaphore wait of readbacks and 12.0 % sleeping in the game's own frame pacing.
- Prefetch + redirect: 1.5 % waiting and 23.3 % sleeping. The exception dispatch of the redirect costs ~0.6 %.
- The time saved on `Game:Main` becomes sleep, so at 120 FPS `Game:Main` is not the limit. The command processor idles more under Precise (10.8 % vs 8.1 % with Off), but the sampler cannot walk through guest frames, so the remaining Precise cost is not located yet.

**Result.**
- No measurable FPS gain at 120 with Precise. Prefetch and read redirect are kept **opt-in** (`DCPC_READBACK_PREFETCH=1`, `DCPC_READBACK_REDIRECT=1`; `DCPC_READBACK_AB=1` for in-run A/B), so the default behaviour is unchanged.
- An opt-in race rendered identically to a default race (screenshots at 90/105/118 s) and ran without errors.
- They are worth revisiting once the command processor gets faster and the game threads become the limit.
- The second queue and the early submit were dropped.

## Wave 1: AF, post-process AA, P-cores and readback counters (16.10.2026)

Release + LTO, fork `dec0064` plus the Wave 1 changes (dirty build), clang 19.1.5.
Those exact source changes were subsequently committed as `c1f913a`; no runtime behaviour
was changed after the race. The final distribution rebuild updates the embedded revision.
One unattended 125 s run, race window 70–122 s, target 120 FPS, Precise readbacks,
AF 16x, P-core preference on, deferred recording/prefetch/redirect off, asynchronous logging.
AA cycles Off/FXAA/SMAA every ten seconds. Run: `logs/reference-CUSA00093-20261016-112157.log.meta.txt`;
raw present/profile/GPU CSVs have the same stamp. Summary: `docs/data/wave1-race-20261016.json`,
generated by `scripts/wave1_report.py` and checked against `scripts/race_window_stats.py`.

| Measurement, full race window | Result |
|---|---:|
| Host presents (same measure as earlier race tables) | **74.2 FPS** |
| Host present p50 / p99 | 12.69 / 26.39 ms |
| New guest-frame profiler rate | **70.6 FPS** |
| CP work per guest frame | 10.56 ms |
| Draws per guest frame / CP cost per draw | 1,958 / 5.39 µs |
| GPU utilisation / graphics clock mean | 40.1% / 2,578 MHz |

Host presents include redraws of existing frames; `OnGuestFrame` only counts a newly presented
game frame. These are different denominators. CPU counters below use guest frames. Do not call
the 74.2 host present rate 74.2 unique game frames per second.

| AA phase, cleaned guest frames | Seconds | Guest FPS | Draws/frame | CP work/frame |
|---|---:|---:|---:|---:|
| Off | 9.25 | 69.4 | 1,800 | 10.15 ms |
| FXAA | 17.90 | 73.0 | 2,061 | 10.58 ms |
| SMAA 1x High | 19.34 | 69.8 | 1,963 | 10.81 ms |

Cleaned phases exclude ±1 s around screenshots at 95/110 s and 0.25 s after each phase
transition. The car moves through different scenes, and the phase durations/draw counts differ.
**This does not establish a performance gain or rank the cost of the AA modes.** In particular,
the higher FXAA phase FPS is not evidence that adding FXAA makes the emulator faster.
The overall result is within the previous settled Precise range. AF and P-core preference
were not individually A/B tested; their performance gains are unknown.

| New inclusive counters per guest frame | Count | All-thread CPU time | CP-only CPU time |
|---|---:|---:|---:|
| Scheduler submits (all schedulers) | 31.28 | 1.390 ms | 1.283 ms |
| RecordReadback calls | 6.00 | 0.478 ms | 0.478 ms |
| DownloadMemory calls, including no-op calls | 0.21 | 0.129 ms | 0.129 ms |
| Readback Flush/Finish calls | 6.07 | 0.543 ms | 0.543 ms |
| Page-manager Protect calls | 186.57 | 0.645 ms | 0.433 ms |

These scopes overlap and must not be summed. Readback flushes are about one fifth of all
scheduler submits by count, and protection is about 4.1% of CP work. Readback flush time includes
some synchronous DownloadMemory waits. The counters locate measurable work but do not yet
explain the entire historical Precise-vs-Off difference; that needs a controlled readback A/B
and the command-processor profile in Wave 2.

Validation:
- The run reached the 125 s time limit without a runtime critical/assertion or unprotected
  red-zone fault. Existing missing-file and HLE errors remain; this is not an error-free log.
- CPU set detection found P-core mask `0x3c03`. GPU command processor was pinned to `0x2000`
  and Game:Main to `0x1c03`; both API calls succeeded.
- Six in-game PNGs in `logs/wave1-screenshots`: raw/FXAA/SMAA of identical guest frames at
  95 and 110 s, captured before FSR at 1920x1080. All three modes render; visual inspection
  found expected edge filtering and no blank/corrupt output. FXAA also softens HUD text.
- The user requested **one race**: no separate 60 FPS race and no AF-Off/16x image comparison
  were run. AF 16x was enabled throughout. No claim about its visual improvement is made.
- C++ and all four AA shader variants compiled in Release + LTO. No physical input or
  desktop screenshots were used. Original game/GR2Launcher folders remained read-only.

## Wave 2: descriptor templates, sampler hints and readback batching (16.10.2026)

**Result: no demonstrated FPS gain. The bundle remains opt-in (`DCPC_PERF=1`); the default is 0.**
Runtime sources committed locally as `2eb396c`; test builds were the preceding dirty tree.
The final disabled-path guard avoids entering the template helper when the experiment is off;
no game data or synchronization behaviour was changed after the completed trials.
FXAA + AF 16x + Precise readbacks and P-core preference remain the normal settings.

First, the current baseline was built as RelWithDebInfo + LTO and sampled at 500 Hz in the
70–125 s race window (`logs/sample-CUSA00093-20261016-114540/shadPS4_GpuCommandProcessor/report.txt`).
Exclusive samples: NVIDIA driver module 21.28%, NtProtectVirtualMemory 3.46%, BindTextures 2.27%,
FindImageMemoized 2.18%, GetProgram 2.10%, GatherSpecKeyImpl 1.88%, BindBuffers 1.48%.
This build's FPS is not compared to Release. The profile motivated the three contained trials
described in `docs/runtime-fork.md`; the page manager already coalesces contiguous changes,
so no wider deferred protection scheme was introduced.

Two **completed Release + LTO control races**, target 120, fixed image quality, fresh isolated
test users with the source shader cache copied in, identical autopilot, 125 s limit. The same
binary switches the three features off/on every ten seconds; the second race reverses phases.
Both reached the time limit, with no critical logs, unprotected red-zone faults, device loss,
skip-cache divergence, or descriptor-template creation failures. Game-only screenshots from
95/110 s are retained in `logs/wave2-screenshots/forward` and `reverse`.

| Control race | Baseline guest FPS | Bundle guest FPS | Change | CP work baseline → bundle |
|---|---:|---:|---:|---:|
| Forward (`…-115821`) | 68.74 | 68.97 | +0.33% | 10.78 → 10.90 ms |
| Reverse (`…-120103`) | 68.32 | 67.52 | −1.17% | 10.34 → 11.03 ms |
| Pooled | 68.57 | 68.13 | **−0.65%** | see machine-readable report |

These differences do not establish a benefit; scene/draw counts still vary. The bundle must
not be presented as a performance upgrade. Host presents (including repeats) for the two full
windows were 68.41 and 67.00/s; they are not the table's unique guest-frame rate.

**Correction to the initial +6% reading.** Both races created **58 new pipelines in baseline
phases**, totalling 1,147.7 / 1,126.5 ms CPU time. In the first race this was near 119 s; in the
second, near 106 s. The copied source cache was incomplete for these scene variants. Despite
reversed phases, both compilation bursts happened to fall in baseline. Before accounting for
that, the apparent guest FPS gains were +6.19% / +6.96%, pooled +6.15%. Those are confounded,
not an optimisation result. The corrected primary table excludes the first 0.5 s after each
phase change, ±1 s around screenshots, and **2 s before / 1 s after each pipeline-creation
sample**. No pipeline creations remain in either primary phase. Original compilation-including
values and exclusion windows are retained in the JSON, not overwritten without evidence.

New paths were exercised (~2,169 template updates per enabled guest frame). However, readback
flush counts stayed near six per frame and their inclusive CP time did not materially improve.
The apparent fall in `readback_record_cp_ms` is **not a speedup**: the flush moved outside that
scope into command-drain exit. Use the separate flush / total CP measurements. Sampler hints
were not measured alone; neither were the three components separately attributed.

Reproduce the corrected analysis:
`python -I scripts/wave2_report.py logs/reference-CUSA00093-20261016-115821.log.meta.txt logs/reference-CUSA00093-20261016-120103.log.meta.txt --out docs/data/wave2-ab-20261016.json`.
The raw CSVs and metadata share each run's stamp. One earlier prototype crashed before the
race (`…-115347`); a Vulkan-Hpp payload overload bug was corrected before both completed runs.
The debugger smoke run `…-115502` timed out during startup and is not a race result.

Remaining work: find a material reduction in per-draw work or the unexplained Precise penalty.
These experiments did not deliver it. Parallel GPU processing, resolution scaling and game
patches remain outside this wave's agreed scope.

## Relaxed / Off readback controls (16.10.2026)

Two completed 125 s races, in the requested order Relaxed then Off, using final clean
Release + LTO `2eb396c`, FXAA, AF16, P-core preference on, target 120, `DCPC_PERF=0`.
Existing isolated test user/cache reused; original source saves recopied before each race.
Original game/launcher folders were read-only. Distribution settings remain Precise.
The harness temporarily overrides `GPU.readbacks_mode` and restores it after each run.

| Mode | Clean unique guest FPS | CP work proxy/frame | Readback flushes/frame | Flush CP ms/frame | GPU utilisation, full window |
|---|---:|---:|---:|---:|---:|
| Precise, previous forward phase 0 | 68.74 | 10.78 ms | 6.01 | 0.561 | 37.4% |
| Precise, previous reverse phase 0 | 68.32 | 10.34 ms | 5.90 | 0.456 | 36.4% |
| Relaxed (`…-122844`) | **74.07** | 10.10 ms | 5.29 | 0.395 | 40.5% |
| Off (`…-123101`) | **85.95** | 10.11 ms | **0** | **0** | 44.5% |

Primary window 70–122 s, excluding +/-1 s around 95/110 s screenshots and 2 s before /
1 s after pipeline creation; old Precise phases also exclude the first 0.5 s after toggles.
Both new races had **no pipeline creations in the full race window**. Primary sample durations:
Relaxed 47.98 s / Off 48.01 s. Raw whole-window host present rates: 74.55 / 86.39 per second.
Primary p99 guest frame intervals: 27.346 / 23.823 ms. These include normal gameplay stalls.

Relative to pooled previous Precise 68.57 guest FPS, Relaxed is about +8.0% and Off +25.3%;
Off is +16.0% vs Relaxed. **Off includes a broken/black 3D scene for part of the measured
window, so its number is not a quality-preserving speedup.** These are observed cross-run
differences, not isolated causal
effect sizes: laptop thermal drift can reach ~15%, paths diverge after collisions, and old
Precise comes from prior A/B phases. Old/new Release builds differ only by the final disabled
template-helper guard in the baseline path. No contemporaneous Precise race was added.
The three mode settings are not image-correctness-equivalent.

Readback work actually changes: Off has zero RecordReadback/download/readback-flush events;
submits fall from ~30–31 to 23.94 per guest frame. Relaxed retains 5.29 flushes/frame.
Protect CP costs are still ~0.40–0.45 ms/frame in all modes: CPU write tracking remains.
CP idle falls from 3.77/4.30 ms in old Precise phases to 3.40 ms Relaxed and 1.52 ms Off.
Thus the Off difference is not explained by a large reduction in per-draw translation cost
(Relaxed/Off both ~10.1 ms per frame). Changed synchronization and work-arrival gaps matter.

**Bottleneck certainty:** profiles locate the limiting domain in CPU-side emulated GPU
command processing / driver submission / producer-consumer synchronization, with substantial
host-GPU headroom, rather than demonstrating a single dominant function. The symbol profile
has 21.28% exclusive samples in the NVIDIA driver, several 1–3% resource/pipeline functions,
3.46% NtProtectVirtualMemory and 17.33% waiting in ZwWaitForAlertByThreadId. Driver exported
symbol names are coarse aliases, not evidence of repeated `vkGetInstanceProcAddr` lookups.
The CP `busy - wait_gfx` measure is a wall-time proxy, not exclusive CPU execution; async
queue label waits overlap graphics processing and cannot be subtracted or summed as serial
stall time. Neither the full Precise penalty nor the exact remaining critical path is solved.

Visual evidence: original in-game 1920x1080 images, before host FXAA/FSR, archived in
`logs/readback-screenshots-20261016/{relaxed,off}`. Both new modes show cockpit, road, snow,
spectators, lighting, mirrors and HUD at 95 s, with no obvious missing geometry/texture blocks.
They are not pixel-identical scenes: collision history and camera pose differ. At 110 s,
Relaxed has a blurred/skewed frame with partly clipped HUD during a collision-like camera
motion; this cannot be attributed to readbacks from one image. **Off's 110 s image is almost
entirely black in the 3D scene while HUD remains visible.** This initially needed checking
against a possible brief respawn fade.

**Targeted Off visual follow-up (`…-123342`): reproduced a persistent problem.** No profiler
or FPS capture; same Release, FXAA/AF16, mode Off and autopilot, with extra in-game shots.
At 95 s the world renders normally. All **eight** captures at 102/106/108/110/112/114/118/121 s
show a nearly black world with visible, updating HUD and minimap. At 118 s there is a
"Reset to Track" countdown; the 121 s shot still has the dark world after the minimap position
changes. This persists across sampled points spanning **19 seconds**, not just one captured
fade frame. It occurred in both Off runs; no comparable black-world image is present in the
available Precise/Relaxed captures. This establishes a serious observed visual problem under
Off, although the exact missing-data/postprocess/game-state mechanism is not diagnosed.
The follow-up is not included in FPS comparisons: extra screenshot stalls and disabled profiling
make it a visual diagnostic only. Nine original PNGs: `logs/readback-screenshots-20261016/off-followup`.

Recommendation: retain **Precise** as the default; **Off is not suitable as an equivalent
performance preset in this tested race**. Relaxed is a candidate for a longer visual check,
not yet demonstrated equivalent to Precise. Static captures do not establish freedom from
transient errors or correctness on other tracks. Validation: `logs/readback-validation-20261016.json`.

Both primary runs reached the time limit; no critical/assert/device-loss or skip-cache
unsound events were found. Existing missing-file/HLE errors remain. Reproduce the analysis:

```powershell
python -I scripts/readback_report.py precise-forward=logs/reference-CUSA00093-20261016-115821.log.meta.txt precise-reverse=logs/reference-CUSA00093-20261016-120103.log.meta.txt relaxed=logs/reference-CUSA00093-20261016-122844.log.meta.txt off=logs/reference-CUSA00093-20261016-123101.log.meta.txt --out docs/data/readbacks-20261016.json
```

## Combined CPU-path controls on Relaxed (16.10.2026)

Owner requested trying both next CPU targets together. Implemented locally in `e253a89`:
short-gap merging of partial buffer/sampler descriptor writes, plus producer unlock-before-
notify and skipping empty mapped queues without locking. Readback waits/data ordering are
unchanged. Details: `docs/runtime-fork.md`, combined CPU-path experiment.

Two completed 125 s Release + LTO races, fixed **Relaxed + FXAA + AF16 + P-core preference**,
target 120, with the two features switching together every ten seconds. Earlier templates /
sampler hints / readback batching and CP backoff were explicitly disabled. Second race reverses
phase order. Existing isolated shader cache reused; source saves recopied before each run.
No original game/launcher data written. Test binary was the dirty `93b4885` tree; final commit
adds only formatting/comment placement after those semantic edits, followed by a clean rebuild.

| Race | Bundle off, unique FPS | Bundle on, unique FPS | Observed change | CP work proxy off -> on |
|---|---:|---:|---:|---:|
| Forward (`…-125453`) | 71.71 | 72.50 | +1.10% | 10.329 -> 10.259 ms |
| Reverse (`…-125706`) | 70.60 | 74.41 | +5.40% | 10.841 -> 10.923 ms |
| Pooled | **71.08** | **73.33** | **+3.16%** | 10.616 -> 10.552 ms |

Primary selection: 70–122 s, exclude the first 0.5 s after toggles and +/-1 s around
95/110 s screenshots; usual pipeline-creation exclusion also applied. **No pipelines were
created anywhere in either full race window**, so this result has no earlier compilation
confound. Clean off/on durations: forward 19.76/25.68 s; reverse 25.68/19.76 s.
Whole-window host present rates (repeats included): 73.23 / 73.05; not the table's guest FPS.

**Interpretation: small positive signal, not a proven general 3% gain.** Both orders favour
the bundle, but effects vary and scene/collision/thermal drift still exists. Pooled draw counts
are close (1,919.5 -> 1,914.8/frame), yet per-draw CP cost barely changes (5.531 -> 5.511 us).
Most observed frame-time improvement is CP idle falling from 3.450 to 3.084 ms/frame, rather
than a large reduction in command translation cost. This is not a diagnosis of all remaining
idle time, and the two features were not attributed separately.

| Pooled counters per guest frame | Off | On |
|---|---:|---:|
| Ordinary Pipeline push calls | 2,057 | 2,052 |
| Descriptor write headers | 7,431 | 7,322 |
| Descriptor elements sent | 16,925 | 16,953 |
| Unchanged gap elements included | 0 | 86 |
| Empty queue mutex acquisitions avoided | 0 | 1,956 |

This is a ~1.5% reduction in write headers, with a small increase in descriptor payload;
it does not remove all descriptor or driver work. The counters establish both paths ran.
Frame-time tail did **not** improve: p99 off -> on was 28.144 -> 28.785 ms forward and
27.492 -> 28.159 ms reverse. Do not advertise smoother frame pacing from this test.

Four game-only 1920x1080 screenshots (95/110 s, two races) are archived under
`logs/cpu-path-screenshots/{forward,reverse}`. All were visually inspected: cockpit, track,
lights, mirrors and HUD render; no black-world failure or obvious missing geometry/texture
blocks. Different collision/camera poses prevent pixel-equivalence claims. Both runs reached
the limit, with zero critical/assertion/device-loss/skip-cache-unsound log matches. Existing
HLE/missing-file messages remain. Evidence: `logs/cpu-path-validation-20261016.json`.

**Final policy: keep opt-in.** Defaults stay Relaxed / FXAA / AF16, `DCPC_PERF=0`, new flags 0.
For this bundle set `DCPC_PERF=1`, `DCPC_PERF_DESCRIPTOR_RANGES=1`,
`DCPC_PERF_SUBMIT_HANDOFF=1`, and the earlier three feature flags to 0. No push to remote.
The clean committed Release + LTO build was packaged to `dist/Driveclub-PC`; all 2,474
existing user files retained identical hashes. Evidence: `logs/cpu-path-dist-verification-20261016.json`.

Reproduce in two separate PowerShell invocations:

```powershell
.\scripts\run_cpu_path_ab.ps1
.\scripts\run_cpu_path_ab.ps1 -Reverse
python -I scripts/wave2_report.py logs/reference-CUSA00093-20261016-125453.log.meta.txt logs/reference-CUSA00093-20261016-125706.log.meta.txt --out docs/data/cpu-path-ab-20261016.json
```

## Race-start workload: the usual benchmark omits the pack launch (16.10.2026)

The owner reports 40–50 FPS at the start among other cars. Existing data supports short
intervals in that range, but the prior **70–122 s benchmark does not include this start**.
At the 95 s captures the race clock is already ~39.7 s, placing race timing around 55.3 s
after process start. Autopilot starts throttle only at 65 s, so it also lets the pack move
away before accelerating. Later results (~73 FPS) must not be described as representative
of an immediate launch in traffic. Previous optimisation A/B results remain valid only for
their selected later scene/windows, not an asserted improvement of the pack start.

Reanalysis of existing Relaxed races, no new race or runtime changes:

| Run | 55–60 s guest FPS | Draws/frame | CP work proxy/frame | GPU use | 80–90 s guest FPS |
|---|---:|---:|---:|---:|---:|
| Earlier Relaxed (`…-122844`) | 66.2 | 3,184 | 13.44 ms | 39.0% | 78.4 |
| CPU forward (`…-125453`) | 64.2 | 3,189 | 14.13 ms | 39.2% | 72.0 |
| CPU reverse (`…-125706`) | 54.6 | 3,191 | 16.48 ms | 35.2% | 76.1 |

All these intervals have zero pipeline creations. Raw one-second bins in the reverse run:
53–54 s 50.3 FPS, 54–55 s 53.8, **55–56 s 47.0**; 52–53 s is 35.9 FPS but includes the
transition into the grid and should not be called exclusively active racing. The forward
run has 45.8 FPS at 52–53 s. This confirms short lows, **not a sustained 40–50 FPS average
throughout all starts**. Caps, builds and manually driven paths can differ from these tests.
The old manually driven heavy-scene profile also averaged 43.7 FPS at ~2,856 draws/frame
with a nearly continuously busy command processor, but it used an older Precise build.

Start workload is ~3,200 draws versus ~1,900 later (~+67%). CP idle at 55–60 s is only
1.44/1.83 ms in the two recent controls, compared with 3.51/2.51 ms at 80–90 s. There is
more serial CP work and less idle to recover: wakeup tweaks are unlikely to solve the pack
start alone. Cars/render passes plausibly contribute, but these counters do not attribute
individual draws to car geometry, shadows, reflections or scenery.

Next proposed priorities (not implemented by this analysis):

1. Establish an actual pack-start benchmark with throttle held before the countdown ends,
   dedicated start screenshots and separate start/later statistics. Profile the 53–65 s
   interval under Relaxed before choosing another fine-grained optimisation.
2. Investigate separating mostly static texture/sampler bindings from changing per-draw
   buffer bindings, with descriptor/layout compatibility and generation guards retained.
   This is a broader descriptor/payload change than merging write headers; first identify
   its cost in the start profile. Existing plan/spec/input memos already remove substantial
   repeated work, so blindly adding another memo is not a distinct solution.
3. Evaluate profile-guided optimisation of the Release + LTO runtime using an actual traffic
   workload. Local clang tools include llvm-profdata; the instrumented/use build flow still
   needs implementation and verification. No FPS estimate is established; driver time and
   dependencies are outside its direct scope.
4. A larger later phase could prepare pure per-draw data on workers, validate it on the
   serial CP and fall back on mismatch. The local Bloodborne reference documents such a
   design (`references/bloodborne_pc_windows_port/docs/parallel_gpu.md`), but Driveclub has
   no such worker path and the other title's gains do not transfer as predictions. This
   remains outside the currently authorised implementation wave.

Reproduce: `python -I scripts/race_start_report.py logs/reference-CUSA00093-20261016-122844.log.meta.txt logs/reference-CUSA00093-20261016-125453.log.meta.txt logs/reference-CUSA00093-20261016-125706.log.meta.txt --out docs/data/race-start-20261016.json`.
The JSON retains both five/ten-second and one-second bins, raw with no A/B exclusions.

## Race-start benchmark: pack launch, default vs CPU-path bundle (16.10.2026)

New harness `scripts/run_start_bench.ps1` (`-Variant default|cpupath`, `-EventAt`, default 30). The autopilot:
- presses X at 12 s (skips the intro video) and again at 14 s (dismisses "Welcome", which sometimes waits for a button);
- starts the event at 30 s and holds throttle from 31 s through the countdown, so the car launches with the pack;
- takes snapshots at 32 s and 64 s.

The race clock in the 64 s snapshot dates the race start (≈33.4 s). A run takes ~66 s instead of 125 s. Analysis: `scripts/start_bench_report.py`; windows are relative to the race start. Data: `docs/data/start-bench-20261016.json`, snapshots in `logs/start-bench-screenshots/`.

Fixed settings: Relaxed, FXAA, AF 16x, P-core preference, target 120, Release + LTO `e253a89`. `cpupath` adds `DCPC_PERF=1` + descriptor ranges + submit handoff (earlier trial flags off). The pair below ran back to back with the same schedule and ended at the same spot (race clock 0:30.635 / 0:30.640). No pipeline creations in any window.

| Run | 0–10 s (pack launch) | 10–30 s | 0–30 s | Worst second | Draws/frame 0–10 s | CP work 0–10 s | CP idle 0–10 s |
|---|---:|---:|---:|---:|---:|---:|---:|
| default (`…-133533`) | **59.9** | 66.7 | 64.4 | 49.5 | 3,230 | 15.86 ms | 0.82 ms |
| cpupath (`…-133416`) | **56.7** | 66.4 | 63.2 | 49.5 | 3,233 | 16.07 ms | 1.55 ms |
| default, earlier schedule (`…-132751`, start 23.3 s) | 60.3 | 69.2 | 66.2 | 54.9 | 3,231 | 15.53 ms | 1.07 ms |

FPS values are unique guest frames per second.

- **The pack launch runs at ~57–60 FPS on average, with single seconds at ~50.** The first second after the start is the worst in every run.
- At the start the frame has ~3,230 draws and ~16 ms of command-processor work, with only ~1 ms of idle. It is purely bound by the command processor: there is nothing to win on the game side or in wake-ups.
- **The CPU-path bundle gives no gain at the start** (−5 % in 0–10 s, equal later). This is a single pair, so it does not prove a loss, but it rules out a useful gain. It stays opt-in.
- Observation, cause not established: in three earlier attempts with `cpupath` and a single X at 12 s, "Welcome" stayed up until the next X press (until 23, 32 and 47 s). The default run with the same single press moved on by itself at ~15 s. The double press at 12/14 s avoids the problem, so it did not affect the measured pair.

### Command-processor profile of the pack launch (16.10.2026)

RelWithDebInfo + LTO `e253a89`, default settings, `run_start_bench.ps1 -BuildName dcpc-relwithdebinfo-lto -Sample -Offset 7`. This build boots ~7 s later, hence the offset; race start 40.3 s. dc-sampler at 500 Hz on `shadPS4:GpuCommandProcessor` for 32.9 s from the start, 14,646 samples. Report: `logs/sample-CUSA00093-20261016-134222/`. The FPS of this build (0–10 s: 49.3, CP work 19.5 ms, idle 0.75 ms) is not comparable with Release.

| Where the thread's time goes (exclusive) | Share |
|---|---:|
| NVIDIA driver (`nvoglv64`; symbol names are coarse aliases) | 21.8 % |
| Kernel submit calls (`win32u` DxgK submit/signal) | 4.9 % |
| Page protection (`NtProtectVirtualMemory`, CPU write tracking) | 5.0 % |
| Waiting (`ZwWaitForAlertByThreadId`, incl. 0.55 % readback waits) | 8.7 % |
| Emulator per-draw work, spread: `BindTextures` 2.8, `FindImageMemoized` 2.6, `GatherSpecKeyImpl` 2.6, `GetProgram` 2.4, `RunGraphicsPackets` 2.3, `BindBuffers` 1.4, `BindResources` 1.4, `BindVertexBuffers` 1.0, `AccessBuffer` 1.0 | each ≤ 2.8 % |
| Stream copy lane (`DrainProducer`, `Push`, `Completed`, `ClaimAndCopy`) and `CopySparseMemory` | ~3–4 % exclusive |

- **The largest single block is the Vulkan driver itself: ~27 % of the thread** (driver plus kernel submits). At ~3,200 draws per frame that is roughly 1.5–2 µs of driver time per draw.
- The emulator's own per-draw code has no single hotspot; it is spread over many 1–3 % functions.
- The stack walker does not attribute driver time to its caller (20 % of driver samples have no Driveclub-PC frame). Which Vulkan calls cost the most needs a per-call-type count, not this profile.
- Candidates, in order of effort:
  1. Move driver calls off the command-processor thread with the existing opt-in deferred recording (`DCPC_DEFERRED_RECORDING=1`). Earlier it saved ~1 ms of CP work but did not help in a game-limited later race window; the pack launch is CP-bound with < 1 ms idle, so it has not been tested where it matters.
  2. Count Vulkan calls per draw by type and remove redundant binds and dynamic-state sets.
  3. Replace push descriptors with descriptor buffers (`VK_EXT_descriptor_buffer`): a larger change that targets the per-draw driver cost directly.
  4. Cheaper CPU write tracking (5 % here).

### Vulkan calls by function at the pack launch (16.10.2026)

Fork `7cea0ed` adds `DCPC_VK_STATS=1` (`src/driveclub/vk_call_stats.*`). Every `vkCmd*` plus submit, command buffer begin/end, descriptor updates and synchronisation calls is counted and timed with the TSC. Command-processor and other threads are kept apart.
- Per-frame totals go to profiler columns `vk_cp_calls`, `vk_cp_ms`, `vk_other_ms`.
- `DCPC_VK_STATS_WINDOW=a:b` writes a per-function table.
- Harness: `run_start_bench.ps1 -VkStats`, which copies the table to `logs/vk-call-stats-*.csv`.
- The thunks add roughly 15–20 ns per call, so the run is not an FPS reference.

Release + LTO, default settings, one run (`…-135628`, race start 33.3 s, table `logs/vk-call-stats-default-135628.csv`).

| Window | Guest FPS | Draws/frame | CP work | CP Vulkan calls/frame | CP time in the driver |
|---|---:|---:|---:|---:|---:|
| 0–10 s | 56.3 | 3,237 | 16.95 ms | 18,787 | **5.10 ms** |
| 10–30 s | 71.0 | 2,057 | 11.47 ms | 12,341 | 3.77 ms |

Per function, 30 s table (window 32–62 s after device creation, 1,994 guest frames, 2,329 draws/frame, CP 14,056 calls and 4.13 ms per frame):

| Group | CP ms/frame | Calls/frame | Cost per call |
|---|---:|---:|---:|
| Queue submits + command buffer begin/end | **1.06** | 34 submits | **27.5 µs per submit** |
| Descriptors (push descriptor set 2,475/frame, update/bind sets, push constants) | 0.81 | ~3,400 | push 271 ns |
| Pipeline barriers | 0.59 | 409 | **1.45 µs** |
| Pipeline binds | 0.58 | 945 (0.41 per draw) | 612 ns |
| Begin/end rendering | 0.32 | 174 + 174 | begin 1.49 µs |
| Draws and dispatches | 0.28 | ~2,600 | 40–570 ns |
| Vertex/index buffers, vertex input | 0.22 | ~4,800 | 38–80 ns |
| Other dynamic state | 0.08 | ~1,000 | 20–140 ns |

Other threads spend 2.4 ms/frame in the driver, mostly the presenter (`vkQueuePresentKHR` 0.64 ms) and semaphore waits. Those are not on the command-processor path.

**What this points at:**
1. **Queue submits are the largest single cost**: ~34 per frame at ~28 µs each, mostly interval/ring-drain flushes and readback flushes. Two ways to attack it: fewer submits (`flush_draw_interval` and `ring_drain_flush_draws` are existing settings, so this is a settings experiment), or submitting from a separate thread.
2. **Barriers** cost 1.45 µs each and **pipeline binds** 0.6 µs. Coalescing barriers and checking how many of the 945 binds per frame are redundant are contained changes.
3. **Descriptor pushes** are many but cheap individually. Descriptor buffers would address them, but it is a large change for ~0.8 ms.
4. **Deferred recording** (`DCPC_DEFERRED_RECORDING=1`) would move all ~4–5 ms to the recorder thread. With this table it can now be checked directly: the CP-thread driver time should fall and the "other" time should rise.

### Fewer submits and deferred recording at the pack launch (16.10.2026)

Same Release + LTO build (`7cea0ed`) with `-VkStats`, default settings (Relaxed, FXAA, AF 16x, P-cores). Three separate runs, so drift applies; the car paths diverged after collisions, as the later-phase draw counts show. Data: `docs/data/start-submit-deferred-20261016.json`, tables `logs/vk-call-stats-default*-1356..1413*.csv`.

| Run | 0–10 s FPS | 10–30 s FPS | CP work 0–10 s | CP idle 0–10 s | CP driver ms/frame (30 s) | Other threads' driver ms/frame | Submits/frame | Game:Main write-fault readbacks |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| default (`…-135628`) | 56.3 | 71.0 | 16.95 ms | 0.81 ms | 4.13 | 2.36 | 35.7 | 125–160 ms/s |
| fewer submits: `ring_drain_flush_draws=0`, `flush_draw_interval=1024` (`…-141158`) | 57.7 | **60.3** | 13.94 ms | 3.39 ms | 3.65 | 5.53 | 9.3 | **260–360 ms/s** |
| `DCPC_DEFERRED_RECORDING=1` (`…-141315`) | 56.8 | 72.3 | 14.15 ms | 3.45 ms | **1.11** | 6.00 | 32.1 | 120–178 ms/s |

- **Both changes take ~3 ms of work per frame off the command processor at the launch, but FPS does not follow: its idle time grows by the same ~2.6 ms.** Once the command processor is lighter, it waits for the game's next submission.
- **What holds the game back here is the readback waits on CPU writes.** In Relaxed mode reads no longer fault, but writes into pages that hold GPU-written bytes do. `Game:Main` then waits for the download (~200 faults/s, 12–16 % of its time). With fewer submits each readback waits for a bigger batch, so the time doubles to 26–36 % and the later phase loses 15 %. Fewer submits is therefore **not** viable on its own.
- Deferred recording works as intended: driver time moves from the command processor (4.13 → 1.11 ms/frame) to the recorder thread, and rendering is correct (snapshots).
- **Next hypothesis:** deferred recording only pays once the game's write-fault waits shrink. The existing opt-in prefetch (`DCPC_READBACK_PREFETCH=1`, commit `dec0064`) downloads hot windows ahead and halved write-fault time in the earlier Precise test.

**Combination: `DCPC_DEFERRED_RECORDING=1` + `DCPC_READBACK_PREFETCH=1`** (`…-141959`, same build and settings, `-VkStats`, one run):

| | 0–10 s FPS | Worst second | 10–30 s FPS | Draws/frame 10–30 s | CP idle 0–10 s | Game:Main write-fault readbacks | Readback flushes/frame |
|---|---:|---:|---:|---:|---:|---:|---:|
| default (`…-135628`) | 56.3 | 48.2 | 71.0 | 2,057 | 0.81 ms | ~200/s, 125–160 ms/s | 5.1 |
| deferred only (`…-141315`) | 56.8 | 45.6 | 72.3 | 1,969 | 3.45 ms | 120–178 ms/s | 4.9 |
| **deferred + prefetch** | **60.5** | **53.4** | 64.2 | **2,615** | 2.20 ms | **~70/s, 36–78 ms/s** | 1.2 |

FPS values are unique guest frames per second.

- Prefetch does what it should: Game:Main's write-fault waits fall to about a third, and readback flushes on the command processor drop from ~5 to ~1 per frame.
- **The launch is the best of today's VkStats runs: 60.5 FPS, worst second 53.4.** It is about equal to the earlier default runs without stats (59.9 / 60.3), though, and this is a single run, so a gain is suggested but not established.
- The 10–30 s phase is not comparable: this car path had ~27 % more draws per frame (2,615 vs ~2,000).
- "Other threads' driver time" (9 ms/frame) includes the prefetch finisher's semaphore waits; it is not work on the critical path.
- Confirming it needs a back-to-back pair (default vs combination, without `-VkStats`), or both in one run.

**Confirming pair, without `-VkStats`**: back to back, same build and settings. Data: `docs/data/start-deferred-prefetch-pair-20261016.json`.

| Run | 0–10 s FPS | Draws/frame 0–10 s | Worst second | CP work / idle 0–10 s | 10–30 s FPS (draws/frame) | Game:Main write-fault readbacks |
|---|---:|---:|---:|---:|---:|---:|
| default (`…-142808`) | **59.9** | 3,215 | **53.9** | 15.44 / 1.26 ms | 69.1 (2,358) | ~205/s, 80–115 ms/s |
| deferred + prefetch (`…-142919`) | 56.9 | 3,233 | 49.4 | 14.53 / 3.04 ms | 72.4 (2,057) | ~60–100/s, 32–69 ms/s |

**Result: not confirmed.**
- At the launch, with the same workload (~3,220 draws per frame), the combination is about 5 % *slower*. The 60.5 FPS of the single run was drift.
- The later phase is not comparable: the default run drove through a heavier scene, with 15 % more draws.
- The mechanisms work: the command processor is lighter and Game:Main's write-fault waits roughly halve. Yet the freed time again becomes command-processor idle (1.3 → 3.0 ms). Something other than readbacks or command-processor work then limits the frame.
- Both features stay opt-in. Fewer submits and deferred recording, alone or with prefetch, are not a gain at the launch.

### Game threads at the pack launch: system calls, faults, own code (16.10.2026)

Fork `aeda58c` adds `DCPC_GUEST_STATS=1` (`src/driveclub/guest_stats.*`).
- Every HLE function the game calls is timed in `Core::HostCallWrapperImpl`, as exclusive time; the guest fault handler is timed too.
- Threads are grouped into Game:Main, TBB workers and others.
- Harness: `run_start_bench.ps1 -GuestStats`, which writes `logs/guest-stats-*.csv`.
- Overhead: two TSC reads and two atomic adds per call.

Release + LTO, default settings, one run (`…-143944`, 30 s window from the race start, 1,968 guest frames, 65.6 FPS). The first attempt (`…-143636`) booked only the outermost call and showed 0 ms of HLE for Game:Main, because the thread runs inside a wrapped entry; it is superseded.

**Game:Main, per second of wall time:**

| What | ms/s | Calls/s | Per call |
|---|---:|---:|---:|
| `sched_yield` (spin-wait loop) | 189 | 248,800 | 0.76 µs |
| `sceKernelGetProcessTime` (polled in the same loop) | 29 | 615,200 | 0.05 µs |
| **pthread mutex lock + unlock** | **~155** | ~70,000 locks | **1.7 µs per lock** |
| **Write-fault handling** (CPU write tracking, incl. ~200/s readbacks) | **119** | 8,700 | 13.7 µs |
| `sceKernelWaitSema` (blocking) | 40 | 650 | 62 µs |
| Other HLE (Gnm, pad, TLS, …) | ~10 | | |
| **Game's own code + unmeasured** (incl. kernel exception dispatch) | **445** | | |

**TBB workers (all together):** `posix_sem_wait` 2,795 ms/s (idle workers parked), **mutex lock 535 + unlock 76 ms/s (105,000 locks/s, 5.1 µs per lock)**, `sched_yield` 252 ms/s, write faults 65 ms/s (1,000/s, 64 µs each).

**What this shows:**
- Game:Main is busy for only about half of the time with its own code. ~220 ms/s is spin-waiting (`sched_yield` + time polling), i.e. it waits for other threads: TBB jobs or frame completion.
- **Mutexes are expensive.** On Windows `TimedMutex` (`core/libraries/kernel/sync/mutex.h`) is a kernel mutex object (`CreateMutex` / alertable `WaitForSingleObjectEx` / `ReleaseMutex`). Every lock and unlock is a system call even without contention: 1.7 µs on Game:Main, 5.1 µs on the workers, ~770 ms/s in total.
  - A user-space fast path (atomic compare-and-swap, a kernel wait only on contention) would remove most of this.
  - It has to keep recursive/error-check semantics, timeouts and the alertable wait the signal and suspend mechanism relies on.
- **Write tracking costs Game:Main 12 %**: 8,700 faults/s at 13.7 µs each. Coarser or cheaper tracking for pages the game rewrites every frame is the second target.
- These are emulator overheads on the game threads, which do not show up in the command-processor profile.

### Fast mutex for guest pthread mutexes (16.10.2026)

Fork `3ba48a7`: `TimedMutex` (`core/libraries/kernel/sync/mutex.*`), the lock behind every guest pthread mutex, gets a user-space fast path.
- The state lives in an atomic word: 0 free, 1 locked, 2 locked with possible waiters.
- A contended lock spins 64 pauses, then sleeps on an auto-reset event in an alertable wait, as before. An unlock that sees state 2 sets the event.
- Recursion and ownership stay in `PthreadMutex`.
- **On by default**; `DCPC_FAST_MUTEX=0` restores the kernel mutex object.

Race start, Release + LTO, default settings, `-GuestStats` in both runs (same overhead): `…-143944` before, `…-144658` after. Data: `logs/guest-stats-default-143943.csv` / `-144658.csv`.

| | Original mutex | **Fast mutex** |
|---|---:|---:|
| Lock cost, Game:Main / TBB workers | 1.70 / 5.11 µs | **0.09 / 0.13 µs** |
| Mutex time, Game:Main / TBB workers | ~155 / ~610 ms/s | **~14 / ~21 ms/s** |
| Game:Main HLE time | 436 ms/s | 336 ms/s |
| Guest FPS 0–10 s (draws/frame) | 58.3 (3,227) | **61.5** (3,218) |
| Guest FPS 10–30 s (draws/frame) | 68.7 (2,155) | **75.1** (2,352) |
| Whole 30 s / worst second / p99 | 65.2 / 55.1 / 29.9 ms | **70.6 / 57.3 / 26.6 ms** |
| CP idle 0–10 s / 10–30 s | 1.40 / 3.18 ms | **0.52 / 0.91 ms** |

- The first change today that **moves FPS**: +5.5 % at the launch, and +9 % later despite 9 % more draws per frame.
- The mechanism is visible in the numbers: the command processor's wait for the game nearly disappears. The game now keeps up, and the command processor is the limit again. That also means the earlier command-processor savings (deferred recording, fewer submits) are worth retesting on top of this.
- Single pair, so drift applies, but the measured lock cost (×20–40 cheaper) and the collapse of CP idle make the direction clear.
- Checked: boot, menus, loading and the race run without errors; snapshots render normally. Not yet checked: long sessions, other events and the 30/60 targets.

**Without statistics, and deferred recording retested on top** (`…-145111` fast mutex, `…-145222` + `DCPC_DEFERRED_RECORDING=1`, back to back). Data: `docs/data/start-fastmutex-deferred-20261016.json`.

| Run | 0–10 s FPS | Worst second | p99 0–10 s | CP idle 0–10 s | 10–30 s FPS (draws/frame) |
|---|---:|---:|---:|---:|---:|
| before the fast mutex (`…-142808`, from the earlier pair) | 59.9 | 53.9 | 32.8 ms | 1.26 ms | 69.1 (2,358) |
| **fast mutex** | **63.9** | **60.1** | 26.8 ms | 0.23 ms | 86.4 (2,031) |
| fast mutex + deferred recording | 63.7 | 58.0 | 28.0 ms | 0.70 ms | 75.1 (2,473) |

- **The fast mutex holds without statistics: the launch goes from 59.9 to 63.9 FPS (+6.7 %), and the worst second from 53.9 to 60.1.** The 10–30 s phases are not comparable, since draws per frame differ by up to 20 %.
- **Deferred recording still brings nothing** at the launch: 63.7 vs 63.9 FPS, and the command processor's work barely changes (15.42 → 15.00 ms). It stays off.

### Queue submits on a separate thread (16.10.2026)

Fork `ab979cb`: the rasterizer scheduler's `vkQueueSubmit` calls go, in order and with unchanged batch sizes, to a submit thread (`dcpc:QueueSubmit`).
- Submits of the presenter and flip schedulers first wait until that thread's queue is empty, so a batch waiting on a rasterizer tick never overtakes it on the queue.
- The swapchain drains the thread before `waitIdle`.
- **On by default**; `DCPC_ASYNC_SUBMIT=0` restores direct submission.

Race start, Release + LTO with the fast mutex, back-to-back pair (`…-152621` submit thread, `…-152733` without). Data: `docs/data/start-async-submit-20261016.json`.

| | Without submit thread | **With submit thread** |
|---|---:|---:|
| 0–10 s FPS (draws/frame) | 68.5 (3,228) | **80.9** (3,228) |
| Worst second | 63.9 | **77.5** |
| p99 0–10 s | 24.9 ms | 24.6 ms |
| 10–30 s FPS (draws/frame) | 98.6 (2,031) | 101.9 (2,306) |
| CP work 0–10 s | 14.54 ms | **12.29 ms** |
| CP time in submits, 0–10 s | 1.39 ms/frame | 0.44 ms/frame (enqueue only) |

- **+18 % at the pack launch** with an identical workload, and the worst second improves from 64 to 77 FPS.
- The command processor saves 2.25 ms per frame, more than the 0.95 ms of submit time it no longer spends. The driver's submit evidently also cost it lock and cache effects.
- The baseline (68.5) is higher than in the earlier fast-mutex runs (63.9) because the laptop cooled down during the implementation. That is the usual drift; the pair itself ran back to back, the submit-thread run first.
- Single pair; rendering checked from snapshots, no errors. Not yet checked: window resize/HDR switch (drained before `waitIdle`), long sessions, 30/60 targets.

### Profile-guided optimisation (PGO) (16.10.2026)

`scripts/build_pgo.ps1`: an instrumented Release + LTO build (`build_reference.ps1 -Pgo generate`, `-fprofile-instr-generate`), then one recording run, then an optimised build from the merged profile (`-Pgo use -PgoProfile build\pgo\driveclub.profdata`, `-fprofile-instr-use`).
- The recording run covers intro, menus, loading and ~75 s of the race.
- The autopilot step `pgodump` writes the profile, because the harness terminates the process. The instrumented build is much slower, so the recording presses X every 3 s instead of using fixed timings.
- Each build is a full build in its own directory (`gr2fork-dcpc-pgogen`, `gr2fork-dcpc-pgouse`). Redo after code changes.

Race start, both builds from `ab979cb` (fast mutex + submit thread), back-to-back pair, Release first so cooling does not favour PGO (`…-191232` Release, `…-191347` PGO). Data: `docs/data/start-pgo-20261016.json`.

| | Release | **Release + PGO** |
|---|---:|---:|
| 0–10 s FPS (draws/frame) | 67.7 (3,226) | **74.6** (3,227) |
| Worst second | 63.4 | **69.0** |
| 10–30 s FPS (draws/frame) | 76.2 (2,408) | **82.2** (2,463) |
| p99 0–10 s | 26.0 ms | 24.8 ms |
| CP work 0–10 s / 10–30 s | 13.74 / 11.28 ms | **12.45 / 10.29 ms** |

- **+10 % at the launch, +8 % later**, from the compiler alone: no code or image changes. The command processor needs ~9 % less time per frame.
- Both runs rendered normally; no errors.
- This evening's absolute numbers are lower than the afternoon's (Release 67.7 vs 80.9 at the launch with the same code). That is machine drift; only the back-to-back pair is comparable.
- Two earlier attempts this evening (`…-190946`, `…-191057`) did not reach the race. The game booted ~9 s late (cold disk cache after a long pause), so the fixed autopilot presses missed. Booting once warms the cache. A boot-relative schedule would make the harness robust against this.

### Minor tuning round: submit interval, P-core pinning, AVX2 (16.10.2026 evening)

All runs: race start, default settings, plain Release + LTO (no PGO). Data: `docs/data/start-flush-interval-20261016.json` and `docs/data/start-pin-avx2-20261016.json`.

**Submit interval** (`GPU.flush_draw_interval` / `GPU.ring_drain_flush_draws`, build `ab979cb`):

| Setting | 0–10 s | 10–30 s | 0–30 s | Submits/frame |
|---|---:|---:|---:|---:|
| 384 / 64 (default, `…-200449`) | **72.2** | 80.9 | **78.0** | 20.7 |
| 192 / 32 (`…-200221`) | 70.5 | 81.5 | 77.8 | 38.0 |
| 128 / 32 (`…-200329`) | 69.2 | 81.6 | 77.5 | 42.1 |

More frequent submits bring nothing, even though they are cheap now. The default stays. The first 384/64 attempt (`…-200114`) missed the race start and is excluded.

**P-core pinning and priority for the submit/recorder threads** (`25f2bef`) **and an x86-64-v3 (AVX2) build** (`build_reference.ps1 -ArchFlags '/clang:-march=x86-64-v3'`, build `gr2fork-dcpc-avx2`; `/arch:AVX2` is rejected by zlib-ng's `-Werror`). Three back-to-back runs (`…-202804`, `…-202912`, `…-203019`):

| Build | 0–10 s | 10–30 s | 0–30 s | Worst second |
|---|---:|---:|---:|---:|
| `ab979cb` | 87.7 | 104.7 | 99.1 | 84.2 |
| `25f2bef` (pinning + priority) | 87.5 | 103.8 | 98.4 | 83.4 |
| `25f2bef` + x86-64-v3 | 86.8 | 103.9 | 98.2 | 82.5 |

- **No measurable effect from either.** The pinning stays (harmless); the AVX2 build is not used, since it would also limit the exe to newer CPUs.
- Barrier merging and pipeline-bind dedup were not implemented. Barriers are already collected and only written on a real hazard. Graphics binds are already deduplicated; compute binds would save at most ~0.1 ms/frame, but need extra invalidation in the FSR/AA/fault-manager passes.
- **Harness note:** a newly built exe (or one in a new place) boots ~9 s late on its first launch, very likely an antivirus scan of the new file. The fixed start-test schedule then misses the race. One warm-up launch (`run_reference.ps1 -Seconds 20`) avoids it. This also explains the misses this evening.
- The plain Release now reaches ~87–88 FPS at the launch and ~104 later in this (cool) state, compared with 67–72 two hours earlier with the same code: drift remains large, and only back-to-back runs are comparable.

## Owner's manual play, Precise default again (16.10.2026 evening)

The owner's own races, without instruments: the original shadPS4 dropped to 35 FPS. With the dist build and Relaxed readbacks they got 90–100 FPS, 70–80 in hard parts, but **shadows and vegetation glitched**. With Precise they got 75–90 FPS through the whole race, with occasional small stutters. **Precise is the default again** (launcher and the runtime's default game config). The start-benchmark numbers above were measured with Relaxed, so they are not directly comparable with Precise play.

Manual profiling, so the stutters can be examined in real play: `scripts/run_manual_profile.ps1`. The first F1 starts the Vulkan-call and game-thread tables (`DCPC_STATS_TRIGGER`), and the second F1 writes them and closes the game. The report comes from `scripts/manual_profile_report.py`.

## Earlier menu measurement (15.10.2026)

Without input the game sits in its menu. From 60 s on the present rate is stable. Measured window 90–230 s, fullscreen, game window **not** in the foreground. The harness cannot bring it to front without injecting input, which was removed as too intrusive.

| Run | avg FPS | median ms | p95 ms | 1% low | Data |
|---|---:|---:|---:|---:|---|
| user binary, redzone off | 56.3 | 16.68 | 33.1 | 24.2 | `frames-CUSA00093-20261015-220028.csv` |
| self Release+LTO, redzone off (pilot) | 57.0 | 16.69 | 31.5 | 26.1 | `frames-CUSA00093-20261015-215520.csv` |

The planned 4×2 menu matrix was stopped after the first run in favour of the race benchmark at the user's request. These two runs are indicative only.
