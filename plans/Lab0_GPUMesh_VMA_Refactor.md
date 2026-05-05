# Lab0 第二轮重构：抽离 GPU Mesh Buffer 并接入 VMA

这份文档带你完成 `Lab0` 的第二轮重构。

你现在已经做了两件关键事情：

- 在 `base/vk_types.h` 里定义了 `AllocatedBuffer` 和 `GPUMeshBuffers`
- 在 `Lab0` 里生成了圆盘网格，并且已经有了 `MeshData`

这一轮的目标是把下面这三个散落字段：

```cpp
VulkanBuffer vertexBuffer;
VulkanBuffer indexBuffer;
uint32_t indexCount{ 0 };
```

替换成一个明确的 GPU mesh 对象：

```cpp
GPUMeshBuffers circleMeshBuffers;
```

同时让圆盘的 vertex/index/staging buffer 走 VMA，不再手动写：

```cpp
vkGetBufferMemoryRequirements
getMemoryTypeIndex
vkAllocateMemory
vkBindBufferMemory
```

## 1. 先明确这轮重构的边界

这一轮只改：

- 圆盘 mesh 的 vertex buffer
- 圆盘 mesh 的 index buffer
- 上传时用到的 staging buffer
- draw 时绑定 mesh buffer 的代码

暂时不改：

- uniform buffer
- descriptor
- pipeline
- depth image
- swapchain
- command buffer 结构

这样改动范围会很清楚。你这轮只是在回答一个问题：

```text
一个 mesh 放到 GPU 上以后，应该如何被表达、创建、销毁和绘制？
```

## 2. 你现在的三个数据层

接下来要刻意区分三层数据。

### 2.1 CPU 侧 mesh 数据

这个已经在 `lab0.h` 里了：

```cpp
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
```

它只表示 CPU 内存里的几何数据。

特点：

- 可以用 `std::vector` 管
- 可以随便生成、修改、打印
- 不直接参与 `vkCmdDrawIndexed`
- 不需要 Vulkan 内存管理

### 2.2 GPU 侧 buffer

这个在 `vk_types.h` 里：

```cpp
struct AllocatedBuffer {
    VkBuffer handle{ VK_NULL_HANDLE };
    VmaAllocation allocation{ VK_NULL_HANDLE };
    VmaAllocationInfo allocationInfo{};
    VkDeviceSize size{ 0 };
};
```

它表示一块由 VMA 分配和管理的 Vulkan buffer。

特点：

- `handle` 给 Vulkan 命令使用
- `allocation` 给 VMA 销毁和管理内存使用
- 不再有 `VkDeviceMemory`
- 销毁时用 `vmaDestroyBuffer`

### 2.3 GPU 侧 mesh

这个也在 `vk_types.h` 里：

```cpp
struct GPUMeshBuffers {
    AllocatedBuffer vertexBuffer;
    AllocatedBuffer indexBuffer;
    uint32_t indexCount{ 0 };
    VkIndexType indexType{ VK_INDEX_TYPE_UINT32 };
};
```

它表示“一个可以被 draw 的 mesh”。

特点：

- 它不关心顶点格式
- 它不关心圆盘、立方体还是模型
- 它只关心 draw indexed 所需的 GPU 资源

这三个层次以后会一直用到：

```text
MeshData        = CPU 侧几何数据
AllocatedBuffer = GPU 侧 buffer 资源
GPUMeshBuffers  = GPU 侧可绘制 mesh
```

## 3. 第一步：让 Lab0 正确 include vk_types

你的 `lab0.h` 里已经用了：

```cpp
GPUMeshBuffers circleMeshBuffers;
```

所以要确保它能看到 `vk_types.h`。

在 `lab0.h` 里加：

```cpp
#include "vk_types.h"
```

建议放在：

```cpp
#include "vulkanexamplebase.h"
#include "vk_initializers.h"
#include "vk_types.h"
```

如果不加，编译器可能会报 `GPUMeshBuffers` 未定义。

## 4. 第二步：在 Lab0 里加入 VmaAllocator

VMA 需要一个 allocator 对象。

建议先把它放在 `VulkanExample` 里，等你后面封装稳定了，再考虑移动到 `VulkanExampleBase` 或 `VulkanDevice`。

在 `lab0.h` 的成员里加：

```cpp
VmaAllocator allocator{ VK_NULL_HANDLE };
```

放在 `enabledFeatures` 附近就可以。

你还可以加两个小函数声明：

```cpp
void createVmaAllocator();
void destroyVmaAllocator();
```

这不是必须，但对阅读很友好。

## 5. 第三步：创建和销毁 VmaAllocator

在 `lab0.cpp` 里实现：

```cpp
void VulkanExample::createVmaAllocator()
{
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;

    VK_CHECK_RESULT(vmaCreateAllocator(&allocatorInfo, &allocator));
}
```

销毁：

```cpp
void VulkanExample::destroyVmaAllocator()
{
    if (allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }
}
```

调用位置建议这样：

```cpp
void VulkanExample::prepare() {
    VulkanExampleBase::prepare();
    createVmaAllocator();
    createSynchronizationPrimitives();
    createCommandBuffers();
    createVertexBuffer();
    createUniformBuffers();
    createDescriptors();
    createPipeline();
    prepared = true;
}
```

注意：`createVmaAllocator()` 要放在 `VulkanExampleBase::prepare()` 后面。

原因是 base prepare 之后，`physicalDevice`、`device`、`instance`、swapchain 等 Vulkan 基础对象才已经准备好。

析构函数里，VMA allocator 要在 VMA 创建的资源销毁之后再销毁：

```cpp
vmaDestroyBuffer(allocator, circleMeshBuffers.vertexBuffer.handle, circleMeshBuffers.vertexBuffer.allocation);
vmaDestroyBuffer(allocator, circleMeshBuffers.indexBuffer.handle, circleMeshBuffers.indexBuffer.allocation);
destroyVmaAllocator();
```

顺序很重要：

```text
先销毁 VMA 分配出来的 buffer
再销毁 VmaAllocator
最后基类析构继续销毁 VkDevice 等对象
```

## 6. 第四步：删除旧的 mesh buffer 成员

现在 `lab0.h` 里还有：

```cpp
VulkanBuffer vertexBuffer;
VulkanBuffer indexBuffer;
uint32_t indexCount{ 0 };
GPUMeshBuffers circleMeshBuffers;
```

这一轮结束时应该变成：

```cpp
GPUMeshBuffers circleMeshBuffers;
```

`VulkanBuffer` 暂时还不能删，因为 `UniformBuffer : VulkanBuffer` 还在用它。

所以先只删成员：

```cpp
VulkanBuffer vertexBuffer;
VulkanBuffer indexBuffer;
uint32_t indexCount{ 0 };
```

保留：

```cpp
struct VulkanBuffer { ... };
struct UniformBuffer : VulkanBuffer { ... };
```

这表示：本轮 VMA 只接管 mesh buffer，uniform buffer 仍然走旧路线。

## 7. 第五步：改析构函数

原来析构里有：

```cpp
vkDestroyBuffer(device, vertexBuffer.handle, nullptr);
vkFreeMemory(device, vertexBuffer.memory, nullptr);
vkDestroyBuffer(device, indexBuffer.handle, nullptr);
vkFreeMemory(device, indexBuffer.memory, nullptr);
```

改成：

```cpp
if (circleMeshBuffers.vertexBuffer.handle != VK_NULL_HANDLE) {
    vmaDestroyBuffer(
        allocator,
        circleMeshBuffers.vertexBuffer.handle,
        circleMeshBuffers.vertexBuffer.allocation);
}

if (circleMeshBuffers.indexBuffer.handle != VK_NULL_HANDLE) {
    vmaDestroyBuffer(
        allocator,
        circleMeshBuffers.indexBuffer.handle,
        circleMeshBuffers.indexBuffer.allocation);
}
```

然后在这两个 buffer 销毁之后调用：

```cpp
destroyVmaAllocator();
```

注意：不要对 VMA buffer 调用 `vkFreeMemory`。

VMA 的规则是：

```text
vmaCreateBuffer  -> vmaDestroyBuffer
vmaCreateImage   -> vmaDestroyImage
```

不要混用：

```text
vmaCreateBuffer  -> vkDestroyBuffer + vkFreeMemory
```

## 8. 第六步：先写一个 createAllocatedBuffer helper

这一轮不要把所有 helper 都抽大。

先在 `VulkanExample` 里写一个小函数：

```cpp
AllocatedBuffer createAllocatedBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaAllocationCreateFlags allocationFlags,
    VmaMemoryUsage memoryUsage);
```

声明放在 `lab0.h`：

```cpp
AllocatedBuffer createAllocatedBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaAllocationCreateFlags allocationFlags,
    VmaMemoryUsage memoryUsage);
```

实现放在 `lab0.cpp`：

```cpp
AllocatedBuffer VulkanExample::createAllocatedBuffer(
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
```

第一版先放在 `Lab0` 类里。

等你用顺了，再移动到 `vk_resources.h/.cpp`。

## 9. 第七步：改 createVertexBuffer 的 staging buffer

原来你手写了 staging buffer：

```cpp
VulkanBuffer stagingBuffer;
vkCreateBuffer(...)
vkGetBufferMemoryRequirements(...)
vkAllocateMemory(...)
vkBindBufferMemory(...)
vkMapMemory(...)
```

改成 VMA：

```cpp
AllocatedBuffer stagingBuffer = createAllocatedBuffer(
    vertexBufferSize + indexBufferSize,
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT,
    VMA_MEMORY_USAGE_AUTO);
```

然后写入数据：

```cpp
uint8_t* data = static_cast<uint8_t*>(stagingBuffer.allocationInfo.pMappedData);
memcpy(data, vertices.data(), vertexBufferSize);
memcpy(data + vertexBufferSize, indices.data(), indexBufferSize);
```

这里不再需要：

```cpp
vkMapMemory
vkUnmapMemory
```

原因是你创建 staging buffer 时用了：

```cpp
VMA_ALLOCATION_CREATE_MAPPED_BIT
```

VMA 会把映射指针填到：

```cpp
stagingBuffer.allocationInfo.pMappedData
```

## 10. 第八步：改 vertex/index buffer 创建

原来的 vertex buffer 创建：

```cpp
vkCreateBuffer(...)
vkGetBufferMemoryRequirements(...)
vkAllocateMemory(...)
vkBindBufferMemory(...)
```

改成：

```cpp
circleMeshBuffers.vertexBuffer = createAllocatedBuffer(
    vertexBufferSize,
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    0,
    VMA_MEMORY_USAGE_AUTO);
```

index buffer：

```cpp
circleMeshBuffers.indexBuffer = createAllocatedBuffer(
    indexBufferSize,
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    0,
    VMA_MEMORY_USAGE_AUTO);
```

同时设置 draw 参数：

```cpp
circleMeshBuffers.indexCount = static_cast<uint32_t>(indices.size());
circleMeshBuffers.indexType = VK_INDEX_TYPE_UINT32;
```

这一步完成后，`createVertexBuffer()` 里就不应该再给 mesh buffer 调：

```cpp
getMemoryTypeIndex
vkAllocateMemory
vkBindBufferMemory
```

## 11. 第九步：改 vkCmdCopyBuffer 的目标

原来：

```cpp
vkCmdCopyBuffer(copyCmd, stagingBuffer.handle, vertexBuffer.handle, 1, &copyRegion);
...
vkCmdCopyBuffer(copyCmd, stagingBuffer.handle, indexBuffer.handle, 1, &copyRegion);
```

改成：

```cpp
vkCmdCopyBuffer(
    copyCmd,
    stagingBuffer.handle,
    circleMeshBuffers.vertexBuffer.handle,
    1,
    &copyRegion);

copyRegion.size = indexBufferSize;
copyRegion.srcOffset = vertexBufferSize;
copyRegion.dstOffset = 0;

vkCmdCopyBuffer(
    copyCmd,
    stagingBuffer.handle,
    circleMeshBuffers.indexBuffer.handle,
    1,
    &copyRegion);
```

这里顺手把 `dstOffset` 显式设成 `0`。

因为同一个 `copyRegion` 被复用，显式写出来更不容易在后续改代码时踩坑。

## 12. 第十步：销毁 staging buffer

原来：

```cpp
vkDestroyBuffer(device, stagingBuffer.handle, nullptr);
vkFreeMemory(device, stagingBuffer.memory, nullptr);
```

改成：

```cpp
vmaDestroyBuffer(allocator, stagingBuffer.handle, stagingBuffer.allocation);
```

staging buffer 是临时资源，copy 完并等待 fence 后就可以销毁。

## 13. 第十一步：改 render 里的 bind/draw

原来：

```cpp
VkDeviceSize offsets[1]{ 0 };
vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer.handle, offsets);
vkCmdBindIndexBuffer(commandBuffer, indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
```

改成：

```cpp
VkDeviceSize offsets[1]{ 0 };
VkBuffer vertexBuffer = circleMeshBuffers.vertexBuffer.handle;

vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
vkCmdBindIndexBuffer(
    commandBuffer,
    circleMeshBuffers.indexBuffer.handle,
    0,
    circleMeshBuffers.indexType);
vkCmdDrawIndexed(
    commandBuffer,
    circleMeshBuffers.indexCount,
    1,
    0,
    0,
    0);
```

这一步是这轮重构的阅读收益所在：

```text
render() 只关心“绑定哪个 mesh”
不关心 mesh 的内存是 vkAllocateMemory 还是 VMA
```

## 14. 第十二步：检查旧变量是否清干净

改完后跑：

```powershell
rg -n "vertexBuffer|indexBuffer|indexCount|VulkanBuffer" examples/Lab0/lab0.h examples/Lab0/lab0.cpp
```

你应该还能看到：

- `UniformBuffer : VulkanBuffer`
- `circleMeshBuffers.vertexBuffer`
- `circleMeshBuffers.indexBuffer`
- `circleMeshBuffers.indexCount`

但不应该再看到旧成员：

```cpp
VulkanBuffer vertexBuffer;
VulkanBuffer indexBuffer;
uint32_t indexCount;
```

也不应该在 mesh buffer 创建部分看到：

```cpp
vkAllocateMemory
vkBindBufferMemory
getMemoryTypeIndex
```

注意：uniform buffer 还没改 VMA，所以 `createUniformBuffers()` 里仍然会有这些旧 API，这是正常的。

## 15. 第十三步：构建验证

先只构建 `lab0`：

```powershell
cmake --build build --config Debug --target lab0 -j 32
```

如果构建失败，优先看这几类错误。

### 15.1 找不到 `GPUMeshBuffers`

通常是：

```cpp
#include "vk_types.h"
```

没加到 `lab0.h`。

### 15.2 找不到 `VmaAllocation`

通常是：

```cpp
#include <vk_mem_alloc.h>
```

没在 `vk_types.h` 里，或者 `base` target 没有 include `external/vma`。

你现在已经在 `base/CMakeLists.txt` 里加了：

```cmake
target_include_directories(base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../external/vma)
```

所以这块应该是好的。

### 15.3 `vmaCreateBuffer` 链接错误

检查是否只有一个 `.cpp` 里定义了：

```cpp
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
```

你现在应该是：

```cpp
base/vma_impl.cpp
```

这里不要在别的文件里再定义一次 `VMA_IMPLEMENTATION`。

### 15.4 程序运行时崩在析构

优先检查销毁顺序：

```text
vmaDestroyBuffer(mesh buffer)
-> vmaDestroyAllocator
-> 基类析构销毁 device
```

不要在 `vmaDestroyAllocator` 之后再销毁 VMA buffer。

## 16. 这轮重构完成后的代码形态

完成后，你的 `Lab0` 应该是这样：

```text
MeshData
  CPU 侧圆盘 vertices / indices

GPUMeshBuffers circleMeshBuffers
  GPU 侧 vertex/index buffer
  draw indexed 所需 indexCount / indexType

VmaAllocator allocator
  负责 mesh buffer 和 staging buffer 的内存分配
```

`render()` 应该只出现这个语义：

```text
绑定 circleMeshBuffers
draw indexed
```

`createVertexBuffer()` 则负责：

```text
生成 MeshData
创建 staging buffer
创建 GPU vertex/index buffer
copy staging -> GPU buffers
销毁 staging buffer
```

## 17. 这轮之后下一步做什么

这轮完成后，下一步最值得做的是抽：

```cpp
AllocatedBuffer createDeviceLocalBuffer(
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage);
```

那时 `createVertexBuffer()` 会进一步变短：

```cpp
circleMeshBuffers.vertexBuffer = createDeviceLocalBuffer(
    vertices.data(),
    vertexBufferSize,
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

circleMeshBuffers.indexBuffer = createDeviceLocalBuffer(
    indices.data(),
    indexBufferSize,
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
```

但这一步先别急。

当前这一轮先做到：

```text
mesh buffer 归组到 GPUMeshBuffers
mesh buffer 分配切到 VMA
render() 只通过 circleMeshBuffers 绘制
```

这就已经是一个很扎实的进阶节点了。
