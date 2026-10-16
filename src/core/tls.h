// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstring>
#include "common/types.h"
#include "driveclub/guest_stats.h"
#ifdef _WIN32
#include <malloc.h>
#endif

namespace Xbyak {
class CodeGenerator;
}

namespace Libraries::Fiber {
struct OrbisFiberContext;
}

namespace Core {

union DtvEntry {
    std::size_t counter;
    u8* pointer;
};

struct Tcb {
    Tcb* tcb_self;
    DtvEntry* tcb_dtv;
    void* tcb_thread;
    void* tcb_spare[2];
    u64 tcb_canary;
    ::Libraries::Fiber::OrbisFiberContext* tcb_fiber;
};

#ifdef _WIN32
/// Gets the thread local storage key for the TCB block.
u32 GetTcbKey();
#endif

/// Sets the data pointer to the TCB block.
void SetTcbBase(void* image_address);

/// Retrieves Tcb structure for the calling thread.
Tcb* GetTcbBase();

/// Makes sure TLS is initialized for the thread before entering guest.
void InitializeTLS();

template <auto f>
struct HostCallWrapperImpl;

template <class ReturnType, class... Args, PS4_SYSV_ABI ReturnType (*func)(Args...)>
struct HostCallWrapperImpl<func> {
    static ReturnType PS4_SYSV_ABI wrap(Args... args) {
        // driveclub-pc: DCPC_GUEST_STATS times every HLE call (driveclub/guest_stats.h).
        if (DriveclubPC::GuestStats::Active()) [[unlikely]] {
#ifdef _MSC_VER
            static const u32 id = DriveclubPC::GuestStats::Register(__FUNCSIG__);
#else
            static const u32 id = DriveclubPC::GuestStats::Register(__PRETTY_FUNCTION__);
#endif
            DriveclubPC::GuestStats::Scope scope{id};
            return func(args...);
        }
        return func(args...);
    }
};

#define HOST_CALL(func) (Core::HostCallWrapperImpl<func>::wrap)

} // namespace Core
