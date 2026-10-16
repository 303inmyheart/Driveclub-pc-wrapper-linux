// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace DriveclubPC::Performance {
// driveclub-pc: one control for exact baseline/optimised comparisons at fixed image quality.
enum class Feature {
    DescriptorTemplates,
    SamplerHints,
    ReadbackBatch,
    DescriptorRanges,
    SubmitHandoff
};
bool Enabled(Feature feature);
void OnGuestFrame();
} // namespace DriveclubPC::Performance
