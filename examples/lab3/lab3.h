#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#include "vk_images.h"
#include "vk_initializers.h"
#include "vk_bloom.h"
#include "VulkanTexture.h"

/*
    Lab3: Deferred Rendering + SSAO + Instancing

    当前文件先搭建 C++ 侧基础框架：
    - per-frame UBO / descriptor set
    - G-Buffer / SSAO / HDR offscreen 资源
    - instance buffer 数据结构
    - 后续 pass 的函数入口

    真正的 shader 和 pipeline 会在下一步逐个填充。
*/

class VulkanExample : public VulkanExampleBase {
public:
    static constexpr uint32_t kSSAOSize = 64;
    static constexpr uint32_t kSSAONoiseDim = 4;
    static constexpr uint32_t kMaxInstanceCount = 1024;
    static constexpr uint32_t kMaxLightCount = 150;
    static constexpr uint32_t kMaxBloomMipCount = 8;

    struct CameraUBO {
        glm::mat4 projection{1.0f};
        glm::mat4 view{1.0f};
        glm::mat4 inverseProjection{1.0f};
        glm::mat4 inverseView{1.0f};
        glm::vec4 cameraPos{0.0f};
        glm::vec4 screenSize{0.0f}; // x:width, y:height, z:1/width, w:1/height
    };

    struct Light {
        glm::vec4 position{0.0f};
        glm::vec4 color{1.0f};
        // x: PBR 照明强度，y: 光源球显示亮度，z: 光源球显示半径，w: 实际照明半径。
        glm::vec4 intensity{1.0f};
    };

    struct LightsUBO {
        Light lights[kMaxLightCount];
        glm::ivec4 lightCount{static_cast<int32_t>(kMaxLightCount), 0, 0, 0};
    };

    struct SSAOParamsUBO {
        glm::mat4 projection{1.0f};
        glm::mat4 inverseProjection{1.0f};
        glm::vec4 params{0.5f, 0.025f, 1.5f, static_cast<float>(kSSAOSize)}; // radius, bias, power, kernelSize
        glm::vec4 noiseScale{1.0f}; // xy: 屏幕 / noise 贴图尺寸
    };

    struct InstanceData {
        glm::mat4 model{1.0f};
        glm::mat4 normalMatrix{1.0f};
        glm::vec4 color{1.0f};
        // 正式 PBR 材质来自 glTF 贴图；这里保留为每实例调制参数/扩展位。
        glm::vec4 materialParams{1.0f, 1.0f, 1.0f, 0.0f}; // metallicMul, roughnessMul, emissiveMul, materialIndex/unused
    };

    struct FrameUniformBuffers {
        AllocatedBuffer camera;
        AllocatedBuffer lights;
        AllocatedBuffer ssaoParams;
    };

    struct RenderAttachment {
        AllocatedImage image;
        VkDescriptorImageInfo descriptor{};
    };

    struct GBufferResources {
        RenderAttachment albedoMetallic;
        RenderAttachment normalRoughness;
        RenderAttachment emissiveAO;
        RenderAttachment depth;

        VkFormat albedoMetallicFormat{VK_FORMAT_R16G16B16A16_SFLOAT};
        VkFormat normalRoughnessFormat{VK_FORMAT_R16G16B16A16_SFLOAT};
        VkFormat emissiveAOFormat{VK_FORMAT_R16G16B16A16_SFLOAT};
        VkFormat depthAttachmentFormat{VK_FORMAT_D32_SFLOAT};
        VkExtent2D extent{};
    };

    struct GBufferDebugPushConstants {
        int32_t debugView{0};
        int32_t enableBloom{1};
        float nearPlane{0.1f};
        float farPlane{256.0f};
        float exposure{1.0f};
        float bloomStrength{0.08f};
        glm::vec2 padding{0.0f}; // 对齐到 16 字节，方便 shader 端稳定读取。
    };

    struct SSAOResources {
        RenderAttachment raw;
        RenderAttachment blurred;
        AllocatedTexture noise;
        AllocatedBuffer kernel;

        VkFormat format{VK_FORMAT_R8_UNORM};
        VkExtent2D extent{};
    };

    struct HDRResources {
        RenderAttachment sceneColor;
        VkFormat format{VK_FORMAT_R16G16B16A16_SFLOAT};
        VkExtent2D extent{};
    };

    struct DescriptorSetLayouts {
        VkDescriptorSetLayout scene{VK_NULL_HANDLE};
        VkDescriptorSetLayout gBufferDebug{VK_NULL_HANDLE};
        VkDescriptorSetLayout ssao{VK_NULL_HANDLE};
        VkDescriptorSetLayout ssaoBlur{VK_NULL_HANDLE};
        VkDescriptorSetLayout deferredLighting{VK_NULL_HANDLE};
        VkDescriptorSetLayout composite{VK_NULL_HANDLE};
    };

    struct DescriptorSets {
        VkDescriptorSet scene{VK_NULL_HANDLE};
        VkDescriptorSet gBufferDebug{VK_NULL_HANDLE};
        VkDescriptorSet ssao{VK_NULL_HANDLE};
        VkDescriptorSet ssaoBlur{VK_NULL_HANDLE};
        VkDescriptorSet deferredLighting{VK_NULL_HANDLE};
        VkDescriptorSet composite{VK_NULL_HANDLE};
    };

    struct PipelineLayouts {
        VkPipelineLayout gBuffer{VK_NULL_HANDLE};
        VkPipelineLayout gBufferDebug{VK_NULL_HANDLE};
        VkPipelineLayout ssao{VK_NULL_HANDLE};
        VkPipelineLayout ssaoBlur{VK_NULL_HANDLE};
        VkPipelineLayout deferredLighting{VK_NULL_HANDLE};
        VkPipelineLayout lightProxy{VK_NULL_HANDLE};
        VkPipelineLayout composite{VK_NULL_HANDLE};
    };

    struct Pipelines {
        VkPipeline gBuffer{VK_NULL_HANDLE};
        VkPipeline gBufferInstanced{VK_NULL_HANDLE};
        VkPipeline gBufferDebug{VK_NULL_HANDLE};
        VkPipeline ssao{VK_NULL_HANDLE};
        VkPipeline ssaoBlur{VK_NULL_HANDLE};
        VkPipeline deferredLighting{VK_NULL_HANDLE};
        VkPipeline lightProxy{VK_NULL_HANDLE};
        VkPipeline composite{VK_NULL_HANDLE};
    };

    struct RenderSettings {
        int32_t debugView{0};
        int32_t enableSSAO{1};
        int32_t enableSSAOBlur{1};
        int32_t enableInstancing{1};
        int32_t enableBloom{1};
        int32_t showLightProxy{1};
        int32_t instanceCount{128};
        int32_t lightCount{static_cast<int32_t>(kMaxLightCount)};
        float lightIntensity{25.0f};
        float lightVisualIntensity{18.0f};
        float lightVisualRadius{0.18f};
        float exposure{1.0f};
        float bloomStrength{0.08f};
        float bloomFilterRadius{0.5f};
        int32_t bloomMipCount{5};
        int32_t bloomUseKaris{1};
    };

    struct SSAOSettings {
        float radius{0.5f};
        float bias{0.025f};
        float power{1.5f};
        int32_t kernelSize{static_cast<int32_t>(kSSAOSize)};
    };

public:
    VmaAllocator allocator{VK_NULL_HANDLE};
    VkPhysicalDeviceVulkan13Features vulkan13Features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};

    GBufferResources gBuffer;
    SSAOResources ssao;
    HDRResources hdr;

    std::array<FrameUniformBuffers, maxConcurrentFrames> uniformBuffers;
    std::array<DescriptorSets, maxConcurrentFrames> descriptorSets;
    DescriptorSetLayouts descriptorSetLayouts;
    PipelineLayouts pipelineLayouts;
    Pipelines pipelines;
    vkutil::BloomPass bloomPass;
    vkutil::BloomPass::Settings bloomSettings;

    AllocatedBuffer instanceBuffer;
    std::vector<InstanceData> instanceCpuData;
    vkglTF::Model sceneModel;
    vkglTF::Model lightProxyModel;

    CameraUBO cameraUBO;
    LightsUBO lightsUBO;
    SSAOParamsUBO ssaoParamsUBO;

    VkSampler gBufferSampler{VK_NULL_HANDLE};
    VkSampler screenSampler{VK_NULL_HANDLE};

    RenderSettings renderSettings;
    SSAOSettings ssaoSettings;
    std::vector<std::string> debugViewNames;

public:
    VulkanExample();
    ~VulkanExample() override;

    void prepare() override;
    void render() override;
    void windowResized() override;
    void OnUpdateUIOverlay(vks::UIOverlay* overlay) override;

private:
    void createVmaAllocator();
    void destroyVmaAllocator();

    void createSamplers();
    void destroySamplers();

    void loadAssets();
    void destroyAssets();

    void createFrameResources();
    void destroyFrameResources();

    void createStaticResources();
    void destroyStaticResources();
    void createSSAONoiseTexture();
    void createSSAOKernelBuffer();
    void createInstanceBuffer();
    void updateInstanceBuffer();

    void createUniformBuffers();
    void destroyUniformBuffers();

    void createDescriptorPool();
    void createBloomPass();
    void resizeBloomPass();
    void destroyBloomPass();
    void setupDescriptors();
    void createDescriptorSetLayouts();
    void allocateDescriptorSets();
    void updateDescriptorSets();
    void destroyDescriptors();

    void createPipelines();
    void destroyPipelines();

    void updateUniformBuffers();
    void updateLights();
    void updateBloomSettings();
    void buildCommandBuffer();

    // 后续逐步把这些空 pass 填成真正的 Lab3 渲染链路。
    void cmdDrawGBuffer(VkCommandBuffer cmd);
    void cmdDrawGBufferDebug(VkCommandBuffer cmd);
    void cmdDrawSSAO(VkCommandBuffer cmd);
    void cmdDrawSSAOBlur(VkCommandBuffer cmd);
    void cmdDrawDeferredLighting(VkCommandBuffer cmd);
    void cmdDrawLightProxy(VkCommandBuffer cmd);
    void cmdDrawComposite(VkCommandBuffer cmd);
    void cmdDrawClearOnly(VkCommandBuffer cmd);

    void transitionGBufferForWriting(VkCommandBuffer cmd);
    void transitionGBufferForSampling(VkCommandBuffer cmd);

    RenderAttachment createColorAttachment(VkExtent2D extent, VkFormat format);
    RenderAttachment createDepthAttachment(VkExtent2D extent, VkFormat format);
    void destroyAttachment(RenderAttachment& attachment);

private:
    const std::string gBufferVertexShader = "lab3/GBuffer.vert.spv";
    const std::string gBufferFragmentShader = "lab3/GBuffer.frag.spv";
    const std::string gBufferInstancedVertexShader = "lab3/gbuffer_instanced.vert.spv";

    const std::string deferredLightingVertexShader = "lab3/fullscreen.vert.spv";
    const std::string deferredLightingFragmentShader = "lab3/deferredLighting.frag.spv";

    const std::string gBufferDebugVertexShader = "lab3/fullscreen.vert.spv";
    const std::string gBufferDebugFragmentShader = "lab3/GBufferDebug.frag.spv";

    const std::string pbrModelPath = "models/DamagedHelmet/DamagedHelmet.gltf";
    const std::string lightProxyModelPath = "models/sphere.gltf";
    const std::string lightProxyVertexShader = "lab3/lightProxy.vert.spv";
    const std::string lightProxyFragmentShader = "lab3/lightProxy.frag.spv";
    
    const std::string ssaoVertexShader = "lab3/fullscreen.vert.spv";
    const std::string ssaoFragmentShader = "lab3/ssao.frag.spv";
    const std::string ssaoBlurFragmentShader = "lab3/ssaoBlur.frag.spv";

    const std::string compositeFragmentShader = "lab3/composite.frag.spv";
};
