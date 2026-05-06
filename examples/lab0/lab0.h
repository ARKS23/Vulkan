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

constexpr auto MAX_CONCURRENT_FRAMES = 2;

class VulkanExample : public VulkanExampleBase {
public:
    struct Vertex {
		float position[3];
		float color[3];
	};

    struct VulkanBuffer {
        VkDeviceMemory memory{ VK_NULL_HANDLE };
		VkBuffer handle{ VK_NULL_HANDLE };
    };

    struct UniformBuffer : VulkanBuffer {
        VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
        uint8_t* mapped{ nullptr }; // 映射后的指针，方便后面直接通过 memcpy 更新内容
    };

    struct ShaderData {
        glm::mat4 projectionMatrix;
        glm::mat4 modelMatrix;
        glm::mat4 viewMatrix;
    };

    struct MeshData {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

public:
	GPUMeshBuffers circleMeshBuffers;

    std::array<UniformBuffer, MAX_CONCURRENT_FRAMES> uniformBuffers; // 每个 in-flight frame 对应一份 UBO

    VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };

    VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
    VkPipeline pipeline{ VK_NULL_HANDLE };

    std::vector<VkSemaphore> presentCompleteSemaphores{};
	std::vector<VkSemaphore> renderCompleteSemaphores{};
    std::array<VkFence, MAX_CONCURRENT_FRAMES> waitFences{};

    VkCommandPool commandPool{ VK_NULL_HANDLE };
	std::array<VkCommandBuffer, MAX_CONCURRENT_FRAMES> commandBuffers{};

    uint32_t currentFrame {0};

    VkPhysicalDeviceVulkan13Features enabledFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };

	VmaAllocator allocator{ VK_NULL_HANDLE }; // 等稳定后加入基类

public:
    VulkanExample();
    virtual ~VulkanExample() override;

    virtual void getEnabledFeatures() override;
    uint32_t getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties);

	void createVmaAllocator();
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

    // dynamic render不需要这两个了，这里把它们重写成空实现，否则基类会按传统路径去创建 framebuffer 和 render pass
	void setupFrameBuffer() override {}
	void setupRenderPass() override {}

	void destroyVmaAllocator();

	//helper function 稳定后加入工具函数
	AllocatedBuffer createAllocatedBuffer(size_t allocSize, VkBufferUsageFlags usage,  VmaAllocationCreateFlags allocationFlags, VmaMemoryUsage memoryUsage);
	AllocatedBuffer createDeviceLocalBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage); // 封装CPU上传数据到GPU
	void destroyAllocatedBuffer(AllocatedBuffer& buffer);
	void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

private:
	MeshData createCircleMesh(float radius, uint32_t segmentCount);
};


VULKAN_EXAMPLE_MAIN()
