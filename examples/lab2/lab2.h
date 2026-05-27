#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fstream>
#include <vector>
#include <functional>
#include <exception>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#include "vk_initializers.h"
#include "VulkanTexture.h"
#include "vk_images.h"

/*
 Lab2: PBR basic
*/

struct Material {
public:
    struct PushBlock {
        float roughness;
        float metallic;
        float r, g, b;
    };

public:
    PushBlock params;
    std::string name;

public:
    Material() {};
    Material(std::string n, glm::vec3 color, float r, float m) {
        name = n;
        params.r = color.r;
        params.g = color.g;
        params.b = color.b;
        params.roughness = r;
        params.metallic = m;
    }
};

class VulkanExample : public VulkanExampleBase {
public:
    struct UniformDataMatrices {
        glm::mat4 projection;
        glm::mat4 model;
        glm::mat4 view;
        glm::vec3 camPos;

        uint32_t mipNums = 5;
    };

    struct UniformDataLights {
        glm::vec4 lightsPos[4];
        glm::vec4 lightsColor[4];
        glm::vec4 lightIntensity[4];
    };

    struct UniformDataSkyBox {
        glm::mat4 projection;
        glm::mat4 model;
        glm::mat4 view;
    };

    struct UniformBuffers {
        AllocatedBuffer matricesBuffer;
        AllocatedBuffer pbrTextureMatricesBuffer;
        AllocatedBuffer lightBuffer;
        AllocatedBuffer lightSourceMatricesBuffer;
        AllocatedBuffer skyBoxMatricesBuffer;
    };

    struct Textures {
        vks::TextureCubeMap environmentCubeMap;
        AllocatedCubeTexture irradianceCubeMap;
        AllocatedCubeTexture prefilteredCubeMap;
        AllocatedTexture brdfLUT;
    };

    struct PushconstantsLight {
        glm::vec4 Pos;
        glm::vec4 Color;
        glm::vec4 Intensity;
        glm::vec4 VisualIntensity;
    };

    struct Piplelines {
        VkPipeline scenePipeline = {VK_NULL_HANDLE};
        VkPipeline fullScreenPipeline = {VK_NULL_HANDLE};
        VkPipeline pbrTexturePipeline = {VK_NULL_HANDLE};
        VkPipeline skyboxPipeline = {VK_NULL_HANDLE};
        VkPipeline lightPipeline = {VK_NULL_HANDLE};
    };

    struct PipelinesLayout {
        VkPipelineLayout scenePipelineLayout = {VK_NULL_HANDLE};
        VkPipelineLayout fullScreenPipelineLayout = {VK_NULL_HANDLE};
        VkPipelineLayout pbrTexturePipelineLayout = {VK_NULL_HANDLE};
        VkPipelineLayout skyboxPipelineLayout = {VK_NULL_HANDLE};
        VkPipelineLayout lightPipelineLayout = {VK_NULL_HANDLE};
    };

    struct DescriptorSets {
        VkDescriptorSet sceneDescriptor{ VK_NULL_HANDLE };
        VkDescriptorSet fullScreenDescriptor{ VK_NULL_HANDLE };
        VkDescriptorSet pbrTextureDescriptor{ VK_NULL_HANDLE };
        VkDescriptorSet skyboxDescriptor{ VK_NULL_HANDLE };
        VkDescriptorSet lightDescriptor{ VK_NULL_HANDLE };
    };

    struct DescriptorSetLayouts {
        VkDescriptorSetLayout sceneDescriptorSetLayout{ VK_NULL_HANDLE };
        VkDescriptorSetLayout fullScreenDescriptorSetLayout{ VK_NULL_HANDLE };
        VkDescriptorSetLayout skyboxDescriptorSetLayout{ VK_NULL_HANDLE };
        VkDescriptorSetLayout lightDescriptorSetLayout{ VK_NULL_HANDLE };
    };

    // Physical Based Bloom
    struct BloomMip {
        AllocatedImage image;
        VkDescriptorImageInfo descriptor{};
        VkExtent2D extent{};
    };

    struct BloomResrouces {
        AllocatedImage hdrSceneColor;
        VkDescriptorImageInfo hdrSceneDescriptor{};

        std::vector<BloomMip> mips; // 多级mip，用于上下采样
        VkSampler sampler = VK_NULL_HANDLE;

        VkFormat hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkExtent2D sceneExtent{};
        uint32_t mipCount = 5;
    };

    struct BloomDescriptorSetLayouts {
        VkDescriptorSetLayout sampleDescriptorSetLayout{ VK_NULL_HANDLE };
        VkDescriptorSetLayout compositeDescriptorSetLayout{ VK_NULL_HANDLE };
    };

    struct BloomDescriptorSets {
        // 第 0 次下采样从 HDR scene 采样，单独保留一个 descriptor set 避免和 mip 索引混在一起。
        VkDescriptorSet hdrSceneSet = VK_NULL_HANDLE;
        // mipSets[i] 始终对应 bloom.mips[i]，上采样和最终合成时不会出现 off-by-one。
        std::vector<VkDescriptorSet> mipSets;
        // 最终合成同时读取原 HDR scene 和第一层 bloom mip。
        VkDescriptorSet compositeSet = VK_NULL_HANDLE;
    };

    struct BloomPipelinesLayout {
        VkPipelineLayout bloomDownsamplePipelineLayout{ VK_NULL_HANDLE };
        VkPipelineLayout bloomUpsamplePipelineLayout{ VK_NULL_HANDLE };
        VkPipelineLayout bloomCompositePipelineLayout{ VK_NULL_HANDLE };
    };

    struct BloomPipelines {
        VkPipeline bloomDownsamplePipeline{ VK_NULL_HANDLE };
        VkPipeline bloomUpsamplePipeline{ VK_NULL_HANDLE };
        VkPipeline bloomCompositePipeline{ VK_NULL_HANDLE };
    };

public:
    VmaAllocator allocator {VK_NULL_HANDLE};
    VkPhysicalDeviceVulkan13Features vulkan13Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };

    // 管线
    Piplelines pipelines;
    PipelinesLayout pipelinesLayout;

    // 描述符
    std::array<DescriptorSets, maxConcurrentFrames> descriptorSets;
    DescriptorSetLayouts descriptorSetLayouts;

    // UBO
    UniformDataMatrices UBOMatrix;
    UniformDataMatrices UBOPBRTextureMatrix;
    UniformDataLights UBOLights;
    UniformDataMatrices UBOLightSourceMatrix;
    UniformDataSkyBox UBOSkyBox;
    std::array<UniformBuffers, maxConcurrentFrames> uniformBuffersScene;

    // push constant
    glm::vec3 objectPos = {0.0f, 0.0f, 0.0f};
    PushconstantsLight pushconstantsLight;
    glm::vec4 lightVisualIntensity[4] = {
        glm::vec4(14.0f, 14.0f, 14.0f, 1.0f),
        glm::vec4(12.0f, 12.0f, 12.0f, 1.0f),
        glm::vec4(16.0f, 16.0f, 16.0f, 1.0f),
        glm::vec4(10.0f, 10.0f, 10.0f, 1.0f)
    };

    // 场景
    std::vector<vkglTF::Model> objects;
    int32_t objectIndex = 0;
    std::vector<std::string> objectNames;
    std::vector<Material> materials;
    std::vector<std::string> materialNames;
    int32_t materialIndex = 0;

    vkglTF::Model pbrObject;
    glm::vec3 pbrObjectPos = glm::vec3(-10.0f, -5.5f, -7.0f);
    float rotationAngle = 0.0f;
    float scaleRatio = 1.0f;

    // 天空盒顶点
    vkglTF::Model skyboxCube;

    // 纹理
    Textures textures;

    // 光源
    vkglTF::Model lightObject;

    // Physical Based Bloom
    BloomResrouces bloom;
    BloomDescriptorSetLayouts bloomDescriptorSetLayouts;
    BloomDescriptorSets bloomDescriptorSets;
    BloomPipelinesLayout bloomPipelinesLayout;
    BloomPipelines bloomPipelines;
    int enableBloom = 1;
    float exposure = 1.0f;
    float bloomStrength = 0.08f;
    float bloomFilterRadius = 0.5f;
    int bloomMipCount = 5;
    
public:
    VulkanExample();
    virtual ~VulkanExample() override;

    void createVmaAllocator();
    void loadAssets();
    void createDescriptorsPool();
    void createBloomResources();
    void createBloomDescriptorSets();
    void createUniformBuffers();
    void setupDescriptors();
    void createPipelines();
    
    void destroyPipelines();
    void destroyDescriptors();
    void destroyUniformBuffers();
    void destroyBloomDescriptorSets();
    void destroyBloomResources();
    void destroyAssets();
    void destroyVmaAllocator();

    void createScenePipelineLayout();
    void createScenePipeline();
    void createFullScreenPipelineLayout();
    void createFullScreenPipeline();
    void createPBRTexturePipelineLayout();
    void createPBRTexturePipeline();
    void createSkyboxPipelineLayout();
    void createSkyboxPipeline();
    void createLightPipelineLayout();
    void createLightPipeline();
    void createBloomPipelinesLayout();
    void createBloomPipelines();

    void generateIrradianceCubeMap();
    void generatePrefilteredCubeMap();
    void generateBRDFLUT();

    virtual void render() override;
    virtual void prepare() override;
    virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay) override;

    void updateUniformBuffers();
    void buildCommandBuffer();

    void cmdDrawSecne(VkCommandBuffer cmd);
    void cmdDrawPBRTexture(VkCommandBuffer cmd);
    void cmdDrawLight(VkCommandBuffer cmd);
    void cmdDrawSkybox(VkCommandBuffer cmd);
    void cmdDrawBloomDownsample(VkCommandBuffer cmd);
    void cmdDrawBloomUpsample(VkCommandBuffer cmd);
    void cmdDrawBloomComposite(VkCommandBuffer cmd);
    void cmdDrawFullScreen(VkCommandBuffer cmd);

private:
    const std::string filterCubeVertexShader = "lab2/fliterCube.vert.spv";
    const std::string irradianceFragmentShader = "lab2/irradianceMap.frag.spv";
    const std::string prefilterFragmentShader = "lab2/prefilterMap.frag.spv";

    const std::string brdfLUTVertexShader = "lab2/BRDFLUT.vert.spv";
    const std::string brdfLUTFragmentShader = "lab2/BRDFLUT.frag.spv";

    const std::string pbrSceneVertexShader = "lab2/pbrScene.vert.spv";
    const std::string pbrSceneFragmentShader = "lab2/pbrScene.frag.spv";

    const std::string fullScreenVertexShader = "lab2/fullscreen.vert.spv";
    const std::string fullScreenFragmentShader = "lab2/fullscreen.frag.spv";

    const std::string pbrModelPath = "models/DamagedHelmet/DamagedHelmet.gltf";
    const std::string pbrTextureVertexShader = "lab2/pbrTexture.vert.spv";
    const std::string pbrTextureFragmentShader = "lab2/pbrTexture.frag.spv";

    const std::string lightVertexShader = "lab2/light.vert.spv";
    const std::string lightFragmentShader = "lab2/light.frag.spv";

    const std::string skyboxVertexShader = "lab2/skybox.vert.spv";
    const std::string skyboxFragmentShader = "lab2/skybox.frag.spv";

    const std::string bloomDownsampleVertexShader = fullScreenVertexShader;
    const std::string bloomDownsampleFragmentShader = "lab2/downSample.frag.spv";

    const std::string bloomUpsampleVertexShader = fullScreenVertexShader;
    const std::string bloomUpsampleFragmentShader = "lab2/upSample.frag.spv";

    const std::string bloomCompositeVertexShader = fullScreenVertexShader;
    const std::string bloomCompositeFragmentShader = "lab2/composite.frag.spv";

    const std::string hdrFilePath = "textures/hdr/pisa_cube.ktx";
};

VULKAN_EXAMPLE_MAIN();
