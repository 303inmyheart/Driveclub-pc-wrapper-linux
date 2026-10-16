// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/thread.h"
#include "driveclub/diag_tables.h"
#include "driveclub/frame_profiler.h"
#include "driveclub/stats_trigger.h"

namespace DriveclubPC::DiagTables {

namespace {

std::atomic<bool> g_active{false};
bool g_done = false;
u64 g_frames = 0;

struct Stat {
    u64 count = 0;
    u64 ns = 0;
    u64 bytes = 0;
};

std::mutex g_mutex;
// (write, cpu_data, rip, page, thread) -> stat
std::map<std::tuple<bool, bool, u64, u64, std::string>, Stat> g_faults;
// (window, thread, packet) -> stat
std::map<std::tuple<u64, std::string, u32>, Stat> g_downloads;
// (site, address, width, height, format, tile) -> creates (count) / bytes = guest size
using ImageKey = std::tuple<u32, u64, u32, u32, u32, u32>;
std::map<ImageKey, Stat> g_images;
// (dst, src, bytes, reason, packet) -> count
std::map<std::tuple<u64, u64, u32, u32, u32>, Stat> g_copies;

template <typename Map, typename Row>
std::vector<std::pair<typename Map::key_type, Stat>> Sorted(const Map& map, Row) {
    std::vector<std::pair<typename Map::key_type, Stat>> rows(map.begin(), map.end());
    std::ranges::sort(rows, [](const auto& a, const auto& b) {
        return a.second.count != b.second.count ? a.second.count > b.second.count
                                                : a.second.ns > b.second.ns;
    });
    return rows;
}

std::FILE* Open(const char* name) {
    const auto path = Common::FS::GetUserPath(Common::FS::PathType::LogDir) / name;
    return std::fopen(path.string().c_str(), "w");
}

void Write() {
    std::scoped_lock lk{g_mutex};
    const double frames = std::max<double>(1.0, static_cast<double>(g_frames));
    if (std::FILE* f = Open("readback_faults.csv")) {
        std::fprintf(f, "# %llu guest frames; read-cpu = the bytes read were CPU data (false sharing)\n"
                        "kind,rip,page,thread,count,per_frame,total_ms,us_each\n",
                     static_cast<unsigned long long>(g_frames));
        for (const auto& [key, s] : Sorted(g_faults, 0)) {
            const auto& [write, cpu_data, rip, page, thread] = key;
            std::fprintf(f, "%s,%#llx,%#llx,\"%s\",%llu,%.3f,%.3f,%.1f\n",
                         write ? "write" : (cpu_data ? "read-cpu" : "read"),
                         static_cast<unsigned long long>(rip), static_cast<unsigned long long>(page),
                         thread.c_str(), static_cast<unsigned long long>(s.count),
                         s.count / frames, s.ns / 1e6, s.ns / 1e3 / s.count);
        }
        std::fclose(f);
    }
    if (std::FILE* f = Open("readback_downloads.csv")) {
        std::fprintf(f, "# %llu guest frames; packet = (engine << 8) | PM4 opcode, engine 1 CE, 2 GFX, 3 ASC\n"
                        "window,thread,packet,count,per_frame,bytes_each,total_ms,us_each\n",
                     static_cast<unsigned long long>(g_frames));
        for (const auto& [key, s] : Sorted(g_downloads, 0)) {
            const auto& [window, thread, packet] = key;
            std::fprintf(f, "%#llx,\"%s\",%#x,%llu,%.3f,%.0f,%.3f,%.1f\n",
                         static_cast<unsigned long long>(window), thread.c_str(), packet,
                         static_cast<unsigned long long>(s.count), s.count / frames,
                         static_cast<double>(s.bytes) / s.count, s.ns / 1e6,
                         s.ns / 1e3 / s.count);
        }
        std::fclose(f);
    }
    if (std::FILE* f = Open("image_churn.csv")) {
        std::fprintf(f, "# %llu guest frames; site 0 overlap recreate, 1 expand, 2 new, 3 stencil, "
                        "255 delete\nsite,address,width,height,format,tile_mode,guest_size,count,"
                        "per_frame\n",
                     static_cast<unsigned long long>(g_frames));
        for (const auto& [key, s] : Sorted(g_images, 0)) {
            const auto& [site, address, width, height, format, tile] = key;
            std::fprintf(f, "%u,%#llx,%u,%u,%u,%u,%llu,%llu,%.3f\n", site,
                         static_cast<unsigned long long>(address), width, height, format, tile,
                         static_cast<unsigned long long>(s.bytes / std::max<u64>(1, s.count)),
                         static_cast<unsigned long long>(s.count), s.count / frames);
        }
        std::fclose(f);
    }
    if (std::FILE* f = Open("dma_copies.csv")) {
        std::fprintf(f, "# %llu guest frames; reason bits 1 dst GPU modified, 2 src GPU modified, "
                        "4 src in an image\ndst,src,bytes,reason,packet,count,per_frame\n",
                     static_cast<unsigned long long>(g_frames));
        for (const auto& [key, s] : Sorted(g_copies, 0)) {
            const auto& [dst, src, bytes, reason, packet] = key;
            std::fprintf(f, "%#llx,%#llx,%u,%u,%#x,%llu,%.3f\n", static_cast<unsigned long long>(dst),
                         static_cast<unsigned long long>(src), bytes, reason, packet,
                         static_cast<unsigned long long>(s.count), s.count / frames);
        }
        std::fclose(f);
    }
    LOG_INFO(Debug, "Diagnostic tables written for {} guest frames: {} fault sites, {} download "
                    "windows, {} image kinds",
             g_frames, g_faults.size(), g_downloads.size(), g_images.size());
}

} // namespace

bool Active() {
    return g_active.load(std::memory_order_relaxed);
}

void NoteFault(bool write, bool cpu_data, u64 rip, u64 address, u64 ns) {
    if (!Active() || (write && ns < 50'000)) {
        return;
    }
    std::string thread = Common::GetCurrentThreadName();
    std::scoped_lock lk{g_mutex};
    auto& s = g_faults[{write, cpu_data, rip, address & ~u64{0xFFF}, std::move(thread)}];
    ++s.count;
    s.ns += ns;
}

void NoteDownload(u64 window, u64 bytes, u64 ns) {
    if (!Active()) {
        return;
    }
    std::string thread = Common::GetCurrentThreadName();
    std::scoped_lock lk{g_mutex};
    auto& s = g_downloads[{window, std::move(thread), t_cp_packet}];
    ++s.count;
    s.ns += ns;
    s.bytes += bytes;
}

void NoteImage(u32 site, u64 guest_address, u32 width, u32 height, u32 format, u32 tile_mode,
               u64 guest_size) {
    if (!Active()) {
        return;
    }
    std::scoped_lock lk{g_mutex};
    auto& s = g_images[{site, guest_address, width, height, format, tile_mode}];
    ++s.count;
    s.bytes += guest_size;
}

void NoteGpuCopy(u64 dst, u64 src, u32 bytes, u32 reason) {
    if (!Active()) {
        return;
    }
    std::scoped_lock lk{g_mutex};
    ++g_copies[{dst, src, bytes, reason, t_cp_packet}].count;
}

void OnFrame() {
    if (g_done || !Profiler::Enabled() || !StatsTrigger::Configured()) {
        return;
    }
    const auto state = StatsTrigger::Poll();
    if (state == StatsTrigger::State::Running) {
        g_active.store(true, std::memory_order_relaxed);
        ++g_frames;
    } else if (state == StatsTrigger::State::Stopped) {
        g_active.store(false, std::memory_order_relaxed);
        g_done = true;
        Write();
    }
}

} // namespace DriveclubPC::DiagTables
