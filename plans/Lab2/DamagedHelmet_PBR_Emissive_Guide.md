# Lab2: DamagedHelmet PBR 材质与自发光接入指南

这份文档只保留当前 Lab2 接入 `DamagedHelmet` 真正需要的内容：资源放置、glTF 贴图加载链路、descriptor 绑定方式、PBR 贴图采样代码、自发光处理，以及常见调试点。

当前底层 `base/VulkanglTFModel.h/.cpp` 已经补充了 glTF PBR 材质贴图 descriptor 支持，所以这份文档不再重复“应该如何扩展底层”的旧方案，而是直接指导你在 Lab2 中如何使用。

官方资源：

- Khronos glTF Sample Assets: <https://github.khronos.org/glTF-Assets/>
- DamagedHelmet: <https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet>

## 1. 目标

本轮目标是让 Lab2 从“程序材质 PBR”升级为“读取 glTF PBR 贴图材质”：

```text
DamagedHelmet.gltf
    -> tinygltf 读取 mesh / image / material
    -> vkglTF::Material 保存 PBR 贴图指针
    -> vkglTF::Material::createDescriptorSet 写入 set1
    -> Lab2 scene pipeline 使用 set0(scene) + set1(material)
    -> pbrScene.frag 采样 baseColor / MR / normal / AO / emissive
    -> direct lighting + IBL + emissive
```

优先级建议：

| 阶段 | 内容 | 通过标准 |
|---|---|---|
| 1 | 加载 DamagedHelmet 几何 | 模型完整显示 |
| 2 | baseColor | 贴图颜色正确 |
| 3 | metallic/roughness/AO | 金属、粗糙度、环境遮蔽正确 |
| 4 | emissive | 自发光能参与最终颜色 |
| 5 | normal map | 破损细节和凹凸方向正确 |

## 2. 模型资源放置

下载 `DamagedHelmet` 的 `glTF` 版本，不建议一开始使用 `.glb`。当前项目的 loader 走 `LoadASCIIFromFile`，所以 separate glTF 最稳。

建议目录：

```text
assets/models/DamagedHelmet/glTF/DamagedHelmet.gltf
assets/models/DamagedHelmet/glTF/DamagedHelmet.bin
assets/models/DamagedHelmet/glTF/Default_albedo.jpg
assets/models/DamagedHelmet/glTF/Default_metalRoughness.jpg
assets/models/DamagedHelmet/glTF/Default_normal.jpg
assets/models/DamagedHelmet/glTF/Default_AO.jpg
assets/models/DamagedHelmet/glTF/Default_emissive.jpg
```

注意：代码里不要手动 hard code 这些贴图文件名。`.gltf` 会描述 material 使用了哪些 image，`vkglTF::Model::loadFromFile()` 会自动加载并绑定。

## 3. DamagedHelmet PBR 贴图含义

DamagedHelmet 使用 glTF 2.0 metallic-roughness 工作流：

| 贴图 | glTF 材质槽 | 用途 | 色彩空间 |
|---|---|---|---|
| `Default_albedo.jpg` | `baseColorTexture` | 基础颜色 | sRGB |
| `Default_metalRoughness.jpg` | `metallicRoughnessTexture` | 金属度和粗糙度 | Linear |
| `Default_normal.jpg` | `normalTexture` | 切线空间法线 | Linear |
| `Default_AO.jpg` | `occlusionTexture` | 环境遮蔽 | Linear |
| `Default_emissive.jpg` | `emissiveTexture` | 自发光颜色 | sRGB |

`metallicRoughnessTexture` 的通道约定：

| 通道 | 含义 |
|---|---|
| R | 通常不用 |
| G | roughness |
| B | metallic |
| A | 通常不用 |

这点非常容易写错。glTF 不是 `R=metallic, G=roughness`，而是 `G=roughness, B=metallic`。

## 4. 当前底层已经支持的能力

`vkglTF::Material` 当前已经有这些贴图字段：

```cpp
vkglTF::Texture* baseColorTexture;
vkglTF::Texture* metallicRoughnessTexture;
vkglTF::Texture* normalTexture;
vkglTF::Texture* occlusionTexture;
vkglTF::Texture* emissiveTexture;
```

也支持这些 descriptor flags：

```cpp
vkglTF::DescriptorBindingFlags::ImageBaseColor
vkglTF::DescriptorBindingFlags::ImageMetallicRoughness
vkglTF::DescriptorBindingFlags::ImageNormalMap
vkglTF::DescriptorBindingFlags::ImageOcclusionMap
vkglTF::DescriptorBindingFlags::ImageEmissiveMap
```

如果 Lab2 启用完整 PBR flags，material descriptor set 的 binding 约定如下：

| set | binding | 内容 |
|---|---:|---|
| set1 | 0 | baseColorTexture |
| set1 | 1 | metallicRoughnessTexture |
| set1 | 2 | normalTexture |
| set1 | 3 | occlusionTexture |
| set1 | 4 | emissiveTexture |

Lab2 自己的 scene 数据继续放在 set0：

| set | binding | 内容 |
|---|---:|---|
| set0 | 0 | camera/model matrices |
| set0 | 1 | lights |
| set0 | 2 | irradiance map |
| set0 | 3 | prefilter map |
| set0 | 4 | BRDF LUT |

## 5. Lab2 加载 DamagedHelmet

在 `loadAssets()` 中，先启用完整 glTF material flags，再加载模型。这个设置必须发生在 `objects[i].loadFromFile(...)` 之前。

```cpp
void VulkanExample::loadAssets() {
    vkglTF::descriptorBindingFlags =
        vkglTF::DescriptorBindingFlags::ImageBaseColor |
        vkglTF::DescriptorBindingFlags::ImageMetallicRoughness |
        vkglTF::DescriptorBindingFlags::ImageNormalMap |
        vkglTF::DescriptorBindingFlags::ImageOcclusionMap |
        vkglTF::DescriptorBindingFlags::ImageEmissiveMap;

    objectNames = {"DamagedHelmet"};
    objects.resize(objectNames.size());

    objects[0].loadFromFile(
        getAssetPath() + "models/DamagedHelmet/glTF/DamagedHelmet.gltf",
        vulkanDevice,
        queue,
        vkglTF::FileLoadingFlags::PreTransformVertices |
        vkglTF::FileLoadingFlags::FlipY
    );

    lightObject.loadFromFile(
        getAssetPath() + "models/sphere.gltf",
        vulkanDevice,
        queue,
        vkglTF::FileLoadingFlags::PreTransformVertices |
        vkglTF::FileLoadingFlags::FlipY
    );

    textures.environmentCubeMap.loadFromFile(
        getAssetPath() + hdrFilePath,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        vulkanDevice,
        queue
    );

    skyboxCube.loadFromFile(
        getAssetPath() + "models/cube.gltf",
        vulkanDevice,
        queue,
        vkglTF::FileLoadingFlags::PreTransformVertices |
        vkglTF::FileLoadingFlags::FlipY
    );
}
```

第一版建议只画一个 DamagedHelmet，不要继续画 7x7 材质球矩阵。这样能减少变量，方便定位贴图和 descriptor 问题。

## 6. Scene Pipeline Layout 使用 set0 + set1

`vkglTF::descriptorSetLayoutImage` 在模型加载时创建，代表 glTF material texture layout。你的 scene pipeline layout 需要同时包含 set0 和 set1：

```cpp
void VulkanExample::createScenePipelineLayout() {
    std::array<VkDescriptorSetLayout, 2> setLayouts = {
        descriptorSetLayouts.sceneDescriptorSetLayout,
        vkglTF::descriptorSetLayoutImage
    };

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo =
        vks::initializers::pipelineLayoutCreateInfo(
            setLayouts.data(),
            static_cast<uint32_t>(setLayouts.size())
        );

    std::vector<VkPushConstantRange> pushConstantRanges = {
        vks::initializers::pushConstantRange(
            VK_SHADER_STAGE_VERTEX_BIT,
            sizeof(glm::vec3),
            0
        )
    };

    pipelineLayoutCreateInfo.pushConstantRangeCount =
        static_cast<uint32_t>(pushConstantRanges.size());
    pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();

    VK_CHECK_RESULT(vkCreatePipelineLayout(
        device,
        &pipelineLayoutCreateInfo,
        nullptr,
        &pipelinesLayout.scenePipelineLayout
    ));
}
```

如果你还需要用 push constants 传 debug mode 或 emissive strength，可以继续加 fragment stage 的 push constant range，但材质颜色、roughness、metallic 不应该再用旧的手写 `Material::PushBlock`。

## 7. Scene Pipeline 顶点输入

PBR 贴图至少需要 UV，normal map 需要 tangent。建议直接把 scene pipeline 顶点输入改成：

```cpp
void VulkanExample::createScenePipeline() {
    vkutil::PipelineBuilder builder;

    builder.setPipelineLayout(pipelinesLayout.scenePipelineLayout)
        .setShaders(
            loadShader(getShadersPath() + pbrSceneVertexShader, VK_SHADER_STAGE_VERTEX_BIT),
            loadShader(getShadersPath() + pbrSceneFragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT))
        .setVertexInput(*vkglTF::Vertex::getPipelineVertexInputState({
            vkglTF::VertexComponent::Position,
            vkglTF::VertexComponent::Normal,
            vkglTF::VertexComponent::UV,
            vkglTF::VertexComponent::Tangent
        }))
        .setColorAttachmentFormat(swapChain.colorFormat)
        .setDepthFormat(depthFormat)
        .enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .disableBlending();

    pipelines.scenePipeline = builder.build(device, pipelineCache);
}
```

如果你还没准备接 normal map，也可以先只加 `UV`。但 DamagedHelmet 本身有 tangent，最终还是建议接上。

## 8. 绘制时绑定材质贴图

关键调用是 `vkglTF::RenderFlags::BindImages`。它会让 `drawNode()` 在每个 primitive 绘制前自动绑定该 primitive 对应材质的 descriptor set。

```cpp
void VulkanExample::cmdDrawSecne(VkCommandBuffer cmd) {
    VkRenderingAttachmentInfo colorAttachment = vkutil::renderingAttachmentInfo(
        swapChain.imageViews[currentImageIndex],
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        VkClearValue{{0.01f, 0.02f, 0.025f, 1.0f}}
    );

    VkRenderingAttachmentInfo depthAttachment = vkutil::renderingdepthAttachmentInfo(
        depthStencil.view,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        1.0f
    );

    VkExtent2D extent{width, height};
    vkutil::cmdBeginColorDepthRendering(cmd, extent, colorAttachment, depthAttachment);
    {
        vkutil::cmdSetViewportAndScissor(cmd, extent.width, extent.height);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.scenePipeline);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelinesLayout.scenePipelineLayout,
            0,
            1,
            &descriptorSets[currentBuffer].sceneDescriptor,
            0,
            nullptr
        );

        objectPos = glm::vec3(0.0f);
        vkCmdPushConstants(
            cmd,
            pipelinesLayout.scenePipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(glm::vec3),
            &objectPos
        );

        objects[objectIndex].draw(
            cmd,
            vkglTF::RenderFlags::BindImages,
            pipelinesLayout.scenePipelineLayout,
            1
        );
    }
    vkutil::cmdEndRendering(cmd);
}
```

最后一个参数 `1` 表示 material descriptor set 绑定到 `set = 1`。如果你写成 `objects[objectIndex].draw(cmd)`，shader 中的 `space1` 贴图就不会被绑定。

## 9. Vertex Shader 示例

`pbrScene.vert` 需要把 UV 和 tangent 传给 fragment shader：

```hlsl
struct VSInput {
    [[vk::location(0)]] float3 pos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float4 tangent : TANGENT0;
};

struct VSOutput {
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float4 worldPos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float4 tangent : TANGENT0;
};

struct UBOMatrix {
    float4x4 projection;
    float4x4 model;
    float4x4 view;
};
ConstantBuffer<UBOMatrix> matrices : register(b0, space0);

struct Pushconstants {
    float3 posOffset;
};
[[vk::push_constant]] Pushconstants pushconstants;

VSOutput main(VSInput input) {
    VSOutput output;

    float4 worldPos = mul(matrices.model, float4(input.pos, 1.0));
    worldPos += float4(pushconstants.posOffset, 0.0);

    output.pos = mul(matrices.projection, mul(matrices.view, worldPos));
    output.worldPos = worldPos;
    output.normal = normalize(mul((float3x3)matrices.model, input.normal));
    output.tangent = float4(normalize(mul((float3x3)matrices.model, input.tangent.xyz)), input.tangent.w);
    output.uv = input.uv;

    return output;
}
```

## 10. Fragment Shader 贴图声明

set0 仍然是 Lab2 的 scene 资源：

```hlsl
ConstantBuffer<UBOMatrix> matrices : register(b0, space0);
ConstantBuffer<UBOLight> light : register(b1, space0);

TextureCube irradianceMap : register(t2, space0);
SamplerState irradianceMapSampler : register(s2, space0);

TextureCube prefilterMap : register(t3, space0);
SamplerState prefilterMapSampler : register(s3, space0);

Texture2D brdfLUT : register(t4, space0);
SamplerState brdfLUTSampler : register(s4, space0);
```

set1 是 glTF material 贴图：

```hlsl
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

## 11. PBR 贴图采样函数

建议先把贴图采样整理成一个小函数，避免 `main()` 变得太乱。

```hlsl
struct FSInput {
    [[vk::location(0)]] float4 worldPos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float4 tangent : TANGENT0;
};

struct PBRMaterialSample {
    float3 albedo;
    float roughness;
    float metallic;
    float ao;
    float3 emissive;
    float alpha;
};

PBRMaterialSample samplePBRMaterial(float2 uv) {
    PBRMaterialSample mat;

    float4 baseColor = baseColorMap.Sample(baseColorSampler, uv);
    float4 mr = metallicRoughnessMap.Sample(metallicRoughnessSampler, uv);
    float ao = occlusionMap.Sample(occlusionSampler, uv).r;
    float3 emissive = emissiveMap.Sample(emissiveSampler, uv).rgb;

    // 当前 loader 对 jpg/png 使用 UNORM，不会自动 sRGB decode。
    mat.albedo = pow(saturate(baseColor.rgb), 2.2);
    mat.alpha = baseColor.a;

    // glTF metallic-roughness: G = roughness, B = metallic。
    mat.roughness = clamp(mr.g, 0.04, 1.0);
    mat.metallic = saturate(mr.b);
    mat.ao = saturate(ao);

    // emissive 也是颜色贴图，按 sRGB 输入处理。
    mat.emissive = pow(saturate(emissive), 2.2);

    return mat;
}
```

如果未来 texture loader 能按材质用途区分 `SRGB/UNORM` 格式，那么这里的 `pow(..., 2.2)` 就应该移除，避免重复解码。

## 12. Normal Map 采样函数

Normal map 是 tangent-space normal，需要用 TBN 转到 world space。

```hlsl
float3 getWorldNormal(FSInput input) {
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent.xyz);

    // Gram-Schmidt 正交化，减少插值和模型矩阵带来的误差。
    T = normalize(T - N * dot(N, T));

    float3 B = normalize(cross(N, T) * input.tangent.w);
    float3 tangentNormal = normalMap.Sample(normalSampler, input.uv).xyz * 2.0 - 1.0;

    // 如果凹凸方向明显反了，可以尝试打开这一行。
    // tangentNormal.y *= -1.0;

    return normalize(mul(tangentNormal, float3x3(T, B, N)));
}
```

如果 normal map 出现明显接缝，优先检查这些点：

| 现象 | 可能原因 |
|---|---|
| 凹凸方向反了 | normal map green channel 方向不一致 |
| 半边球/模型反转 | TBN 的 `mul` 方向或 `tangent.w` 处理不对 |
| 接缝很明显 | `PreTransformVertices` 处理 normal 但没有同步处理 tangent |
| 法线随机闪烁 | pipeline vertex input location 和 HLSL location 不一致 |

## 13. 在 PBR 主流程中使用贴图

下面是把 glTF PBR 贴图接到你现有 Cook-Torrance + IBL 结构里的示例：

```hlsl
FSOutput main(FSInput input) {
    FSOutput output;

    PBRMaterialSample mat = samplePBRMaterial(input.uv);

    float3 N = getWorldNormal(input);
    float3 V = normalize(matrices.camPos - input.worldPos.xyz);
    float3 F0 = lerp(0.04.xxx, mat.albedo, mat.metallic);

    float3 diffuseIBL = computeDiffuseIBL(N, V, mat.albedo, mat.metallic, mat.roughness, F0);
    float3 specularIBL = computeSpecularIBL(N, V, F0, mat.roughness);

    // AO 先只压 diffuse IBL，避免把镜面环境反射压得太死。
    float3 ambient = diffuseIBL * mat.ao + specularIBL;

    float3 Lo = 0.0.xxx;
    for (int i = 0; i < 4; ++i) {
        float3 L = normalize(light.lightsPos[i].xyz - input.worldPos.xyz);
        float3 H = normalize(V + L);

        float distance = length(light.lightsPos[i].xyz - input.worldPos.xyz);
        float attenuation = 1.0 / max(distance * distance, 0.01);
        float3 radiance = light.lightsColor[i].xyz * light.lightIntensity[i].xyz * attenuation;

        float NdotL = max(dot(N, L), 0.0);
        float3 brdf = direcBRDF(N, V, L, H, mat.roughness, mat.metallic, mat.albedo, F0);

        Lo += radiance * brdf * NdotL;
    }

    float emissiveStrength = 1.0;
    float3 color = ambient + Lo + mat.emissive * emissiveStrength;

    color = color / (color + 1.0);
    color = pow(saturate(color), 1.0 / 2.2);

    output.color = float4(color, mat.alpha);
    return output;
}
```

如果 emissive 不明显，可以临时把 `emissiveStrength` 调到 `3.0` 或 `5.0`。后续更好的方式是把它放进 UI 或 UBO。

## 14. 自发光和 Bloom 的区别

`emissive` 只表示材质自己发亮，它不会自动产生光晕，也不会照亮周围物体。

如果想做 bloom，需要后处理：

```text
PBR scene -> HDR color image
    -> bright pass
    -> downsample / blur
    -> upsample / composite
    -> tone mapping
    -> swapchain
```

所以本阶段只需要把 emissive 加进最终颜色。等 Lab2 PBR 材质稳定后，再考虑 HDR offscreen + bloom。

## 15. 调试顺序

不要一上来就看最终 PBR。建议加 debug mode 分层检查：

| Debug Mode | 输出 |
|---|---|
| UV | `float4(input.uv, 0, 1)` |
| BaseColor | `baseColorMap.Sample(...).rgb` |
| Roughness | `mr.g.xxx` |
| Metallic | `mr.b.xxx` |
| AO | `ao.xxx` |
| Emissive | `emissive.rgb` |
| VertexNormal | `normalize(input.normal) * 0.5 + 0.5` |
| NormalMap | `getWorldNormal(input) * 0.5 + 0.5` |
| FinalPBR | 最终结果 |

出现问题时优先看 debug mode，不要直接猜 BRDF。

## 16. 常见问题

| 现象 | 高概率原因 | 解决 |
|---|---|---|
| 模型加载失败 | 没下载完整 `glTF` 文件夹 | 确认 `.gltf/.bin/jpg` 都在同一目录 |
| 贴图全黑 | 没有绑定 set1 | draw 使用 `RenderFlags::BindImages` |
| validation 报 set/layout 不匹配 | pipeline layout 只有 set0 | scene pipeline layout 加 `vkglTF::descriptorSetLayoutImage` |
| 颜色发灰 | baseColor 没做 sRGB decode | shader 中 `pow(baseColor, 2.2)` |
| 金属/粗糙度反了 | MR 通道读错 | `roughness = G`，`metallic = B` |
| AO 过黑 | AO 乘了 direct light 或整个 specular | 先只乘 diffuse IBL |
| normal map 乱 | TBN、tangent.w、FlipY 问题 | 输出 normal debug view |
| emissive 没光晕 | 没做 bloom | 当前阶段正常，后续做 HDR 后处理 |

## 17. 当前实现的一个 TODO

为了保证 descriptor 有效，当前缺失贴图会回退到原项目已有的 `emptyTexture`。这能避免 descriptor 未写入导致的错误，但它还不是严格语义默认值。

长期更好的默认贴图应该是：

| 贴图 | 默认值 |
|---|---|
| baseColor | 白色 |
| metallicRoughness | roughness=1, metallic=0 |
| normal | `(0.5, 0.5, 1.0)` |
| occlusion | 白色 |
| emissive | 黑色 |

DamagedHelmet 贴图齐全，所以当前阶段可以先不处理这个 TODO。等你要加载更多随机 glTF 资产时，再补一套语义默认贴图会更稳。

