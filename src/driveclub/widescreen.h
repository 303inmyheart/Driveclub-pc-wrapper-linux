// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: ultrawide output for displays wider than 16:9.
//
// Based on rafidwayne's Driveclub_Widescreen (github.com/rafidwayne/Driveclub_Widescreen,
// GPL-2.0-or-later): camera aspect, video-out width and a Hor+ FOV factor in the v1.28 eboot.
// Extended here so the game also renders its scene at the wide size instead of stretching a
// 16:9 image: the engine's scene-size table gets wider widths, and its -resolutionscale height
// (width * 9 / 16) is replaced by table height * scale.
//
// The launcher picks the output size (ComputeOutput) from the display or window aspect and the
// render resolution lines, and passes it as DCPC_WIDESCREEN=<w>x<h> (plus DCPC_RENDER_RES for the
// video-out buffers, render_res.h). DCPC_WIDESCREEN_SCENE=0 keeps the 16:9 scene (stretched).
// The maths lives in this header so it can be unit tested.

#pragma once

#include "common/types.h"

namespace DriveclubPC::Widescreen {

/// Largest scene the game's memory budget is known to hold (with DCPC_VRAM_EXTRA_MB=1536).
inline constexpr u64 kMaxPixels = 3840ull * 2160ull;
inline constexpr u32 kMaxWidth = 7680;

struct Output {
    u32 width;  ///< video-out width
    u32 height; ///< video-out height (= render lines)
    bool wide;  ///< wider than 16:9
};

/// True if width x height is wider than 16:9.
[[nodiscard]] inline bool IsWide(s32 width, s32 height) {
    return width > 0 && height > 0 && static_cast<s64>(width) * 9 > static_cast<s64>(height) * 16;
}

/// Output size for a display/window of width x height and the chosen render lines (720, 1080,
/// 1440, 2160). 16:9 and narrower keep the 16:9 size. Wider ones get round(lines * aspect), even; lines
/// drop to the next step while the frame is over kMaxPixels.
[[nodiscard]] inline Output ComputeOutput(s32 width, s32 height, u32 lines) {
    if (!IsWide(width, height) || width > 16384 || height > 16384) {
        return {lines * 16 / 9, lines, false};
    }
    const auto wide_width = [&](u32 l) {
        u32 w = static_cast<u32>((static_cast<u64>(l) * static_cast<u64>(width) +
                                  static_cast<u64>(height) / 2) /
                                 static_cast<u64>(height));
        w &= ~1u;
        return w > kMaxWidth ? kMaxWidth : w;
    };
    for (u32 l : {2160u, 1440u}) {
        if (lines >= l && static_cast<u64>(wide_width(l)) * l > kMaxPixels) {
            lines = l == 2160 ? 1440 : 1080;
        }
    }
    const u32 w = wide_width(lines);
    if (w <= lines * 16 / 9) {
        return {lines * 16 / 9, lines, false};
    }
    return {w, lines, true};
}

/// Camera aspect ratio for an output size.
[[nodiscard]] inline float Aspect(u32 width, u32 height) {
    return static_cast<float>(width) / static_cast<float>(height);
}

/// Hor+ FOV factor: the game's 0.5 at 16:9, scaled with the aspect (same as the community
/// "FOV fix for wide resolutions" at 21:9).
[[nodiscard]] inline float Fov(u32 width, u32 height) {
    return 0.5f * Aspect(width, height) / (16.0f / 9.0f);
}

/// A scene-table width widened for the output aspect, rounded to a multiple of 128 like the
/// engine's own -resolutionscale widths.
[[nodiscard]] inline u32 SceneWidth(u32 table_width, u32 width, u32 height) {
    const double w = static_cast<double>(table_width) * Aspect(width, height) / (16.0 / 9.0);
    const u32 aligned = static_cast<u32>((w + 64.0) / 128.0) * 128u;
    return aligned < table_width ? table_width : aligned;
}

/// Patches the loaded eboot image at base from DCPC_WIDESCREEN (nothing when unset).
/// Every site is checked for its v1.28 value first; any mismatch skips the whole patch.
void Apply(VAddr base, u64 size);

} // namespace DriveclubPC::Widescreen
