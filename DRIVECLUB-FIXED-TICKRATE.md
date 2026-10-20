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
