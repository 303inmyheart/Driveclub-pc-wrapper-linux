// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <memory>
#include <vector>
#include "video_core/buffer_cache/buffer.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan::HostPasses {

// driveclub-pc: FXAA / SMAA before upscaling. Resources live until the frame ring can reuse them.
class AaPass {
public:
    void Create(vk::Device device, VmaAllocator allocator, u32 num_images);
    vk::ImageView Render(vk::CommandBuffer cmd, vk::ImageView input, vk::Extent2D size, int mode,
                         bool srgb_encoded);
    vk::Image OutputImage() const {
        return output_image;
    }

private:
    struct Target {
        VideoCore::UniqueImage image;
        vk::UniqueImageView view;
    };
    struct Frame {
        std::array<Target, 3> targets;
        vk::Extent2D size{};
    };
    void CreateTarget(Target& target, vk::Extent2D size, vk::Format format);
    void UploadLookups(vk::CommandBuffer cmd);
    void Barrier(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout old_layout,
                 vk::ImageLayout new_layout);

    vk::Device device{};
    VmaAllocator allocator{};
    vk::UniqueSampler sampler;
    vk::UniqueDescriptorSetLayout descriptors;
    vk::UniquePipelineLayout layout;
    std::array<vk::UniquePipeline, 4> pipelines;
    Target area, search;
    std::unique_ptr<VideoCore::UniqueBuffer> upload;
    bool uploaded = false;
    std::vector<Frame> frames;
    size_t next_frame = 0;
    vk::Image output_image{};
};
} // namespace Vulkan::HostPasses
