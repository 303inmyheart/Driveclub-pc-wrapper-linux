// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <string_view>

namespace DriveclubPC {

// Tested patch reference, not a whitelist of allowed game IDs.
inline constexpr std::string_view ReferenceTitleId = "CUSA00093";
inline constexpr std::string_view ReferenceAppVersion = "01.28";

inline bool IsValidGameTitleId(std::string_view id) {
    return id.size() == 9 && id.starts_with("CUSA") &&
           std::all_of(id.begin() + 4, id.end(), [](char c) { return c >= '0' && c <= '9'; });
}

inline bool IsReferenceBuild(std::string_view id, std::string_view version) {
    return id == ReferenceTitleId && version == ReferenceAppVersion;
}

inline bool IsRenderResolution(int lines) {
    return lines == 480 || lines == 576 || lines == 720 || lines == 1080 ||
           lines == 1440 || lines == 2160;
}

// Sub-native rendering lowers the 3D scene only. The menus/HUD still use the native
// output coordinate system; shrinking their framebuffer crops the interface.
inline unsigned PresentationLines(unsigned scene_lines) {
    return scene_lines < 1080 ? 1080 : scene_lines;
}

inline std::string_view RenderScaleArgument(unsigned lines) {
    switch (lines) {
    case 480: return "-resolutionscale 0.444444";
    case 576: return "-resolutionscale 0.533333";
    case 720: return "-resolutionscale 0.666667";
    case 1440: return "-resolutionscale 1.3333";
    case 2160: return "-resolutionscale 2";
    default: return {};
    }
}

} // namespace DriveclubPC
