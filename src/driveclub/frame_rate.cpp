// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>

#include "common/logging/log.h"
#include "core/emulator_settings.h"
#include "driveclub/frame_rate.h"

namespace DriveclubPC::FrameRate {

namespace {
constexpr u32 kFineVblankHz = 480;
s32 g_min_flip_rate = 0;
u64 g_min_flip_interval_ns = 0;

// Both the global and the per-game value: a game config must not override the launcher.
void SetVblank(u32 hz) {
    EmulatorSettings.SetVblankFrequency(hz);
    EmulatorSettings.SetVblankFrequency(hz, true);
}
} // namespace

void Apply(u32 configured) {
    u32 target = configured;
    if (const char* env = std::getenv("DCPC_TARGET_FPS")) {
        target = static_cast<u32>(std::atoi(env));
    }
    if (target == 0) {
        return;
    }
    if (target != 30 && target != 60 && target != 120) {
        LOG_ERROR(Config, "Target frame rate {} is not supported (30, 60 or 120); keeping {} Hz",
                  target, EmulatorSettings.GetVblankFrequency());
        return;
    }
    const char* classic = std::getenv("DCPC_CLASSIC_VBLANK");
    if (classic != nullptr && std::atoi(classic) != 0) {
        // PS4-style pacing: 60 or 120 Hz vblanks, a late frame waits for the next one.
        SetVblank(target == 120 ? 120 : 60);
        g_min_flip_rate = target == 30 ? 1 : 0;
        g_min_flip_interval_ns = 0;
    } else {
        // A fine vblank grid with a flip-interval limit: a frame that is ready after 10 ms is
        // shown on the next ~2 ms vblank instead of waiting for a whole 8.3 or 16.7 ms one, and
        // a late frame at 60 FPS costs a few ms instead of a whole frame. The game times
        // gameplay with the real clock (race timer checked against wall time at 60/120 FPS).
        SetVblank(kFineVblankHz);
        g_min_flip_rate = 0;
        g_min_flip_interval_ns = 1'000'000'000ull / target;
    }
    LOG_INFO(Config, "Target frame rate {}: vblank {} Hz, minimum flip rate {}, flip interval {} us",
             target, EmulatorSettings.GetVblankFrequency(), g_min_flip_rate,
             g_min_flip_interval_ns / 1000);
}

s32 MinFlipRate() {
    return g_min_flip_rate;
}

u64 MinFlipIntervalNs() {
    return g_min_flip_interval_ns;
}

} // namespace DriveclubPC::FrameRate
