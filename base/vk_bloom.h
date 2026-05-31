#pragma once

#include "vk_images.h"

#include <string>
#include <vector>

namespace vkutil {
    class BloomPass {
    public:
        struct Settings {
            uint32_t mipCount{5};
            float filterRadius{0.5f};
            bool useKarisAverage{true};
            bool enabled{true};
        };

        struct InitInfo {
            VkDevice device{VK_NULL_HANDLE};
            VmaAllocator allocator{VK_NULL_HANDLE};
            VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
            VkPipelineCache pipelineCache{VK_NULL_HANDLE};
            VkFormat hdrFormat{VK_FORMAT_R16G16B16A16_SFLOAT};
            std::string shaderPath;
            std::string fullscreenVertexShader{"shared/bloomFullscreen.vert.spv"};
            std::string downsampleFragmentShader{"shared/bloomDownsample.frag.spv"};
            std::string upsampleFragmentShader{"shared/bloomUpsample.frag.spv"};
        };

        struct Mip {
            AllocatedImage image;
            VkDescriptorImageInfo descriptor{};
            VkExtent2D extent{};
        };

        BloomPass() = default;
        BloomPass(const BloomPass&) = delete;
        BloomPass& operator=(const BloomPass&) = delete;

        void init(const InitInfo& initInfo);
        void resize(VkExtent2D extent, VkFormat hdrFormat, uint32_t mipCount);
        void destroy();

        void updateSource(const VkDescriptorImageInfo& hdrSceneDescriptor);
        void record(VkCommandBuffer cmd, const Settings& settings);

        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] bool hasOutput() const { return !mips.empty(); }
        [[nodiscard]] VkExtent2D getExtent() const { return sceneExtent; }
        [[nodiscard]] VkFormat getFormat() const { return hdrFormat; }
        [[nodiscard]] uint32_t getMipCount() const { return static_cast<uint32_t>(mips.size()); }
        [[nodiscard]] const std::vector<Mip>& getMips() const { return mips; }
        [[nodiscard]] VkDescriptorImageInfo getBloomDescriptor() const;

    private:
        struct DownsamplePushConstants {
            float srcResolution[2];
            uint32_t useKarisAverage;
            float padding;
        };

        struct UpsamplePushConstants {
            float srcResolution[2];
            float filterRadius;
            float padding;
        };

        void createSampler();
        void createDescriptorSetLayout();
        void createPipelineLayouts();
        void createPipelines();
        void destroyPipelines();
        void destroyMipResources();

        void allocateDescriptorSets(uint32_t mipCount);
        void updateMipDescriptorSets();

        [[nodiscard]] uint32_t clampMipCount(VkExtent2D extent, uint32_t requestedMipCount) const;
        [[nodiscard]] std::string shaderFile(const std::string& relativePath) const;

    private:
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{VK_NULL_HANDLE};
        VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
        VkPipelineCache pipelineCache{VK_NULL_HANDLE};

        VkFormat hdrFormat{VK_FORMAT_R16G16B16A16_SFLOAT};
        VkExtent2D sceneExtent{};
        std::string shaderPath;
        std::string fullscreenVertexShader;
        std::string downsampleFragmentShader;
        std::string upsampleFragmentShader;

        VkSampler sampler{VK_NULL_HANDLE};
        VkDescriptorSetLayout sampleSetLayout{VK_NULL_HANDLE};
        VkDescriptorSet hdrSourceSet{VK_NULL_HANDLE};
        std::vector<VkDescriptorSet> mipSets;
        VkDescriptorImageInfo sourceDescriptor{};

        VkPipelineLayout downsampleLayout{VK_NULL_HANDLE};
        VkPipelineLayout upsampleLayout{VK_NULL_HANDLE};
        VkPipeline downsamplePipeline{VK_NULL_HANDLE};
        VkPipeline upsamplePipeline{VK_NULL_HANDLE};

        std::vector<Mip> mips;
        bool initialized{false};
    };
}

