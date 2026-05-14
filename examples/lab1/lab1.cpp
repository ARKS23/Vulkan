#include "lab1.h"
#include "VulkanTools.h"

VulkanExample::VulkanExample()
{
    title = "Lab1: shadow mapping";
    //settings.overlay = false;

    width = 1920;
    height = 1080;

    apiVersion = VK_API_VERSION_1_3;
    useDynamicRendering = true;
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.synchronization2 = VK_TRUE;
    deviceCreatepNextChain = &vulkan13Features;

    camera.type = Camera::lookat;
    camera.setPosition(glm::vec3(0.0f, 0.0f, -12.5f));
    camera.setRotation(glm::vec3(-25.0f, -390.0f, 0.0f));
    camera.setPerspective(45.f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 256.0f);
    timerSpeed *= 1.0f;
}

VulkanExample::~VulkanExample()
{
    if (device) {
        destroyPipelines();

        destroyDescriptors();
        destroyUniformBuffers();
        destroyShadowResources();
        destroyVmaAllocator();
    }
}

void VulkanExample::getEnabledFeatures()
{
    if (deviceProperties.apiVersion < VK_API_VERSION_1_3) {
        vks::tools::exitFatal("Selected GPU does not support Vulkan 1.3", VK_ERROR_INCOMPATIBLE_DRIVER);
    }
}

void VulkanExample::prepare()
{
    createVmaAllocator();
    VulkanExampleBase::prepare();

    loadAssets();
    createShadowResources();
    createUniformBuffers();
    setupDescriptors();
    createPipelines();
    prepared = true;
}

void VulkanExample::render()
{
    if (!prepared) return;

    VulkanExampleBase::prepareFrame();
    if (!paused || camera.updated) updateLight();
    updateUniformBuffers();
    buildCommandBuffer();
    VulkanExampleBase::submitFrame();
}

void VulkanExample::OnUpdateUIOverlay(vks::UIOverlay *overlay)
{
    if (overlay->header("Settings")) {
        overlay->comboBox("Scenes", &sceneIndex, sceneNames);
        overlay->comboBox("Debug Mode", &pushConstan.debugMode, 
            { "Normal Rende", "Shadow Mask", "Shadow UV",
             "Current Depth", "Closest Depth", "Shadow Bias", 
            "Normal Visualization"});
    }

    if (overlay->header("Light Settings")) {
        overlay->colorPicker("Light color", &pushConstan.lightColor.x);
        overlay->sliderFloat("LightSize", &pushConstan.lightSize, 0.1f, 40.0f);
        overlay->sliderFloat("Light Radius", &lightRadius, 0.1f, 20.0f);
        // overlay->sliderFloat("Light X", &lightPos.x, -20.0f, 20.0f);
        // overlay->sliderFloat("Light Y", &lightPos.y, -20.0f, 20.0f);
        // overlay->sliderFloat("Light Z", &lightPos.z, -20.0f, 20.0f);
    }

    if (overlay->header("Shadow Settings")) {
        overlay->sliderFloat("Min Bias", &pushConstan.minShadowBias, 0.0f, 0.01f);
        overlay->sliderFloat("Slope Bias", &pushConstan.slopeShadowBias, 0.0f, 0.05f);
        overlay->sliderFloat("Raster Bias", &depthBiasConstant, 0.0f, 5.0f);
        overlay->sliderFloat("Raster Slope", &depthBiasSlope, 0.0f, 5.0f);
        overlay->comboBox("Shadow Mode", &pushConstan.shadowMode, {"Hard Shadow", "PCF", "PCSS"});
        overlay->sliderInt("Use Possion Disk", &pushConstan.usePoissonDisk, 0, 1);
        overlay->sliderInt("Poisson Sample Count", &pushConstan.PoissonSampleCount, 1, 16);
        overlay->sliderInt("PCF Radius", &pushConstan.PCFRadius, 1, 8);
    }
}

void VulkanExample::createVmaAllocator()
{
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.device = device;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.instance = instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    VK_CHECK_RESULT(vmaCreateAllocator(&allocatorInfo, &allocator));
}

void VulkanExample::destroyVmaAllocator()
{
    if (allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }
}

void VulkanExample::loadAssets()
{
    const uint32_t glTFLoadingFlags =
        vkglTF::FileLoadingFlags::PreTransformVertices |
        vkglTF::FileLoadingFlags::PreMultiplyVertexColors |
        vkglTF::FileLoadingFlags::FlipY |
        vkglTF::FileLoadingFlags::DontLoadImages;

    scenes.resize(2);
    scenes[0].loadFromFile(getAssetPath() + shadowScenePath, vulkanDevice, queue, glTFLoadingFlags);
    scenes[1].loadFromFile(getAssetPath() + sampleScenePath, vulkanDevice, queue, glTFLoadingFlags);
    sceneNames = { "Scene1", "Scene2" };

    lightSphere.loadFromFile(getAssetPath() + "models/sphere.gltf", vulkanDevice, queue, glTFLoadingFlags);
}

void VulkanExample::createShadowResources()
{
    destroyShadowResources();

    shadowMap.shadowTexture.image = vkutil::createAllocatedImage(
        device,
        allocator,
        VkExtent3D{ shadowMap.extent.width, shadowMap.extent.height, 1 },
        shadowMap.format,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VK_CHECK_RESULT(vkCreateSampler(device, &samplerInfo, nullptr, &shadowMap.shadowTexture.sampler));

    shadowMap.shadowTexture.descriptor.sampler = shadowMap.shadowTexture.sampler;
    shadowMap.shadowTexture.descriptor.imageView = shadowMap.shadowTexture.image.imageView;
    shadowMap.shadowTexture.descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
}

void VulkanExample::destroyShadowResources()
{
    vkutil::destroyTexture(device, allocator, shadowMap.shadowTexture);
}

void VulkanExample::createUniformBuffers()
{
    for (UniformBuffers& buffer : uniformBuffers) {
        buffer.shadowOffscreenBuffer = vkutil::createAllocatedBuffer(
            allocator,
            sizeof(UniformDataShadowPass),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_MEMORY_USAGE_AUTO);

        buffer.sceneBuffer = vkutil::createAllocatedBuffer(
            allocator,
            sizeof(UniformDataScenePass),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_MEMORY_USAGE_AUTO);
    }
}

void VulkanExample::destroyUniformBuffers()
{
    for (UniformBuffers& buffer : uniformBuffers) {
        vkutil::destroyAllocatedBuffer(allocator, buffer.sceneBuffer);
        vkutil::destroyAllocatedBuffer(allocator, buffer.shadowOffscreenBuffer);
    }
}

void VulkanExample::setupDescriptors()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames * 3),
        vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames * 3)
    };
    VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames * 3);
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
        vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),
        vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
    };
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout));

    VkDescriptorImageInfo shadowMapDescriptor = shadowMap.shadowTexture.descriptor;
    VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        VkDescriptorBufferInfo sceneBufferInfo{};
        sceneBufferInfo.buffer = uniformBuffers[i].sceneBuffer.handle;
        sceneBufferInfo.offset = 0;
        sceneBufferInfo.range = sizeof(UniformDataScenePass);

        VkDescriptorBufferInfo shadowBufferInfo{};
        shadowBufferInfo.buffer = uniformBuffers[i].shadowOffscreenBuffer.handle;
        shadowBufferInfo.offset = 0;
        shadowBufferInfo.range = sizeof(UniformDataShadowPass);

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i].debug));
        std::array<VkWriteDescriptorSet, 2> debugWrites = {
            vks::initializers::writeDescriptorSet(descriptorSets[i].debug, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &sceneBufferInfo),
            vks::initializers::writeDescriptorSet(descriptorSets[i].debug, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &shadowMapDescriptor)
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(debugWrites.size()), debugWrites.data(), 0, nullptr);

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i].offscreen));
        std::array<VkWriteDescriptorSet, 2> offscreenWrites = {
            vks::initializers::writeDescriptorSet(descriptorSets[i].offscreen, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &shadowBufferInfo),
            vks::initializers::writeDescriptorSet(descriptorSets[i].offscreen, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &shadowMapDescriptor)
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(offscreenWrites.size()), offscreenWrites.data(), 0, nullptr);

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i].scene));
        std::array<VkWriteDescriptorSet, 2> sceneWrites = {
            vks::initializers::writeDescriptorSet(descriptorSets[i].scene, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &sceneBufferInfo),
            vks::initializers::writeDescriptorSet(descriptorSets[i].scene, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &shadowMapDescriptor)
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(sceneWrites.size()), sceneWrites.data(), 0, nullptr);
    }
}

void VulkanExample::destroyDescriptors()
{
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VulkanExample::createPipelines()
{
    createPipelineLayout();
    createDebugPipeline();
    createScenePipeline();
    createShadowPipeline();
    createLightPipeline();
}

void VulkanExample::createPipelineLayout()
{
    // 弄了一个超大的push constant，足够所有管线使用
    static_assert(sizeof(PushconstantData) <= 128, "Scene push constants exceed the guaranteed Vulkan minimum");
    static_assert(sizeof(PushConstantDataLight) <= 128, "Light push constants exceed the guaranteed Vulkan minimum");

    const uint32_t pushConstantSize = static_cast<uint32_t>(
        sizeof(PushConstantDataLight) > sizeof(PushconstantData) ? sizeof(PushConstantDataLight) : sizeof(PushconstantData));

    VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        pushConstantSize,
        0);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
}

void VulkanExample::createDebugPipeline()
{
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
    VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
    VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
    VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
    VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
    VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

    VkPipelineRenderingCreateInfo renderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &swapChain.colorFormat;
    renderingCreateInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    shaderStages[0] = loadShader(getShadersPath() + quadVertexShaderPath, VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = loadShader(getShadersPath() + quadFragmentShaderPath, VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();

    VkGraphicsPipelineCreateInfo pipelineCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.renderPass = VK_NULL_HANDLE;
    pipelineCI.subpass = 0;
    pipelineCI.pNext = &renderingCreateInfo;
    pipelineCI.layout = pipelineLayout;
    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    pipelineCI.pVertexInputState = &emptyInputState;
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pRasterizationState = &rasterizationStateCI;
    pipelineCI.pColorBlendState = &colorBlendStateCI;
    pipelineCI.pDepthStencilState = &depthStencilStateCI;
    pipelineCI.pMultisampleState = &multisampleStateCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.debug));
}

void VulkanExample::createScenePipeline()
{
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
    VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
    VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
    VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
    VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
    VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

    VkPipelineRenderingCreateInfo renderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &swapChain.colorFormat;
    renderingCreateInfo.depthAttachmentFormat = depthFormat;
    renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    shaderStages[0] = loadShader(getShadersPath() + sceneVertexShaderPath, VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = loadShader(getShadersPath() + sceneFragmentShaderPath, VK_SHADER_STAGE_FRAGMENT_BIT);

    VkGraphicsPipelineCreateInfo pipelineCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.renderPass = VK_NULL_HANDLE;
    pipelineCI.subpass = 0;
    pipelineCI.pNext = &renderingCreateInfo;
    pipelineCI.layout = pipelineLayout;
    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal });
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pRasterizationState = &rasterizationStateCI;
    pipelineCI.pColorBlendState = &colorBlendStateCI;
    pipelineCI.pDepthStencilState = &depthStencilStateCI;
    pipelineCI.pMultisampleState = &multisampleStateCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.sceneShadow));
}

void VulkanExample::createShadowPipeline()
{
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
    VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
    rasterizationStateCI.depthBiasEnable = VK_TRUE;
    VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(0, nullptr);
    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
    VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
    VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };
    VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
    std::array<VkPipelineShaderStageCreateInfo, 1> shaderStages{};

    VkPipelineRenderingCreateInfo renderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingCreateInfo.colorAttachmentCount = 0;
    renderingCreateInfo.pColorAttachmentFormats = nullptr;
    renderingCreateInfo.depthAttachmentFormat = shadowMap.format;
    renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    shaderStages[0] = loadShader(getShadersPath() + shadowVertexShaderPath, VK_SHADER_STAGE_VERTEX_BIT);

    VkGraphicsPipelineCreateInfo pipelineCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.renderPass = VK_NULL_HANDLE;
    pipelineCI.subpass = 0;
    pipelineCI.pNext = &renderingCreateInfo;
    pipelineCI.layout = pipelineLayout;
    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal });
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pRasterizationState = &rasterizationStateCI;
    pipelineCI.pColorBlendState = &colorBlendStateCI;
    pipelineCI.pDepthStencilState = &depthStencilStateCI;
    pipelineCI.pMultisampleState = &multisampleStateCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.shadowOffscreen));
}

void VulkanExample::createLightPipeline()
{
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
    VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
    VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
    VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
    VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
    VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

    VkPipelineRenderingCreateInfo renderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &swapChain.colorFormat;
    renderingCreateInfo.depthAttachmentFormat = depthFormat;
    renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    shaderStages[0] = loadShader(getShadersPath() + lightVertexShaderPath, VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = loadShader(getShadersPath() + lightFragmentShaderPath, VK_SHADER_STAGE_FRAGMENT_BIT);

    VkGraphicsPipelineCreateInfo pipelineCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.renderPass = VK_NULL_HANDLE;
    pipelineCI.subpass = 0;
    pipelineCI.pNext = &renderingCreateInfo;
    pipelineCI.layout = pipelineLayout;
    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position });
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pRasterizationState = &rasterizationStateCI;
    pipelineCI.pColorBlendState = &colorBlendStateCI;
    pipelineCI.pDepthStencilState = &depthStencilStateCI;
    pipelineCI.pMultisampleState = &multisampleStateCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.lightSphere));
}

void VulkanExample::destroyPipelines()
{
    vkDestroyPipeline(device, pipelines.debug, nullptr);
    vkDestroyPipeline(device, pipelines.sceneShadow, nullptr);
    vkDestroyPipeline(device, pipelines.lightSphere, nullptr);
    vkDestroyPipeline(device, pipelines.shadowOffscreen, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
}

void VulkanExample::updateLight()
{
    rotationAngle += rotationSpeed * 0.025;
    lightPos.x = sin(rotationAngle) * lightRadius;
    lightPos.z = cos(rotationAngle) * lightRadius;

    glm::mat4 model = glm::translate(glm::mat4(1.0f), lightPos) * glm::scale(glm::mat4(1.0f), glm::vec3(0.085f * pushConstan.lightSize));
    pushConstantLight.mvp = camera.matrices.perspective * camera.matrices.view * model;
    pushConstantLight.lightColor = pushConstan.lightColor;
}

void VulkanExample::updateUniformBuffers()
{
    glm::mat4 depthProjectionMatrix = glm::ortho(-10.f, 10.f, -10.f, 10.f, shadowNearPlane, shadowFarPlane);
    glm::mat4 depthViewMatrix = glm::lookAt(lightPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 depthModelMatrix = glm::mat4(1.0f);
    uniformDataShadow.depthMVP = depthProjectionMatrix * depthViewMatrix * depthModelMatrix;
    memcpy(uniformBuffers[currentBuffer].shadowOffscreenBuffer.allocationInfo.pMappedData, &uniformDataShadow, sizeof(uniformDataShadow));
    vmaFlushAllocation(allocator, uniformBuffers[currentBuffer].shadowOffscreenBuffer.allocation, 0, sizeof(uniformDataShadow));

    uniformDataScene.projection = camera.matrices.perspective;
    uniformDataScene.view = camera.matrices.view;
    uniformDataScene.model = glm::scale(glm::mat4(1.0f), glm::vec3(1.5f));
    uniformDataScene.lightPos = glm::vec4(lightPos, 1.0f);
    uniformDataScene.cameraPos = camera.viewPos;
    uniformDataScene.depthBiasMVP = uniformDataShadow.depthMVP;
    uniformDataScene.zNear = shadowNearPlane;
    uniformDataScene.zFar = shadowFarPlane;
    memcpy(uniformBuffers[currentBuffer].sceneBuffer.allocationInfo.pMappedData, &uniformDataScene, sizeof(uniformDataScene));
    vmaFlushAllocation(allocator, uniformBuffers[currentBuffer].sceneBuffer.allocation, 0, sizeof(uniformDataScene));
}

void VulkanExample::buildCommandBuffer()
{
    VkCommandBuffer commandBuffer = drawCmdBuffers[currentBuffer];
    VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
    VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufInfo));

    vkutil::cmdTransitionImageLayout(commandBuffer, shadowMap.shadowTexture.image.image, shadowMap.shadowTexture.image.layout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    shadowMap.shadowTexture.image.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    drawShadowMap(commandBuffer);
    vkutil::cmdTransitionImageLayout(commandBuffer, shadowMap.shadowTexture.image.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    shadowMap.shadowTexture.image.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    vkutil::cmdTransitionImageLayout(commandBuffer, depthStencil.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[currentImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    drawScene(commandBuffer);
    vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[currentImageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
}

void VulkanExample::drawShadowMap(VkCommandBuffer commandBuffer)
{
    VkRenderingAttachmentInfo depthAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depthAttachment.imageView = shadowMap.shadowTexture.image.imageView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderingInfo.renderArea = { 0, 0, shadowMap.extent.width, shadowMap.extent.height };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.pColorAttachments = nullptr;
    renderingInfo.pDepthAttachment = &depthAttachment;
    renderingInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    {
        VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(shadowMap.extent.width), static_cast<float>(shadowMap.extent.height), 0.0f, 1.0f };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        VkRect2D scissor{ { 0, 0 }, { shadowMap.extent.width, shadowMap.extent.height } };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.shadowOffscreen);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentBuffer].offscreen, 0, nullptr);
        vkCmdSetDepthBias(commandBuffer, depthBiasConstant, 0.0f, depthBiasSlope);
        scenes[sceneIndex].draw(commandBuffer);
    }
    vkCmdEndRendering(commandBuffer);
}

void VulkanExample::drawScene(VkCommandBuffer commandBuffer)
{
    VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView = swapChain.imageViews[currentImageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { 0.01f, 0.02f, 0.025f, 1.0f };

    VkRenderingAttachmentInfo depthAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depthAttachment.imageView = depthStencil.view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderingInfo.renderArea = { 0, 0, width, height };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;
    renderingInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    {
        VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        VkRect2D scissor{ { 0, 0 }, { width, height } };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.sceneShadow);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentBuffer].scene, 0, nullptr);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushconstantData), &pushConstan);
        scenes[sceneIndex].draw(commandBuffer);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.lightSphere);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantDataLight), &pushConstantLight);
        lightSphere.draw(commandBuffer);
        drawUI(commandBuffer);
    }
    vkCmdEndRendering(commandBuffer);
}

void VulkanExample::drawQuad(VkCommandBuffer commandBuffer)
{
    VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView = swapChain.imageViews[currentImageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { 0.01f, 0.02f, 0.025f, 1.0f };

    VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderingInfo.renderArea = { 0, 0, width, height };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = nullptr;
    renderingInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    {
        VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        VkRect2D scissor{ { 0, 0 }, { width, height } };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.debug);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentBuffer].debug, 0, nullptr);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
    vkCmdEndRendering(commandBuffer);
}
