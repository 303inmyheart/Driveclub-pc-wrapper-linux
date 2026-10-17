// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: 60 FPS unlock with correct game speed for the unmodified v1.28 eboot.
//
// Some installs carry a modded v1.28 eboot that changes three sites; a clean v1.28 eboot runs at
// a 30 FPS flip interval and, when the emulator presents faster, the simulation speeds up. The same
// three byte changes are applied in memory here, only where the original bytes match.
// DCPC_FPS_UNLOCK=0 disables it.

#pragma once

#include "common/types.h"

namespace DriveclubPC::FpsUnlock {

/// Patches the loaded eboot image at base (before static code patching).
void Apply(VAddr base, u64 size);

} // namespace DriveclubPC::FpsUnlock
