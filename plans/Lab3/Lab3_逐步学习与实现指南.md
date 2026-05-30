# Lab3 逐步学习与实现指南：Deferred Rendering + SSAO + Instancing

这份文档是我给自己实现 Lab3 时使用的“施工型学习笔记”。它不是只讲概念，而是从当前 `examples/lab3/lab3.cpp` / `lab3.h` 的基础框架出发，告诉我下一步应该改哪里、为什么这样改、每一步怎么验证。

Lab3 的大目标是：

```text
Geometry Pass
    -> G-Buffer
    -> SSAO
    -> SSAO Blur
    -> Deferred Lighting
    -> HDR / Bloom / Tone Mapping
    -> Swapchain
```

但真正实现时不能一口吃完。正确节奏应该是：

```text
阶段 1：G-Buffer 可视化
阶段 2：Deferred Lighting 恢复基础 PBR
阶段 3：SSAO Raw
阶段 4：SSAO Blur + 合入环境光
阶段 5：Instancing 写入 G-Buffer
阶段 6：接回 HDR Bloom / Composite
```

我现在应该先把 **G-Buffer debug view** 做扎实。它是 Lab3 的地基。

## 0. 当前 Lab3 框架怎么读

当前 `Lab3` 框架已经准备好了几类资源：

```cpp
GBufferResources gBuffer;
SSAOResources ssao;
HDRResources hdr;

std::array<FrameUniformBuffers, maxConcurrentFrames> uniformBuffers;
std::array<DescriptorSets, maxConcurrentFrames> descriptorSets;

DescriptorSetLayouts descriptorSetLayouts;
PipelineLayouts pipelineLayouts;
Pipelines pipelines;

AllocatedBuffer instanceBuffer;
std::vector<InstanceData> instanceCpuData;
```

我可以把它理解成：

```text
gBuffer:
    第一遍 Geometry Pass 的输出。

ssao:
    屏幕空间环境光遮蔽的 raw / blurred 结果。

hdr:
    Deferred Lighting 之后的线性 HDR 场景颜色。

uniformBuffers:
    每帧一份 camera / lights / ssao 参数，避免 frame overlap 时 CPU 覆盖 GPU 正在读的数据。

descriptorSets:
    每帧一组 descriptor set，和 uniform buffer 对齐。

instanceBuffer:
    存放实例化绘制需要的 model matrix / normal matrix / 材质参数。
```

当前 `buildCommandBuffer()` 还只是：

```cpp
cmdDrawClearOnly(commandBuffer);
```

所以程序目前只是清屏和显示 UI。真正的 Lab3 渲染链路要逐步替换成：

```cpp
cmdDrawGBuffer(commandBuffer);
cmdDrawSSAO(commandBuffer);
cmdDrawSSAOBlur(commandBuffer);
cmdDrawDeferredLighting(commandBuffer);
cmdDrawComposite(commandBuffer);
```

## 1. 第一阶段：G-Buffer 可视化

### 1.1 这个阶段的目标

第一阶段不要做完整光照，也不要急着做 SSAO。目标只有一个：

```text
把模型渲染进 G-Buffer，然后能在屏幕上切换显示每一张 G-Buffer 纹理。
```

我要能看到：

```text
Debug View 1: Albedo
Debug View 2: Normal
Debug View 3: Roughness
Debug View 4: Metallic
Debug View 5: Depth
```

只有这些都正确，后面的 Deferred Lighting 和 SSAO 才有可靠输入。

### 1.2 G-Buffer 当前设计

当前 `lab3.h` 里有：

```cpp
struct GBufferResources {
    RenderAttachment albedoMetallic;
    RenderAttachment normalRoughness;
    RenderAttachment emissiveAO;
    RenderAttachment depth;

    VkFormat albedoMetallicFormat{VK_FORMAT_R16G16B16A16_SFLOAT};
    VkFormat normalRoughnessFormat{VK_FORMAT_R16G16B16A16_SFLOAT};
    VkFormat emissiveAOFormat{VK_FORMAT_R16G16B16A16_SFLOAT};
    VkFormat depthAttachmentFormat{VK_FORMAT_D32_SFLOAT};
    VkExtent2D extent{};
};
```

建议第一版存：

```text
gBuffer.albedoMetallic.rgb:
    albedo，线性空间颜色

gBuffer.albedoMetallic.a:
    metallic

gBuffer.normalRoughness.rgb:
    normal 编码到 [0, 1]

gBuffer.normalRoughness.a:
    roughness

gBuffer.emissiveAO.rgb:
    emissive

gBuffer.emissiveAO.a:
    material AO

gBuffer.depth:
    hardware depth
```

正式版应该直接接 glTF PBR 材质贴图。也就是说，`gbuffer.frag` 不是从顶点颜色里拿 albedo，也不是从实例参数里拿 metallic/roughness，而是像 Lab2 的 `PBRTexture.frag` 一样采样：

```text
baseColorMap
metallicRoughnessMap
normalMap
occlusionMap
emissiveMap
```

实例数据仍然有价值，但它更多是 transform、颜色调制、材质倍率或后续 GPU culling / material index 的扩展位，不是 PBR 材质的主要来源。

### 1.3 G-Buffer pass 绑定哪个 descriptor

G-Buffer pass 不是采样 G-Buffer 纹理，而是写入 G-Buffer attachment。

所以 G-Buffer pipeline 需要的 descriptor 是场景输入：

```text
descriptorSets[currentBuffer].scene
```

它的 layout 是：

```cpp
descriptorSetLayouts.scene
```

当前布局：

```text
set 0 binding 0:
    CameraUBO
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    vertex + fragment 可见

set 0 binding 1:
    LightsUBO
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    fragment 可见

set 0 binding 2:
    InstanceData buffer
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    vertex 可见
```

正式版 G-Buffer 还需要 glTF 每个 primitive 自己的材质贴图 descriptor：

```text
set 1:
    vkglTF::descriptorSetLayoutImage

set 1 binding 0:
    baseColorMap

set 1 binding 1:
    metallicRoughnessMap

set 1 binding 2:
    normalMap

set 1 binding 3:
    occlusionMap

set 1 binding 4:
    emissiveMap
```

所以 G-Buffer pipeline layout 不是只有 `descriptorSetLayouts.scene`，而应该是：

```cpp
pipelineLayouts.gBuffer = vkutil::createPipelineLayout(
    device,
    {descriptorSetLayouts.scene, vkglTF::descriptorSetLayoutImage}
);
```

对应 HLSL 可以写成：

```hlsl
struct CameraUBO {
    float4x4 projection;
    float4x4 view;
    float4x4 inverseProjection;
    float4x4 inverseView;
    float4 cameraPos;
    float4 screenSize;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUBO> camera;

struct InstanceData {
    float4x4 model;
    float4x4 normalMatrix;
    float4 color;
    float4 materialParams; // 调制参数，不是主要 PBR 材质来源
};

[[vk::binding(2, 0)]]
StructuredBuffer<InstanceData> instances;

Texture2D baseColorMap : register(t0, space1);
SamplerState baseColorSampler : register(s0, space1);

Texture2D metallicRoughnessMap : register(t1, space1);
SamplerState metallicRoughnessSampler : register(s1, space1);

Texture2D normalMap : register(t2, space1);
SamplerState normalSampler : register(s2, space1);

Texture2D occlusionMap : register(t3, space1);
SamplerState occlusionSampler : register(s3, space1);

Texture2D emissiveMap : register(t4, space1);
SamplerState emissiveSampler : register(s4, space1);
```

### 1.4 需要新增的 shader

建议先创建这些文件：

```text
shaders/hlsl/lab3/gbuffer.vert
shaders/hlsl/lab3/gbuffer.frag
shaders/hlsl/lab3/fullscreen.vert
shaders/hlsl/lab3/debugGBuffer.frag
```

当前 `lab3.h` 里 composite shader 名字是：

```cpp
const std::string compositeFragmentShader = "lab3/composite.frag.spv";
```

第一阶段可以直接让 `composite.frag` 充当 debug G-Buffer shader。也可以改名成 `debugGBuffer.frag`。如果我想少改 C++，就先写：

```text
shaders/hlsl/lab3/composite.frag
```

让它读取 G-Buffer 并根据 `debugView` 输出。

### 1.5 gbuffer.vert 第一版思路

如果先做 instancing 版本，可以用 `SV_InstanceID`：

```hlsl
struct VSInput {
    float3 pos     : POSITION0;
    float3 normal  : NORMAL0;
    float2 uv      : TEXCOORD0;
    float4 tangent : TANGENT0;
};

struct VSOutput {
    float4 position       : SV_POSITION;
    float3 worldPos       : TEXCOORD0;
    float3 worldNormal    : TEXCOORD1;
    float2 uv             : TEXCOORD2;
    float4 worldTangent   : TEXCOORD3;
    float4 instanceColor  : TEXCOORD4;
    float4 materialParams : TEXCOORD5;
};

VSOutput main(VSInput input, uint instanceID : SV_InstanceID) {
    InstanceData inst = instances[instanceID];

    float4 worldPos = mul(inst.model, float4(input.pos, 1.0));
    float3 worldNormal = normalize(mul((float3x3)inst.normalMatrix, input.normal));
    float3 worldTangent = normalize(mul((float3x3)inst.model, input.tangent.xyz));

    VSOutput output;
    output.position = mul(camera.projection, mul(camera.view, worldPos));
    output.worldPos = worldPos.xyz;
    output.worldNormal = worldNormal;
    output.uv = input.uv;
    output.worldTangent = float4(worldTangent, input.tangent.w);
    output.instanceColor = inst.color;
    output.materialParams = inst.materialParams;
    return output;
}
```

这里的重点不是最终光照，而是确认：

```text
instanceID 能读到正确实例数据
model matrix 生效
normal matrix 生效
tangent 能传到 fragment shader，用于 normal map 的 TBN
position 能进入 depth
```

### 1.6 gbuffer.frag 第一版思路

正式版输出来自 glTF PBR 贴图的材质：

```hlsl
struct FSInput {
    float3 worldPos       : TEXCOORD0;
    float3 worldNormal    : TEXCOORD1;
    float2 uv             : TEXCOORD2;
    float4 worldTangent   : TEXCOORD3;
    float4 instanceColor  : TEXCOORD4;
    float4 materialParams : TEXCOORD5;
};

struct GBufferOutput {
    float4 albedoMetallic  : SV_TARGET0;
    float4 normalRoughness : SV_TARGET1;
    float4 emissiveAO      : SV_TARGET2;
};

Texture2D baseColorMap : register(t0, space1);
SamplerState baseColorSampler : register(s0, space1);

Texture2D metallicRoughnessMap : register(t1, space1);
SamplerState metallicRoughnessSampler : register(s1, space1);

Texture2D normalMap : register(t2, space1);
SamplerState normalSampler : register(s2, space1);

Texture2D occlusionMap : register(t3, space1);
SamplerState occlusionSampler : register(s3, space1);

Texture2D emissiveMap : register(t4, space1);
SamplerState emissiveSampler : register(s4, space1);

float3 getWorldNormalFromMap(FSInput input) {
    float3 N = normalize(input.worldNormal);
    float3 T = normalize(input.worldTangent.xyz);
    T = normalize(T - N * dot(T, N));
    float3 B = normalize(cross(N, T) * input.worldTangent.w);

    float3 tangentNormal = normalMap.Sample(normalSampler, input.uv).xyz * 2.0 - 1.0;
    return normalize(mul(tangentNormal, float3x3(T, B, N)));
}

GBufferOutput main(FSInput input) {
    float4 baseColor = baseColorMap.Sample(baseColorSampler, input.uv);
    float4 metallicRoughness = metallicRoughnessMap.Sample(metallicRoughnessSampler, input.uv);
    float ao = occlusionMap.Sample(occlusionSampler, input.uv).r;
    float3 emissive = emissiveMap.Sample(emissiveSampler, input.uv).rgb;

    // baseColor / emissive 如果按 SRGB 格式创建，采样时会自动解码到 linear。
    float3 albedo = saturate(baseColor.rgb * input.instanceColor.rgb);
    float metallic = saturate(metallicRoughness.b * input.materialParams.x);
    float roughness = clamp(metallicRoughness.g * input.materialParams.y, 0.04, 1.0);
    emissive *= input.materialParams.z;

    float3 N = getWorldNormalFromMap(input);

    GBufferOutput output;
    output.albedoMetallic = float4(albedo, metallic);
    output.normalRoughness = float4(N * 0.5 + 0.5, roughness);
    output.emissiveAO = float4(emissive, ao);
    return output;
}
```

注意：如果后面 SSAO 打算在 view space 做，这里可以把 normal 从 world space 转成 view space 后再写入 `normalRoughness.rgb`。关键是：**G-Buffer 写入空间和后续读取空间必须一致**。

### 1.7 G-Buffer pipeline 怎么创建

`createPipelines()` 现在只创建了 pipeline layout：

```cpp
pipelineLayouts.gBuffer =
    vkutil::createPipelineLayout(device, {descriptorSetLayouts.scene, vkglTF::descriptorSetLayoutImage});
```

下一步要创建真正的 `pipelines.gBufferInstanced`。

大致结构：

```cpp
VkPipelineVertexInputStateCreateInfo vertexInput =
    *vkglTF::Vertex::getPipelineVertexInputState({
        vkglTF::VertexComponent::Position,
        vkglTF::VertexComponent::Normal,
        vkglTF::VertexComponent::UV,
        vkglTF::VertexComponent::Tangent
    });

VkPipelineShaderStageCreateInfo vert =
    loadShader(getShadersPath() + gBufferInstancedVertexShader, VK_SHADER_STAGE_VERTEX_BIT);

VkPipelineShaderStageCreateInfo frag =
    loadShader(getShadersPath() + gBufferFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT);

pipelines.gBufferInstanced = vkutil::PipelineBuilder{}
    .setPipelineLayout(pipelineLayouts.gBuffer)
    .setShaders(vert, frag)
    .setVertexInput(vertexInput)
    .setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
    .setPolygonMode(VK_POLYGON_MODE_FILL)
    .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
    .setColorAttachmentFormats({
        gBuffer.albedoMetallicFormat,
        gBuffer.normalRoughnessFormat,
        gBuffer.emissiveAOFormat
    })
    .setDepthFormat(gBuffer.depthAttachmentFormat)
    .enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL)
    .disableBlending()
    .build(device, pipelineCache);
```

这里要注意：

```text
G-Buffer pipeline 有 3 个 color attachment。
顺序必须和 fragment shader 的 SV_TARGET0/1/2 对齐。
depth format 必须和 gBuffer.depthAttachmentFormat 对齐。
```

### 1.8 cmdDrawGBuffer 怎么写

伪代码：

```cpp
void VulkanExample::cmdDrawGBuffer(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachments[] = {
        vkutil::renderingAttachmentInfo(
            gBuffer.albedoMetallic.image.imageView,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}}
        ),
        vkutil::renderingAttachmentInfo(
            gBuffer.normalRoughness.image.imageView,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VkClearValue{{0.5f, 0.5f, 1.0f, 1.0f}}
        ),
        vkutil::renderingAttachmentInfo(
            gBuffer.emissiveAO.image.imageView,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            VkClearValue{{0.0f, 0.0f, 0.0f, 1.0f}}
        )
    };

    VkRenderingAttachmentInfo depthAttachment =
        vkutil::renderingdepthAttachmentInfo(
            gBuffer.depth.image.imageView,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            1.0f
        );

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, gBuffer.extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 3;
    renderingInfo.pColorAttachments = colorAttachments;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    {
        vkutil::cmdSetViewportAndScissor(cmd, gBuffer.extent.width, gBuffer.extent.height);

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

        sceneModel.drawInstanced(
            cmd,
            renderSettings.instanceCount,
            0,
            vkglTF::RenderFlags::BindImages,
            pipelineLayouts.gBuffer,
            1
        );
    }
    vkCmdEndRendering(cmd);
}
```

这一步还需要一个 `sceneModel`。可以先在 `lab3.h` 加：

```cpp
vkglTF::Model sceneModel;
```

然后 `loadAssets()` 加。注意这里要先打开 glTF PBR 贴图 descriptor flags：

```cpp
vkglTF::descriptorBindingFlags =
    vkglTF::DescriptorBindingFlags::ImageBaseColor |
    vkglTF::DescriptorBindingFlags::ImageMetallicRoughness |
    vkglTF::DescriptorBindingFlags::ImageNormalMap |
    vkglTF::DescriptorBindingFlags::ImageOcclusionMap |
    vkglTF::DescriptorBindingFlags::ImageEmissiveMap;

sceneModel.loadFromFile(
    getAssetPath() + "models/DamagedHelmet/DamagedHelmet.gltf",
    vulkanDevice,
    queue,
    vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY
);
```

这样 `vkglTF::Model` 会为每个材质创建 `vkglTF::descriptorSetLayoutImage` 和材质 descriptor set。G-Buffer pipeline layout 的 `set 1` 就依赖这个 layout。

### 1.9 G-Buffer layout transition

在 `buildCommandBuffer()` 里，G-Buffer pass 前要把 images 转成 attachment layout：

```cpp
vkutil::cmdTransitionImageLayout(cmd, gBuffer.albedoMetallic.image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
vkutil::cmdTransitionImageLayout(cmd, gBuffer.normalRoughness.image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
vkutil::cmdTransitionImageLayout(cmd, gBuffer.emissiveAO.image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
vkutil::cmdTransitionImageLayout(cmd, gBuffer.depth.image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
```

G-Buffer pass 后转成 shader read：

```cpp
vkutil::cmdTransitionImageLayout(cmd, gBuffer.albedoMetallic.image.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
vkutil::cmdTransitionImageLayout(cmd, gBuffer.normalRoughness.image.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
vkutil::cmdTransitionImageLayout(cmd, gBuffer.emissiveAO.image.image, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
vkutil::cmdTransitionImageLayout(cmd, gBuffer.depth.image.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
```

第一版每帧从 `UNDEFINED` 开始是可以的，因为我每帧都会 clear 并重写全部 G-Buffer。后面如果做更复杂复用，再精确追踪 layout。

### 1.10 Debug G-Buffer shader

第一阶段 `composite.frag` 可以这样设计：

```hlsl
[[vk::binding(0, 0)]]
Texture2D albedoMetallicTex;
[[vk::binding(0, 0)]]
SamplerState albedoMetallicSampler;

[[vk::binding(1, 0)]]
Texture2D normalRoughnessTex;
[[vk::binding(1, 0)]]
SamplerState normalRoughnessSampler;

[[vk::binding(2, 0)]]
Texture2D emissiveAOTex;
[[vk::binding(2, 0)]]
SamplerState emissiveAOSampler;

[[vk::binding(3, 0)]]
Texture2D depthTex;
[[vk::binding(3, 0)]]
SamplerState depthSampler;

struct PushConstants {
    int debugView;
    float exposure;
    float2 padding;
};
[[vk::push_constant]]
PushConstants pc;

float4 main(float2 uv : TEXCOORD0) : SV_TARGET {
    float4 am = albedoMetallicTex.Sample(albedoMetallicSampler, uv);
    float4 nr = normalRoughnessTex.Sample(normalRoughnessSampler, uv);
    float4 ea = emissiveAOTex.Sample(emissiveAOSampler, uv);
    float depth = depthTex.Sample(depthSampler, uv).r;

    if (pc.debugView == 1) return float4(am.rgb, 1.0);
    if (pc.debugView == 2) return float4(nr.rgb, 1.0);
    if (pc.debugView == 3) return float4(nr.aaa, 1.0);
    if (pc.debugView == 4) return float4(am.aaa, 1.0);
    if (pc.debugView == 5) return float4(depth.xxx, 1.0);

    return float4(am.rgb, 1.0);
}
```

注意：上面只是表达绑定关系，HLSL 里 `Texture2D + SamplerState` 的 register 写法要和你项目现有 HLSL 风格保持一致。也可以使用 `Texture2D.Sample(sampler, uv)` 的普通写法。

### 1.11 第一阶段验收标准

完成第一阶段后，我应该看到：

```text
Albedo:
    来自 baseColorMap 的贴图颜色正常，并且不是全白/全黑。

Normal:
    来自 normalMap + TBN 的法线正常，不是纯色或乱色。

Roughness:
    来自 metallicRoughnessMap.g，灰度应能看出材质粗糙度变化。

Metallic:
    来自 metallicRoughnessMap.b，金属区域和非金属区域应明显不同。

Depth:
    近处更亮或更暗都可以，但必须随距离连续变化。
```

如果 normal 显示异常，先查：

```text
vertex input attribute 是否和 vkglTF::Vertex 对齐
normal matrix 是否正确
Tangent 是否作为 vertex input 传入
glTF material descriptor 是否绑定到 set 1
是否 normalize
encode/decode 是否一致
```

如果 depth 没有变化，先查：

```text
depth attachment layout
depth format
pipeline depth test
model 是否在相机视野内
projection/view 是否正确上传
```

## 2. 第二阶段：Deferred Lighting

### 2.1 目标

第二阶段目标是用 G-Buffer 恢复基础 PBR 直接光照：

```text
G-Buffer
    -> fullscreen deferredLighting.frag
    -> hdr.sceneColor
```

暂时不做 SSAO，不做 IBL，先让直接光照成立。

### 2.2 Deferred Lighting descriptor layout

当前 layout 是：

```text
set 0 binding 0: gBuffer.albedoMetallic
set 0 binding 1: gBuffer.normalRoughness
set 0 binding 2: gBuffer.emissiveAO
set 0 binding 3: gBuffer.depth
set 0 binding 4: ssao.blurred
set 0 binding 5: CameraUBO
set 0 binding 6: LightsUBO
```

第一版可以先忽略 `binding 4`，把 SSAO 当作 1.0。

### 2.3 shader 逻辑

Deferred lighting 做的事情：

```text
读取 albedo / metallic
读取 normal / roughness
从 depth 重建 position
读取 camera / light
计算 Cook-Torrance BRDF
输出 HDR color
```

先做简化版：

```hlsl
float3 color = 0;
for each light:
    color += DiffuseLambert + SimpleSpecular;
color += emissive;
```

等方向和深度都正确后，再把 Lab2 的 `D_GGX / G_Smith / F_Schlick` 搬进来。

### 2.4 验收标准

```text
光源移动时，高光和明暗方向正确。
roughness 越低，高光越集中。
metallic 越高，漫反射越少，镜面反射颜色越接近 albedo。
输出仍然是 HDR linear，不做 tone mapping。
```

## 3. 第三阶段：SSAO Raw

### 3.1 目标

SSAO Raw pass 输入：

```text
gBuffer.depth
gBuffer.normalRoughness
ssao.noise
ssao.kernel
ssaoParamsUBO
```

输出：

```text
ssao.raw
```

第一版应该是有噪声但逻辑正确的 AO 图。

### 3.2 关键思想

对每个像素：

```text
1. 从 depth 重建 view-space position。
2. 从 normal buffer 取 normal。
3. 构造 TBN，把 sample kernel 转到当前 normal 的半球方向。
4. 投影 sample position 到屏幕空间。
5. 比较 sample depth 和 depth buffer 中的深度。
6. 统计遮蔽比例。
```

### 3.3 最容易出错的点

SSAO 最容易出错的是空间不一致：

```text
如果 normal 是 world space，sample position 是 view space，就会错。
如果 depth reconstruction 是 view space，但 light/deferred 用 world space，也要明确转换。
```

建议 SSAO 阶段统一：

```text
normal 存 view-space normal
position 从 depth 重建 view-space position
kernel 在 tangent/view space 中使用
```

这意味着第一阶段 G-Buffer 的 normal 最好在做 SSAO 前改成 view-space normal。

### 3.4 验收标准

```text
物体接触地面的位置更暗。
凹陷、角落、缝隙更暗。
空旷平面不要整片发黑。
调大 radius，影响范围变大。
调大 bias，自遮蔽减少。
```

## 4. 第四阶段：SSAO Blur 与合入 Lighting

### 4.1 普通 blur

第一版做 4x4 或 5x5 平均：

```hlsl
float ao = 0;
for x/y:
    ao += ssaoRaw.Sample(...).r;
ao /= sampleCount;
```

输出到：

```text
ssao.blurred
```

### 4.2 合入 deferred lighting

SSAO 不建议直接乘最终颜色：

```hlsl
color *= ssao; // 不推荐第一选择
```

更合理的是主要影响 ambient / IBL：

```hlsl
float ambientAO = materialAO * ssao;
float3 color = directLighting + indirectLighting * ambientAO + emissive;
```

如果第二阶段还没做 IBL，可以先做一个简单 ambient：

```hlsl
float3 ambient = albedo * 0.03;
float3 color = directLighting + ambient * ssao + emissive;
```

### 4.3 验收标准

```text
SSAO raw 有噪声。
SSAO blurred 更平滑。
开启/关闭 SSAO 时，接触阴影变化明显。
直接光照区域不应该被 SSAO 过度压黑。
```

## 5. 第五阶段：Instancing

### 5.1 目标

把大量相同模型通过一次 instanced draw 写入 G-Buffer：

```cpp
sceneModel.drawInstanced(
    cmd,
    renderSettings.instanceCount,
    0,
    vkglTF::RenderFlags::BindImages,
    pipelineLayouts.gBuffer,
    1
);
```

当前已经新增了 `vkglTF::Model::drawInstanced()`，所以 Lab3 不需要手动绕开 `model.draw()`。

### 5.2 当前 instance 数据

`InstanceData`：

```cpp
struct InstanceData {
    glm::mat4 model;
    glm::mat4 normalMatrix;
    glm::vec4 color;
    glm::vec4 materialParams; // metallicMul, roughnessMul, emissiveMul, materialIndex/unused
};
```

它被写入 `instanceBuffer`，descriptor 在：

```text
descriptorSets[currentBuffer].scene
binding 2
```

shader 通过：

```hlsl
StructuredBuffer<InstanceData> instances;
InstanceData inst = instances[SV_InstanceID];
```

读取。

### 5.3 验收标准

```text
instanceCount = 1 时只画一个。
instanceCount 增大时，画面出现更多实例。
不同 instance 的 metallic / roughness debug view 有规律变化。
CPU draw call 不随 instanceCount 线性增加。
```

## 6. 第六阶段：接回 HDR / Bloom / Composite

### 6.1 目标

等 Deferred Lighting 输出 `hdr.sceneColor` 后，再把 Lab2 的后处理链路接回来：

```text
hdr.sceneColor
    -> bloom downsample
    -> bloom upsample
    -> composite
    -> tone mapping + gamma
    -> swapchain
```

第一版可以先不做 Bloom，只做：

```text
hdr.sceneColor -> tone mapping -> swapchain
```

### 6.2 注意事项

```text
G-Buffer pass 不做 tone mapping。
Deferred Lighting pass 不做 gamma。
HDR sceneColor 保持 linear HDR。
最终只在 composite 做一次 tone mapping + gamma。
```

这和 Lab2 的经验完全一致。

## 7. 建议的实际提交顺序

我后面可以按这个顺序写代码：

```text
1. 添加 sceneModel，加载 DamagedHelmet.gltf，并打开 glTF PBR 贴图 descriptor flags。
2. 写 lab3/gbuffer.vert 和 lab3/gbuffer.frag。
3. 让 pipelineLayouts.gBuffer 使用 set 0 scene + set 1 vkglTF::descriptorSetLayoutImage。
4. 创建 pipelines.gBufferInstanced。
5. 实现 cmdDrawGBuffer，并用 drawInstanced(... BindImages ..., bindImageSet = 1)。
6. 写 fullscreen.vert 和 composite/debugGBuffer.frag。
7. 修改 composite descriptor layout，让它读取 G-Buffer。
8. 实现 cmdDrawComposite，显示 debugView。
9. buildCommandBuffer 改成 GBuffer -> Composite。
10. 验证 Albedo / Normal / Roughness / Metallic / Depth。
11. 写 deferredLighting.frag，输出 hdr.sceneColor。
12. composite 改成读 hdr.sceneColor。
13. 写 ssao.frag。
14. 写 ssaoBlur.frag。
15. 把 ssao.blurred 合入 deferred lighting。
16. 优化 instancing 材质调制参数和调试 UI。
17. 接回 Bloom。
```

## 8. 每一步都要保留 Debug View

Lab3 绝对不能只看最终画面。建议 debug view 一直保留：

```text
0 Final
1 Albedo
2 Normal
3 Roughness
4 Metallic
5 Depth
6 SSAO Raw
7 SSAO Blurred
8 Direct Lighting Only
9 Ambient / IBL Only
10 Emissive Only
```

只要画面不对，就先切 debug view，而不是直接调参数。

## 9. 我应该重点理解的概念

### 9.1 G-Buffer 是“缓存材质与几何信息”

Forward Rendering 是：

```text
每个物体 shader 里直接算光照
```

Deferred Rendering 是：

```text
第一遍只记录这个像素是什么材质、什么法线、什么深度
第二遍再统一算光照
```

所以 G-Buffer 是一种屏幕空间缓存。

### 9.2 SSAO 是“利用屏幕空间几何估计环境遮蔽”

SSAO 不是真实阴影。它只是说：

```text
如果一个点附近很拥挤、很封闭，它收到的环境光应该少一点。
```

因此 SSAO 最适合压暗：

```text
接触区域
缝隙
角落
凹陷
```

不应该拿它替代 shadow map。

### 9.3 Instancing 是“减少重复提交”

Instancing 的本质不是让 GPU magically 更快，而是：

```text
同一个 mesh 的 vertex/index buffer 只绑定一次
一次 draw call 画多个 instance
shader 用 instanceID 找到每个实例自己的 transform/material
```

Lab3 使用 storage buffer 的方式更接近现代 renderer，因为它后面可以自然扩展到：

```text
GPU culling
indirect draw
LOD
bindless material
per-instance previous matrix
```

## 10. 当前阶段最重要的一句话

现在不要急着做 SSAO，也不要急着恢复完整 PBR。Lab3 第一阶段只要咬住这一件事：

```text
把 G-Buffer 写正确，并且能把每一项 G-Buffer 内容显示出来。
```

只要这一步稳定，Deferred Lighting、SSAO 和 Instancing 都会变得清晰。G-Buffer debug view 就是 Lab3 的仪表盘。
