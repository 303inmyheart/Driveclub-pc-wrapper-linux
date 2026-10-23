// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <iostream>
#include <array>
#include "driveclub/patch_plan.h"
#include "driveclub/game_profile.h"
#include "driveclub/profile_options.h"
#include "driveclub/widescreen.h"

int main() {
    using namespace DriveclubPC;
    assert(IsValidLocalUserName("Rico"));
    assert(IsValidLocalUserName("1234567890123456"));
    assert(IsValidLocalUserName("J\xc3\xb6rg"));
    assert(!IsValidLocalUserName(std::string_view("a\0b", 3)));
    assert(!IsValidLocalUserName("\xc2\x85"));
    for (auto name : {"", "   ", "12345678901234567", "bad\nname", "bad\tname",
                      "\xc3", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80"})
        assert(!IsValidLocalUserName(name));
    assert(IsValidGameTitleId("CUSA00093"));
    assert(IsValidGameTitleId("CUSA00003"));
    assert(!IsReferenceBuild("CUSA00003", "01.28"));
    assert(IsValidGameTitleId("CUSA12345"));
    for (auto id : {"CUSA1234", "CUSA123456", "CUSA12A45", "../CUSA12", "CUSA/1234"})
        assert(!IsValidGameTitleId(id));
    assert(IsReferenceBuild("CUSA00093", "01.28"));
    assert(!IsReferenceBuild("CUSA12345", "01.28"));
    assert(!IsReferenceBuild("CUSA00093", "01.27"));
    assert(!IsReferenceBuild("CUSA00093", "unknown"));
    assert(IsRenderResolution(720) && IsRenderResolution(1080));
    for (const auto lines : {240u, 360u, 480u, 576u}) {
        assert(IsRenderResolution(lines));
        assert(PresentationLines(lines) == 1080);
        const auto output = Widescreen::ComputeOutput(1920, 1080, PresentationLines(lines));
        assert(output.width == 1920 && output.height == 1080);
        const auto ultra = Widescreen::ComputeOutput(3440, 1440, PresentationLines(lines));
        assert(ultra.width == 2580 && ultra.height == 1080);
    }
    assert(RenderScaleArgument(240) == "-resolutionscale 0.222222");
    assert(RenderScaleArgument(360) == "-resolutionscale 0.333333");
    assert(RenderScaleArgument(480) == "-resolutionscale 0.444444");
    assert(RenderScaleArgument(576) == "-resolutionscale 0.533333");
    assert(IsRenderResolution(1440) && IsRenderResolution(2160));
    assert(!IsRenderResolution(0) && !IsRenderResolution(900));
    assert(RenderScaleArgument(720) == "-resolutionscale 0.666667");
    assert(RenderScaleArgument(1080).empty());
    assert(RenderScaleArgument(1440) == "-resolutionscale 1.3333");
    assert(RenderScaleArgument(2160) == "-resolutionscale 2");
    assert(PresentationLines(720) == 1080);
    assert(PresentationLines(1080) == 1080 && PresentationLines(1440) == 1440);
    // Regression: 720p scene rendering must not shrink the framebuffer used by menu/HUD.
    const auto scene720 = Widescreen::ComputeOutput(1920, 1080, PresentationLines(720));
    assert(scene720.width == 1920 && scene720.height == 1080 && !scene720.wide);
    const auto scene720wide = Widescreen::ComputeOutput(3440, 1440, PresentationLines(720));
    assert(scene720wide.width == 2580 && scene720wide.height == 1080 && scene720wide.wide);
    assert(RenderScaleArgument(720) == "-resolutionscale 0.666667");
    const auto normal = Widescreen::ComputeOutput(1920, 1080, 720);
    assert(normal.width == 1280 && normal.height == 720 && !normal.wide);
    const auto narrow = Widescreen::ComputeOutput(1920, 1200, 720);
    assert(narrow.width == 1280 && narrow.height == 720 && !narrow.wide);
    const auto wide = Widescreen::ComputeOutput(3440, 1440, 720);
    assert(wide.width == 1720 && wide.height == 720 && wide.wide);
    std::array<std::uint8_t, 8> image{1,2,3,4,5,6,7,8};
    const auto original = image;
    const std::array<std::uint8_t, 2> first{1,2}, last{7,8}, wrong{0,0}, replacement{9,9};
    PatchPlan mismatch{image};
    assert(mismatch.Add(0, first, replacement));
    assert(!mismatch.Add(6, wrong, replacement));
    assert(!mismatch.Commit() && image == original);
    PatchPlan outside{image};
    assert(!outside.Add(8, last, replacement));
    assert(!outside.Commit() && image == original);
    PatchPlan changed{image};
    assert(changed.Add(0, first, replacement));
    assert(changed.Add(6, last, replacement));
    image[6] = 0;
    assert(!changed.Commit() && image[0] == 1);
    image = original;
    PatchPlan matching{image};
    assert(matching.Add(0, first, replacement));
    assert(matching.Add(6, last, replacement));
    assert(matching.Commit() && image[0] == 9 && image[6] == 9 && image[3] == 4);
    std::cout << "Game IDs, reference-profile detection and 720p scaling: OK\n";
}
