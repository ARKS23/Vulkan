#include "lab0.h"
#include "vk_resources.h"


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
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

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

uint32_t VulkanExample::getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties) {
    // 遍历当前设备可用的全部内存类型
    for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; i++) {
        if ((typeBits & 1) == 1) {
            if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        typeBits >>= 1;
    }
    throw "Could not find a suitable memory type!";
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
    VkDescriptorPoolSize descriptorTypeCounts[1]{};
    descriptorTypeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorTypeCounts[0].descriptorCount = MAX_CONCURRENT_FRAMES;

    VkDescriptorPoolCreateInfo descriptorPoolCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    descriptorPoolCI.poolSizeCount = 1;
    descriptorPoolCI.pPoolSizes = descriptorTypeCounts;
    descriptorPoolCI.maxSets = MAX_CONCURRENT_FRAMES;
	VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));

    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBinding.descriptorCount = 1;
    layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    descriptorLayoutCI.bindingCount = 1;
    descriptorLayoutCI.pBindings = &layoutBinding;
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayout));

    for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &uniformBuffersV2[i].descriptorSet));

        VkWriteDescriptorSet writeDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffersV2[i].buffer.handle;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ShaderData);

        writeDescriptorSet.dstSet = uniformBuffersV2[i].descriptorSet;
        writeDescriptorSet.descriptorCount = 1;
        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeDescriptorSet.pBufferInfo = &bufferInfo;
        writeDescriptorSet.dstBinding = 0;
        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
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
    std::array<VkVertexInputAttributeDescription, 2> vertexInputAttributs{};
    vertexInputAttributs[0].binding = 0;
    vertexInputAttributs[0].location = 0;
    vertexInputAttributs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexInputAttributs[0].offset = offsetof(Vertex, position);
    vertexInputAttributs[1].binding = 0;
    vertexInputAttributs[1].location = 1;
    vertexInputAttributs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexInputAttributs[1].offset = offsetof(Vertex, color);
    VkPipelineVertexInputStateCreateInfo vertexInputStateCI{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInputStateCI.vertexBindingDescriptionCount = 1;
    vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
    vertexInputStateCI.vertexAttributeDescriptionCount = 2;
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
    VulkanExampleBase::prepare();

    createVmaAllocator();
    createSynchronizationPrimitives();
    createCommandBuffers();
    createVertexBuffer();
    createUniformBuffers();
    createDescriptors();
    createPipeline();
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

    ShaderData shaderData{};
    shaderData.projectionMatrix = camera.matrices.perspective;
    shaderData.viewMatrix = camera.matrices.view;
    static float rotation{ 0.0f };
    //rotation += 0.05f;
    shaderData.modelMatrix = glm::rotate(glm::mat4(1.f), glm::radians(rotation), glm::vec3(1.0f, 0.0f, 0.0f));
    memcpy(uniformBuffersV2[currentFrame].mapped, &shaderData, sizeof(ShaderData));
    VK_CHECK_RESULT(vmaFlushAllocation(allocator, uniformBuffersV2[currentFrame].buffer.allocation, 0, sizeof(ShaderData)));

    vkResetCommandBuffer(commandBuffers[currentFrame], 0);
    VkCommandBufferBeginInfo cmdBufInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    const VkCommandBuffer commandBuffer = commandBuffers[currentFrame];
    VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufInfo));
    {
        vks::tools::insertImageMemoryBarrier(commandBuffer, swapChain.images[imageIndex], 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
		vks::tools::insertImageMemoryBarrier(commandBuffer, depthStencil.image, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 });

        VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		colorAttachment.imageView = swapChain.imageViews[imageIndex];
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.clearValue.color = { 0.11f, 0.10f, 0.13f, 0.0f };

        VkRenderingAttachmentInfo depthStencilAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		depthStencilAttachment.imageView = depthStencil.view;
		depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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

        VkViewport viewport{ 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{ 0, 0, width, height };
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &uniformBuffersV2[currentFrame].descriptorSet, 0, nullptr);
		
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        // push constants
        PushConstantData pushConstantData{};
        pushConstantData.modelMatrix = shaderData.modelMatrix;
        pushConstantData.colorMultiplier = glm::vec4(1.0f, 1.0f, 0.5f, 1.0f);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantData), &pushConstantData);
        
        VkDeviceSize offsets[1]{ 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &circleMeshBuffers.vertexBuffer.handle, offsets);
		
		vkCmdBindIndexBuffer(commandBuffer, circleMeshBuffers.indexBuffer.handle, 0, circleMeshBuffers.indexType);
		
		vkCmdDrawIndexed(commandBuffer, circleMeshBuffers.indexCount, 1, 0, 0, 0);

        // per draw push constants update
        pushConstantData.modelMatrix = glm::translate(pushConstantData.modelMatrix, glm::vec3(1.5f, 1.5f, 2.0f));
        pushConstantData.colorMultiplier = glm::vec4(0.5f, 2.0f, 1.0f, 1.0f);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantData), &pushConstantData);
        vkCmdDrawIndexed(commandBuffer, circleMeshBuffers.indexCount, 1, 0, 0, 0);
		
		vkCmdEndRendering(commandBuffer);

        vks::tools::insertImageMemoryBarrier(commandBuffer, swapChain.images[imageIndex], VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_NONE, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
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
    // 创建一个 optimal tiled 的 image，作为深度/模板附件使用
    VkImageCreateInfo imageCI{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = depthFormat;
    imageCI.extent = { width, height, 1 };
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &depthStencil.image));

    // 为该 image 分配 device local 内存，并绑定到 image 上
    VkMemoryAllocateInfo memAlloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, depthStencil.image, &memReqs);
    memAlloc.allocationSize = memReqs.size;
    memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &depthStencil.memory));
    VK_CHECK_RESULT(vkBindImageMemory(device, depthStencil.image, depthStencil.memory, 0));

    VkImageViewCreateInfo depthStencilViewCI{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    depthStencilViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthStencilViewCI.format = depthFormat;
    depthStencilViewCI.subresourceRange = {};
    depthStencilViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    // 只有 depth+stencil 格式才需要把 stencil aspect 也包含进来
    if (depthFormat >= VK_FORMAT_D16_UNORM_S8_UINT) {
        depthStencilViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    depthStencilViewCI.subresourceRange.baseMipLevel = 0;
    depthStencilViewCI.subresourceRange.levelCount = 1;
    depthStencilViewCI.subresourceRange.baseArrayLayer = 0;
    depthStencilViewCI.subresourceRange.layerCount = 1;
    depthStencilViewCI.image = depthStencil.image;
    VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilViewCI, nullptr, &depthStencil.view));
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
    mesh.vertices.push_back({ { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });

    for (uint32_t i = 0; i < segmentCount; i++) {
        float angle = 2.0f * glm::pi<float>() * static_cast<float>(i) /
        static_cast<float>(segmentCount);
        float x = radius * std::cos(angle);
        float y = radius * std::sin(angle);
        float t = static_cast<float>(i) / static_cast<float>(segmentCount);
        mesh.vertices.push_back({ { x, y, 0.0f }, { t, 1.0f - t, 0.33f } });
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
