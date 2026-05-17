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
    // struct PushConstantParamsData {
    //     float roughness;
    //     float metallic;
    //     glm::vec3 baseColor;
    // };

    struct UniformDataMatrices {
        glm::mat4 projection;
        glm::mat4 model;
        glm::mat4 view;
        glm::vec3 camPos;
    };

    struct UniformDataLights {
        glm::vec4 lightsPos[4];
        glm::vec4 lightsColor[4];
        glm::vec4 lightIntensity[4];
    };

    struct UniformBuffers {
        AllocatedBuffer matricesBuffer;
        AllocatedBuffer lightBuffer;
        AllocatedBuffer lightSourceMatricesBuffer;
    };

    struct PushconstantsLight {
        glm::vec4 Pos;
        glm::vec4 Color;
        glm::vec4 Intensity;
    };

    struct Piplelines {
        VkPipeline scenePipeline = {VK_NULL_HANDLE};
        VkPipeline lightPipeline = {VK_NULL_HANDLE};
    };

    struct PipelinesLayout {
        VkPipelineLayout scenePipelineLayout = {VK_NULL_HANDLE};
        VkPipelineLayout lightPipelineLayout = {VK_NULL_HANDLE};
    };

    struct DescriptorSets {
        VkDescriptorSet sceneDescriptor{ VK_NULL_HANDLE };
        VkDescriptorSet lightDescriptor{ VK_NULL_HANDLE };
    };

    struct DescriptorSetLayouts {
        VkDescriptorSetLayout sceneDescriptorSetLayout{ VK_NULL_HANDLE };
        VkDescriptorSetLayout lightDescriptorSetLayout{ VK_NULL_HANDLE };
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
    UniformDataLights UBOLights;
    UniformDataMatrices UBOLightSourceMatrix;
    std::array<UniformBuffers, maxConcurrentFrames> uniformBuffersScene;

    // push constant
    glm::vec3 objectPos = {0.0f, 0.0f, 0.0f};
    PushconstantsLight pushconstantsLight;

    // 场景
    std::vector<vkglTF::Model> objects;
    int32_t objectIndex = 0;
    std::vector<std::string> objectNames;
    std::vector<Material> materials;
    std::vector<std::string> materialNames;
    int32_t materialIndex = 0;


    // 光源
    vkglTF::Model lightObject;
    
public:
    VulkanExample();
    virtual ~VulkanExample() override;

    void createVmaAllocator();
    void loadAssets();
    void createUniformBuffers();
    void setupDescriptors();
    void createPipelines();
    
    void destroyPipelines();
    void destroyDescriptors();
    void destroyUniformBuffers();
    void destroyVmaAllocator();

    void createScenePipelineLayout();
    void createScenePipeline();
    void createLightPipelineLayout();
    void createLightPipeline();

    virtual void render() override;
    virtual void prepare() override;
    virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay) override;

    void updateUniformBuffers();
    void buildCommandBuffer();

    void cmdDrawSecne(VkCommandBuffer cmd);
    void cmdDrawLight(VkCommandBuffer cmd);

private:
    const std::string pbrSceneVertexShader = "lab2/pbrScene.vert.spv";
    const std::string pbrSceneFragmentShader = "lab2/pbrScene.frag.spv";

    const std::string lightVertexShader = "lab2/light.vert.spv";
    const std::string lightFragmentShader = "lab2/light.frag.spv";
};

VULKAN_EXAMPLE_MAIN();
