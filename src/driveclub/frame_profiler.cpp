// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/rdtsc.h"
#include "driveclub/frame_profiler.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace DriveclubPC::Profiler {

namespace {

// Written by several threads: locked RMW.
struct SharedCounters {
    std::atomic<u64> cp_busy_ns{};
    std::atomic<u64> cp_idle_ns{};
    std::atomic<u64> sched_wait_ticks{};
    std::atomic<u64> present_wait_ns{};
    std::atomic<u64> pipelines{};
    std::atomic<u64> pipeline_ns{};
};

// Written only by the command processor thread: monotonically growing totals updated with plain
// load+store (no lock prefix); the frame writer reads them and keeps the previous snapshot.
struct CpCounters {
    std::atomic<u64> draws{};
    std::atomic<u64> dispatches{};
    std::atomic<u64> backoffs{};
    std::atomic<u64> waits{};
    std::atomic<u64> wait_gfx_ns{};
    std::atomic<u64> wait_asc_ns{};
    std::atomic<u64> wait_gpu_label_ns{};
    std::atomic<u64> wait_cpu_label_ns{};
};

SharedCounters g_shared;
CpCounters g_cp;
std::atomic<int> g_enabled{-1}; // -1 = not decided yet
std::atomic<u64> g_frames{0};

void Bump(std::atomic<u64>& c, u64 v) {
    c.store(c.load(std::memory_order_relaxed) + v, std::memory_order_relaxed);
}

struct Writer {
    std::mutex mutex;
    std::FILE* file = nullptr;
    u64 frame = 0;
    u64 last_frame_ns = 0;
    double tsc_ticks_per_ns = 0.0;
    // previous snapshots of the CpCounters
    u64 draws = 0, dispatches = 0, backoffs = 0, waits = 0;
    u64 wait_gfx = 0, wait_asc = 0, wait_gpu = 0, wait_cpu = 0;
};
Writer g_writer;

// Command processor thread only.
struct AddressStat {
    u64 ns = 0;
    u64 count = 0;
    bool gpu_written = false;
    bool gfx = false;
    bool asc = false;
};
std::unordered_set<u64> g_gpu_written;
std::unordered_map<u64, AddressStat> g_wait_addresses;
u64 g_last_address_dump_frame = 0;

bool Decide() {
    const char* env = std::getenv("DCPC_PROFILE");
    const bool on = env && env[0] == '1';
    if (on) {
        std::scoped_lock lock{g_writer.mutex};
        const auto path = Common::FS::GetUserPath(Common::FS::PathType::LogDir) / "frame_profile.csv";
        g_writer.file = std::fopen(path.string().c_str(), "w");
        if (g_writer.file) {
            std::setvbuf(g_writer.file, nullptr, _IOFBF, 1 << 16);
            std::fprintf(g_writer.file,
                         "frame,qpc_ms,frame_ms,cp_busy_ms,cp_idle_ms,sched_wait_ms,"
                         "present_wait_ms,pipelines,pipeline_ms,draws,dispatches,wait_gfx_ms,"
                         "wait_asc_ms,wait_gpu_label_ms,wait_cpu_label_ms,waits,backoffs\n");
            g_writer.tsc_ticks_per_ns = static_cast<double>(Common::EstimateRDTSCFrequency()) / 1e9;
            LOG_INFO(Debug, "Driveclub-PC frame profiler writing {}", path.string());
        } else {
            LOG_ERROR(Debug, "Driveclub-PC frame profiler cannot open {}", path.string());
        }
    }
    g_enabled.store(on && g_writer.file ? 1 : 0, std::memory_order_release);
    return on && g_writer.file;
}

u64 Take(std::atomic<u64>& counter) {
    return counter.exchange(0, std::memory_order_relaxed);
}

u64 Delta(const std::atomic<u64>& counter, u64& previous) {
    const u64 now = counter.load(std::memory_order_relaxed);
    const u64 d = now - previous;
    previous = now;
    return d;
}

double Ms(u64 ns) {
    return static_cast<double>(ns) / 1e6;
}

double QpcMs() {
#ifdef _WIN32
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return 1000.0 * static_cast<double>(now.QuadPart) / static_cast<double>(freq.QuadPart);
#else
    return static_cast<double>(NowNs()) / 1e6;
#endif
}

void DumpWaitAddresses(u64 frame) {
    std::vector<std::pair<u64, AddressStat>> v(g_wait_addresses.begin(), g_wait_addresses.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.ns > b.second.ns; });
    LOG_INFO(Debug, "Wait addresses, frames {}-{} ({} distinct):", g_last_address_dump_frame, frame,
             v.size());
    for (size_t i = 0; i < v.size() && i < 12; ++i) {
        const auto& [addr, s] = v[i];
        LOG_INFO(Debug, "  {:#x}: {:.1f} ms in {} waits, {}, queues{}{}", addr, Ms(s.ns), s.count,
                 s.gpu_written ? "written by a GPU queue" : "not written by any GPU queue (CPU)",
                 s.gfx ? " gfx" : "", s.asc ? " asc" : "");
    }
    g_wait_addresses.clear();
    g_last_address_dump_frame = frame;
}

} // namespace

bool Enabled() {
    const int state = g_enabled.load(std::memory_order_acquire);
    if (state >= 0) [[likely]] {
        return state == 1;
    }
    static std::once_flag once;
    std::call_once(once, [] { Decide(); });
    return g_enabled.load(std::memory_order_acquire) == 1;
}

u64 NowNs() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}

void AddCpBusy(u64 ns) {
    g_shared.cp_busy_ns.fetch_add(ns, std::memory_order_relaxed);
}
void AddCpIdle(u64 ns) {
    g_shared.cp_idle_ns.fetch_add(ns, std::memory_order_relaxed);
}
void AddSchedWaitTicks(u64 tsc_ticks) {
    g_shared.sched_wait_ticks.fetch_add(tsc_ticks, std::memory_order_relaxed);
}
void AddPresentWait(u64 ns) {
    g_shared.present_wait_ns.fetch_add(ns, std::memory_order_relaxed);
}
void AddPipeline(u64 ns) {
    g_shared.pipelines.fetch_add(1, std::memory_order_relaxed);
    g_shared.pipeline_ns.fetch_add(ns, std::memory_order_relaxed);
}
void AddDraw() {
    Bump(g_cp.draws, 1);
}
void AddDispatch() {
    Bump(g_cp.dispatches, 1);
}
void AddBackoff() {
    Bump(g_cp.backoffs, 1);
}

void NoteGpuWrite(u64 address) {
    if (Enabled()) {
        g_gpu_written.insert(address);
    }
}

void WaitScope::Blocked() {
    if (start_ns_ == 0 && Enabled()) {
        start_ns_ = NowNs();
    }
}

WaitScope::~WaitScope() {
    if (start_ns_ == 0) {
        return;
    }
    const u64 ns = NowNs() - start_ns_;
    const bool gpu = g_gpu_written.contains(address_);
    Bump(g_cp.waits, 1);
    Bump(queue_ == WaitQueue::Gfx ? g_cp.wait_gfx_ns : g_cp.wait_asc_ns, ns);
    Bump(gpu ? g_cp.wait_gpu_label_ns : g_cp.wait_cpu_label_ns, ns);
    auto& s = g_wait_addresses[address_];
    s.ns += ns;
    ++s.count;
    s.gpu_written = gpu;
    (queue_ == WaitQueue::Gfx ? s.gfx : s.asc) = true;
    const u64 frame = g_frames.load(std::memory_order_relaxed);
    if (frame >= g_last_address_dump_frame + 600) {
        DumpWaitAddresses(frame);
    }
}

void OnGuestFrame() {
    if (!Enabled()) {
        return;
    }
    const u64 now = NowNs();
    std::scoped_lock lock{g_writer.mutex};
    auto& w = g_writer;
    const double frame_ms = w.last_frame_ns ? Ms(now - w.last_frame_ns) : 0.0;
    w.last_frame_ns = now;
    const double sched_ms =
        w.tsc_ticks_per_ns > 0
            ? static_cast<double>(Take(g_shared.sched_wait_ticks)) / w.tsc_ticks_per_ns / 1e6
            : 0.0;
    std::fprintf(w.file,
                 "%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%.3f,%llu,%llu,%.3f,%.3f,%.3f,%.3f,%llu,%llu\n",
                 static_cast<unsigned long long>(w.frame), QpcMs(), frame_ms,
                 Ms(Take(g_shared.cp_busy_ns)), Ms(Take(g_shared.cp_idle_ns)), sched_ms,
                 Ms(Take(g_shared.present_wait_ns)),
                 static_cast<unsigned long long>(Take(g_shared.pipelines)),
                 Ms(Take(g_shared.pipeline_ns)),
                 static_cast<unsigned long long>(Delta(g_cp.draws, w.draws)),
                 static_cast<unsigned long long>(Delta(g_cp.dispatches, w.dispatches)),
                 Ms(Delta(g_cp.wait_gfx_ns, w.wait_gfx)), Ms(Delta(g_cp.wait_asc_ns, w.wait_asc)),
                 Ms(Delta(g_cp.wait_gpu_label_ns, w.wait_gpu)),
                 Ms(Delta(g_cp.wait_cpu_label_ns, w.wait_cpu)),
                 static_cast<unsigned long long>(Delta(g_cp.waits, w.waits)),
                 static_cast<unsigned long long>(Delta(g_cp.backoffs, w.backoffs)));
    ++w.frame;
    g_frames.store(w.frame, std::memory_order_relaxed);
    if ((w.frame & 63) == 0) {
        std::fflush(w.file);
    }
}

} // namespace DriveclubPC::Profiler
