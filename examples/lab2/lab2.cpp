#include "lab2.h"
#include "vk_descriptors.h"
#include "vk_rendering.h"
#include "vk_pipelines.h"
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

    if (overlay->header("SceneSettings")) {
        overlay->sliderFloat("Object X", &pbrObjectPos.x, -20.0f, 20.0f);
        overlay->sliderFloat("Object Y", &pbrObjectPos.y, -20.0f, 20.0f);
        overlay->sliderFloat("Object Z", &pbrObjectPos.z, -20.0f, 20.0f);
        overlay->sliderFloat("Rotation", &rotationAngle, 0.0f, 360.0f);
        overlay->sliderFloat("Scale", &scaleRatio, 0.5f, 10.0f);
    }

    if (overlay->header("BloomSettings")) {
        overlay->sliderInt("Enable Bloom", &enableBloom, 0, 1);
        overlay->sliderInt("Use Karis Average", &useKaris, 0, 1);
        overlay->sliderFloat("Exposure", &exposure, 0.2f, 1.0f);
        overlay->sliderFloat("Bloom Strength", &bloomStrength, 0.0f, 0.5f);
        overlay->sliderFloat("Bloom Filter Radius", &bloomFilterRadius, 0.1f, 5.0f);
    }
}

VulkanExample::~VulkanExample() {
    if (device) {
        destroyPipelines();
        destroyBloomDescriptorSets();
        destroyDescriptors();
        destroyUniformBuffers();
        destroyBloomResources();
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

    createBloomResources();
    createBloomDescriptorSets();
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
    // PBR Texture物体模型
    vkglTF::descriptorBindingFlags = 
        vkglTF::DescriptorBindingFlags::ImageBaseColor | 
        vkglTF::ImageMetallicRoughness | 
        vkglTF::ImageNormalMap | 
        vkglTF::ImageOcclusionMap | 
        vkglTF::ImageEmissiveMap;
    pbrObject.loadFromFile(getAssetPath() + pbrModelPath, vulkanDevice, queue, vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY);

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

void VulkanExample::createBloomResources() {
    bloom.hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    bloom.sceneExtent = {width, height};
    // 至少保留一层 bloom mip，后面的 composite descriptor 会直接引用 bloom.mips[0]。
    bloom.mipCount = static_cast<uint32_t>(std::max(1, bloomMipCount));

    bloom.hdrSceneColor = vkutil::createAllocatedImage(device, allocator,
        VkExtent3D{width, height, 1},
        bloom.hdrFormat,
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
    // 每个 BloomMip 都是独立 image，不是同一张 image 的 mip level，所以采样器固定在 lod 0。
    samplerCI.maxLod = 0.0f;
    VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &bloom.sampler));

    bloom.hdrSceneDescriptor = vkutil::descriptorImageInfo(bloom.sampler, bloom.hdrSceneColor.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // 多级mips采样纹理初始化
    bloom.mips.clear();
    uint32_t mipWidth = width / 2;
    uint32_t mipHeight = height / 2;
    for (uint32_t i = 0; i < bloom.mipCount; ++i) {
        mipWidth = std::max(1u, mipWidth);
        mipHeight = std::max(1u, mipHeight);

        BloomMip mip{};
        mip.extent = {mipWidth, mipHeight};
        mip.image = vkutil::createAllocatedImage(device, allocator, 
            VkExtent3D{mipWidth, mipHeight, 1}, bloom.hdrFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
        mip.descriptor = vkutil::descriptorImageInfo(bloom.sampler, mip.image.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        bloom.mips.push_back(mip);

        mipWidth /= 2;
        mipHeight /= 2;
    }
}

void VulkanExample::destroyBloomResources() {
    for (BloomMip &mip : bloom.mips) {
        vkutil::destroyAllocatedImage(device, allocator, mip.image);
    }
    bloom.mips.clear();

    vkutil::destroyAllocatedImage(device, allocator, bloom.hdrSceneColor);
    if (bloom.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, bloom.sampler, nullptr);
        bloom.sampler = VK_NULL_HANDLE;
    }
}

void VulkanExample::createBloomDescriptorSets() {
    // sample layout 只描述“采一张纹理”，下采样、上采样都可以复用。
    std::vector<VkDescriptorSetLayoutBinding> sampleBindings = {
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0)  
    };
    VkDescriptorSetLayoutCreateInfo sampleLayoutCI = vkutil::descriptorSetLayoutCreateInfo(sampleBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &sampleLayoutCI, nullptr, &bloomDescriptorSetLayouts.sampleDescriptorSetLayout));

    std::vector<VkDescriptorSetLayoutBinding> compositeBindings = {
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) 
    };
    VkDescriptorSetLayoutCreateInfo compositeLayoutCI = vkutil::descriptorSetLayoutCreateInfo(compositeBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &compositeLayoutCI, nullptr, &bloomDescriptorSetLayouts.compositeDescriptorSetLayout));

    VkDescriptorSetAllocateInfo allocInfoSample = vkutil::descriptorSetAllocateInfo(descriptorPool, &bloomDescriptorSetLayouts.sampleDescriptorSetLayout, 1);
    VkDescriptorSetAllocateInfo allocInfoComposite = vkutil::descriptorSetAllocateInfo(descriptorPool, &bloomDescriptorSetLayouts.compositeDescriptorSetLayout, 1);

    // 第 0 层下采样的输入是完整 HDR scene，不属于 bloom.mips 数组。
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfoSample, &bloomDescriptorSets.hdrSceneSet));
    std::vector<VkWriteDescriptorSet> hdrSceneWrites = {
        vkutil::writeCombinedImageSampler(bloomDescriptorSets.hdrSceneSet, 0, &bloom.hdrSceneDescriptor)
    };
    vkutil::updateDescriptorSet(device, hdrSceneWrites);

    // mipSets[i] 只采样 bloom.mips[i]，这样上采样时可以直接按 mip 下标绑定。
    bloomDescriptorSets.mipSets.resize(bloom.mipCount);
    for (uint32_t mip = 0; mip < bloom.mipCount; ++mip) {
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfoSample, &bloomDescriptorSets.mipSets[mip]));
        std::vector<VkWriteDescriptorSet> sampleWrites = {
            vkutil::writeCombinedImageSampler(bloomDescriptorSets.mipSets[mip], 0, &bloom.mips[mip].descriptor)
        };
        vkutil::updateDescriptorSet(device, sampleWrites);
    }

    // 最终合成读取原始 HDR scene 和最亮、分辨率最高的一层 bloom 结果。
    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfoComposite, &bloomDescriptorSets.compositeSet));
    std::vector<VkWriteDescriptorSet> compositeWrites = {
        vkutil::writeCombinedImageSampler(bloomDescriptorSets.compositeSet, 0, &bloom.hdrSceneDescriptor),
        vkutil::writeCombinedImageSampler(bloomDescriptorSets.compositeSet, 1, &bloom.mips[0].descriptor)
    };
    vkutil::updateDescriptorSet(device, compositeWrites);
}

void VulkanExample::destroyBloomDescriptorSets() {
    // descriptor set 本身随 descriptorPool 释放；这里清空句柄，避免 resize/recreate 后误用旧 set。
    bloomDescriptorSets.hdrSceneSet = VK_NULL_HANDLE;
    bloomDescriptorSets.mipSets.clear();
    bloomDescriptorSets.compositeSet = VK_NULL_HANDLE;

    if (bloomDescriptorSetLayouts.sampleDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, bloomDescriptorSetLayouts.sampleDescriptorSetLayout, nullptr);
        bloomDescriptorSetLayouts.sampleDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (bloomDescriptorSetLayouts.compositeDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, bloomDescriptorSetLayouts.compositeDescriptorSetLayout, nullptr);
        bloomDescriptorSetLayouts.compositeDescriptorSetLayout = VK_NULL_HANDLE;
    }
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

        buffer.pbrTextureMatricesBuffer = vkutil::createAllocatedBuffer(
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
        vkutil::destroyAllocatedBuffer(allocator, buffer.pbrTextureMatricesBuffer);
        vkutil::destroyAllocatedBuffer(allocator, buffer.lightBuffer);
        vkutil::destroyAllocatedBuffer(allocator, buffer.lightSourceMatricesBuffer);
        vkutil::destroyAllocatedBuffer(allocator, buffer.skyBoxMatricesBuffer);
    }
}

void VulkanExample::createDescriptorsPool() {
    // Pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        vkutil::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames * 32),
        vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames * 32)
    };
    VkDescriptorPoolCreateInfo poolCI = vkutil::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames * 32);
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

    std::vector<VkDescriptorSetLayoutBinding> fullScreenLayout = { // FullScreen Layout
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT , 0)
    };
    VkDescriptorSetLayoutCreateInfo fullScreenLayoutCI = vkutil::descriptorSetLayoutCreateInfo(fullScreenLayout);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &fullScreenLayoutCI, nullptr, &descriptorSetLayouts.fullScreenDescriptorSetLayout));

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
    VkDescriptorSetAllocateInfo fullScreenAllocInfo = vkutil::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.fullScreenDescriptorSetLayout, 1);
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

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &fullScreenAllocInfo, &descriptorSets[i].fullScreenDescriptor));
        std::vector<VkWriteDescriptorSet> fullScreenWrites = {
            vkutil::writeCombinedImageSampler(descriptorSets[i].fullScreenDescriptor, 0, &bloom.hdrSceneDescriptor)
        };
        vkutil::updateDescriptorSet(device, fullScreenWrites);

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i].pbrTextureDescriptor));
        VkDescriptorBufferInfo pbrTextureMatricesBufferInfo = vkutil::descriptorBufferInfo(uniformBuffersScene[i].pbrTextureMatricesBuffer.handle, sizeof(UniformDataMatrices), 0);
        std::vector<VkWriteDescriptorSet> pbrTextureWrites = {
            vkutil::writeUniformBuffer(descriptorSets[i].pbrTextureDescriptor, 0, &pbrTextureMatricesBufferInfo),
            vkutil::writeUniformBuffer(descriptorSets[i].pbrTextureDescriptor, 1, &lightBufferInfo),
            vkutil::writeCombinedImageSampler(descriptorSets[i].pbrTextureDescriptor, 2, &irradianceImageInfo),
            vkutil::writeCombinedImageSampler(descriptorSets[i].pbrTextureDescriptor, 3, &prefilterImageInfo),
            vkutil::writeCombinedImageSampler(descriptorSets[i].pbrTextureDescriptor, 4, &brdfLUTImageInfo)
        };
        vkutil::updateDescriptorSet(device, pbrTextureWrites);

        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &skyboxAllocInfo, &descriptorSets[i].skyboxDescriptor));
        VkDescriptorBufferInfo skyboxMatricesBufferInfo = vkutil::descriptorBufferInfo(uniformBuffersScene[i].skyBoxMatricesBuffer.handle, sizeof(UniformDataSkyBox), 0);
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

    if (descriptorSetLayouts.fullScreenDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.fullScreenDescriptorSetLayout, nullptr);
        descriptorSetLayouts.fullScreenDescriptorSetLayout = VK_NULL_HANDLE;
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
    createFullScreenPipelineLayout();
    createFullScreenPipeline();
    createPBRTexturePipelineLayout();
    createPBRTexturePipeline();
    createSkyboxPipelineLayout();
    createSkyboxPipeline();
    createLightPipelineLayout();
    createLightPipeline();
    createBloomPipelinesLayout();
    createBloomPipelines();
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

    if (pipelines.fullScreenPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipelines.fullScreenPipeline, nullptr);
        pipelines.fullScreenPipeline = VK_NULL_HANDLE;
    }
    if (pipelinesLayout.fullScreenPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelinesLayout.fullScreenPipelineLayout, nullptr);
        pipelinesLayout.fullScreenPipelineLayout = VK_NULL_HANDLE;
    }

    if (pipelines.pbrTexturePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipelines.pbrTexturePipeline, nullptr);
        pipelines.pbrTexturePipeline = VK_NULL_HANDLE;
    }
    if (pipelinesLayout.pbrTexturePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelinesLayout.pbrTexturePipelineLayout, nullptr);
        pipelinesLayout.pbrTexturePipelineLayout = VK_NULL_HANDLE;
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

    std::vector<VkPipeline*> pipelinesToDestroy = {
        &bloomPipelines.bloomDownsamplePipeline,
        &bloomPipelines.bloomUpsamplePipeline,
        &bloomPipelines.bloomCompositePipeline
    };
    for (VkPipeline* pipeline : pipelinesToDestroy) {
        if (*pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, *pipeline, nullptr);
            *pipeline = VK_NULL_HANDLE;
        }
    }

    std::vector<VkPipelineLayout*> pipelineLayoutsToDestroy = {
        &bloomPipelinesLayout.bloomDownsamplePipelineLayout,
        &bloomPipelinesLayout.bloomUpsamplePipelineLayout,
        &bloomPipelinesLayout.bloomCompositePipelineLayout
    };
    for (VkPipelineLayout* layout : pipelineLayoutsToDestroy) {
        if (*layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, *layout, nullptr);
            *layout = VK_NULL_HANDLE;
        }
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
    vkutil::PipelineBuilder builder;
    builder.setPipelineLayout(pipelinesLayout.scenePipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + pbrSceneVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + pbrSceneFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal }))
        .setColorAttachmentFormat(bloom.hdrFormat)
        .setDepthFormat(depthFormat)
        .enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();
    pipelines.scenePipeline = builder.build(device, pipelineCache);
}

void VulkanExample::createFullScreenPipelineLayout() {
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.fullScreenDescriptorSetLayout, 1);
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelinesLayout.fullScreenPipelineLayout));
}

void VulkanExample::createFullScreenPipeline() {
    vkutil::PipelineBuilder builder;
    builder.setPipelineLayout(pipelinesLayout.fullScreenPipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + fullScreenVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + fullScreenFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput({})
        .setColorAttachmentFormat(swapChain.colorFormat)
        .setDepthFormat(depthFormat)
        .disableDepthTest()
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();
    pipelines.fullScreenPipeline = builder.build(device, pipelineCache);
}

void VulkanExample::createBloomPipelinesLayout() {
    struct BloomDownsamplePushConstants {
        glm::vec2 inputTextureSize;
        int useKairsWeight;
        float padding;
    };
    VkPipelineLayoutCreateInfo downsampleLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&bloomDescriptorSetLayouts.sampleDescriptorSetLayout, 1);
    std::vector<VkPushConstantRange> downsamplePushConstants = {
        vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(BloomDownsamplePushConstants), 0)
    };
    downsampleLayoutCI.pushConstantRangeCount = 1;
    downsampleLayoutCI.pPushConstantRanges = downsamplePushConstants.data();
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &downsampleLayoutCI, nullptr, &bloomPipelinesLayout.bloomDownsamplePipelineLayout));

    struct BloomUpsamplePushConstants {
        glm::vec2 inputTextureSize;
        float filterRadius;
        float padding; // 对齐到 16 字节
    };
    VkPipelineLayoutCreateInfo upsampleLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&bloomDescriptorSetLayouts.sampleDescriptorSetLayout, 1);
    std::vector<VkPushConstantRange> upsamplePushConstants = {
        vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(BloomUpsamplePushConstants), 0)
    };
    upsampleLayoutCI.pushConstantRangeCount = 1;
    upsampleLayoutCI.pPushConstantRanges = upsamplePushConstants.data();
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &upsampleLayoutCI, nullptr, &bloomPipelinesLayout.bloomUpsamplePipelineLayout));

    struct BloomCompositePushConstants {
        float exposure;
        float bloomStrength;
        uint32_t enableBloom;
        float padding;
    };
    VkPipelineLayoutCreateInfo compositeLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&bloomDescriptorSetLayouts.compositeDescriptorSetLayout, 1);
    std::vector<VkPushConstantRange> compositePushConstants = {
        vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(BloomCompositePushConstants), 0) 
    };
    compositeLayoutCI.pushConstantRangeCount = 1;
    compositeLayoutCI.pPushConstantRanges = compositePushConstants.data();
    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &compositeLayoutCI, nullptr, &bloomPipelinesLayout.bloomCompositePipelineLayout));
}

void VulkanExample::createBloomPipelines() {
    vkutil::PipelineBuilder downsampleBuilder;
    downsampleBuilder.setPipelineLayout(bloomPipelinesLayout.bloomDownsamplePipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + bloomDownsampleVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + bloomDownsampleFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput({})
        .setColorAttachmentFormat(bloom.hdrFormat)
        .setDepthFormat(depthFormat)
        .disableDepthTest()
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();
    bloomPipelines.bloomDownsamplePipeline = downsampleBuilder.build(device, pipelineCache);

    vkutil::PipelineBuilder upsampleBuilder;
    upsampleBuilder.setPipelineLayout(bloomPipelinesLayout.bloomUpsamplePipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + bloomUpsampleVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + bloomUpsampleFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput({})
        .setColorAttachmentFormat(bloom.hdrFormat)
        .setDepthFormat(depthFormat)
        .disableDepthTest()
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .enableAdditiveBlending();
    bloomPipelines.bloomUpsamplePipeline = upsampleBuilder.build(device, pipelineCache);

    vkutil::PipelineBuilder compositeBuilder;
    compositeBuilder.setPipelineLayout(bloomPipelinesLayout.bloomCompositePipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + bloomCompositeVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + bloomCompositeFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput({})
        .setColorAttachmentFormat(swapChain.colorFormat)
        .setDepthFormat(depthFormat)
        .disableDepthTest()
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();
    bloomPipelines.bloomCompositePipeline = compositeBuilder.build(device, pipelineCache);
}

void VulkanExample::createPBRTexturePipelineLayout() {
    std::vector<VkDescriptorSetLayout> setLayouts = {
        descriptorSetLayouts.sceneDescriptorSetLayout,
        vkglTF::descriptorSetLayoutImage
    };
    pipelinesLayout.pbrTexturePipelineLayout = vkutil::createPipelineLayout(device, setLayouts, {});
}

void VulkanExample::createPBRTexturePipeline() {
    vkutil::PipelineBuilder builder;
    builder.setPipelineLayout(pipelinesLayout.pbrTexturePipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + pbrTextureVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + pbrTextureFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Tangent}))
        .setColorAttachmentFormat(bloom.hdrFormat)
        .setDepthFormat(depthFormat)
        .enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();
    pipelines.pbrTexturePipeline = builder.build(device, pipelineCache);
}

void VulkanExample::createSkyboxPipelineLayout() {
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.skyboxDescriptorSetLayout, 1);
     VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelinesLayout.skyboxPipelineLayout));
}

void VulkanExample::createSkyboxPipeline() {
    vkutil::PipelineBuilder builder;
    builder.setPipelineLayout(pipelinesLayout.skyboxPipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + skyboxVertexShader, VK_SHADER_STAGE_VERTEX_BIT), //TODO
            loadShader(getShadersPath() + skyboxFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position }))
        .setColorAttachmentFormat(bloom.hdrFormat)
        .setDepthFormat(depthFormat)
        .enableDepthTest(false, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();

    pipelines.skyboxPipeline = builder.build(device, pipelineCache);
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
    vkutil::PipelineBuilder builder;

    builder
        .setPipelineLayout(pipelinesLayout.lightPipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + lightVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + lightFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({
            vkglTF::VertexComponent::Position,
            vkglTF::VertexComponent::Normal
        }))
        .setColorAttachmentFormat(bloom.hdrFormat)
        .setDepthFormat(depthFormat)
        .enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();

    pipelines.lightPipeline = builder.build(device, pipelineCache);
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

    // PBR纹理矩阵数据
    UBOPBRTextureMatrix.model = glm::translate(glm::mat4(1.0f), pbrObjectPos);
    UBOPBRTextureMatrix.model = glm::scale(UBOPBRTextureMatrix.model, glm::vec3(scaleRatio));
    UBOPBRTextureMatrix.model = glm::rotate(UBOPBRTextureMatrix.model, glm::radians(rotationAngle), glm::vec3(0.0f, 1.0f, 0.0f));
    UBOPBRTextureMatrix.projection = camera.matrices.perspective;
    UBOPBRTextureMatrix.view = camera.matrices.view;
    UBOPBRTextureMatrix.camPos = glm::vec3(glm::inverse(camera.matrices.view)[3]);
    memcpy(uniformBuffersScene[currentBuffer].pbrTextureMatricesBuffer.allocationInfo.pMappedData, &UBOPBRTextureMatrix, sizeof(UBOPBRTextureMatrix));
    vmaFlushAllocation(allocator, uniformBuffersScene[currentBuffer].pbrTextureMatricesBuffer.allocation, 0, sizeof(UBOPBRTextureMatrix));

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
    UBOLights.lightIntensity[0] = glm::vec4(24.0f, 24.0f, 24.0f, 1.0f);
    UBOLights.lightIntensity[1] = glm::vec4(15.0f, 15.0f, 15.0f, 1.0f);
    UBOLights.lightIntensity[2] = glm::vec4(33.0f, 33.0f, 33.0f, 1.0f);
    UBOLights.lightIntensity[3] = glm::vec4(20.0f, 20.0f, 20.0f, 1.0f);

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
    vkutil::cmdTransitionImageLayout(commandBuffer, bloom.hdrSceneColor.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    cmdDrawSecne(commandBuffer);
    cmdDrawPBRTexture(commandBuffer);
    cmdDrawLight(commandBuffer);
    cmdDrawSkybox(commandBuffer);
    vkutil::cmdTransitionImageLayout(commandBuffer, bloom.hdrSceneColor.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[currentImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    cmdDrawBloomDownsample(commandBuffer);
    cmdDrawBloomUpsample(commandBuffer);
    cmdDrawBloomComposite(commandBuffer);
    //cmdDrawFullScreen(commandBuffer);
    vkutil::cmdTransitionImageLayout(commandBuffer, swapChain.images[currentImageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
}

void VulkanExample::cmdDrawSecne(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        bloom.hdrSceneColor.imageView,
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

void VulkanExample::cmdDrawPBRTexture(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        bloom.hdrSceneColor.imageView,
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
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbrTexturePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinesLayout.pbrTexturePipelineLayout, 0, 1, 
                                &descriptorSets[currentBuffer].pbrTextureDescriptor, 0, nullptr);
        pbrObject.draw(cmd, vkglTF::RenderFlags::BindImages, pipelinesLayout.pbrTexturePipelineLayout, 1);
    }
    vkutil::cmdEndRendering(cmd);
}

void VulkanExample::cmdDrawLight(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        bloom.hdrSceneColor.imageView,
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
            pushconstantsLight.VisualIntensity = lightVisualIntensity[i];
            vkCmdPushConstants(cmd, pipelinesLayout.lightPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushconstantsLight), &pushconstantsLight);
            lightObject.draw(cmd);
        }
    }
    vkutil::cmdEndRendering(cmd);
}

void VulkanExample::cmdDrawSkybox(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        bloom.hdrSceneColor.imageView,
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
    }
    vkutil::cmdEndRendering(cmd);
}

void VulkanExample::cmdDrawBloomDownsample(VkCommandBuffer cmd) {
    VkExtent2D srcExtent = bloom.sceneExtent;
    for (int i = 0; i < bloom.mips.size(); ++i) {
        BloomMip& dstMip = bloom.mips[i];
        vkutil::cmdTransitionImageLayout(cmd, dstMip.image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        
        VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(dstMip.image.imageView, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}}, VK_ATTACHMENT_LOAD_OP_CLEAR);
        vkutil::cmdBeginColorOnlyRendering(cmd, dstMip.extent, colorAttachment);
        {
            vkutil::cmdSetViewportAndScissor(cmd, dstMip.extent.width, dstMip.extent.height);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelines.bloomDownsamplePipeline);
            VkDescriptorSet srcSet = (i == 0) ? bloomDescriptorSets.hdrSceneSet : bloomDescriptorSets.mipSets[i - 1];
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelinesLayout.bloomDownsamplePipelineLayout, 0, 1, &srcSet, 0, nullptr);
            glm::vec2 srcResolution {srcExtent.width, srcExtent.height};    // 采样分辨率

            int useKarisWeight = (i == 0) ? 1 : 0; // 只有第一次下采样使用Karis权重，后续级别使用13权重
            if (!useKaris)  useKarisWeight = 0;
            struct PC {
                glm::vec2 srcResolution;
                int useKairsWeight;
                float padding; // 对齐到 16 字节
            } pc {srcResolution, useKarisWeight, 0.0f};
            vkCmdPushConstants(cmd, bloomPipelinesLayout.bloomDownsamplePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PC), &pc);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }
        vkutil::cmdEndRendering(cmd);

        vkutil::cmdTransitionImageLayout(cmd, dstMip.image.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

        srcExtent = dstMip.extent;  // 采样纹理分辨率更新
    }
}

void VulkanExample::cmdDrawBloomUpsample(VkCommandBuffer cmd) {
    for (size_t i = bloom.mips.size() - 1; i > 0; --i) {
        BloomMip& srcMip = bloom.mips[i];
        BloomMip& dstMip = bloom.mips[i - 1];

        vkutil::cmdTransitionImageLayout(cmd, dstMip.image.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(dstMip.image.imageView, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}}, VK_ATTACHMENT_LOAD_OP_LOAD);
        vkutil::cmdBeginColorOnlyRendering(cmd, dstMip.extent, colorAttachment);
        {
            vkutil::cmdSetViewportAndScissor(cmd, dstMip.extent.width, dstMip.extent.height);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelines.bloomUpsamplePipeline);
            VkDescriptorSet srcSet = bloomDescriptorSets.mipSets[i];
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelinesLayout.bloomUpsamplePipelineLayout, 0, 1, &srcSet, 0, nullptr);
            struct PC {
                glm::vec2 srcResolution;
                float filterRadius;
                float padding; // 对齐到 16 字节
            } pc{{srcMip.extent.width, srcMip.extent.height}, bloomFilterRadius, 0.0};
            vkCmdPushConstants(cmd, bloomPipelinesLayout.bloomUpsamplePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PC), &pc);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }
        vkutil::cmdEndRendering(cmd);
        vkutil::cmdTransitionImageLayout(cmd, dstMip.image.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void VulkanExample::cmdDrawBloomComposite(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(swapChain.imageViews[currentImageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}}, VK_ATTACHMENT_LOAD_OP_CLEAR);
    vkutil::cmdBeginColorOnlyRendering(cmd, VkExtent2D{width, height}, colorAttachment);
    {
        vkutil::cmdSetViewportAndScissor(cmd, width, height);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelines.bloomCompositePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelinesLayout.bloomCompositePipelineLayout, 0, 1, &bloomDescriptorSets.compositeSet, 0, nullptr);
        struct PC {
            float exposure;
            float bloomStrength;
            uint32_t enableBloom;
            float padding;
        }pc {exposure, bloomStrength, static_cast<uint32_t>(enableBloom), 0.0f};
        vkCmdPushConstants(cmd, bloomPipelinesLayout.bloomCompositePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PC), &pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        drawUI(cmd);
    }
    vkutil::cmdEndRendering(cmd);
}

void VulkanExample::cmdDrawFullScreen(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        swapChain.imageViews[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VkClearValue{{ 0.01f, 0.02f, 0.025f, 1.0f }}
    );

    VkExtent2D extent = VkExtent2D{width, height};
    vkutil::cmdBeginColorOnlyRendering(cmd, extent, colorAttachment);
    {
        vkutil::cmdSetViewportAndScissor(cmd, extent.width, extent.height);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.fullScreenPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinesLayout.fullScreenPipelineLayout, 0, 1, 
                                &descriptorSets[currentBuffer].fullScreenDescriptor, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);

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
    VkPipelineLayout pipelinelayout = vkutil::createPipelineLayout(
        device,
        {descriptorsetlayout},
        {vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PushConstantsIrradiance), 0)}
    );

    vkutil::PipelineBuilder builder;
    builder
        .setPipelineLayout(pipelinelayout)
        .setShaders(
            loadShader(getShadersPath() + filterCubeVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + irradianceFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({
            vkglTF::VertexComponent::Position,
            vkglTF::VertexComponent::Normal,
            vkglTF::VertexComponent::UV
        }))
        .setColorAttachmentFormat(format)
        .disableDepthTest()
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();
    VkPipeline pipeline = builder.build(device, pipelineCache);

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
            prefilterPushconstant.sampleCounts = 4096;
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
