// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <utility>
#include "common/adaptive_mutex.h"
#include "common/assert.h"
#include "common/debug.h"
#include "common/decoder.h"
#include "common/div_ceil.h"
#include "common/error.h"
#include "common/logging/log.h"
#include "common/multi_level_page_table.h"
#include "common/rdtsc.h"
#include "common/scope_exit.h"
#include "common/signal_context.h"
#include "common/thread.h"
#include "core/emulator_settings.h"
#include "core/memory.h"
#include "core/signals.h"
#include "driveclub/frame_profiler.h"
#include "driveclub/diag_tables.h"
#include "driveclub/frame_profiler.h"
#include "driveclub/guest_stats.h"
#include "video_core/page_manager.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"

#ifndef _WIN64
#include <sys/mman.h>
#include "common/adaptive_mutex.h"
#else
#include <windows.h>
#endif

#ifdef __linux__
#include <thread>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#else
#include "common/spin_lock.h"
#endif

namespace VideoCore {

struct PageManager::Impl {
    struct PageState {
        u8 num_write_watchers;
        u8 num_read_watchers;

        Core::MemoryPermission WritePerm() const noexcept {
            return num_write_watchers == 0 ? Core::MemoryPermission::Write
                                           : Core::MemoryPermission::None;
        }

        Core::MemoryPermission ReadPerm() const noexcept {
            return num_read_watchers == 0 ? Core::MemoryPermission::Read
                                          : Core::MemoryPermission::None;
        }

        Core::MemoryPermission Perms() const noexcept {
            return ReadPerm() | WritePerm();
        }

        template <bool is_read>
        u8 GetPage() const {
            if constexpr (is_read) {
                return num_read_watchers;
            } else {
                return num_write_watchers;
            }
        }

        constexpr Core::MemoryPermission Update(PageOp write_op, bool update_write = true,
                                                PageOp read_op = PageOp::None,
                                                bool update_read = false) {
            if (update_read) {
                if (read_op == PageOp::Track) {
                    ASSERT_MSG(num_read_watchers == 0, "Too many watchers");
                } else if (read_op == PageOp::Untrack) {
                    ASSERT_MSG(num_read_watchers > 0, "Not enough watchers");
                }
                num_read_watchers += std::to_underlying(read_op);
            }
            if (update_write) {
                if (write_op == PageOp::Track) {
                    ASSERT_MSG(num_write_watchers < 255, "Too many watchers");
                } else if (write_op == PageOp::Untrack) {
                    ASSERT_MSG(num_write_watchers > 0, "Not enough watchers");
                }
                num_write_watchers += std::to_underlying(write_op);
            }
            return Perms();
        }
    };

    static constexpr size_t ADDRESS_BITS = 40;
    static constexpr size_t NUM_ADDRESS_PAGES = 1ULL << (40 - PM_PAGE_BITS);
    static constexpr size_t NUM_ADDRESS_LOCKS = NUM_ADDRESS_PAGES / NUM_REGION_PAGES;
    inline static Vulkan::Rasterizer* rasterizer;

    Impl() = default;
    virtual ~Impl() = default;

    virtual void OnMap(VAddr address, size_t size) {
        // No-op
        EnsurePages(address, address + size);
    }

    virtual void OnUnmap(VAddr address, size_t size) {
        // No-op
    }

    virtual void Protect(VAddr address, size_t size, Core::MemoryPermission perms) = 0;

    void EnsurePages(VAddr begin, VAddr end) {
        end = std::min(end, VAddr{1} << ADDRESS_BITS) - 1;
        // The reserve is inclusive of the end page; an empty range, or one wholly past the table
        // (a POSIX unmap reports the range merged with adjacent free space), reserves nothing.
        if (begin > end) {
            return;
        }
        const size_t start_page = begin >> PM_PAGE_BITS;
        const size_t end_page = end >> PM_PAGE_BITS;
        // Guest mapping and the fallback in UpdatePageWatchers can both reserve, and the page
        // table pools are not thread safe.
        std::scoped_lock lk{reserve_mutex};
        cached_pages.reserve(start_page, end_page);
        locks.reserve(start_page, end_page);
    }

    void UpdatePageWatchers(VAddr addr, u64 size, PageOp write_op) {
        const u64 page_start = addr >> PM_PAGE_BITS;
        const u64 page_end = Common::DivCeil(addr + size, PM_PAGE_SIZE);

        Core::MemoryPermission perms{};
        u64 range_begin = page_start;
        u64 range_pages = 0;
        u64 potential_pages = 0;

        const auto release_pending = [&] {
            if (range_pages > 0) {
                Protect(range_begin << PM_PAGE_BITS, range_pages << PM_PAGE_BITS, perms);
                range_pages = 0;
                potential_pages = 0;
            }
        };

        // Iterate requested pages
        const u64 aligned_addr = page_start << PM_PAGE_BITS;
        const u64 aligned_end = page_end << PM_PAGE_BITS;
        if (!rasterizer->IsMapped(aligned_addr, aligned_end - aligned_addr)) {
            LOG_WARNING(Render,
                        "Tracking memory region {:#x} - {:#x} which is not fully GPU mapped.",
                        aligned_addr, aligned_end);
            EnsurePages(aligned_addr, aligned_end);
        }

        for (u64 page = page_start; page != page_end; ++page) {
            PageState* state = cached_pages.find(page);
            if (!state) {
                continue;
            }

            locks[page].lock();

            const auto old_perms = state->Perms();
            if (page == page_start) {
                perms = old_perms;
            }

            // Apply the change to the page state
            const auto new_perms = state->Update(write_op);
            if (new_perms != perms) [[unlikely]] {
                // If the protection changed add pending (un)protect action
                release_pending();
                perms = new_perms;
            } else if (range_pages != 0) {
                ++potential_pages;
            }

            // If the page must be (un)protected
            if (new_perms != old_perms) {
                if (range_pages == 0) {
                    // Start a new potential range
                    range_begin = page;
                    potential_pages = 1;
                }
                // Extend current range up to potential range
                range_pages = potential_pages;
            }
        }

        // Add pending (un)protect action
        release_pending();

        for (u64 page = page_start; page != page_end; ++page) {
            if (auto* lock = locks.find(page)) {
                lock->unlock();
            }
        }
    }

    void UpdatePageWatchersForRegion(VAddr base_addr, const Bounds& bounds,
                                     const RegionBits& write_mask, const RegionBits& read_mask,
                                     PageOp write_op, PageOp read_op) {
        const u64 base_page = base_addr >> PM_PAGE_BITS;
        const u64 page_start = bounds.start_word * PAGES_PER_WORD + bounds.start_page;
        const u64 page_end = bounds.end_word * PAGES_PER_WORD + bounds.end_page + 1;

        Core::MemoryPermission perms{};
        u64 range_begin = base_page + page_start;
        u64 range_pages = 0;
        u64 potential_pages = 0;

        const auto release_pending = [&] {
            if (range_pages > 0) {
                Protect(range_begin << PM_PAGE_BITS, range_pages << PM_PAGE_BITS, perms);
                range_pages = 0;
                potential_pages = 0;
            }
        };

        for (u64 page = page_start; page != page_end; ++page) {
            PageState* state = cached_pages.find(base_page + page);
            if (!state) {
                continue;
            }

            locks[base_page + page].lock();

            const auto old_perms = state->Perms();
            if (page == page_start) {
                perms = old_perms;
            }

            // Apply the change to the page state
            const bool update_write = write_op != PageOp::None && write_mask.GetPage(page);
            const bool update_read = read_op != PageOp::None && read_mask.GetPage(page);
            const auto new_perms = state->Update(write_op, update_write, read_op, update_read);

            if (new_perms != perms) [[unlikely]] {
                // If the protection changed add pending (un)protect action
                release_pending();
                perms = new_perms;
            } else if (range_pages != 0) {
                // If the protection did not change, extend the potential range
                ++potential_pages;
            }

            // If the page must be (un)protected
            if (new_perms != old_perms) {
                if (range_pages == 0) {
                    // Start a new potential range
                    range_begin = base_page + page;
                    potential_pages = 1;
                }
                // Extend current rango up to potential range
                range_pages = potential_pages;
            }
        }

        // Add pending (un)protect action
        release_pending();

        for (u64 page = page_start; page != page_end; ++page) {
            if (auto* lock = locks.find(base_page + page)) {
                lock->unlock();
            }
        }
    }

    struct PageTraits {
        using Entry = PageState;
        static constexpr size_t ADDRESS_SPACE_BITS = ADDRESS_BITS;
        static constexpr size_t L1_BITS = 16;
        static constexpr size_t PAGE_BITS = PM_PAGE_BITS;
        static constexpr bool NULL_CHECK = false;
    };
    Common::MultiLevelPageTable<PageTraits> cached_pages;
    struct MutexTraits {
#ifdef PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
        using Entry = Common::AdaptiveMutex;
#else
        using Entry = std::mutex;
#endif
        static constexpr size_t ADDRESS_SPACE_BITS = ADDRESS_BITS;
        static constexpr size_t L1_BITS = 16;
        static constexpr size_t PAGE_BITS = PM_PAGE_BITS;
        static constexpr bool NULL_CHECK = false;
    };
    Common::MultiLevelPageTable<MutexTraits> locks;
    std::mutex reserve_mutex;
};

#ifdef __linux__
struct UffdImpl : public PageManager::Impl {
private:
    std::jthread ufd_thread;
    int uffd;

public:
    UffdImpl(Vulkan::Rasterizer* rasterizer_) : Impl() {
        rasterizer = rasterizer_;
        uffd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY);
        if (uffd == -1) {
            LOG_ERROR(Common_Memory,
                      "userfaultfd syscall failed: {}, falling back to signal implementation",
                      Common::GetLastErrorMsg());
            throw std::runtime_error("userfaultfd");
        }

        // Request uffdio features from kernel.
        uffdio_api api;
        api.api = UFFD_API;
        api.features = UFFD_FEATURE_THREAD_ID;
        const int ret = ioctl(uffd, UFFDIO_API, &api);
        if (ret != 0) {
            LOG_ERROR(Common_Memory,
                      "uffdio_api call failed: {}, falling back to signal implementation",
                      Common::GetLastErrorMsg());
            throw std::runtime_error("uffdio_api");
        }

        // Create uffd handler thread
        ufd_thread = std::jthread([&](std::stop_token token) { UffdHandler(token); });
    }

    ~UffdImpl() = default;

    void OnMap(VAddr address, size_t size) override {
        PageManager::Impl::OnMap(address, size);
        uffdio_register reg;
        reg.range.start = address;
        reg.range.len = size;
        reg.mode = UFFDIO_REGISTER_MODE_WP;
        const int ret = ioctl(uffd, UFFDIO_REGISTER, &reg);
        ASSERT_MSG(ret != -1, "Uffdio register failed with error: {}", Common::GetLastErrorMsg());
    }

    void OnUnmap(VAddr address, size_t size) override {
        uffdio_range range;
        range.start = address;
        range.len = size;
        const int ret = ioctl(uffd, UFFDIO_UNREGISTER, &range);
        ASSERT_MSG(ret != -1, "Uffdio unregister failed with error: {}", Common::GetLastErrorMsg());
    }

    void Protect(VAddr address, size_t size, Core::MemoryPermission perms) override {
        bool allow_write = True(perms & Core::MemoryPermission::Write);
        uffdio_writeprotect wp;
        wp.range.start = address;
        wp.range.len = size;
        wp.mode = allow_write ? UFFDIO_WRITEPROTECT_MODE_DONTWAKE : UFFDIO_WRITEPROTECT_MODE_WP;
        const int ret = ioctl(uffd, UFFDIO_WRITEPROTECT, &wp);
        ASSERT_MSG(ret != -1, "Uffdio writeprotect failed with error: {}",
                   Common::GetLastErrorMsg());
    }

    void UffdHandler(std::stop_token token) {
        Common::SetCurrentThreadName("shadPS4:Uffd");

        auto regions = Core::Memory::Instance()->GetAddressSpace().GetUsableRegions();
        for (auto& region : regions) {
            OnMap(region.lower(), region.upper());
        }
        LOG_INFO(Common_Memory, "registered reserved memory with userfaultfd");

        while (!token.stop_requested()) {
            pollfd pollfd;
            pollfd.fd = uffd;
            pollfd.events = POLLIN;

            // Block until the descriptor is ready for data reads.
            const int pollres = poll(&pollfd, 1, -1);
            switch (pollres) {
            case -1:
                perror("Poll userfaultfd");
                continue;
                break;
            case 0:
                continue;
            case 1:
                break;
            default:
                UNREACHABLE_MSG("Unexpected number of descriptors {} out of poll", pollres);
            }

            // We don't want an error condition to have occured.
            ASSERT_MSG(!(pollfd.revents & POLLERR), "POLLERR on userfaultfd");

            // We waited until there is data to read, we don't care about anything else.
            if (!(pollfd.revents & POLLIN)) {
                continue;
            }

            // Read message from kernel.
            uffd_msg msg;
            const int readret = read(uffd, &msg, sizeof(msg));
            if (readret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                LOG_ERROR(Common_Memory, "Unexpected result of uffd read: {}",
                          Common::GetLastErrorMsg());
                break;
            }
            ASSERT_MSG(readret == sizeof(msg), "Unexpected short read, exiting");
            ASSERT(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP);

            // Notify rasterizer about the fault.
            const VAddr addr = msg.arg.pagefault.address;
            const auto ptid = msg.arg.pagefault.feat.ptid;
            rasterizer->InvalidateMemory(addr, 1,
                                         ptid == rasterizer->GetGpuCommandProcessorThreadId());

            // Some calls to InvalidateMemory never reach the UFFDIO_WRITEPROTECT ioctl in
            // ::Protect, therefore we use MODE_DONTWAKE and wake the thread with UFFDIO_WAKE here
            uffdio_range wake;
            wake.start = msg.arg.pagefault.address;
            wake.len = PageManager::PM_PAGE_SIZE;
            const int ret = ioctl(uffd, UFFDIO_WAKE, &wake);
            ASSERT_MSG(ret != -1, "Waking thread {} failed with: {}", ptid,
                       Common::GetLastErrorMsg());
        }
    }
};
#endif // __linux__

#ifdef _WIN32
// driveclub-pc: the tracker protects whole pages, but the GPU often writes a few bytes of a page
// whose other bytes the guest uses as plain CPU data (counters copied from GDS next to the
// objects that own them). A read fault whose exact access misses every GPU written byte of the
// page needs no readback: the instruction is single-stepped with its base register pointing into
// the unprotected backing view of the same memory. Any other fault takes the readback.
// Opt-in: DCPC_READBACK_REDIRECT=1.
namespace {

struct RedirectState {
    bool active{};
    bool restore{};
    ZydisRegister base{};
    u64 delta{};
};
thread_local RedirectState redirect_state{};

u32 redirect_mode = 0;
std::atomic<u64> redirect_counts[2]{};

constexpr u32 TRAP_FLAG = 0x100;

u64* GprSlot(CONTEXT* ctx, ZydisRegister reg) {
    switch (reg) {
    case ZYDIS_REGISTER_RAX:
        return &ctx->Rax;
    case ZYDIS_REGISTER_RBX:
        return &ctx->Rbx;
    case ZYDIS_REGISTER_RCX:
        return &ctx->Rcx;
    case ZYDIS_REGISTER_RDX:
        return &ctx->Rdx;
    case ZYDIS_REGISTER_RSI:
        return &ctx->Rsi;
    case ZYDIS_REGISTER_RDI:
        return &ctx->Rdi;
    case ZYDIS_REGISTER_RBP:
        return &ctx->Rbp;
    case ZYDIS_REGISTER_R8:
        return &ctx->R8;
    case ZYDIS_REGISTER_R9:
        return &ctx->R9;
    case ZYDIS_REGISTER_R10:
        return &ctx->R10;
    case ZYDIS_REGISTER_R11:
        return &ctx->R11;
    case ZYDIS_REGISTER_R12:
        return &ctx->R12;
    case ZYDIS_REGISTER_R13:
        return &ctx->R13;
    case ZYDIS_REGISTER_R14:
        return &ctx->R14;
    case ZYDIS_REGISTER_R15:
        return &ctx->R15;
    default:
        return nullptr;
    }
}

ZydisRegister Enclosing(ZydisRegister reg) {
    return ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, reg);
}

bool RedirectEnabled() {
    return redirect_mode != 0 && VideoCore::ReadbackFeaturesActive();
}

/// Returns true when the faulting read was set up to run against the backing view.
bool TryRedirectRead(Vulkan::Rasterizer* rasterizer, void* context, VAddr fault_addr) {
    auto* ctx = static_cast<EXCEPTION_POINTERS*>(context)->ContextRecord;
    ZydisDecodedInstruction inst;
    ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(Common::Decoder::Instance()->decodeInstruction(
            inst, ops, reinterpret_cast<void*>(ctx->Rip)))) {
        return false;
    }
    const ZydisDecodedOperand* mem = nullptr;
    for (u32 i = 0; i < inst.operand_count; ++i) {
        const auto& op = ops[i];
        if (op.type != ZYDIS_OPERAND_TYPE_MEMORY) {
            continue;
        }
        if (op.visibility == ZYDIS_OPERAND_VISIBILITY_EXPLICIT) {
            if (mem) {
                return false;
            }
            mem = &op;
        } else if (Enclosing(op.mem.base) != ZYDIS_REGISTER_RSP) {
            // Hidden memory operands other than the stack (string instructions) are not handled.
            return false;
        }
    }
    if (!mem || mem->mem.type != ZYDIS_MEMOP_TYPE_MEM ||
        (mem->actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) ||
        (mem->mem.segment != ZYDIS_REGISTER_DS && mem->mem.segment != ZYDIS_REGISTER_NONE)) {
        return false;
    }
    const ZydisRegister base = mem->mem.base;
    u64* const base_slot = GprSlot(ctx, base);
    if (!base_slot) {
        return false;
    }
    u64 address = *base_slot + static_cast<u64>(mem->mem.disp.value);
    if (mem->mem.index != ZYDIS_REGISTER_NONE) {
        const u64* index_slot = GprSlot(ctx, mem->mem.index);
        if (!index_slot || mem->mem.index == base) {
            return false;
        }
        address += *index_slot * mem->mem.scale;
    }
    const u64 size = mem->size / 8;
    if (size == 0 || size > 64 || fault_addr < address || fault_addr >= address + size ||
        (address >> 12) != ((address + size - 1) >> 12)) {
        return false;
    }
    // The base register must not feed the instruction otherwise; a full-width write of it ends
    // the redirection by itself.
    bool restore = true;
    for (u32 i = 0; i < inst.operand_count; ++i) {
        const auto& op = ops[i];
        if (&op == mem || op.type != ZYDIS_OPERAND_TYPE_REGISTER ||
            Enclosing(op.reg.value) != base) {
            continue;
        }
        if ((op.actions & (ZYDIS_OPERAND_ACTION_MASK_READ | ZYDIS_OPERAND_ACTION_CONDWRITE)) ||
            ZydisRegisterGetWidth(ZYDIS_MACHINE_MODE_LONG_64, op.reg.value) < 32) {
            return false;
        }
        restore = false;
    }
    if (!rasterizer->IsReadOfCpuDataOnGpuPage(address, size)) {
        redirect_counts[1].fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    Core::MemoryManager::BackingSpan span{};
    if (Core::Memory::Instance()->ResolveBackingSpans(address, size, &span, 1, false) != 1 ||
        span.size < size) {
        return false;
    }
    const u64 delta = reinterpret_cast<u64>(span.ptr) - address;
    *base_slot += delta;
    ctx->EFlags |= TRAP_FLAG;
    redirect_state = {.active = true, .restore = restore, .base = base, .delta = delta};
    redirect_counts[0].fetch_add(1, std::memory_order_relaxed);
    return true;
}

/// Ends a redirection after its instruction ran, or after it faulted again (then it re-runs
/// against guest memory).
bool EndRedirect(void* context, bool completed) {
    if (!redirect_state.active) {
        return false;
    }
    auto* ctx = static_cast<EXCEPTION_POINTERS*>(context)->ContextRecord;
    if (redirect_state.restore || !completed) {
        *GprSlot(ctx, redirect_state.base) -= redirect_state.delta;
    }
    ctx->EFlags &= ~TRAP_FLAG;
    redirect_state.active = false;
    return true;
}

bool SingleStepHandler(void* context) {
    return EndRedirect(context, true);
}

} // namespace
#endif

bool ReadbackFeaturesActive() {
    static const bool alternate = [] {
        const char* env = std::getenv("DCPC_READBACK_AB");
        const bool on = env && std::strtoul(env, nullptr, 10) != 0;
        if (on) {
            DriveclubPC::Profiler::SetPhase(1);
            LOG_INFO(Render, "GPU readback features alternate every 10 s (profiler phase 1 = on)");
        }
        return on;
    }();
    if (!alternate) {
        return true;
    }
    static std::atomic<s64> phase_start_ns{0};
    static std::atomic<bool> active{true};
    const s64 now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    s64 start = phase_start_ns.load(std::memory_order_relaxed);
    if (start == 0) {
        phase_start_ns.compare_exchange_strong(start, now);
    } else if (now - start >= 10'000'000'000 &&
               phase_start_ns.compare_exchange_strong(start, now)) {
        const bool next = !active.load(std::memory_order_relaxed);
        active.store(next, std::memory_order_relaxed);
        DriveclubPC::Profiler::SetPhase(next ? 1 : 0);
    }
    return active.load(std::memory_order_relaxed);
}

struct SignalImpl : public PageManager::Impl {
    SignalImpl(Vulkan::Rasterizer* rasterizer_) : Impl() {
        rasterizer = rasterizer_;

        // Should be called first.
        constexpr auto priority = std::numeric_limits<u32>::min();
        Core::Signals::Instance()->RegisterAccessViolationHandler(GuestFaultSignalHandler,
                                                                  priority);
#ifdef _WIN32
        if (const char* env = std::getenv("DCPC_READBACK_REDIRECT")) {
            redirect_mode = static_cast<u32>(std::strtoul(env, nullptr, 10));
        }
        if (EmulatorSettings.GetReadbacksMode() != GpuReadbacksMode::Precise) {
            redirect_mode = 0;
        }
        if (redirect_mode != 0) {
            Core::Signals::Instance()->RegisterSingleStepHandler(SingleStepHandler);
            LOG_INFO(Render, "Read faults that miss GPU written bytes read the backing view");
        }
#endif
    }

    void Protect(VAddr address, size_t size, Core::MemoryPermission perms) override {
        RENDERER_TRACE;
        auto* memory = Core::Memory::Instance();
        auto& impl = memory->GetAddressSpace();
        ASSERT_MSG(perms != Core::MemoryPermission::Write,
                   "Attempted to protect region as write-only which is not a valid permission");
        // driveclub-pc: measure actual page-protection calls, not tracker bookkeeping.
        DriveclubPC::Profiler::CostScope cost{DriveclubPC::Profiler::Cost::Protect};
        impl.Protect(address, size, perms);
    }

    // driveclub-pc: fault statistics, logged every 10 s (write/read faults, on guest threads
    // vs the GPU command thread), to judge whether a larger unprotect window would pay.
    static void CountFault(bool write, bool gpu_thread) {
        static std::atomic<u64> counts[4]{};
        static std::atomic<s64> last_report_ns{0};
        counts[(write ? 0 : 2) + (gpu_thread ? 1 : 0)].fetch_add(1, std::memory_order_relaxed);
        const s64 now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
        s64 last = last_report_ns.load(std::memory_order_relaxed);
        if (last == 0) {
            last_report_ns.compare_exchange_strong(last, now);
            return;
        }
        if (now - last >= 10'000'000'000 &&
            last_report_ns.compare_exchange_strong(last, now)) {
            const double secs = static_cast<double>(now - last) / 1e9;
            LOG_INFO(Render, "Guest faults per second: write {:.0f} (GPU thread {:.0f}), read {:.0f} "
                             "(GPU thread {:.0f})",
                     counts[0].exchange(0) / secs, counts[1].exchange(0) / secs,
                     counts[2].exchange(0) / secs, counts[3].exchange(0) / secs);
#ifdef _WIN32
            LOG_INFO(Render,
                     "Read faults served from the backing view {:.0f}/s, needing GPU bytes "
                     "{:.0f}/s",
                     redirect_counts[0].exchange(0) / secs, redirect_counts[1].exchange(0) / secs);
#endif
        }
    }

    static bool GuestFaultSignalHandler(void* context, void* fault_address) {
        const auto addr = reinterpret_cast<VAddr>(fault_address);
        const auto size = std::min<u64>(8, PageManager::GetNextPageAddr(addr) - addr);
        const auto is_gpu_thread =
            std::this_thread::get_id() == rasterizer->GetGpuCommandProcessorThread();
        const u64 stats_start =
            DriveclubPC::GuestStats::Active() && !is_gpu_thread ? Common::FencedRDTSC() : 0;
        SCOPE_EXIT {
            if (stats_start != 0) {
                DriveclubPC::GuestStats::BookFault(Common::IsWriteError(context),
                                                   Common::FencedRDTSC() - stats_start);
            }
        };
#ifdef _WIN32
        if (EndRedirect(context, false)) {
            // The redirected instruction faulted again; it re-runs against guest memory.
            return true;
        }
#endif
        CountFault(Common::IsWriteError(context), is_gpu_thread);
#ifdef _WIN32
        if (!is_gpu_thread && !Common::IsWriteError(context) && RedirectEnabled() &&
            TryRedirectRead(rasterizer, context, addr)) {
            return true;
        }
#endif
        if (Common::IsWriteError(context)) {
            return rasterizer->InvalidateMemory(addr, size, is_gpu_thread);
        } else {
            if (!is_gpu_thread && DriveclubPC::DiagTables::Active()) {
                // driveclub-pc: which guest instruction reads GPU-written memory back.
                const u64 start = DriveclubPC::Profiler::NowNs();
                const bool handled = rasterizer->ReadMemory(addr, size, is_gpu_thread);
                DriveclubPC::DiagTables::NoteReadFault(
                    reinterpret_cast<u64>(Common::GetRip(context)), addr,
                    DriveclubPC::Profiler::NowNs() - start);
                return handled;
            }
            return rasterizer->ReadMemory(addr, size, is_gpu_thread);
        }
        return false;
    }
};

PageManager::PageManager(Vulkan::Rasterizer* rasterizer_) {
#ifdef __linux__
    if (EmulatorSettings.IsUserfaultfdTracking()) {
        // Write-protect faults only: the readback path needs the read faults the
        // signal implementation delivers, so this fork keeps signals.
        LOG_WARNING(Config, "userfaultfd tracking is not used: readbacks need read faults");
    }
    LOG_INFO(Config, "Memory tracking method: signals");
#endif
    impl = std::make_unique<SignalImpl>(rasterizer_);
}

PageManager::~PageManager() = default;

void PageManager::OnGpuMap(VAddr address, size_t size) {
    impl->OnMap(address, size);
}

void PageManager::OnGpuUnmap(VAddr address, size_t size) {
    impl->OnUnmap(address, size);
}

void PageManager::UpdatePageWatchers(VAddr addr, u64 size, PageOp write_op) const {
    impl->UpdatePageWatchers(addr, size, write_op);
}

void PageManager::UpdatePageWatchersForRegion(VAddr base_addr, const Bounds& bounds,
                                              const RegionBits& write_mask,
                                              const RegionBits& read_mask, PageOp write_op,
                                              PageOp read_op) const {
    impl->UpdatePageWatchersForRegion(base_addr, bounds, write_mask, read_mask, write_op, read_op);
}

} // namespace VideoCore
