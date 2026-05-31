#include "lab3.h"

#include "vk_descriptors.h"
#include "vk_pipelines.h"
#include "vk_rendering.h"
#include "VulkanTools.h"

VulkanExample::VulkanExample() : VulkanExampleBase() {
    title = "Lab3 : Deferred Rendering + SSAO + Instancing";

    width = 1920;
    height = 1080;

    apiVersion = VK_API_VERSION_1_3;
    useDynamicRendering = true;
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.synchronization2 = VK_TRUE;
    deviceCreatepNextChain = &vulkan13Features;

    camera.type = Camera::CameraType::firstperson;
    camera.movementSpeed = 5.0f;
    camera.rotationSpeed = 0.25f;
    camera.setPosition(glm::vec3(0.0f, 2.0f, -8.0f));
    camera.setRotation(glm::vec3(-10.0f, 0.0f, 0.0f));
    camera.setPerspective(60.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 256.0f);

    debugViewNames = {
        "Final",
        "GBuffer Albedo",
        "GBuffer Normal",
        "GBuffer Roughness",
        "GBuffer Metallic",
        "GBuffer Emissive",
        "GBuffer AO",
        "Depth",
        "SSAO Raw",
        "SSAO Blurred",
        "Direct Only",
        "IBL Only",
        "Emissive Only"
    };
}

VulkanExample::~VulkanExample() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);

        destroyPipelines();
        destroyBloomPass();
        destroyDescriptors();
        destroyUniformBuffers();
        destroyStaticResources();
        destroyFrameResources();
        destroyAssets();
        destroySamplers();
        destroyVmaAllocator();
    }
}

void VulkanExample::prepare() {
    createVmaAllocator();
    VulkanExampleBase::prepare();

    loadAssets();
    createSamplers();
    createFrameResources();
    createStaticResources();
    createUniformBuffers();
    createDescriptorPool();
    createBloomPass();
    setupDescriptors();
    createPipelines();
    updateUniformBuffers();

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

void VulkanExample::createSamplers() {
    VkSamplerCreateInfo samplerCI = vks::initializers::samplerCreateInfo();
    samplerCI.magFilter = VK_FILTER_NEAREST;
    samplerCI.minFilter = VK_FILTER_NEAREST;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = 0.0f;
    VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &gBufferSampler));

    samplerCI.magFilter = VK_FILTER_LINEAR;
    samplerCI.minFilter = VK_FILTER_LINEAR;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &screenSampler));
}

void VulkanExample::destroySamplers() {
    if (gBufferSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, gBufferSampler, nullptr);
        gBufferSampler = VK_NULL_HANDLE;
    }
    if (screenSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, screenSampler, nullptr);
        screenSampler = VK_NULL_HANDLE;
    }
}

void VulkanExample::loadAssets() {
    // Configure glTF material texture descriptors for the G-Buffer pass.
    vkglTF::descriptorBindingFlags =
        vkglTF::DescriptorBindingFlags::ImageBaseColor |
        vkglTF::DescriptorBindingFlags::ImageMetallicRoughness |
        vkglTF::DescriptorBindingFlags::ImageNormalMap |
        vkglTF::DescriptorBindingFlags::ImageOcclusionMap |
        vkglTF::DescriptorBindingFlags::ImageEmissiveMap;

    sceneModel.loadFromFile(
        getAssetPath() + pbrModelPath,
        vulkanDevice,
        queue,
        vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY
    );

    lightProxyModel.loadFromFile(
        getAssetPath() + lightProxyModelPath,
        vulkanDevice,
        queue,
        vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY
    );
}

void VulkanExample::destroyAssets() {
    // vkglTF::Model releases its own resources in its destructor.
}

VulkanExample::RenderAttachment VulkanExample::createColorAttachment(VkExtent2D extent, VkFormat format) {
    RenderAttachment attachment{};
    attachment.image = vkutil::createAllocatedImage(
        device,
        allocator,
        VkExtent3D{extent.width, extent.height, 1},
        format,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    attachment.descriptor = vkutil::descriptorImageInfo(screenSampler, attachment.image.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return attachment;
}

VulkanExample::RenderAttachment VulkanExample::createDepthAttachment(VkExtent2D extent, VkFormat format) {
    RenderAttachment attachment{};
    attachment.image = vkutil::createAllocatedImage(
        device,
        allocator,
        VkExtent3D{extent.width, extent.height, 1},
        format,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );
    attachment.descriptor = vkutil::descriptorImageInfo(gBufferSampler, attachment.image.imageView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    return attachment;
}

void VulkanExample::destroyAttachment(RenderAttachment& attachment) {
    vkutil::destroyAllocatedImage(device, allocator, attachment.image);
    attachment.descriptor = {};
}

void VulkanExample::createFrameResources() {
    const VkExtent2D extent{
        std::max(1u, width),
        std::max(1u, height)
    };

    gBuffer.extent = extent;
    gBuffer.depthAttachmentFormat = depthFormat != VK_FORMAT_UNDEFINED ? depthFormat : VK_FORMAT_D32_SFLOAT;
    gBuffer.albedoMetallic = createColorAttachment(extent, gBuffer.albedoMetallicFormat);
    gBuffer.normalRoughness = createColorAttachment(extent, gBuffer.normalRoughnessFormat);
    gBuffer.emissiveAO = createColorAttachment(extent, gBuffer.emissiveAOFormat);
    gBuffer.depth = createDepthAttachment(extent, gBuffer.depthAttachmentFormat);

    // Keep G-Buffer sampling nearest to avoid filtering normals, roughness and depth.
    gBuffer.albedoMetallic.descriptor.sampler = gBufferSampler;
    gBuffer.normalRoughness.descriptor.sampler = gBufferSampler;
    gBuffer.emissiveAO.descriptor.sampler = gBufferSampler;

    ssao.extent = extent;
    ssao.raw = createColorAttachment(extent, ssao.format);
    ssao.blurred = createColorAttachment(extent, ssao.format);

    hdr.extent = extent;
    hdr.sceneColor = createColorAttachment(extent, hdr.format);
}

void VulkanExample::destroyFrameResources() {
    destroyAttachment(gBuffer.albedoMetallic);
    destroyAttachment(gBuffer.normalRoughness);
    destroyAttachment(gBuffer.emissiveAO);
    destroyAttachment(gBuffer.depth);
    gBuffer.extent = {};

    destroyAttachment(ssao.raw);
    destroyAttachment(ssao.blurred);
    ssao.extent = {};

    destroyAttachment(hdr.sceneColor);
    hdr.extent = {};
}

void VulkanExample::createStaticResources() {
    createSSAONoiseTexture();
    createSSAOKernelBuffer();
    createInstanceBuffer();
}

void VulkanExample::destroyStaticResources() {
    vkutil::destroyTexture(device, allocator, ssao.noise);
    vkutil::destroyAllocatedBuffer(allocator, ssao.kernel);
    vkutil::destroyAllocatedBuffer(allocator, instanceBuffer);
    instanceCpuData.clear();
}

void VulkanExample::createSSAONoiseTexture() {
    std::default_random_engine rng(1337);
    std::uniform_real_distribution<float> randomFloat(-1.0f, 1.0f);

    std::vector<glm::vec4> noise(kSSAONoiseDim * kSSAONoiseDim);
    for (glm::vec4& value : noise) {
        glm::vec3 dir(randomFloat(rng), randomFloat(rng), 0.0f);
        dir = glm::normalize(dir);
        value = glm::vec4(dir, 0.0f);
    }

    vkutil::ImmediateSubmitContext submitContext{};
    submitContext.device = device;
    submitContext.queue = queue;
    submitContext.commandPool = cmdPool;

    ssao.noise = vkutil::createTexture2DFromPixels(
        device,
        allocator,
        submitContext,
        noise.data(),
        kSSAONoiseDim,
        kSSAONoiseDim,
        VK_FORMAT_R32G32B32A32_SFLOAT
    );
}

void VulkanExample::createSSAOKernelBuffer() {
    std::default_random_engine rng(2026);
    std::uniform_real_distribution<float> random01(0.0f, 1.0f);
    std::uniform_real_distribution<float> randomSigned(-1.0f, 1.0f);

    std::array<glm::vec4, kSSAOSize> kernel{};
    for (uint32_t i = 0; i < kSSAOSize; ++i) {
        glm::vec3 sample(randomSigned(rng), randomSigned(rng), random01(rng));
        sample = glm::normalize(sample);
        sample *= random01(rng);

        float scale = static_cast<float>(i) / static_cast<float>(kSSAOSize);
        scale = glm::mix(0.1f, 1.0f, scale * scale);
        kernel[i] = glm::vec4(sample * scale, 0.0f);
    }

    ssao.kernel = vkutil::createAllocatedBuffer(
        allocator,
        sizeof(kernel),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        VMA_MEMORY_USAGE_AUTO
    );

    memcpy(ssao.kernel.allocationInfo.pMappedData, kernel.data(), sizeof(kernel));
    vmaFlushAllocation(allocator, ssao.kernel.allocation, 0, sizeof(kernel));
}

void VulkanExample::createInstanceBuffer() {
    instanceCpuData.resize(kMaxInstanceCount);
    updateInstanceBuffer();

    instanceBuffer = vkutil::createAllocatedBuffer(
        allocator,
        sizeof(InstanceData) * instanceCpuData.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        VMA_MEMORY_USAGE_AUTO
    );

    memcpy(instanceBuffer.allocationInfo.pMappedData, instanceCpuData.data(), instanceBuffer.size);
    vmaFlushAllocation(allocator, instanceBuffer.allocation, 0, instanceBuffer.size);
}

void VulkanExample::updateInstanceBuffer() {
    const uint32_t gridDim = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(kMaxInstanceCount))));
    const float spacing = 2.5f;
    const glm::vec3 originOffset(
        -0.5f * spacing * static_cast<float>(gridDim - 1),
        0.0f,
        -0.5f * spacing * static_cast<float>(gridDim - 1)
    );

    for (uint32_t i = 0; i < kMaxInstanceCount; ++i) {
        const uint32_t x = i % gridDim;
        const uint32_t z = i / gridDim;

        glm::mat4 model = glm::translate(glm::mat4(1.0f), originOffset + glm::vec3(x * spacing, 0.0f, z * spacing));
        model = glm::scale(model, glm::vec3(0.85f));

        const float metallic = static_cast<float>(x) / static_cast<float>(std::max(1u, gridDim - 1));
        const float roughness = glm::clamp(static_cast<float>(z) / static_cast<float>(std::max(1u, gridDim - 1)), 0.05f, 1.0f);

        InstanceData& instance = instanceCpuData[i];
        instance.model = model;
        instance.normalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(model)));
        instance.color = glm::vec4(1.0f, 0.78f, 0.35f, 1.0f);
        // PBR material values come from glTF textures; x/y are optional per-instance modulation factors.
        instance.materialParams = glm::vec4(metallic, roughness, 1.0f, 0.0f);
    }
}

void VulkanExample::createUniformBuffers() {
    for (FrameUniformBuffers& buffers : uniformBuffers) {
        buffers.camera = vkutil::createAllocatedBuffer(
            allocator,
            sizeof(CameraUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_MEMORY_USAGE_AUTO
        );

        buffers.lights = vkutil::createAllocatedBuffer(
            allocator,
            sizeof(LightsUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_MEMORY_USAGE_AUTO
        );

        buffers.ssaoParams = vkutil::createAllocatedBuffer(
            allocator,
            sizeof(SSAOParamsUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_MEMORY_USAGE_AUTO
        );
    }
}

void VulkanExample::destroyUniformBuffers() {
    for (FrameUniformBuffers& buffers : uniformBuffers) {
        vkutil::destroyAllocatedBuffer(allocator, buffers.camera);
        vkutil::destroyAllocatedBuffer(allocator, buffers.lights);
        vkutil::destroyAllocatedBuffer(allocator, buffers.ssaoParams);
    }
}

void VulkanExample::createDescriptorPool() {
    std::vector<VkDescriptorPoolSize> poolSizes = {
        vkutil::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames * 24),
        vkutil::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxConcurrentFrames * 4),
        vkutil::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames * 30)
    };

    VkDescriptorPoolCreateInfo poolCI = vkutil::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames * 20);
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolCI, nullptr, &descriptorPool));
}

void VulkanExample::createBloomPass() {
    vkutil::BloomPass::InitInfo initInfo{};
    initInfo.device = device;
    initInfo.allocator = allocator;
    initInfo.descriptorPool = descriptorPool;
    initInfo.pipelineCache = pipelineCache;
    initInfo.hdrFormat = hdr.format;
    initInfo.shaderPath = getShadersPath();

    bloomPass.init(initInfo);
    resizeBloomPass();
}

void VulkanExample::resizeBloomPass() {
    if (!bloomPass.isInitialized()) {
        return;
    }

    // BloomPass 只生成线性 HDR bloom 纹理；最终 tone mapping/gamma 仍放在 Final debug shader。
    bloomPass.resize(hdr.extent, hdr.format, kMaxBloomMipCount);
    bloomPass.updateSource(hdr.sceneColor.descriptor);
}

void VulkanExample::destroyBloomPass() {
    bloomPass.destroy();
}

void VulkanExample::setupDescriptors() {
    createDescriptorSetLayouts();
    allocateDescriptorSets();
    updateDescriptorSets();
}

void VulkanExample::createDescriptorSetLayouts() {
    std::vector<VkDescriptorSetLayoutBinding> sceneBindings = {
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1),
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 2)
    };
    VkDescriptorSetLayoutCreateInfo sceneLayoutCI = vkutil::descriptorSetLayoutCreateInfo(sceneBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &sceneLayoutCI, nullptr, &descriptorSetLayouts.scene));

    std::vector<VkDescriptorSetLayoutBinding> gBufferDebugBindings = {
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0), // albedo + metallic
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1), // normal + roughness
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2), // emissive + material AO
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),  // depth
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 4),  // final HDR scene
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 5)   // final bloom texture
    };
    VkDescriptorSetLayoutCreateInfo gBufferDebugLayoutCI = vkutil::descriptorSetLayoutCreateInfo(gBufferDebugBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &gBufferDebugLayoutCI, nullptr, &descriptorSetLayouts.gBufferDebug));

    std::vector<VkDescriptorSetLayoutBinding> ssaoBindings = {
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0), // depth
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1), // normal
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2), // noise
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),          // kernel
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 4)           // params
    };
    VkDescriptorSetLayoutCreateInfo ssaoLayoutCI = vkutil::descriptorSetLayoutCreateInfo(ssaoBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &ssaoLayoutCI, nullptr, &descriptorSetLayouts.ssao));

    std::vector<VkDescriptorSetLayoutBinding> ssaoBlurBindings = {
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0), // ssao raw
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1), // depth
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2)  // normal
    };
    VkDescriptorSetLayoutCreateInfo ssaoBlurLayoutCI = vkutil::descriptorSetLayoutCreateInfo(ssaoBlurBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &ssaoBlurLayoutCI, nullptr, &descriptorSetLayouts.ssaoBlur));

    std::vector<VkDescriptorSetLayoutBinding> deferredBindings = {
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0), // albedo + metallic
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1), // normal + roughness
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2), // emissive + material AO
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3), // depth
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 4), // SSAO
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 5),
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 6)
    };
    VkDescriptorSetLayoutCreateInfo deferredLayoutCI = vkutil::descriptorSetLayoutCreateInfo(deferredBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &deferredLayoutCI, nullptr, &descriptorSetLayouts.deferredLighting));

    std::vector<VkDescriptorSetLayoutBinding> compositeBindings = {
        vkutil::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0)
    };
    VkDescriptorSetLayoutCreateInfo compositeLayoutCI = vkutil::descriptorSetLayoutCreateInfo(compositeBindings);
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &compositeLayoutCI, nullptr, &descriptorSetLayouts.composite));
}

void VulkanExample::allocateDescriptorSets() {
    for (DescriptorSets& sets : descriptorSets) {
        VkDescriptorSetAllocateInfo allocInfo = vkutil::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.scene, 1);
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &sets.scene));

        allocInfo = vkutil::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.gBufferDebug, 1);
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &sets.gBufferDebug));

        allocInfo = vkutil::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.ssao, 1);
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &sets.ssao));

        allocInfo = vkutil::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.ssaoBlur, 1);
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &sets.ssaoBlur));

        allocInfo = vkutil::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.deferredLighting, 1);
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &sets.deferredLighting));

        allocInfo = vkutil::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.composite, 1);
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &sets.composite));
    }
}

void VulkanExample::updateDescriptorSets() {
    VkDescriptorBufferInfo kernelInfo = vkutil::descriptorBufferInfo(ssao.kernel.handle, ssao.kernel.size);
    VkDescriptorBufferInfo instanceInfo = vkutil::descriptorBufferInfo(instanceBuffer.handle, instanceBuffer.size);
    VkDescriptorImageInfo bloomOutputInfo = bloomPass.hasOutput() ? bloomPass.getBloomDescriptor() : hdr.sceneColor.descriptor;

    for (size_t i = 0; i < descriptorSets.size(); ++i) {
        VkDescriptorBufferInfo cameraInfo = vkutil::descriptorBufferInfo(uniformBuffers[i].camera.handle, sizeof(CameraUBO));
        VkDescriptorBufferInfo lightsInfo = vkutil::descriptorBufferInfo(uniformBuffers[i].lights.handle, sizeof(LightsUBO));
        VkDescriptorBufferInfo ssaoParamsInfo = vkutil::descriptorBufferInfo(uniformBuffers[i].ssaoParams.handle, sizeof(SSAOParamsUBO));

        std::vector<VkWriteDescriptorSet> sceneWrites = {
            vkutil::writeUniformBuffer(descriptorSets[i].scene, 0, &cameraInfo),
            vkutil::writeUniformBuffer(descriptorSets[i].scene, 1, &lightsInfo),
            vkutil::writeBufferDescriptorSet(descriptorSets[i].scene, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, &instanceInfo)
        };
        vkutil::updateDescriptorSet(device, sceneWrites);

        std::vector<VkWriteDescriptorSet> gBufferDebugWrites = {
            vkutil::writeCombinedImageSampler(descriptorSets[i].gBufferDebug, 0, &gBuffer.albedoMetallic.descriptor),
            vkutil::writeCombinedImageSampler(descriptorSets[i].gBufferDebug, 1, &gBuffer.normalRoughness.descriptor),
            vkutil::writeCombinedImageSampler(descriptorSets[i].gBufferDebug, 2, &gBuffer.emissiveAO.descriptor),
            vkutil::writeCombinedImageSampler(descriptorSets[i].gBufferDebug, 3, &gBuffer.depth.descriptor),
            vkutil::writeCombinedImageSampler(descriptorSets[i].gBufferDebug, 4, &hdr.sceneColor.descriptor),
            // Final 视图读取 BloomPass 产出的线性 HDR bloom 纹理，再统一做显示变换。
            vkutil::writeCombinedImageSampler(descriptorSets[i].gBufferDebug, 5, &bloomOutputInfo)
        };
        vkutil::updateDescriptorSet(device, gBufferDebugWrites);

        std::vector<VkWriteDescriptorSet> ssaoWrites = {
            vkutil::writeCombinedImageSampler(descriptorSets[i].ssao, 0, &gBuffer.depth.descriptor),
            vkutil::writeCombinedImageSampler(descriptorSets[i].ssao, 1, &gBuffer.normalRoughness.descriptor),
            vkutil::writeCombinedImageSampler(descriptorSets[i].ssao, 2, &ssao.noise.descriptor),
            vkutil::writeUniformBuffer(descriptorSets[i].ssao, 3, &kernelInfo),
            vkutil::writeUniformBuffer(descriptorSets[i].ssao, 4, &ssaoParamsInfo)
        };
        vkutil::updateDescriptorSet(device, ssaoWrites);

        std::vector<VkWriteDescriptorSet> ssaoBlurWrites = {
            vkutil::writeCombinedImageSampler(descriptorSets[i].ssaoBlur, 0, &ssao.raw.descriptor),
            vkutil::writeCombinedImageSampler(descriptorSets[i].ssaoBlur, 1, &gBuffer.depth.descriptor),
            vkutil::writeCombinedImageSampler(descriptorSets[i].ssaoBlur, 2, &gBuffer.normalRoughness.descriptor)
        };
        vkutil::updateDescriptorSet(device, ssaoBlurWrites);

        std::vector<VkWriteDescriptorSet> deferredWrites = {
            vkutil::writeCombinedImageSampler(descriptorSets[i].deferredLighting, 0, &gBuffer.albedoMetallic.descriptor),
            vkutil::writeCombinedImageSampler(descriptorSets[i].deferredLighting, 1, &gBuffer.normalRoughness.descriptor),
            vkutil::writeCombinedImageSampler(descriptorSets[i].deferredLighting, 2, &gBuffer.emissiveAO.descriptor),
            vkutil::writeCombinedImageSampler(descriptorSets[i].deferredLighting, 3, &gBuffer.depth.descriptor),
            vkutil::writeCombinedImageSampler(descriptorSets[i].deferredLighting, 4, &ssao.blurred.descriptor),
            vkutil::writeUniformBuffer(descriptorSets[i].deferredLighting, 5, &cameraInfo),
            vkutil::writeUniformBuffer(descriptorSets[i].deferredLighting, 6, &lightsInfo)
        };
        vkutil::updateDescriptorSet(device, deferredWrites);

        std::vector<VkWriteDescriptorSet> compositeWrites = {
            vkutil::writeCombinedImageSampler(descriptorSets[i].composite, 0, &hdr.sceneColor.descriptor)
        };
        vkutil::updateDescriptorSet(device, compositeWrites);
    }
}

void VulkanExample::destroyDescriptors() {
    auto destroyLayout = [this](VkDescriptorSetLayout& layout) {
        if (layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, layout, nullptr);
            layout = VK_NULL_HANDLE;
        }
    };

    destroyLayout(descriptorSetLayouts.scene);
    destroyLayout(descriptorSetLayouts.gBufferDebug);
    destroyLayout(descriptorSetLayouts.ssao);
    destroyLayout(descriptorSetLayouts.ssaoBlur);
    destroyLayout(descriptorSetLayouts.deferredLighting);
    destroyLayout(descriptorSetLayouts.composite);
}

void VulkanExample::createPipelines() {
    // Create pipeline layouts used by the Lab3 passes.
    pipelineLayouts.gBuffer = vkutil::createPipelineLayout(device, {descriptorSetLayouts.scene, vkglTF::descriptorSetLayoutImage});
    VkPushConstantRange gBufferDebugPushConstant{};
    gBufferDebugPushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    gBufferDebugPushConstant.offset = 0;
    gBufferDebugPushConstant.size = sizeof(GBufferDebugPushConstants);
    pipelineLayouts.gBufferDebug = vkutil::createPipelineLayout(
        device,
        {descriptorSetLayouts.gBufferDebug},
        {gBufferDebugPushConstant}
    );

    pipelineLayouts.ssao = vkutil::createPipelineLayout(device, {descriptorSetLayouts.ssao});
    pipelineLayouts.ssaoBlur = vkutil::createPipelineLayout(device, {descriptorSetLayouts.ssaoBlur});
    pipelineLayouts.deferredLighting = vkutil::createPipelineLayout(device, {descriptorSetLayouts.deferredLighting});
    pipelineLayouts.lightProxy = vkutil::createPipelineLayout(device, {descriptorSetLayouts.scene});
    pipelineLayouts.composite = vkutil::createPipelineLayout(device, {descriptorSetLayouts.composite});

    // TODO(Lab3): shader 鍐欏ソ鍚庡湪杩欓噷鍒涘缓 gBuffer / SSAO / deferred lighting / composite pipelines銆?    // G-Buffer pipeline
    // G-Buffer pipeline: writes MRT, with material textures bound by each glTF primitive.
    vkutil::PipelineBuilder gBufferBuilder;
    gBufferBuilder.setPipelineLayout(pipelineLayouts.gBuffer)
        .setShaders(
            loadShader(getShadersPath() + gBufferVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + gBufferFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Tangent, vkglTF::VertexComponent::Color }))
        .setColorAttachmentFormats({gBuffer.albedoMetallicFormat, gBuffer.normalRoughnessFormat, gBuffer.emissiveAOFormat})
        .setDepthFormat(gBuffer.depthAttachmentFormat)
        .enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();
    pipelines.gBufferInstanced = gBufferBuilder.build(device, pipelineCache);

    // G-Buffer debug pipeline: visualize selected G-Buffer/HDR textures on the swapchain.
    const std::string fullscreenVertexPath = getShadersPath() + gBufferDebugVertexShader;
    const std::string gBufferDebugFragmentPath = getShadersPath() + gBufferDebugFragmentShader;
    if (vks::tools::fileExists(fullscreenVertexPath) && vks::tools::fileExists(gBufferDebugFragmentPath)) {
        vkutil::PipelineBuilder gBufferDebugBuilder;
        gBufferDebugBuilder.setPipelineLayout(pipelineLayouts.gBufferDebug)
            .setShaders(
                loadShader(fullscreenVertexPath, VK_SHADER_STAGE_VERTEX_BIT),
                loadShader(gBufferDebugFragmentPath, VK_SHADER_STAGE_FRAGMENT_BIT))
            .setEmptyVertexInput()
            .setColorAttachmentFormat(swapChain.colorFormat)
            .disableDepthTest()
            .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .disableBlending();
        pipelines.gBufferDebug = gBufferDebugBuilder.build(device, pipelineCache);
    }

    // Lighting pipeline
    const std::string lightingVertexPath = getShadersPath() + gBufferDebugVertexShader;
    const std::string lightingFragmentPath = getShadersPath() + deferredLightingFragmentShader;
    vkutil::PipelineBuilder lightingBuilder;
    lightingBuilder.setPipelineLayout(pipelineLayouts.deferredLighting)
        .setShaders(
            loadShader(lightingVertexPath, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(lightingFragmentPath, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setEmptyVertexInput()
        .setColorAttachmentFormat(hdr.format)
        .setDepthFormat(gBuffer.depthAttachmentFormat)
        .disableDepthTest()
        .setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();
    pipelines.deferredLighting = lightingBuilder.build(device, pipelineCache);

    // Light proxy pipeline: draw visible emissive spheres into the HDR scene before Bloom.
    vkutil::PipelineBuilder lightProxyBuilder;
    lightProxyBuilder.setPipelineLayout(pipelineLayouts.lightProxy)
        .setShaders(
            loadShader(getShadersPath() + lightProxyVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + lightProxyFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position }))
        .setColorAttachmentFormat(hdr.format)
        .setDepthFormat(gBuffer.depthAttachmentFormat)
        .enableDepthTest(false, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();
    pipelines.lightProxy = lightProxyBuilder.build(device, pipelineCache);
}

void VulkanExample::destroyPipelines() {
    std::array<VkPipeline*, 8> pipelineHandles = {
        &pipelines.gBuffer,
        &pipelines.gBufferInstanced,
        &pipelines.gBufferDebug,
        &pipelines.ssao,
        &pipelines.ssaoBlur,
        &pipelines.deferredLighting,
        &pipelines.lightProxy,
        &pipelines.composite
    };
    for (VkPipeline* pipeline : pipelineHandles) {
        if (*pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, *pipeline, nullptr);
            *pipeline = VK_NULL_HANDLE;
        }
    }

    std::array<VkPipelineLayout*, 7> layoutHandles = {
        &pipelineLayouts.gBuffer,
        &pipelineLayouts.gBufferDebug,
        &pipelineLayouts.ssao,
        &pipelineLayouts.ssaoBlur,
        &pipelineLayouts.deferredLighting,
        &pipelineLayouts.lightProxy,
        &pipelineLayouts.composite
    };
    for (VkPipelineLayout* layout : layoutHandles) {
        if (*layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, *layout, nullptr);
            *layout = VK_NULL_HANDLE;
        }
    }
}

void VulkanExample::updateLights() {
    renderSettings.lightCount = glm::clamp(
        renderSettings.lightCount,
        1,
        static_cast<int32_t>(kMaxLightCount)
    );

    lightsUBO.lightCount = glm::ivec4(renderSettings.lightCount, 0, 0, 0);

    const float goldenAngle = 2.39996323f;
    const float maxRadius = 34.0f;
    const uint32_t activeLightCount = static_cast<uint32_t>(renderSettings.lightCount);

    for (uint32_t i = 0; i < kMaxLightCount; ++i) {
        Light& light = lightsUBO.lights[i];

        if (i >= activeLightCount) {
            light.position = glm::vec4(0.0f);
            light.color = glm::vec4(0.0f);
            light.intensity = glm::vec4(0.0f);
            continue;
        }

        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(activeLightCount);
        const float angle = static_cast<float>(i) * goldenAngle;
        const float radius = maxRadius * std::sqrt(t);
        const float height = -5.0f + 3.5f * (0.5f + 0.5f * std::sin(static_cast<float>(i) * 0.73f));

        const glm::vec3 position(
            std::cos(angle) * radius,
            height,
            std::sin(angle) * radius
        );

        glm::vec3 color(
            0.5f + 0.5f * std::sin(static_cast<float>(i) * 0.37f + 0.0f),
            0.5f + 0.5f * std::sin(static_cast<float>(i) * 0.61f + 2.0f),
            0.5f + 0.5f * std::sin(static_cast<float>(i) * 0.83f + 4.0f)
        );
        color = glm::mix(color, glm::vec3(1.0f), 0.18f);

        const float variation = 0.75f + 0.5f * (static_cast<float>(i % 7) / 6.0f);
        const float lightingIntensity = renderSettings.lightIntensity * variation;
        const float visualIntensity = renderSettings.lightVisualIntensity * variation;
        const float visualRadius = renderSettings.lightVisualRadius;
        // 将光源球的显示半径映射到实际照明半径：UI 调整 Light Radius 时，early culling 的范围也会同步变化。
        const float lightingRadiusScale = 55.0f;
        const float minLightingRadius = 1.0f;
        const float lightingRadius = glm::max(visualRadius * lightingRadiusScale * std::sqrt(variation), minLightingRadius);

        light.position = glm::vec4(position, 1.0f);
        light.color = glm::vec4(color, 1.0f);
        // x 给 deferred/PBR 照明使用，y/z 只给光源球可视化使用，两套强度彼此独立。
        light.intensity = glm::vec4(lightingIntensity, visualIntensity, visualRadius, lightingRadius);
    }
}

void VulkanExample::updateBloomSettings() {
    renderSettings.bloomMipCount = glm::clamp(
        renderSettings.bloomMipCount,
        1,
        static_cast<int32_t>(kMaxBloomMipCount)
    );

    bloomSettings.enabled = renderSettings.enableBloom != 0 && renderSettings.debugView == 0;
    bloomSettings.mipCount = static_cast<uint32_t>(renderSettings.bloomMipCount);
    bloomSettings.filterRadius = renderSettings.bloomFilterRadius;
    bloomSettings.useKarisAverage = renderSettings.bloomUseKaris != 0;
}

void VulkanExample::updateUniformBuffers() {
    cameraUBO.projection = camera.matrices.perspective;
    cameraUBO.view = camera.matrices.view;
    cameraUBO.inverseProjection = glm::inverse(cameraUBO.projection);
    cameraUBO.inverseView = glm::inverse(cameraUBO.view);
    cameraUBO.cameraPos = glm::vec4(glm::vec3(cameraUBO.inverseView[3]), 1.0f);
    cameraUBO.screenSize = glm::vec4(
        static_cast<float>(width),
        static_cast<float>(height),
        1.0f / static_cast<float>(std::max(1u, width)),
        1.0f / static_cast<float>(std::max(1u, height))
    );

    updateLights();

    renderSettings.instanceCount = glm::clamp(renderSettings.instanceCount, 1, static_cast<int32_t>(kMaxInstanceCount));

    ssaoParamsUBO.projection = camera.matrices.perspective;
    ssaoParamsUBO.inverseProjection = glm::inverse(camera.matrices.perspective);
    ssaoParamsUBO.params = glm::vec4(
        ssaoSettings.radius,
        ssaoSettings.bias,
        ssaoSettings.power,
        static_cast<float>(ssaoSettings.kernelSize)
    );
    ssaoParamsUBO.noiseScale = glm::vec4(
        static_cast<float>(std::max(1u, width)) / static_cast<float>(kSSAONoiseDim),
        static_cast<float>(std::max(1u, height)) / static_cast<float>(kSSAONoiseDim),
        static_cast<float>(kSSAONoiseDim),
        0.0f
    );

    FrameUniformBuffers& buffers = uniformBuffers[currentBuffer];

    memcpy(buffers.camera.allocationInfo.pMappedData, &cameraUBO, sizeof(cameraUBO));
    vmaFlushAllocation(allocator, buffers.camera.allocation, 0, sizeof(cameraUBO));

    memcpy(buffers.lights.allocationInfo.pMappedData, &lightsUBO, sizeof(lightsUBO));
    vmaFlushAllocation(allocator, buffers.lights.allocation, 0, sizeof(lightsUBO));

    memcpy(buffers.ssaoParams.allocationInfo.pMappedData, &ssaoParamsUBO, sizeof(ssaoParamsUBO));
    vmaFlushAllocation(allocator, buffers.ssaoParams.allocation, 0, sizeof(ssaoParamsUBO));
}

void VulkanExample::render() {
    if (!prepared) {
        return;
    }

    VulkanExampleBase::prepareFrame();
    updateUniformBuffers();
    buildCommandBuffer();
    VulkanExampleBase::submitFrame();
}

void VulkanExample::buildCommandBuffer() {
    VkCommandBuffer commandBuffer = drawCmdBuffers[currentBuffer];
    VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
    VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufInfo));

    // Generate G-Buffer, run deferred lighting, then display through the debug/fullscreen pass.
    transitionGBufferForWriting(commandBuffer);
    cmdDrawGBuffer(commandBuffer);
    transitionGBufferForSampling(commandBuffer);
    vkutil::cmdTransitionTrackedImageLayout(commandBuffer, hdr.sceneColor.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    cmdDrawDeferredLighting(commandBuffer);
    vkutil::cmdTransitionTrackedImageLayout(commandBuffer, hdr.sceneColor.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    updateBloomSettings();
    bloomPass.record(commandBuffer, bloomSettings);

    cmdDrawGBufferDebug(commandBuffer);

    VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
}

void VulkanExample::transitionGBufferForWriting(VkCommandBuffer cmd) {
    // Rewrite the G-Buffer every frame, so switch color/depth images to attachment layouts first.
    vkutil::cmdTransitionTrackedImageLayout(cmd, gBuffer.albedoMetallic.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    vkutil::cmdTransitionTrackedImageLayout(cmd, gBuffer.normalRoughness.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    vkutil::cmdTransitionTrackedImageLayout(cmd, gBuffer.emissiveAO.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    vkutil::cmdTransitionTrackedImageLayout(cmd, gBuffer.depth.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void VulkanExample::transitionGBufferForSampling(VkCommandBuffer cmd) {
    // After the G-Buffer pass, later passes sample these images as textures.
    vkutil::cmdTransitionTrackedImageLayout(cmd, gBuffer.albedoMetallic.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    vkutil::cmdTransitionTrackedImageLayout(cmd, gBuffer.normalRoughness.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    vkutil::cmdTransitionTrackedImageLayout(cmd, gBuffer.emissiveAO.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    vkutil::cmdTransitionTrackedImageLayout(cmd, gBuffer.depth.image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void VulkanExample::cmdDrawGBuffer(VkCommandBuffer cmd) {
    // Keep the pass as a no-op until the G-Buffer pipeline is available.
    if (pipelines.gBufferInstanced == VK_NULL_HANDLE) {
        return;
    }

    std::array<VkRenderingAttachmentInfo, 3> colorAttachments = {
        vkutil::renderingAttachmentInfo(
            gBuffer.albedoMetallic.image.imageView,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}}
        ),
        vkutil::renderingAttachmentInfo(
            gBuffer.normalRoughness.image.imageView,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VkClearValue{{0.5f, 0.5f, 1.0f, 1.0f}}
        ),
        vkutil::renderingAttachmentInfo(
            gBuffer.emissiveAO.image.imageView,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}}
        )
    };

    VkRenderingAttachmentInfo depthAttachment = vkutil::renderingdepthAttachmentInfo(
        gBuffer.depth.image.imageView,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        1.0f
    );

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, gBuffer.extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    renderingInfo.pColorAttachments = colorAttachments.data();
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    {
        vkutil::cmdSetViewportAndScissor(cmd, gBuffer.extent.width, gBuffer.extent.height);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.gBufferInstanced);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayouts.gBuffer,
            0,
            1,
            &descriptorSets[currentBuffer].scene,
            0,
            nullptr
        );

        sceneModel.drawInstanced(
            cmd,
            static_cast<uint32_t>(renderSettings.instanceCount),
            0,
            vkglTF::RenderFlags::BindImages,
            pipelineLayouts.gBuffer,
            1
        );
    }
    vkCmdEndRendering(cmd);
}

void VulkanExample::cmdDrawGBufferDebug(VkCommandBuffer cmd) {
    vkutil::cmdTransitionImageLayout(
        cmd,
        swapChain.images[currentImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    VkClearValue clearColor{};
    clearColor.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        swapChain.imageViews[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        clearColor
    );

    vkutil::cmdBeginColorOnlyRendering(cmd, VkExtent2D{width, height}, colorAttachment);
    {
        vkutil::cmdSetViewportAndScissor(cmd, width, height);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.gBufferDebug);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayouts.gBufferDebug,
            0,
            1,
            &descriptorSets[currentBuffer].gBufferDebug,
            0,
            nullptr
        );

        GBufferDebugPushConstants pushConstants{};
        pushConstants.debugView = renderSettings.debugView;
        pushConstants.enableBloom = renderSettings.enableBloom;
        pushConstants.nearPlane = camera.getNearClip();
        pushConstants.farPlane = camera.getFarClip();
        pushConstants.exposure = renderSettings.exposure;
        pushConstants.bloomStrength = renderSettings.bloomStrength;
        vkCmdPushConstants(
            cmd,
            pipelineLayouts.gBufferDebug,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(GBufferDebugPushConstants),
            &pushConstants
        );

        // fullscreen.vert generates one large triangle that covers the whole screen.
        vkCmdDraw(cmd, 3, 1, 0, 0);
        drawUI(cmd);
    }
    vkutil::cmdEndRendering(cmd);

    vkutil::cmdTransitionImageLayout(
        cmd,
        swapChain.images[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
}

void VulkanExample::cmdDrawSSAO(VkCommandBuffer cmd) {
    (void)cmd;
    // TODO(Lab3): Generate ssao.raw from depth + normal + noise + kernel.
}

void VulkanExample::cmdDrawSSAOBlur(VkCommandBuffer cmd) {
    (void)cmd;
    // TODO(Lab3): Generate ssao.raw from depth + normal + noise + kernel.
}

void VulkanExample::cmdDrawDeferredLighting(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        hdr.sceneColor.image.imageView,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}}
    );

    VkRenderingAttachmentInfo depthAttachment = vkutil::renderingdepthAttachmentInfo(
        gBuffer.depth.image.imageView,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        1.0f,
        VK_ATTACHMENT_LOAD_OP_LOAD,
        VK_ATTACHMENT_STORE_OP_STORE
    );

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, VkExtent2D{width, height}};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    {
        vkutil::cmdSetViewportAndScissor(cmd, width, height);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.deferredLighting);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayouts.deferredLighting,
            0,
            1,
            &descriptorSets[currentBuffer].deferredLighting,
            0,
            nullptr
        );
        vkCmdDraw(cmd, 3, 1, 0, 0);

        // 光源球画进 HDR sceneColor，后续 Bloom 会自然把它们扩散成光晕。
        cmdDrawLightProxy(cmd);
    }
    vkutil::cmdEndRendering(cmd);
}

void VulkanExample::cmdDrawLightProxy(VkCommandBuffer cmd) {
    if (renderSettings.showLightProxy == 0 || pipelines.lightProxy == VK_NULL_HANDLE || renderSettings.lightCount <= 0) {
        return;
    }

    vkutil::cmdSetViewportAndScissor(cmd, width, height);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.lightProxy);
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayouts.lightProxy,
        0,
        1,
        &descriptorSets[currentBuffer].scene,
        0,
        nullptr
    );

    lightProxyModel.drawInstanced(
        cmd,
        static_cast<uint32_t>(renderSettings.lightCount)
    );
}

void VulkanExample::cmdDrawComposite(VkCommandBuffer cmd) {
    (void)cmd;
    // TODO(Lab3): 如果后续拆出独立 CompositePass，再把 Final debug 里的显示变换搬到这里。
}

void VulkanExample::cmdDrawClearOnly(VkCommandBuffer cmd) {
    vkutil::cmdTransitionImageLayout(
        cmd,
        swapChain.images[currentImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    VkClearValue clearColor{};
    clearColor.color = {{0.012f, 0.016f, 0.022f, 1.0f}};
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        swapChain.imageViews[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        clearColor
    );

    vkutil::cmdBeginColorOnlyRendering(cmd, VkExtent2D{width, height}, colorAttachment);
    {
        vkutil::cmdSetViewportAndScissor(cmd, width, height);
        drawUI(cmd);
    }
    vkutil::cmdEndRendering(cmd);

    vkutil::cmdTransitionImageLayout(
        cmd,
        swapChain.images[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
}

void VulkanExample::windowResized() {
    if (device == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE) {
        return;
    }

    destroyFrameResources();
    createFrameResources();
    resizeBloomPass();

    if (descriptorSetLayouts.scene != VK_NULL_HANDLE) {
        updateDescriptorSets();
    }
}

void VulkanExample::OnUpdateUIOverlay(vks::UIOverlay* overlay) {
    if (overlay->header("Lab3")) {
        overlay->comboBox("Debug View", &renderSettings.debugView, debugViewNames);
        overlay->checkBox("SSAO", &renderSettings.enableSSAO);
        overlay->checkBox("SSAO Blur", &renderSettings.enableSSAOBlur);
        //overlay->checkBox("Instancing", &renderSettings.enableInstancing);
        overlay->checkBox("Bloom", &renderSettings.enableBloom);
        overlay->sliderInt("Instances", &renderSettings.instanceCount, 1, static_cast<int32_t>(kMaxInstanceCount));
        overlay->sliderInt("Lights", &renderSettings.lightCount, 1, static_cast<int32_t>(kMaxLightCount));
        overlay->sliderFloat("Light Intensity", &renderSettings.lightIntensity, 1.0f, 80.0f);
        overlay->checkBox("Light Proxy", &renderSettings.showLightProxy);
        overlay->sliderFloat("Light Visual", &renderSettings.lightVisualIntensity, 0.0f, 80.0f);
        overlay->sliderFloat("Light Radius", &renderSettings.lightVisualRadius, 0.03f, 0.8f);
        overlay->sliderFloat("Exposure", &renderSettings.exposure, 0.1f, 5.0f);
        overlay->sliderFloat("Bloom Strength", &renderSettings.bloomStrength, 0.0f, 0.5f);
        overlay->sliderFloat("Bloom Radius", &renderSettings.bloomFilterRadius, 0.1f, 5.0f);
        overlay->sliderInt("Bloom Mips", &renderSettings.bloomMipCount, 1, static_cast<int32_t>(kMaxBloomMipCount));
        overlay->checkBox("Bloom Karis", &renderSettings.bloomUseKaris);
    }

    if (overlay->header("SSAO Settings")) {
        overlay->sliderFloat("Radius", &ssaoSettings.radius, 0.05f, 5.0f);
        overlay->sliderFloat("Bias", &ssaoSettings.bias, 0.001f, 0.2f);
        overlay->sliderFloat("Power", &ssaoSettings.power, 0.25f, 4.0f);
        overlay->sliderInt("Kernel Size", &ssaoSettings.kernelSize, 8, static_cast<int32_t>(kSSAOSize));
    }
}

VULKAN_EXAMPLE_MAIN();

