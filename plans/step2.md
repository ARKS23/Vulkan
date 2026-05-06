# Step 2：把 UniformBuffer 迁移到 VMA AllocatedBuffer

你现在可以进入 Step 2。

这一阶段的目标不是做新效果，而是统一资源管理：

```text
vertex buffer  -> AllocatedBuffer + VMA
index buffer   -> AllocatedBuffer + VMA
uniform buffer -> 目前仍是 VkBuffer + VkDeviceMemory，下一步改成 AllocatedBuffer + VMA
```

完成后，Lab0 里所有 buffer 都会走同一套资源生命周期。
这会让你后面做 texture、offscreen、shadow map、G-buffer 时更舒服，因为资源创建和销毁会越来越统一。

## 1. 这一步你要理解什么

当前 `UniformBuffer` 的作用是：

```text
每个 in-flight frame 一份 ShaderData
CPU 每帧 memcpy 更新当前帧的 projection/view/model
vertex shader 通过 descriptor set 读取这份 UBO
```

现在的问题是，它的创建方式还停留在原始 Vulkan 手动内存管理：

```cpp
vkCreateBuffer
vkGetBufferMemoryRequirements
vkAllocateMemory
vkBindBufferMemory
vkMapMemory
```

这一轮要把它改成：

```cpp
vkutil::createAllocatedBuffer(...)
```

也就是：

```text
让 VMA 帮你创建 buffer、选择 memory type、分配 memory、绑定 memory、保存 mapped pointer
```

这一步非常适合学习 Vulkan，因为它会把你之前学到的两条线接起来：

```text
descriptor 数据流
-> shader 通过 descriptor 访问 buffer

VMA 资源流
-> buffer 的内存由 VMA 管理
```

## 2. 当前代码里哪些地方要改

你当前主要会动这几个位置：

```text
examples/Lab0/lab0.h
examples/Lab0/lab0.cpp
```

暂时不需要改：

```text
base/vk_types.h
base/vk_resources.h
base/vk_resources.cpp
```

因为 `AllocatedBuffer` 和 `vkutil::createAllocatedBuffer()` 已经足够完成这一步。

## 3. 第一步：修改 UniformBuffer 结构

当前 `lab0.h` 里大概是这样：

```cpp
struct VulkanBuffer {
    VkDeviceMemory memory{ VK_NULL_HANDLE };
    VkBuffer handle{ VK_NULL_HANDLE };
};

struct UniformBuffer : VulkanBuffer {
    VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
    uint8_t* mapped{ nullptr };
};
```

建议改成：

```cpp
struct UniformBuffer {
    AllocatedBuffer buffer;
    VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
    uint8_t* mapped{ nullptr };
};
```

然后可以删除旧的：

```cpp
struct VulkanBuffer
```

因为 Lab0 里不再需要手写 `VkBuffer + VkDeviceMemory` 这一套了。

这一步改完后，你的语义会变清楚：

```text
UniformBuffer 不是一种特殊 VulkanBuffer
UniformBuffer 是一个“带 descriptor set 和 mapped pointer 的 AllocatedBuffer”
```

这是更贴近你自己框架的表达。

## 4. 第二步：修改析构函数

当前析构函数里 uniform buffer 还是手动销毁：

```cpp
vkDestroyBuffer(device, uniformBuffers[i].handle, nullptr);
vkFreeMemory(device, uniformBuffers[i].memory, nullptr);
```

改成：

```cpp
vkutil::destroyAllocatedBuffer(allocator, uniformBuffers[i].buffer);
uniformBuffers[i].mapped = nullptr;
```

完整位置大概是在：

```cpp
for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
    vkDestroyFence(device, waitFences[i], nullptr);
    ...
}
```

建议改成类似：

```cpp
for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
    vkDestroyFence(device, waitFences[i], nullptr);
    vkutil::destroyAllocatedBuffer(allocator, uniformBuffers[i].buffer);
    uniformBuffers[i].mapped = nullptr;
}
```

注意销毁顺序：

```text
先 destroyAllocatedBuffer
再 destroyVmaAllocator
```

你现在的析构末尾已经是 `destroyVmaAllocator()`，保持这个顺序就好。

## 5. 第三步：重写 createUniformBuffers

当前函数大概是：

```cpp
void VulkanExample::createUniformBuffers() {
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = sizeof(ShaderData);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        vkCreateBuffer(...);
        vkGetBufferMemoryRequirements(...);
        vkAllocateMemory(...);
        vkBindBufferMemory(...);
        vkMapMemory(...);
    }
}
```

改成：

```cpp
void VulkanExample::createUniformBuffers() {
    for (uint32_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        uniformBuffers[i].buffer = vkutil::createAllocatedBuffer(
            allocator,
            sizeof(ShaderData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT,
            VMA_MEMORY_USAGE_AUTO);

        uniformBuffers[i].mapped =
            static_cast<uint8_t*>(uniformBuffers[i].buffer.allocationInfo.pMappedData);
    }
}
```

这里的关键参数是：

```cpp
VMA_ALLOCATION_CREATE_MAPPED_BIT
```

它告诉 VMA：

```text
创建后直接保持 mapped
```

所以后面每帧更新 UBO 时，你不需要再 `vkMapMemory()`。

另一个关键参数是：

```cpp
VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
```

它告诉 VMA：

```text
CPU 会顺序写入这块 buffer
```

这很适合 uniform buffer。

## 6. 第四步：修改 descriptor 写入

当前 descriptor 写入里应该是：

```cpp
VkDescriptorBufferInfo bufferInfo{};
bufferInfo.buffer = uniformBuffers[i].handle;
bufferInfo.range = sizeof(ShaderData);
```

改成：

```cpp
VkDescriptorBufferInfo bufferInfo{};
bufferInfo.buffer = uniformBuffers[i].buffer.handle;
bufferInfo.offset = 0;
bufferInfo.range = sizeof(ShaderData);
```

descriptor set 本身不关心 buffer 是怎么分配内存的。

它只需要知道：

```text
VkBuffer handle
offset
range
```

所以迁移到 VMA 后，descriptor 逻辑基本不用变，只是 buffer handle 的路径变了。

这点很重要：

```text
VMA 改变的是资源内存管理方式
不是 Vulkan descriptor 模型
```

## 7. 第五步：修改每帧 memcpy

当前 render 里应该是：

```cpp
memcpy(uniformBuffers[currentFrame].mapped, &shaderData, sizeof(ShaderData));
```

这句可以保持不变。

但建议你在后面加一个 flush：

```cpp
memcpy(uniformBuffers[currentFrame].mapped, &shaderData, sizeof(ShaderData));
VK_CHECK_RESULT(vmaFlushAllocation(
    allocator,
    uniformBuffers[currentFrame].buffer.allocation,
    0,
    sizeof(ShaderData)));
```

为什么建议保留 flush？

```text
有些内存不是 HOST_COHERENT
flush 能明确告诉 GPU：CPU 写入的数据需要可见
学习阶段保留 flush 更容易建立正确同步意识
```

你可能会问：如果 VMA 选到了 host coherent 内存，还需要 flush 吗？

实际运行中可能不需要，但保留它是安全的。
`vmaFlushAllocation()` 会根据内存类型处理细节。

## 8. 第六步：检查 prepare 顺序

当前顺序应该类似：

```cpp
createVmaAllocator();
createSynchronizationPrimitives();
createCommandBuffers();
createVertexBuffer();
createUniformBuffers();
createDescriptors();
createPipeline();
```

这个顺序可以保持。

关键是：

```text
createUniformBuffers() 必须在 createVmaAllocator() 之后
createDescriptors() 必须在 createUniformBuffers() 之后
```

因为 descriptor 写入时需要拿到有效的 uniform buffer handle。

## 9. 第七步：清理 getMemoryTypeIndex 的使用

这一步完成后，Lab0 里 `getMemoryTypeIndex()` 可能只剩 depth stencil 在使用：

```cpp
setupDepthStencil()
```

这很正常。

暂时不要急着删 `getMemoryTypeIndex()`，因为 depth image 还没迁移到 VMA。

等你后面进入 image helper 阶段，再把 depth stencil 也改成 VMA image。
那时候 `getMemoryTypeIndex()` 才可能从 Lab0 中消失。

## 10. 编译前检查清单

改完后先跑：

```powershell
rg -n "uniformBuffers\\[[^\\]]+\\]\\.(handle|memory)" examples/Lab0
```

理想结果：

```text
没有结果
```

因为 `UniformBuffer` 不应该再直接有：

```cpp
handle
memory
```

再检查：

```powershell
rg -n "vkCreateBuffer|vkAllocateMemory|vkBindBufferMemory|vkMapMemory|vkFreeMemory" examples/Lab0/lab0.cpp
```

预期：

```text
vkCreateBuffer / vkAllocateMemory / vkBindBufferMemory / vkMapMemory
-> 不应该再出现在 createUniformBuffers 里

vkFreeMemory
-> 不应该再用于 uniform buffer

如果 setupDepthStencil 里还有 vkAllocateMemory / vkBindImageMemory
-> 暂时可以保留
```

## 11. 编译和运行

编译：

```powershell
cmake --build build --config Debug --target lab0 -j 32
```

运行 validation：

```powershell
build\bin\Debug\lab0.exe -v -vl
```

成功标准：

```text
画面和之前一样
validation layer 没有新增错误
关闭程序不报资源销毁错误
```

这一步如果黑屏，优先检查：

```text
descriptor bufferInfo.buffer 是否改成 uniformBuffers[i].buffer.handle
uniformBuffers[i].mapped 是否来自 allocationInfo.pMappedData
createUniformBuffers() 是否在 createDescriptors() 前
allocator 是否在 uniform buffer 销毁后才 destroy
```

## 12. 这一步完成后你应该获得的理解

完成 Step 2 后，你应该能很清楚地描述这条数据流：

```text
VMA 创建 host visible uniform buffer
-> VMA 返回 mapped pointer
-> CPU 每帧 memcpy ShaderData
-> flush allocation
-> descriptor set 指向 VkBuffer handle
-> vertex shader 读取 projection/view/model
-> 当前帧 command buffer draw 使用对应 descriptor set
```

这条线非常重要。

后面你做：

```text
camera buffer
material buffer
light buffer
shadow matrix buffer
SSAO kernel buffer
PBR parameter buffer
```

本质上都会复用这个理解。

## 13. Step 2 完成后下一步做什么

完成后建议不要马上冲 texture。

先做一个小增强：

```text
新增 push constant
```

例如：

```text
用 push constant 控制圆盘颜色强度
或控制一个 per-draw model matrix
```

原因是：

```text
UniformBuffer 适合 per-frame / per-camera 数据
PushConstant 适合很小的 per-draw 数据
```

这两个概念组合起来，你后面画多个 mesh 时会很自然：

```text
camera/view/projection -> UBO
每个物体的 model/color/material id -> push constant
```

这是从“单物体 sample”走向“真正场景渲染”的第一步。

