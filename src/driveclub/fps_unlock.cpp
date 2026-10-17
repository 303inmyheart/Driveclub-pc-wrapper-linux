// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "common/logging/log.h"
#include "driveclub/fps_unlock.h"

namespace DriveclubPC::FpsUnlock {

namespace {

struct Site {
    const char* name;
    u64 addr;
    std::vector<u8> original;
    std::vector<u8> patched;
};

} // namespace

void Apply(VAddr base, u64 size) {
    const char* env = std::getenv("DCPC_FPS_UNLOCK");
    if (env && std::strcmp(env, "0") == 0) {
        LOG_INFO(Loader, "driveclub-pc: fps unlock disabled (DCPC_FPS_UNLOCK=0)");
        return;
    }
    const Site sites[] = {
        // Elapsed-time helper: always use real elapsed time instead of the fixed 30 FPS step.
        {"timer", 0x66306e, {0x75, 0x07}, {0x90, 0x90}},
        // fixedFramesPerSecond option check: setne al -> mov al, 1.
        {"fixed-fps", 0xd84725, {0x0f, 0x95, 0xc0}, {0x48, 0xb0, 0x01}},
        // Flip interval: return 1 (60 Hz) instead of 2 below 60 FPS.
        {"interval", 0xd84801, {0x02}, {0x01}},
    };
    for (const auto& s : sites) {
        if (s.addr + s.original.size() > size) {
            return;
        }
    }
    int applied = 0, already = 0;
    for (const auto& s : sites) {
        u8* p = reinterpret_cast<u8*>(base + s.addr);
        if (std::memcmp(p, s.original.data(), s.original.size()) == 0) {
            ++applied;
        } else if (std::memcmp(p, s.patched.data(), s.patched.size()) == 0) {
            ++already;
        } else {
            LOG_INFO(Loader, "driveclub-pc: fps unlock skipped, bytes at {} (+{:#x}) do not match",
                     s.name, s.addr);
            return;
        }
    }
    if (already == 3) {
        LOG_INFO(Loader, "driveclub-pc: fps unlock already present in eboot");
        return;
    }
    for (const auto& s : sites) {
        std::memcpy(reinterpret_cast<u8*>(base + s.addr), s.patched.data(), s.patched.size());
    }
    LOG_INFO(Loader, "driveclub-pc: fps unlock applied ({} sites patched)", applied);
}

} // namespace DriveclubPC::FpsUnlock
