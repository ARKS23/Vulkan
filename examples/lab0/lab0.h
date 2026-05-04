#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fstream>
#include <vector>
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

public:
    VulkanBuffer vertexBuffer;
	VulkanBuffer indexBuffer;
	uint32_t indexCount{ 0 };

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

public:
    VulkanExample();
    virtual ~VulkanExample() override;

    virtual void getEnabledFeatures() override;
    uint32_t getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties);

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
};


#if defined(_WIN32)
// Windows 入口
VulkanExample *vulkanExample;
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (vulkanExample != NULL)
	{
		vulkanExample->handleMessages(hWnd, uMsg, wParam, lParam);
	}
	return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}
int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_  HINSTANCE hPrevInstance, _In_ LPSTR, _In_ int)
{
	for (size_t i = 0; i < __argc; i++) { VulkanExample::args.push_back(__argv[i]); };
	vulkanExample = new VulkanExample();
	vulkanExample->initVulkan();
	vulkanExample->setupWindow(hInstance, WndProc);
	vulkanExample->prepare();
	vulkanExample->renderLoop();
	delete(vulkanExample);
	return 0;
}

#elif defined(__ANDROID__)
// Android 入口
VulkanExample *vulkanExample;
void android_main(android_app* state)
{
	vulkanExample = new VulkanExample();
	state->userData = vulkanExample;
	state->onAppCmd = VulkanExample::handleAppCommand;
	state->onInputEvent = VulkanExample::handleAppInput;
	androidApp = state;
	vulkanExample->renderLoop();
	delete(vulkanExample);
}
#elif defined(_DIRECT2DISPLAY)

// Linux 入口（Direct to Display WSI）
// D2D 常用于嵌入式平台
VulkanExample *vulkanExample;
static void handleEvent()
{
}
int main(const int argc, const char *argv[])
{
	for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
	vulkanExample = new VulkanExample();
	vulkanExample->initVulkan();
	vulkanExample->prepare();
	vulkanExample->renderLoop();
	delete(vulkanExample);
	return 0;
}
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
VulkanExample *vulkanExample;
static void handleEvent(const DFBWindowEvent *event)
{
	if (vulkanExample != NULL)
	{
		vulkanExample->handleEvent(event);
	}
}
int main(const int argc, const char *argv[])
{
	for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
	vulkanExample = new VulkanExample();
	vulkanExample->initVulkan();
	vulkanExample->setupWindow();
	vulkanExample->prepare();
	vulkanExample->renderLoop();
	delete(vulkanExample);
	return 0;
}
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
VulkanExample *vulkanExample;
int main(const int argc, const char *argv[])
{
	for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
	vulkanExample = new VulkanExample();
	vulkanExample->initVulkan();
	vulkanExample->setupWindow();
	vulkanExample->prepare();
	vulkanExample->renderLoop();
	delete(vulkanExample);
	return 0;
}
#elif defined(__linux__) || defined(__FreeBSD__)

// Linux 入口
VulkanExample *vulkanExample;
#if defined(VK_USE_PLATFORM_XCB_KHR)
static void handleEvent(const xcb_generic_event_t *event)
{
	if (vulkanExample != NULL)
	{
		vulkanExample->handleEvent(event);
	}
}
#else
static void handleEvent()
{
}
#endif
int main(const int argc, const char *argv[])
{
	for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
	vulkanExample = new VulkanExample();
	vulkanExample->initVulkan();
	vulkanExample->setupWindow();
	vulkanExample->prepare();
	vulkanExample->renderLoop();
	delete(vulkanExample);
	return 0;
}
#elif (defined(VK_USE_PLATFORM_MACOS_MVK) || defined(VK_USE_PLATFORM_METAL_EXT)) && defined(VK_EXAMPLE_XCODE_GENERATED)
VulkanExample *vulkanExample;
int main(const int argc, const char *argv[])
{
	@autoreleasepool
	{
		for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };
		vulkanExample = new VulkanExample();
		vulkanExample->initVulkan();
		vulkanExample->setupWindow(nullptr);
		vulkanExample->prepare();
		vulkanExample->renderLoop();
		delete(vulkanExample);
	}
	return 0;
}
#elif defined(VK_USE_PLATFORM_SCREEN_QNX)
VULKAN_EXAMPLE_MAIN()
#endif