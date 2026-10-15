// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// driveclub-pc: reuse of Vulkan images and their memory.
//
// Driveclub aliases transient render targets in guest memory, so the texture cache frees and
// re-creates images with identical parameters every frame. Each round trip cost a dedicated VMA
// allocation and free in the driver and kernel (about 4-6 % of the command processor thread,
// docs/parallel-gpu-design.md). Images released by ~UniqueImage (which the texture cache only
// reaches after the GPU finished with them) are kept here, keyed by their exact create info, and
// handed out again by UniqueImage::Create. The pool is capped; the oldest entries are destroyed.
//
// DCPC_IMAGE_RECYCLE_MB=<n> sets the cap in MiB (default 512); 0 disables recycling.

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

struct VmaAllocator_T;
struct VmaAllocation_T;

namespace DriveclubPC::ImageRecycler {

bool Enabled();

// Returns true and fills image/allocation/size when a pooled image with exactly this create
// info and allocator is available.
bool Take(const VkImageCreateInfo& ci, VmaAllocator_T* allocator, VkImage& image,
          VmaAllocation_T*& allocation, u64& size_bytes);

// Takes ownership. Returns false when the image cannot be pooled (the caller destroys it).
bool Put(const VkImageCreateInfo& ci, VmaAllocator_T* allocator, VkImage image,
         VmaAllocation_T* allocation, u64 size_bytes);

} // namespace DriveclubPC::ImageRecycler
