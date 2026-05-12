#include "lab1.h"
#include "VulkanTools.h"

VulkanExample::VulkanExample()
{
    title = "Lab1: shadow mapping";
    //settings.overlay = false;

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
    if (!prepared)  return;

    VulkanExampleBase::prepareFrame();
    //if (!paused || camera.updated) updateLight();
    updateUniformBuffers();
    buildCommandBuffer();
    VulkanExampleBase::submitFrame();
}

void VulkanExample::OnUpdateUIOverlay(vks::UIOverlay *overlay) {
    if (overlay->header("Settings")) {
        overlay->comboBox("Scenes", &sceneIndex, sceneNames);
        overlay->colorPicker("Light color", &pushConstan.lightColor.x);
        overlay->sliderFloat("Min Bias", &pushConstan.minShadowBias, 0.0f, 0.01f);
        overlay->sliderFloat("Slope Bias", &pushConstan.slopeShadowBias, 0.0f, 0.05f);
        overlay->sliderFloat("Raster Bias", &depthBiasConstant, 0.0f, 5.0f);
        overlay->sliderFloat("Raster Slope", &depthBiasSlope, 0.0f, 5.0f);
        overlay->sliderInt("EnablePCF", &pushConstan.enablePCF, 0, 1);
        overlay->sliderInt("PCF Radius", &pushConstan.PCFRadius, 1, 3);
        overlay->sliderFloat("Light X", &lightPos.x, -20.0f, 20.0f);
        overlay->sliderFloat("Light Y", &lightPos.y, -20.0f, 20.0f);
        overlay->sliderFloat("Light Z", &lightPos.z, -20.0f, 20.0f);
    }
}

void VulkanExample::createVmaAllocator()
{
    VmaAllocatorCreateInfo allocatorInfo {};
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
    // 第一阶段只需要几何参与 depth/debug 流程，先不加载材质贴图，避免 descriptor 体系过早复杂化。
    const uint32_t glTFLoadingFlags =
        vkglTF::FileLoadingFlags::PreTransformVertices |
        vkglTF::FileLoadingFlags::PreMultiplyVertexColors |
        vkglTF::FileLoadingFlags::FlipY |
        vkglTF::FileLoadingFlags::DontLoadImages;

    scenes.resize(2);
    scenes[0].loadFromFile(getAssetPath() + shadowScenePath, vulkanDevice, queue, glTFLoadingFlags);
    scenes[1].loadFromFile(getAssetPath() + sampleScenePath, vulkanDevice, queue, glTFLoadingFlags);
    sceneNames = { "Scene1", "Scene2" };
}

void VulkanExample::createShadowResources()
{
    destroyShadowResources();

    // Shadow map 是 depth attachment + sampled image：第一遍写深度，第二遍/debug pass 采样它。
    shadowMap.shadowTexture.image = vkutil::createAllocatedImage(
        device,
        allocator,
        VkExtent3D{ shadowMap.extent.width, shadowMap.extent.height, 1 },
        shadowMap.format,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    VkSamplerCreateInfo samplerInfo { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
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
    // 暂时使用一套 layout 稳住数据流：binding 0 是当前 pass 的 UBO，binding 1 是 shadow map。
    std::vector<VkDescriptorPoolSize> poolSizes = {
        vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames * 3),
        vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames * 3)
    };
    VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames * 3);
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

    // binding 0 : UBO            biding 1 : texture
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
        vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),
        vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
    };
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout));

    VkDescriptorImageInfo shadowMapDescriptor = shadowMap.shadowTexture.descriptor;
    VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        VkDescriptorBufferInfo sceneBufferInfo {};
        sceneBufferInfo.buffer = uniformBuffers[i].sceneBuffer.handle;
        sceneBufferInfo.offset = 0;
        sceneBufferInfo.range = sizeof(UniformDataScenePass);

        VkDescriptorBufferInfo shadowBufferInfo {};
        shadowBufferInfo.buffer = uniformBuffers[i].shadowOffscreenBuffer.handle;
        shadowBufferInfo.offset = 0;
        shadowBufferInfo.range = sizeof(UniformDataShadowPass);

        // debug descriptor
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i].debug));
        std::array<VkWriteDescriptorSet, 2> debugWrites = {
            vks::initializers::writeDescriptorSet(descriptorSets[i].debug, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &sceneBufferInfo),
            vks::initializers::writeDescriptorSet(descriptorSets[i].debug, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &shadowMapDescriptor)
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(debugWrites.size()), debugWrites.data(), 0, nullptr);

        // shadow map descriptor
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i].offscreen));
        std::array<VkWriteDescriptorSet, 2> offscreenWrites = {
            vks::initializers::writeDescriptorSet(descriptorSets[i].offscreen, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &shadowBufferInfo),
            vks::initializers::writeDescriptorSet(descriptorSets[i].offscreen, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &shadowMapDescriptor)
        };
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(offscreenWrites.size()), offscreenWrites.data(), 0, nullptr);

        // scene descriptor
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
    // push constants
    VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PushconstantData), 0);

    // 复用一个 pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

    // pipeline 细节设置
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

    // dynamic rendering设置
    VkPipelineRenderingCreateInfo renderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &swapChain.colorFormat;
    renderingCreateInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineCI = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineCI.renderPass = VK_NULL_HANDLE;
    pipelineCI.subpass = 0;
    pipelineCI.pNext = &renderingCreateInfo;
    // 常规设置
    pipelineCI.layout = pipelineLayout;
    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pRasterizationState = &rasterizationStateCI;
    pipelineCI.pColorBlendState = &colorBlendStateCI;
    pipelineCI.pDepthStencilState = &depthStencilStateCI;
    pipelineCI.pMultisampleState = &multisampleStateCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();

    // debug pipeline
    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
    shaderStages[0] = loadShader(getShadersPath() + quadVertexShaderPath, VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = loadShader(getShadersPath() + quadFragmentShaderPath, VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
    pipelineCI.pVertexInputState = &emptyInputState; // 配置空的顶点输入信息

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.debug));

    // scene pipeline
    VkPipelineVertexInputStateCreateInfo *pSceneVertexInputStateCI = vkglTF::Vertex::getPipelineVertexInputState({vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal});
    pipelineCI.pVertexInputState = pSceneVertexInputStateCI;
    rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
    renderingCreateInfo.depthAttachmentFormat = depthFormat;
    shaderStages[0] = loadShader(getShadersPath() + sceneVertexShaderPath, VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = loadShader(getShadersPath() + sceneFragmentShaderPath, VK_SHADER_STAGE_FRAGMENT_BIT);
    // TODO：PCF管线
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.sceneShadow));

    // shadow pipeline
    VkPipelineRenderingCreateInfo shadowRenderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    shadowRenderingInfo.colorAttachmentCount = 0;
    shadowRenderingInfo.pColorAttachmentFormats = nullptr;
    shadowRenderingInfo.depthAttachmentFormat = shadowMap.format;
    shadowRenderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
    pipelineCI.pNext = &shadowRenderingInfo;
    pipelineCI.renderPass = VK_NULL_HANDLE;

    shaderStages[0] = loadShader(getShadersPath() + shadowVertexShaderPath, VK_SHADER_STAGE_VERTEX_BIT);
    pipelineCI.stageCount = 1;  // 深度图不需要片段着色器

    pipelineCI.pVertexInputState = pSceneVertexInputStateCI;    // 配置输入顶点信息

    colorBlendStateCI.attachmentCount = 0;  // 不需要混合
    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE; // 深度图关闭剔除
    depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    rasterizationStateCI.depthBiasEnable = VK_TRUE;
    dynamicStateEnables.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS); // 动态修改深度偏移
    dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.shadowOffscreen));
}

void VulkanExample::destroyPipelines()
{
    vkDestroyPipeline(device, pipelines.debug, nullptr);
    vkDestroyPipeline(device, pipelines.sceneShadow, nullptr);
    vkDestroyPipeline(device, pipelines.shadowOffscreen, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
}

void VulkanExample::updateLight()
{
    float deltaTime = 0.01f * 0.5f;
    rotationAngle += rotationSpeed * deltaTime;
    if (rotationAngle > 2 * 3.1415926f) rotationAngle -= 2 * 3.1415926f;

    float newX = lightRadius * std::sin(rotationAngle);
    float newY = lightRadius * std::cos(rotationAngle);
    lightPos = glm::vec3(newX, newY, 3.f);
}

void VulkanExample::updateUniformBuffers()
{
    // depth uniform: 更新CPU端再映射到GPU缓存
    glm::mat4 depthProjectionMatrix = glm::ortho(-10.f, 10.f, -10.f, 10.f, shadowNearPlane, shadowFarPlane);
    glm::mat4 depthViewMatrix = glm::lookAt(lightPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 depthModelMatrix = glm::mat4(1.0f);
    uniformDataShadow.depthMVP = depthProjectionMatrix * depthViewMatrix * depthModelMatrix;
    memcpy(uniformBuffers[currentBuffer].shadowOffscreenBuffer.allocationInfo.pMappedData, &uniformDataShadow, sizeof(uniformDataShadow));
    vmaFlushAllocation(allocator, uniformBuffers[currentBuffer].shadowOffscreenBuffer.allocation, 0, sizeof(uniformDataShadow));

    // scene uniform
    uniformDataScene.projection = camera.matrices.perspective;
    uniformDataScene.view = camera.matrices.view;
    uniformDataScene.model = glm::mat4(1.0f);
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

    // shadow map
    vkutil::cmdTransitionImageLayout(commandBuffer, shadowMap.shadowTexture.image.image, shadowMap.shadowTexture.image.layout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    shadowMap.shadowTexture.image.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    drawShadowMap(commandBuffer);
    vkutil::cmdTransitionImageLayout(commandBuffer, shadowMap.shadowTexture.image.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    shadowMap.shadowTexture.image.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    vkutil::cmdTransitionImageLayout(commandBuffer, depthStencil.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    // TODO: 场景绘制


    vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[currentImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    //drawQuad(commandBuffer);
    drawScene(commandBuffer);
    vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[currentImageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
}

void VulkanExample::drawShadowMap(VkCommandBuffer commandBuffer)
{
    VkRenderingAttachmentInfo depthAttachment { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depthAttachment.imageView = shadowMap.shadowTexture.image.imageView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo { VK_STRUCTURE_TYPE_RENDERING_INFO };
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

// TODO: draw 和 shader & transition
