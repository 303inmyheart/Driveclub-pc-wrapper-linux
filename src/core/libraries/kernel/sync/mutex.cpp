// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "mutex.h"

#include "common/assert.h"

#ifdef _WIN64
#include <cstdlib>
#include <immintrin.h>
#endif

namespace Libraries::Kernel {

#ifdef _WIN64
namespace {
bool UseFastMutex() {
    static const bool fast = [] {
        const char* env = std::getenv("DCPC_FAST_MUTEX");
        return !(env && env[0] == '0');
    }();
    return fast;
}

// Short critical sections are usually released within this many pauses.
constexpr int kSpinCount = 64;
} // namespace
#endif

TimedMutex::TimedMutex() {
#ifdef _WIN64
    fast = UseFastMutex();
    handle =
        fast ? CreateEvent(nullptr, false, false, nullptr) : CreateMutex(nullptr, false, nullptr);
    ASSERT(handle);
#endif
}

TimedMutex::~TimedMutex() {
#ifdef _WIN64
    CloseHandle(handle);
#endif
}

void TimedMutex::lock() {
#ifdef _WIN64
    if (!fast) {
        for (;;) {
            u64 res = WaitForSingleObjectEx(handle, INFINITE, true);
            if (res == WAIT_OBJECT_0) {
                return;
            }
        }
    }
    if (try_lock()) {
        return;
    }
    for (int i = 0; i < kSpinCount; ++i) {
        _mm_pause();
        if (state.load(std::memory_order_relaxed) == 0 && try_lock()) {
            return;
        }
    }
    // Whoever takes the lock from here on leaves state 2, so its unlock wakes the next waiter.
    while (MarkWaiting() != 0) {
        // An APC (alertable wait) or a stale signal only means another round.
        WaitForSingleObjectEx(handle, INFINITE, true);
    }
#else
    mtx.lock();
#endif
}

bool TimedMutex::try_lock() {
#ifdef _WIN64
    if (!fast) {
        return WaitForSingleObjectEx(handle, 0, true) == WAIT_OBJECT_0;
    }
    u32 expected = 0;
    return state.compare_exchange_strong(expected, 1, std::memory_order_acquire,
                                         std::memory_order_relaxed);
#else
    return mtx.try_lock();
#endif
}

void TimedMutex::unlock() {
#ifdef _WIN64
    if (!fast) {
        ReleaseMutex(handle);
        return;
    }
    if (state.exchange(0, std::memory_order_release) == 2) {
        SetEvent(handle);
    }
#else
    mtx.unlock();
#endif
}

} // namespace Libraries::Kernel
