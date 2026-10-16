// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: diagnostic split of the game threads' time between the emulator's system
// functions (HLE calls), guest memory fault handling and the game's own code.
//
// DCPC_GUEST_STATS=1 times every HLE function the guest calls (Core::HostCallWrapperImpl, so
// every LIB_FUNCTION export) as exclusive time (nested HLE calls excluded), and the guest fault
// handler. Threads are grouped: Game:Main, TBB workers, all other guest threads.
//
// Output:
// - frame profiler columns main_hle_ms, main_fault_ms (time of Game:Main per guest frame);
// - with DCPC_GUEST_STATS_WINDOW=<from>:<to> (seconds after Install) a per-function table for
//   that window in user/log/guest_stats.csv and the log. For Game:Main, wall time minus HLE and
//   fault time is the game's own code plus anything not measured (kernel exception dispatch).
// The cost is two TSC reads and two locked adds per HLE call.

#pragma once

#include <atomic>
#include "common/types.h"

namespace DriveclubPC::GuestStats {

extern bool g_active;

inline bool Active() {
    return g_active;
}

/// Registers an HLE function under its wrapper signature; returns its id.
u32 Register(const char* signature);

/// Booked by Scope; also used by the fault handler (fault_kind 0 = read, 1 = write).
void BookCall(u32 id, u64 ticks);
void BookFault(bool write, u64 ticks);

/// Times one HLE call; books its exclusive time (minus nested HLE calls).
class Scope {
public:
    explicit Scope(u32 id);
    ~Scope();

private:
    u32 id_;
    u64 start_;
    u64 parent_child_ticks_;
};

/// Call once after start-up (with VkCallStats::Install); reads the environment.
void Install();

/// Per guest frame totals of Game:Main since the previous call (frame profiler writer).
/// Also drives the DCPC_GUEST_STATS_WINDOW report.
void TakeFrame(double& main_hle_ms, double& main_fault_ms);

} // namespace DriveclubPC::GuestStats
