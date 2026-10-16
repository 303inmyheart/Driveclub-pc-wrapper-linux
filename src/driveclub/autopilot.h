// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: scripted controller input and screenshots for unattended test runs.
//
// DCPC_AUTOPILOT holds a schedule of steps "<seconds>:<action>" separated by ';', timed from
// the start of the emulator. Actions:
//   press:<button>[:<ms>]   press and release (default 150 ms)
//   hold:<button>           press and keep pressed
//   release:<button>
//   axis:<axis>:<value>     set an axis (0..255; sticks centre at 128)
//   shot                    save a game-only screenshot (path in the log)
//   repeat:<seconds>:<action>  from now on, also run <action> every <seconds>
// Buttons: cross circle square triangle up down left right l1 r1 l2 r2 l3 r3 options touchpad.
// Axes: lx ly rx ry l2 r2.
// The input goes to the emulated pad of player 1, exactly like a physical controller's;
// nothing is sent to the host's windows or input devices.
//
// Example: "40:shot;45:press:cross;50:shot;60:hold:r2;60:repeat:15:shot"

#pragma once

namespace DriveclubPC::Autopilot {

/// Starts the schedule thread when DCPC_AUTOPILOT is set. Call after the controllers exist.
void Start();

} // namespace DriveclubPC::Autopilot
