// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: diagnostic count and driver time of every Vulkan call, by function.
//
// DCPC_VK_STATS=1 replaces every vkCmd* entry of vulkan.hpp's default dispatcher, plus queue
// submission, command buffer begin/end, descriptor updates and synchronisation calls, with a thunk
// that counts the call and the TSC ticks spent in the driver. Calls on the GPU command processor
// thread and on all other threads are kept apart. Installed before deferred recording, so with
// both enabled the recorder thread's driver time shows up under "other".
//
// Output:
// - frame profiler columns vk_cp_calls, vk_cp_ms, vk_other_ms (needs DCPC_PROFILE=1);
// - with DCPC_VK_STATS_WINDOW=<from>:<to> (seconds after device creation) a per-function table
//   for that window, per guest frame, in user/log/vk_call_stats.csv and the log.
// The thunks cost two TSC reads and a few stores per call, so totals include some overhead.

#pragma once

#include "common/types.h"

namespace DriveclubPC::VkCallStats {

/// Replaces the dispatcher's entries; call once after VULKAN_HPP_DEFAULT_DISPATCHER.init(device)
/// and before DeferredRecording::Install().
void Install();

bool Enabled();

/// Per guest frame totals since the previous call; called by the frame profiler's frame writer.
/// Also drives the DCPC_VK_STATS_WINDOW report.
void TakeFrame(u64& cp_calls, double& cp_ms, double& other_ms);

} // namespace DriveclubPC::VkCallStats
