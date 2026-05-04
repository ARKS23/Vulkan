/*
* Vulkan 示例 - 使用 Vulkan 1.3 绘制基础索引三角形
*
* 说明：
* 这是 triangle 示例的一个变体，主要演示 Vulkan 1.3 的一些特性
* 其中最重要的是 dynamic rendering，它可以替代传统 render pass（连同 framebuffer 一起）
* 从而让 API 组织方式更简洁一些
*
* Copyright (C) 2024-2025 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

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

// 为了让 GPU 和 CPU 都尽量保持忙碌，我们会在上一帧仍在执行时就开始准备下一帧的命令缓冲
// 这个值定义了最多允许多少帧同时处于“进行中”状态
// 增大这个值可能带来更好的吞吐，但也会增加额外延迟
constexpr auto MAX_CONCURRENT_FRAMES = 2;

class VulkanExample : public VulkanExampleBase
{
public:
	// 本示例使用的顶点布局
	struct Vertex {
		float position[3];
		float color[3];
	};

	struct VulkanBuffer {
		VkDeviceMemory memory{ VK_NULL_HANDLE };
		VkBuffer handle{ VK_NULL_HANDLE };
	};

	VulkanBuffer vertexBuffer;
	VulkanBuffer indexBuffer;
	uint32_t indexCount{ 0 };

	// Uniform buffer 对象
	struct UniformBuffer : VulkanBuffer {
		// descriptor set 保存了 shader 各个 binding point 上实际绑定的资源
		// 它把 shader 中声明的 binding 和这里真正使用的 buffer / image 连接起来
		VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
		// 保存映射后的指针，方便后面直接通过 memcpy 更新内容
		uint8_t* mapped{ nullptr };
	};
	// 每个 in-flight frame 对应一份 UBO，这样帧之间可以重叠，也不会在 GPU 仍使用时被 CPU 改写
	std::array<UniformBuffer, MAX_CONCURRENT_FRAMES> uniformBuffers;

	// 为了简化示例，这里的 uniform 数据布局和 shader 中保持一致
	// 这样就可以直接把整块数据 memcpy 到 UBO 中
	// 注意：真实项目里最好优先使用和 GPU 对齐友好的类型，避免手动填充（如 vec4、mat4）
	struct ShaderData {
		glm::mat4 projectionMatrix;
		glm::mat4 modelMatrix;
		glm::mat4 viewMatrix;
	};

	// pipeline layout 用来告诉 pipeline：后续会绑定什么样的 descriptor set
	// 它定义的是 shader 资源接口，而不是具体资源本身
	// 只要接口兼容，多个 pipeline 可以共享同一个 pipeline layout
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };

	// pipeline（也常被称为 pipeline state object）会把影响图形管线的各种状态提前固化下来
	// 不像 OpenGL 可以在运行时随时改大量状态，Vulkan 更强调“提前描述完整状态”
	// 这样虽然需要更多前期规划，但也给驱动做优化提供了空间
	VkPipeline pipeline{ VK_NULL_HANDLE };

	// descriptor set layout 描述 shader 侧的 binding 布局，但它本身还不指向具体 descriptor
	// 可以把它理解成“资源接口蓝图”
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };

	// 同步原语
	// Vulkan 中同步是非常重要的概念，很多 OpenGL 时代被隐藏起来的问题在这里都要显式处理
	// semaphore 用来协调队列内不同操作之间的顺序
	std::vector<VkSemaphore> presentCompleteSemaphores{};
	std::vector<VkSemaphore> renderCompleteSemaphores{};
	// fence 用来确保 command buffer 在 GPU 执行完之前不会被重新录制
	std::array<VkFence, MAX_CONCURRENT_FRAMES> waitFences{};

	VkCommandPool commandPool{ VK_NULL_HANDLE };
	std::array<VkCommandBuffer, MAX_CONCURRENT_FRAMES> commandBuffers{};

	// 用来记录当前是第几个 in-flight frame，从而选到对应的同步对象和命令缓冲
	uint32_t currentFrame{ 0 };

	VkPhysicalDeviceVulkan13Features enabledFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };

	VulkanExample() : VulkanExampleBase()
	{
		title = "Basic indexed triangle using Vulkan 1.3";
		// 为了让示例尽量简单，这里不启用框架自带的 UI overlay
		settings.overlay = false;
		// 设置一个默认的 look-at 相机
		camera.type = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -2.5f));
		camera.setRotation(glm::vec3(0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 1.0f, 256.0f);
		// 我们希望显式使用 Vulkan 1.3，并开启 dynamic rendering 与 synchronization2 相关特性
		apiVersion = VK_API_VERSION_1_3;
		enabledFeatures.dynamicRendering = VK_TRUE;
		enabledFeatures.synchronization2 = VK_TRUE;
		deviceCreatepNextChain = &enabledFeatures;
	}

	~VulkanExample() override
	{
		// 清理本类创建的 Vulkan 资源
		// 注意：基类析构函数会继续清理基类自己持有的资源
		if (device) {
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			vkDestroyBuffer(device, vertexBuffer.handle, nullptr);
			vkFreeMemory(device, vertexBuffer.memory, nullptr);
			vkDestroyBuffer(device, indexBuffer.handle, nullptr);
			vkFreeMemory(device, indexBuffer.memory, nullptr);
			vkDestroyCommandPool(device, commandPool, nullptr);
			for (size_t i = 0; i < presentCompleteSemaphores.size(); i++) {
				vkDestroySemaphore(device, presentCompleteSemaphores[i], nullptr);
			}
			for (size_t i = 0; i < renderCompleteSemaphores.size(); i++) {
				vkDestroySemaphore(device, renderCompleteSemaphores[i], nullptr);
			}
			for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
				vkDestroyFence(device, waitFences[i], nullptr);
				vkDestroyBuffer(device, uniformBuffers[i].handle, nullptr);
				vkFreeMemory(device, uniformBuffers[i].memory, nullptr);
			}
		}
	}

	virtual void getEnabledFeatures() override
	{
		// 本示例要求设备至少支持 Vulkan 1.3
		if (deviceProperties.apiVersion < VK_API_VERSION_1_3) {
			vks::tools::exitFatal("Selected GPU does not support support Vulkan 1.3", VK_ERROR_INCOMPATIBLE_DRIVER);
		}
	}

	// 这个函数用于寻找满足指定属性标志的内存类型（例如 device local、host visible）
	// 成功时返回匹配的 memory type index
	// Vulkan 实现通常会提供多种不同属性组合的内存类型，所以这一步是必须的
	// 可参考 https://vulkan.gpuinfo.org/ 了解不同 GPU 的内存配置
	uint32_t getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties)
	{
		// 遍历当前设备可用的全部内存类型
		for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; i++) {
			if ((typeBits & 1) == 1) {
				if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
					return i;
				}
			}
			typeBits >>= 1;
		}
		throw "Could not find a suitable memory type!";
	}

	// 创建本示例使用的同步对象，包括按帧轮转和按 swapchain image 组织的同步原语
	void createSynchronizationPrimitives()
	{
		// fence 按 in-flight frame 维度创建
		for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {		
			// 用于确保 command buffer 执行完成后才可再次复用
			VkFenceCreateInfo fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			// 初始设为 signaled，这样第一帧不会在 wait fence 时卡住
			fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			VK_CHECK_RESULT(vkCreateFence(device, &fenceCI, nullptr, &waitFences[i]));
		}
		// semaphore 用于确保队列内的执行顺序正确
		// 这里的 presentCompleteSemaphore 用于等待 swapchain image 可用
		presentCompleteSemaphores.resize(MAX_CONCURRENT_FRAMES);
		for (auto& semaphore : presentCompleteSemaphores) {
			VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
		}
		// renderCompleteSemaphore 用于确保渲染完成后再把图像提交给 present
		renderCompleteSemaphores.resize(swapChain.images.size());
		for (auto& semaphore : renderCompleteSemaphores) {
			VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
		}
	}

	// command buffer 用来记录命令，之后再提交给队列执行
	void createCommandBuffers()
	{
		// 所有 command buffer 都从同一个 command pool 分配
		VkCommandPoolCreateInfo commandPoolCI{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		commandPoolCI.queueFamilyIndex = swapChain.queueNodeIndex;
		commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK_RESULT(vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool));
		// 从上面的 command pool 中为每个 in-flight frame 分配一个 primary command buffer
		VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, MAX_CONCURRENT_FRAMES);
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, commandBuffers.data()));
	}

	// 创建用于索引绘制三角形的顶点/索引缓冲
	// 同时借助 staging buffer 把数据上传到 device local memory，并准备好与 vertex shader 对应的输入布局
	void createVertexBuffer()
	{
		// 关于 Vulkan 内存管理补充一句：
		// 对示例程序来说，小块单独分配内存便于理解流程
		// 但在真实项目里，通常更推荐申请大块内存后自己做子分配

		// 顶点数据
		const std::vector<Vertex> vertices{
			{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
			{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			{ {  0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
		};
		uint32_t vertexBufferSize = static_cast<uint32_t>(vertices.size()) * sizeof(Vertex);

		// 索引数据
		// 三角形其实可以不使用索引直接绘制，这里保留索引只是为了演示更通用的绘制路径
		std::vector<uint32_t> indices{ 0, 1, 2 };
		indexCount = static_cast<uint32_t>(indices.size());
		uint32_t indexBufferSize = indexCount * sizeof(uint32_t);

		VkMemoryAllocateInfo memAlloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		VkMemoryRequirements memReqs;

		// 顶点和索引这类静态数据，通常最好放在 device local memory 中，以获得更好的 GPU 访问效率
		//
		// 因此这里使用经典的 staging buffer 路线：
		// - 先创建一个 CPU 可见且可映射的缓冲
		// - 把数据拷进去
		// - 再创建一个真正用于渲染的 device local buffer
		// - 通过 command buffer 把数据从 staging buffer 拷贝到目标 buffer
		// - 最后销毁 staging buffer
		//
		// 注意：如果平台是统一内存架构，staging 可能不是必需的
		// 为了保持示例清晰，这里不额外分支处理

		// 创建一个 host visible 的 staging buffer，顶点和索引都会先写到这里，再从这里拷到设备本地 buffer
		VulkanBuffer stagingBuffer;
		VkBufferCreateInfo stagingBufferCI{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		stagingBufferCI.size = vertexBufferSize + indexBufferSize;
		// 该缓冲会作为拷贝源
		stagingBufferCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		// 创建可被 CPU 看到的 staging buffer
		VK_CHECK_RESULT(vkCreateBuffer(device, &stagingBufferCI, nullptr, &stagingBuffer.handle));
		vkGetBufferMemoryRequirements(device, stagingBuffer.handle, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		// 请求 host visible 内存，方便 CPU 写入
		// 同时要求 host coherent，这样 unmap 后 GPU 可以立刻看到写入结果
		memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &stagingBuffer.memory));
		VK_CHECK_RESULT(vkBindBufferMemory(device, stagingBuffer.handle, stagingBuffer.memory, 0));
		// 映射 staging buffer，并把顶点和索引都写进去
		// 这样后面可以直接用这一块 staging buffer 给 vertex/index 两个目标 buffer 提供数据源
		uint8_t* data{ nullptr };
		VK_CHECK_RESULT(vkMapMemory(device, stagingBuffer.memory, 0, memAlloc.allocationSize, 0, (void**)&data));
		memcpy(data, vertices.data(), vertexBufferSize);
		memcpy(((char*)data) + vertexBufferSize, indices.data(), indexBufferSize);

		// 创建一个 device local 的顶点缓冲，后续会把 staging buffer 里的顶点数据拷到这里，真正绘制时也使用它
		VkBufferCreateInfo vertexbufferCI{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		vertexbufferCI.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		vertexbufferCI.size = vertexBufferSize;
		VK_CHECK_RESULT(vkCreateBuffer(device, &vertexbufferCI, nullptr, &vertexBuffer.handle));
		vkGetBufferMemoryRequirements(device, vertexBuffer.handle, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &vertexBuffer.memory));
		VK_CHECK_RESULT(vkBindBufferMemory(device, vertexBuffer.handle, vertexBuffer.memory, 0));

		// 创建一个 device local 的索引缓冲，后续会把 staging buffer 里的索引数据拷到这里，真正绘制时也使用它
		VkBufferCreateInfo indexbufferCI{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		indexbufferCI.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		indexbufferCI.size = indexBufferSize;
		VK_CHECK_RESULT(vkCreateBuffer(device, &indexbufferCI, nullptr, &indexBuffer.handle));
		vkGetBufferMemoryRequirements(device, indexBuffer.handle, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &indexBuffer.memory));
		VK_CHECK_RESULT(vkBindBufferMemory(device, indexBuffer.handle, indexBuffer.memory, 0));

		// buffer 拷贝同样要通过队列提交，因此这里需要一个临时 command buffer
		VkCommandBuffer copyCmd;

		VkCommandBufferAllocateInfo cmdBufAllocateInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		cmdBufAllocateInfo.commandPool = commandPool;
		cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdBufAllocateInfo.commandBufferCount = 1;
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &copyCmd));

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));
		// 把顶点和索引数据拷到 device local buffer
		VkBufferCopy copyRegion{};
		copyRegion.size = vertexBufferSize;
		vkCmdCopyBuffer(copyCmd, stagingBuffer.handle, vertexBuffer.handle, 1, &copyRegion);
		copyRegion.size = indexBufferSize;
		// 索引数据在 staging buffer 中紧跟在顶点数据后面，所以这里要设置 srcOffset
		copyRegion.srcOffset = vertexBufferSize;
		vkCmdCopyBuffer(copyCmd, stagingBuffer.handle, indexBuffer.handle,	1, &copyRegion);
		VK_CHECK_RESULT(vkEndCommandBuffer(copyCmd));

		// 把拷贝命令提交到队列执行
		VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &copyCmd;

		// 创建 fence，确保拷贝命令执行完成
		VkFenceCreateInfo fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		VkFence fence;
		VK_CHECK_RESULT(vkCreateFence(device, &fenceCI, nullptr, &fence));
		// 提交拷贝
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
		// 等待 fence 变为 signaled，确认拷贝已经结束
		VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));
		vkDestroyFence(device, fence, nullptr);
		vkFreeCommandBuffers(device, commandPool, 1, &copyCmd);

		// 既然 fence 已确认拷贝完成，就可以安全销毁 staging buffer
		vkDestroyBuffer(device, stagingBuffer.handle, nullptr);
		vkFreeMemory(device, stagingBuffer.memory, nullptr);
	}

	// descriptor 用来把数据传给 shader
	// 在这个 sample 里，我们主要用它把矩阵等 uniform 数据传给 vertex shader
	void createDescriptors()
	{
		// descriptor 需要从 pool 中分配，pool 要预先声明“最多会分配多少个、都是什么类型”
		VkDescriptorPoolSize descriptorTypeCounts[1]{};
		// 本示例只用到一种 descriptor：uniform buffer
		descriptorTypeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		// 每帧一份 uniform buffer，因此对应也要有每帧一个 descriptor
		descriptorTypeCounts[0].descriptorCount = MAX_CONCURRENT_FRAMES;
		// 如果后续需要更多 descriptor 类型，就继续往这个数组里追加
		// 比如 combined image sampler 等

		// 创建全局 descriptor pool
		// 这个示例中所有 descriptor 都从这里分配
		VkDescriptorPoolCreateInfo descriptorPoolCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		descriptorPoolCI.poolSizeCount = 1;
		descriptorPoolCI.pPoolSizes = descriptorTypeCounts;
		// 指定这个 pool 最多能分配多少个 descriptor set
		// 我们这里是每帧一个 uniform buffer，对应每帧一个 descriptor set
		descriptorPoolCI.maxSets = MAX_CONCURRENT_FRAMES;
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));

		// descriptor set layout 用来定义应用和 shader 之间的资源接口
		// 可以理解成：shader 里每一个 binding 都要在这里对应描述出来
		// 这里的 binding 0 对应 vertex shader 使用的 uniform buffer
		VkDescriptorSetLayoutBinding layoutBinding{};
		layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		layoutBinding.descriptorCount = 1;
		layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		descriptorLayoutCI.bindingCount = 1;
		descriptorLayoutCI.pBindings = &layoutBinding;
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayout));

		// descriptor set layout 只是接口描述，descriptor set 才真正指向具体数据
		// 由于每帧 descriptor 内容都可能变化，所以要按 in-flight frame 数量各建一份
		for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
			VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &descriptorSetLayout;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &uniformBuffers[i].descriptorSet));

			// 更新 descriptor set，让 shader 的 binding point 真正指向我们准备好的资源
			VkWriteDescriptorSet writeDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

			// 用 descriptor info 来描述这个 buffer
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = uniformBuffers[i].handle;
			bufferInfo.range = sizeof(ShaderData);

			// binding 0：uniform buffer
			writeDescriptorSet.dstSet = uniformBuffers[i].descriptorSet;
			writeDescriptorSet.descriptorCount = 1;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSet.pBufferInfo = &bufferInfo;
			writeDescriptorSet.dstBinding = 0;
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
		}
	}

	// 创建深度（以及模板）附件
	// 虽然这个 sample 本身并不依赖复杂深度测试，但 depth attachment 在真实项目里非常常见，所以从最小示例开始就值得一起学习
	void setupDepthStencil() override
	{
		// 创建一个 optimal tiled 的 image，作为深度/模板附件使用
		VkImageCreateInfo imageCI{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = depthFormat;
		imageCI.extent = { width, height, 1 };
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &depthStencil.image));

		// 为该 image 分配 device local 内存，并绑定到 image 上
		VkMemoryAllocateInfo memAlloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, depthStencil.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &depthStencil.memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, depthStencil.image, depthStencil.memory, 0));

		// 为深度/模板 image 创建 image view
		// Vulkan 中通常不是直接访问 image，而是通过 image view 访问其某个子范围
		VkImageViewCreateInfo depthStencilViewCI{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		depthStencilViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthStencilViewCI.format = depthFormat;
		depthStencilViewCI.subresourceRange = {};
		depthStencilViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		// 只有 depth+stencil 格式才需要把 stencil aspect 也包含进来
		if (depthFormat >= VK_FORMAT_D16_UNORM_S8_UINT) {
			depthStencilViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		depthStencilViewCI.subresourceRange.baseMipLevel = 0;
		depthStencilViewCI.subresourceRange.levelCount = 1;
		depthStencilViewCI.subresourceRange.baseArrayLayer = 0;
		depthStencilViewCI.subresourceRange.layerCount = 1;
		depthStencilViewCI.image = depthStencil.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilViewCI, nullptr, &depthStencil.view));
	}

	// Vulkan 运行时加载的 shader 是 SPIR-V
	// shader 源码通常离线编译成 SPIR-V，这个函数负责把二进制文件读入并创建 shader module
	VkShaderModule loadSPIRVShader(const std::string& filename)
	{
		size_t shaderSize;
		char* shaderCode{ nullptr };

#if defined(__ANDROID__)
		// 从 Android 压缩资源中加载 shader
		AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		assert(asset);
		shaderSize = AAsset_getLength(asset);
		assert(shaderSize > 0);

		shaderCode = new char[shaderSize];
		AAsset_read(asset, shaderCode, shaderSize);
		AAsset_close(asset);
#else
		std::ifstream is(filename, std::ios::binary | std::ios::in | std::ios::ate);

		if (is.is_open()) {
			shaderSize = is.tellg();
			is.seekg(0, std::ios::beg);
			// 把文件内容读进一块内存
			shaderCode = new char[shaderSize];
			is.read(shaderCode, shaderSize);
			is.close();
			assert(shaderSize > 0);
		}
#endif
		if (shaderCode) {
			// 创建 shader module，后续 pipeline 创建时会使用它
			VkShaderModuleCreateInfo shaderModuleCI{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
			shaderModuleCI.codeSize = shaderSize;
			shaderModuleCI.pCode = (uint32_t*)shaderCode;

			VkShaderModule shaderModule;
			VK_CHECK_RESULT(vkCreateShaderModule(device, &shaderModuleCI, nullptr, &shaderModule));

			delete[] shaderCode;

			return shaderModule;
		} else {
			std::cerr << "Error: Could not open shader file \"" << filename << "\"" << std::endl;
			return VK_NULL_HANDLE;
		}
	}

	void createPipeline()
	{
		// pipeline layout 描述了未来会绑定给 pipeline 的 descriptor 接口形式
		VkPipelineLayoutCreateInfo pipelineLayoutCI{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipelineLayoutCI.setLayoutCount = 1;
		pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

		// 创建本示例使用的 graphics pipeline
		// Vulkan 会把固定功能状态和 shader 一起组织进 pipeline，中途切换时效率更高

		VkGraphicsPipelineCreateInfo pipelineCI{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		// 指定本 pipeline 使用的 pipeline layout
		pipelineCI.layout = pipelineLayout;

		// 下面开始依次构建组成 pipeline 的各类状态

		// Input assembly：定义顶点如何组装成图元
		// 这里使用 triangle list
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// Rasterization 状态
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizationStateCI.depthClampEnable = VK_FALSE;
		rasterizationStateCI.rasterizerDiscardEnable = VK_FALSE;
		rasterizationStateCI.depthBiasEnable = VK_FALSE;
		rasterizationStateCI.lineWidth = 1.0f;

		// Color blend 状态描述颜色如何混合
		// 即使这里不启用 blending，也要为每个 color attachment 提供一份 attachment state
		VkPipelineColorBlendAttachmentState blendAttachmentState{};
		blendAttachmentState.colorWriteMask = 0xf;
		blendAttachmentState.blendEnable = VK_FALSE;
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		colorBlendStateCI.attachmentCount = 1;
		colorBlendStateCI.pAttachments = &blendAttachmentState;

		// Viewport state 描述这个 pipeline 预期会使用多少个 viewport / scissor
		// 注意：真正的 viewport/scissor 数值会在 command buffer 中通过 dynamic state 指定
		VkPipelineViewportStateCreateInfo viewportStateCI{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewportStateCI.viewportCount = 1;
		viewportStateCI.scissorCount = 1;

		// 开启 dynamic state
		// Vulkan 中大多数状态会固化进 pipeline，但也有少数状态可以在录命令时动态设置
		// 这里把 viewport 和 scissor 标记为动态状态，实际值稍后在 command buffer 中给出
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
		dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

		// Depth/stencil 状态
		// 这里开启深度测试与深度写入，并采用 less-or-equal 作为比较方式
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		depthStencilStateCI.depthTestEnable = VK_TRUE;
		depthStencilStateCI.depthWriteEnable = VK_TRUE;
		depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencilStateCI.depthBoundsTestEnable = VK_FALSE;
		depthStencilStateCI.back.failOp = VK_STENCIL_OP_KEEP;
		depthStencilStateCI.back.passOp = VK_STENCIL_OP_KEEP;
		depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;
		depthStencilStateCI.stencilTestEnable = VK_FALSE;
		depthStencilStateCI.front = depthStencilStateCI.back;

		// 本示例不使用 MSAA，但 multisample 状态仍然需要提供
		VkPipelineMultisampleStateCreateInfo multisampleStateCI{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Vertex input 描述，用来告诉 pipeline 顶点数据如何解释

		// 顶点输入 binding
		// 这里在 binding 0 上绑定一个顶点缓冲（见 vkCmdBindVertexBuffers）
		VkVertexInputBindingDescription vertexInputBinding{};
		vertexInputBinding.binding = 0;
		vertexInputBinding.stride = sizeof(Vertex);
		vertexInputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		// attribute 描述 shader 输入 location 与内存布局的对应关系
		std::array<VkVertexInputAttributeDescription, 2> vertexInputAttributs{};
		// 这里要和 shader 中的输入完全对齐：
		//	layout (location = 0) in vec3 inPos;
		//	layout (location = 1) in vec3 inColor;
		// location 0：位置
		vertexInputAttributs[0].binding = 0;
		vertexInputAttributs[0].location = 0;
		// 位置由三个 32 位浮点数组成（R32 G32 B32）
		vertexInputAttributs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertexInputAttributs[0].offset = offsetof(Vertex, position);
		// location 1：颜色
		vertexInputAttributs[1].binding = 0;
		vertexInputAttributs[1].location = 1;
		// 颜色同样由三个 32 位浮点数组成（R32 G32 B32）
		vertexInputAttributs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertexInputAttributs[1].offset = offsetof(Vertex, color);

		// 用于 pipeline 创建的 vertex input 状态
		VkPipelineVertexInputStateCreateInfo vertexInputStateCI{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		vertexInputStateCI.vertexBindingDescriptionCount = 1;
		vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
		vertexInputStateCI.vertexAttributeDescriptionCount = 2;
		vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributs.data();

		// shader 阶段
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

		// 顶点着色器
		shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		shaderStages[0].module = loadSPIRVShader(getShadersPath() + "triangle/triangle.vert.spv");
		shaderStages[0].pName = "main";
		assert(shaderStages[0].module != VK_NULL_HANDLE);

		// 片段着色器
		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderStages[1].module = loadSPIRVShader(getShadersPath() + "triangle/triangle.frag.spv");
		shaderStages[1].pName = "main";
		assert(shaderStages[1].module != VK_NULL_HANDLE);

		// 把 shader stage 信息挂到 pipeline create info 上
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		// dynamic rendering 需要在 pipeline 创建时显式提供 attachment format 信息
		VkPipelineRenderingCreateInfoKHR pipelineRenderingCI{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
		pipelineRenderingCI.colorAttachmentCount = 1;
		pipelineRenderingCI.pColorAttachmentFormats = &swapChain.colorFormat;
		pipelineRenderingCI.depthAttachmentFormat = depthFormat;
		pipelineRenderingCI.stencilAttachmentFormat = depthFormat;

		// 把前面准备好的各种状态结构挂到 pipeline create info 上
		pipelineCI.pVertexInputState = &vertexInputStateCI;
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.pNext = &pipelineRenderingCI;

		// 根据这些状态创建 graphics pipeline
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));

		// pipeline 创建完成后，shader module 就可以销毁了
		vkDestroyShaderModule(device, shaderStages[0].module, nullptr);
		vkDestroyShaderModule(device, shaderStages[1].module, nullptr);
	}

	void createUniformBuffers()
	{
		// 创建并初始化每帧的 uniform buffer
		// Vulkan 不像老式 OpenGL 那样直接设置单个 uniform，通常都是通过 uniform buffer block 传递
		VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = sizeof(ShaderData);
		// 指定这个 buffer 的用途是 uniform buffer
		bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

		// 为每个 in-flight frame 各创建一份 uniform buffer
		for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
			VK_CHECK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &uniformBuffers[i].handle));
			// 读取该 buffer 的内存需求，包括大小、对齐以及可用 memory type
			VkMemoryRequirements memReqs;
			vkGetBufferMemoryRequirements(device, uniformBuffers[i].handle, &memReqs);
			VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			// 注意：这里分配时使用的是内存需求中的大小，而不是逻辑 buffer 大小
			// 因为设备可能会因为对齐要求让实际分配尺寸更大
			allocInfo.allocationSize = memReqs.size;
			// 选择支持 host visible 的内存，便于 CPU 更新
			// 同时要求 host coherent，这样每次写完后不用显式 flush
			allocInfo.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			// 为 uniform buffer 分配内存
			VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &(uniformBuffers[i].memory)));
			// 绑定 buffer 和内存
			VK_CHECK_RESULT(vkBindBufferMemory(device, uniformBuffers[i].handle, uniformBuffers[i].memory, 0));
			// 只 map 一次，后面每帧直接 memcpy 更新即可
			VK_CHECK_RESULT(vkMapMemory(device, uniformBuffers[i].memory, 0, sizeof(ShaderData), 0, (void**)&uniformBuffers[i].mapped));
		}

	}

	void prepare() override
	{
		VulkanExampleBase::prepare();
		createSynchronizationPrimitives();
		createCommandBuffers();
		createVertexBuffer();
		createUniformBuffers();
		createDescriptors();
		createPipeline();
		prepared = true;
	}

	void render() override
	{
		// 用 fence 等待当前帧对应的 command buffer 执行完成，之后才能安全重用
		vkWaitForFences(device, 1, &waitFences[currentFrame], VK_TRUE, UINT64_MAX);
		VK_CHECK_RESULT(vkResetFences(device, 1, &waitFences[currentFrame]));

		// 从 swapchain 中获取下一张可渲染图像
		// 注意：实现层可以按任意顺序返回 image，所以必须通过 acquire 调用获取，而不能自己按索引轮着猜
		uint32_t imageIndex{ 0 };
		VkResult result = vkAcquireNextImageKHR(device, swapChain.swapChain, UINT64_MAX, presentCompleteSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			windowResize();
			return;
		} else if ((result != VK_SUCCESS) && (result != VK_SUBOPTIMAL_KHR)) {
			throw "Could not acquire the next swap chain image!";
		}

		// 更新当前帧要使用的 uniform buffer
		ShaderData shaderData{};
		shaderData.projectionMatrix = camera.matrices.perspective;
		shaderData.viewMatrix = camera.matrices.view;
		shaderData.modelMatrix = glm::mat4(1.0f);
		// 把当前矩阵写入这一帧对应的 uniform buffer
		// 因为这里申请的是 host coherent 内存，所以写入结果会立刻对 GPU 可见
		memcpy(uniformBuffers[currentFrame].mapped, &shaderData, sizeof(ShaderData));

		// 录制当前帧的 command buffer
		vkResetCommandBuffer(commandBuffers[currentFrame], 0);
		VkCommandBufferBeginInfo cmdBufInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		const VkCommandBuffer commandBuffer = commandBuffers[currentFrame];
		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufInfo));

		// dynamic rendering 不像传统 render pass 那样自动帮你处理 attachment layout
		// 所以这里要手动插 barrier，把 color/depth image 切换到适合输出的 layout
		vks::tools::insertImageMemoryBarrier(commandBuffer, swapChain.images[imageIndex], 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
		vks::tools::insertImageMemoryBarrier(commandBuffer, depthStencil.image, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VkImageSubresourceRange{ VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 });

		// dynamic rendering 使用新的结构来描述本次渲染要用到的 attachment
		// 颜色附件
		VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		colorAttachment.imageView = swapChain.imageViews[imageIndex];
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.clearValue.color = { 0.0f, 0.0f, 0.2f, 0.0f };
		// 深度/模板附件
		VkRenderingAttachmentInfo depthStencilAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		depthStencilAttachment.imageView = depthStencil.view;
		depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthStencilAttachment.clearValue.depthStencil = { 1.0f,  0 };

		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
		renderingInfo.renderArea = { 0, 0, width, height };
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;
		renderingInfo.pDepthAttachment = &depthStencilAttachment;
		renderingInfo.pStencilAttachment = &depthStencilAttachment;

		// 开始一个 dynamic rendering 渲染段
		vkCmdBeginRendering(commandBuffer, &renderingInfo);
		// 设置动态 viewport
		VkViewport viewport{ 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		// 设置动态 scissor
		VkRect2D scissor{ 0, 0, width, height };
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
		// 绑定当前帧的 descriptor set，让 shader 使用这一帧对应的 uniform buffer
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &uniformBuffers[currentFrame].descriptorSet, 0, nullptr);
		// 绑定 pipeline，等于把图形管线相关的大部分固定状态一次性切换到位
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		// 绑定三角形顶点缓冲（包含位置和颜色）
		VkDeviceSize offsets[1]{ 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer.handle, offsets);
		// 绑定索引缓冲
		vkCmdBindIndexBuffer(commandBuffer, indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
		// 发出索引绘制命令
		vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
		// 结束当前 dynamic rendering 渲染段
		vkCmdEndRendering(commandBuffer);

		// 这个 barrier 把 color image 准备到 present layout
		// 深度图像在这里不需要额外处理
		vks::tools::insertImageMemoryBarrier(commandBuffer, swapChain.images[imageIndex], VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_NONE, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		// 把 command buffer 提交给 graphics queue

		// 指定等待 semaphore 时，对应发生在图形管线的哪个阶段
		VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		// submit info 用于描述这一批要提交的命令
		VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.pWaitDstStageMask = &waitStageMask;      // 等待 semaphore 时所处的 pipeline stage
		submitInfo.pCommandBuffers = &commandBuffer;		// 本次提交要执行的 command buffer
		submitInfo.commandBufferCount = 1;                  // 这里只提交一个 command buffer

		// 执行 command buffer 前，需要先等待 image acquire 完成
		submitInfo.pWaitSemaphores = &presentCompleteSemaphores[currentFrame];
		submitInfo.waitSemaphoreCount = 1;
		// command buffer 执行结束后，发出 render complete 信号
		submitInfo.pSignalSemaphores = &renderCompleteSemaphores[imageIndex];
		submitInfo.signalSemaphoreCount = 1;

		// 提交到 graphics queue，同时附带 fence，方便 CPU 侧等待这一帧执行完成
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, waitFences[currentFrame]));

		// 把当前图像提交给 swapchain 进行显示
		// 这里把上一步提交时 signal 的 semaphore 作为 present 的 wait semaphore
		// 这样可以确保渲染没完成之前，图像不会被窗口系统拿去显示
		VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderCompleteSemaphores[imageIndex];
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapChain.swapChain;
		presentInfo.pImageIndices = &imageIndex;
		result = vkQueuePresentKHR(queue, &presentInfo);
		if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
			windowResize();
		} else if (result != VK_SUCCESS) {
			throw "Could not present the image to the swap chain!";
		}

		// 切换到下一帧对应的资源槽位
		currentFrame = (currentFrame + 1) % MAX_CONCURRENT_FRAMES;
	}

	// 这里把它们重写成空实现，否则基类会按传统路径去创建 framebuffer 和 render pass
	void setupFrameBuffer() override {}
	void setupRenderPass() override {}
};

// 各平台入口
// 大部分代码在不同平台之间是共享的，主要差异在窗口系统和消息处理上

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
