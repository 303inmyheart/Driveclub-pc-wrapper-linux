// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: front-end checks that run before the emulator core starts. Driveclub-PC.exe runs
// one game, so a wrong folder or missing firmware modules are reported up front in plain words
// instead of failing later inside the runtime.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include "driveclub/game_profile.h"

namespace DriveclubPC {

constexpr std::string_view TitleId = "CUSA00093";
constexpr std::string_view TargetAppVersion = "01.28";
constexpr std::string_view AppName = "Driveclub-PC";

// Accepts the base game folder, its update folder (<base>-UPDATE / <base>-patch) or an eboot.bin
// inside either, or a containing folder searched recursively. Returns the base eboot.bin;
// multiple matching installations require a more specific selection.
std::optional<std::filesystem::path> ResolveAndValidateGame(const std::filesystem::path& arg,
                                                            std::string& error);

// Firmware modules are the user's own (never shipped). Driveclub needs libSceLibcInternal.sprx.
bool ValidateSysModules(std::string& error);

// Describe unpacked DLCs for this title in either an addcont root or a direct DLC container.
std::optional<std::string> GameTitleId(const std::filesystem::path& folder);
std::string GameCompatibilityText(const std::filesystem::path& folder);
std::string DescribeAddonFolder(const std::filesystem::path& root,
                                std::string_view title_id = TitleId);

// Writes the Driveclub per-game settings once, when the user has none: the GPU readback settings
// recommended for the game (Precise/readbacks_mode=2, readback_offload=true), FSR 1 + RCAS, and
// borderless fullscreen at the primary display's resolution.
void EnsureDefaultGameConfig(std::string_view title_id = TitleId);

void ShowError(std::string_view message);

std::string UsageText();

} // namespace DriveclubPC
