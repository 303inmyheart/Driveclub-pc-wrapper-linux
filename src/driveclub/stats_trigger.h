// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: manual window for the diagnostic tables (VkCallStats, GuestStats).
//
// DCPC_STATS_TRIGGER=<file>: the harness writes "start" into the file at the first F1 press and
// "stop" at the second (scripts/run_reference.ps1 -MarkKey F1). The tables then cover exactly
// the time between the two presses instead of a DCPC_*_STATS_WINDOW in seconds.

#pragma once

namespace DriveclubPC::StatsTrigger {

enum class State { Waiting, Running, Stopped };

bool Configured();

/// Current state; reads the file at most every 100 ms. Call from the frame writer thread only.
State Poll();

} // namespace DriveclubPC::StatsTrigger
