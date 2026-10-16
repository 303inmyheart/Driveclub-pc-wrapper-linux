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
//   wait_gpu_label_ms     part of those waits that ended after an emulated GPU queue wrote the
//                         awaited address (EOP, EOS, ReleaseMem, WriteData) during the wait
//   wait_cpu_label_ms     part that ended without any GPU-queue write to it: the value was written
//                         by the game's CPU code
//   waits                 number of blocked waits that completed in the frame
//   backoffs              command processor backoff steps taken while every task was blocked
//   gfx_wait_late_ms      graphics wait time where the queue that writes the awaited label received
//                         a new submission from the game *after* the wait began (game submitted
//                         late)
//   gfx_wait_queued_ms    graphics wait time where it did not (the work was already queued)
//   vk_cp_calls, vk_cp_ms Vulkan calls the command processor made and the time it spent in the
//                         driver; vk_other_ms the driver time of all other threads
//                         (DCPC_VK_STATS=1, otherwise 0; see vk_call_stats.h)
//   main_hle_ms, main_fault_ms  Game:Main time in HLE calls and in guest fault handling
//                         (DCPC_GUEST_STATS=1, otherwise 0; see guest_stats.h)
//   Stutter diagnostics (events counted in the frame, see Event):
//   resident_binds, resident_kb  buffer-cache arena blocks made resident (vkAllocateMemory +
//                         sparse bind) and their size; sparse_submits  vkQueueBindSparse calls
//   image_creates, image_deletes  texture-cache images created / deleted (garbage collector)
//   image_uploads, image_upload_kb  CPU-written images uploaded (detiled) to the GPU
//   file_reads, file_read_kb  game file reads (streaming)
//   gpu_ms, gpu_max_ms, gpu_batches  GPU execution time of the command processor's batches
//                         completed in the frame (completion time minus the later of its submit
//                         and the previous batch's completion), the longest batch, batch count
//   submit_delay_max_ms, present_hold_max_ms  see MaxStat
// Every 600 frames the command processor also logs the wait addresses with the most time and which
// game threads submitted to which queue.

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
// driveclub-pc: verifies that the Wave 2 fast path actually executes in each A/B phase.
void AddTemplateUpdate();
void AddDescriptorPush(u32 writes, u32 elements);
void AddDescriptorGapFill(u32 elements);
void AddEmptyQueueSkips(u64 count);

// driveclub-pc: stutter diagnostics; any thread, no-op unless profiling is enabled.
enum class Event : u8 {
    ResidentBinds,
    ResidentKb,
    SparseSubmits,
    ImageCreates,
    ImageDeletes,
    ImageUploads,
    ImageUploadKb,
    FileReads,
    FileReadKb,
    Count
};
void AddEvent(Event event, u64 value = 1);

// Largest value in the frame.
//   SubmitDelay  a batch's wait on the submit thread: hand-over to vkQueueSubmit done
//   PresentHold  the presenter holding the queue-submit lock around vkQueuePresentKHR
// Columns submit_delay_max_ms, present_hold_max_ms.
enum class MaxStat : u8 { SubmitDelay, PresentHold, Count };
void AddMax(MaxStat stat, u64 ns);

// GPU execution time of one command processor batch (from the GPU-completion watcher).
void AddGpuBatch(u64 ns);

// A/B phase written with every frame (column "phase"), for features that alternate at run time.
void SetPhase(u32 phase);

// Called by the command processor for every address a GPU queue writes (fences, labels).
// queue: 0 = graphics, otherwise the Gnm virtual compute queue id.
void NoteGpuWrite(u64 address, u32 queue);

// Called on the game's thread when it submits work to a queue (same numbering).
void NoteSubmit(u32 queue);

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
    u64 gpu_writes_at_start_ = 0;
};

// Called by the presenter for every presented guest frame.
void OnGuestFrame();

// Event trace of a short window (DCPC_TRACE_AT=<seconds after the first frame>,
// DCPC_TRACE_MS=<length, default 300>), written to user/log/frame_trace.csv with
// columns ns,kind,thread,queue,address,value. Needs DCPC_PROFILE=1.
//   gpu_write   value at a fence/label address right after a queue wrote it
//   wait_begin  a queue started blocking on an address (value = what it holds now)
//   wait_end    the wait was satisfied (value = what it holds now)
//   submit      the game submitted to a queue
//   flip        a guest frame was presented
enum class TraceKind : u8 { GpuWrite, WaitBegin, WaitEnd, Submit, Flip };
void Trace(TraceKind kind, u64 address, u32 queue);
/// gpu_write for an address a queue just wrote.
void TraceGpuWrite(u64 address, u32 queue);

u64 NowNs();

// driveclub-pc: inclusive CPU timings; *_cp_ms isolates the command processor thread.
enum class Cost : u8 { Submit, ReadbackRecord, Download, ReadbackFlush, Protect, Count };
void MarkCommandProcessorThread();
class CostScope {
public:
    explicit CostScope(Cost cost);
    ~CostScope();

private:
    Cost cost;
    u64 start;
};

} // namespace DriveclubPC::Profiler
