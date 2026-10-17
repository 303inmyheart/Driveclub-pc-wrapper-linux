// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <fmt/format.h>

#include "common/logging/log.h"
#include "common/thread.h"
#include "driveclub/async_pipelines.h"

namespace DriveclubPC::AsyncPipelines {

namespace {

bool ReadEnabled() {
    // driveclub-pc: on by default since 0.2; DCPC_ASYNC_PIPELINES=0 turns it off.
    const char* v = std::getenv("DCPC_ASYNC_PIPELINES");
    return !(v && v[0] == '0');
}

u32 ReadThreads() {
    const char* v = std::getenv("DCPC_ASYNC_PIPELINES_THREADS");
    const int n = v ? std::atoi(v) : 2;
    return static_cast<u32>(std::clamp(n, 1, 8));
}

class Pool {
public:
    Pool() {
        const u32 count = ReadThreads();
        for (u32 i = 0; i < count; ++i) {
            workers.emplace_back([this, i] { Run(i); });
        }
        LOG_INFO(Render_Vulkan, "driveclub-pc: async graphics pipeline creation on {} threads",
                 count);
    }

    ~Pool() {
        {
            std::scoped_lock lk{mutex};
            stop = true;
        }
        cv.notify_all();
        for (auto& t : workers) {
            t.join();
        }
    }

    void Push(std::function<void()> job) {
        {
            std::scoped_lock lk{mutex};
            jobs.push_back(std::move(job));
        }
        cv.notify_one();
    }

private:
    void Run(u32 index) {
        Common::SetCurrentThreadName(fmt::format("dcpc:PipelineCompile{}", index).c_str());
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lk{mutex};
                cv.wait(lk, [this] { return stop || !jobs.empty(); });
                if (jobs.empty()) {
                    return;
                }
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            job();
        }
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::function<void()>> jobs;
    std::vector<std::thread> workers;
    bool stop = false;
};

std::atomic<u64> g_skips{};

} // namespace

bool Enabled() {
    static const bool enabled = ReadEnabled();
    return enabled;
}

void Submit(std::function<void()> job) {
    static Pool pool;
    pool.Push(std::move(job));
}

void CountSkip() {
    g_skips.fetch_add(1, std::memory_order_relaxed);
}

u64 TakeSkips() {
    return g_skips.exchange(0, std::memory_order_relaxed);
}

} // namespace DriveclubPC::AsyncPipelines
