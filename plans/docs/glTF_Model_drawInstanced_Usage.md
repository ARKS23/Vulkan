# glTF Model::drawInstanced 使用说明

这份文档说明项目中新增的 `vkglTF::Model::drawInstanced()` 如何使用，以及它在 `VulkanglTFModel` 内部是怎样实现实例化渲染的。

## 1. 为什么需要 drawInstanced

原来的 `vkglTF::Model::draw()` 最终会调用：

```cpp
vkCmdDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, 0, 0);
```

这里第二个参数 `instanceCount` 固定是 `1`，所以即使 shader 里写了 `SV_InstanceID`，也只能得到一个 instance。

Lab3 要做 instancing，需要让同一个 glTF mesh 一次 draw call 绘制多个实例，因此新增：

```cpp
void drawInstanced(
    VkCommandBuffer commandBuffer,
    uint32_t instanceCount,
    uint32_t firstInstance = 0,
    uint32_t renderFlags = 0,
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE,
    uint32_t bindImageSet = 1
);
```

最常用的调用方式是：

```cpp
model.drawInstanced(cmd, instanceCount);
```

如果需要沿用 glTF material image descriptor 绑定：

```cpp
model.drawInstanced(
    cmd,
    instanceCount,
    0,
    vkglTF::RenderFlags::BindImages,
    pipelineLayout,
    1
);
```

## 2. 最推荐的 Lab3 用法：StorageBuffer + SV_InstanceID

Lab3 的 deferred / G-Buffer pass 更建议把 instance 数据放在 storage buffer 里，然后在 vertex shader 中通过 `SV_InstanceID` 取数据。

C++ 侧准备 instance buffer：

```cpp
struct InstanceData {
    glm::mat4 model;
    glm::mat4 normalMatrix;
    glm::vec4 color;
    glm::vec4 materialParams; // x metallic, y roughness, z emissiveStrength, w materialIndex
};

AllocatedBuffer instanceBuffer;
std::vector<InstanceData> instances;
```

descriptor layout 中绑定：

```cpp
vkutil::descriptorSetLayoutBinding(
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    VK_SHADER_STAGE_VERTEX_BIT,
    2
);
```

绘制时：

```cpp
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.gBufferInstanced);
vkCmdBindDescriptorSets(
    cmd,
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    pipelineLayouts.gBuffer,
    0,
    1,
    &descriptorSets[currentBuffer].scene,
    0,
    nullptr
);

sceneModel.drawInstanced(cmd, renderSettings.instanceCount);
```

shader 侧：

```hlsl
struct InstanceData {
    float4x4 model;
    float4x4 normalMatrix;
    float4 color;
    float4 materialParams;
};

StructuredBuffer<InstanceData> instances : register(t2, space0);

VSOutput main(VSInput input, uint instanceID : SV_InstanceID) {
    InstanceData inst = instances[instanceID];

    float4 worldPos = mul(inst.model, float4(input.pos, 1.0));
    output.position = mul(camera.projection, mul(camera.view, worldPos));
    output.color = inst.color;
    output.materialParams = inst.materialParams;
    return output;
}
```

这种做法的好处是：不用额外增加 vertex input binding，pipeline vertex layout 仍然只描述普通 glTF 顶点。实例数据完全由 descriptor 提供，和 deferred G-Buffer 的设计更搭。

## 3. 另一种用法：Instance Vertex Buffer

也可以使用传统的 instance vertex buffer。这个方式和 `examples/instancing/instancing.cpp` 更接近。

绘制前手动绑定两个 vertex buffer：

```cpp
VkBuffer vertexBuffers[] = {
    model.vertices.buffer,
    instanceBuffer.buffer
};
VkDeviceSize offsets[] = {0, 0};

vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
vkCmdBindIndexBuffer(cmd, model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

model.drawInstanced(cmd, instanceCount);
```

pipeline vertex input 中需要有两个 binding：

```cpp
binding 0: glTF vertex data, VK_VERTEX_INPUT_RATE_VERTEX
binding 1: instance data,    VK_VERTEX_INPUT_RATE_INSTANCE
```

这种方式的优点是 vertex shader 输入声明更直观；缺点是每种 instance 数据布局都要同步修改 pipeline vertex input。

## 4. 函数内部如何实现

新增了两个函数：

```cpp
void vkglTF::Model::drawInstanced(...);
void vkglTF::Model::drawNodeInstanced(...);
```

`drawInstanced()` 负责：

```cpp
if (instanceCount == 0) {
    return;
}

if (!buffersBound) {
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
}

for (auto& node : nodes) {
    drawNodeInstanced(node, commandBuffer, instanceCount, firstInstance, renderFlags, pipelineLayout, bindImageSet);
}
```

`drawNodeInstanced()` 负责递归遍历 glTF node：

```cpp
if (node->mesh) {
    for (Primitive* primitive : node->mesh->primitives) {
        if (renderFlags & vkglTF::RenderFlags::BindImages) {
            vkCmdBindDescriptorSets(..., &primitive->material.descriptorSet, ...);
        }

        vkCmdDrawIndexed(
            commandBuffer,
            primitive->indexCount,
            instanceCount,
            primitive->firstIndex,
            0,
            firstInstance
        );
    }
}

for (auto& child : node->children) {
    drawNodeInstanced(child, ...);
}
```

也就是说，它和原来的 `draw()` 一样支持：

- glTF node 递归绘制
- 多 primitive 绘制
- `RenderOpaqueNodes` / `RenderAlphaMaskedNodes` / `RenderAlphaBlendedNodes` 过滤
- `RenderFlags::BindImages` 自动绑定材质贴图 descriptor

唯一核心区别是：原来 `instanceCount = 1`，现在由调用方传入。

## 5. 注意事项

1. `drawInstanced()` 只负责把 Vulkan draw call 的 `instanceCount` 打开，不负责创建 instance buffer。

2. 如果使用 `StorageBuffer + SV_InstanceID`，需要先绑定包含 instance buffer 的 descriptor set。

3. 如果使用 instance vertex buffer，需要在调用前确保 binding 1 已经绑定，并且 pipeline vertex input 声明了 `VK_VERTEX_INPUT_RATE_INSTANCE`。

4. `firstInstance` 默认是 `0`。一般 Lab3 第一版保持 `0` 就够了；如果之后做分批绘制或 GPU culling，再考虑使用非零 `firstInstance`。

5. 每个 primitive 都会绘制 `instanceCount` 个实例。如果一个 glTF 模型有多个 primitive，那么每个 primitive 都会发出一次 instanced draw call。

6. 多材质模型使用 `RenderFlags::BindImages` 时，每个 primitive 仍会绑定自己的材质 descriptor，所以 draw call 数量仍然等于 primitive 数量。Instancing 主要减少的是“同一个 primitive 重复画 N 个物体”的 CPU 提交成本。

## 6. Lab3 推荐集成方式

Lab3 的第一版建议这样接：

```cpp
void VulkanExample::cmdDrawGBuffer(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.gBufferInstanced);
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayouts.gBuffer,
        0,
        1,
        &descriptorSets[currentBuffer].scene,
        0,
        nullptr
    );

    sceneModel.drawInstanced(cmd, renderSettings.instanceCount);
}
```

对应 shader：

```hlsl
VSOutput main(VSInput input, uint instanceID : SV_InstanceID) {
    InstanceData instance = instances[instanceID];
    ...
}
```

这样我就可以先把 `instanceCount` 从 UI 暴露出来，确认 draw call 数量不随实例数量线性增长，再继续把每个 instance 的位置、材质参数、颜色写进 G-Buffer。
