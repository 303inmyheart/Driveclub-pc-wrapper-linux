// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/rdtsc.h"
#include "driveclub/frame_profiler.h"
#include "driveclub/vk_call_stats.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace DriveclubPC::Profiler {

namespace {

constexpr u32 MaxQueues = 64;

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
    std::atomic<u64> descriptor_pushes{}, descriptor_writes{}, descriptor_elements{};
    std::atomic<u64> descriptor_gap_fills{}, empty_queue_skips{};
    std::atomic<u64> template_updates{};
    std::atomic<u64> draws{};
    std::atomic<u64> dispatches{};
    std::atomic<u64> backoffs{};
    std::atomic<u64> waits{};
    std::atomic<u64> wait_gfx_ns{};
    std::atomic<u64> wait_asc_ns{};
    std::atomic<u64> wait_gpu_label_ns{};
    std::atomic<u64> wait_cpu_label_ns{};
    std::atomic<u64> gfx_late_ns{};
    std::atomic<u64> gfx_queued_ns{};
};

SharedCounters g_shared;
// driveclub-pc: only touched when profiling is enabled.
struct CostCounters {
    std::atomic<u64> count{}, ns{}, cp_ns{};
};
std::array<CostCounters, static_cast<size_t>(Cost::Count)> g_costs;
thread_local bool g_command_processor = false;
CpCounters g_cp;
std::atomic<int> g_enabled{-1}; // -1 = not decided yet
std::atomic<u32> g_phase{0};

// Event trace (DCPC_TRACE_AT).
struct TraceEvent {
    u64 ns;
    u64 address;
    u64 value;
    u32 queue;
    u32 tid;
    TraceKind kind;
};
constexpr size_t kTraceCapacity = 1u << 20;
std::atomic<int> g_trace_state{0}; // 0 waiting, 1 recording, 2 done
std::atomic<size_t> g_trace_count{0};
std::unique_ptr<TraceEvent[]> g_trace;
u64 g_trace_start_ns = 0, g_trace_len_ns = 0;
double g_trace_at_s = -1;
u64 g_first_frame_ns = 0;
std::atomic<u64> g_frames{0};
std::array<std::atomic<u64>, MaxQueues> g_last_submit_ns{};

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
    u64 template_updates = 0;
    u64 descriptor_pushes = 0, descriptor_writes = 0, descriptor_elements = 0;
    u64 descriptor_gap_fills = 0, empty_queue_skips = 0;
    u64 wait_gfx = 0, wait_asc = 0, wait_gpu = 0, wait_cpu = 0, gfx_late = 0, gfx_queued = 0;
};
Writer g_writer;

// Command processor thread only.
struct AddressStat {
    u64 ns = 0;
    u64 late_ns = 0;
    u64 cpu_ended_ns = 0;
    u64 count = 0;
    int writer_queue = -1;
    bool gfx = false;
    bool asc = false;
};
struct LabelWrites {
    u32 queue = 0;  // queue that wrote the address last
    u64 count = 0;  // GPU-queue writes to the address so far
};
std::unordered_map<u64, LabelWrites> g_label_writer;
std::unordered_map<u64, AddressStat> g_wait_addresses;
u64 g_last_address_dump_frame = 0;

// Game threads submit; the command processor dumps.
std::mutex g_submitters_mutex;
std::map<std::pair<u32, std::string>, u64> g_submitters; // (queue, thread name) -> submits

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
                         "wait_asc_ms,wait_gpu_label_ms,wait_cpu_label_ms,waits,backoffs,"
                         "gfx_wait_late_ms,gfx_wait_queued_ms,phase");
            for (const char* name :
                 {"submit", "readback_record", "download", "readback_flush", "protect"}) {
                std::fprintf(g_writer.file, ",%s_count,%s_ms,%s_cp_ms", name, name, name);
            }
            std::fprintf(g_writer.file,
                         ",template_updates,descriptor_pushes,descriptor_writes,"
                         "descriptor_elements,descriptor_gap_fills,empty_queue_skips,"
                         "vk_cp_calls,vk_cp_ms,vk_other_ms\n");
            g_writer.tsc_ticks_per_ns = static_cast<double>(Common::EstimateRDTSCFrequency()) / 1e9;
            if (const char* at = std::getenv("DCPC_TRACE_AT")) {
                g_trace_at_s = std::atof(at);
                const char* len = std::getenv("DCPC_TRACE_MS");
                g_trace_len_ns = static_cast<u64>((len ? std::atof(len) : 300.0) * 1e6);
                g_trace = std::make_unique<TraceEvent[]>(kTraceCapacity);
            }
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

const std::string& CurrentThreadName() {
    thread_local std::string name;
    if (name.empty()) {
#ifdef _WIN32
        PWSTR desc = nullptr;
        if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &desc)) && desc && desc[0]) {
            const int n = WideCharToMultiByte(CP_UTF8, 0, desc, -1, nullptr, 0, nullptr, nullptr);
            name.resize(n > 0 ? n - 1 : 0);
            WideCharToMultiByte(CP_UTF8, 0, desc, -1, name.data(), n, nullptr, nullptr);
        }
        if (desc) {
            LocalFree(desc);
        }
        name += " (tid " + std::to_string(GetCurrentThreadId()) + ")";
#else
        name = "thread";
#endif
    }
    return name;
}

void DumpWaitAddresses(u64 frame) {
    std::vector<std::pair<u64, AddressStat>> v(g_wait_addresses.begin(), g_wait_addresses.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.ns > b.second.ns; });
    LOG_INFO(Debug, "Wait addresses, frames {}-{} ({} distinct):", g_last_address_dump_frame, frame,
             v.size());
    for (size_t i = 0; i < v.size() && i < 12; ++i) {
        const auto& [addr, s] = v[i];
        LOG_INFO(Debug, "  {:#x}: {:.1f} ms in {} waits ({:.1f} ms ended by a CPU write, {:.1f} ms after a "
                        "late submit), last GPU writer queue {}, waiting queues{}{}",
                 addr, Ms(s.ns), s.count, Ms(s.cpu_ended_ns), Ms(s.late_ns),
                 s.writer_queue < 0 ? std::string("none (CPU)") : std::to_string(s.writer_queue),
                 s.gfx ? " gfx" : "", s.asc ? " asc" : "");
    }
    g_wait_addresses.clear();
    g_last_address_dump_frame = frame;
    std::map<std::pair<u32, std::string>, u64> submitters;
    {
        std::scoped_lock lock{g_submitters_mutex};
        submitters.swap(g_submitters);
    }
    for (const auto& [key, count] : submitters) {
        LOG_INFO(Debug, "  submits to queue {}: {} from thread '{}'", key.first, count, key.second);
    }
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

void MarkCommandProcessorThread() {
    g_command_processor = true;
}

CostScope::CostScope(Cost cost) : cost{cost}, start{Enabled() ? NowNs() : 0} {}

CostScope::~CostScope() {
    if (!start) {
        return;
    }
    auto& c = g_costs[static_cast<size_t>(cost)];
    const u64 elapsed = NowNs() - start;
    c.count.fetch_add(1, std::memory_order_relaxed);
    c.ns.fetch_add(elapsed, std::memory_order_relaxed);
    if (g_command_processor) {
        c.cp_ns.fetch_add(elapsed, std::memory_order_relaxed);
    }
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
void AddTemplateUpdate() {
    if (Enabled())
        Bump(g_cp.template_updates, 1);
}

// driveclub-pc: single-writer counts of actual push payloads and the new CPU-path trials.
void AddDescriptorPush(u32 writes, u32 elements) {
    if (Enabled()) {
        Bump(g_cp.descriptor_pushes, 1);
        Bump(g_cp.descriptor_writes, writes);
        Bump(g_cp.descriptor_elements, elements);
    }
}
void AddDescriptorGapFill(u32 elements) {
    if (Enabled())
        Bump(g_cp.descriptor_gap_fills, elements);
}
void AddEmptyQueueSkips(u64 count) {
    if (Enabled())
        Bump(g_cp.empty_queue_skips, count);
}

namespace {

u32 CurrentTid() {
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return 0;
#endif
}

std::string ThreadNameOf(u32 tid) {
#ifdef _WIN32
    std::string name;
    if (HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid)) {
        PWSTR desc = nullptr;
        if (SUCCEEDED(GetThreadDescription(h, &desc)) && desc && desc[0]) {
            const int n = WideCharToMultiByte(CP_UTF8, 0, desc, -1, nullptr, 0, nullptr, nullptr);
            name.resize(n > 0 ? n - 1 : 0);
            WideCharToMultiByte(CP_UTF8, 0, desc, -1, name.data(), n, nullptr, nullptr);
        }
        if (desc) {
            LocalFree(desc);
        }
        CloseHandle(h);
    }
    return name.empty() ? std::to_string(tid) : name;
#else
    return std::to_string(tid);
#endif
}

u64 ReadGuestU64(u64 address) {
    // Labels may be 4 bytes at the end of a page; never read past it.
    u64 value = 0;
    if (address != 0) {
        const size_t bytes = (address & 0xFFF) <= 0xFF8 ? 8 : 4;
        std::memcpy(&value, reinterpret_cast<const void*>(address), bytes);
    }
    return value;
}

void DumpTrace() {
    const size_t count = std::min(g_trace_count.load(std::memory_order_acquire), kTraceCapacity);
    const auto path = Common::FS::GetUserPath(Common::FS::PathType::LogDir) / "frame_trace.csv";
    std::FILE* file = std::fopen(path.string().c_str(), "w");
    if (!file) {
        LOG_ERROR(Debug, "Driveclub-PC frame trace cannot open {}", path.string());
        return;
    }
    static constexpr const char* kKinds[] = {"gpu_write", "wait_begin", "wait_end", "submit",
                                             "flip"};
    std::unordered_map<u32, std::string> names;
    std::fprintf(file, "ns,kind,thread,queue,address,value\n");
    for (size_t i = 0; i < count; ++i) {
        const auto& e = g_trace[i];
        auto [it, inserted] = names.try_emplace(e.tid);
        if (inserted) {
            it->second = ThreadNameOf(e.tid);
        }
        std::fprintf(file, "%llu,%s,%s,%u,0x%llx,0x%llx\n",
                     static_cast<unsigned long long>(e.ns - g_trace_start_ns),
                     kKinds[static_cast<u32>(e.kind)], it->second.c_str(), e.queue,
                     static_cast<unsigned long long>(e.address),
                     static_cast<unsigned long long>(e.value));
    }
    std::fclose(file);
    LOG_INFO(Debug, "Driveclub-PC frame trace: {} events written to {}", count, path.string());
}

/// Called per presented frame: starts and ends the trace window.
void UpdateTraceWindow(u64 now) {
    if (!g_trace) {
        return;
    }
    if (g_first_frame_ns == 0) {
        g_first_frame_ns = now;
    }
    const int state = g_trace_state.load(std::memory_order_relaxed);
    if (state == 0 && now - g_first_frame_ns >= static_cast<u64>(g_trace_at_s * 1e9)) {
        g_trace_start_ns = now;
        g_trace_state.store(1, std::memory_order_release);
        LOG_INFO(Debug, "Driveclub-PC frame trace: recording {} ms", g_trace_len_ns / 1000000);
    } else if (state == 1 && now - g_trace_start_ns >= g_trace_len_ns) {
        g_trace_state.store(2, std::memory_order_release);
        // Writers that read state 1 just before may still be storing; let them finish.
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        DumpTrace();
    }
}

} // namespace

void Trace(TraceKind kind, u64 address, u32 queue) {
    if (g_trace_state.load(std::memory_order_relaxed) != 1) {
        return;
    }
    const size_t slot = g_trace_count.fetch_add(1, std::memory_order_relaxed);
    if (slot >= kTraceCapacity) {
        return;
    }
    g_trace[slot] = {NowNs(), address,
                     kind == TraceKind::Submit || kind == TraceKind::Flip ? 0
                                                                          : ReadGuestU64(address),
                     queue, CurrentTid(), kind};
}

void TraceGpuWrite(u64 address, u32 queue) {
    Trace(TraceKind::GpuWrite, address, queue);
}

void SetPhase(u32 phase) {
    g_phase.store(phase, std::memory_order_relaxed);
}

void NoteGpuWrite(u64 address, u32 queue) {
    if (Enabled()) {
        auto& w = g_label_writer[address];
        w.queue = queue;
        ++w.count;
    }
}

void NoteSubmit(u32 queue) {
    if (!Enabled() || queue >= MaxQueues) {
        return;
    }
    g_last_submit_ns[queue].store(NowNs(), std::memory_order_release);
    Trace(TraceKind::Submit, 0, queue);
    std::scoped_lock lock{g_submitters_mutex};
    ++g_submitters[{queue, CurrentThreadName()}];
}

void WaitScope::Blocked() {
    if (start_ns_ == 0 && Enabled()) {
        start_ns_ = NowNs();
        Trace(TraceKind::WaitBegin, address_, queue_ == WaitQueue::Gfx ? 0 : 1);
        const auto it = g_label_writer.find(address_);
        gpu_writes_at_start_ = it != g_label_writer.end() ? it->second.count : 0;
    }
}

WaitScope::~WaitScope() {
    if (start_ns_ == 0) {
        return;
    }
    const u64 end = NowNs();
    const u64 ns = end - start_ns_;
    Trace(TraceKind::WaitEnd, address_, queue_ == WaitQueue::Gfx ? 0 : 1);
    const auto writer = g_label_writer.find(address_);
    const bool known = writer != g_label_writer.end();
    // Ended by a GPU-queue write during the wait; otherwise the game's CPU code wrote the value.
    const bool gpu = known && writer->second.count > gpu_writes_at_start_;
    // The game submitted to the writing queue while we waited: the producer arrived late.
    const bool late = gpu && writer->second.queue < MaxQueues &&
                      g_last_submit_ns[writer->second.queue].load(std::memory_order_acquire) >
                          start_ns_;
    Bump(g_cp.waits, 1);
    Bump(queue_ == WaitQueue::Gfx ? g_cp.wait_gfx_ns : g_cp.wait_asc_ns, ns);
    Bump(gpu ? g_cp.wait_gpu_label_ns : g_cp.wait_cpu_label_ns, ns);
    if (queue_ == WaitQueue::Gfx) {
        Bump(late ? g_cp.gfx_late_ns : g_cp.gfx_queued_ns, ns);
    }
    auto& s = g_wait_addresses[address_];
    s.ns += ns;
    s.late_ns += late ? ns : 0;
    ++s.count;
    s.writer_queue = known ? static_cast<int>(writer->second.queue) : -1;
    s.cpu_ended_ns += gpu ? 0 : ns;
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
    Trace(TraceKind::Flip, 0, 0);
    UpdateTraceWindow(now);
    std::scoped_lock lock{g_writer.mutex};
    auto& w = g_writer;
    const double frame_ms = w.last_frame_ns ? Ms(now - w.last_frame_ns) : 0.0;
    w.last_frame_ns = now;
    const double sched_ms =
        w.tsc_ticks_per_ns > 0
            ? static_cast<double>(Take(g_shared.sched_wait_ticks)) / w.tsc_ticks_per_ns / 1e6
            : 0.0;
    std::fprintf(
        w.file,
        "%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%.3f,%llu,%llu,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%.3f,"
        "%.3f,"
        "%u",
        static_cast<unsigned long long>(w.frame), QpcMs(), frame_ms, Ms(Take(g_shared.cp_busy_ns)),
        Ms(Take(g_shared.cp_idle_ns)), sched_ms, Ms(Take(g_shared.present_wait_ns)),
        static_cast<unsigned long long>(Take(g_shared.pipelines)), Ms(Take(g_shared.pipeline_ns)),
        static_cast<unsigned long long>(Delta(g_cp.draws, w.draws)),
        static_cast<unsigned long long>(Delta(g_cp.dispatches, w.dispatches)),
        Ms(Delta(g_cp.wait_gfx_ns, w.wait_gfx)), Ms(Delta(g_cp.wait_asc_ns, w.wait_asc)),
        Ms(Delta(g_cp.wait_gpu_label_ns, w.wait_gpu)), Ms(Delta(g_cp.wait_cpu_label_ns, w.wait_cpu)),
        static_cast<unsigned long long>(Delta(g_cp.waits, w.waits)),
        static_cast<unsigned long long>(Delta(g_cp.backoffs, w.backoffs)),
        Ms(Delta(g_cp.gfx_late_ns, w.gfx_late)), Ms(Delta(g_cp.gfx_queued_ns, w.gfx_queued)),
        g_phase.load(std::memory_order_relaxed));
    for (auto& c : g_costs) {
        std::fprintf(w.file, ",%llu,%.3f,%.3f", static_cast<unsigned long long>(Take(c.count)),
                     Ms(Take(c.ns)), Ms(Take(c.cp_ns)));
    }
    u64 vk_cp_calls = 0;
    double vk_cp_ms = 0, vk_other_ms = 0;
    VkCallStats::TakeFrame(vk_cp_calls, vk_cp_ms, vk_other_ms);
    std::fprintf(
        w.file, ",%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.3f,%.3f\n",
        static_cast<unsigned long long>(Delta(g_cp.template_updates, w.template_updates)),
        static_cast<unsigned long long>(Delta(g_cp.descriptor_pushes, w.descriptor_pushes)),
        static_cast<unsigned long long>(Delta(g_cp.descriptor_writes, w.descriptor_writes)),
        static_cast<unsigned long long>(Delta(g_cp.descriptor_elements, w.descriptor_elements)),
        static_cast<unsigned long long>(Delta(g_cp.descriptor_gap_fills, w.descriptor_gap_fills)),
        static_cast<unsigned long long>(Delta(g_cp.empty_queue_skips, w.empty_queue_skips)),
        static_cast<unsigned long long>(vk_cp_calls), vk_cp_ms, vk_other_ms);
    ++w.frame;
    g_frames.store(w.frame, std::memory_order_relaxed);
    if ((w.frame & 63) == 0) {
        std::fflush(w.file);
    }
}

} // namespace DriveclubPC::Profiler
