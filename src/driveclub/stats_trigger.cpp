// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>

#include "driveclub/stats_trigger.h"

namespace DriveclubPC::StatsTrigger {

namespace {
const std::string& Path() {
    static const std::string path = [] {
        const char* env = std::getenv("DCPC_STATS_TRIGGER");
        return std::string{env ? env : ""};
    }();
    return path;
}
} // namespace

bool Configured() {
    return !Path().empty();
}

State Poll() {
    static State state = State::Waiting;
    static auto last_check = std::chrono::steady_clock::time_point{};
    if (!Configured() || state == State::Stopped) {
        return state;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_check < std::chrono::milliseconds(100)) {
        return state;
    }
    last_check = now;
    std::ifstream file(Path());
    std::string word;
    if (file >> word) {
        if (word == "start" && state == State::Waiting) {
            state = State::Running;
        } else if (word == "stop") {
            state = State::Stopped;
        }
    }
    return state;
}

} // namespace DriveclubPC::StatsTrigger
