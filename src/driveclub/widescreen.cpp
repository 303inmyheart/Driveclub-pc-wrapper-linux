// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>

#include <fmt/format.h>

#include "common/logging/log.h"
#include "driveclub/widescreen.h"

namespace DriveclubPC::Widescreen {

namespace {

// Offsets in the v1.28 eboot image (patch XML address minus 0x400000).
constexpr u64 kAspectOffset = 0x729978; // float camera aspect (16/9)
constexpr u64 kFovOffset = 0x12efac4;   // float FOV factor (0.5)
// Scene-size table read only by eboot+0x4dc1b0 (render target set creation), indexed by mode.
constexpr u64 kSceneWidths = 0x12d0540;
constexpr u64 kSceneHeights = 0x12d0550;
constexpr std::array<u32, 4> kTableWidths = {1280, 1920, 2560, 4096};
constexpr std::array<u32, 4> kTableHeights = {720, 1080, 1600, 2304};
// -resolutionscale height in that function: lea ecx, [rcx + rcx * 8]; shr ecx, 4 (width * 9 / 16);
// mov [rbp - 0xac], ecx. Diverted to a stub that stores round(table height * scale) instead.
constexpr u64 kHeightSite = 0x4dc37a;
constexpr u64 kHeightResume = 0x4dc386;
constexpr std::array<u8, 12> kHeightOriginal = {0x8d, 0x0c, 0xc9, 0xc1, 0xe9, 0x04,
                                                0x89, 0x8d, 0x54, 0xff, 0xff, 0xff};
// Zero tail of the code segment; render_res.cpp uses the first 96 bytes for its heap stubs.
constexpr u64 kCave = 0x1552080;
constexpr u32 kCaveBytes = 64;

u32 Read32(VAddr addr) {
    u32 value;
    std::memcpy(&value, reinterpret_cast<const void*>(addr), sizeof(value));
    return value;
}

void Write32(VAddr addr, u32 value) {
    std::memcpy(reinterpret_cast<void*>(addr), &value, sizeof(value));
}

bool SceneSitesMatch(VAddr base) {
    for (size_t i = 0; i < kTableWidths.size(); ++i) {
        if (Read32(base + kSceneWidths + i * 4) != kTableWidths[i] ||
            Read32(base + kSceneHeights + i * 4) != kTableHeights[i]) {
            return false;
        }
    }
    if (std::memcmp(reinterpret_cast<const void*>(base + kHeightSite), kHeightOriginal.data(),
                    kHeightOriginal.size()) != 0) {
        return false;
    }
    const u8* cave = reinterpret_cast<const u8*>(base + kCave);
    for (u32 i = 0; i < kCaveBytes; ++i) {
        if (cave[i] != 0) {
            return false;
        }
    }
    return true;
}

void PatchScene(VAddr base, u32 width, u32 height) {
    for (size_t i = 0; i < kTableWidths.size(); ++i) {
        Write32(base + kSceneWidths + i * 4, SceneWidth(kTableWidths[i], width, height));
    }
    // Stub, entered with eax = mode index, xmm0 free (reloaded after the resume point):
    //   lea rcx, [rip + heights]; mov ecx, [rcx + rax * 4]; vcvtsi2ss xmm0, xmm0, ecx;
    //   vmulss xmm0, xmm0, [r14 + 0x10] (scale); vcvtss2si ecx, xmm0; mov [rbp - 0xac], ecx;
    //   jmp resume
    u8* c = reinterpret_cast<u8*>(base + kCave);
    u32 n = 0;
    const auto emit = [&](std::initializer_list<u8> bytes) {
        for (u8 v : bytes) {
            c[n++] = v;
        }
    };
    const auto emit_rel = [&](u64 target) {
        const s32 v = static_cast<s32>(static_cast<s64>(target) - static_cast<s64>(kCave + n + 4));
        std::memcpy(c + n, &v, 4);
        n += 4;
    };
    emit({0x48, 0x8d, 0x0d});
    emit_rel(kSceneHeights);
    emit({0x8b, 0x0c, 0x81});
    emit({0xc5, 0xfa, 0x2a, 0xc1});
    emit({0xc4, 0xc1, 0x7a, 0x59, 0x46, 0x10});
    emit({0xc5, 0xfa, 0x2d, 0xc8});
    emit({0x89, 0x8d, 0x54, 0xff, 0xff, 0xff});
    emit({0xe9});
    emit_rel(kHeightResume);
    u8* p = reinterpret_cast<u8*>(base + kHeightSite);
    p[0] = 0xe9;
    const s32 to_stub = static_cast<s32>(kCave - (kHeightSite + 5));
    std::memcpy(p + 1, &to_stub, 4);
    std::memset(p + 5, 0x90, kHeightOriginal.size() - 5);
}

} // namespace

void Apply(VAddr base, u64 size) {
    const char* env = std::getenv("DCPC_WIDESCREEN");
    u32 w = 0, h = 0;
    if (!env || std::sscanf(env, "%ux%u", &w, &h) != 2) {
        return;
    }
    if (h < 360 || h > 4320 || w > kMaxWidth || !IsWide(static_cast<s32>(w), static_cast<s32>(h))) {
        LOG_WARNING(Loader, "driveclub-pc: widescreen {}x{} is not a valid wide size, ignored", w,
                    h);
        return;
    }
    if (kCave + kCaveBytes > size || kFovOffset + 4 > size) {
        return;
    }
    if (Read32(base + kAspectOffset) != std::bit_cast<u32>(16.0f / 9.0f) ||
        Read32(base + kFovOffset) != std::bit_cast<u32>(0.5f)) {
        LOG_WARNING(Loader, "driveclub-pc: widescreen aspect/FOV values do not match, skipped");
        return;
    }
    const char* scene_env = std::getenv("DCPC_WIDESCREEN_SCENE");
    const bool scene = !(scene_env && scene_env[0] == '0');
    if (scene && !SceneSitesMatch(base)) {
        LOG_WARNING(Loader, "driveclub-pc: widescreen scene-size sites do not match, skipped");
        return;
    }
    Write32(base + kAspectOffset, std::bit_cast<u32>(Aspect(w, h)));
    Write32(base + kFovOffset, std::bit_cast<u32>(Fov(w, h)));
    if (scene) {
        PatchScene(base, w, h);
    }
    LOG_INFO(Loader, "driveclub-pc: widescreen {}x{}: aspect {:.4f}, FOV factor {:.5f}, scene {}",
             w, h, Aspect(w, h), Fov(w, h),
             scene ? fmt::format("table width {} at 1080 lines", SceneWidth(1920, w, h))
                   : std::string("16:9 (stretched)"));
}

} // namespace DriveclubPC::Widescreen
