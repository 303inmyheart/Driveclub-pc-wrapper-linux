// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <string_view>

namespace DriveclubPC {

inline bool IsValidGameTitleId(std::string_view id) {
    return id.size() == 9 && id.starts_with("CUSA") &&
           std::all_of(id.begin() + 4, id.end(), [](char c) { return c >= '0' && c <= '9'; });
}

inline bool IsReferenceBuild(std::string_view id, std::string_view version) {
    return id == "CUSA00093" && version == "01.28";
}

inline bool IsRenderResolution(int lines) {
    return lines == 720 || lines == 1080 || lines == 1440 || lines == 2160;
}

inline std::string_view RenderScaleArgument(unsigned lines) {
    switch (lines) {
    case 720: return "-resolutionscale 0.666667";
    case 1440: return "-resolutionscale 1.3333";
    case 2160: return "-resolutionscale 2";
    default: return {};
    }
}

} // namespace DriveclubPC
