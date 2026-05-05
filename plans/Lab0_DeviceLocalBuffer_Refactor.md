# Lab0 第三轮重构：抽出 createDeviceLocalBuffer

这一轮接在 `GPUMeshBuffers + VMA` 重构之后。

你现在已经做到：

```text
MeshData                 = CPU 侧圆盘几何数据
GPUMeshBuffers           = GPU 侧可绘制 mesh
AllocatedBuffer + VMA    = GPU buffer 分配方式
```

但目前 `createVertexBuffer()` 里仍然混着很多职责：

```text
生成 MeshData
计算 buffer size
创建 staging buffer
memcpy 数据
flush staging allocation
创建 device local vertex/index buffer
分配临时 command buffer
录 vkCmdCopyBuffer
提交 queue 并等待 fence
销毁 staging buffer
设置 circleMeshBuffers.indexCount
```

这一轮的目标是抽出：

```cpp
AllocatedBuffer createDeviceLocalBuffer(
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage);
```

让 `createVertexBuffer()` 变成更接近图形项目里的“上传 mesh”逻辑。

## 1. 这一轮重构要达成什么

重构完成后，`createVertexBuffer()` 应该接近这样：

```cpp
void VulkanExample::createVertexBuffer()
{
    MeshData circle = createCircleMesh(0.8f, 64);

    circleMeshBuffers.vertexBuffer = createDeviceLocalBuffer(
        circle.vertices.data(),
        circle.vertices.size() * sizeof(Vertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    circleMeshBuffers.indexBuffer = createDeviceLocalBuffer(
        circle.indices.data(),
        circle.indices.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    circleMeshBuffers.indexCount = static_cast<uint32_t>(circle.indices.size());
    circleMeshBuffers.indexType = VK_INDEX_TYPE_UINT32;
}
```

这段代码表达的是：

```text
生成圆盘 CPU mesh
上传 vertices
上传 indices
记录 draw indexed 所需信息
```

这就是你真正想在 `createVertexBuffer()` 里看到的主线。

## 2. 先不要急着抽 immediateSubmit

你可能已经注意到，`createDeviceLocalBuffer()` 内部会包含一大段：

```text
分配临时 command buffer
begin command buffer
vkCmdCopyBuffer
end command buffer
queue submit
wait fence
free command buffer
```

这段以后会继续抽成：

```cpp
void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);
```

但这一轮建议先不抽。

原因是：你现在最重要的是先得到一个稳定的“CPU 数据 -> GPU device local buffer”函数。等这个函数跑通后，再抽 `immediateSubmit()` 会更自然。

所以本轮只做一个 helper：

```cpp
createDeviceLocalBuffer()
```

## 3. 第一步：在 lab0.h 里声明 helper

在 `VulkanExample` 里找到你现在的 helper：

```cpp
AllocatedBuffer createAllocatedBuffer(
    size_t allocSize,
    VkBufferUsageFlags usage,
    VmaAllocationCreateFlags allocationFlags,
    VmaMemoryUsage memoryUsage);
```

在它下面加：

```cpp
AllocatedBuffer createDeviceLocalBuffer(
    const void* data,
    VkDeviceSize size,
    VkBufferUsageFlags usage);
```

建议这一阶段先放在 `VulkanExample` 类里。

等你后面把接口用顺，再移动到：

```text
base/vk_resources.h
base/vk_resources.cpp
```

## 4. 第二步：实现 createDeviceLocalBuffer

先把完整版本写在 `lab0.cpp` 里，建议放在 `createAllocatedBuffer()` 之前或之后。

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
    VK_CHECK_RESULT(vmaFlushAllocation(allocator, stagingBuffer.allocation, 0, VK_WHOLE_SIZE));

    AllocatedBuffer deviceBuffer = createAllocatedBuffer(
        size,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        0,
        VMA_MEMORY_USAGE_AUTO);

    VkCommandBuffer copyCmd;
    VkCommandBufferAllocateInfo cmdBufAllocateInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdBufAllocateInfo.commandPool = commandPool;
    cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocateInfo.commandBufferCount = 1;
    VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &copyCmd));

    VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
    VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));
    {
        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(copyCmd, stagingBuffer.handle, deviceBuffer.handle, 1, &copyRegion);
    }
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

    vmaDestroyBuffer(allocator, stagingBuffer.handle, stagingBuffer.allocation);

    return deviceBuffer;
}
```

这段代码本质上就是把你当前 `createVertexBuffer()` 里的 staging 上传流程封进一个函数。

## 5. 这个 helper 的职责边界

`createDeviceLocalBuffer()` 做这些事：

```text
接收一段 CPU 内存
创建 CPU 可写 staging buffer
把 CPU 数据拷进 staging buffer
创建 GPU 用 device buffer
通过 vkCmdCopyBuffer 上传
等待上传完成
销毁 staging buffer
返回 GPU buffer
```

它不做这些事：

```text
不知道 Vertex 是什么
不知道 MeshData 是什么
不知道 indexCount 是多少
不知道 draw indexed 怎么画
不知道 descriptor / pipeline / shader
```

这个边界非常重要。

它以后可以用于：

- vertex buffer
- index buffer
- storage buffer
- compute 初始数据

只要是“CPU 数据上传到 GPU buffer”，都可以走这条路。

## 6. 第三步：简化 createVertexBuffer

把 `createVertexBuffer()` 里从 staging buffer 创建开始，到 staging buffer 销毁结束的整块代码移除。

然后改成：

```cpp
void VulkanExample::createVertexBuffer()
{
    MeshData circle = createCircleMesh(0.8f, 64);

    const VkDeviceSize vertexBufferSize =
        circle.vertices.size() * sizeof(Vertex);
    const VkDeviceSize indexBufferSize =
        circle.indices.size() * sizeof(uint32_t);

    circleMeshBuffers.vertexBuffer = createDeviceLocalBuffer(
        circle.vertices.data(),
        vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    circleMeshBuffers.indexBuffer = createDeviceLocalBuffer(
        circle.indices.data(),
        indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    circleMeshBuffers.indexCount = static_cast<uint32_t>(circle.indices.size());
    circleMeshBuffers.indexType = VK_INDEX_TYPE_UINT32;
}
```

注意这里有一个小变化：

```cpp
usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT
```

被放到了 `createDeviceLocalBuffer()` 内部。

所以调用者只写：

```cpp
VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
VK_BUFFER_USAGE_INDEX_BUFFER_BIT
```

调用者不用记得加 `TRANSFER_DST`。

## 7. 第四步：构建前做一次搜索

改完后先搜索：

```powershell
rg -n "stagingBuffer|vkCmdCopyBuffer|vkQueueSubmit|vkWaitForFences|vmaFlushAllocation" examples/Lab0/lab0.cpp
```

你应该看到：

- 这些词主要出现在 `createDeviceLocalBuffer()`
- `createVertexBuffer()` 里不再有 staging/copy/fence 细节

如果 `createVertexBuffer()` 里仍然有大量 `vkCmdCopyBuffer` 或 `vmaDestroyBuffer(stagingBuffer...)`，说明还没拆干净。

## 8. 第五步：构建验证

先只构建 `lab0`：

```powershell
cmake --build build --config Debug --target lab0 -j 32
```

如果构建失败，优先查这些点。

### 8.1 `createDeviceLocalBuffer` 未声明

检查 `lab0.h` 是否加了声明。

### 8.2 `memcpy` 类型不匹配

`VmaAllocationInfo::pMappedData` 是 `void*`。

可以这样写：

```cpp
void* mappedData = stagingBuffer.allocationInfo.pMappedData;
memcpy(mappedData, data, static_cast<size_t>(size));
```

### 8.3 `usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT` 类型问题

`VkBufferUsageFlags` 本质是 bitmask，正常可以这样写：

```cpp
usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT
```

### 8.4 跑起来黑屏

重点检查：

```cpp
circleMeshBuffers.indexCount
circleMeshBuffers.indexBuffer.handle
circleMeshBuffers.vertexBuffer.handle
```

以及 `render()` 是否仍然绑定：

```cpp
circleMeshBuffers.vertexBuffer.handle
circleMeshBuffers.indexBuffer.handle
circleMeshBuffers.indexCount
```

不要回退到旧的 `vertexBuffer/indexBuffer/indexCount`。

## 9. 第六步：运行验证

建议先正常运行。

如果窗口行为不好判断，可以跑 benchmark 几帧：

```powershell
build\bin\Debug\lab0.exe -b -bfs 10 -bf lab0_benchmark.csv -v -vl
```

如果 benchmark 能生成结果，说明 `render()` 至少成功执行了多帧。

运行完可以删掉：

```text
lab0_benchmark.csv
validation_output.txt
```

这些只是验证产物。

## 10. 重构完成后的结构

完成后，你的核心结构会变成：

```text
createCircleMesh()
  只负责 CPU 侧几何数据

createDeviceLocalBuffer()
  只负责 CPU 数据上传到 GPU device local buffer

createVertexBuffer()
  只负责把 circle mesh 上传成 GPUMeshBuffers

render()
  只负责绑定 GPUMeshBuffers 并 draw indexed
```

这就是非常重要的一步：

```text
数据生成
资源上传
绘制命令
```

三者开始分开。

## 11. 下一轮重构预告：immediateSubmit

这一轮之后，`createDeviceLocalBuffer()` 里面仍然有一段重复潜力很高的代码：

```text
分配 command buffer
begin
record
end
submit
wait fence
free
```

下一轮可以把它抽成：

```cpp
void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);
```

那时 `createDeviceLocalBuffer()` 会进一步缩短成：

```cpp
immediateSubmit([&](VkCommandBuffer cmd) {
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, stagingBuffer.handle, deviceBuffer.handle, 1, &copyRegion);
});
```

但现在先别急。当前这一轮先把 `createDeviceLocalBuffer()` 跑稳。

## 12. 一个判断标准

改完后，打开 `createVertexBuffer()`。

如果它读起来像这样：

```text
生成圆
上传顶点
上传索引
设置 draw 参数
```

这轮就成功了。

如果它还读起来像这样：

```text
创建 staging
映射内存
拷贝内存
创建 fence
提交 command buffer
销毁 staging
```

那说明上传细节还没有真正被抽出去。
