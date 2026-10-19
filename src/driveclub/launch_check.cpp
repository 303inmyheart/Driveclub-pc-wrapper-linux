// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <cstring>
#include <vector>

#include <SDL3/SDL_messagebox.h>
#include <fmt/format.h>

#include "common/logging/log.h"
#include "common/content_paths.h"
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
    // Discovery can encounter unrelated or incomplete dumps. Read only bounded SFO strings;
    // the core's general-purpose PSF loader assumes it was given a complete game file.
    std::ifstream input(folder / "sce_sys" / "param.sfo", std::ios::binary | std::ios::ate);
    if (!input) {
        return std::nullopt;
    }
    const auto size = input.tellg();
    if (size < static_cast<std::streamoff>(sizeof(PSFHeader)) || size > 1024 * 1024) {
        return std::nullopt;
    }
    std::vector<char> bytes(static_cast<size_t>(size));
    input.seekg(0);
    if (!input.read(bytes.data(), bytes.size())) return std::nullopt;
    PSFHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    const size_t count = header.index_table_entries;
    const size_t keys = header.key_table_offset, data = header.data_table_offset;
    if (header.magic != PSF_MAGIC ||
        (header.version != PSF_VERSION_1_1 && header.version != PSF_VERSION_1_0) ||
        count > (bytes.size() - sizeof(header)) / sizeof(PSFRawEntry) ||
        keys < sizeof(header) + count * sizeof(PSFRawEntry) || keys > data || data > bytes.size()) {
        return std::nullopt;
    }
    for (size_t i = 0; i < count; ++i) {
        PSFRawEntry entry{};
        std::memcpy(&entry, bytes.data() + sizeof(header) + i * sizeof(entry), sizeof(entry));
        const size_t offset = entry.key_offset;
        if (offset >= data - keys) return std::nullopt;
        const char* name = bytes.data() + keys + offset;
        const auto* end = static_cast<const char*>(std::memchr(name, 0, data - keys - offset));
        if (!end) return std::nullopt;
        if (std::string_view(name, end - name) != key) continue;
        const size_t value_offset = entry.data_offset, length = entry.param_len;
        if (entry.param_fmt.Raw() != static_cast<u16>(PSFEntryFmt::Text) ||
            value_offset > bytes.size() - data || length > bytes.size() - data - value_offset) {
            return std::nullopt;
        }
        const char* value = bytes.data() + data + value_offset;
        const auto* value_end = static_cast<const char*>(std::memchr(value, 0, length));
        return value_end ? std::optional<std::string>(std::string(value, value_end)) : std::nullopt;
    }
    return std::nullopt;
}

} // namespace

std::optional<std::filesystem::path> ResolveAndValidateGame(const std::filesystem::path& arg,
                                                            std::string& error) {
    std::error_code ec;
    error.clear();
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
    const auto is_base = [](const std::filesystem::path& candidate) {
        std::error_code file_error;
        if (!std::filesystem::is_regular_file(candidate / "eboot.bin", file_error) ||
            ReadSfoString(candidate, "TITLE_ID") != TitleId) {
            return false;
        }
        const auto category = ReadSfoString(candidate, "CATEGORY");
        return !category || (*category != "gp" && !category->starts_with("ac"));
    };
    if (!is_base(folder)) {
        const auto search = Common::FS::FindGameDirectories(folder, is_base);
        if (search.incomplete) {
            error = "This folder contains too many nested folders. Select a folder closer to Driveclub.";
            return std::nullopt;
        }
        if (search.matches.size() > 1) {
            error = "Several Driveclub installations were found. Select the one you want:";
            for (const auto& match : search.matches) error += "\n" + match.string();
            return std::nullopt;
        }
        if (search.matches.empty()) {
            error = fmt::format("No Driveclub base game ({}) found in this folder or its subfolders:\n{}",
                                TitleId, folder.string());
            return std::nullopt;
        }
        folder = search.matches.front();
    }
    folder = std::filesystem::weakly_canonical(folder, ec);
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

std::string DescribeAddonFolder(const std::filesystem::path& root) {
    std::error_code ec;
    const auto folder = Common::FS::AddonContentPath(root, TitleId);
    if (!std::filesystem::is_directory(folder, ec)) {
        return "No DLC folder found (optional). Choose a folder with unpacked DLCs.";
    }
    size_t count = 0;
    std::filesystem::directory_iterator it(folder,
        std::filesystem::directory_options::skip_permission_denied, ec), end;
    while (it != end && !ec) {
        const auto category = ReadSfoString(it->path(), "CATEGORY");
        const auto content_id = ReadSfoString(it->path(), "CONTENT_ID");
        if (category && category->starts_with("ac") && content_id &&
            content_id->find(fmt::format("-{}_", TitleId)) != std::string::npos) {
            ++count;
        }
        it.increment(ec);
    }
    return fmt::format("{} matching DLC folder(s) found in {}", count, folder.string());
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
        "with {}. Use Import firmware in the launcher, or set General.sys_modules_dir in config.json.",
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
#ifdef _WIN32
    constexpr std::string_view executable = "Driveclub-PC.exe";
#else
    constexpr std::string_view executable = "Driveclub-PC";
#endif
    return fmt::format("{} runs the PS4 version of Driveclub ({}, update {}).\n\n"
                       "Usage: {} <Driveclub base game folder>\n\n"
                       "The update must sit next to it as <folder>-UPDATE or <folder>-patch.\n"
                       "Settings and saves are kept in:\n{}",
                       AppName, TitleId, TargetAppVersion, executable,
                       Common::FS::GetUserPath(Common::FS::PathType::UserDir).string());
}

} // namespace DriveclubPC
