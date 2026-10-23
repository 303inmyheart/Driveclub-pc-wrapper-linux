// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <deque>
#include <filesystem>
#include <string_view>
#include <set>
#include <vector>

namespace Common::FS {

inline bool IsGameOverlay(const std::filesystem::path& folder) {
    const auto name = folder.filename().string();
    return name.ends_with("-patch") || name.ends_with("-UPDATE") || name.ends_with("-mods");
}

struct GameDirectorySearch {
    std::vector<std::filesystem::path> matches;
    bool incomplete = false;
};

// Search containers without descending into game payloads, overlays or previously visited directory targets.
// Limits keep an accidentally selected filesystem root from blocking the launcher indefinitely.
template <typename IsBaseGame>
GameDirectorySearch FindGameDirectories(const std::filesystem::path& root, IsBaseGame is_base_game,
                                        size_t max_directories = 10000, unsigned max_depth = 16) {
    namespace fs = std::filesystem;
    GameDirectorySearch result;
    std::deque<std::pair<fs::path, unsigned>> pending{{root, 0}};
    size_t visited = 0;
    std::set<fs::path> targets;
    while (!pending.empty()) {
        if (++visited > max_directories) {
            result.incomplete = true;
            break;
        }
        auto [folder, depth] = std::move(pending.front());
        pending.pop_front();
        std::error_code target_error;
        const auto target = fs::canonical(folder, target_error);
        if (target_error || !targets.insert(target).second) continue;
        if (IsGameOverlay(folder)) {
            continue;
        }
        if (is_base_game(folder)) {
            result.matches.push_back(folder);
            continue;
        }
        std::error_code ec;
        if (fs::is_regular_file(folder / "eboot.bin", ec)) {
            continue;
        }
        fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
        while (it != end && !ec) {
            const auto entry = *it;
            if (entry.is_directory(ec) && !IsGameOverlay(entry.path())) {
                if (depth == max_depth) {
                    result.incomplete = true;
                } else {
                    pending.emplace_back(entry.path(), depth + 1);
                }
            }
            it.increment(ec);
        }
    }
    std::sort(result.matches.begin(), result.matches.end());
    return result;
}

// Accept both addcont/<title>/<DLC> and a directly selected folder containing DLCs.
inline std::filesystem::path AddonContentPath(const std::filesystem::path& root,
                                             std::string_view title_id) {
    std::error_code ec;
    const auto title_folder = root / title_id;
    return std::filesystem::is_directory(title_folder, ec) ? title_folder : root;
}

} // namespace Common::FS
