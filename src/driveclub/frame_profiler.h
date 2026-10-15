// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: per-frame CPU-side profile of the runtime, written as one CSV line per presented
// guest frame to user/log/frame_profile.csv. Enabled with the environment variable
// DCPC_PROFILE=1; when disabled every hook is a single relaxed atomic load.
//
// Columns (per guest frame, i.e. between two presented game frames):
//   frame, qpc_ms         frame number and QueryPerformanceCounter time in ms (aligns with
//                         VK_LAYER_DC_framelog and the harness F-key marks)
//   frame_ms              time since the previous presented guest frame
//   cp_busy_ms            GPU command processor thread: time spent processing guest PM4 work
//   cp_idle_ms            same thread: time spent waiting for new guest submissions
//   sched_wait_ms         time the command processor blocked on the host GPU (Scheduler waits)
//   present_wait_ms       presenter: time waiting for a free swapchain frame / its fence
//   pipelines, pipeline_ms  Vulkan pipelines created and the time spent creating them
//   draws, dispatches     guest draws and compute dispatches decoded

#pragma once

#include "common/types.h"

namespace DriveclubPC::Profiler {

bool Enabled();

void AddCpBusy(u64 ns);
void AddCpIdle(u64 ns);
void AddSchedWaitTicks(u64 tsc_ticks);
void AddPresentWait(u64 ns);
void AddPipeline(u64 ns);
void AddDraw();
void AddDispatch();

// Called by the presenter for every presented guest frame.
void OnGuestFrame();

u64 NowNs();

} // namespace DriveclubPC::Profiler
