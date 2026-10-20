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
    // driveclub-pc: opt-in alternative from kalaposfos' v1.28 fixed-tickrate patch.
    // Validate the entire option name and all deltatime sites before changing any bytes.
    const char* fixed = std::getenv("DCPC_FIXED_TICKRATE");
    if (fixed && std::strcmp(fixed, "1") == 0) {
        constexpr u64 option_addr = 0x1362801;
        constexpr char original[] = "fixedFramesPerSecond";
        constexpr char patched[] = "gixedFramesPerSecond";
        if (option_addr + sizeof(original) > size) {
            LOG_ERROR(Loader, "driveclub-pc: fixed tickrate skipped, image too small");
            return;
        }
        auto* option = reinterpret_cast<void*>(base + option_addr);
        if (std::memcmp(option, original, sizeof(original)) != 0 &&
            std::memcmp(option, patched, sizeof(patched)) != 0) {
            LOG_ERROR(Loader, "driveclub-pc: fixed tickrate skipped, option name does not match");
            return;
        }
        for (const auto& s : sites) {
            if (s.addr + s.original.size() > size) {
                LOG_ERROR(Loader, "driveclub-pc: fixed tickrate skipped, image too small");
                return;
            }
            const auto* p = reinterpret_cast<const void*>(base + s.addr);
            if (std::memcmp(p, s.original.data(), s.original.size()) != 0 &&
                std::memcmp(p, s.patched.data(), s.patched.size()) != 0) {
                LOG_ERROR(Loader, "driveclub-pc: fixed tickrate skipped, {} bytes do not match",
                          s.name);
                return;
            }
        }
        for (const auto& s : sites) {
            std::memcpy(reinterpret_cast<void*>(base + s.addr), s.original.data(),
                        s.original.size());
        }
        std::memcpy(option, patched, sizeof(patched));
        LOG_INFO(Loader,
                 "driveclub-pc: experimental fixed tickrate applied; deltatime sites restored; "
                 "validate at target 60 before using other rates");
        return;
    }
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
