// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <atomic>
#include <cstdlib>
#include "common/logging/log.h"
#include "driveclub/frame_profiler.h"
#include "driveclub/performance.h"

namespace DriveclubPC::Performance {
namespace {
int Env(const char* key, int fallback) {
    const char* value = std::getenv(key);
    return value ? std::atoi(value) : fallback;
}
struct State {
    int mode = Env("DCPC_PERF", 0); // opt-in: corrected race A/B did not establish an FPS gain
    bool reverse = Env("DCPC_PERF_AB_REVERSE", 0) != 0;
    std::array<bool, 5> features{
        Env("DCPC_PERF_TEMPLATES", 1) != 0, Env("DCPC_PERF_SAMPLER_HINTS", 1) != 0,
        Env("DCPC_PERF_READBACK_BATCH", 1) != 0, Env("DCPC_PERF_DESCRIPTOR_RANGES", 0) != 0,
        Env("DCPC_PERF_SUBMIT_HANDOFF", 0) != 0};
    std::atomic<bool> active{mode == 1};
    u64 start = 0;
    int last_phase = -1;
    State() {
        LOG_INFO(
            Render,
            "driveclub-pc: performance mode {}, templates={}, sampler hints={}, readback batch={}, "
            "descriptor ranges={}, submit handoff={}",
            mode, features[0], features[1], features[2], features[3], features[4]);
    }
};
State& GetState() {
    static State state;
    return state;
}
} // namespace

bool Enabled(Feature feature) {
    auto& state = GetState();
    return state.features[static_cast<size_t>(feature)] &&
           state.active.load(std::memory_order_relaxed);
}

void OnGuestFrame() {
    auto& state = GetState();
    int phase = state.mode == 1 ? 1 : 0;
    if (state.mode == 2) {
        const u64 now = Profiler::NowNs();
        if (!state.start)
            state.start = now;
        phase = int(((now - state.start) / 10000000000ULL) & 1) ^ int(state.reverse);
    }
    state.active.store(phase != 0, std::memory_order_relaxed);
    if (state.mode == 2 || state.mode == 1)
        Profiler::SetPhase(phase);
    if (state.last_phase != phase) {
        state.last_phase = phase;
        LOG_INFO(Render, "driveclub-pc: performance phase {}", phase);
    }
}
} // namespace DriveclubPC::Performance
