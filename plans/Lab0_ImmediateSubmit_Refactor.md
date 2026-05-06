# Lab0 第四轮重构：抽出 immediateSubmit 与资源销毁 helper

这一轮接在 `createDeviceLocalBuffer()` 之后。

你现在已经把 `createVertexBuffer()` 整理得很清楚了：

```text
createCircleMesh()
-> createDeviceLocalBuffer(vertices)
-> createDeviceLocalBuffer(indices)
-> 设置 circleMeshBuffers.indexCount / indexType
```

这是很好的状态。现在新的重复点转移到了 `createDeviceLocalBuffer()` 内部：

```text
分配临时 command buffer
begin command buffer
录制 vkCmdCopyBuffer
end command buffer
创建 fence
queue submit
wait fence
销毁 fence
释放 command buffer
```

这段代码以后不只 buffer upload 会用。

后面你做 texture、image layout transition、mipmap、offscreen 初始化时，也会不断需要：

```text
开一个临时 command buffer
录几条一次性命令
提交并等待完成
```

所以这一轮目标是抽出：

```cpp
void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);
```

顺手再抽一个：

```cpp
void destroyAllocatedBuffer(AllocatedBuffer& buffer);
```

## 1. 这一轮的目标

完成后，`createDeviceLocalBuffer()` 应该接近这样：

```cpp
AllocatedBuffer VulkanExample::createDeviceLocalBuffer(
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage)
{
    AllocatedBuffer stagingBuffer = createAllocatedBuffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT,
        VMA_MEMORY_USAGE_AUTO);

    void* mappedData = stagingBuffer.allocationInfo.pMappedData;
    memcpy(mappedData, data, static_cast<size_t>(size));
    VK_CHECK_RESULT(vmaFlushAllocation(allocator, stagingBuffer.allocation, 0, size));

    AllocatedBuffer deviceBuffer = createAllocatedBuffer(
        size,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        0,
        VMA_MEMORY_USAGE_AUTO);

    immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, stagingBuffer.handle, deviceBuffer.handle, 1, &copyRegion);
    });

    destroyAllocatedBuffer(stagingBuffer);
    return deviceBuffer;
}
```

这段代码的语义就非常清楚：

```text
创建 staging
写 CPU 数据
创建 GPU buffer
提交一次 copy
销毁 staging
返回 GPU buffer
```

## 2. 第一步：在 lab0.h 声明 immediateSubmit

在 `lab0.h` 里先 include：

```cpp
#include <functional>
```

你现在 `vk_types.h` 已经 include 了 `<functional>`，但 `immediateSubmit` 是 `Lab0` 自己的函数签名，建议 `lab0.h` 自己也明确 include 一下，减少隐式依赖。

然后在 helper 声明区加：

```cpp
void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);
```

建议放在：

```cpp
AllocatedBuffer createDeviceLocalBuffer(...);
```

附近。

## 3. 第二步：实现 immediateSubmit

把下面函数加到 `lab0.cpp`，建议放在 `createDeviceLocalBuffer()` 前面。

```cpp
void VulkanExample::immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
    VkCommandBuffer copyCmd;
    VkCommandBufferAllocateInfo cmdCI{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdCI.commandPool = commandPool;
    cmdCI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdCI.commandBufferCount = 1;
    VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdCI, &copyCmd));

    VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
    cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));

    function(copyCmd);

    VK_CHECK_RESULT(vkEndCommandBuffer(copyCmd));

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &copyCmd;

    VkFenceCreateInfo fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    VK_CHECK_RESULT(vkCreateFence(device, &fenceCI, nullptr, &fence));
    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
    VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, commandPool, 1, &copyCmd);
}
```

这个函数的意义是：

```text
调用者只告诉我“要录什么命令”
我负责 command buffer 的申请、开始、结束、提交、等待和释放
```

这就是 `vk-guide` 里很常见的思路。

## 4. 第三步：用 immediateSubmit 改 createDeviceLocalBuffer

在 `createDeviceLocalBuffer()` 里删掉这一整段：

```text
VkCommandBuffer copyCmd;
vkAllocateCommandBuffers
vkBeginCommandBuffer
vkCmdCopyBuffer
vkEndCommandBuffer
VkSubmitInfo
vkCreateFence
vkQueueSubmit
vkWaitForFences
vkDestroyFence
vkFreeCommandBuffers
```

换成：

```cpp
immediateSubmit([&](VkCommandBuffer cmd) {
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, stagingBuffer.handle, deviceBuffer.handle, 1, &copyRegion);
});
```

注意这里用 `[&]` 捕获。

因为 lambda 里要访问：

```cpp
size
stagingBuffer
deviceBuffer
```

`immediateSubmit()` 是同步等待完成后才返回，所以这里引用捕获是安全的。

## 5. 第四步：抽 destroyAllocatedBuffer

你现在有几处类似：

```cpp
vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
```

建议抽成：

```cpp
void destroyAllocatedBuffer(AllocatedBuffer& buffer);
```

在 `lab0.h` 声明：

```cpp
void destroyAllocatedBuffer(AllocatedBuffer& buffer);
```

在 `lab0.cpp` 实现：

```cpp
void VulkanExample::destroyAllocatedBuffer(AllocatedBuffer& buffer)
{
    if (buffer.handle != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
        buffer.handle = VK_NULL_HANDLE;
        buffer.allocation = VK_NULL_HANDLE;
        buffer.allocationInfo = {};
        buffer.size = 0;
    }
}
```

然后把析构函数里的：

```cpp
if (circleMeshBuffers.vertexBuffer.handle != VK_NULL_HANDLE) {
    vmaDestroyBuffer(...);
}

if (circleMeshBuffers.indexBuffer.handle != VK_NULL_HANDLE) {
    vmaDestroyBuffer(...);
}
```

改成：

```cpp
destroyAllocatedBuffer(circleMeshBuffers.vertexBuffer);
destroyAllocatedBuffer(circleMeshBuffers.indexBuffer);
```

把 `createDeviceLocalBuffer()` 里的：

```cpp
vmaDestroyBuffer(allocator, stagingBuffer.handle, stagingBuffer.allocation);
```

改成：

```cpp
destroyAllocatedBuffer(stagingBuffer);
```

这样你的资源销毁语义会统一很多。

## 6. 第五步：搜索检查

改完后跑：

```powershell
rg -n "vkAllocateCommandBuffers|vkBeginCommandBuffer|vkQueueSubmit|vkWaitForFences|vkFreeCommandBuffers" examples/Lab0/lab0.cpp
```

你应该看到：

- 每帧 render 提交相关代码仍然在 `render()`
- 一次性提交相关代码集中在 `immediateSubmit()`
- `createDeviceLocalBuffer()` 不再直接出现 `vkQueueSubmit`

再跑：

```powershell
rg -n "vmaDestroyBuffer" examples/Lab0/lab0.cpp
```

理想状态：

- 只在 `destroyAllocatedBuffer()` 里出现

如果别的地方还有直接 `vmaDestroyBuffer`，也不一定错，但说明销毁封装还没完全统一。

## 7. 第六步：构建验证

先构建：

```powershell
cmake --build build --config Debug --target lab0 -j 32
```

如果报错，优先查：

```text
std::function 没 include
immediateSubmit 声明和实现签名不一致
lambda 捕获写错
destroyAllocatedBuffer 声明和实现签名不一致
```

## 8. 第七步：运行验证

正常运行一次。

如果你想确认 `render()` 路径至少跑了多帧，可以用：

```powershell
build\bin\Debug\lab0.exe -b -bfs 10 -bf lab0_benchmark.csv -v -vl
```

跑完后删除验证产物：

```text
lab0_benchmark.csv
validation_output.txt
```

## 9. 完成后的代码结构

完成后，你的层次会更清楚：

```text
createCircleMesh()
  CPU 几何生成

createAllocatedBuffer()
  VMA buffer 创建

immediateSubmit()
  一次性 command buffer 提交

createDeviceLocalBuffer()
  CPU 数据上传到 GPU buffer

createVertexBuffer()
  把圆盘 mesh 上传成 GPUMeshBuffers

render()
  绑定 GPUMeshBuffers 并 draw
```

这一步非常关键，因为后面做 texture 时，你会复用同一个 `immediateSubmit()`：

```cpp
immediateSubmit([&](VkCommandBuffer cmd) {
    // transition image layout
    // copy buffer to image
    // transition image layout
});
```

也就是说，这不是为了“代码好看”而抽。

它会直接服务后面的 Vulkan 图形实验。

## 10. 这轮之后建议做什么

完成这轮后，我建议你先不要动 pipeline。

下一步可以二选一：

```text
路线 A：把 uniform buffer 也切到 VMA
路线 B：给 Lab0 加自己的 HLSL shader，不再复用 triangle shader
```

我更推荐先走路线 B。

原因：

- 你的 mesh buffer 已经 VMA 化了
- uniform buffer 旧实现暂时还能工作
- 但 shader 还在复用 `triangle/triangle.vert.spv`
- 拥有自己的 shader 目录，会让 `Lab0` 变成真正独立的 sample

所以这一轮完成后，比较自然的下一步是：

```text
shaders/hlsl/lab0/lab0.vert
shaders/hlsl/lab0/lab0.frag
```

然后在 pipeline 里加载：

```cpp
getShadersPath() + "lab0/lab0.vert.spv"
getShadersPath() + "lab0/lab0.frag.spv"
```

这会把你的 sample 从“结构独立”推进到“shader 也独立”。
