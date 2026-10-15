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
//   wait_gfx_ms, wait_asc_ms  wall time graphics / compute queues spent blocked in WaitRegMem,
//                         MemSemaphore or Rewind (from the first failed test to success)
//   wait_gpu_label_ms     part of those waits on addresses a GPU queue writes (EOP, EOS,
//                         ReleaseMem, WriteData): waiting on another emulated queue
//   wait_cpu_label_ms     part on addresses no GPU queue wrote: waiting on the game's CPU threads
//   waits                 number of blocked waits that completed in the frame
//   backoffs              command processor backoff steps taken while every task was blocked
// Every 600 frames the command processor also logs the wait addresses with the most time.

#pragma once

#include "common/types.h"

namespace DriveclubPC::Profiler {

bool Enabled();

void AddCpBusy(u64 ns);
void AddCpIdle(u64 ns);
void AddSchedWaitTicks(u64 tsc_ticks);
void AddPresentWait(u64 ns);
void AddPipeline(u64 ns);
// Single writer (the command processor thread): plain stores, no locked RMW.
void AddDraw();
void AddDispatch();
void AddBackoff();

// Called by the command processor for every address a GPU queue writes (fences, labels).
void NoteGpuWrite(u64 address);

enum class WaitQueue : u8 { Gfx, Asc };

// Lives in the coroutine frame around a wait loop: Blocked() before each yield; the destructor
// books the wait. Only used on the command processor thread.
class WaitScope {
public:
    WaitScope(WaitQueue queue, u64 address) : queue_{queue}, address_{address} {}
    WaitScope(const WaitScope&) = delete;
    WaitScope& operator=(const WaitScope&) = delete;
    ~WaitScope();
    void Blocked();

private:
    WaitQueue queue_;
    u64 address_;
    u64 start_ns_ = 0;
};

// Called by the presenter for every presented guest frame.
void OnGuestFrame();

u64 NowNs();

} // namespace DriveclubPC::Profiler
