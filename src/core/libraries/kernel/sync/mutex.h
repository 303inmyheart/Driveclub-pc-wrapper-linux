// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>

#include "common/types.h"

#ifdef _WIN64
#include <atomic>
#include <windows.h>
#else
#include <mutex>
#endif

namespace Libraries::Kernel {

class TimedMutex {
public:
    TimedMutex();
    ~TimedMutex();

    void lock();
    bool try_lock();

    void unlock();

    template <class Rep, class Period>
    bool try_lock_for(const std::chrono::duration<Rep, Period>& rel_time) {
#ifdef _WIN64
        constexpr auto zero = std::chrono::duration<Rep, Period>::zero();
        const auto now = std::chrono::steady_clock::now();

        std::chrono::steady_clock::time_point abs_time = now;
        if (rel_time > zero) {
            constexpr auto max = (std::chrono::steady_clock::time_point::max)();
            if (abs_time < max - rel_time) {
                abs_time += rel_time;
            } else {
                abs_time = max;
            }
        }

        return try_lock_until(abs_time);
#else
        return mtx.try_lock_for(rel_time);
#endif
    }

    template <class Clock, class Duration>
    bool try_lock_until(const std::chrono::time_point<Clock, Duration>& abs_time) {
#ifdef _WIN64
        if (try_lock()) {
            return true;
        }
        u32 state = fast ? MarkWaiting() : 1;
        while (state != 0) {
            const auto now = Clock::now();
            if (abs_time <= now) {
                return false;
            }
            const auto rel_ms = std::chrono::ceil<std::chrono::milliseconds>(abs_time - now);
            const u64 res = WaitForSingleObjectEx(handle, static_cast<u64>(rel_ms.count()), true);
            if (!fast) {
                if (res == WAIT_OBJECT_0) {
                    return true;
                } else if (res == WAIT_TIMEOUT) {
                    return false;
                }
                continue;
            }
            state = MarkWaiting();
        }
        return true;
#else
        return mtx.try_lock_until(abs_time);
#endif
    }

private:
#ifdef _WIN64
    // driveclub-pc: a kernel mutex object made every lock and unlock a system call, even without
    // contention (~1.7-5 us per lock on the game threads, docs/benchmarks.md). The fast mutex
    // keeps its state in user space: 0 free, 1 locked, 2 locked with possible waiters. Only a
    // contended lock sleeps, on an auto-reset event in an alertable wait like the kernel mutex
    // did; an unlock that sees state 2 sets the event. DCPC_FAST_MUTEX=0 restores the kernel
    // mutex object.
    u32 MarkWaiting() {
        return state.exchange(2, std::memory_order_acquire);
    }

    std::atomic<u32> state{0};
    bool fast;
    HANDLE handle; ///< auto-reset event (fast) or mutex object
#else
    std::timed_mutex mtx;
#endif
};

} // namespace Libraries::Kernel
