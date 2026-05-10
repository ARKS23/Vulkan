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

#include "vk_initializers.h"
#include "VulkanTexture.h"
#include "vk_images.h"

constexpr auto MAX_CONCURRENT_FRAMES = 2;

class VulkanExample : public VulkanExampleBase {
public:
    struct Vertex {
		float position[3];
		float color[3];
		float uv[2];
	};

    // struct VulkanBuffer {
    //     VkDeviceMemory memory{ VK_NULL_HANDLE };
	// 	VkBuffer handle{ VK_NULL_HANDLE };
    // };

	struct UniformBufferV2 {
		AllocatedBuffer buffer;
		VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
		uint8_t* mapped { nullptr };
	};

    struct ShaderData {
        glm::mat4 projectionMatrix;
        glm::mat4 modelMatrix;
        glm::mat4 viewMatrix;
    };

	struct PushConstantData {
		glm::mat4 modelMatrix;
		glm::vec4 colorMultiplier {1.0f, 1.0f, 1.0f, 1.0f};
	};

    struct MeshData {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

public:
	GPUMeshBuffers circleMeshBuffers;

	std::array<UniformBufferV2, MAX_CONCURRENT_FRAMES> uniformBuffersV2; // 每个 in-flight frame 对应一份 UBO

    VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };

    VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
    VkPipeline pipeline{ VK_NULL_HANDLE };

    std::vector<VkSemaphore> presentCompleteSemaphores{};
	std::vector<VkSemaphore> renderCompleteSemaphores{};
    std::array<VkFence, MAX_CONCURRENT_FRAMES> waitFences{};

	// 场景物体使用的基础纹理，以及 scene pass 中配套使用的深度附件。
	AllocatedTexture baseColorTexture;
	AllocatedImage depthImage;

	// 离屏渲染资源
	// 离屏颜色目标：第一遍作为 color attachment 写入，第二遍作为 sampled image 读取。
	AllocatedTexture offscreenColor;
	// blit pass 专用描述符和管线，用来把 offscreenColor 画回 swapchain。
	VkDescriptorSetLayout blitDescriptorSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet blitDescriptorSet{ VK_NULL_HANDLE };
	VkPipelineLayout blitPipelineLayout{ VK_NULL_HANDLE };
	VkPipeline blitPipeline{ VK_NULL_HANDLE };

    VkCommandPool commandPool{ VK_NULL_HANDLE };
	std::array<VkCommandBuffer, MAX_CONCURRENT_FRAMES> commandBuffers{};

    uint32_t currentFrame {0};

    VkPhysicalDeviceVulkan13Features enabledFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };

	VmaAllocator allocator{ VK_NULL_HANDLE }; // 等稳定后加入基类

public:
    VulkanExample();
    virtual ~VulkanExample() override;

    virtual void getEnabledFeatures() override;

	void createVmaAllocator();	// 等稳定后加入基类
    void createSynchronizationPrimitives();
    void createCommandBuffers();
    void createVertexBuffer();
    void createUniformBuffers();
    void createDescriptors();
    void createPipeline();
    virtual void prepare() override;

    virtual void render() override;

    virtual void setupDepthStencil() override;
    VkShaderModule loadSPIRVShader(const std::string& filename);
	void loadTexture();

    // dynamic render不需要这两个了，这里把它们重写成空实现，否则基类会按传统路径去创建 framebuffer 和 render pass
	void setupFrameBuffer() override {}
	void setupRenderPass() override {}

	void destroyVmaAllocator(); // 等稳定后加入基类

	// 离屏渲染和全屏 blit。后续做后处理、GBuffer 或阴影图时，也会沿用这条资源组织思路。
	void createOffscreenResources();
	void destroyOffscreenResources();
	void createBlitDescriptors();
	void updateBlitDescriptor();
	void createBlitPipeline();
	void drawScene(VkCommandBuffer commandBuffer, const glm::mat4& baseModelMatrix);
	void windowResized() override; // resize 时重建 offscreen image，并更新 blit descriptor。

private:
	MeshData createCircleMesh(float radius, uint32_t segmentCount);

	const std::string vertShaderPath = "lab0/lab0.vert.spv";
	const std::string fragShaderPath = "lab0/lab0.frag.spv";

	const std::string blitVertShaderPath = "lab0/fullscreen.vert.spv";
	const std::string blitFragShaderPath = "lab0/fullscreen.frag.spv";
};


VULKAN_EXAMPLE_MAIN()
