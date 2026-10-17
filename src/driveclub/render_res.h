// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: experimental internal render resolution (DCPC_RENDER_RES=<width>x<height>).
//
// Driveclub (CUSA00093 v1.28) hard-codes 1920x1080 in eight functions of its eboot (integer and
// float immediates; scripts/find_imm_pairs.py). Before the executable runs, each of those
// immediates is rewritten to the requested size. A site whose bytes do not match the expected
// instruction is skipped and logged, so another eboot version is left untouched.
// Unset: nothing is patched.

#pragma once

#include "common/types.h"

namespace DriveclubPC::RenderRes {

/// Patches the loaded eboot image at base (before static code patching).
void Apply(VAddr base, u64 size);

} // namespace DriveclubPC::RenderRes
