// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>

#include <SDL3/SDL_messagebox.h>
#include <fmt/format.h>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "core/file_format/psf.h"
#include "core/file_sys/fs.h"
#include "driveclub/launch_check.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace DriveclubPC {

namespace {

std::optional<std::string> ReadSfoString(const std::filesystem::path& folder, std::string_view key) {
    PSF psf;
    if (!psf.Open(folder / "sce_sys" / "param.sfo")) {
        return std::nullopt;
    }
    if (const auto value = psf.GetString(key)) {
        return std::string(*value);
    }
    return std::nullopt;
}

} // namespace

std::optional<std::filesystem::path> ResolveAndValidateGame(const std::filesystem::path& arg,
                                                            std::string& error) {
    std::error_code ec;
    std::filesystem::path folder = arg;
    if (std::filesystem::is_regular_file(folder, ec)) {
        folder = folder.parent_path();
    }
    if (!std::filesystem::is_directory(folder, ec)) {
        error = fmt::format("The game folder does not exist:\n{}", arg.string());
        return std::nullopt;
    }
    if (const auto base = Core::FileSys::BaseGameFromOverlay(folder)) {
        folder = *base;
    }
    const auto eboot = folder / "eboot.bin";
    if (!std::filesystem::is_regular_file(eboot, ec)) {
        error = fmt::format("No eboot.bin in\n{}\n\nSelect the Driveclub base game folder ({}).",
                            folder.string(), TitleId);
        return std::nullopt;
    }
    const auto title_id = ReadSfoString(folder, "TITLE_ID");
    if (!title_id) {
        error = fmt::format("sce_sys/param.sfo is missing or unreadable in\n{}", folder.string());
        return std::nullopt;
    }
    if (*title_id != TitleId) {
        error = fmt::format("This folder holds {}, not Driveclub ({}):\n{}", *title_id, TitleId,
                            folder.string());
        return std::nullopt;
    }

    std::optional<std::filesystem::path> update;
    for (const auto suffix : {"-UPDATE", "-patch"}) {
        const auto candidate = Core::FileSys::OverlayPath(folder, suffix);
        if (std::filesystem::is_directory(candidate, ec)) {
            update = candidate;
            break;
        }
    }
    const auto base_version = ReadSfoString(folder, "APP_VER").value_or("?");
    const auto update_version =
        update ? ReadSfoString(*update, "APP_VER").value_or("?") : std::string("none");
    LOG_INFO(Loader, "{}: {} base {} (APP_VER {}), update {} (APP_VER {})", AppName, TitleId,
             folder.string(), base_version, update ? update->string() : "not found",
             update_version);
    if (update_version != TargetAppVersion) {
        LOG_WARNING(Loader,
                    "{}: tested with update {}; this installation runs {}. Expect differences.",
                    AppName, TargetAppVersion, update ? update_version : base_version);
    }
    return eboot;
}

bool ValidateSysModules(std::string& error) {
    const auto dir = EmulatorSettings.GetSysModulesDir();
    std::error_code ec;
    if (std::filesystem::is_regular_file(dir / "libSceLibcInternal.sprx", ec)) {
        LOG_INFO(Loader, "{}: firmware modules from {}", AppName, dir.string());
        return true;
    }
    error = fmt::format(
        "PS4 firmware modules were not found in\n{}\n\nDriveclub needs libSceLibcInternal.sprx "
        "and the other system modules from your own PS4 firmware dump. They are not included "
        "with {}. Copy them into that folder, or set General.sys_modules_dir in user\\config.json.",
        dir.string(), AppName);
    return false;
}

void EnsureDefaultGameConfig() {
    const auto path = Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
                      fmt::format("{}.json", TitleId);
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return;
    }
    // Borderless fullscreen at the desktop resolution of the primary display; the game renders at
    // its native 1920x1080 and the presenter scales it with FSR 1 + RCAS.
    int width = 1920;
    int height = 1080;
#ifdef _WIN32
    // Physical pixels of the primary display (GetSystemMetrics would be DPI-scaled here).
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmPelsWidth > 0 &&
        mode.dmPelsHeight > 0) {
        width = static_cast<int>(mode.dmPelsWidth);
        height = static_cast<int>(mode.dmPelsHeight);
    }
#endif
    std::ofstream out(path);
    out << fmt::format("{{\n  \"GPU\": {{\n"
                       "    \"readbacks_mode\": 2,\n"
                       "    \"readback_offload\": true,\n"
                       "    \"fsr_enabled\": true,\n"
                       "    \"rcas_enabled\": true,\n"
                       "    \"full_screen\": true,\n"
                       "    \"full_screen_mode\": \"Fullscreen (Borderless)\",\n"
                       "    \"window_width\": {},\n"
                       "    \"window_height\": {}\n"
                       "  }}\n}}\n",
                       width, height);
    LOG_INFO(Loader, "{}: wrote default Driveclub settings to {} (borderless {}x{}, FSR 1)", AppName,
             path.string(), width, height);
}

void ShowError(std::string_view message) {
    LOG_CRITICAL(Loader, "{}", message);
    Common::Log::Flush();
    fmt::print(stderr, "{}: {}\n", AppName, message);
    std::fflush(stderr);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, std::string(AppName).c_str(),
                             std::string(message).c_str(), nullptr);
}

std::string UsageText() {
    return fmt::format("{} runs the PS4 version of Driveclub ({}, update {}).\n\n"
                       "Usage: {}.exe <Driveclub base game folder>\n\n"
                       "The update must sit next to it as <folder>-UPDATE or <folder>-patch.\n"
                       "Settings and saves are kept in the 'user' folder next to the program.",
                       AppName, TitleId, TargetAppVersion, AppName);
}

} // namespace DriveclubPC
