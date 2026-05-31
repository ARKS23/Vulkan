#include "vk_bloom.h"

#include "vk_descriptors.h"
#include "vk_pipelines.h"
#include "vk_rendering.h"
#include "VulkanTools.h"

#include <algorithm>
#include <stdexcept>

namespace vkutil {
    void BloomPass::init(const InitInfo& initInfo) {
        device = initInfo.device;
        allocator = initInfo.allocator;
        descriptorPool = initInfo.descriptorPool;
        pipelineCache = initInfo.pipelineCache;
        hdrFormat = initInfo.hdrFormat;
        shaderPath = initInfo.shaderPath;
        fullscreenVertexShader = initInfo.fullscreenVertexShader;
        downsampleFragmentShader = initInfo.downsampleFragmentShader;
        upsampleFragmentShader = initInfo.upsampleFragmentShader;

        if (device == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE || descriptorPool == VK_NULL_HANDLE) {
            throw std::runtime_error("BloomPass::init received an invalid Vulkan handle");
        }

        createSampler();
        createDescriptorSetLayout();
        createPipelineLayouts();
        createPipelines();
        initialized = true;
    }

    void BloomPass::resize(VkExtent2D extent, VkFormat newHdrFormat, uint32_t requestedMipCount) {
        if (!initialized) {
            throw std::runtime_error("BloomPass::resize called before init");
        }

        destroyMipResources();

        if (hdrFormat != newHdrFormat) {
            destroyPipelines();
            hdrFormat = newHdrFormat;
            createPipelineLayouts();
            createPipelines();
        } else {
            hdrFormat = newHdrFormat;
        }

        sceneExtent = {std::max(1u, extent.width), std::max(1u, extent.height)};

        const uint32_t mipCount = clampMipCount(sceneExtent, requestedMipCount);
        mips.reserve(mipCount);

        uint32_t mipWidth = std::max(1u, sceneExtent.width / 2u);
        uint32_t mipHeight = std::max(1u, sceneExtent.height / 2u);

        for (uint32_t i = 0; i < mipCount; ++i) {
            Mip mip{};
            mip.extent = {mipWidth, mipHeight};
            mip.image = createAllocatedImage(
                device,
                allocator,
                VkExtent3D{mipWidth, mipHeight, 1},
                hdrFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT
            );
            mip.descriptor = descriptorImageInfo(sampler, mip.image.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            mips.push_back(mip);

            mipWidth = std::max(1u, mipWidth / 2u);
            mipHeight = std::max(1u, mipHeight / 2u);
        }

        allocateDescriptorSets(mipCount);
        updateMipDescriptorSets();

        if (sourceDescriptor.imageView != VK_NULL_HANDLE) {
            updateSource(sourceDescriptor);
        }
    }

    void BloomPass::destroy() {
        destroyMipResources();
        destroyPipelines();

        if (sampleSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, sampleSetLayout, nullptr);
            sampleSetLayout = VK_NULL_HANDLE;
        }

        if (sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, sampler, nullptr);
            sampler = VK_NULL_HANDLE;
        }

        hdrSourceSet = VK_NULL_HANDLE;
        mipSets.clear();
        sourceDescriptor = {};
        initialized = false;
    }

    void BloomPass::updateSource(const VkDescriptorImageInfo& hdrSceneDescriptor) {
        sourceDescriptor = hdrSceneDescriptor;
        if (sourceDescriptor.sampler == VK_NULL_HANDLE) {
            sourceDescriptor.sampler = sampler;
        }

        if (hdrSourceSet == VK_NULL_HANDLE || sourceDescriptor.imageView == VK_NULL_HANDLE) {
            return;
        }

        std::vector<VkWriteDescriptorSet> writes = {
            writeCombinedImageSampler(hdrSourceSet, 0, &sourceDescriptor)
        };
        updateDescriptorSet(device, writes);
    }

    void BloomPass::record(VkCommandBuffer cmd, const Settings& settings) {
        if (!initialized || !settings.enabled || mips.empty() || hdrSourceSet == VK_NULL_HANDLE) {
            return;
        }

        const uint32_t activeMipCount = std::min(
            std::max(1u, settings.mipCount),
            static_cast<uint32_t>(mips.size())
        );

        VkExtent2D srcExtent = sceneExtent;
        for (uint32_t i = 0; i < activeMipCount; ++i) {
            Mip& dstMip = mips[i];
            cmdTransitionTrackedImageLayout(cmd, dstMip.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

            VkRenderingAttachmentInfo colorAttachment = renderingAttachmentInfo(
                dstMip.image.imageView,
                VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}}
            );

            cmdBeginColorOnlyRendering(cmd, dstMip.extent, colorAttachment);
            {
                cmdSetViewportAndScissor(cmd, dstMip.extent.width, dstMip.extent.height);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, downsamplePipeline);

                VkDescriptorSet srcSet = (i == 0) ? hdrSourceSet : mipSets[i - 1];
                vkCmdBindDescriptorSets(
                    cmd,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    downsampleLayout,
                    0,
                    1,
                    &srcSet,
                    0,
                    nullptr
                );

                DownsamplePushConstants push{};
                push.srcResolution[0] = static_cast<float>(srcExtent.width);
                push.srcResolution[1] = static_cast<float>(srcExtent.height);
                push.useKarisAverage = settings.useKarisAverage ? 1u : 0u;
                vkCmdPushConstants(cmd, downsampleLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
            cmdEndRendering(cmd);

            cmdTransitionTrackedImageLayout(cmd, dstMip.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
            srcExtent = dstMip.extent;
        }

        if (activeMipCount < 2) {
            return;
        }

        for (uint32_t i = activeMipCount - 1; i > 0; --i) {
            Mip& srcMip = mips[i];
            Mip& dstMip = mips[i - 1];

            cmdTransitionTrackedImageLayout(cmd, dstMip.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

            VkRenderingAttachmentInfo colorAttachment = renderingAttachmentInfo(
                dstMip.image.imageView,
                VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}},
                VK_ATTACHMENT_LOAD_OP_LOAD,
                VK_ATTACHMENT_STORE_OP_STORE
            );

            cmdBeginColorOnlyRendering(cmd, dstMip.extent, colorAttachment);
            {
                cmdSetViewportAndScissor(cmd, dstMip.extent.width, dstMip.extent.height);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, upsamplePipeline);

                VkDescriptorSet srcSet = mipSets[i];
                vkCmdBindDescriptorSets(
                    cmd,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    upsampleLayout,
                    0,
                    1,
                    &srcSet,
                    0,
                    nullptr
                );

                UpsamplePushConstants push{};
                push.srcResolution[0] = static_cast<float>(srcMip.extent.width);
                push.srcResolution[1] = static_cast<float>(srcMip.extent.height);
                push.filterRadius = settings.filterRadius;
                vkCmdPushConstants(cmd, upsampleLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
            cmdEndRendering(cmd);

            cmdTransitionTrackedImageLayout(cmd, dstMip.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

    VkDescriptorImageInfo BloomPass::getBloomDescriptor() const {
        if (mips.empty()) {
            return {};
        }
        return mips.front().descriptor;
    }

    void BloomPass::createSampler() {
        VkSamplerCreateInfo samplerCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerCI.magFilter = VK_FILTER_LINEAR;
        samplerCI.minFilter = VK_FILTER_LINEAR;
        samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerCI.minLod = 0.0f;
        samplerCI.maxLod = 0.0f;
        samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &sampler));
    }

    void BloomPass::createDescriptorSetLayout() {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0)
        };
        VkDescriptorSetLayoutCreateInfo layoutCI = descriptorSetLayoutCreateInfo(bindings);
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &sampleSetLayout));
    }

    void BloomPass::createPipelineLayouts() {
        VkPushConstantRange downsampleRange{};
        downsampleRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        downsampleRange.offset = 0;
        downsampleRange.size = sizeof(DownsamplePushConstants);

        VkPushConstantRange upsampleRange{};
        upsampleRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        upsampleRange.offset = 0;
        upsampleRange.size = sizeof(UpsamplePushConstants);

        downsampleLayout = createPipelineLayout(device, {sampleSetLayout}, {downsampleRange});
        upsampleLayout = createPipelineLayout(device, {sampleSetLayout}, {upsampleRange});
    }

    void BloomPass::createPipelines() {
        VkShaderModule fullscreenModule = vks::tools::loadShader(shaderFile(fullscreenVertexShader).c_str(), device);
        VkShaderModule downsampleModule = vks::tools::loadShader(shaderFile(downsampleFragmentShader).c_str(), device);
        VkShaderModule upsampleModule = vks::tools::loadShader(shaderFile(upsampleFragmentShader).c_str(), device);

        if (fullscreenModule == VK_NULL_HANDLE || downsampleModule == VK_NULL_HANDLE || upsampleModule == VK_NULL_HANDLE) {
            if (fullscreenModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, fullscreenModule, nullptr);
            }
            if (downsampleModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, downsampleModule, nullptr);
            }
            if (upsampleModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, upsampleModule, nullptr);
            }
            throw std::runtime_error("BloomPass failed to load shader modules");
        }

        VkPipelineShaderStageCreateInfo fullscreenStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        fullscreenStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        fullscreenStage.module = fullscreenModule;
        fullscreenStage.pName = "main";

        VkPipelineShaderStageCreateInfo downsampleStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        downsampleStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        downsampleStage.module = downsampleModule;
        downsampleStage.pName = "main";

        VkPipelineShaderStageCreateInfo upsampleStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        upsampleStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        upsampleStage.module = upsampleModule;
        upsampleStage.pName = "main";

        PipelineBuilder downsampleBuilder;
        downsampleBuilder.setPipelineLayout(downsampleLayout)
            .setShaders(fullscreenStage, downsampleStage)
            .setEmptyVertexInput()
            .setColorAttachmentFormat(hdrFormat)
            .disableDepthTest()
            .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .disableBlending();
        downsamplePipeline = downsampleBuilder.build(device, pipelineCache);

        PipelineBuilder upsampleBuilder;
        upsampleBuilder.setPipelineLayout(upsampleLayout)
            .setShaders(fullscreenStage, upsampleStage)
            .setEmptyVertexInput()
            .setColorAttachmentFormat(hdrFormat)
            .disableDepthTest()
            .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .enableAdditiveBlending();
        upsamplePipeline = upsampleBuilder.build(device, pipelineCache);

        vkDestroyShaderModule(device, fullscreenModule, nullptr);
        vkDestroyShaderModule(device, downsampleModule, nullptr);
        vkDestroyShaderModule(device, upsampleModule, nullptr);
    }

    void BloomPass::destroyPipelines() {
        if (downsamplePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, downsamplePipeline, nullptr);
            downsamplePipeline = VK_NULL_HANDLE;
        }
        if (upsamplePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, upsamplePipeline, nullptr);
            upsamplePipeline = VK_NULL_HANDLE;
        }
        if (downsampleLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, downsampleLayout, nullptr);
            downsampleLayout = VK_NULL_HANDLE;
        }
        if (upsampleLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, upsampleLayout, nullptr);
            upsampleLayout = VK_NULL_HANDLE;
        }
    }

    void BloomPass::destroyMipResources() {
        for (Mip& mip : mips) {
            destroyAllocatedImage(device, allocator, mip.image);
            mip.descriptor = {};
            mip.extent = {};
        }
        mips.clear();
        sceneExtent = {};
    }

    void BloomPass::allocateDescriptorSets(uint32_t mipCount) {
        if (hdrSourceSet == VK_NULL_HANDLE) {
            VkDescriptorSetAllocateInfo sourceAllocInfo = descriptorSetAllocateInfo(descriptorPool, &sampleSetLayout, 1);
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &sourceAllocInfo, &hdrSourceSet));
        }

        while (mipSets.size() < mipCount) {
            VkDescriptorSet mipSet = VK_NULL_HANDLE;
            VkDescriptorSetAllocateInfo mipAllocInfo = descriptorSetAllocateInfo(descriptorPool, &sampleSetLayout, 1);
            VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &mipAllocInfo, &mipSet));
            mipSets.push_back(mipSet);
        }
    }

    void BloomPass::updateMipDescriptorSets() {
        for (size_t i = 0; i < mips.size(); ++i) {
            std::vector<VkWriteDescriptorSet> writes = {
                writeCombinedImageSampler(mipSets[i], 0, &mips[i].descriptor)
            };
            updateDescriptorSet(device, writes);
        }
    }

    uint32_t BloomPass::clampMipCount(VkExtent2D extent, uint32_t requestedMipCount) const {
        uint32_t mipWidth = std::max(1u, extent.width / 2u);
        uint32_t mipHeight = std::max(1u, extent.height / 2u);
        uint32_t maxMipCount = 1;

        while (mipWidth > 1u || mipHeight > 1u) {
            mipWidth = std::max(1u, mipWidth / 2u);
            mipHeight = std::max(1u, mipHeight / 2u);
            ++maxMipCount;
        }

        return std::clamp(requestedMipCount, 1u, maxMipCount);
    }

    std::string BloomPass::shaderFile(const std::string& relativePath) const {
        if (shaderPath.empty()) {
            return relativePath;
        }

        const char last = shaderPath.back();
        if (last == '/' || last == '\\') {
            return shaderPath + relativePath;
        }
        return shaderPath + "/" + relativePath;
    }
}
