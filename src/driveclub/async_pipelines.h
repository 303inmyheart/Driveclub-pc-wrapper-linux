// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: graphics pipeline creation off the command processor thread.
//
// A new graphics pipeline used to be created inside the draw that first needs it, which blocks the
// command processor for the whole driver compile (tens of ms for a pipeline missing from the
// driver's cache). With DCPC_ASYNC_PIPELINES=1 the pipeline object is still built on the command
// processor (keys, layouts, shader modules, serialisation data), but the vkCreateGraphicsPipelines
// call runs on a small worker pool. Draws that need a pipeline that is not ready yet are skipped
// (counted in the profiler column pipeline_skips), so the object appears a few frames later
// instead of the frame stalling. Compute pipelines stay synchronous: a skipped dispatch could
// leave data the game reads back undefined.
//
// DCPC_ASYNC_PIPELINES=1 enables it (default off), DCPC_ASYNC_PIPELINES_THREADS (default 2) sets
// the worker count.
//
// DCPC_ASYNC_SHADERS=1 moves the vertex/fragment shader translation (GCN -> IR -> SPIR-V and the
// shader module) to the same pool (PipelineCache::AsyncCreateProgram); draws needing a program
// or permutation still being translated are skipped and counted in pipeline_skips too. Compute,
// geometry and tessellation stages stay synchronous.

#pragma once

#include <functional>

#include "common/types.h"

namespace DriveclubPC::AsyncPipelines {

bool Enabled();

/// Runs job on a worker thread.
void Submit(std::function<void()> job);

/// Counts a draw skipped because its pipeline was still compiling.
void CountSkip();

/// Draws skipped and jobs completed since the last call (for the frame profiler).
u64 TakeSkips();

} // namespace DriveclubPC::AsyncPipelines
