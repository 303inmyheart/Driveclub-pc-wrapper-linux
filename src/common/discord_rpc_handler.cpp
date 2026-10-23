// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <ctime>
#include "discord_rpc_handler.h"
#include "common/logging/log.h"
#include "common/thread.h"
#include "common/polyfill_thread.h"

namespace DiscordRPCHandler {

void RPC::init() {
    DiscordEventHandlers handlers{};
    handlers.ready = [](const DiscordUser*) { LOG_INFO(Config, "Discord RPC: client connected"); };
    handlers.errored = [](int code, const char* message) {
        LOG_WARNING(Config, "Discord RPC error {}: {}", code, message ? message : "unknown");
    };
    handlers.disconnected = [](int code, const char* message) {
        LOG_WARNING(Config, "Discord RPC disconnected {}: {}", code, message ? message : "unknown");
    };
    LOG_INFO(Config, "Discord RPC: enabled; searching for a running client (native/Flatpak IPC)");

    Discord_Initialize("1139939140494971051", &handlers, 1, nullptr);
    startTimestamp = time(nullptr);
    rpcEnabled = true;
    callback_thread = std::jthread([](std::stop_token stop) {
        Common::SetCurrentThreadName("dcpc:DiscordRPC");
        while (!stop.stop_requested()) {
            Discord_RunCallbacks();
            Common::StoppableTimedWait(stop, std::chrono::milliseconds(500));
        }
    });
}

void RPC::setStatusIdling() {
    DiscordRichPresence rpc{};
    rpc.largeImageKey = "https://cdn.jsdelivr.net/gh/shadps4-emu/shadPS4@main/.github/shadps4.png";
    rpc.largeImageText = "shadPS4 is a PS4 emulator";
    rpc.startTimestamp = startTimestamp;
    rpc.details = "Idle";

    status = RPCStatus::Idling;
    Discord_UpdatePresence(&rpc);
}

void RPC::setStatusPlaying(const std::string& game_name, const std::string& game_id) {
    DiscordRichPresence rpc{};

    rpc.details = "Playing";
    rpc.state = game_name.c_str();
    std::string largeImageUrl =
        "https://store.playstation.com/store/api/chihiro/00_09_000/titlecontainer/US/en/999/" +
        game_id + "_00/image";
    rpc.largeImageKey = largeImageUrl.c_str();
    rpc.largeImageText = game_name.c_str();
    rpc.startTimestamp = startTimestamp;

    status = RPCStatus::Playing;
    Discord_UpdatePresence(&rpc);
}

void RPC::shutdown() {
    if (rpcEnabled) {
        rpcEnabled = false;
        callback_thread.request_stop();
        if (callback_thread.joinable()) callback_thread.join();
        Discord_ClearPresence();
        Discord_Shutdown();
    }
}

bool RPC::getRPCEnabled() {
    return rpcEnabled;
}

} // namespace DiscordRPCHandler
