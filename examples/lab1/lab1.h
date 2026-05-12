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
 Lab1: Shadow Mapping   
*/

class VulkanExample : public VulkanExampleBase {
public:
    struct UniformDataScenePass {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 model;
        glm::mat4 depthBiasMVP;
        glm::vec4 lightPos;
        glm::vec4 cameraPos;

        float zNear;
        float zFar;
    };

    struct UniformDataShadowPass {
        glm::mat4 depthMVP;
    };

    struct PushconstantData {
        glm::vec4 lightColor = glm::vec4(0.95f, 0.98f, 0.98f, 1.0f);
        float minShadowBias = 0.001f;
        float slopeShadowBias = 0.001f;
        int enablePCF = 1;
        int PCFRadius = 3;
    };

    struct PushConstantDataLight {
        glm::vec4 lightColor = glm::vec4(0.95f, 0.98f, 0.98f, 1.0f);
        glm::mat4 mvp;
    };

    struct UniformBuffers {
        AllocatedBuffer sceneBuffer;
        AllocatedBuffer shadowOffscreenBuffer;
    };

    struct Pipelines {
        VkPipeline shadowOffscreen{ VK_NULL_HANDLE };
		VkPipeline sceneShadow{ VK_NULL_HANDLE };
        VkPipeline lightSphere{ VK_NULL_HANDLE };
		// Pipeline with percentage close filtering (PCF) of the shadow map 
		// VkPipeline sceneShadowPCF{ VK_NULL_HANDLE };
		VkPipeline debug{ VK_NULL_HANDLE };
    };

    struct DescriptorSets {
        VkDescriptorSet offscreen{ VK_NULL_HANDLE };
        VkDescriptorSet scene{ VK_NULL_HANDLE };
        VkDescriptorSet debug{ VK_NULL_HANDLE };
    };
    
    struct ShadowMap {
        AllocatedTexture shadowTexture;
        VkExtent2D extent {2048, 2048};
        VkFormat format { VK_FORMAT_D16_UNORM };
    };

public:
    VmaAllocator allocator {VK_NULL_HANDLE};
    VkPhysicalDeviceVulkan13Features vulkan13Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };

    UniformDataScenePass uniformDataScene;
    UniformDataShadowPass uniformDataShadow;

    // 管线配置
    Pipelines pipelines;
    VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };

    // UBO
    std::array<UniformBuffers, maxConcurrentFrames> uniformBuffers;

    // pushConstant
    PushconstantData pushConstan;
    PushConstantDataLight pushConstantLight;

    // 描述符
    VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
    std::array<DescriptorSets, maxConcurrentFrames> descriptorSets;

    // 场景选项表
    std::vector<vkglTF::Model> scenes;
    std::vector<std::string> sceneNames;
    int32_t sceneIndex = 0;
    vkglTF::Model lightSphere;

    // 阴影相关
    ShadowMap shadowMap;
    float shadowNearPlane = 0.1f, shadowFarPlane = 50.f;
    float depthBiasConstant = 1.25f;
    float depthBiasSlope = 1.75f;

    // 光源
    float rotationAngle = 0.0f;
    float rotationSpeed = 0.01f;
    float lightRadius = 5.f;
    glm::vec3 lightPos = glm::vec3(4.0f, -7.f, 3.f);

public:
    VulkanExample();
    virtual ~VulkanExample() override;

    virtual void getEnabledFeatures() override;

    void createVmaAllocator();
    void loadAssets();
    void createShadowResources();
    void createUniformBuffers();
    void setupDescriptors();
    void createPipelines();

    void destroyPipelines();
    void destroyDescriptors();
    void destroyUniformBuffers();
    void destroyShadowResources();
    void destroyVmaAllocator();

    void buildCommandBuffer();
    virtual void render() override;
    virtual void prepare() override;
    virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay) override;
    //virtual void windowResized() override;
    void updateLight();
    void updateUniformBuffers();

    void createLightPipeline();

    void drawShadowMap(VkCommandBuffer commandBuffer);
    void drawScene(VkCommandBuffer commandBuffer);
    void drawQuad(VkCommandBuffer commandBuffer);

private:
    // shader
    const std::string shadowVertexShaderPath = "lab1/shadowVertex.vert.spv";
    const std::string sceneVertexShaderPath = "lab1/scene.vert.spv";
    const std::string sceneFragmentShaderPath = "lab1/scene.frag.spv";
    const std::string quadVertexShaderPath = "lab1/quad.vert.spv";
    const std::string quadFragmentShaderPath = "lab1/quad.frag.spv";
    const std::string lightVertexShaderPath = "lab1/light.vert.spv";
    const std::string lightFragmentShaderPath = "lab1/light.frag.spv";

    // model
    const std::string shadowScenePath = "models/vulkanscene_shadow.gltf";
    const std::string sampleScenePath = "models/samplescene.gltf";
};

VULKAN_EXAMPLE_MAIN()
