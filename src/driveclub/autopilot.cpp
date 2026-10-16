// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "common/logging/log.h"
#include "common/singleton.h"
#include "common/thread.h"
#include "driveclub/autopilot.h"
#include "input/controller.h"
#include "video_core/renderdoc.h"

#ifdef DCPC_PGO_GENERATE
extern "C" int __llvm_profile_write_file(void);
#endif

namespace DriveclubPC::Autopilot {

namespace {

using Libraries::Pad::OrbisPadButtonDataOffset;
using Clock = std::chrono::steady_clock;

struct Step {
    double at_s;
    std::string action; ///< everything after "<seconds>:"
};

std::vector<std::string> Split(const std::string& text, char sep) {
    std::vector<std::string> parts;
    std::stringstream stream{text};
    std::string part;
    while (std::getline(stream, part, sep)) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

std::optional<OrbisPadButtonDataOffset> ParseButton(const std::string& name) {
    using B = OrbisPadButtonDataOffset;
    static const std::pair<const char*, B> table[] = {
        {"cross", B::Cross}, {"circle", B::Circle}, {"square", B::Square},
        {"triangle", B::Triangle}, {"up", B::Up}, {"down", B::Down},
        {"left", B::Left}, {"right", B::Right}, {"l1", B::L1},
        {"r1", B::R1}, {"l2", B::L2}, {"r2", B::R2},
        {"l3", B::L3}, {"r3", B::R3}, {"options", B::Options},
        {"touchpad", B::TouchPad},
    };
    for (const auto& [key, value] : table) {
        if (name == key) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<Input::Axis> ParseAxis(const std::string& name) {
    using A = Input::Axis;
    static const std::pair<const char*, A> table[] = {
        {"lx", A::LeftX}, {"ly", A::LeftY},       {"rx", A::RightX},
        {"ry", A::RightY}, {"l2", A::TriggerLeft}, {"r2", A::TriggerRight},
    };
    for (const auto& [key, value] : table) {
        if (name == key) {
            return value;
        }
    }
    return std::nullopt;
}

Input::GameController& Pad() {
    return *(*Common::Singleton<Input::GameControllers>::Instance())[0];
}

/// Runs one action; returns false (and logs) when it cannot be parsed.
bool Run(const std::string& action, std::vector<Step>& repeats, double now_s) {
    const auto parts = Split(action, ':');
    if (parts.empty()) {
        return false;
    }
    const std::string& verb = parts[0];
    if (verb == "shot" && parts.size() == 1) {
        VideoCore::RequestScreenshot(VideoCore::ScreenshotRequest::GameOnly);
        return true;
    }
    if (verb == "pgodump" && parts.size() == 1) {
        // Profile-guided optimisation: the instrumented build writes LLVM_PROFILE_FILE here,
        // since unattended runs end by terminating the process.
#ifdef DCPC_PGO_GENERATE
        LOG_INFO(Debug, "Autopilot: profile written ({})", __llvm_profile_write_file());
#else
        LOG_WARNING(Debug, "Autopilot: pgodump needs a -Pgo generate build");
#endif
        return true;
    }
    if ((verb == "press" || verb == "hold" || verb == "release") && parts.size() >= 2) {
        const auto button = ParseButton(parts[1]);
        if (!button) {
            return false;
        }
        if (verb == "release") {
            Pad().Button(*button, false);
        } else {
            Pad().Button(*button, true);
            if (verb == "press") {
                const int ms = parts.size() >= 3 ? std::atoi(parts[2].c_str()) : 150;
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                Pad().Button(*button, false);
            }
        }
        return true;
    }
    if (verb == "axis" && parts.size() == 3) {
        const auto axis = ParseAxis(parts[1]);
        if (!axis) {
            return false;
        }
        Pad().Axis(*axis, std::atoi(parts[2].c_str()), false);
        return true;
    }
    if (verb == "repeat" && parts.size() >= 3) {
        const double period = std::atof(parts[1].c_str());
        if (period <= 0) {
            return false;
        }
        std::string inner = parts[2];
        for (size_t i = 3; i < parts.size(); ++i) {
            inner += ":" + parts[i];
        }
        repeats.push_back({period, inner});
        // the first repetition runs one period from now
        repeats.back().at_s = period;
        (void)now_s;
        return true;
    }
    return false;
}

void ScheduleLoop(std::stop_token stop, std::vector<Step> steps, Clock::time_point start) {
    Common::SetCurrentThreadName("dcpc:Autopilot");
    struct Repeat {
        double period;
        double next;
        std::string action;
    };
    std::vector<Repeat> repeats;
    size_t next_step = 0;
    while (!stop.stop_requested()) {
        const double now_s = std::chrono::duration<double>(Clock::now() - start).count();
        while (next_step < steps.size() && steps[next_step].at_s <= now_s) {
            const auto& step = steps[next_step++];
            std::vector<Step> new_repeats;
            if (Run(step.action, new_repeats, now_s)) {
                LOG_INFO(Debug, "Autopilot {:.1f} s: {}", now_s, step.action);
            } else {
                LOG_ERROR(Debug, "Autopilot: cannot run step '{}'", step.action);
            }
            for (const auto& r : new_repeats) {
                repeats.push_back({r.at_s, now_s + r.at_s, r.action});
            }
        }
        for (auto& r : repeats) {
            if (r.next <= now_s) {
                std::vector<Step> ignored;
                if (Run(r.action, ignored, now_s)) {
                    LOG_INFO(Debug, "Autopilot {:.1f} s (repeat): {}", now_s, r.action);
                }
                r.next += r.period;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

std::jthread g_thread;

} // namespace

void Start() {
    const char* env = std::getenv("DCPC_AUTOPILOT");
    if (env == nullptr || env[0] == '\0') {
        return;
    }
    std::vector<Step> steps;
    for (const auto& item : Split(env, ';')) {
        const size_t colon = item.find(':');
        if (colon == std::string::npos) {
            LOG_ERROR(Debug, "Autopilot: step '{}' has no time", item);
            continue;
        }
        steps.push_back({std::atof(item.substr(0, colon).c_str()), item.substr(colon + 1)});
    }
    std::stable_sort(steps.begin(), steps.end(),
                     [](const Step& a, const Step& b) { return a.at_s < b.at_s; });
    LOG_INFO(Debug, "Autopilot: {} steps", steps.size());
    g_thread = std::jthread([steps = std::move(steps), start = Clock::now()](
                                std::stop_token stop) { ScheduleLoop(stop, steps, start); });
}

} // namespace DriveclubPC::Autopilot
