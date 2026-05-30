#include "vk_pipelines.h"
#include "VulkanTools.h"

namespace vkutil {
    VkPipelineLayout createPipelineLayout(VkDevice device, const std::vector<VkDescriptorSetLayout>& setLayouts, const std::vector<VkPushConstantRange>& pushConstantRanges) {
        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        info.pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data();
        info.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
        info.pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data();
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &info, nullptr, &layout));
        return layout;
    }

    PipelineBuilder::PipelineBuilder() {
        clear();
    }

    PipelineBuilder& PipelineBuilder::clear() {
        shaderStages.clear();
        vertexBindings.clear();
        vertexAttributes.clear();
        colorAttachmentFormats.clear();

        vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        inputAssembly = {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        rasterizer = {};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.sampleShadingEnable = VK_FALSE;

        depthStencil = {};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        dynamicStateInfo = {};
        dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;

        renderingInfo = {};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

        pipelineLayout = VK_NULL_HANDLE;
        depthFormat = VK_FORMAT_UNDEFINED;
        stencilFormat = VK_FORMAT_UNDEFINED;

        return *this;
    }

    VkPipeline PipelineBuilder::build(VkDevice device, VkPipelineCache pipelineCache) {
        if (pipelineLayout == VK_NULL_HANDLE) 
            vks::tools::exitFatal("PipelineBuilder: pipeline layout is null", -1);

        if (shaderStages.empty()) 
            vks::tools::exitFatal("PipelineBuilder: shader stages are empty", -1);

        refreshVertexInputPointers();

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicStateInfo.pDynamicStates = dynamicStates.empty() ? nullptr : dynamicStates.data();

        VkPipelineColorBlendStateCreateInfo colorBlendState{};
        colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendState.logicOpEnable = VK_FALSE;

        // Dynamic Rendering 的 MRT 数量由 colorAttachmentFormats 决定。
        // blend attachment 数量必须和 colorAttachmentCount 一致，否则只有第一个 RT 的状态是完整的，
        // G-Buffer 这类多目标输出会导致后续 RT 保持 clear value 或触发验证层错误。
        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(
            colorAttachmentFormats.size(),
            colorBlendAttachment
        );
        colorBlendState.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        colorBlendState.pAttachments = colorBlendAttachments.empty() ? nullptr : colorBlendAttachments.data();

        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentFormats.size());
        renderingInfo.pColorAttachmentFormats = colorAttachmentFormats.empty() ? nullptr : colorAttachmentFormats.data();
        renderingInfo.depthAttachmentFormat = depthFormat;
        renderingInfo.stencilAttachmentFormat = stencilFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlendState;
        pipelineInfo.pDynamicState = &dynamicStateInfo;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = VK_NULL_HANDLE;
        pipelineInfo.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline));
        return pipeline;
    }

    PipelineBuilder& PipelineBuilder::setPipelineLayout(VkPipelineLayout layout) {
        pipelineLayout = layout;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setShaders(VkPipelineShaderStageCreateInfo vertexShader, VkPipelineShaderStageCreateInfo fragmentShader) {
        shaderStages.clear();
        shaderStages.push_back(vertexShader);
        shaderStages.push_back(fragmentShader);
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setInputTopology(VkPrimitiveTopology topology) {
        inputAssembly.topology = topology;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setPolygonMode(VkPolygonMode mode) {
        rasterizer.polygonMode = mode;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace) {
        rasterizer.cullMode = cullMode;
        rasterizer.frontFace = frontFace;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setVertexInput(const VkPipelineVertexInputStateCreateInfo& input) {
        vertexBindings.assign(
            input.pVertexBindingDescriptions,
            input.pVertexBindingDescriptions + input.vertexBindingDescriptionCount);

        vertexAttributes.assign(
            input.pVertexAttributeDescriptions,
            input.pVertexAttributeDescriptions + input.vertexAttributeDescriptionCount);

        refreshVertexInputPointers();
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setEmptyVertexInput() {
        vertexBindings.clear();
        vertexAttributes.clear();
        refreshVertexInputPointers();
        return *this;
    }

    void PipelineBuilder::refreshVertexInputPointers() {
        vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
        vertexInputInfo.pVertexBindingDescriptions = vertexBindings.empty() ? nullptr : vertexBindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.empty() ? nullptr : vertexAttributes.data();
    }

    PipelineBuilder& PipelineBuilder::setColorAttachmentFormat(VkFormat format) {
        colorAttachmentFormats = { format };
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setColorAttachmentFormats(const std::vector<VkFormat>& formats) {
        colorAttachmentFormats = formats;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setDepthFormat(VkFormat format) {
        depthFormat = format;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::enableDepthTest(bool depthWriteEnable, VkCompareOp compareOp) {
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = depthWriteEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = compareOp;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::disableDepthTest() {
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setDepthTest(bool enable, VkCompareOp compareOp) {
        depthStencil.depthTestEnable = enable ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = compareOp;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setDepthWrite(bool enable) {
        depthStencil.depthWriteEnable = enable ? VK_TRUE : VK_FALSE;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::disableBlending() {
        colorBlendAttachment.blendEnable = VK_FALSE;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::enableAlphaBlending() {
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::enableAdditiveBlending() {
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setMultisamplingNone() {
        multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.minSampleShading = 1.0f;
        multisampling.pSampleMask = nullptr;
        multisampling.alphaToCoverageEnable = VK_FALSE;
        multisampling.alphaToOneEnable = VK_FALSE;
        return *this;
    }

    PipelineBuilder& PipelineBuilder::setDynamicStates(const std::vector<VkDynamicState>& states) {
        dynamicStates = states;
        dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicStateInfo.pDynamicStates = dynamicStates.empty() ? nullptr : dynamicStates.data();
        return *this;
    }
}
