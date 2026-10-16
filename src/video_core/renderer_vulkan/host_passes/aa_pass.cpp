// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/host_passes/aa_pass.h"

#include <cstdlib>
#include <cstring>
#include <vk_mem_alloc.h>
#include "common/assert.h"
#include "video_core/host_shaders/aa_blend_comp.h"
#include "video_core/host_shaders/aa_edges_comp.h"
#include "video_core/host_shaders/aa_fxaa_comp.h"
#include "video_core/host_shaders/aa_weights_comp.h"
#include "video_core/host_shaders/smaa/AreaTex.h"
#include "video_core/host_shaders/smaa/SearchTex.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/skipcache/skipcache.h"

namespace Vulkan::HostPasses {

void AaPass::Create(vk::Device device, VmaAllocator allocator, u32 num_images) {
    // driveclub-pc: lazy image allocation; disabled AA records no work.
    this->device = device;
    this->allocator = allocator;
    const char* compare = std::getenv("DCPC_AA_COMPARE_SHOTS");
    frames.resize(num_images * (compare && compare[0] == '1' ? 3 : 1));
    sampler = Check<"AA sampler">(device.createSamplerUnique({
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxLod = 0.0f,
    }));
    std::array<vk::DescriptorSetLayoutBinding, 6> bindings{};
    for (u32 i = 0; i < bindings.size(); ++i) {
        bindings[i] = {.binding = i,
                       .descriptorType = i == 5 ? vk::DescriptorType::eStorageImage
                                                : vk::DescriptorType::eCombinedImageSampler,
                       .descriptorCount = 1,
                       .stageFlags = vk::ShaderStageFlagBits::eCompute};
    }
    descriptors = Check<"AA descriptors">(device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    }));
    const vk::PushConstantRange constants{vk::ShaderStageFlagBits::eCompute, 0, 20};
    layout = Check<"AA layout">(device.createPipelineLayoutUnique({
        .setLayoutCount = 1,
        .pSetLayouts = &descriptors.get(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &constants,
    }));
    const std::array modules{CompileSPV(AA_FXAA_COMP, device), CompileSPV(AA_EDGES_COMP, device),
                             CompileSPV(AA_WEIGHTS_COMP, device),
                             CompileSPV(AA_BLEND_COMP, device)};
    for (size_t i = 0; i < modules.size(); ++i) {
        pipelines[i] = Check<"AA pipeline">(device.createComputePipelineUnique(
            {}, {
                    .stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                              .module = modules[i],
                              .pName = "main"},
                    .layout = layout.get(),
                }));
        device.destroyShaderModule(modules[i]);
    }
}

void AaPass::CreateTarget(Target& target, vk::Extent2D size, vk::Format format) {
    target.view.reset();
    target.image.Destroy();
    target.image.device = device;
    target.image.allocator = allocator;
    target.image.Create({
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {size.width, size.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
                 vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc,
        .initialLayout = vk::ImageLayout::eUndefined,
    });
    target.view = Check<"AA view">(device.createImageViewUnique({
        .image = target.image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                             .levelCount = 1,
                             .layerCount = 1},
    }));
}

void AaPass::Barrier(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout old_layout,
                     vk::ImageLayout new_layout) {
    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .image = image,
        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                             .levelCount = 1,
                             .layerCount = 1},
    };
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

void AaPass::UploadLookups(vk::CommandBuffer cmd) {
    if (uploaded)
        return;
    CreateTarget(area, {AREATEX_WIDTH, AREATEX_HEIGHT}, vk::Format::eR8G8Unorm);
    CreateTarget(search, {SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT}, vk::Format::eR8Unorm);
    upload = std::make_unique<VideoCore::UniqueBuffer>(device, allocator);
    vk::BufferCreateInfo info{.size = AREATEX_SIZE + SEARCHTEX_SIZE,
                              .usage = vk::BufferUsageFlagBits::eTransferSrc};
    VmaAllocationInfo allocation{};
    upload->Create(info, VideoCore::MemoryType::HostUncached, &allocation);
    std::memcpy(allocation.pMappedData, areaTexBytes, AREATEX_SIZE);
    std::memcpy(static_cast<u8*>(allocation.pMappedData) + AREATEX_SIZE, searchTexBytes,
                SEARCHTEX_SIZE);
    vmaFlushAllocation(allocator, upload->allocation, 0, VK_WHOLE_SIZE);
    for (int i = 0; i < 2; ++i) {
        auto& target = i == 0 ? area : search;
        Barrier(cmd, target.image, vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal);
        const vk::BufferImageCopy copy{
            .bufferOffset = i == 0 ? 0u : AREATEX_SIZE,
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageExtent = {u32(i == 0 ? AREATEX_WIDTH : SEARCHTEX_WIDTH),
                            u32(i == 0 ? AREATEX_HEIGHT : SEARCHTEX_HEIGHT), 1},
        };
        cmd.copyBufferToImage(upload->buffer, target.image, vk::ImageLayout::eTransferDstOptimal,
                              copy);
        Barrier(cmd, target.image, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal);
    }
    uploaded = true;
}

vk::ImageView AaPass::Render(vk::CommandBuffer cmd, vk::ImageView input, vk::Extent2D size,
                             int mode, bool srgb_encoded) {
    if (mode == 0)
        return input;
    UploadLookups(cmd);
    auto& frame = frames[next_frame++ % frames.size()];
    if (frame.size != size) {
        // Same lifetime discipline as the existing FSR frame ring.
        for (auto& target : frame.targets)
            CreateTarget(target, size, vk::Format::eR16G16B16A16Sfloat);
        frame.size = size;
    }
    struct Constants {
        float metrics[4];
        int srgb;
    } constants{
        {1.0f / size.width, 1.0f / size.height, float(size.width), float(size.height)},
        srgb_encoded ? 1 : 0,
    };
    const int first = mode == 1 ? 0 : 1;
    const int last = mode == 1 ? 0 : 3;
    for (int pass = first; pass <= last; ++pass) {
        const int target_index = pass <= 1 ? 0 : pass - 1;
        auto& target = frame.targets[target_index];
        Barrier(cmd, target.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
        std::array<vk::DescriptorImageInfo, 6> infos{};
        // Unused inputs also receive valid views, with layouts matching their actual state.
        const std::array views{input, pass >= 2 ? frame.targets[0].view.get() : input,
                               pass == 3 ? frame.targets[1].view.get() : input, area.view.get(),
                               search.view.get()};
        std::array<vk::WriteDescriptorSet, 6> writes{};
        for (u32 i = 0; i < writes.size(); ++i) {
            infos[i] = {.sampler = i == 5 ? vk::Sampler{} : sampler.get(),
                        .imageView = i == 5 ? target.view.get() : views[i],
                        .imageLayout = i == 5 ? vk::ImageLayout::eGeneral
                                              : vk::ImageLayout::eShaderReadOnlyOptimal};
            writes[i] = {.dstBinding = i,
                         .descriptorCount = 1,
                         .descriptorType = i == 5 ? vk::DescriptorType::eStorageImage
                                                  : vk::DescriptorType::eCombinedImageSampler,
                         .pImageInfo = &infos[i]};
        }
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipelines[pass].get());
        VideoCore::Skipcache::Framework::Instance().BumpForeignPushGen(1);
        cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, layout.get(), 0, writes);
        cmd.pushConstants(layout.get(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants),
                          &constants);
        cmd.dispatch((size.width + 7) / 8, (size.height + 7) / 8, 1);
        Barrier(cmd, target.image, vk::ImageLayout::eGeneral,
                vk::ImageLayout::eShaderReadOnlyOptimal);
        output_image = target.image;
    }
    return frame.targets[mode == 1 ? 0 : 2].view.get();
}

} // namespace Vulkan::HostPasses
