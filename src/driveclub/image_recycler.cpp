// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>

#include "common/logging/log.h"
#include "driveclub/image_recycler.h"

#include <vk_mem_alloc.h>

namespace DriveclubPC::ImageRecycler {

namespace {

// The parts of VkImageCreateInfo that define an image; only images without pNext chains and
// without explicit queue families are pooled, so these fields describe them completely.
struct Key {
    VkImageCreateFlags flags;
    VkImageType type;
    VkFormat format;
    VkExtent3D extent;
    u32 mips;
    u32 layers;
    VkSampleCountFlagBits samples;
    VkImageTiling tiling;
    VkImageUsageFlags usage;
    VkSharingMode sharing;
    VkImageLayout initial_layout;
    VmaAllocator allocator;

    bool operator==(const Key& o) const {
        return flags == o.flags && type == o.type && format == o.format &&
               extent.width == o.extent.width && extent.height == o.extent.height &&
               extent.depth == o.extent.depth && mips == o.mips && layers == o.layers &&
               samples == o.samples && tiling == o.tiling && usage == o.usage &&
               sharing == o.sharing && initial_layout == o.initial_layout && allocator == o.allocator;
    }
};

struct Entry {
    Key key;
    VkImage image;
    VmaAllocation allocation;
    u64 size;
};

struct Pool {
    std::mutex mutex;
    std::deque<Entry> entries; // oldest first
    u64 bytes = 0;
    u64 cap = 0;
    int enabled = -1;
    // statistics since the last report
    u64 takes = 0, hits = 0, puts = 0, pooled = 0, evicted = 0;
    std::chrono::steady_clock::time_point last_report = std::chrono::steady_clock::now();
};
Pool g_pool;

bool Poolable(const VkImageCreateInfo& ci) {
    return ci.pNext == nullptr && ci.queueFamilyIndexCount == 0;
}

Key MakeKey(const VkImageCreateInfo& ci, VmaAllocator allocator) {
    return Key{ci.flags,  ci.imageType, ci.format,  ci.extent,      ci.mipLevels,     ci.arrayLayers,
               ci.samples, ci.tiling,   ci.usage,   ci.sharingMode, ci.initialLayout, allocator};
}

void MaybeReport() {
    const auto now = std::chrono::steady_clock::now();
    if (now - g_pool.last_report < std::chrono::seconds(10)) {
        return;
    }
    LOG_INFO(Render_Vulkan,
             "Image recycler: creates {} (reused {}), releases {} (pooled {}), evicted {}, pool {} "
             "images / {} MiB of {} MiB, per 10 s",
             g_pool.takes, g_pool.hits, g_pool.puts, g_pool.pooled, g_pool.evicted,
             g_pool.entries.size(), g_pool.bytes >> 20, g_pool.cap >> 20);
    g_pool.takes = g_pool.hits = g_pool.puts = g_pool.pooled = g_pool.evicted = 0;
    g_pool.last_report = now;
}

} // namespace

bool Enabled() {
    std::scoped_lock lock{g_pool.mutex};
    if (g_pool.enabled < 0) {
        const char* env = std::getenv("DCPC_IMAGE_RECYCLE_MB");
        const u64 mb = env ? std::strtoull(env, nullptr, 10) : 512;
        g_pool.cap = mb << 20;
        g_pool.enabled = mb > 0 ? 1 : 0;
        LOG_INFO(Render_Vulkan, "Image recycler: {}", g_pool.enabled ? "on" : "off");
    }
    return g_pool.enabled == 1;
}

bool Take(const VkImageCreateInfo& ci, VmaAllocator_T* allocator, VkImage& image,
          VmaAllocation_T*& allocation, u64& size_bytes) {
    if (!Enabled() || !Poolable(ci)) {
        return false;
    }
    const Key key = MakeKey(ci, allocator);
    std::scoped_lock lock{g_pool.mutex};
    ++g_pool.takes;
    MaybeReport();
    // newest first: the most recently released image is the most likely to still be resident
    for (auto it = g_pool.entries.rbegin(); it != g_pool.entries.rend(); ++it) {
        if (it->key == key) {
            image = it->image;
            allocation = it->allocation;
            size_bytes = it->size;
            g_pool.bytes -= it->size;
            g_pool.entries.erase(std::next(it).base());
            ++g_pool.hits;
            return true;
        }
    }
    return false;
}

bool Put(const VkImageCreateInfo& ci, VmaAllocator_T* allocator, VkImage image,
         VmaAllocation_T* allocation, u64 size_bytes) {
    if (!Enabled() || !Poolable(ci)) {
        return false;
    }
    std::scoped_lock lock{g_pool.mutex};
    ++g_pool.puts;
    if (size_bytes > g_pool.cap) {
        return false;
    }
    while (g_pool.bytes + size_bytes > g_pool.cap && !g_pool.entries.empty()) {
        Entry& old = g_pool.entries.front();
        vmaDestroyImage(old.key.allocator, old.image, old.allocation);
        g_pool.bytes -= old.size;
        g_pool.entries.pop_front();
        ++g_pool.evicted;
    }
    g_pool.entries.push_back(Entry{MakeKey(ci, allocator), image, allocation, size_bytes});
    g_pool.bytes += size_bytes;
    ++g_pool.pooled;
    return true;
}

} // namespace DriveclubPC::ImageRecycler
