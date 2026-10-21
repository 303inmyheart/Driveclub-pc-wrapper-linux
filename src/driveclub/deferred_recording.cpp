// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <fmt/format.h>
#include <immintrin.h>

#include "common/logging/log.h"
#include "common/thread.h"
#include "common/types.h"
#include "driveclub/deferred_recording.h"
#include "driveclub/frame_profiler.h"
#include "driveclub/recorder_wake.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#pragma comment(lib, "Synchronization.lib")
#endif

namespace DriveclubPC::DeferredRecording {

namespace {

using Dispatch = std::remove_cvref_t<decltype(VULKAN_HPP_DEFAULT_DISPATCHER)>;

/// The driver's entries, as the dispatcher held them before the thunks were installed.
Dispatch g_real{};
bool g_enabled = false;

/// Mode 2 alternates deferral on and off (A/B in one run); the frame profiler's "phase"
/// column records which half each frame ran in (1 = deferred).
bool g_alternate = false;
bool g_active = true;
std::chrono::seconds g_period{10};
std::chrono::steady_clock::time_point g_phase_start{};

enum class CmdId : u32 {
#define X(name) name,
#include "driveclub/vkcmd_list.inc"
#undef X
    Count,
};

constexpr const char* kCmdNames[] = {
#define X(name) #name,
#include "driveclub/vkcmd_list.inc"
#undef X
};

// ---------------------------------------------------------------------------------------------
// Command chunks: one producer at a time appends records, the recorder runs them in order.

constexpr size_t kChunkBytes = 1u << 20;
constexpr size_t kMaxClosure = 128;
constexpr size_t kMaxRecordData = kChunkBytes / 4; // larger commands run directly (sync)

struct Chunk {
    std::atomic<u32> committed{0};    ///< bytes the recorder may run
    std::atomic<Chunk*> next{nullptr}; ///< set once the producer moved on
    u32 used = 0;                      ///< producer side
    alignas(64) std::byte data[kChunkBytes];
};

struct Record {
    void (*run)(const void* closure, VkCommandBuffer cb);
    VkCommandBuffer cb;
    u32 size;    ///< whole record, header to closure end, 16-byte aligned
    u32 closure; ///< offset of the closure from the record start
};

constexpr size_t AlignUp(size_t value, size_t align) {
    return (value + align - 1) & ~(align - 1);
}

// Producer state. Producers are serialised by the callers' own command buffer synchronisation.
Chunk* g_write = nullptr;
std::atomic<u64> g_produced{0};
std::atomic<VkCommandBuffer> g_target0{VK_NULL_HANDLE};
std::atomic<VkCommandBuffer> g_target1{VK_NULL_HANDLE};

// Recorder state.
std::atomic<u64> g_done{0};
std::atomic<u32> g_sleeping{0};
#ifdef _WIN32
std::atomic<u32> g_wake_word{0};
#else
RecorderWake g_wake_word;
#endif
std::jthread g_recorder;

std::mutex g_free_mutex;
std::vector<Chunk*> g_free_chunks;

/// Producer-side statistics (one producer at a time).
struct Stats {
    u64 commands = 0;
    u64 oversize = 0;    ///< commands too large for a chunk, run directly
    u64 chunks_new = 0;
    std::array<u64, static_cast<size_t>(CmdId::Count)> syncs{};
    std::chrono::steady_clock::time_point last_report = std::chrono::steady_clock::now();
};
Stats g_stats;
// Drains also come from other threads (destroys), so their counters are atomic.
std::atomic<u64> g_drains{0}; ///< drains that had to wait
std::atomic<u64> g_drain_ns{0};
std::atomic<u64> g_recorder_busy_ns{0};

Chunk* AcquireChunk() {
    {
        std::scoped_lock lk{g_free_mutex};
        if (!g_free_chunks.empty()) {
            Chunk* chunk = g_free_chunks.back();
            g_free_chunks.pop_back();
            return chunk;
        }
    }
    ++g_stats.chunks_new;
    return new Chunk;
}

void RecycleChunk(Chunk* chunk) {
    chunk->committed.store(0, std::memory_order_relaxed);
    chunk->next.store(nullptr, std::memory_order_relaxed);
    chunk->used = 0;
    std::scoped_lock lk{g_free_mutex};
    g_free_chunks.push_back(chunk);
}

void WakeRecorder() {
#ifdef _WIN32
    g_wake_word.fetch_add(1, std::memory_order_seq_cst);
    WakeByAddressSingle(&g_wake_word);
#else
    g_wake_word.Notify();
#endif
}

/// Wakes the recorder if it went to sleep. The fence orders the caller's publication before
/// the read of the flag, pairing with the recorder's store of the flag before its last check.
void WakeIfSleeping() {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (g_sleeping.load(std::memory_order_relaxed) != 0) {
        WakeRecorder();
    }
}

bool Pending() {
    return g_done.load(std::memory_order_acquire) < g_produced.load(std::memory_order_acquire);
}

inline bool IsTarget(VkCommandBuffer cb) {
    return cb != VK_NULL_HANDLE && (cb == g_target0.load(std::memory_order_relaxed) ||
                                    cb == g_target1.load(std::memory_order_relaxed));
}

/// Builds one record: reserve, copy the pointed-to data, then commit the closure.
class Writer {
public:
    Writer(VkCommandBuffer cb, size_t data_bytes) : cb{cb} {
        const size_t need = sizeof(Record) + data_bytes + kMaxClosure + 64;
        if (data_bytes > kMaxRecordData) {
            ++g_stats.oversize;
            return;
        }
        if (g_write->used + need > kChunkBytes) {
            Chunk* next = AcquireChunk();
            // Everything in the old chunk is committed: the recorder moves on once it sees `next`.
            g_write->next.store(next, std::memory_order_release);
            g_write = next;
        }
        chunk = g_write;
        base = chunk->used;
        cursor = base + sizeof(Record);
    }

    [[nodiscard]] bool Ok() const {
        return chunk != nullptr;
    }

    template <typename T>
    const T* Copy(const T* src, size_t count) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (src == nullptr || count == 0) {
            return nullptr;
        }
        cursor = AlignUp(cursor, alignof(T) < 8 ? 8 : alignof(T));
        T* dst = reinterpret_cast<T*>(chunk->data + cursor);
        std::memcpy(dst, src, count * sizeof(T));
        cursor += count * sizeof(T);
        return dst;
    }

    const char* CopyString(const char* str) {
        return str ? Copy(str, std::strlen(str) + 1) : nullptr;
    }

    template <typename F>
    void Commit(F&& func) {
        using Closure = std::decay_t<F>;
        static_assert(sizeof(Closure) <= kMaxClosure, "deferred command closure too large");
        static_assert(std::is_trivially_destructible_v<Closure>);
        const size_t closure_at = AlignUp(cursor, 16);
        new (chunk->data + closure_at) Closure(std::forward<F>(func));
        const size_t end = AlignUp(closure_at + sizeof(Closure), 16);
        auto* record = new (chunk->data + base) Record{};
        record->run = [](const void* closure, VkCommandBuffer target) {
            (*static_cast<const Closure*>(closure))(target);
        };
        record->cb = cb;
        record->size = static_cast<u32>(end - base);
        record->closure = static_cast<u32>(closure_at - base);
        chunk->used = static_cast<u32>(end);
        chunk->committed.store(static_cast<u32>(end), std::memory_order_release);
        const u64 produced = g_produced.load(std::memory_order_relaxed) + 1;
        g_produced.store(produced, std::memory_order_release);
        ++g_stats.commands;
        // Waking costs a fence; check every 8th command (a missed wake-up is caught by the next
        // check, a drain or Kick()).
        if ((produced & 7) == 0) {
            WakeIfSleeping();
        }
    }

private:
    VkCommandBuffer cb;
    Chunk* chunk = nullptr;
    size_t base = 0;
    size_t cursor = 0;
};

/// Byte budget of the data a record copies.
struct Need {
    size_t bytes = 0;
    template <typename T>
    void Add(const T* src, size_t count) {
        if (src != nullptr) {
            bytes += count * sizeof(T) + 16;
        }
    }
};

void Sync(CmdId id) {
    ++g_stats.syncs[static_cast<size_t>(id)];
    Drain();
}

void MaybeReport() {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - g_stats.last_report;
    if (elapsed < std::chrono::seconds(10)) {
        return;
    }
    const double secs = std::chrono::duration<double>(elapsed).count();
    std::string syncs;
    for (size_t i = 0; i < g_stats.syncs.size(); ++i) {
        if (g_stats.syncs[i] != 0) {
            syncs += fmt::format(" {}={}", kCmdNames[i], g_stats.syncs[i]);
        }
    }
    const u64 busy_ns = g_recorder_busy_ns.exchange(0, std::memory_order_relaxed);
    const u64 drains = g_drains.exchange(0, std::memory_order_relaxed);
    const u64 drain_ns = g_drain_ns.exchange(0, std::memory_order_relaxed);
    LOG_INFO(Render_Vulkan,
             "Deferred recording: {:.0f} commands/s, drains {:.0f}/s waiting {:.2f} ms/s, "
             "recorder busy {:.1f} %, oversize {}, new chunks {}, sync:{}",
             g_stats.commands / secs, drains / secs, drain_ns / 1e6 / secs,
             100.0 * busy_ns / 1e9 / secs, g_stats.oversize, g_stats.chunks_new,
             syncs.empty() ? " none" : syncs);
    g_stats = Stats{};
    g_stats.last_report = now;
}

// ---------------------------------------------------------------------------------------------
// Recorder thread

bool RecorderHasWork(const Chunk* chunk, u32 offset) {
    return chunk->committed.load(std::memory_order_acquire) > offset ||
           chunk->next.load(std::memory_order_acquire) != nullptr;
}

void RecorderLoop(std::stop_token stop, Chunk* chunk) {
    Common::SetCurrentThreadName("dcpc:VkRecorder");
    u32 offset = 0;
    u64 done = 0;
    auto busy_since = std::chrono::steady_clock::now();
    bool busy = false;
    while (!stop.stop_requested()) {
        const u32 committed = chunk->committed.load(std::memory_order_acquire);
        if (offset < committed) {
            if (!busy) {
                busy = true;
                busy_since = std::chrono::steady_clock::now();
            }
            while (offset < committed) {
                const auto* record = reinterpret_cast<const Record*>(chunk->data + offset);
                record->run(chunk->data + offset + record->closure, record->cb);
                offset += record->size;
                g_done.store(++done, std::memory_order_release);
            }
            continue;
        }
        if (Chunk* next = chunk->next.load(std::memory_order_acquire)) {
            // `next` is published after the last commit of `chunk`: re-read before leaving it.
            if (chunk->committed.load(std::memory_order_acquire) != offset) {
                continue;
            }
            RecycleChunk(chunk);
            chunk = next;
            offset = 0;
            continue;
        }
        if (busy) {
            busy = false;
            g_recorder_busy_ns.fetch_add(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - busy_since)
                    .count(),
                std::memory_order_relaxed);
        }
        // Idle: spin briefly (draws arrive microseconds apart), then sleep.
        bool work = false;
        for (u32 spin = 0; spin < 4096 && !work; ++spin) {
            _mm_pause();
            work = RecorderHasWork(chunk, offset);
        }
        if (work) {
            continue;
        }
#ifdef _WIN32
        const u32 word = g_wake_word.load(std::memory_order_seq_cst);
#else
        const u32 word = g_wake_word.Snapshot();
#endif
        g_sleeping.store(1, std::memory_order_seq_cst);
        if (!stop.stop_requested() && !RecorderHasWork(chunk, offset)) {
#ifdef _WIN32
            WaitOnAddress(&g_wake_word, const_cast<u32*>(&word), sizeof(word), 50);
#else
            // Park until the producer publishes work, rather than polling after a 100 us
            // sleep. The producer increments the word before notifying, so a wake between
            // the work check and wait cannot be lost.
            g_wake_word.Wait(word, stop);
#endif
        }
        g_sleeping.store(0, std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------------------------------------
// Thunks

/// A value the recorder can use later: scalars and Vulkan handles. Handles of non-dispatchable
/// objects are pointers to incomplete `Vk*_T` structs on 64-bit targets; every pointer to data
/// in a vkCmd* signature points to const.
template <typename T>
constexpr bool kByValue =
    !std::is_pointer_v<T> || !std::is_const_v<std::remove_pointer_t<T>>;

/// Any vkCmd*: deferred when every argument is a value, otherwise synchronous.
template <auto Member, CmdId Id>
struct CmdThunk;

template <typename R, typename... A, R(VKAPI_PTR* Dispatch::*Member)(VkCommandBuffer, A...),
          CmdId Id>
struct CmdThunk<Member, Id> {
    static R VKAPI_CALL Call(VkCommandBuffer cb, A... args) {
        if (IsTarget(cb)) {
            if constexpr (std::is_void_v<R> && (kByValue<A> && ...)) {
                Writer writer{cb, 0};
                if (writer.Ok()) {
                    writer.Commit(
                        [args...](VkCommandBuffer target) { (g_real.*Member)(target, args...); });
                    return;
                }
            }
            Sync(Id);
        }
        return (g_real.*Member)(cb, args...);
    }
};

/// Device-level calls that must not overtake recorded commands.
template <auto Member>
struct DrainThunk;

template <typename R, typename... A, R(VKAPI_PTR* Dispatch::*Member)(A...)>
struct DrainThunk<Member> {
    static R VKAPI_CALL Call(A... args) {
        Drain();
        return (g_real.*Member)(args...);
    }
};

// Commands with pointer arguments that the rasterizer records per draw or per pass.

template <auto Member, CmdId Id>
void VKAPI_CALL CmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo* info) {
    if (IsTarget(cb) && info->pNext == nullptr) {
        bool chained = false;
        for (u32 i = 0; i < info->memoryBarrierCount; ++i) {
            chained |= info->pMemoryBarriers[i].pNext != nullptr;
        }
        for (u32 i = 0; i < info->bufferMemoryBarrierCount; ++i) {
            chained |= info->pBufferMemoryBarriers[i].pNext != nullptr;
        }
        for (u32 i = 0; i < info->imageMemoryBarrierCount; ++i) {
            chained |= info->pImageMemoryBarriers[i].pNext != nullptr;
        }
        Need need;
        need.Add(info, 1);
        need.Add(info->pMemoryBarriers, info->memoryBarrierCount);
        need.Add(info->pBufferMemoryBarriers, info->bufferMemoryBarrierCount);
        need.Add(info->pImageMemoryBarriers, info->imageMemoryBarrierCount);
        Writer writer{cb, need.bytes};
        if (!chained && writer.Ok()) {
            VkDependencyInfo copy = *info;
            copy.pMemoryBarriers = writer.Copy(info->pMemoryBarriers, info->memoryBarrierCount);
            copy.pBufferMemoryBarriers =
                writer.Copy(info->pBufferMemoryBarriers, info->bufferMemoryBarrierCount);
            copy.pImageMemoryBarriers =
                writer.Copy(info->pImageMemoryBarriers, info->imageMemoryBarrierCount);
            const VkDependencyInfo* stored = writer.Copy(&copy, 1);
            writer.Commit([stored](VkCommandBuffer t) { (g_real.*Member)(t, stored); });
            return;
        }
    }
    if (IsTarget(cb)) {
        Sync(Id);
    }
    (g_real.*Member)(cb, info);
}

void VKAPI_CALL CmdPipelineBarrier(VkCommandBuffer cb, VkPipelineStageFlags src,
                                   VkPipelineStageFlags dst, VkDependencyFlags flags,
                                   u32 num_mem, const VkMemoryBarrier* mem, u32 num_buf,
                                   const VkBufferMemoryBarrier* buf, u32 num_img,
                                   const VkImageMemoryBarrier* img) {
    if (IsTarget(cb)) {
        bool chained = false;
        for (u32 i = 0; i < num_mem; ++i) {
            chained |= mem[i].pNext != nullptr;
        }
        for (u32 i = 0; i < num_buf; ++i) {
            chained |= buf[i].pNext != nullptr;
        }
        for (u32 i = 0; i < num_img; ++i) {
            chained |= img[i].pNext != nullptr;
        }
        Need need;
        need.Add(mem, num_mem);
        need.Add(buf, num_buf);
        need.Add(img, num_img);
        Writer writer{cb, need.bytes};
        if (!chained && writer.Ok()) {
            const auto* m = writer.Copy(mem, num_mem);
            const auto* b = writer.Copy(buf, num_buf);
            const auto* i = writer.Copy(img, num_img);
            writer.Commit([=](VkCommandBuffer t) {
                g_real.vkCmdPipelineBarrier(t, src, dst, flags, num_mem, m, num_buf, b, num_img, i);
            });
            return;
        }
        Sync(CmdId::vkCmdPipelineBarrier);
    }
    g_real.vkCmdPipelineBarrier(cb, src, dst, flags, num_mem, mem, num_buf, buf, num_img, img);
}

template <auto Member, CmdId Id>
void VKAPI_CALL CmdPushDescriptorSet(VkCommandBuffer cb, VkPipelineBindPoint point,
                                     VkPipelineLayout layout, u32 set, u32 count,
                                     const VkWriteDescriptorSet* writes) {
    if (IsTarget(cb)) {
        bool chained = false;
        Need need;
        need.Add(writes, count);
        for (u32 i = 0; i < count; ++i) {
            chained |= writes[i].pNext != nullptr;
            need.Add(writes[i].pImageInfo, writes[i].descriptorCount);
            need.Add(writes[i].pBufferInfo, writes[i].descriptorCount);
            need.Add(writes[i].pTexelBufferView, writes[i].descriptorCount);
        }
        Writer writer{cb, need.bytes};
        if (!chained && writer.Ok()) {
            VkWriteDescriptorSet* copies =
                const_cast<VkWriteDescriptorSet*>(writer.Copy(writes, count));
            for (u32 i = 0; i < count; ++i) {
                copies[i].pImageInfo = writer.Copy(writes[i].pImageInfo, writes[i].descriptorCount);
                copies[i].pBufferInfo =
                    writer.Copy(writes[i].pBufferInfo, writes[i].descriptorCount);
                copies[i].pTexelBufferView =
                    writer.Copy(writes[i].pTexelBufferView, writes[i].descriptorCount);
            }
            const VkWriteDescriptorSet* stored = copies;
            writer.Commit([=](VkCommandBuffer t) {
                (g_real.*Member)(t, point, layout, set, count, stored);
            });
            return;
        }
        Sync(Id);
    }
    (g_real.*Member)(cb, point, layout, set, count, writes);
}

void VKAPI_CALL CmdPushConstants(VkCommandBuffer cb, VkPipelineLayout layout,
                                 VkShaderStageFlags stages, u32 offset, u32 size,
                                 const void* values) {
    if (IsTarget(cb)) {
        Writer writer{cb, size + 16u};
        if (writer.Ok()) {
            const auto* data = writer.Copy(static_cast<const std::byte*>(values), size);
            writer.Commit([=](VkCommandBuffer t) {
                g_real.vkCmdPushConstants(t, layout, stages, offset, size, data);
            });
            return;
        }
        Sync(CmdId::vkCmdPushConstants);
    }
    g_real.vkCmdPushConstants(cb, layout, stages, offset, size, values);
}

template <auto Member, CmdId Id>
void VKAPI_CALL CmdPushConstants2(VkCommandBuffer cb, const VkPushConstantsInfo* info) {
    if (IsTarget(cb)) {
        Writer writer{cb, sizeof(VkPushConstantsInfo) + info->size + 32u};
        if (info->pNext == nullptr && writer.Ok()) {
            VkPushConstantsInfo copy = *info;
            copy.pValues = writer.Copy(static_cast<const std::byte*>(info->pValues), info->size);
            const VkPushConstantsInfo* stored = writer.Copy(&copy, 1);
            writer.Commit([stored](VkCommandBuffer t) { (g_real.*Member)(t, stored); });
            return;
        }
        Sync(Id);
    }
    (g_real.*Member)(cb, info);
}

/// Deep copy of descriptor writes; null when one carries a pNext chain.
const VkWriteDescriptorSet* CopyWrites(Writer& writer, const VkWriteDescriptorSet* writes,
                                       u32 count) {
    for (u32 i = 0; i < count; ++i) {
        if (writes[i].pNext != nullptr) {
            return nullptr;
        }
    }
    auto* copies = const_cast<VkWriteDescriptorSet*>(writer.Copy(writes, count));
    for (u32 i = 0; i < count; ++i) {
        copies[i].pImageInfo = writer.Copy(writes[i].pImageInfo, writes[i].descriptorCount);
        copies[i].pBufferInfo = writer.Copy(writes[i].pBufferInfo, writes[i].descriptorCount);
        copies[i].pTexelBufferView =
            writer.Copy(writes[i].pTexelBufferView, writes[i].descriptorCount);
    }
    return copies;
}

void AddWrites(Need& need, const VkWriteDescriptorSet* writes, u32 count) {
    need.Add(writes, count);
    for (u32 i = 0; i < count; ++i) {
        need.Add(writes[i].pImageInfo, writes[i].descriptorCount);
        need.Add(writes[i].pBufferInfo, writes[i].descriptorCount);
        need.Add(writes[i].pTexelBufferView, writes[i].descriptorCount);
    }
}

template <auto Member, CmdId Id>
void VKAPI_CALL CmdPushDescriptorSet2(VkCommandBuffer cb, const VkPushDescriptorSetInfo* info) {
    if (IsTarget(cb)) {
        Need need;
        need.Add(info, 1);
        AddWrites(need, info->pDescriptorWrites, info->descriptorWriteCount);
        Writer writer{cb, need.bytes};
        if (info->pNext == nullptr && writer.Ok()) {
            if (const auto* writes =
                    CopyWrites(writer, info->pDescriptorWrites, info->descriptorWriteCount)) {
                VkPushDescriptorSetInfo copy = *info;
                copy.pDescriptorWrites = writes;
                const VkPushDescriptorSetInfo* stored = writer.Copy(&copy, 1);
                writer.Commit([stored](VkCommandBuffer t) { (g_real.*Member)(t, stored); });
                return;
            }
        }
        Sync(Id);
    }
    (g_real.*Member)(cb, info);
}

template <auto Member, CmdId Id>
void VKAPI_CALL CmdBindDescriptorSets2(VkCommandBuffer cb, const VkBindDescriptorSetsInfo* info) {
    if (IsTarget(cb)) {
        Need need;
        need.Add(info, 1);
        need.Add(info->pDescriptorSets, info->descriptorSetCount);
        need.Add(info->pDynamicOffsets, info->dynamicOffsetCount);
        Writer writer{cb, need.bytes};
        if (info->pNext == nullptr && writer.Ok()) {
            VkBindDescriptorSetsInfo copy = *info;
            copy.pDescriptorSets = writer.Copy(info->pDescriptorSets, info->descriptorSetCount);
            copy.pDynamicOffsets = writer.Copy(info->pDynamicOffsets, info->dynamicOffsetCount);
            const VkBindDescriptorSetsInfo* stored = writer.Copy(&copy, 1);
            writer.Commit([stored](VkCommandBuffer t) { (g_real.*Member)(t, stored); });
            return;
        }
        Sync(Id);
    }
    (g_real.*Member)(cb, info);
}

void VKAPI_CALL CmdUpdateBuffer(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset,
                                VkDeviceSize size, const void* data) {
    if (IsTarget(cb)) {
        Writer writer{cb, static_cast<size_t>(size) + 16};
        if (writer.Ok()) {
            const auto* copy = writer.Copy(static_cast<const std::byte*>(data), size);
            writer.Commit([=](VkCommandBuffer t) {
                g_real.vkCmdUpdateBuffer(t, buffer, offset, size, copy);
            });
            return;
        }
        Sync(CmdId::vkCmdUpdateBuffer);
    }
    g_real.vkCmdUpdateBuffer(cb, buffer, offset, size, data);
}

template <auto Member, CmdId Id>
void VKAPI_CALL CmdBeginRendering(VkCommandBuffer cb, const VkRenderingInfo* info) {
    if (IsTarget(cb)) {
        bool chained = info->pNext != nullptr ||
                       (info->pDepthAttachment && info->pDepthAttachment->pNext) ||
                       (info->pStencilAttachment && info->pStencilAttachment->pNext);
        for (u32 i = 0; i < info->colorAttachmentCount; ++i) {
            chained |= info->pColorAttachments[i].pNext != nullptr;
        }
        Need need;
        need.Add(info, 1);
        need.Add(info->pColorAttachments, info->colorAttachmentCount);
        need.Add(info->pDepthAttachment, 1);
        need.Add(info->pStencilAttachment, 1);
        Writer writer{cb, need.bytes};
        if (!chained && writer.Ok()) {
            VkRenderingInfo copy = *info;
            copy.pColorAttachments =
                writer.Copy(info->pColorAttachments, info->colorAttachmentCount);
            copy.pDepthAttachment = writer.Copy(info->pDepthAttachment, 1);
            copy.pStencilAttachment = writer.Copy(info->pStencilAttachment, 1);
            const VkRenderingInfo* stored = writer.Copy(&copy, 1);
            writer.Commit([stored](VkCommandBuffer t) { (g_real.*Member)(t, stored); });
            return;
        }
        Sync(Id);
    }
    (g_real.*Member)(cb, info);
}

/// (cb, u32 first_or_count..., n, const T* array) shapes: viewports, scissors, color masks.
template <auto Member, CmdId Id, typename T>
void VKAPI_CALL CmdFirstCountArray(VkCommandBuffer cb, u32 first, u32 count, const T* items) {
    if (IsTarget(cb)) {
        Need need;
        need.Add(items, count);
        Writer writer{cb, need.bytes};
        if (writer.Ok()) {
            const T* stored = writer.Copy(items, count);
            writer.Commit(
                [=](VkCommandBuffer t) { (g_real.*Member)(t, first, count, stored); });
            return;
        }
        Sync(Id);
    }
    (g_real.*Member)(cb, first, count, items);
}

template <auto Member, CmdId Id, typename T>
void VKAPI_CALL CmdCountArray(VkCommandBuffer cb, u32 count, const T* items) {
    if (IsTarget(cb)) {
        Need need;
        need.Add(items, count);
        Writer writer{cb, need.bytes};
        if (writer.Ok()) {
            const T* stored = writer.Copy(items, count);
            writer.Commit([=](VkCommandBuffer t) { (g_real.*Member)(t, count, stored); });
            return;
        }
        Sync(Id);
    }
    (g_real.*Member)(cb, count, items);
}

void VKAPI_CALL CmdSetBlendConstants(VkCommandBuffer cb, const float constants[4]) {
    if (IsTarget(cb)) {
        Writer writer{cb, 32};
        if (writer.Ok()) {
            const float* stored = writer.Copy(constants, 4);
            writer.Commit([=](VkCommandBuffer t) { g_real.vkCmdSetBlendConstants(t, stored); });
            return;
        }
        Sync(CmdId::vkCmdSetBlendConstants);
    }
    g_real.vkCmdSetBlendConstants(cb, constants);
}

void VKAPI_CALL CmdBindVertexBuffers(VkCommandBuffer cb, u32 first, u32 count,
                                     const VkBuffer* buffers, const VkDeviceSize* offsets) {
    if (IsTarget(cb)) {
        Need need;
        need.Add(buffers, count);
        need.Add(offsets, count);
        Writer writer{cb, need.bytes};
        if (writer.Ok()) {
            const auto* b = writer.Copy(buffers, count);
            const auto* o = writer.Copy(offsets, count);
            writer.Commit(
                [=](VkCommandBuffer t) { g_real.vkCmdBindVertexBuffers(t, first, count, b, o); });
            return;
        }
        Sync(CmdId::vkCmdBindVertexBuffers);
    }
    g_real.vkCmdBindVertexBuffers(cb, first, count, buffers, offsets);
}

template <auto Member, CmdId Id>
void VKAPI_CALL CmdBindVertexBuffers2(VkCommandBuffer cb, u32 first, u32 count,
                                      const VkBuffer* buffers, const VkDeviceSize* offsets,
                                      const VkDeviceSize* sizes, const VkDeviceSize* strides) {
    if (IsTarget(cb)) {
        Need need;
        need.Add(buffers, count);
        need.Add(offsets, count);
        need.Add(sizes, count);
        need.Add(strides, count);
        Writer writer{cb, need.bytes};
        if (writer.Ok()) {
            const auto* b = writer.Copy(buffers, count);
            const auto* o = writer.Copy(offsets, count);
            const auto* s = writer.Copy(sizes, count);
            const auto* st = writer.Copy(strides, count);
            writer.Commit(
                [=](VkCommandBuffer t) { (g_real.*Member)(t, first, count, b, o, s, st); });
            return;
        }
        Sync(Id);
    }
    (g_real.*Member)(cb, first, count, buffers, offsets, sizes, strides);
}

void VKAPI_CALL CmdBindDescriptorSets(VkCommandBuffer cb, VkPipelineBindPoint point,
                                      VkPipelineLayout layout, u32 first, u32 count,
                                      const VkDescriptorSet* sets, u32 num_dynamic,
                                      const u32* dynamic_offsets) {
    if (IsTarget(cb)) {
        Need need;
        need.Add(sets, count);
        need.Add(dynamic_offsets, num_dynamic);
        Writer writer{cb, need.bytes};
        if (writer.Ok()) {
            const auto* s = writer.Copy(sets, count);
            const auto* d = writer.Copy(dynamic_offsets, num_dynamic);
            writer.Commit([=](VkCommandBuffer t) {
                g_real.vkCmdBindDescriptorSets(t, point, layout, first, count, s, num_dynamic, d);
            });
            return;
        }
        Sync(CmdId::vkCmdBindDescriptorSets);
    }
    g_real.vkCmdBindDescriptorSets(cb, point, layout, first, count, sets, num_dynamic,
                                   dynamic_offsets);
}

void VKAPI_CALL CmdSetVertexInputEXT(VkCommandBuffer cb, u32 num_bindings,
                                     const VkVertexInputBindingDescription2EXT* bindings,
                                     u32 num_attributes,
                                     const VkVertexInputAttributeDescription2EXT* attributes) {
    if (IsTarget(cb)) {
        bool chained = false;
        for (u32 i = 0; i < num_bindings; ++i) {
            chained |= bindings[i].pNext != nullptr;
        }
        for (u32 i = 0; i < num_attributes; ++i) {
            chained |= attributes[i].pNext != nullptr;
        }
        Need need;
        need.Add(bindings, num_bindings);
        need.Add(attributes, num_attributes);
        Writer writer{cb, need.bytes};
        if (!chained && writer.Ok()) {
            const auto* b = writer.Copy(bindings, num_bindings);
            const auto* a = writer.Copy(attributes, num_attributes);
            writer.Commit([=](VkCommandBuffer t) {
                g_real.vkCmdSetVertexInputEXT(t, num_bindings, b, num_attributes, a);
            });
            return;
        }
        Sync(CmdId::vkCmdSetVertexInputEXT);
    }
    g_real.vkCmdSetVertexInputEXT(cb, num_bindings, bindings, num_attributes, attributes);
}

void VKAPI_CALL CmdCopyBuffer(VkCommandBuffer cb, VkBuffer src, VkBuffer dst, u32 count,
                              const VkBufferCopy* regions) {
    if (IsTarget(cb)) {
        Need need;
        need.Add(regions, count);
        Writer writer{cb, need.bytes};
        if (writer.Ok()) {
            const auto* r = writer.Copy(regions, count);
            writer.Commit([=](VkCommandBuffer t) { g_real.vkCmdCopyBuffer(t, src, dst, count, r); });
            return;
        }
        Sync(CmdId::vkCmdCopyBuffer);
    }
    g_real.vkCmdCopyBuffer(cb, src, dst, count, regions);
}

void VKAPI_CALL CmdCopyBufferToImage(VkCommandBuffer cb, VkBuffer src, VkImage dst,
                                     VkImageLayout layout, u32 count,
                                     const VkBufferImageCopy* regions) {
    if (IsTarget(cb)) {
        Need need;
        need.Add(regions, count);
        Writer writer{cb, need.bytes};
        if (writer.Ok()) {
            const auto* r = writer.Copy(regions, count);
            writer.Commit([=](VkCommandBuffer t) {
                g_real.vkCmdCopyBufferToImage(t, src, dst, layout, count, r);
            });
            return;
        }
        Sync(CmdId::vkCmdCopyBufferToImage);
    }
    g_real.vkCmdCopyBufferToImage(cb, src, dst, layout, count, regions);
}

void VKAPI_CALL CmdCopyImageToBuffer(VkCommandBuffer cb, VkImage src, VkImageLayout layout,
                                     VkBuffer dst, u32 count, const VkBufferImageCopy* regions) {
    if (IsTarget(cb)) {
        Need need;
        need.Add(regions, count);
        Writer writer{cb, need.bytes};
        if (writer.Ok()) {
            const auto* r = writer.Copy(regions, count);
            writer.Commit([=](VkCommandBuffer t) {
                g_real.vkCmdCopyImageToBuffer(t, src, layout, dst, count, r);
            });
            return;
        }
        Sync(CmdId::vkCmdCopyImageToBuffer);
    }
    g_real.vkCmdCopyImageToBuffer(cb, src, layout, dst, count, regions);
}

void VKAPI_CALL CmdCopyImage(VkCommandBuffer cb, VkImage src, VkImageLayout src_layout,
                             VkImage dst, VkImageLayout dst_layout, u32 count,
                             const VkImageCopy* regions) {
    if (IsTarget(cb)) {
        Need need;
        need.Add(regions, count);
        Writer writer{cb, need.bytes};
        if (writer.Ok()) {
            const auto* r = writer.Copy(regions, count);
            writer.Commit([=](VkCommandBuffer t) {
                g_real.vkCmdCopyImage(t, src, src_layout, dst, dst_layout, count, r);
            });
            return;
        }
        Sync(CmdId::vkCmdCopyImage);
    }
    g_real.vkCmdCopyImage(cb, src, src_layout, dst, dst_layout, count, regions);
}

void VKAPI_CALL CmdResolveImage(VkCommandBuffer cb, VkImage src, VkImageLayout src_layout,
                                VkImage dst, VkImageLayout dst_layout, u32 count,
                                const VkImageResolve* regions) {
    if (IsTarget(cb)) {
        Need need;
        need.Add(regions, count);
        Writer writer{cb, need.bytes};
        if (writer.Ok()) {
            const auto* r = writer.Copy(regions, count);
            writer.Commit([=](VkCommandBuffer t) {
                g_real.vkCmdResolveImage(t, src, src_layout, dst, dst_layout, count, r);
            });
            return;
        }
        Sync(CmdId::vkCmdResolveImage);
    }
    g_real.vkCmdResolveImage(cb, src, src_layout, dst, dst_layout, count, regions);
}

void VKAPI_CALL CmdClearColorImage(VkCommandBuffer cb, VkImage image, VkImageLayout layout,
                                   const VkClearColorValue* color, u32 count,
                                   const VkImageSubresourceRange* ranges) {
    if (IsTarget(cb)) {
        Need need;
        need.Add(color, 1);
        need.Add(ranges, count);
        Writer writer{cb, need.bytes};
        if (writer.Ok()) {
            const auto* c = writer.Copy(color, 1);
            const auto* r = writer.Copy(ranges, count);
            writer.Commit([=](VkCommandBuffer t) {
                g_real.vkCmdClearColorImage(t, image, layout, c, count, r);
            });
            return;
        }
        Sync(CmdId::vkCmdClearColorImage);
    }
    g_real.vkCmdClearColorImage(cb, image, layout, color, count, ranges);
}

template <auto Member, CmdId Id>
void VKAPI_CALL CmdDebugLabel(VkCommandBuffer cb, const VkDebugUtilsLabelEXT* label) {
    if (IsTarget(cb)) {
        const size_t name_len = label->pLabelName ? std::strlen(label->pLabelName) + 1 : 0;
        Writer writer{cb, sizeof(VkDebugUtilsLabelEXT) + name_len + 32};
        if (label->pNext == nullptr && writer.Ok()) {
            VkDebugUtilsLabelEXT copy = *label;
            copy.pLabelName = writer.CopyString(label->pLabelName);
            const VkDebugUtilsLabelEXT* stored = writer.Copy(&copy, 1);
            writer.Commit([stored](VkCommandBuffer t) { (g_real.*Member)(t, stored); });
            return;
        }
        Sync(Id);
    }
    (g_real.*Member)(cb, label);
}

// ---------------------------------------------------------------------------------------------
// Installation

template <auto Member, CmdId Id>
void InstallGeneric(Dispatch& d) {
    if (d.*Member != nullptr) {
        d.*Member = &CmdThunk<Member, Id>::Call;
    }
}

template <auto Member, typename Fn>
void InstallCustom(Dispatch& d, Fn* thunk) {
    if (d.*Member != nullptr) {
        d.*Member = thunk;
    }
}

template <auto Member>
void InstallDrain(Dispatch& d) {
    if (d.*Member != nullptr) {
        d.*Member = &DrainThunk<Member>::Call;
    }
}

} // namespace

bool Install() {
    const char* env = std::getenv("DCPC_DEFERRED_RECORDING");
    const int mode = env != nullptr ? std::atoi(env) : 0;
    g_enabled = mode != 0;
    g_alternate = mode == 2;
    if (const char* period = std::getenv("DCPC_DEFERRED_RECORDING_PERIOD_S")) {
        g_period = std::chrono::seconds(std::max(1, std::atoi(period)));
    }
    LOG_INFO(Render_Vulkan, "Deferred command recording: {}",
             !g_enabled ? "off"
             : g_alternate
                 ? fmt::format("alternating every {} s (A/B, profiler column phase)",
                               g_period.count())
                 : std::string("on"));
    if (!g_enabled) {
        return false;
    }
    Profiler::SetPhase(1);
    g_phase_start = std::chrono::steady_clock::now();
    auto& d = VULKAN_HPP_DEFAULT_DISPATCHER;
    g_real = d;

    // Every vkCmd*: deferred by value, or synchronous when it takes pointers.
#define X(name) InstallGeneric<&Dispatch::name, CmdId::name>(d);
#include "driveclub/vkcmd_list.inc"
#undef X

    // Pointer-taking commands the rasterizer uses: deferred with deep copies.
    InstallCustom<&Dispatch::vkCmdPipelineBarrier2>(
        d, &CmdPipelineBarrier2<&Dispatch::vkCmdPipelineBarrier2, CmdId::vkCmdPipelineBarrier2>);
    InstallCustom<&Dispatch::vkCmdPipelineBarrier2KHR>(
        d, &CmdPipelineBarrier2<&Dispatch::vkCmdPipelineBarrier2KHR,
                                CmdId::vkCmdPipelineBarrier2KHR>);
    InstallCustom<&Dispatch::vkCmdPipelineBarrier>(d, &CmdPipelineBarrier);
    InstallCustom<&Dispatch::vkCmdPushDescriptorSetKHR>(
        d, &CmdPushDescriptorSet<&Dispatch::vkCmdPushDescriptorSetKHR,
                                 CmdId::vkCmdPushDescriptorSetKHR>);
    InstallCustom<&Dispatch::vkCmdPushDescriptorSet>(
        d, &CmdPushDescriptorSet<&Dispatch::vkCmdPushDescriptorSet,
                                 CmdId::vkCmdPushDescriptorSet>);
    InstallCustom<&Dispatch::vkCmdPushConstants>(d, &CmdPushConstants);
    InstallCustom<&Dispatch::vkCmdPushConstants2>(
        d, &CmdPushConstants2<&Dispatch::vkCmdPushConstants2, CmdId::vkCmdPushConstants2>);
    InstallCustom<&Dispatch::vkCmdPushConstants2KHR>(
        d, &CmdPushConstants2<&Dispatch::vkCmdPushConstants2KHR, CmdId::vkCmdPushConstants2KHR>);
    InstallCustom<&Dispatch::vkCmdPushDescriptorSet2>(
        d, &CmdPushDescriptorSet2<&Dispatch::vkCmdPushDescriptorSet2,
                                  CmdId::vkCmdPushDescriptorSet2>);
    InstallCustom<&Dispatch::vkCmdPushDescriptorSet2KHR>(
        d, &CmdPushDescriptorSet2<&Dispatch::vkCmdPushDescriptorSet2KHR,
                                  CmdId::vkCmdPushDescriptorSet2KHR>);
    InstallCustom<&Dispatch::vkCmdBindDescriptorSets2>(
        d, &CmdBindDescriptorSets2<&Dispatch::vkCmdBindDescriptorSets2,
                                   CmdId::vkCmdBindDescriptorSets2>);
    InstallCustom<&Dispatch::vkCmdBindDescriptorSets2KHR>(
        d, &CmdBindDescriptorSets2<&Dispatch::vkCmdBindDescriptorSets2KHR,
                                   CmdId::vkCmdBindDescriptorSets2KHR>);
    InstallCustom<&Dispatch::vkCmdUpdateBuffer>(d, &CmdUpdateBuffer);
    InstallCustom<&Dispatch::vkCmdBeginRendering>(
        d, &CmdBeginRendering<&Dispatch::vkCmdBeginRendering, CmdId::vkCmdBeginRendering>);
    InstallCustom<&Dispatch::vkCmdBeginRenderingKHR>(
        d, &CmdBeginRendering<&Dispatch::vkCmdBeginRenderingKHR, CmdId::vkCmdBeginRenderingKHR>);
    InstallCustom<&Dispatch::vkCmdSetViewport>(
        d, &CmdFirstCountArray<&Dispatch::vkCmdSetViewport, CmdId::vkCmdSetViewport, VkViewport>);
    InstallCustom<&Dispatch::vkCmdSetScissor>(
        d, &CmdFirstCountArray<&Dispatch::vkCmdSetScissor, CmdId::vkCmdSetScissor, VkRect2D>);
    InstallCustom<&Dispatch::vkCmdSetColorWriteMaskEXT>(
        d, &CmdFirstCountArray<&Dispatch::vkCmdSetColorWriteMaskEXT,
                               CmdId::vkCmdSetColorWriteMaskEXT, VkColorComponentFlags>);
    InstallCustom<&Dispatch::vkCmdSetViewportWithCount>(
        d, &CmdCountArray<&Dispatch::vkCmdSetViewportWithCount, CmdId::vkCmdSetViewportWithCount,
                          VkViewport>);
    InstallCustom<&Dispatch::vkCmdSetViewportWithCountEXT>(
        d, &CmdCountArray<&Dispatch::vkCmdSetViewportWithCountEXT,
                          CmdId::vkCmdSetViewportWithCountEXT, VkViewport>);
    InstallCustom<&Dispatch::vkCmdSetScissorWithCount>(
        d, &CmdCountArray<&Dispatch::vkCmdSetScissorWithCount, CmdId::vkCmdSetScissorWithCount,
                          VkRect2D>);
    InstallCustom<&Dispatch::vkCmdSetScissorWithCountEXT>(
        d, &CmdCountArray<&Dispatch::vkCmdSetScissorWithCountEXT,
                          CmdId::vkCmdSetScissorWithCountEXT, VkRect2D>);
    InstallCustom<&Dispatch::vkCmdSetBlendConstants>(d, &CmdSetBlendConstants);
    InstallCustom<&Dispatch::vkCmdBindVertexBuffers>(d, &CmdBindVertexBuffers);
    InstallCustom<&Dispatch::vkCmdBindVertexBuffers2>(
        d, &CmdBindVertexBuffers2<&Dispatch::vkCmdBindVertexBuffers2,
                                  CmdId::vkCmdBindVertexBuffers2>);
    InstallCustom<&Dispatch::vkCmdBindVertexBuffers2EXT>(
        d, &CmdBindVertexBuffers2<&Dispatch::vkCmdBindVertexBuffers2EXT,
                                  CmdId::vkCmdBindVertexBuffers2EXT>);
    InstallCustom<&Dispatch::vkCmdBindDescriptorSets>(d, &CmdBindDescriptorSets);
    InstallCustom<&Dispatch::vkCmdSetVertexInputEXT>(d, &CmdSetVertexInputEXT);
    InstallCustom<&Dispatch::vkCmdCopyBuffer>(d, &CmdCopyBuffer);
    InstallCustom<&Dispatch::vkCmdCopyBufferToImage>(d, &CmdCopyBufferToImage);
    InstallCustom<&Dispatch::vkCmdCopyImageToBuffer>(d, &CmdCopyImageToBuffer);
    InstallCustom<&Dispatch::vkCmdCopyImage>(d, &CmdCopyImage);
    InstallCustom<&Dispatch::vkCmdResolveImage>(d, &CmdResolveImage);
    InstallCustom<&Dispatch::vkCmdClearColorImage>(d, &CmdClearColorImage);
    InstallCustom<&Dispatch::vkCmdBeginDebugUtilsLabelEXT>(
        d, &CmdDebugLabel<&Dispatch::vkCmdBeginDebugUtilsLabelEXT,
                          CmdId::vkCmdBeginDebugUtilsLabelEXT>);
    InstallCustom<&Dispatch::vkCmdInsertDebugUtilsLabelEXT>(
        d, &CmdDebugLabel<&Dispatch::vkCmdInsertDebugUtilsLabelEXT,
                          CmdId::vkCmdInsertDebugUtilsLabelEXT>);

    // Calls that must not overtake recorded commands.
    InstallDrain<&Dispatch::vkBeginCommandBuffer>(d);
    InstallDrain<&Dispatch::vkEndCommandBuffer>(d);
    InstallDrain<&Dispatch::vkResetCommandBuffer>(d);
    InstallDrain<&Dispatch::vkResetCommandPool>(d);
    InstallDrain<&Dispatch::vkAllocateCommandBuffers>(d);
    InstallDrain<&Dispatch::vkFreeCommandBuffers>(d);
    InstallDrain<&Dispatch::vkDestroyCommandPool>(d);
    InstallDrain<&Dispatch::vkTrimCommandPool>(d);
    InstallDrain<&Dispatch::vkResetDescriptorPool>(d);
    InstallDrain<&Dispatch::vkDestroyDescriptorPool>(d);
    InstallDrain<&Dispatch::vkFreeDescriptorSets>(d);
    InstallDrain<&Dispatch::vkDestroyBuffer>(d);
    InstallDrain<&Dispatch::vkDestroyBufferView>(d);
    InstallDrain<&Dispatch::vkDestroyImage>(d);
    InstallDrain<&Dispatch::vkDestroyImageView>(d);
    InstallDrain<&Dispatch::vkDestroySampler>(d);
    InstallDrain<&Dispatch::vkDestroyPipeline>(d);
    InstallDrain<&Dispatch::vkDestroyPipelineLayout>(d);
    InstallDrain<&Dispatch::vkDestroyDescriptorSetLayout>(d);
    InstallDrain<&Dispatch::vkDestroyQueryPool>(d);
    InstallDrain<&Dispatch::vkDestroyEvent>(d);

    g_write = new Chunk;
    g_recorder = std::jthread([first = g_write](std::stop_token stop) { RecorderLoop(stop, first); });
    return true;
}

bool Enabled() {
    return g_enabled;
}

void SetTargets(VkCommandBuffer primary, VkCommandBuffer upload) {
    if (!g_enabled) {
        return;
    }
    if (g_alternate) {
        const auto now = std::chrono::steady_clock::now();
        if (now - g_phase_start >= g_period) {
            // Commands already deferred run first; afterwards the targets record directly.
            Drain();
            g_active = !g_active;
            g_phase_start = now;
            Profiler::SetPhase(g_active ? 1 : 0);
            LOG_INFO(Render_Vulkan, "Deferred recording phase: {}", g_active ? "on" : "off");
        }
    }
    g_target0.store(g_active ? primary : VK_NULL_HANDLE, std::memory_order_relaxed);
    g_target1.store(g_active ? upload : VK_NULL_HANDLE, std::memory_order_relaxed);
    MaybeReport();
}

void Drain() {
    const u64 target = g_produced.load(std::memory_order_acquire);
    if (g_done.load(std::memory_order_acquire) >= target) {
        return;
    }
    g_drains.fetch_add(1, std::memory_order_relaxed);
    const auto t0 = std::chrono::steady_clock::now();
    WakeIfSleeping();
    for (u32 spins = 1; g_done.load(std::memory_order_acquire) < target; ++spins) {
        _mm_pause();
        if ((spins & 1023) == 0) {
            WakeIfSleeping();
        }
    }
    g_drain_ns.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - t0)
                             .count(),
                         std::memory_order_relaxed);
}

void Kick() {
    if (g_enabled && Pending()) {
        WakeIfSleeping();
    }
}

} // namespace DriveclubPC::DeferredRecording
