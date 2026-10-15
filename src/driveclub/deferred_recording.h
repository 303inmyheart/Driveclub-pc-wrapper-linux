// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: Vulkan command recording on a separate thread.
//
// The command processor thread spent ~18 % of its time inside the driver recording commands
// (docs/parallel-gpu-design.md §3c). The Bloodborne port moved recording to its own thread
// (gpu/shadps4 Scheduler::Record, vk_draw_pipe). This is the minimal form of that idea for this
// fork: every vkCmd* entry of vulkan.hpp's default dispatcher is replaced by a thunk. A command
// for one of the rasterizer scheduler's command buffers (the "targets") is copied, with every
// array and structure it points to, into a chunk and executed later, in order, by the recorder
// thread. Other command buffers are recorded directly as before.
//
// Correctness rules:
// - A command with arguments the thunks cannot copy (an unknown pointer argument or a pNext
//   chain) first waits until the recorder has run everything before it, then runs directly on
//   the calling thread ("sync"; counted per function in the statistics).
// - Calls that touch command buffers, command pools or objects a recorded command may still
//   reference (begin/end/reset/allocate/free, descriptor pool resets, destroys) wait for the
//   recorder first ("drain"). Command buffers are submitted only after vkEndCommandBuffer, so
//   a submitted buffer is always fully recorded.
// - Producers need no lock of their own: Vulkan already requires callers to synchronise access
//   to a command buffer, and the recorder only ever reads what a producer published.
//
// DCPC_DEFERRED_RECORDING=1 enables it (default off while it is being validated).

#pragma once

#include "video_core/renderer_vulkan/vk_common.h"

namespace DriveclubPC::DeferredRecording {

/// Replaces the dispatcher's entries; call once after VULKAN_HPP_DEFAULT_DISPATCHER.init(device).
/// Returns whether deferred recording is enabled.
bool Install();

bool Enabled();

/// The command buffers whose commands are deferred (null for none). Called by the scheduler
/// whenever it begins one.
void SetTargets(VkCommandBuffer primary, VkCommandBuffer upload);

/// Waits until the recorder has run every command published so far.
void Drain();

/// Wakes the recorder if it sleeps with work pending; for producers that are about to idle.
void Kick();

} // namespace DriveclubPC::DeferredRecording
