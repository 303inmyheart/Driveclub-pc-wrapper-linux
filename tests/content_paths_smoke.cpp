// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone filesystem regression tests; no game dumps or emulator dependencies needed.
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include "common/content_paths.h"

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("driveclub-content-paths-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(fs::create_directory(root));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{root};
    const auto make_game = [](const fs::path& folder, bool matching = true) {
        fs::create_directories(folder);
        std::ofstream(folder / "eboot.bin") << "not a game executable";
        if (matching) std::ofstream(folder / ".matching-title") << "fixture";
    };
    const auto matches_title = [](const fs::path& folder) {
        return fs::is_regular_file(folder / ".matching-title");
    };
    const auto container = root / "Drive Club";
    const auto base = container / "archive" / "games" / "CUSA00093";
    make_game(base);
    make_game(fs::path(base.string() + "-patch"));
    make_game(fs::path(base.string() + "-UPDATE"));
    make_game(fs::path(base.string() + "-mods") / "nested");
    make_game(container / "Other Game", false);
    make_game(container / "Other Game" / "payload");
    fs::create_directory_symlink(container, container / "loop");

    auto found = Common::FS::FindGameDirectories(container, matches_title);
    assert(!found.incomplete && found.matches == std::vector<fs::path>{base});
    found = Common::FS::FindGameDirectories(base, matches_title);
    assert(!found.incomplete && found.matches == std::vector<fs::path>{base});
    assert(Common::FS::FindGameDirectories(container / "missing", matches_title).matches.empty());
    assert(Common::FS::FindGameDirectories(container, matches_title, 1).incomplete);
    assert(Common::FS::FindGameDirectories(container, matches_title, 10000, 1).incomplete);

    const auto second = container / "another copy" / "CUSA00093";
    make_game(second);
    found = Common::FS::FindGameDirectories(container, matches_title);
    assert(!found.incomplete && found.matches.size() == 2);
    assert(std::find(found.matches.begin(), found.matches.end(), base) != found.matches.end());
    assert(std::find(found.matches.begin(), found.matches.end(), second) != found.matches.end());

    const auto addons = root / "My DLCs";
    fs::create_directories(addons / "CUSA00093" / "a DLC");
    assert(Common::FS::AddonContentPath(addons, "CUSA00093") == addons / "CUSA00093");
    assert(Common::FS::AddonContentPath(addons / "CUSA00093", "CUSA00093") == addons / "CUSA00093");
    const auto flat = root / "unpacked DLC collection";
    fs::create_directories(flat / "a DLC");
    assert(Common::FS::AddonContentPath(flat, "CUSA00093") == flat);
    fs::create_directories(addons / "CUSA00003" / "region DLC");
    assert(Common::FS::AddonContentPath(addons, "CUSA00003") == addons / "CUSA00003");
    assert(Common::FS::AddonContentPath(addons / "CUSA00003", "CUSA00003") == addons / "CUSA00003");
    std::cout << "PASS: recursive discovery, overlays, payload pruning, symlink loops, ambiguity, "
                 "search limits and DLC container layouts\n";
}
