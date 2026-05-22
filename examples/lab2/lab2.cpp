#include "lab2.h"
#include "vk_descriptors.h"
#include "vk_rendering.h"
#include "VulkanTools.h"

VulkanExample::VulkanExample() : VulkanExampleBase() {
    title = "Lab2 : PBR";

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

    materials.push_back(Material("Gold", glm::vec3(1.0f, 0.765557f, 0.336057f), 0.1f, 1.0f));
    materials.push_back(Material("Copper", glm::vec3(0.955008f, 0.637427f, 0.538163f), 0.1f, 1.0f));
    materials.push_back(Material("Chromium", glm::vec3(0.549585f, 0.556114f, 0.554256f), 0.1f, 1.0f));
    materials.push_back(Material("Nickel", glm::vec3(0.659777f, 0.608679f, 0.525649f), 0.1f, 1.0f));
    materials.push_back(Material("Titanium", glm::vec3(0.541931f, 0.496791f, 0.449419f), 0.1f, 1.0f));
    materials.push_back(Material("Cobalt", glm::vec3(0.662124f, 0.654864f, 0.633732f), 0.1f, 1.0f));
    materials.push_back(Material("Platinum", glm::vec3(0.672411f, 0.637331f, 0.585456f), 0.1f, 1.0f));

    materials.push_back(Material("White", glm::vec3(1.0f), 0.1f, 1.0f));
    materials.push_back(Material("Red", glm::vec3(1.0f, 0.0f, 0.0f), 0.1f, 1.0f));
    materials.push_back(Material("Blue", glm::vec3(0.0f, 0.0f, 1.0f), 0.1f, 1.0f));
    materials.push_back(Material("Black", glm::vec3(0.0f), 0.1f, 1.0f));

    for (const Material& mat : materials) {
        materialNames.push_back(mat.name);
    }
}

void VulkanExample::OnUpdateUIOverlay(vks::UIOverlay *overlay) {
    if (overlay->header("Settings")) {
        overlay->comboBox("Model", &objectIndex, objectNames);
        overlay->comboBox("Material", &materialIndex, materialNames);
    }
}

VulkanExample::~VulkanExample() {
    if (device) {
        destroyPipelines();
        destroyDescriptors();
        destroyUniformBuffers();
        destroyAssets();
        destroyVmaAllocator();
    }
}

void VulkanExample::prepare() {
    createVmaAllocator();
    VulkanExampleBase::prepare();
    loadAssets();
    createDescriptorsPool();

    generateIrradianceCubeMap();
    generatePrefilteredCubeMap();
    generateBRDFLUT();

    createUniformBuffers();
    setupDescriptors();
    createPipelines();

    prepared = true;
}

void VulkanExample::createVmaAllocator() {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.device = device;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.instance = instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    VK_CHECK_RESULT(vmaCreateAllocator(&allocatorInfo, &allocator));
}

void VulkanExample::destroyVmaAllocator() {
    if (allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }
}

void VulkanExample::loadAssets() {
    objectNames = {"Sphere", "Teapot", "Torusknot", "Venus"};
    std::vector<std::string> filenames = { "sphere.gltf", "teapot.gltf", "torusknot.gltf", "venus.gltf" };
    objects.resize(filenames.size());
    for (size_t i = 0; i < filenames.size(); ++i) {
        std::string path = getAssetPath() + "models/" + filenames[i];
        objects[i].loadFromFile(path, vulkanDevice, queue, vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY);
    }

    // 光源物体模型
    lightObject.loadFromFile(getAssetPath() + "models/sphere.gltf", vulkanDevice, queue, vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY);

    // IBL 天空盒纹理和顶点
    textures.environmentCubeMap.loadFromFile(getAssetPath() + hdrFilePath, VK_FORMAT_R16G16B16A16_SFLOAT, vulkanDevice, queue);
    skyboxCube.loadFromFile(getAssetPath() + "models/cube.gltf", vulkanDevice, queue, vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY);
}

void VulkanExample::destroyAssets() {
    textures.environmentCubeMap.destroy();
    vkutil::destroyAllocatedCubeTexture(device, allocator, textures.irradianceCubeMap);
    vkutil::destroyAllocatedCubeTexture(device, allocator, textures.prefilteredCubeMap);
    vkutil::destroyTexture(device, allocator, textures.brdfLUT);
}

void VulkanExample::createUniformBuffers() {
    for (UniformBuffers& buffer : uniformBuffersScene) {
        buffer.matricesBuffer = vkutil::createAllocatedBuffer(
            allocator, 
            sizeof(UniformDataMatrices),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_MEMORY_USAGE_AUTO
        );

        buffer.lightBuffer = vkutil::createAllocatedBuffer(
            allocator, 
            sizeof(UniformDataLights),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_MEMORY_USAGE_AUTO
        );

        buffer.lightSourceMatricesBuffer = vkutil::createAllocatedBuffer(
            allocator, 
            sizeof(UniformDataMatrices),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_MEMORY_USAGE_AUTO
        );

        buffer.skyBoxMatricesBuffer = vkutil::createAllocatedBuffer(
            allocator, 
            sizeof(UniformDataSkyBox),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_MEMORY_USAGE_AUTO
        );
    }
}

void VulkanExample::destroyUniformBuffers() {
    for (UniformBuffers& buffer : uniformBuffersScene) {
        vkutil::destroyAllocatedBuffer(allocator, buffer.matricesBuffer);
        vkutil::destroyAllocatedBuffer(allocator, buffer.lightBuffer);
        vkutil::destroyAllocatedBuffer(allocator, buffer.lightSourceMatricesBuffer);
        vkutil::destroyAllocatedBuffer(allocator, buffer.skyBoxMatricesBuffer);
    }
}

void VulkanExample::createDescriptorsPool() {
    // Pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        vkutil::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames * 10),
        vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames * 10)
    };
    VkDescriptorPoolCreateInfo poolCI = vkutil::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames * 10);
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolCI, nullptr, &descriptorPool));
}

void VulkanExample::setupDescriptors() {
    // -------------------------------------------------------- Layout --------------------------------------------------------
    std::vector<VkDescriptorSetLayoutBinding> bindingLayout = { // 场景管线layout
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT , 1),
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT , 2), // irradiance map
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT , 3), // prefilter map
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT , 4)  // brdf LUT
    };
    VkDescriptorSetLayoutCreateInfo layoutCI = vkutil::descriptorSetLayoutCreateInfo(bindingLayout);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &descriptorSetLayouts.sceneDescriptorSetLayout));

    std::vector<VkDescriptorSetLayoutBinding> skyboxLayout = {  // 天空盒管线layout
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
    };
    VkDescriptorSetLayoutCreateInfo skyboxLayoutCI = vkutil::descriptorSetLayoutCreateInfo(skyboxLayout);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &skyboxLayoutCI, nullptr, &descriptorSetLayouts.skyboxDescriptorSetLayout));

    std::vector<VkDescriptorSetLayoutBinding> lightBindingLayout = {   // 光源管线layout
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0)
    };
    VkDescriptorSetLayoutCreateInfo lightLayoutCI = vkutil::descriptorSetLayoutCreateInfo(lightBindingLayout);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &lightLayoutCI, nullptr, &descriptorSetLayouts.lightDescriptorSetLayout));

    // -------------------------------------------------------- Alloc Info --------------------------------------------------------
    VkDescriptorSetAllocateInfo allocInfo = vkutil::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.sceneDescriptorSetLayout, 1);
    VkDescriptorSetAllocateInfo skyboxAllocInfo = vkutil::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.skyboxDescriptorSetLayout, 1);
    VkDescriptorSetAllocateInfo LightAllocInfo = vkutil::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.lightDescriptorSetLayout, 1);

    // -------------------------------------------------------- set & write & update --------------------------------------------------------
    for (int i = 0; i < uniformBuffersScene.size(); ++i) {
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i].sceneDescriptor));
        VkDescriptorBufferInfo matricesBufferInfo = vkutil::descriptorBufferInfo(uniformBuffersScene[i].matricesBuffer.handle, sizeof(UniformDataMatrices), 0);
        VkDescriptorBufferInfo lightBufferInfo = vkutil::descriptorBufferInfo(uniformBuffersScene[i].lightBuffer.handle, sizeof(UniformDataLights), 0);
        VkDescriptorImageInfo irradianceImageInfo = vkutil::descriptorImageInfo(textures.irradianceCubeMap.sampler, textures.irradianceCubeMap.view, textures.irradianceCubeMap.layout);
        VkDescriptorImageInfo prefilterImageInfo = vkutil::descriptorImageInfo(textures.prefilteredCubeMap.sampler, textures.prefilteredCubeMap.view, textures.prefilteredCubeMap.layout);
        VkDescriptorImageInfo brdfLUTImageInfo = vkutil::descriptorImageInfo(textures.brdfLUT.descriptor.sampler, textures.brdfLUT.descriptor.imageView, textures.brdfLUT.descriptor.imageLayout);
        std::vector<VkWriteDescriptorSet> writes = {
            vkutil::writeUniformBuffer(descriptorSets[i].sceneDescriptor, 0, &matricesBufferInfo),
            vkutil::writeUniformBuffer(descriptorSets[i].sceneDescriptor, 1, &lightBufferInfo),
            vkutil::writeCombinedImageSampler(descriptorSets[i].sceneDescriptor, 2, &irradianceImageInfo),
            vkutil::writeCombinedImageSampler(descriptorSets[i].sceneDescriptor, 3, &prefilterImageInfo),
            vkutil::writeCombinedImageSampler(descriptorSets[i].sceneDescriptor, 4, &brdfLUTImageInfo)
        };
        vkutil::updateDescriptorSet(device, writes);

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &skyboxAllocInfo, &descriptorSets[i].skyboxDescriptor));
        VkDescriptorBufferInfo skyboxMatricesBufferInfo = vkutil::descriptorBufferInfo(uniformBuffersScene[i].skyBoxMatricesBuffer.handle, sizeof(UniformDataSkyBox), 0);
        //VkDescriptorImageInfo skyboxImageInfo = vkutil::descriptorImageInfo(textures.irradianceCubeMap.sampler, textures.irradianceCubeMap.view, textures.irradianceCubeMap.descriptor.imageLayout); // irradiance map测试
        VkDescriptorImageInfo skyboxImageInfo = vkutil::descriptorImageInfo(textures.environmentCubeMap.sampler, textures.environmentCubeMap.view, textures.environmentCubeMap.imageLayout);
        std::vector<VkWriteDescriptorSet> skyboxWrites = {
            vkutil::writeUniformBuffer(descriptorSets[i].skyboxDescriptor, 0, &skyboxMatricesBufferInfo),
            vkutil::writeCombinedImageSampler(descriptorSets[i].skyboxDescriptor, 1, &skyboxImageInfo)
        };
        vkutil::updateDescriptorSet(device, skyboxWrites);

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &LightAllocInfo, &descriptorSets[i].lightDescriptor));
        VkDescriptorBufferInfo lightMatricesBufferInfo = vkutil::descriptorBufferInfo(uniformBuffersScene[i].lightSourceMatricesBuffer.handle, sizeof(UniformDataMatrices), 0);
        std::vector<VkWriteDescriptorSet> lightWrites = {
            vkutil::writeUniformBuffer(descriptorSets[i].lightDescriptor, 0, &lightMatricesBufferInfo)
        };
        vkutil::updateDescriptorSet(device, lightWrites);
    }
}

void VulkanExample::destroyDescriptors() {
    if (descriptorSetLayouts.sceneDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.sceneDescriptorSetLayout, nullptr);
        descriptorSetLayouts.sceneDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (descriptorSetLayouts.lightDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.lightDescriptorSetLayout, nullptr);
        descriptorSetLayouts.lightDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (descriptorSetLayouts.skyboxDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.skyboxDescriptorSetLayout, nullptr);
        descriptorSetLayouts.skyboxDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanExample::createPipelines() {
    createScenePipelineLayout();
    createScenePipeline();
    createSkyboxPipelineLayout();
    createSkyboxPipeline();
    createLightPipelineLayout();
    createLightPipeline();
}

void VulkanExample::destroyPipelines() {
    if (pipelines.scenePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipelines.scenePipeline, nullptr);
        pipelines.scenePipeline = VK_NULL_HANDLE;
    }
    if (pipelinesLayout.scenePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelinesLayout.scenePipelineLayout, nullptr);
        pipelinesLayout.scenePipelineLayout = VK_NULL_HANDLE;
    }

    if (pipelines.skyboxPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipelines.skyboxPipeline, nullptr);
        pipelines.skyboxPipeline = VK_NULL_HANDLE;
    }
    if (pipelinesLayout.skyboxPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelinesLayout.skyboxPipelineLayout, nullptr);
        pipelinesLayout.skyboxPipelineLayout = VK_NULL_HANDLE;
    }

    if (pipelines.lightPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipelines.lightPipeline, nullptr);
        pipelines.lightPipeline = VK_NULL_HANDLE;
    }
    if (pipelinesLayout.lightPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelinesLayout.lightPipelineLayout, nullptr);
        pipelinesLayout.lightPipelineLayout = VK_NULL_HANDLE;
    }
}

void VulkanExample::createScenePipelineLayout() {
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.sceneDescriptorSetLayout, 1);
    std::vector<VkPushConstantRange> pushConstantRanges = {
        vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::vec3), 0), // 位置
        vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Material::PushBlock), sizeof(glm::vec3)) // 材质参数，需要注意这里的偏移量 
    };
    pipelineLayoutCreateInfo.pushConstantRangeCount = 2;
    pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelinesLayout.scenePipelineLayout));
}

void VulkanExample::createScenePipeline() {
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =  vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
    VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
    VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
    VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
    VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1);
    VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{
        loadShader(getShadersPath() + pbrSceneVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
        loadShader(getShadersPath() + pbrSceneFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT)
    };

    VkPipelineRenderingCreateInfo renderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &swapChain.colorFormat;
    renderingCreateInfo.depthAttachmentFormat = depthFormat;
    renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.pNext = &renderingCreateInfo;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.pInputAssemblyState = &inputAssemblyState;
    pipelineCI.pRasterizationState = &rasterizationState;
    pipelineCI.pColorBlendState = &colorBlendState;
    pipelineCI.pDepthStencilState = &depthStencilState;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pMultisampleState = &multisampleState;
    pipelineCI.pDynamicState = &dynamicState;
    pipelineCI.layout = pipelinesLayout.scenePipelineLayout;
    pipelineCI.renderPass = renderPass;
    pipelineCI.subpass = 0;
    pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal });

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.scenePipeline));
}

void VulkanExample::createSkyboxPipelineLayout() {
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.skyboxDescriptorSetLayout, 1);
     VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelinesLayout.skyboxPipelineLayout));
}

void VulkanExample::createSkyboxPipeline() {
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =  vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
    VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
    VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
    VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
    VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1);
    VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{
        loadShader(getShadersPath() + skyboxVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
        loadShader(getShadersPath() + skyboxFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT)
    };

    VkPipelineRenderingCreateInfo renderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &swapChain.colorFormat;
    renderingCreateInfo.depthAttachmentFormat = depthFormat;
    renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.pNext = &renderingCreateInfo;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.pInputAssemblyState = &inputAssemblyState;
    pipelineCI.pRasterizationState = &rasterizationState;
    pipelineCI.pColorBlendState = &colorBlendState;
    pipelineCI.pDepthStencilState = &depthStencilState;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pMultisampleState = &multisampleState;
    pipelineCI.pDynamicState = &dynamicState;
    pipelineCI.layout = pipelinesLayout.skyboxPipelineLayout;
    pipelineCI.renderPass = renderPass;
    pipelineCI.subpass = 0;
    pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position });

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skyboxPipeline));
}

void VulkanExample::createLightPipelineLayout() {
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.lightDescriptorSetLayout, 1);
    std::vector<VkPushConstantRange> pushConstantRanges = {
        vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PushconstantsLight), 0)
    };
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
    pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelinesLayout.lightPipelineLayout));
} 

void VulkanExample::createLightPipeline() {
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =  vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
    VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
    VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
    VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
    VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1);
    VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{
        loadShader(getShadersPath() + lightVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
        loadShader(getShadersPath() + lightFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT)
    };

    VkPipelineRenderingCreateInfo renderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &swapChain.colorFormat;
    renderingCreateInfo.depthAttachmentFormat = depthFormat;
    renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.pNext = &renderingCreateInfo;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.pInputAssemblyState = &inputAssemblyState;
    pipelineCI.pRasterizationState = &rasterizationState;
    pipelineCI.pColorBlendState = &colorBlendState;
    pipelineCI.pDepthStencilState = &depthStencilState;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pMultisampleState = &multisampleState;
    pipelineCI.pDynamicState = &dynamicState;
    pipelineCI.layout = pipelinesLayout.lightPipelineLayout;
    pipelineCI.renderPass = renderPass;
    pipelineCI.subpass = 0;
    pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal });

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.lightPipeline));
}

void VulkanExample::updateUniformBuffers() {
    // 矩阵数据
    // UBOMatrix.camPos = camera.position;
    UBOMatrix.model = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f + (objectIndex == 1 ? 45.0f : 0.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
    UBOMatrix.projection = camera.matrices.perspective;
    UBOMatrix.view = camera.matrices.view;
    UBOMatrix.camPos = glm::vec3(glm::inverse(camera.matrices.view)[3]);
    memcpy(uniformBuffersScene[currentBuffer].matricesBuffer.allocationInfo.pMappedData, &UBOMatrix, sizeof(UBOMatrix));
    vmaFlushAllocation(allocator, uniformBuffersScene[currentBuffer].matricesBuffer.allocation, 0, sizeof(UBOMatrix));

    // 光源数据
    const float p = 20.0f;
    UBOLights.lightsPos[0] = glm::vec4(-p, -p*0.5f, -p, 1.0f);
    UBOLights.lightsPos[1] = glm::vec4(-p, -p*0.5f,  p, 1.0f);
    UBOLights.lightsPos[2] = glm::vec4( 0, -5.3, 0, 1.0f);
    UBOLights.lightsPos[3] = glm::vec4( p - 5, -p*0.5f, -p + 5, 1.0f);
    if (!paused) {
        UBOLights.lightsPos[0].x = sin(glm::radians(timer * 360.0f)) * 20.0f;
        UBOLights.lightsPos[0].z = cos(glm::radians(timer * 360.0f)) * 20.0f;
        UBOLights.lightsPos[1].x = cos(glm::radians(timer * 360.0f)) * 20.0f;
        UBOLights.lightsPos[1].y = sin(glm::radians(timer * 360.0f)) * 20.0f;
    }
    UBOLights.lightIntensity[0] = glm::vec4(356.0f, 356.0f, 356.0f, 1.0f);
    UBOLights.lightIntensity[1] = glm::vec4(195.0f, 195.0f, 195.0f, 1.0f);
    UBOLights.lightIntensity[2] = glm::vec4(360.0f, 360.0f, 360.0f, 1.0f);
    UBOLights.lightIntensity[3] = glm::vec4(500.0f, 500.0f, 500.0f, 1.0f);

    UBOLights.lightsColor[0] = glm::vec4(0.85f, 0.47f, 0.33f, 1.0f);
    UBOLights.lightsColor[1] = glm::vec4(0.23f, 0.66f, 0.36f, 1.0f);
    UBOLights.lightsColor[2] = glm::vec4(0.98f, 0.29f, 0.27f, 1.0f);
    UBOLights.lightsColor[3] = glm::vec4(0.52f, 0.76f, 0.85f, 1.0f);
    memcpy(uniformBuffersScene[currentBuffer].lightBuffer.allocationInfo.pMappedData, &UBOLights, sizeof(UBOLights));
    vmaFlushAllocation(allocator, uniformBuffersScene[currentBuffer].lightBuffer.allocation, 0, sizeof(UBOLights));

    // 光源矩阵数据
    UBOLightSourceMatrix.camPos = camera.position;
    UBOLightSourceMatrix.model = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f + (objectIndex == 1 ? 45.0f : 0.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
    UBOLightSourceMatrix.model = glm::scale(UBOLightSourceMatrix.model, glm::vec3(0.3, 0.3, 0.3));
    UBOLightSourceMatrix.projection = camera.matrices.perspective;
    UBOLightSourceMatrix.view = camera.matrices.view;
    memcpy(uniformBuffersScene[currentBuffer].lightSourceMatricesBuffer.allocationInfo.pMappedData, &UBOLightSourceMatrix, sizeof(UBOLightSourceMatrix));
    vmaFlushAllocation(allocator, uniformBuffersScene[currentBuffer].lightSourceMatricesBuffer.allocation, 0, sizeof(UBOLightSourceMatrix));

    // 天空盒数据
    UBOSkyBox.model = glm::mat4(1.0f);
    UBOSkyBox.projection = camera.matrices.perspective;
    UBOSkyBox.view = glm::mat4(glm::mat3(camera.matrices.view)); // 去除平移分量
    memcpy(uniformBuffersScene[currentBuffer].skyBoxMatricesBuffer.allocationInfo.pMappedData, &UBOSkyBox, sizeof(UBOSkyBox));
    vmaFlushAllocation(allocator, uniformBuffersScene[currentBuffer].skyBoxMatricesBuffer.allocation, 0, sizeof(UBOSkyBox));
}

void VulkanExample::render() {
    if (!prepared)
		return;
    
    VulkanExampleBase::prepareFrame();
    updateUniformBuffers();
    buildCommandBuffer();
    VulkanExampleBase::submitFrame();
}

void VulkanExample::buildCommandBuffer() {
    VkCommandBuffer commandBuffer = drawCmdBuffers[currentBuffer];
    VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
    VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufInfo));

    vkutil::cmdTransitionImageLayout(commandBuffer, depthStencil.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[currentImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    cmdDrawSecne(commandBuffer);
    cmdDrawLight(commandBuffer);
    cmdDrawSkybox(commandBuffer);
    vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[currentImageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
}

void VulkanExample::cmdDrawSecne(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        swapChain.imageViews[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VkClearValue{{ 0.01f, 0.02f, 0.025f, 1.0f }}
    );
    
    VkRenderingAttachmentInfo depthAttachment = vkutil::renderingdepthAttachmentInfo(
        depthStencil.view,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        1.0f
    );

    VkExtent2D extent = VkExtent2D{width, height};
    vkutil::cmdBeginColorDepthRendering(cmd, extent, colorAttachment, depthAttachment);
    {
        vkutil::cmdSetViewportAndScissor(cmd, extent.width, extent.height);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.scenePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinesLayout.scenePipelineLayout, 0, 1, 
                                &descriptorSets[currentBuffer].sceneDescriptor, 0, nullptr);

        Material mat = materials[materialIndex];
        int gridSize = 7;
        for (int x = 0; x < gridSize; ++x) {
            for (int y = 0; y < gridSize; ++y) {
                objectPos = glm::vec3(float(x - (gridSize / 2.0f)) * 4.5f, 0.0f, float(y - (gridSize / 2.0f)) * 4.5f);
                mat.params.metallic = glm::clamp((float)x / (float)(gridSize - 1), 0.1f, 1.0f);
				mat.params.roughness = glm::clamp((float)y / (float)(gridSize - 1), 0.05f, 1.0f);
                vkCmdPushConstants(cmd, pipelinesLayout.scenePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec3), &objectPos);
                vkCmdPushConstants(cmd, pipelinesLayout.scenePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::vec3), sizeof(Material::PushBlock), &mat);
                objects[objectIndex].draw(cmd);
            }
        }
    }
    vkutil::cmdEndRendering(cmd);
}

void VulkanExample::cmdDrawLight(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        swapChain.imageViews[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VkClearValue{{ 0.01f, 0.02f, 0.025f, 1.0f }},
        VK_ATTACHMENT_LOAD_OP_LOAD
    );
    
    VkRenderingAttachmentInfo depthAttachment = vkutil::renderingdepthAttachmentInfo(
        depthStencil.view,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        1.0f,
        VK_ATTACHMENT_LOAD_OP_LOAD
    );

    VkExtent2D extent = VkExtent2D{width, height};
    vkutil::cmdBeginColorDepthRendering(cmd, extent, colorAttachment, depthAttachment);
    {
        vkutil::cmdSetViewportAndScissor(cmd, extent.width, extent.height);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.lightPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinesLayout.lightPipelineLayout, 0, 1, 
                                &descriptorSets[currentBuffer].lightDescriptor, 0, nullptr);
        for (int i = 0; i < 4; ++i) {
            pushconstantsLight.Pos = UBOLights.lightsPos[i];
            pushconstantsLight.Color = UBOLights.lightsColor[i];
            pushconstantsLight.Intensity = UBOLights.lightIntensity[i];
            vkCmdPushConstants(cmd, pipelinesLayout.lightPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushconstantsLight), &pushconstantsLight);
            lightObject.draw(cmd);
        }
    }
    vkutil::cmdEndRendering(cmd);
}

void VulkanExample::cmdDrawSkybox(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        swapChain.imageViews[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VkClearValue{{ 0.01f, 0.02f, 0.025f, 1.0f }},
        VK_ATTACHMENT_LOAD_OP_LOAD
    );
    
    VkRenderingAttachmentInfo depthAttachment = vkutil::renderingdepthAttachmentInfo(
        depthStencil.view,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        1.0f,
        VK_ATTACHMENT_LOAD_OP_LOAD
    );

    VkExtent2D extent = VkExtent2D{width, height};
    vkutil::cmdBeginColorDepthRendering(cmd, extent, colorAttachment, depthAttachment);
    {
        vkutil::cmdSetViewportAndScissor(cmd, extent.width, extent.height);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skyboxPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinesLayout.skyboxPipelineLayout, 0, 1, 
                                &descriptorSets[currentBuffer].skyboxDescriptor, 0, nullptr);
        skyboxCube.draw(cmd);
        drawUI(cmd);
    }
    vkutil::cmdEndRendering(cmd);
}

void VulkanExample::generateIrradianceCubeMap() {
    // 资源初始化
    const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
    const int32_t dim = 64;
	const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;
    textures.irradianceCubeMap = vkutil::createAllocatedCubeTexture(
        device,
        allocator,
        dim,
        numMips,
        format,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );
    AllocatedImage offscreen = vkutil::createAllocatedImage( // 中转image
        device,
        allocator,
        {dim, dim, 1},
        format,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    // pushconstants数据
    struct PushConstantsIrradiance {
        glm::mat4 mvp;
        float deltaPhi = (2.0f * float(M_PI)) / 180.f;  // 0~2pi，分180份
        float deltaTheta = (0.5f * float(M_PI)) / 64.f; // 0~0.5pi，分64份
    } pushConstantsIrradiance;

    // 描述符
    VkDescriptorSetLayout descriptorsetlayout;
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
	};
    VkDescriptorSetLayoutCreateInfo descriptorsetlayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorsetlayoutCI, nullptr, &descriptorsetlayout));

    VkDescriptorSet descriptorset;
    VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorsetlayout, 1);
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorset));

    VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(descriptorset, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &textures.environmentCubeMap.descriptor);
    vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

    // 管线
    VkPipelineLayout pipelinelayout;
    std::vector<VkPushConstantRange> pushConstantRanges = {
        vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PushConstantsIrradiance), 0),
	};
    VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorsetlayout, 1);
    pipelineLayoutCI.pushConstantRangeCount = 1;
    pipelineLayoutCI.pPushConstantRanges = pushConstantRanges.data();
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
    VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
    VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
    VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
    VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1);
    VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

    VkPipelineRenderingCreateInfo renderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &format;
    renderingCreateInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.pNext = &renderingCreateInfo;
    pipelineCI.layout = pipelinelayout;
    pipelineCI.pInputAssemblyState = &inputAssemblyState;
    pipelineCI.pRasterizationState = &rasterizationState;
    pipelineCI.pColorBlendState = &colorBlendState;
    pipelineCI.pMultisampleState = &multisampleState;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pDepthStencilState = &depthStencilState;
    pipelineCI.pDynamicState = &dynamicState;
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.renderPass = VK_NULL_HANDLE;
    pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::UV });

    shaderStages[0] = loadShader(getShadersPath() + filterCubeVertexShader, VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = loadShader(getShadersPath() + irradianceFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipeline pipeline;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));

    // 绘制
    glm::vec3 origin = glm::vec3(0.0f);
    std::vector<glm::mat4> viewMatrices = {
        // +X
        glm::lookAt(origin, origin + glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),

        // -X
        glm::lookAt(origin, origin + glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),

        // +Y
        glm::lookAt(origin, origin + glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),

        // -Y
        glm::lookAt(origin, origin + glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),

        // +Z
        glm::lookAt(origin, origin + glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),

        // -Z
        glm::lookAt(origin, origin + glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    };

    VkExtent2D extent = VkExtent2D{dim, dim};
    // VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
    //     textures.irradianceCubeMap.view,
    //     VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    //     VkClearValue{{ 0.00f, 0.00f, 0.00f, 1.0f }}
    // );

    VkImageSubresourceRange cubeRange = vkutil::cubeSubresourceRange(numMips);
    VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    {
        vkutil::cmdTransitionImageLayout(
            cmdBuf, textures.irradianceCubeMap.image, 
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
            cubeRange
        );
        vkutil::cmdTransitionImageLayout(
            cmdBuf,
            offscreen.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
        for (uint32_t mip = 0; mip < numMips; ++mip) {
            const uint32_t mipDim = static_cast<uint32_t>(dim * std::pow(0.5f, mip));
            for (uint32_t face = 0; face < 6; ++face) {
                // 绘制到offscreen
                VkExtent2D renderExtent {mipDim, mipDim};
                VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(offscreen.imageView, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VkClearValue{0.0, 0.0, 0.0, 1.0});
                vkutil::cmdBeginColorOnlyRendering(cmdBuf, renderExtent, colorAttachment);
                {
                    vkutil::cmdSetViewportAndScissor(cmdBuf, mipDim, mipDim);
                    pushConstantsIrradiance.mvp = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 512.0f) * viewMatrices[face];
                    vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantsIrradiance), &pushConstantsIrradiance);
                    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, nullptr);
                    skyboxCube.draw(cmdBuf);
                }
                vkutil::cmdEndRendering(cmdBuf);

                // 从offscreen复制到cube map的面上
                vkutil::cmdTransitionImageLayout(   // 先转成传输源格式
                    cmdBuf,
                    offscreen.image,
                    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT
                );

                VkImageCopy copyRegion{};
                copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.srcSubresource.mipLevel = 0;
                copyRegion.srcSubresource.baseArrayLayer = 0;   // layer的起始地址
                copyRegion.srcSubresource.layerCount = 1;       // 拷贝layer的数量

                copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.dstSubresource.mipLevel = mip;       // mip层级
                copyRegion.dstSubresource.baseArrayLayer = face;
                copyRegion.dstSubresource.layerCount = 1;

                copyRegion.extent = {mipDim, mipDim, 1};

                vkCmdCopyImage(cmdBuf,
                    offscreen.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    textures.irradianceCubeMap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &copyRegion
                );

                vkutil::cmdTransitionImageLayout(   // 传输完转回颜色附件
                    cmdBuf,
                    offscreen.image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT
                );
            }
        }

        vkutil::cmdTransitionImageLayout(
            cmdBuf, textures.irradianceCubeMap.image, 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
            cubeRange
        );
        textures.irradianceCubeMap.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        textures.irradianceCubeMap.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    vulkanDevice->flushCommandBuffer(cmdBuf, queue);

    // 清理临时数据
    vkutil::destroyAllocatedImage(device, allocator, offscreen);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelinelayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorsetlayout, nullptr);
}

void VulkanExample::generatePrefilteredCubeMap() {
    // 资源初始化
    const VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    const int32_t dim = 1024;
    const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;
    UBOMatrix.mipNums = numMips;

    textures.prefilteredCubeMap = vkutil::createAllocatedCubeTexture(device, allocator, dim, numMips, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    AllocatedImage offscreen = vkutil::createAllocatedImage( // 中转image
        device,
        allocator,
        {dim, dim, 1},
        format,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    // pushconstants
    struct PrefilterPushconstant {
        glm::mat4 mvp;
        float roughness;
        uint32_t sampleCounts;
    } prefilterPushconstant;

    // 描述符
    VkDescriptorSetLayout descriptorsetlayout;
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
        vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0)
    };
    VkDescriptorSetLayoutCreateInfo descriptorsetlayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorsetlayoutCI, nullptr, &descriptorsetlayout));

    VkDescriptorSet descriptorset;
    VkDescriptorSetAllocateInfo allocInfo =	vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorsetlayout, 1);
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorset));
    VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(descriptorset, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &textures.environmentCubeMap.descriptor);
    vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

    // 管线
    VkPipelineLayout pipelinelayout;
    std::vector<VkPushConstantRange> pushConstantRanges = {
			vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PrefilterPushconstant), 0),
	};
    VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorsetlayout, 1);
    pipelineLayoutCI.pushConstantRangeCount = 1;
	pipelineLayoutCI.pPushConstantRanges = pushConstantRanges.data();
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
    VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
    VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
    VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
    VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1);
    VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

    VkPipelineRenderingCreateInfo renderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &format;
    renderingCreateInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.pNext = &renderingCreateInfo;
    pipelineCI.layout = pipelinelayout;
    pipelineCI.pInputAssemblyState = &inputAssemblyState;
    pipelineCI.pRasterizationState = &rasterizationState;
    pipelineCI.pColorBlendState = &colorBlendState;
    pipelineCI.pMultisampleState = &multisampleState;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pDepthStencilState = &depthStencilState;
    pipelineCI.pDynamicState = &dynamicState;
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.renderPass = VK_NULL_HANDLE;
    pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::UV });

    shaderStages[0] = loadShader(getShadersPath() + filterCubeVertexShader, VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = loadShader(getShadersPath() + prefilterFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipeline pipeline;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));

    // 绘制
    glm::vec3 origin = glm::vec3(0.0f);
    std::vector<glm::mat4> viewMatrices = {
        // +X
        glm::lookAt(origin, origin + glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),

        // -X
        glm::lookAt(origin, origin + glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),

        // +Y
        glm::lookAt(origin, origin + glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),

        // -Y
        glm::lookAt(origin, origin + glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),

        // +Z
        glm::lookAt(origin, origin + glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),

        // -Z
        glm::lookAt(origin, origin + glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    };

    VkImageSubresourceRange cubeRange = vkutil::cubeSubresourceRange(numMips);
    VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    {
        vkutil::cmdTransitionImageLayout(
            cmdBuf, textures.prefilteredCubeMap.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            cubeRange
        );
        vkutil::cmdTransitionImageLayout(
            cmdBuf,
            offscreen.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        for (uint32_t mip = 0; mip < numMips; ++mip) {
            const uint32_t mipDim = static_cast<uint32_t>(dim * std::pow(0.5f, mip));
            prefilterPushconstant.roughness = (float)mip / (float)(numMips - 1);
            prefilterPushconstant.sampleCounts = 1024;
            for (uint32_t face = 0; face < 6; ++face) {
                // 绘制到offscreen
                VkExtent2D renderExtent {mipDim, mipDim};
                VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(offscreen.imageView, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VkClearValue{0.0, 0.0, 0.0, 1.0});
                vkutil::cmdBeginColorOnlyRendering(cmdBuf, renderExtent, colorAttachment);
                {
                    vkutil::cmdSetViewportAndScissor(cmdBuf, mipDim, mipDim);
                    prefilterPushconstant.mvp = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 512.0f) * viewMatrices[face];
                    vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(prefilterPushconstant), &prefilterPushconstant);
                    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, nullptr);
                    skyboxCube.draw(cmdBuf);
                }
                vkutil::cmdEndRendering(cmdBuf);

                // 从offscreen复制到cube map的面上
                vkutil::cmdTransitionImageLayout(   // 先转成传输源格式
                    cmdBuf,
                    offscreen.image,
                    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT
                );

                VkImageCopy copyRegion{};
                copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.srcSubresource.mipLevel = 0;
                copyRegion.srcSubresource.baseArrayLayer = 0;   // layer的起始地址
                copyRegion.srcSubresource.layerCount = 1;       // 拷贝layer的数量

                copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.dstSubresource.mipLevel = mip;       // mip层级
                copyRegion.dstSubresource.baseArrayLayer = face;
                copyRegion.dstSubresource.layerCount = 1;

                copyRegion.extent = {mipDim, mipDim, 1};

                vkCmdCopyImage(cmdBuf,
                    offscreen.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    textures.prefilteredCubeMap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &copyRegion
                );

                vkutil::cmdTransitionImageLayout(   // 传输完转回颜色附件
                    cmdBuf,
                    offscreen.image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT
                );
            }
        }

        vkutil::cmdTransitionImageLayout(
            cmdBuf, textures.prefilteredCubeMap.image, 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
            cubeRange
        );
        textures.prefilteredCubeMap.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        textures.prefilteredCubeMap.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    vulkanDevice->flushCommandBuffer(cmdBuf, queue);

    // 清理资源
    vkutil::destroyAllocatedImage(device, allocator, offscreen);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelinelayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorsetlayout, nullptr);
}

void VulkanExample::generateBRDFLUT() {
    // 初始化资源
    const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
    const int32_t dim = 512;

    textures.brdfLUT.image = vkutil::createAllocatedImage(device, allocator,
        VkExtent3D{dim, dim, 1}, format, 
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    VkSamplerCreateInfo samplerCI = vks::initializers::samplerCreateInfo();
    samplerCI.magFilter = VK_FILTER_LINEAR;
    samplerCI.minFilter = VK_FILTER_LINEAR;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = 1.0f;
    samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &textures.brdfLUT.sampler));

    textures.brdfLUT.descriptor.imageView = textures.brdfLUT.image.imageView;
    textures.brdfLUT.descriptor.sampler = textures.brdfLUT.sampler;
    textures.brdfLUT.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // 描述符
    VkDescriptorSetLayout descriptorsetlayout;
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {};
    VkDescriptorSetLayoutCreateInfo descriptorsetlayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorsetlayoutCI, nullptr, &descriptorsetlayout));

    VkDescriptorSet descriptorset;
    VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorsetlayout, 1);
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorset));

    // 管线
    VkPipelineLayout pipelinelayout;
    VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorsetlayout, 1);
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
    VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
    VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
    VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
    VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1);
    VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
    VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

    shaderStages[0] = loadShader(getShadersPath() + brdfLUTVertexShader, VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = loadShader(getShadersPath() + brdfLUTFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT);

    VkPipelineRenderingCreateInfo renderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    renderingCreateInfo.colorAttachmentCount = 1;
    renderingCreateInfo.pColorAttachmentFormats = &format;
    renderingCreateInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    renderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineCI = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineCI.pNext = &renderingCreateInfo;
    pipelineCI.layout = pipelinelayout;
    pipelineCI.pInputAssemblyState = &inputAssemblyState;
    pipelineCI.pRasterizationState = &rasterizationState;
    pipelineCI.pColorBlendState = &colorBlendState;
    pipelineCI.pMultisampleState = &multisampleState;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pDepthStencilState = &depthStencilState;
    pipelineCI.pDynamicState = &dynamicState;
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.renderPass = VK_NULL_HANDLE;
    pipelineCI.pVertexInputState = &emptyInputState;    // 直接画四边形，不用顶点输入

    VkPipeline pipeline;
    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));

    // 绘制
    VkCommandBuffer cmdBuf = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    {
        vkutil::cmdTransitionImageLayout(cmdBuf, textures.brdfLUT.image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

        VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(textures.brdfLUT.image.imageView, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VkClearValue{0.0, 0.0, 0.0, 1.0});
        vkutil::cmdBeginColorOnlyRendering(cmdBuf, VkExtent2D{dim, dim}, colorAttachment);
        {
            vkutil::cmdSetViewportAndScissor(cmdBuf, dim, dim);
            vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, nullptr);
            vkCmdDraw(cmdBuf, 3, 1, 0, 0);
        }
        vkutil::cmdEndRendering(cmdBuf);
        vkutil::cmdTransitionImageLayout(cmdBuf, textures.brdfLUT.image.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    vulkanDevice->flushCommandBuffer(cmdBuf, queue);

    // 资源清理
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelinelayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorsetlayout, nullptr);
}
