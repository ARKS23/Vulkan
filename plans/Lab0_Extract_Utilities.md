# Lab0 工具函数抽离指南：从 sample 成员函数走向可复用 Vulkan Helper

你现在已经完成了两件很关键的事：

```text
AllocatedBuffer / GPUMeshBuffers
-> 把 GPU mesh buffer 变成明确的数据结构

createDeviceLocalBuffer() / immediateSubmit()
-> 把 staging upload 和一次性命令提交封装起来
```

下一步不是继续往 `Lab0` 里堆函数，而是把这些 helper 从 `VulkanExample` 成员函数里拆出来，让它们变成后续所有 sample 都能复用的工具函数。

这一轮重构的核心思想是：

```text
不要让工具函数偷偷依赖某个 sample 的成员变量。
把它需要的 Vulkan 对象作为参数显式传进去。
```

也就是把：

```cpp
createDeviceLocalBuffer(...); // 内部偷偷使用 allocator / device / queue / commandPool
```

改成：

```cpp
vkutil::createDeviceLocalBuffer(
    allocator,
    submitContext,
    data,
    size,
    usage);
```

这一步做完后，你的 Lab0 会更像一个真正的小型 Vulkan framework，而不是一个越来越长的教学 sample。

## 1. 这一轮应该抽什么，不应该抽什么

建议现在抽离这些函数：

```cpp
createAllocatedBuffer()
destroyAllocatedBuffer()
createDeviceLocalBuffer()
immediateSubmit()
```

它们的共同点是：不关心你画的是三角形、圆盘、模型还是 Games202 实验对象，只关心 Vulkan 资源创建和命令提交。

暂时不要抽这些东西：

```cpp
createCircleMesh()
createPipeline()
createDescriptors()
createUniformBuffers()
createVmaAllocator()
```

原因分别是：

```text
createCircleMesh()
-> sample 内容，属于 Lab0 自己的 CPU 几何生成逻辑。

createPipeline()
-> 现在还强依赖 Vertex 格式、shader、descriptor layout，先别急着框架化。

createDescriptors()
-> descriptor 抽象需要等你做 texture、material、UBO/SSBO 后再统一设计。

createUniformBuffers()
-> 之后可以迁移到 VMA，但现在先别和本轮工具抽离混在一起。

createVmaAllocator()
-> 它依赖 instance / physicalDevice / device 生命周期，先留在 Lab0。
```

这轮我们只做一件事：把 buffer helper 和 immediate submit 从 sample 里拆出来。

## 2. 推荐文件结构

建议在 `base/` 下新增两组文件：

```text
base/vk_commands.h
base/vk_commands.cpp

base/vk_resources.h
base/vk_resources.cpp
```

职责划分如下：

```text
vk_commands
-> 管一次性 command buffer 提交。
-> 当前只放 immediateSubmit。
-> 未来可以放 transitionImage、copyBufferToImage、generateMipmaps 等命令型 helper。

vk_resources
-> 管 Vulkan/VMA 资源创建和销毁。
-> 当前放 buffer 创建、buffer 销毁、CPU upload 到 device local buffer。
-> 未来可以放 createAllocatedImage、destroyAllocatedImage、createTextureImage 等资源型 helper。
```

也可以只建一个 `vk_utils.h/.cpp`，但我更建议从一开始就分成 commands/resources。
这样未来不会出现一个巨大杂物间文件。

## 3. 第一步：新增 vk_commands

新建：

```text
base/vk_commands.h
```

内容建议：

```cpp
#pragma once

#include <functional>
#include <vulkan/vulkan.h>

namespace vkutil {

struct ImmediateSubmitContext {
    VkDevice device{ VK_NULL_HANDLE };
    VkQueue queue{ VK_NULL_HANDLE };
    VkCommandPool commandPool{ VK_NULL_HANDLE };
};

void immediateSubmit(
    const ImmediateSubmitContext& context,
    std::function<void(VkCommandBuffer cmd)>&& function);

}
```

这里最重要的是 `ImmediateSubmitContext`。

它把原来 `VulkanExample::immediateSubmit()` 偷偷用到的成员变量显式列出来：

```text
device
queue
commandPool
```

这就是“去 sample 依赖”的关键一步。

然后新建：

```text
base/vk_commands.cpp
```

内容建议：

```cpp
#include "vk_commands.h"

#include "VulkanTools.h"

namespace vkutil {

void immediateSubmit(
    const ImmediateSubmitContext& context,
    std::function<void(VkCommandBuffer cmd)>&& function)
{
    VkCommandBuffer cmd{ VK_NULL_HANDLE };

    VkCommandBufferAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocateInfo.commandPool = context.commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    VK_CHECK_RESULT(vkAllocateCommandBuffers(context.device, &allocateInfo, &cmd));

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK_RESULT(vkBeginCommandBuffer(cmd, &beginInfo));

    function(cmd);

    VK_CHECK_RESULT(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence{ VK_NULL_HANDLE };
    VK_CHECK_RESULT(vkCreateFence(context.device, &fenceInfo, nullptr, &fence));

    VK_CHECK_RESULT(vkQueueSubmit(context.queue, 1, &submitInfo, fence));
    VK_CHECK_RESULT(vkWaitForFences(context.device, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

    vkDestroyFence(context.device, fence, nullptr);
    vkFreeCommandBuffers(context.device, context.commandPool, 1, &cmd);
}

}
```

这里我建议不要继续依赖：

```cpp
vks::initializers::commandBufferBeginInfo()
```

而是直接手写 `VkCommandBufferBeginInfo`。
因为 `vk_commands` 是基础工具层，依赖越少越干净。

## 4. 第二步：新增 vk_resources

新建：

```text
base/vk_resources.h
```

内容建议：

```cpp
#pragma once

#include "vk_commands.h"
#include "vk_types.h"

namespace vkutil {

AllocatedBuffer createAllocatedBuffer(
    VmaAllocator allocator,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaAllocationCreateFlags allocationFlags,
    VmaMemoryUsage memoryUsage);

void destroyAllocatedBuffer(
    VmaAllocator allocator,
    AllocatedBuffer& buffer);

AllocatedBuffer createDeviceLocalBuffer(
    VmaAllocator allocator,
    const ImmediateSubmitContext& submitContext,
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage);

}
```

注意这里我没有把 `AllocatedBuffer` 放进 `vkutil` namespace。

因为你现在已经在 `vk_types.h` 里定义好了全局的：

```cpp
struct AllocatedBuffer
struct GPUMeshBuffers
```

本轮先不要大改类型命名空间，避免一次重构牵扯太大。

未来稳定后可以再做一轮：

```cpp
vkutil::AllocatedBuffer
vkutil::GPUMeshBuffers
```

但现在不是必须。

然后新建：

```text
base/vk_resources.cpp
```

内容建议：

```cpp
#include "vk_resources.h"

#include <cstring>

#include "VulkanTools.h"

namespace vkutil {

AllocatedBuffer createAllocatedBuffer(
    VmaAllocator allocator,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaAllocationCreateFlags allocationFlags,
    VmaMemoryUsage memoryUsage)
{
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = size;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memoryUsage;
    allocationInfo.flags = allocationFlags;

    AllocatedBuffer buffer{};
    buffer.size = size;

    VK_CHECK_RESULT(vmaCreateBuffer(
        allocator,
        &bufferInfo,
        &allocationInfo,
        &buffer.handle,
        &buffer.allocation,
        &buffer.allocationInfo));

    return buffer;
}

void destroyAllocatedBuffer(
    VmaAllocator allocator,
    AllocatedBuffer& buffer)
{
    if (buffer.handle != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
        buffer.handle = VK_NULL_HANDLE;
        buffer.allocation = VK_NULL_HANDLE;
        buffer.allocationInfo = {};
        buffer.size = 0;
    }
}

AllocatedBuffer createDeviceLocalBuffer(
    VmaAllocator allocator,
    const ImmediateSubmitContext& submitContext,
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage)
{
    AllocatedBuffer stagingBuffer = createAllocatedBuffer(
        allocator,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT,
        VMA_MEMORY_USAGE_AUTO);

    std::memcpy(stagingBuffer.allocationInfo.pMappedData, data, static_cast<size_t>(size));
    VK_CHECK_RESULT(vmaFlushAllocation(allocator, stagingBuffer.allocation, 0, size));

    AllocatedBuffer deviceBuffer = createAllocatedBuffer(
        allocator,
        size,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        0,
        VMA_MEMORY_USAGE_AUTO);

    immediateSubmit(submitContext, [&](VkCommandBuffer cmd) {
        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, stagingBuffer.handle, deviceBuffer.handle, 1, &copyRegion);
    });

    destroyAllocatedBuffer(allocator, stagingBuffer);

    return deviceBuffer;
}

}
```

这份实现的依赖关系是：

```text
vk_resources
-> vk_types
-> vk_commands
-> VulkanTools
-> VMA
```

它不再知道 `VulkanExample` 是什么，也不知道 `Lab0` 是什么。

这就是我们想要的效果。

## 5. 第三步：处理 CMake

当前 `base/CMakeLists.txt` 里已经有：

```cmake
file(GLOB BASE_SRC "*.cpp" "*.hpp" "*.h" "../external/imgui/*.cpp")
```

理论上新增的 `base/vk_commands.cpp` 和 `base/vk_resources.cpp` 会被 glob 收进去。

但 Visual Studio + CMake 有时不会自动重新扫描新增文件。
如果你新增文件后编译提示找不到符号，先做一次：

```text
CMake: Configure
```

如果还是不稳，可以在 `base/CMakeLists.txt` 里显式追加：

```cmake
list(APPEND BASE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/vk_initializers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vk_initializers.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/vma_impl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vk_commands.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vk_commands.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/vk_resources.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vk_resources.h")
```

你之前遇到过 `vk_initializers.cpp` 没链接进来的 LNK2019。
这次如果出现类似：

```text
unresolved external symbol vkutil::createDeviceLocalBuffer
unresolved external symbol vkutil::immediateSubmit
```

大概率就是 `.cpp` 没被编进 `base` 静态库。

## 6. 第四步：改 Lab0 的 include 和声明

在 `examples/Lab0/lab0.h` 里加入：

```cpp
#include "vk_resources.h"
```

如果 `lab0.cpp` 也直接使用 `vkutil::ImmediateSubmitContext`，也可以放在 `lab0.cpp` 里 include。
我建议 `lab0.h` 尽量少 include，`lab0.cpp` 需要什么再 include 什么。

然后从 `VulkanExample` 类里删掉这些成员函数声明：

```cpp
AllocatedBuffer createAllocatedBuffer(...);
AllocatedBuffer createDeviceLocalBuffer(...);
void destroyAllocatedBuffer(...);
void immediateSubmit(...);
```

保留：

```cpp
void createVmaAllocator();
void destroyVmaAllocator();
```

因为 allocator 生命周期暂时还是 sample 自己管理。

你也可以在 `lab0.cpp` 里写一个小的局部 context 创建方式：

```cpp
vkutil::ImmediateSubmitContext submitContext{
    device,
    queue,
    commandPool
};
```

暂时不建议在 `lab0.h` 里再加一个 `getImmediateSubmitContext()` 成员函数。
因为它只是三行代码，先保持直观。

## 7. 第五步：替换 createVertexBuffer

你当前的调用类似：

```cpp
circleMeshBuffers.vertexBuffer = createDeviceLocalBuffer(
    circle.vertices.data(),
    vertexBufferSize,
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
```

改成：

```cpp
vkutil::ImmediateSubmitContext submitContext{
    device,
    queue,
    commandPool
};

circleMeshBuffers.vertexBuffer = vkutil::createDeviceLocalBuffer(
    allocator,
    submitContext,
    circle.vertices.data(),
    vertexBufferSize,
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

circleMeshBuffers.indexBuffer = vkutil::createDeviceLocalBuffer(
    allocator,
    submitContext,
    circle.indices.data(),
    indexBufferSize,
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
```

这段代码现在读起来会非常清楚：

```text
用 allocator 创建资源
用 submitContext 提交一次 copy
把 CPU 数据上传成 device local buffer
```

这比成员函数隐式访问 `allocator/queue/commandPool` 更适合长期维护。

## 8. 第六步：替换销毁逻辑

你当前析构里类似：

```cpp
destroyAllocatedBuffer(circleMeshBuffers.vertexBuffer);
destroyAllocatedBuffer(circleMeshBuffers.indexBuffer);
```

改成：

```cpp
vkutil::destroyAllocatedBuffer(allocator, circleMeshBuffers.vertexBuffer);
vkutil::destroyAllocatedBuffer(allocator, circleMeshBuffers.indexBuffer);
```

销毁顺序要保持：

```text
先销毁 AllocatedBuffer
再销毁 VmaAllocator
```

也就是：

```cpp
vkutil::destroyAllocatedBuffer(...);
vkutil::destroyAllocatedBuffer(...);

destroyVmaAllocator();
```

不要反过来。
`AllocatedBuffer` 的 `VmaAllocation` 是 allocator 创建的，allocator 没了以后再销毁 buffer 就是悬空资源。

## 9. 第七步：删除 Lab0 里的旧 helper 实现

确认所有调用都改成 `vkutil::` 后，删除 `lab0.cpp` 里的这些定义：

```cpp
AllocatedBuffer VulkanExample::createAllocatedBuffer(...)
AllocatedBuffer VulkanExample::createDeviceLocalBuffer(...)
void VulkanExample::destroyAllocatedBuffer(...)
void VulkanExample::immediateSubmit(...)
```

这一步很重要。

如果你只新增了工具函数，但旧成员函数还留着，代码虽然能跑，却没有真正完成抽离。
好的重构状态应该是：

```text
Lab0 只描述这个 sample 想创建什么资源。
base/vk_resources 负责资源怎么创建。
base/vk_commands 负责一次性命令怎么提交。
```

## 10. 本轮完成后的依赖图

完成后结构应该像这样：

```text
examples/Lab0/lab0.cpp
    |
    | uses
    v
base/vk_resources.h/.cpp
    |
    | uses
    v
base/vk_commands.h/.cpp
    |
    | uses
    v
Vulkan device / queue / commandPool
```

资源侧：

```text
Lab0 owns:
    VmaAllocator allocator
    GPUMeshBuffers circleMeshBuffers

vk_resources creates/destroys:
    AllocatedBuffer

vk_commands submits:
    vkCmdCopyBuffer
```

这就是一个很干净的分层：

```text
sample 层：我要画什么
resource helper 层：我要怎么创建 GPU 资源
command helper 层：我要怎么提交一次性命令
Vulkan 层：真正执行 API 调用
```

## 11. 常见错误和排查

如果出现 LNK2019：

```text
unresolved external symbol vkutil::...
```

检查：

```text
vk_commands.cpp 是否加入 base target
vk_resources.cpp 是否加入 base target
CMake 是否重新 configure
函数声明和定义的 namespace 是否一致
```

如果出现找不到 `VmaAllocator` 或 `AllocatedBuffer`：

```text
vk_resources.h 是否 include "vk_types.h"
vk_types.h 是否 include <vk_mem_alloc.h>
base target 是否包含 external/vma include directory
```

如果运行黑屏或启动后关闭：

```text
createCommandBuffers() 必须在 createVertexBuffer() 前执行
因为 createDeviceLocalBuffer() 需要 commandPool

createVmaAllocator() 必须在 createVertexBuffer() 前执行
因为 createDeviceLocalBuffer() 需要 allocator

destroyAllocatedBuffer() 必须在 destroyVmaAllocator() 前执行
```

如果 validation layer 报 command pool 问题：

```text
检查 ImmediateSubmitContext.commandPool 是否是有效 commandPool
检查 commandPool 是否属于当前 queue family
当前 Lab0 使用 swapChain.queueNodeIndex 创建 commandPool，是可以的
```

如果出现 `VK_ERROR_MEMORY_MAP_FAILED` 或数据没拷进去：

```text
staging buffer 必须带 VMA_ALLOCATION_CREATE_MAPPED_BIT
staging buffer 必须带 VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
写完后保留 vmaFlushAllocation()
```

虽然某些内存可能是 HOST_COHERENT，但保留 flush 更稳，也更适合作为学习阶段的明确步骤。

## 12. 验收清单

完成后用 `rg` 检查：

```powershell
rg -n "createAllocatedBuffer|createDeviceLocalBuffer|destroyAllocatedBuffer|immediateSubmit" examples/Lab0 base
```

理想结果是：

```text
base/vk_commands.h/.cpp
-> immediateSubmit 声明和定义

base/vk_resources.h/.cpp
-> createAllocatedBuffer / destroyAllocatedBuffer / createDeviceLocalBuffer 声明和定义

examples/Lab0/lab0.cpp
-> 只有 vkutil::createDeviceLocalBuffer 和 vkutil::destroyAllocatedBuffer 的调用
```

然后编译：

```powershell
cmake --build build --config Debug --target lab0 -j 32
```

如果你想顺手跑 validation：

```powershell
build\bin\Debug\lab0.exe -v -vl
```

本轮跑通后，你就可以安心删掉 `createVertexBuffer()` 里曾经保留的旧 staging 代码块。
长期来看，旧代码留太多会让你读代码时分心。

## 13. 下一轮可以做什么

这轮完成后，建议下一轮做：

```text
把 uniform buffer 也迁移到 VMA AllocatedBuffer
```

因为你当前 mesh buffer 已经由 VMA 管理，但 uniform buffer 还在手动：

```cpp
vkCreateBuffer
vkGetBufferMemoryRequirements
vkAllocateMemory
vkBindBufferMemory
vkMapMemory
```

把 uniform buffer 也迁移到 VMA 后，你会得到一个更统一的资源系统：

```text
vertex/index buffer -> VMA
uniform buffer      -> VMA
future image        -> VMA
```

再之后就可以进入更像 Games202 实验室的内容：

```text
push constants
camera uniform
texture upload
offscreen framebuffer / dynamic rendering target
shadow map
deferred shading
SSAO / SSR / IBL
```

但现在先把 helper 从 Lab0 拆出去。
这一步虽然不像画新效果那么刺激，却是在给后面的所有实验铺轨道。

