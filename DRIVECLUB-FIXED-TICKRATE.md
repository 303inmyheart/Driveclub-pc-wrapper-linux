# Fixed tickrate experiment — 20.10.2026

Opt-in only: `DCPC_FIXED_TICKRATE=1`. Default timing is unchanged.
Based on kalaposfos' **60 FPS with fixed tickrate** v1.28 patch:
https://github.com/shadps4-emu/ps4_cheats/blob/main/PATCHES/Driveclub.xml

The upstream change at XML address `0x01762801` replaces the first character of
`fixedFramesPerSecond` with `g`. Our image-relative address is `0x1362801`.
Validate the whole NUL-terminated string and all three known deltatime sites
before changing anything. Restore known deltatime bytes to their original
values in memory, then apply this alternative. Unknown bytes skip the entire
alternative with an error; the deltatime path is not applied on that invocation.
`DCPC_FPS_UNLOCK=0` disables both patch paths.

This does not establish a replay-camera fix or support for 120 FPS. First test
at target 60, comparing race-clock speed and the same replay. The separate
manual launcher selects 60, 1440p and a private copy of the owner's test saves.
Game files, production dist and `gr2fork-dcpc-stage2-next` stay unchanged.

Owner's baseline feedback before this experiment: at 1080p/60 and 1440p/120,
audio sounded normal and no clear rendering errors were observed. The latter
included a demanding rainy race, third-person, first-person and cockpit views.
This is local manual feedback, not AMD validation or a controlled comparison
against 0.2.1. Replay shaking, including the race HUD, remains unresolved.

## Real elapsed-time experiment

Owner follow-up: fixed tickrate at target 60 was stable without replay shaking.
At target 120, actual ~80 FPS appeared ~25–30% faster, consistent with a fixed
60-Hz simulation step per rendered frame. This rules out shipping that mode
as a general 120-FPS solution. Rain is provisionally accepted, not PS4-verified.

`DCPC_FIXED_TICKRATE=1` plus `DCPC_FIXED_TICKRATE_REALTIME=1` tests the renamed
engine option with only the elapsed-time helper changed (`0x66306e`: `75 07`
to `90 90`). The fixed-fps check and flip interval are restored to their
original bytes. All existing signature checks run before any writes.
The new flag has no effect without the fixed-tickrate flag. Defaults unchanged.

Separate artifact: `build/gr2fork-dcpc-fixed-realtime`, private user and saves,
1080p, target 120, FPS counter enabled. Use `Start-Fixed-Realtime.cmd`;
the executable alone uses default deltatime. This is an experimental combination,
not an established speed or replay fix. Compare game-clock seconds against wall
time and repeat the replay test; neither host FPS nor a successful boot proves it.
