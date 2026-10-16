// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: target frame rate.
//
// Driveclub flips on every emulated vblank (it never calls sceVideoOutSetFlipRate), so the
// vblank frequency is its frame-rate cap (docs/parallel-gpu-design.md §3e):
//   480 Hz vblank grid and at most one flip per 1/target s (30, 60 or 120): a frame is shown
//   on the next ~2 ms vblank after it is ready instead of waiting for a whole 8.3/16.7 ms one.
//   DCPC_CLASSIC_VBLANK=1 restores PS4-style pacing (60 or 120 Hz vblank; 30 = every second).
// The target comes from the launcher settings or DCPC_TARGET_FPS (which wins, for tests).

#pragma once

#include "common/types.h"

namespace DriveclubPC::FrameRate {

/// Applies the target to the emulator settings; call before the video-out driver starts.
/// `configured` is the launcher's value (0 = leave the emulator settings alone).
void Apply(u32 configured);

/// Minimum flip rate imposed on the game (0 = none): vblanks per flip minus one.
s32 MinFlipRate();

/// Minimum time between two flips in ns (0 = none); used with a fine vblank grid.
u64 MinFlipIntervalNs();

} // namespace DriveclubPC::FrameRate
