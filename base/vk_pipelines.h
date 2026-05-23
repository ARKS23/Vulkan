#pragma once

#include "vk_types.h"

namespace vkutil {
    // 管线布局封装
    VkPipelineLayout createPipelineLayout(VkDevice device,
        const std::vector<VkDescriptorSetLayout>& setLayouts,
        const std::vector<VkPushConstantRange>& pushConstantRanges = {}
    );

    class PipelineBuilder {
    public:
        PipelineBuilder();

        PipelineBuilder& clear(); // 清除管线状态设置回默认
        VkPipeline build(VkDevice device, VkPipelineCache pipelineCache = VK_NULL_HANDLE);

        PipelineBuilder& setPipelineLayout(VkPipelineLayout layout);

        PipelineBuilder& setShaders(VkPipelineShaderStageCreateInfo vertexShader, VkPipelineShaderStageCreateInfo fragmentShader);

        PipelineBuilder& setVertexInput(const VkPipelineVertexInputStateCreateInfo& vertexInput);
        PipelineBuilder& setEmptyVertexInput();

        PipelineBuilder& setInputTopology(VkPrimitiveTopology topology);
        PipelineBuilder& setPolygonMode(VkPolygonMode mode);
        PipelineBuilder& setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);

        PipelineBuilder& setColorAttachmentFormat(VkFormat format);
        PipelineBuilder& setColorAttachmentFormats(const std::vector<VkFormat>& formats);
        PipelineBuilder& setDepthFormat(VkFormat format);

        PipelineBuilder& enableDepthTest(bool depthWriteEnable, VkCompareOp compareOp);
        PipelineBuilder& disableDepthTest();
        PipelineBuilder& setDepthTest(bool enable, VkCompareOp compareOp = VK_COMPARE_OP_LESS_OR_EQUAL);
        PipelineBuilder& setDepthWrite(bool enable);

        PipelineBuilder& disableBlending();
        PipelineBuilder& enableAlphaBlending();
        PipelineBuilder& enableAdditiveBlending();

        PipelineBuilder& setMultisamplingNone();
        PipelineBuilder& setDynamicStates(const std::vector<VkDynamicState>& states);

    private:
        void refreshVertexInputPointers();

    private:
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

        std::vector<VkVertexInputBindingDescription> vertexBindings;
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        VkPipelineMultisampleStateCreateInfo multisampling{};
        VkPipelineDepthStencilStateCreateInfo depthStencil{};

        std::vector<VkDynamicState> dynamicStates;
        VkPipelineDynamicStateCreateInfo dynamicStateInfo{};

        std::vector<VkFormat> colorAttachmentFormats;
        VkPipelineRenderingCreateInfo renderingInfo{};

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        VkFormat stencilFormat = VK_FORMAT_UNDEFINED;
    };
}
