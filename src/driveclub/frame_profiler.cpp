// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/rdtsc.h"
#include "driveclub/frame_profiler.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace DriveclubPC::Profiler {

namespace {

struct Counters {
    std::atomic<u64> cp_busy_ns{};
    std::atomic<u64> cp_idle_ns{};
    std::atomic<u64> sched_wait_ticks{};
    std::atomic<u64> present_wait_ns{};
    std::atomic<u64> pipelines{};
    std::atomic<u64> pipeline_ns{};
    std::atomic<u64> draws{};
    std::atomic<u64> dispatches{};
};

Counters g_counters;
std::atomic<int> g_enabled{-1}; // -1 = not decided yet

struct Writer {
    std::mutex mutex;
    std::FILE* file = nullptr;
    u64 frame = 0;
    u64 last_frame_ns = 0;
    double tsc_ticks_per_ns = 0.0;
};
Writer g_writer;

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
                         "present_wait_ms,pipelines,pipeline_ms,draws,dispatches\n");
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
    g_counters.cp_busy_ns.fetch_add(ns, std::memory_order_relaxed);
}
void AddCpIdle(u64 ns) {
    g_counters.cp_idle_ns.fetch_add(ns, std::memory_order_relaxed);
}
void AddSchedWaitTicks(u64 tsc_ticks) {
    g_counters.sched_wait_ticks.fetch_add(tsc_ticks, std::memory_order_relaxed);
}
void AddPresentWait(u64 ns) {
    g_counters.present_wait_ns.fetch_add(ns, std::memory_order_relaxed);
}
void AddPipeline(u64 ns) {
    g_counters.pipelines.fetch_add(1, std::memory_order_relaxed);
    g_counters.pipeline_ns.fetch_add(ns, std::memory_order_relaxed);
}
void AddDraw() {
    g_counters.draws.fetch_add(1, std::memory_order_relaxed);
}
void AddDispatch() {
    g_counters.dispatches.fetch_add(1, std::memory_order_relaxed);
}

void OnGuestFrame() {
    if (!Enabled()) {
        return;
    }
    const u64 now = NowNs();
    std::scoped_lock lock{g_writer.mutex};
    const double frame_ms =
        g_writer.last_frame_ns ? static_cast<double>(now - g_writer.last_frame_ns) / 1e6 : 0.0;
    g_writer.last_frame_ns = now;
    const double sched_ms = g_writer.tsc_ticks_per_ns > 0
                                ? static_cast<double>(Take(g_counters.sched_wait_ticks)) /
                                      g_writer.tsc_ticks_per_ns / 1e6
                                : 0.0;
    std::fprintf(g_writer.file, "%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%.3f,%llu,%llu\n",
                 static_cast<unsigned long long>(g_writer.frame++), QpcMs(), frame_ms,
                 static_cast<double>(Take(g_counters.cp_busy_ns)) / 1e6,
                 static_cast<double>(Take(g_counters.cp_idle_ns)) / 1e6, sched_ms,
                 static_cast<double>(Take(g_counters.present_wait_ns)) / 1e6,
                 static_cast<unsigned long long>(Take(g_counters.pipelines)),
                 static_cast<double>(Take(g_counters.pipeline_ns)) / 1e6,
                 static_cast<unsigned long long>(Take(g_counters.draws)),
                 static_cast<unsigned long long>(Take(g_counters.dispatches)));
    if ((g_writer.frame & 63) == 0) {
        std::fflush(g_writer.file);
    }
}

} // namespace DriveclubPC::Profiler
