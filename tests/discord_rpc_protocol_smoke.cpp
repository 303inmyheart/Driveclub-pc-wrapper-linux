// SPDX-License-Identifier: GPL-2.0-or-later
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <discord_rpc.h>
#include "driveclub/discord_assets.h"
static std::atomic<bool> ready{false};
int main() {
    DiscordEventHandlers handlers{};
    handlers.ready = [](const DiscordUser*) { ready.store(true); };
    Discord_Initialize("1139939140494971051", &handlers, 0, nullptr);
    DiscordRichPresence presence{};
    presence.state = "Driveclub IPC test";
    presence.details = "Playing";
    presence.largeImageKey = DriveclubPC::DiscordAssets::CoverUrl;
    Discord_UpdatePresence(&presence);
    for (int i = 0; i < 250; ++i) {
        Discord_RunCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(ready.load());
    Discord_Shutdown();
}
