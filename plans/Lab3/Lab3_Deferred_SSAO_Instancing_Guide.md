# Lab3 指导路线：Deferred Rendering + SSAO + Instancing

这份文档是我给 Lab3 做实现前规划用的。Lab2 已经完成了 PBR、IBL、PBR Texture、HDR、Physically Based Bloom，所以 Lab3 最适合继续向“现代实时渲染管线”推进：把 forward PBR 改造成 deferred rendering，再在屏幕空间里加入 SSAO，最后用 instancing 提升大量重复物体的绘制效率。

我希望 Lab3 最终不是只把三个功能分别做出来，而是形成一条完整的数据流：

```text
Instanced Geometry Pass
    -> G-Buffer
    -> SSAO Pass
    -> SSAO Blur Pass
    -> Deferred PBR Lighting Pass
    -> HDR Post Process / Bloom / Tone Mapping
    -> Swapchain
```

这条线里，延迟渲染负责把“几何信息”和“光照计算”拆开；SSAO 负责利用 G-Buffer 中的深度、法线信息估计局部遮蔽；实例化负责让大量相同 mesh 以较低 CPU 开销提交到 GPU。

## 1. Lab3 的目标定位

Lab3 的核心目标可以拆成三块：

1. **Deferred Rendering**
   - 第一遍只写 G-Buffer，不在材质 shader 里直接算完整光照。
   - 第二遍读取 G-Buffer，在全屏 pass 里统一做 PBR 直接光照、IBL、AO 混合和 HDR 输出。

2. **SSAO**
   - 使用深度和法线在 view space 里重建局部几何关系。
   - 对每个像素周围采样一组半球 sample kernel，估计该点附近是否被遮挡。
   - 对 SSAO 结果做 blur，降低随机噪声。

3. **Instancing**
   - 对大量相同模型，避免每个物体都单独 bind/draw。
   - 用 `SV_InstanceID` 从 instance buffer 中取 model matrix、材质索引、颜色等 per-instance 数据。
   - 最终做到：一个 mesh 一次 draw call 绘制 N 个实例。

我的建议是：**先做 Deferred，再做 SSAO，最后做 Instancing**。原因是 SSAO 依赖 G-Buffer，而 Instancing 主要影响 Geometry Pass 的数据提交方式。如果顺序反过来，很容易同时处理太多变量。

## 2. 总体渲染数据流

推荐 Lab3 第一版保留 Lab2 的 HDR/post-process 思路，但把场景主渲染拆成 G-Buffer 和 Deferred Lighting：

```text
CPU 更新 Camera / Lights / Instances
        |
        v
Geometry Pass:
    mesh / glTF / instanced mesh
        -> gAlbedoMetallic
        -> gNormalRoughness
        -> gEmissiveAO
        -> gDepth
        |
        v
SSAO Pass:
    gDepth + gNormalRoughness + noise texture + sample kernel
        -> ssaoRaw
        |
        v
SSAO Blur Pass:
    ssaoRaw + gDepth / gNormal
        -> ssaoBlurred
        |
        v
Deferred Lighting Pass:
    G-Buffer + ssaoBlurred + lights + IBL
        -> hdrSceneColor
        |
        v
Bloom / Composite:
    hdrSceneColor
        -> bloom mip chain
        -> tone mapping + gamma
        -> swapchain
```

第一版可以先不接 Bloom，只把 `hdrSceneColor -> tone mapping -> swapchain` 跑通。等 Deferred 和 SSAO 稳定后，再把 Lab2 的 Bloom 链路接回来。

## 3. 建议的里程碑

### Milestone 0：搭 Lab3 工程骨架

目标：从 Lab2 复制出 Lab3，但先删掉不需要的实验性逻辑，让主循环更干净。

建议文件：

```text
examples/lab3/lab3.h
examples/lab3/lab3.cpp
shaders/hlsl/lab3/gbuffer.vert
shaders/hlsl/lab3/gbuffer.frag
shaders/hlsl/lab3/deferredLighting.frag
shaders/hlsl/lab3/ssao.frag
shaders/hlsl/lab3/ssaoBlur.frag
shaders/hlsl/lab3/fullscreen.vert
shaders/hlsl/lab3/composite.frag
```

这个阶段不要急着做 SSAO 和实例化，只需要确认：

- Lab3 可以独立编译、运行。
- 场景相机、模型加载、材质贴图可以复用 Lab2 的结构。
- HDR render target 和 fullscreen pass 可以正常工作。

### Milestone 1：G-Buffer Pass 跑通

目标：把场景画到多张 offscreen color attachment 和 depth attachment。

推荐先输出调试色：

```text
gAlbedoMetallic.rgb = albedo
gAlbedoMetallic.a   = metallic

gNormalRoughness.rgb = encoded/view-space normal
gNormalRoughness.a   = roughness

gEmissiveAO.rgb = emissive
gEmissiveAO.a   = material AO
```

这个阶段要做多个 debug view：

```text
DebugMode 0: 最终 deferred lighting
DebugMode 1: albedo
DebugMode 2: normal
DebugMode 3: roughness
DebugMode 4: metallic
DebugMode 5: depth
```

如果 debug view 没做好，后面 SSAO 出问题会很难定位。延迟渲染最怕“黑箱调参”，所以 G-Buffer 可视化一定要早做。

### Milestone 2：Deferred Lighting Pass 跑通

目标：全屏 pass 读取 G-Buffer，并恢复 Lab2 的 PBR 光照效果。

这一阶段先只做直接光照：

```text
G-Buffer -> reconstruct material inputs -> Cook-Torrance direct lighting -> hdrSceneColor
```

然后再加 IBL：

```text
direct light + diffuse IBL + specular IBL + emissive
```

最后接 tone mapping。注意：**tone mapping 仍然只能在最终 composite pass 做一次**，不能在 G-Buffer pass 或 deferredLighting pass 里提前做。

### Milestone 3：SSAO Raw Pass

目标：用 G-Buffer 的 depth 和 normal 生成一张粗糙、有噪声但方向正确的 AO 图。

第一版不用追求好看，只要满足：

- 接触区域变暗。
- 大平面基本不乱黑。
- 相机移动时 SSAO 不明显闪烁。
- 半径和 bias 调参有可预期效果。

### Milestone 4：SSAO Blur Pass

目标：把 SSAO raw 中的噪声压下去。

第一版可以做普通 4x4 或 5x5 blur。之后再升级成 bilateral blur：

```text
如果邻近像素深度差太大，就降低它的权重
如果邻近像素法线差太大，也降低它的权重
```

这样可以避免 AO 被模糊到物体边缘外面，减少“黑边扩散”。

### Milestone 5：Instancing

目标：让大量相同 mesh 可以通过一次 draw call 绘制。

先做最简单版本：

```text
同一个 sphere mesh
同一套材质
N 个 model matrix
一次 vkCmdDrawIndexed(..., instanceCount = N, ...)
```

再做增强版本：

```text
每个 instance 有自己的 materialIndex
每个 instance 有自己的颜色、粗糙度偏移、金属度偏移
每个 instance 可以独立开关 emissive 或 light proxy visual
```

### Milestone 6：整合 Lab2 Bloom 和最终 polish

目标：把 Lab2 的 Bloom、HDR、Tone Mapping 接到 Deferred Lighting 输出之后。

最终渲染链路应该是：

```text
G-Buffer
    -> SSAO
    -> Deferred Lighting to HDR
    -> Bloom
    -> Composite
```

## 4. G-Buffer 设计

### 4.1 第一版推荐格式

Lab3 不建议一开始就过度压缩 G-Buffer。学习阶段要优先保证结果稳定和易调试。

推荐第一版：

```cpp
struct GBuffer {
    Texture albedoMetallic;   // VK_FORMAT_R8G8B8A8_UNORM 或 VK_FORMAT_R16G16B16A16_SFLOAT
    Texture normalRoughness;  // VK_FORMAT_R16G16B16A16_SFLOAT
    Texture emissiveAO;       // VK_FORMAT_R16G16B16A16_SFLOAT
    Texture depth;            // VK_FORMAT_D32_SFLOAT
};
```

各 attachment 含义：

```text
albedoMetallic.rgb:
    线性空间 albedo

albedoMetallic.a:
    metallic

normalRoughness.rgb:
    view-space normal 或 world-space normal

normalRoughness.a:
    roughness

emissiveAO.rgb:
    emissive color * emissive strength

emissiveAO.a:
    material AO

depth:
    hardware depth
```

我更建议 **normal 存 view space**，因为 SSAO 通常在 view space 中做，采样方向和深度重建会更直接。Deferred Lighting 如果想在 world space 中算光照，也可以把 light position 变到 view space 后统一计算。

### 4.2 是否存 position？

有两种路线。

路线 A：存 view-space position。

```text
优点：实现 SSAO 最简单，shader 里不用从 depth 重建 position。
缺点：多一张 R16G16B16A16_SFLOAT G-Buffer，显存和带宽开销大。
```

路线 B：只存 depth，在 SSAO 和 Lighting 中重建 position。

```text
优点：更接近现代引擎做法，G-Buffer 更省。
缺点：需要正确处理投影矩阵逆变换、NDC 深度、Vulkan 坐标约定。
```

我的建议：

```text
Lab3 初期：
    可以临时存 view-space position，先把 SSAO 算法理解清楚。

Lab3 稳定后：
    删除 position buffer，改成 depth reconstruction。
```

这样做学习曲线更平滑，也方便对比“存 position”和“重建 position”的结果是否一致。

### 4.3 G-Buffer Fragment Shader 示例

伪代码：

```hlsl
struct FSInput {
    float3 worldPos   : TEXCOORD0;
    float3 viewPos    : TEXCOORD1;
    float3 normal     : TEXCOORD2;
    float2 uv         : TEXCOORD3;
    float3 tangent    : TEXCOORD4;
    float3 bitangent  : TEXCOORD5;
};

struct GBufferOutput {
    float4 albedoMetallic  : SV_TARGET0;
    float4 normalRoughness : SV_TARGET1;
    float4 emissiveAO      : SV_TARGET2;
};

GBufferOutput main(FSInput input) {
    MaterialData mat = SampleMaterial(input.uv);

    float3 N = normalize(input.normal);

    if (mat.hasNormalMap) {
        float3 tangentNormal = normalTexture.Sample(samplerLinear, input.uv).xyz * 2.0 - 1.0;
        float3 T = normalize(input.tangent);
        float3 B = normalize(input.bitangent);
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(tangentNormal, TBN));
    }

    // 推荐第一版存 view-space normal，方便 SSAO。
    float3 viewNormal = normalize(mul((float3x3)viewMatrix, N));

    GBufferOutput output;
    output.albedoMetallic  = float4(mat.albedo, mat.metallic);
    output.normalRoughness = float4(viewNormal * 0.5 + 0.5, mat.roughness);
    output.emissiveAO      = float4(mat.emissive, mat.ao);
    return output;
}
```

注意点：

- 如果 G-Buffer 存的是 normal，通常要从 `[-1, 1]` encode 到 `[0, 1]`。
- Lighting pass 读取时要 decode：`N = tex.rgb * 2.0 - 1.0`。
- albedo 进入 G-Buffer 前应该是 **linear color**，不要把 sRGB 值直接当线性值存进去。
- roughness 最好 clamp 到 `[0.04, 1.0]`，避免过小导致高光异常尖锐。

## 5. Deferred Lighting 设计

Deferred Lighting 是 Lab3 的核心 pass。它不再处理 mesh 顶点，也不再重新采样所有材质贴图，而是读取 G-Buffer 还原 PBR 输入。

### 5.1 输入资源

Deferred Lighting 需要：

```text
binding 0: gAlbedoMetallic
binding 1: gNormalRoughness
binding 2: gEmissiveAO
binding 3: gDepth
binding 4: ssaoBlurred
binding 5: irradiance cubemap
binding 6: prefiltered environment cubemap
binding 7: BRDF LUT
binding 8: lights UBO / SSBO
binding 9: camera UBO
```

第一版可以先不接 SSAO：

```text
ambientAO = 1.0
```

等 SSAO pass 通了，再把 `ambientAO` 替换成 `ssaoBlurred`。

### 5.2 光照空间选择

有两个常见选择：

1. **World-space lighting**
   - G-Buffer 存 world normal。
   - depth 重建 world position。
   - light position、camera position 本来就是 world space。

2. **View-space lighting**
   - G-Buffer 存 view normal。
   - depth 重建 view position。
   - light position 需要乘 view matrix 变到 view space。

SSAO 更偏 view space，PBR 更常见 world space。为了 Lab3 简化，我建议：

```text
G-Buffer:
    存 view-space normal。

SSAO:
    使用 view-space position 和 view-space normal。

Deferred Lighting:
    第一版也使用 view-space lighting。
    CPU 或 shader 中把 light position 转成 view space。
```

这样能减少空间转换错误。等结果稳定后，再考虑把 deferred lighting 改回 world space。

### 5.3 Lighting Fragment Shader 伪代码

```hlsl
float3 albedo = gAlbedoMetallic.Sample(samplerNearest, uv).rgb;
float metallic = gAlbedoMetallic.Sample(samplerNearest, uv).a;

float4 nr = gNormalRoughness.Sample(samplerNearest, uv);
float3 N = normalize(nr.rgb * 2.0 - 1.0);
float roughness = nr.a;

float4 emissiveAO = gEmissiveAO.Sample(samplerNearest, uv);
float3 emissive = emissiveAO.rgb;
float materialAO = emissiveAO.a;

float depth = depthTexture.Sample(samplerNearest, uv).r;
float3 P = ReconstructViewPosition(uv, depth);
float3 V = normalize(-P);

float3 F0 = lerp(0.04.xxx, albedo, metallic);

float3 direct = 0.0.xxx;
for (int i = 0; i < lightCount; ++i) {
    float3 Lpos = lights[i].viewPosition.xyz;
    float3 L = normalize(Lpos - P);
    float distance = length(Lpos - P);
    float attenuation = 1.0 / max(distance * distance, 0.01);
    float3 radiance = lights[i].color.rgb * lights[i].intensity.rgb * attenuation;

    direct += EvalCookTorrance(albedo, metallic, roughness, F0, N, V, L, radiance);
}

float ssao = ssaoTexture.Sample(samplerLinear, uv).r;
float ambientAO = materialAO * ssao;

float3 ibl = EvalIBL(albedo, metallic, roughness, F0, N, V);

// AO 通常主要作用于间接光，不建议直接把 direct light 也乘黑。
float3 color = direct + ibl * ambientAO + emissive;
output.color = float4(color, 1.0);
```

关键点：

- SSAO 主要压暗环境光 / 间接光，不应该简单粗暴地把整个最终颜色都乘 SSAO。
- 直接光照是否被遮挡应该由 shadow map / ray tracing / visibility 负责，不是 SSAO 的主要职责。
- emissive 通常不乘 AO，因为自发光物体不应该因为环境遮蔽而变暗。

## 6. Depth Reconstruction

如果只存 depth，不存 position，就需要在 SSAO 和 lighting pass 中从屏幕 UV + depth 重建 view-space position。

伪代码：

```hlsl
float3 ReconstructViewPosition(float2 uv, float depth) {
    float4 clip;
    clip.x = uv.x * 2.0 - 1.0;
    clip.y = uv.y * 2.0 - 1.0;
    clip.z = depth;
    clip.w = 1.0;

    float4 view = mul(invProjection, clip);
    view.xyz /= view.w;
    return view.xyz;
}
```

Vulkan 里要特别注意：

- Vulkan NDC depth 是 `[0, 1]`，不是 OpenGL 的 `[-1, 1]`。
- 如果工程里投影矩阵已经做过 Y 翻转，不要在 shader 里重复翻转。
- 如果使用 reversed-Z，重建公式和 depth compare 逻辑都要对应调整。

建议先做一个 debug view：

```text
用重建出来的 viewPos.z 显示灰度深度
用重建出来的 normal 显示 RGB
```

这一步能快速发现投影矩阵、UV、depth range 是否有问题。

## 7. SSAO 设计

### 7.1 SSAO 的直觉

SSAO 的直觉是：

```text
对于当前像素 P，
沿着法线方向附近的半球采样很多点。
如果这些采样点在 depth buffer 里被更近的几何挡住，
说明 P 附近比较“封闭”，环境光应该更暗。
```

也就是说，SSAO 不是精确阴影，而是屏幕空间的局部遮蔽估计。

它的特点：

```text
优点：
    不需要真实几何光线追踪，成本低。
    能明显增强接触阴影、缝隙、角落的体积感。

缺点：
    只能看到屏幕上已有的信息。
    屏幕外物体不会参与遮蔽。
    参数不好会产生黑边、噪声、闪烁、halo。
```

### 7.2 SSAO 资源

需要准备：

```cpp
struct SSAOResources {
    Texture raw;       // VK_FORMAT_R8_UNORM 或 VK_FORMAT_R16_SFLOAT
    Texture blurred;   // VK_FORMAT_R8_UNORM 或 VK_FORMAT_R16_SFLOAT
    Texture noise;     // 4x4 random rotation texture
    Buffer kernelUBO;  // sample kernel
};
```

推荐参数：

```cpp
const uint32_t kernelSize = 64;
const float radius = 0.5f;
const float bias = 0.025f;
```

noise texture 可以是 4x4，每个 texel 存一个随机方向：

```text
noise.xyz = normalize(float3(random(-1, 1), random(-1, 1), 0.0))
```

采样时把 4x4 noise tile 到整个屏幕，用来让每个像素的采样半球旋转方向略有不同，避免明显条纹。

### 7.3 Sample Kernel 生成

CPU 生成半球采样点：

```cpp
std::vector<glm::vec4> ssaoKernel;

for (uint32_t i = 0; i < kernelSize; ++i) {
    glm::vec3 sample(
        randomFloat(-1.0f, 1.0f),
        randomFloat(-1.0f, 1.0f),
        randomFloat(0.0f, 1.0f)
    );

    sample = glm::normalize(sample);
    sample *= randomFloat(0.0f, 1.0f);

    float scale = static_cast<float>(i) / static_cast<float>(kernelSize);
    scale = glm::mix(0.1f, 1.0f, scale * scale);
    sample *= scale;

    ssaoKernel.push_back(glm::vec4(sample, 0.0f));
}
```

这个 `scale * scale` 的作用是让更多 sample 靠近原点。近处遮蔽对 AO 更重要，远处样本太多会让结果变脏。

### 7.4 SSAO Fragment Shader 伪代码

```hlsl
float depth = gDepth.Sample(samplerNearest, uv).r;
float3 fragPos = ReconstructViewPosition(uv, depth);

float3 normal = gNormalRoughness.Sample(samplerNearest, uv).rgb;
normal = normalize(normal * 2.0 - 1.0);

float3 randomVec = noiseTexture.Sample(samplerRepeatNearest, uv * noiseScale).xyz;
randomVec = normalize(randomVec * 2.0 - 1.0);

float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
float3 bitangent = cross(normal, tangent);
float3x3 TBN = float3x3(tangent, bitangent, normal);

float occlusion = 0.0;

for (int i = 0; i < kernelSize; ++i) {
    float3 samplePos = fragPos + mul(kernel[i].xyz, TBN) * radius;

    float4 offset = float4(samplePos, 1.0);
    offset = mul(projection, offset);
    offset.xyz /= offset.w;

    float2 sampleUV = offset.xy * 0.5 + 0.5;

    float sampleDepth = gDepth.Sample(samplerNearest, sampleUV).r;
    float3 sampleViewPos = ReconstructViewPosition(sampleUV, sampleDepth);

    float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleViewPos.z));

    // view space 下通常 z 越大/越小要看你的相机约定，这里只表达逻辑。
    if (sampleViewPos.z >= samplePos.z + bias) {
        occlusion += rangeCheck;
    }
}

occlusion = 1.0 - occlusion / kernelSize;
output.color = occlusion;
```

这里最容易出错的是 `z` 的比较方向。不同矩阵约定下 view space 的前方可能是 `-Z`，所以实际代码里要结合你的工程验证。判断标准很简单：

```text
物体接触地面的地方应该变暗。
孤立平面不应该整片变黑。
调大 radius 时影响范围变大。
调大 bias 时自遮蔽减少。
```

### 7.5 SSAO 参数建议

第一版 UI 参数：

```cpp
struct SSAOSettings {
    int enabled = 1;
    int kernelSize = 64;
    float radius = 0.5f;
    float bias = 0.025f;
    float power = 1.5f;
};
```

shader 最后可以加：

```hlsl
occlusion = pow(saturate(occlusion), ssaoPower);
```

参数直觉：

```text
radius:
    控制 AO 搜索范围。
    太小只剩接触黑边，太大容易脏。

bias:
    防止平面自遮蔽。
    太小会大面积发黑，太大会丢失细节。

power:
    控制 AO 对比度。
    太大容易变成厚重黑线。

kernelSize:
    越大越稳定，但越贵。
    32 是轻量版，64 是常见质量档。
```

## 8. SSAO Blur 设计

SSAO raw 会有明显噪声，所以需要 blur。

### 8.1 普通 Blur

第一版可以这样：

```hlsl
float result = 0.0;

for (int x = -2; x <= 2; ++x) {
    for (int y = -2; y <= 2; ++y) {
        float2 offset = float2(x, y) * texelSize;
        result += ssaoRaw.Sample(samplerLinear, uv + offset).r;
    }
}

result /= 25.0;
```

这很容易实现，但会把 AO 模糊到边缘外。

### 8.2 Bilateral Blur

更推荐最终做 bilateral blur：

```hlsl
float centerDepth = depth.Sample(samplerNearest, uv).r;
float3 centerNormal = DecodeNormal(normalTexture.Sample(samplerNearest, uv).rgb);

float sum = 0.0;
float weightSum = 0.0;

for each neighbor {
    float neighborAO = ssaoRaw.Sample(...).r;
    float neighborDepth = depth.Sample(...).r;
    float3 neighborNormal = DecodeNormal(normalTexture.Sample(...).rgb);

    float depthWeight = exp(-abs(centerDepth - neighborDepth) * depthSharpness);
    float normalWeight = saturate(dot(centerNormal, neighborNormal));
    float weight = spatialWeight * depthWeight * normalWeight;

    sum += neighborAO * weight;
    weightSum += weight;
}

output = sum / max(weightSum, 0.0001);
```

这样可以保护几何边缘，减少“AO 漏到背景上”的问题。

## 9. Instancing 设计

### 9.1 为什么要做 Instancing

在 Lab2 里，如果我画很多球体，很可能是循环中多次更新 push constant、多次 draw：

```cpp
for (int i = 0; i < objectCount; ++i) {
    vkCmdPushConstants(... modelMatrix ...);
    vkCmdDrawIndexed(...);
}
```

这对几十个物体没问题，但如果是几百、几千个重复 mesh，CPU draw call 开销会越来越明显。

Instancing 的思路是：

```text
同一个 vertex/index buffer 绑定一次
instance 数据放到 buffer 里
shader 用 SV_InstanceID 取当前实例的数据
vkCmdDrawIndexed 的 instanceCount 设置为 N
```

### 9.2 Instance Data 设计

推荐第一版：

```cpp
struct InstanceData {
    glm::mat4 model;
    glm::mat4 normalMatrix;
    glm::vec4 color;
    glm::vec4 materialParams; // x metallic, y roughness, z emissiveStrength, w materialIndex
};
```

如果想更省，可以不存 `normalMatrix`，在 shader 里从 model 计算。但学习阶段建议先存，减少 shader 复杂度。

实例 buffer 推荐用 storage buffer：

```cpp
struct InstanceResources {
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint32_t count;
};
```

HLSL：

```hlsl
struct InstanceData {
    float4x4 model;
    float4x4 normalMatrix;
    float4 color;
    float4 materialParams;
};

StructuredBuffer<InstanceData> instances : register(t0, space2);
```

Vertex shader：

```hlsl
VSOutput main(VSInput input, uint instanceID : SV_InstanceID) {
    InstanceData inst = instances[instanceID];

    float4 worldPos = mul(inst.model, float4(input.pos, 1.0));
    output.position = mul(camera.proj, mul(camera.view, worldPos));

    float3 worldNormal = normalize(mul((float3x3)inst.normalMatrix, input.normal));

    output.worldPos = worldPos.xyz;
    output.normal = worldNormal;
    output.instanceColor = inst.color.rgb;
    output.materialParams = inst.materialParams;
    return output;
}
```

### 9.3 Instancing 与 G-Buffer 的关系

Instancing 只影响 Geometry Pass。也就是说：

```text
普通 mesh:
    gbuffer.vert + gbuffer.frag

instanced mesh:
    gbuffer_instanced.vert + 同一个 gbuffer.frag
```

Fragment shader 不需要知道这个像素来自普通 draw 还是 instance draw。它只负责把材质信息写进 G-Buffer。

建议第一版把普通物体和实例物体分成两个 pipeline：

```text
gbufferPipeline
gbufferInstancedPipeline
```

后续如果 vertex input / descriptor 能统一，再合并。

### 9.4 Instance Buffer 更新策略

静态实例：

```text
创建时上传一次到 device local buffer。
之后每帧不更新。
```

动态实例：

```text
每帧更新 staging / mapped buffer。
最好每个 frame-in-flight 一份 instance buffer，避免 GPU 还在读上一帧时 CPU 覆盖。
```

Lab3 第一版建议：

```text
先做静态 instance buffer。
等渲染稳定后，再做动态更新。
```

## 10. Vulkan 资源和描述符规划

### 10.1 资源结构建议

```cpp
struct GBufferResources {
    vks::Texture2D albedoMetallic;
    vks::Texture2D normalRoughness;
    vks::Texture2D emissiveAO;
    vks::Texture2D depth;
    VkExtent2D extent;
};

struct SSAOResources {
    vks::Texture2D raw;
    vks::Texture2D blurred;
    vks::Texture2D noise;
    vks::Buffer kernelBuffer;
    VkExtent2D extent;
};

struct DeferredResources {
    vks::Texture2D hdrSceneColor;
};
```

如果项目保持 `maxConcurrentFrames > 1`，严格来说这些 offscreen render target 最好做成 per-frame：

```cpp
std::array<GBufferResources, maxConcurrentFrames> gbuffer;
std::array<SSAOResources, maxConcurrentFrames> ssao;
std::array<DeferredResources, maxConcurrentFrames> deferred;
```

如果暂时只保留一份，也要确认 command buffer 和 frame overlap 不会导致同一张 image 同时被上一帧读、下一帧写。

### 10.2 描述符集合建议

可以按 pass 拆：

```text
set 0: camera / scene UBO
set 1: material textures
set 2: instance buffer
set 3: G-Buffer inputs
set 4: SSAO inputs
set 5: IBL resources
```

更工程化一点：

```cpp
struct DescriptorSets {
    VkDescriptorSet cameraSet;
    VkDescriptorSet materialSet;
    VkDescriptorSet instanceSet;
    VkDescriptorSet ssaoSet;
    VkDescriptorSet ssaoBlurSet;
    VkDescriptorSet deferredLightingSet;
    VkDescriptorSet compositeSet;
};
```

其中：

```text
ssaoSet:
    depth
    normalRoughness
    noise
    kernel UBO
    camera UBO

ssaoBlurSet:
    ssaoRaw
    depth
    normalRoughness

deferredLightingSet:
    albedoMetallic
    normalRoughness
    emissiveAO
    depth
    ssaoBlurred
    IBL cubemaps
    BRDF LUT
    lights UBO
    camera UBO
```

### 10.3 Render Pass / Dynamic Rendering

如果继续使用 dynamic rendering，G-Buffer pass 需要多个 color attachments：

```cpp
VkRenderingAttachmentInfo colorAttachments[] = {
    MakeColorAttachment(gbuffer.albedoMetallic.view, clearColor),
    MakeColorAttachment(gbuffer.normalRoughness.view, clearColor),
    MakeColorAttachment(gbuffer.emissiveAO.view, clearColor),
};

VkRenderingAttachmentInfo depthAttachment =
    MakeDepthAttachment(gbuffer.depth.view, clearDepth);

VkRenderingInfo renderingInfo{};
renderingInfo.colorAttachmentCount = 3;
renderingInfo.pColorAttachments = colorAttachments;
renderingInfo.pDepthAttachment = &depthAttachment;
```

pipeline 创建时也要声明对应 format：

```cpp
pipelineBuilder
    .setColorAttachmentFormats({
        gbufferAlbedoFormat,
        gbufferNormalFormat,
        gbufferEmissiveFormat
    })
    .setDepthFormat(depthFormat);
```

全屏 pass 通常不需要 depth format：

```cpp
ssaoPipeline:
    color = ssaoRawFormat
    no depth

deferredLightingPipeline:
    color = hdrFormat
    no depth

compositePipeline:
    color = swapchainFormat
    no depth
```

## 11. Command Buffer 推荐顺序

伪代码：

```cpp
void Lab3::buildCommandBuffer(VkCommandBuffer cmd) {
    transitionForGBufferWrite(cmd);

    cmdDrawGBuffer(cmd);
    cmdDrawInstancedGBuffer(cmd);

    transitionGBufferToShaderRead(cmd);
    transitionSSAOToColorWrite(cmd);

    cmdDrawSSAO(cmd);

    transitionSSAORawToShaderRead(cmd);
    transitionSSAOBlurredToColorWrite(cmd);

    cmdDrawSSAOBlur(cmd);

    transitionSSAOBlurredToShaderRead(cmd);
    transitionHDRToColorWrite(cmd);

    cmdDrawDeferredLighting(cmd);

    transitionHDRToShaderRead(cmd);

    if (enableBloom) {
        cmdDrawBloomDownsample(cmd);
        cmdDrawBloomUpsample(cmd);
    }

    transitionSwapchainToColorWrite(cmd);
    cmdDrawComposite(cmd);
}
```

在 Vulkan 中，Lab3 会比 Lab2 更依赖 image layout transition，因为同一张 G-Buffer image 会经历：

```text
COLOR_ATTACHMENT_OPTIMAL
    -> SHADER_READ_ONLY_OPTIMAL
```

SSAO raw 会经历：

```text
COLOR_ATTACHMENT_OPTIMAL
    -> SHADER_READ_ONLY_OPTIMAL
```

HDR scene 会经历：

```text
COLOR_ATTACHMENT_OPTIMAL
    -> SHADER_READ_ONLY_OPTIMAL
```

这部分建议封装小工具函数，避免每个 pass 手写一大段 barrier。

## 12. UI 调参建议

Lab3 最好一开始就加调试 UI：

```cpp
struct Lab3Settings {
    int debugView = 0;

    int enableDeferred = 1;
    int enableSSAO = 1;
    int enableSSAOBlur = 1;
    int enableBloom = 1;

    float exposure = 1.0f;
    float bloomStrength = 0.05f;

    float ssaoRadius = 0.5f;
    float ssaoBias = 0.025f;
    float ssaoPower = 1.5f;
    int ssaoKernelSize = 64;

    int instanceCount = 100;
};
```

推荐 debug view：

```text
0 Final
1 GBuffer Albedo
2 GBuffer Normal
3 GBuffer Roughness
4 GBuffer Metallic
5 Depth
6 SSAO Raw
7 SSAO Blurred
8 Direct Light Only
9 IBL Only
10 Emissive Only
```

这些 debug view 会极大降低排查成本。尤其是 SSAO，肉眼只看最终图很难判断问题来自 depth、normal、reconstruction、kernel 还是 blur。

## 13. 常见错误清单

### 13.1 G-Buffer 相关

- albedo 没从 sRGB 转 linear，导致 lighting 偏灰或偏亮。
- normal encode/decode 不一致，导致光照方向怪异。
- normal map 的 TBN 空间和 mesh tangent 数据不匹配。
- roughness 没 clamp，导致高光闪烁。
- G-Buffer attachment format 太低，normal banding 或 HDR emissive 被截断。

### 13.2 Deferred Lighting 相关

- 在 G-Buffer pass 提前 tone mapping，导致 HDR 能量丢失。
- SSAO 乘到了 direct light 上，结果光照整体脏黑。
- IBL 使用 world normal，但 G-Buffer 存的是 view normal，空间不一致。
- light position 没变到 view space，导致光源方向随相机错误变化。

### 13.3 SSAO 相关

- depth reconstruction 的 NDC z 范围错了。
- view-space z 比较方向反了，结果 AO 变成反向。
- radius 单位和 view-space position 单位不一致。
- bias 太小造成大面积自遮蔽。
- noise texture sampler 没设置 repeat，导致屏幕边缘采样异常。
- blur 没考虑深度/法线边缘，导致黑边扩散。

### 13.4 Instancing 相关

- instance buffer 没按 shader 结构体对齐。
- `mat4` 在 C++ 和 HLSL 中行列主序理解不一致。
- `SV_InstanceID` 没考虑 `firstInstance`。
- 动态更新 instance buffer 时覆盖了 GPU 正在读取的数据。
- per-instance material index 越界。

### 13.5 Vulkan 资源生命周期

- resize 后没有重建 G-Buffer / SSAO / HDR / Bloom resources。
- descriptor 仍然指向旧 image view。
- fullscreen pipeline 设置了不需要的 depth format。
- attachment layout transition 不完整，validation layer 报 read/write hazard。
- 多帧并行时复用同一套 offscreen image，产生 in-flight hazard。

## 14. 推荐实现顺序清单

我后续真正写 Lab3 时，可以按这个顺序推进：

```text
1. 建 lab3.cpp / lab3.h / shader 目录，跑空场景。
2. 创建 GBufferResources，完成 resize 重建逻辑。
3. 写 gbuffer.vert / gbuffer.frag，输出 albedo / normal / roughness / metallic。
4. 写 debug fullscreen pass，逐项显示 G-Buffer。
5. 写 deferredLighting.frag，先恢复直接光照。
6. 接入 IBL，确认和 Lab2 PBR 结果接近。
7. 加 depth reconstruction debug。
8. 生成 SSAO kernel 和 noise texture。
9. 写 ssao.frag，输出 ssaoRaw。
10. 写 ssaoBlur.frag，先普通 blur，再 bilateral blur。
11. 在 deferredLighting 中把 SSAO 乘到 ambient / IBL。
12. 创建 instance buffer，先画大量相同球体。
13. 用 SV_InstanceID 接 per-instance model matrix。
14. 加 per-instance material 参数。
15. 接回 Bloom 和最终 composite。
16. 做性能对比：forward vs deferred，non-instanced vs instanced。
```

## 15. 最终验收标准

Lab3 完成时，我希望能满足这些标准：

```text
Deferred Rendering:
    G-Buffer debug view 正确。
    Deferred lighting 和 Lab2 forward PBR 视觉接近。
    HDR / tone mapping 链路只在最后执行一次。

SSAO:
    接触区域、角落、缝隙有合理遮蔽。
    大平面不明显脏黑。
    相机移动时没有严重闪烁。
    SSAO 可以通过 UI 开关和调参。

Instancing:
    同一 mesh 能一次 draw call 绘制大量实例。
    instanceCount 可调。
    per-instance transform 正确。
    per-instance 材质参数或颜色至少支持一种变化。

工程稳定性:
    resize 后资源和 descriptor 正确重建。
    validation layer 无明显 layout / descriptor / hazard 报错。
    所有 offscreen pass 的 image layout transition 清晰。
```

## 16. 我对 Lab3 的整体理解

Lab2 的重点是“材质和光照是否物理可信”，Lab3 的重点会转向“渲染管线如何组织复杂信息”。

Deferred Rendering 让我把几何信息沉淀到 G-Buffer 中；SSAO 利用这些屏幕空间信息补充局部遮蔽；Instancing 则解决大量重复物体的提交成本。三者连起来后，我就不只是会写单个 PBR shader，而是开始搭建一条更接近真实引擎的 frame graph：

```text
几何数据
    -> 屏幕空间材质缓存
    -> 屏幕空间效果
    -> 延迟光照
    -> HDR 后处理
    -> 最终显示
```

所以 Lab3 最重要的不是一开始就追求画面特别复杂，而是把每个 pass 的输入、输出、格式、空间、layout、descriptor 和调试视图都设计清楚。只要这条数据流稳定，后面继续加 shadow mapping、SSR、TAA、clustered lighting 或 GPU culling 都会自然很多。
