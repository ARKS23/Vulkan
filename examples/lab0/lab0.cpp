#include "lab0.h"
#include "vk_resources.h"

// 工具辅助函数
namespace {
    bool hasStencilComponent(VkFormat format) {
        return format == VK_FORMAT_D16_UNORM_S8_UINT ||
                format == VK_FORMAT_D24_UNORM_S8_UINT ||
                format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    VkImageAspectFlags getDepthAspectMask(VkFormat format) {
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilComponent(format)) {
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        return aspectMask;
    }
}


VulkanExample::VulkanExample() : VulkanExampleBase() {
    title = "My First Vulkan Example";
    settings.overlay = false;

    camera.type = Camera::CameraType::lookat;
    camera.setPosition(glm::vec3(0.0f, 0.0f, -2.5f));
    camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.setPerspective(45.0f, (float)width / (float)height, 1.0f, 256.0f);

    apiVersion = VK_API_VERSION_1_3;
    enabledFeatures.dynamicRendering = VK_TRUE;
    enabledFeatures.synchronization2 = VK_TRUE;
    deviceCreatepNextChain = &enabledFeatures;
}

VulkanExample::~VulkanExample() {
    if (device) {
        // 先释放依赖 offscreenColor 的 blit 管线与描述符布局，再释放离屏纹理本身。
        vkDestroyPipeline(device, blitPipeline, nullptr);
        vkDestroyPipelineLayout(device, blitPipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, blitDescriptorSetLayout, nullptr);
        vkutil::destroyTexture(device, allocator, offscreenColor);

        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        //colorTexture.destroy();
        vkutil::destroyTexture(device, allocator, baseColorTexture);
        vkutil::destroyAllocatedImage(device, allocator, depthImage);

        if (circleMeshBuffers.vertexBuffer.handle != VK_NULL_HANDLE) {
            vkutil::destroyAllocatedBuffer(allocator, circleMeshBuffers.vertexBuffer);
        }

        if (circleMeshBuffers.indexBuffer.handle != VK_NULL_HANDLE) {
            vkutil::destroyAllocatedBuffer(allocator, circleMeshBuffers.indexBuffer);
        }

        vkDestroyCommandPool(device, commandPool, nullptr);

        for (size_t i = 0; i < presentCompleteSemaphores.size(); i++) {
            vkDestroySemaphore(device, presentCompleteSemaphores[i], nullptr);
        }
        for (size_t i = 0; i < renderCompleteSemaphores.size(); i++) {
            vkDestroySemaphore(device, renderCompleteSemaphores[i], nullptr);
        }
        for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
            vkDestroyFence(device, waitFences[i], nullptr);
            vkutil::destroyAllocatedBuffer(allocator, uniformBuffersV2[i].buffer);
            uniformBuffersV2[i].mapped = nullptr;
        }

        destroyVmaAllocator();
    }
}

void VulkanExample::getEnabledFeatures() {
    if (deviceProperties.apiVersion < VK_API_VERSION_1_3) {
        vks::tools::exitFatal("Selected GPU does not support support Vulkan 1.3", VK_ERROR_INCOMPATIBLE_DRIVER);
	}
}

void VulkanExample::createVmaAllocator() {
    VmaAllocatorCreateInfo allocatorInfo {};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    VK_CHECK_RESULT(vmaCreateAllocator(&allocatorInfo, &allocator));
}

void VulkanExample::createSynchronizationPrimitives() {
    // Fence: 确保command buffer执行完成后才可以复用
    for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; ++i) {
        VkFenceCreateInfo fenceCI = vkinit::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
        VK_CHECK_RESULT(vkCreateFence(device, &fenceCI, nullptr, &waitFences[i]));
    }

    // present Semaphore
    presentCompleteSemaphores.resize(MAX_CONCURRENT_FRAMES);
    for (auto& semaphore : presentCompleteSemaphores) {
        VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphoreCreateInfo();
        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphore));
    }

    // render Semaphore: 这里的数量和vk-guide不同
    renderCompleteSemaphores.resize(swapChain.images.size());
    for (auto& semaphore : renderCompleteSemaphores) {
        VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphoreCreateInfo();
        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphore));
    }
}

void VulkanExample::createCommandBuffers() {
    // 所有 command buffer 都从同一个 command pool 分配
    VkCommandPoolCreateInfo commandPoolCI = vkinit::commandPoolCreateInfo(swapChain.queueNodeIndex, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    VK_CHECK_RESULT(vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool));
    // 从上面的 command pool 中为每个 in-flight frame 分配一个 primary command buffer
    VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, MAX_CONCURRENT_FRAMES);
	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, commandBuffers.data()));
}

void VulkanExample::createVertexBuffer() {
    MeshData circle = createCircleMesh(0.8f, 64);

    // 顶点数据
    const std::vector<Vertex> &vertices = circle.vertices;
    uint32_t vertexBufferSize = static_cast<uint32_t>(vertices.size()) * sizeof(Vertex);

    // 索引数据
    const std::vector<uint32_t> &indices = circle.indices;
    const uint32_t indexCount = static_cast<uint32_t>(indices.size());
    uint32_t indexBufferSize = indexCount * sizeof(uint32_t);

    // 处理顶点数据和索引数据的上传
    vkutil::ImmediateSubmitContext submitContext{device, queue, commandPool };
    circleMeshBuffers.vertexBuffer = vkutil::createDeviceLocalBuffer(allocator, submitContext, circle.vertices.data(), vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    circleMeshBuffers.indexBuffer = vkutil::createDeviceLocalBuffer(allocator, submitContext, circle.indices.data(), indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    circleMeshBuffers.indexCount = indexCount;
    circleMeshBuffers.indexType = VK_INDEX_TYPE_UINT32;
}

void VulkanExample::createUniformBuffers() {
    for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        uniformBuffersV2[i].buffer = vkutil::createAllocatedBuffer(allocator, sizeof(ShaderData), 
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_MEMORY_USAGE_AUTO
        );
        uniformBuffersV2[i].mapped = static_cast<uint8_t*>(uniformBuffersV2[i].buffer.allocationInfo.pMappedData);
    }
}

void VulkanExample::createDescriptors() {
    // descriptor pool 同时服务 scene pass 和 blit pass：
    // 每个 in-flight frame 一套场景 UBO/纹理描述符，额外再给全屏 blit 保留一个采样 offscreenColor 的 set。
    VkDescriptorPoolSize descriptorTypeCounts[2]{};
    descriptorTypeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorTypeCounts[0].descriptorCount = MAX_CONCURRENT_FRAMES;
    descriptorTypeCounts[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorTypeCounts[1].descriptorCount = MAX_CONCURRENT_FRAMES + 1;

    VkDescriptorPoolCreateInfo descriptorPoolCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    descriptorPoolCI.poolSizeCount = 2;
    descriptorPoolCI.pPoolSizes = descriptorTypeCounts;
    descriptorPoolCI.maxSets = MAX_CONCURRENT_FRAMES + 1;
	VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));

    // scene pass 的 set layout：binding 0 是相机/矩阵 UBO，binding 1 是物体基础纹理。
    std::array<VkDescriptorSetLayoutBinding, 2> layoutBindings{};
    layoutBindings[0].binding = 0;
    layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBindings[0].descriptorCount = 1;
    layoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layoutBindings[1].binding = 1;
    layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[1].descriptorCount = 1;
    layoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    descriptorLayoutCI.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    descriptorLayoutCI.pBindings = layoutBindings.data();
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayout));

    for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &uniformBuffersV2[i].descriptorSet));

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffersV2[i].buffer.handle;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ShaderData);

        VkDescriptorImageInfo imageInfo = baseColorTexture.descriptor;

        std::array<VkWriteDescriptorSet, 2> writeDescriptorSets{};
        writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSets[0].dstSet = uniformBuffersV2[i].descriptorSet;
        writeDescriptorSets[0].dstBinding = 0;
        writeDescriptorSets[0].descriptorCount = 1;
        writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptorSets[0].pBufferInfo = &bufferInfo;
        writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSets[1].dstSet = uniformBuffersV2[i].descriptorSet;
        writeDescriptorSets[1].dstBinding = 1;
        writeDescriptorSets[1].descriptorCount = 1;
        writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDescriptorSets[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(
            device, 
            static_cast<uint32_t>(writeDescriptorSets.size()), 
            writeDescriptorSets.data(), 
            0, 
            nullptr
        );
    }
}

void VulkanExample::createPipeline() {
    // 补充 push constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantData);

    VkPipelineLayoutCreateInfo pipelineLayoutCI = vkinit::pipelineLayoutCreateInfo();
    pipelineLayoutCI.setLayoutCount = 1;
    pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutCI.pushConstantRangeCount = 1;
    pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

    VkGraphicsPipelineCreateInfo pipelineCI{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.layout = pipelineLayout;

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Rasterization 状态
    VkPipelineRasterizationStateCreateInfo rasterizationStateCI{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
    rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationStateCI.depthClampEnable = VK_FALSE;
    rasterizationStateCI.rasterizerDiscardEnable = VK_FALSE;
    rasterizationStateCI.depthBiasEnable = VK_FALSE;
    rasterizationStateCI.lineWidth = 1.0f;

    // color blend
    VkPipelineColorBlendAttachmentState blendAttachmentState{};
    blendAttachmentState.colorWriteMask = 0xf;
    blendAttachmentState.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlendStateCI{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlendStateCI.attachmentCount = 1;
    colorBlendStateCI.pAttachments = &blendAttachmentState;

    // viewport / scissor
    VkPipelineViewportStateCreateInfo viewportStateCI{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportStateCI.viewportCount = 1;
    viewportStateCI.scissorCount = 1;

    // 开启 dynamic state
    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicStateCI{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
    dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

    // depth/stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencilStateCI.depthTestEnable = VK_TRUE;
    depthStencilStateCI.depthWriteEnable = VK_TRUE;
    depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencilStateCI.depthBoundsTestEnable = VK_FALSE;
    depthStencilStateCI.back.failOp = VK_STENCIL_OP_KEEP;
    depthStencilStateCI.back.passOp = VK_STENCIL_OP_KEEP;
    depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;
    depthStencilStateCI.stencilTestEnable = VK_FALSE;
    depthStencilStateCI.front = depthStencilStateCI.back;

    // MSAA
    VkPipelineMultisampleStateCreateInfo multisampleStateCI{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 顶点输入
    VkVertexInputBindingDescription vertexInputBinding{};
    vertexInputBinding.binding = 0;
    vertexInputBinding.stride = sizeof(Vertex);
    vertexInputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 3> vertexInputAttributs{};
    vertexInputAttributs[0].binding = 0;
    vertexInputAttributs[0].location = 0;
    vertexInputAttributs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexInputAttributs[0].offset = offsetof(Vertex, position);
    vertexInputAttributs[1].binding = 0;
    vertexInputAttributs[1].location = 1;
    vertexInputAttributs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexInputAttributs[1].offset = offsetof(Vertex, color);
    vertexInputAttributs[2].binding = 0;
    vertexInputAttributs[2].location = 2;
    vertexInputAttributs[2].format = VK_FORMAT_R32G32_SFLOAT;
    vertexInputAttributs[2].offset = offsetof(Vertex, uv);
    VkPipelineVertexInputStateCreateInfo vertexInputStateCI{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInputStateCI.vertexBindingDescriptionCount = 1;
    vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
    vertexInputStateCI.vertexAttributeDescriptionCount = 3;
    vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributs.data();

    // shader stages
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    // vertex
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = loadSPIRVShader(getShadersPath() + vertShaderPath);
    shaderStages[0].pName = "main";
    assert(shaderStages[0].module != VK_NULL_HANDLE);

    // fragment
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = loadSPIRVShader(getShadersPath() + fragShaderPath);
    shaderStages[1].pName = "main";
    assert(shaderStages[1].module != VK_NULL_HANDLE);

    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();

    // dynamic rendering 需要在 pipeline 创建时显式提供 attachment format 信息
    VkPipelineRenderingCreateInfoKHR pipelineRenderingCI{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
    pipelineRenderingCI.colorAttachmentCount = 1;
    pipelineRenderingCI.pColorAttachmentFormats = &swapChain.colorFormat;
    pipelineRenderingCI.depthAttachmentFormat = depthFormat;
    pipelineRenderingCI.stencilAttachmentFormat = depthFormat;

    pipelineCI.pVertexInputState = &vertexInputStateCI;
    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    pipelineCI.pRasterizationState = &rasterizationStateCI;
    pipelineCI.pColorBlendState = &colorBlendStateCI;
    pipelineCI.pMultisampleState = &multisampleStateCI;
    pipelineCI.pViewportState = &viewportStateCI;
    pipelineCI.pDepthStencilState = &depthStencilStateCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.pNext = &pipelineRenderingCI;

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));

    vkDestroyShaderModule(device, shaderStages[0].module, nullptr);
    vkDestroyShaderModule(device, shaderStages[1].module, nullptr);
}

void VulkanExample::prepare() {
    createVmaAllocator();
    VulkanExampleBase::prepare();

    // 资源创建顺序很关键：先准备可渲染的场景资源，再创建采样它们的描述符和管线。
    createSynchronizationPrimitives();
    createCommandBuffers();
    createVertexBuffer();
    createUniformBuffers();
    loadTexture();
    // offscreenColor 会被 blit descriptor 引用，所以要先于 createBlitDescriptors 创建。
    createOffscreenResources();
    createDescriptors();
    createPipeline();
    createBlitDescriptors();
    createBlitPipeline();
    prepared = true;
}

void VulkanExample::render() {
    vkWaitForFences(device, 1, &waitFences[currentFrame], VK_TRUE, UINT64_MAX);
    VK_CHECK_RESULT(vkResetFences(device, 1, &waitFences[currentFrame]));

    uint32_t imageIndex{ 0 };
    VkResult result = vkAcquireNextImageKHR(device, swapChain.swapChain, UINT64_MAX, presentCompleteSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        windowResize();
        return;
    } else if ((result != VK_SUCCESS) && (result != VK_SUBOPTIMAL_KHR)) {
        throw "Could not acquire the next swap chain image!";
    }

    // 每帧更新 camera / model 矩阵。UBO 使用持久映射，写完后 flush 给 GPU 可见。
    ShaderData shaderData{};
    shaderData.projectionMatrix = camera.matrices.perspective;
    shaderData.viewMatrix = camera.matrices.view;
    static float rotation{ 0.0f };
    //rotation += 0.05f;
    shaderData.modelMatrix = glm::rotate(glm::mat4(1.f), glm::radians(rotation), glm::vec3(1.0f, 0.0f, 0.0f));
    memcpy(uniformBuffersV2[currentFrame].mapped, &shaderData, sizeof(ShaderData));
    VK_CHECK_RESULT(vmaFlushAllocation(allocator, uniformBuffersV2[currentFrame].buffer.allocation, 0, sizeof(ShaderData)));

    // 本 sample 采用“两遍渲染”：先画到 offscreenColor，再用全屏三角形采样并输出到 swapchain。
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);
    VkCommandBufferBeginInfo cmdBufInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    const VkCommandBuffer commandBuffer = commandBuffers[currentFrame];
    VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufInfo));
    {
        // Pass 1: scene -> offscreenColor。
        // offscreenColor 上一帧结束时通常是 shader-read，本帧开画前要切回 attachment layout。
        vkutil::cmdTransitionImageLayout(commandBuffer, offscreenColor.image.image, offscreenColor.image.layout, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        offscreenColor.image.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		
        const VkImageAspectFlags depthAspectMask = getDepthAspectMask(depthFormat);
        vks::tools::insertImageMemoryBarrier(commandBuffer, depthImage.image, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 });

        VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		colorAttachment.imageView = offscreenColor.image.imageView;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.clearValue.color = { 0.11f, 0.10f, 0.13f, 0.0f };

        VkRenderingAttachmentInfo depthStencilAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		depthStencilAttachment.imageView = depthImage.imageView;
		depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthStencilAttachment.clearValue.depthStencil = { 1.0f,  0 };

        VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
		renderingInfo.renderArea = { 0, 0, width, height };
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;
		renderingInfo.pDepthAttachment = &depthStencilAttachment;
		renderingInfo.pStencilAttachment = &depthStencilAttachment;

        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        {
            VkViewport viewport{ 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

            VkRect2D scissor{ 0, 0, width, height };
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

            drawScene(commandBuffer, shaderData.modelMatrix);
        }
		vkCmdEndRendering(commandBuffer);

        // Pass 1 结束后，offscreenColor 从“被写入的附件”切换为“可被 fragment shader 采样的纹理”。
        vkutil::cmdTransitionImageLayout(commandBuffer, offscreenColor.image.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        offscreenColor.image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Pass 2: offscreenColor -> swapchain image。
        // 这里清空/写入的是最终要 present 的交换链图像，不再需要深度附件。
        vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

        VkRenderingAttachmentInfo presentColorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        presentColorAttachment.imageView = swapChain.imageViews[imageIndex];
        presentColorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        presentColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        presentColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        presentColorAttachment.clearValue.color = { 0.02f, 0.02f, 0.025f, 1.0f };

        VkRenderingInfo presentRenderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        presentRenderingInfo.renderArea = { 0, 0, width, height };
        presentRenderingInfo.layerCount = 1;
        presentRenderingInfo.colorAttachmentCount = 1;
        presentRenderingInfo.pColorAttachments = &presentColorAttachment;
        presentRenderingInfo.pDepthAttachment = nullptr;
        presentRenderingInfo.pStencilAttachment = nullptr;

        vkCmdBeginRendering(commandBuffer, &presentRenderingInfo);
        {
            VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            VkRect2D scissor{ { 0, 0 }, { width, height } };
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeline);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipelineLayout, 0, 1, &blitDescriptorSet, 0, nullptr);
            // fullscreen.vert 通过 SV_VertexID 生成全屏三角形，所以这里不绑定 vertex buffer。
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        }
        vkCmdEndRendering(commandBuffer);

        // 交给 present queue 前必须切到 PRESENT_SRC_KHR。
        vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[imageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.pWaitDstStageMask = &waitStageMask;      // 等待 semaphore 时所处的 pipeline stage
    submitInfo.pCommandBuffers = &commandBuffer;		// 本次提交要执行的 command buffer
    submitInfo.commandBufferCount = 1;                  // 这里只提交一个 command buffer

    // 执行 command buffer 前，需要先等待 image acquire 完成
    submitInfo.pWaitSemaphores = &presentCompleteSemaphores[currentFrame];
    submitInfo.waitSemaphoreCount = 1;
    // command buffer 执行结束后，发出 render complete 信号
    submitInfo.pSignalSemaphores = &renderCompleteSemaphores[imageIndex];
    submitInfo.signalSemaphoreCount = 1;

    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, waitFences[currentFrame]));

    VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderCompleteSemaphores[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain.swapChain;
    presentInfo.pImageIndices = &imageIndex;
    result = vkQueuePresentKHR(queue, &presentInfo);
    if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
        windowResize();
    } else if (result != VK_SUCCESS) {
        throw "Could not present the image to the swap chain!";
    }

    // 切换到下一帧对应的资源槽位
    currentFrame = (currentFrame + 1) % MAX_CONCURRENT_FRAMES;
}

void VulkanExample::setupDepthStencil() {
    vkutil::destroyAllocatedImage(device, allocator, depthImage); // 用于支持resize

    const VkImageAspectFlags aspectMask = getDepthAspectMask(depthFormat);
    depthImage = vkutil::createAllocatedImage(
        device, 
        allocator, 
        VkExtent3D{width, height, 1}, 
        depthFormat, 
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        aspectMask
    );

    depthImage.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    // 基类 depthStencil 由 VulkanExampleBase 手动销毁。
    // Lab0 的 VMA depth image 不交给基类管理，避免 resize/析构时错误释放。
    depthStencil.image = VK_NULL_HANDLE;
    depthStencil.view = VK_NULL_HANDLE;
    depthStencil.memory = VK_NULL_HANDLE;
}

VkShaderModule VulkanExample::loadSPIRVShader(const std::string& filename) {
    	size_t shaderSize;
		char* shaderCode{ nullptr };

#if defined(__ANDROID__)
		// 从 Android 压缩资源中加载 shader
		AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		assert(asset);
		shaderSize = AAsset_getLength(asset);
		assert(shaderSize > 0);

		shaderCode = new char[shaderSize];
		AAsset_read(asset, shaderCode, shaderSize);
		AAsset_close(asset);
#else
		std::ifstream is(filename, std::ios::binary | std::ios::in | std::ios::ate);

		if (is.is_open()) {
			shaderSize = is.tellg();
			is.seekg(0, std::ios::beg);
			// 把文件内容读进一块内存
			shaderCode = new char[shaderSize];
			is.read(shaderCode, shaderSize);
			is.close();
			assert(shaderSize > 0);
		}
#endif
		if (shaderCode) {
			// 创建 shader module，后续 pipeline 创建时会使用它
			VkShaderModuleCreateInfo shaderModuleCI{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			shaderModuleCI.codeSize = shaderSize;
			shaderModuleCI.pCode = (uint32_t*)shaderCode;

			VkShaderModule shaderModule;
			VK_CHECK_RESULT(vkCreateShaderModule(device, &shaderModuleCI, nullptr, &shaderModule));

			delete[] shaderCode;

			return shaderModule;
		} else {
			std::cerr << "Error: Could not open shader file \"" << filename << "\"" << std::endl;
			return VK_NULL_HANDLE;
		}
}

VulkanExample::MeshData VulkanExample::createCircleMesh(float radius, uint32_t segmentCount) {
    MeshData mesh{};
    mesh.vertices.push_back({ { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.5f, 0.5f } });

    for (uint32_t i = 0; i < segmentCount; i++) {
        float angle = 2.0f * glm::pi<float>() * static_cast<float>(i) /
        static_cast<float>(segmentCount);
        float x = radius * std::cos(angle);
        float y = radius * std::sin(angle);
        float t = static_cast<float>(i) / static_cast<float>(segmentCount);
        float u = x / (2.0f * radius) + 0.5f;
        float v = y / (2.0f * radius) + 0.5f;
        mesh.vertices.push_back({ { x, y, 0.0f }, { t, 1.0f - t, 0.33f }, { u, v } });
    }

    for (uint32_t i = 0; i < segmentCount; i++) {
        uint32_t current = i + 1;
        uint32_t next = (i + 1) % segmentCount + 1;
        mesh.indices.push_back(0);
        mesh.indices.push_back(current);
        mesh.indices.push_back(next);
    }

    return mesh;
}

void VulkanExample::destroyVmaAllocator() {
    if (allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }
}

void VulkanExample::loadTexture() {
    constexpr uint32_t textureWidth = 256;
    constexpr uint32_t textureHeight = 256;
    constexpr uint32_t channelCount = 4;
    constexpr uint32_t checkerSize = 32;

    std::vector<uint8_t> pixels(textureWidth * textureHeight * channelCount);
    for (uint32_t y = 0; y < textureHeight; y++) {
        for (uint32_t x = 0; x < textureWidth; x++) {
            const bool checker = ((x / checkerSize) + (y / checkerSize)) % 2 == 0;
            const size_t pixelIndex = static_cast<size_t>(y * textureWidth + x) * channelCount;
            pixels[pixelIndex + 0] = checker ? 230 : 35;
            pixels[pixelIndex + 1] = checker ? 210 : 45;
            pixels[pixelIndex + 2] = checker ? 90 : 120;
            pixels[pixelIndex + 3] = 255;
        }
    }

    vkutil::ImmediateSubmitContext submitContext{device, queue, commandPool };


    baseColorTexture = vkutil::createTexture2DFromPixels(device ,allocator, submitContext, pixels.data(), textureWidth, textureHeight, VK_FORMAT_R8G8B8A8_UNORM);
}

void VulkanExample::createOffscreenResources() {
    // resize 或首次创建时都走这里；先清理旧资源，避免 image/view/sampler 泄漏。
    destroyOffscreenResources();

    // offscreenColor 同时承担两种角色：scene pass 的颜色附件，以及 blit pass 的输入纹理。
    offscreenColor.image = vkutil::createAllocatedImage(
        device, 
        allocator, 
        VkExtent3D{width, height, 1}, 
        swapChain.colorFormat, 
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    VkSamplerCreateInfo samplerCI{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerCI.magFilter = VK_FILTER_LINEAR;
    samplerCI.minFilter = VK_FILTER_LINEAR;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = 0.0f;
    samplerCI.maxAnisotropy = 1.0f;
    samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &offscreenColor.sampler));

    offscreenColor.descriptor.sampler = offscreenColor.sampler;
    offscreenColor.descriptor.imageView = offscreenColor.image.imageView;
    // descriptor 记录的是“采样时”的 layout；真正的 layout 切换在 render() 的 command buffer 中完成。
    offscreenColor.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void VulkanExample::destroyOffscreenResources() {
    // vkutil::destroyTexture 会在内部清空句柄，重复调用时也能安全地处理空资源。
    vkutil::destroyTexture(device, allocator, offscreenColor);
}

void VulkanExample::createBlitDescriptors() {
    // blit pass 只需要一个 combined image sampler：从 offscreenColor 读取上一遍渲染结果。
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    descriptorLayoutCI.bindingCount = 1;
    descriptorLayoutCI.pBindings = &binding;
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &blitDescriptorSetLayout));

    VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &blitDescriptorSetLayout;
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &blitDescriptorSet));

    updateBlitDescriptor();
}

void VulkanExample::updateBlitDescriptor() {
    // resize 后 offscreen imageView 会变，descriptor set 也必须重新指向新的 view。
    VkDescriptorImageInfo imageInfo = offscreenColor.descriptor;

    VkWriteDescriptorSet writeDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writeDescriptorSet.dstSet = blitDescriptorSet;
    writeDescriptorSet.dstBinding = 0;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
}

void VulkanExample::createBlitPipeline() {
    // blit pipeline 是一个极简全屏 pass：无顶点输入、无深度测试，只采样 offscreenColor 并写入当前颜色附件。
    VkPipelineLayoutCreateInfo layoutCI = vkinit::pipelineLayoutCreateInfo();
    layoutCI.setLayoutCount = 1;
    layoutCI.pSetLayouts = &blitDescriptorSetLayout;
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &layoutCI, nullptr, &blitPipelineLayout));

    VkGraphicsPipelineCreateInfo graphicsPipelineCI{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    graphicsPipelineCI.layout = blitPipelineLayout;

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineVertexInputStateCreateInfo vertexInputStateCI{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineRasterizationStateCreateInfo rasterizationStateCI{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
    rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationStateCI.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState blendAttachmentState{};
    blendAttachmentState.colorWriteMask = 0xf;
    blendAttachmentState.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlendStateCI{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlendStateCI.attachmentCount = 1;
    colorBlendStateCI.pAttachments = &blendAttachmentState;

    VkPipelineViewportStateCreateInfo viewportStateCI{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportStateCI.viewportCount = 1;
    viewportStateCI.scissorCount = 1;

    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicStateCI{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
    dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencilStateCI.depthTestEnable = VK_FALSE;
    depthStencilStateCI.depthWriteEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampleStateCI{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // shader stages
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    // vertex
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = loadSPIRVShader(getShadersPath() + blitVertShaderPath);
    shaderStages[0].pName = "main";
    assert(shaderStages[0].module != VK_NULL_HANDLE);
    // fragment
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = loadSPIRVShader(getShadersPath() + blitFragShaderPath);
    shaderStages[1].pName = "main";
    assert(shaderStages[1].module != VK_NULL_HANDLE);

    // dynamic rendering 需要在 pipeline 创建时显式提供 attachment format 信息。
    // 这个 pass 直接写 swapchain，因此颜色格式使用 swapChain.colorFormat，深度/模板格式留空。
    VkPipelineRenderingCreateInfoKHR pipelineRenderingCI{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
    pipelineRenderingCI.colorAttachmentCount = 1;
    pipelineRenderingCI.pColorAttachmentFormats = &swapChain.colorFormat;
    pipelineRenderingCI.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    pipelineRenderingCI.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    graphicsPipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    graphicsPipelineCI.pStages = shaderStages.data();
    graphicsPipelineCI.pVertexInputState = &vertexInputStateCI;
    graphicsPipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
    graphicsPipelineCI.pRasterizationState = &rasterizationStateCI;
    graphicsPipelineCI.pColorBlendState = &colorBlendStateCI;
    graphicsPipelineCI.pMultisampleState = &multisampleStateCI;
    graphicsPipelineCI.pViewportState = &viewportStateCI;
    graphicsPipelineCI.pDepthStencilState = &depthStencilStateCI;
    graphicsPipelineCI.pDynamicState = &dynamicStateCI;
    graphicsPipelineCI.pNext = &pipelineRenderingCI;
    
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &graphicsPipelineCI, nullptr, &blitPipeline));
    vkDestroyShaderModule(device, shaderStages[0].module, nullptr);
    vkDestroyShaderModule(device, shaderStages[1].module, nullptr);
}

void VulkanExample::drawScene(VkCommandBuffer commandBuffer, const glm::mat4& baseModelMatrix) {
    // drawScene 只关心“怎么画场景”，不关心画到哪里；目标附件由外层 vkCmdBeginRendering 决定。
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &uniformBuffersV2[currentFrame].descriptorSet, 0, nullptr);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkDeviceSize offsets[1]{ 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &circleMeshBuffers.vertexBuffer.handle, offsets);
    vkCmdBindIndexBuffer(commandBuffer, circleMeshBuffers.indexBuffer.handle, 0, circleMeshBuffers.indexType);

    PushConstantData pushConstantData{};
    pushConstantData.modelMatrix = baseModelMatrix;
    pushConstantData.colorMultiplier = glm::vec4(1.0f, 1.0f, 0.5f, 1.0f);
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantData), &pushConstantData);
    vkCmdDrawIndexed(commandBuffer, circleMeshBuffers.indexCount, 1, 0, 0, 0);

    pushConstantData.modelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 1.5f, 2.0f)) * baseModelMatrix;
    pushConstantData.colorMultiplier = glm::vec4(0.5f, 2.0f, 1.0f, 1.0f);
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantData), &pushConstantData);
    vkCmdDrawIndexed(commandBuffer, circleMeshBuffers.indexCount, 1, 0, 0, 0);
}

void VulkanExample::windowResized() {
    // 基类已经处理 swapchain 尺寸变化；这里补齐与窗口尺寸绑定的 offscreenColor，并刷新 blit descriptor。
    destroyOffscreenResources();
    createOffscreenResources();
    updateBlitDescriptor();
}

