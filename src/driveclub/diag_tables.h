// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: aggregate tables for the F1-F1 window of a manual profiling session
// (DCPC_PROFILE=1 and DCPC_STATS_TRIGGER, see stats_trigger.h), written to user/log when the
// second F1 arrives:
//   readback_faults.csv  guest read faults on GPU-written pages, and write faults that took
//                        >= 50 us (a download first): kind, faulting instruction (RIP), page,
//                        thread, count and handling time (what the game reads back)
//   readback_downloads.csv  Precise downloads by window and thread: count, bytes, time incl.
//                        the GPU wait
//   image_churn.csv      texture-cache images created / deleted, grouped by address, size,
//                        format, tile mode and creation site
// Every hook is a relaxed atomic load while the window is not running.

#pragma once

#include "common/types.h"

namespace DriveclubPC::DiagTables {

bool Active();

void NoteFault(bool write, u64 rip, u64 address, u64 ns);
void NoteDownload(u64 window, u64 bytes, u64 ns);
// site: 0 overlap recreate, 1 expand, 2 new, 3 stencil, 255 delete
void NoteImage(u32 site, u64 guest_address, u32 width, u32 height, u32 format, u32 tile_mode,
               u64 guest_size);

/// Frame writer thread (frame profiler), once per guest frame.
void OnFrame();

} // namespace DriveclubPC::DiagTables
