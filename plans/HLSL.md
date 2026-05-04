# HLSL 入门与 GLSL 对比

这份文档的目标是帮你完成三件事：

1. 从你已经熟悉的 GLSL 过渡到 HLSL。
2. 理解在 **Vulkan** 项目里使用 HLSL 时，和 DirectX 语境下有什么不一样。
3. 能够在这个仓库里开始自己写 HLSL shader，用于后续图形实验。

这不是一份“完整 HLSL 语言手册”，而是一份面向 **Vulkan + DXC + 本仓库工作流** 的入门指南。

---

## 1. 先建立整体认知

### 1.1 在 Vulkan 里用 HLSL，实际发生了什么

Vulkan 运行时不直接吃 GLSL 或 HLSL 源码，它吃的是 **SPIR-V**。

所以不管你写：

- GLSL
- HLSL
- Slang

最后都要先编译成 `.spv`。

对这个仓库来说：

- GLSL 主要用 `glslangValidator`
- HLSL 主要用 `DXC`

官方 Vulkan Guide 也明确把 **DXC** 当成 Vulkan 下 HLSL -> SPIR-V 的推荐编译器。

在这个仓库里，HLSL 的编译脚本是：

- [`shaders/hlsl/compileshaders.py`](../shaders/hlsl/compileshaders.py)

如果你以后自己写一个 sample，重编 HLSL 的常用方式是：

```bash
cd shaders/hlsl
python compileshaders.py --sample triangle
```

---

## 2. 你可以先把 HLSL 理解成什么

如果你已经会 GLSL，那你可以先把 HLSL 理解成：

- 语法更像 C/C++
- 更强调 `struct`、语义（semantic）和显式输入输出
- 资源绑定方式和 GLSL 很不一样
- 在 Vulkan 里通常还要加一些 `[[vk::...]]` 属性来补齐 Vulkan 语义

一句话概括：

- **GLSL 更像“着色器版 C”**
- **HLSL 更像“着色器版 C++ 风格接口描述”**

---

## 3. 先用 Triangle 对照看一遍

建议你先同时打开这四个文件：

- [`shaders/glsl/triangle/triangle.vert`](../shaders/glsl/triangle/triangle.vert)
- [`shaders/glsl/triangle/triangle.frag`](../shaders/glsl/triangle/triangle.frag)
- [`shaders/hlsl/triangle/triangle.vert`](../shaders/hlsl/triangle/triangle.vert)
- [`shaders/hlsl/triangle/triangle.frag`](../shaders/hlsl/triangle/triangle.frag)

你会发现它们做的是同一件事：

1. 顶点输入：位置 + 颜色
2. UBO 输入：projection / model / view
3. 顶点着色器输出颜色
4. 片元着色器输出最终颜色

只是表达方式不同。

### 3.1 GLSL 版本的核心写法

GLSL 里你熟悉的是这种写法：

```glsl
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

layout(binding = 0) uniform UBO {
    mat4 projectionMatrix;
    mat4 modelMatrix;
    mat4 viewMatrix;
} ubo;

layout(location = 0) out vec3 outColor;
```

### 3.2 HLSL 版本的核心写法

HLSL 里对应成：

```hlsl
struct VSInput
{
    [[vk::location(0)]] float3 Pos : POSITION0;
    [[vk::location(1)]] float3 Color : COLOR0;
};

struct UBO
{
    float4x4 projectionMatrix;
    float4x4 modelMatrix;
    float4x4 viewMatrix;
};

cbuffer ubo : register(b0) { UBO ubo; }

struct VSOutput
{
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float3 Color : COLOR0;
};
```

你可以先看到最明显的区别：

1. 输入输出通常放在 `struct` 里。
2. 资源不再用 `layout(binding=...)`，而是用 `cbuffer` 和 `register(...)`。
3. 顶点位置输出不用 `gl_Position`，而是 `SV_POSITION`。
4. Vulkan 下为了明确 location，通常还会写 `[[vk::location(n)]]`。

---

## 4. GLSL 和 HLSL 的主要异同

下面按最常用的部分来比。

## 4.1 相同点

如果只谈“写图形算法”的核心体验，两者其实很像：

1. 都有向量、矩阵、纹理采样、控制流、函数。
2. 都能表达 vertex / fragment / compute shader。
3. 都能做光照、阴影、PBR、后处理、GPGPU。
4. 在 Vulkan 里最后都会变成 SPIR-V。

所以你不需要把它当成“一门完全陌生的语言”，它更像是“同一类工作、不同方言”。

## 4.2 最核心的不同点

### 1. 输入输出接口组织方式不同

GLSL 常见写法：

```glsl
layout(location = 0) in vec3 inPos;
layout(location = 1) out vec3 outColor;
```

HLSL 常见写法：

```hlsl
struct VSInput {
    [[vk::location(0)]] float3 Pos : POSITION0;
};

struct VSOutput {
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float3 Color : COLOR0;
};
```

HLSL 更喜欢把 stage 间接口放进结构体里。

### 2. HLSL 有 semantic

例如：

- `POSITION0`
- `NORMAL0`
- `TEXCOORD0`
- `COLOR0`
- `SV_POSITION`
- `SV_TARGET`
- `SV_DispatchThreadID`

semantic 可以先理解成：

- “这个字段在图形管线里扮演什么角色”

在 Vulkan 里，semantic 还不够，因为 Vulkan 很依赖明确的 location / binding，所以你还常常会看到：

- `[[vk::location(n)]]`
- `[[vk::binding(n)]]`

### 3. 资源绑定方式不同

GLSL：

```glsl
layout(set = 1, binding = 0) uniform sampler2D colorMap;
```

HLSL 常见两种方式：

#### 方式 A：`register(...) + spaceN`

```hlsl
Texture2D textureColorMap : register(t0, space1);
SamplerState samplerColorMap : register(s0, space1);
```

这里通常可以理解为：

- `space1` 对应 descriptor set 1
- `t0/s0/b0/u0` 对应 binding 编号和资源类型

这个仓库里很常见这种写法，比如：

- [`shaders/hlsl/gltfloading/mesh.frag`](../shaders/hlsl/gltfloading/mesh.frag)
- [`shaders/hlsl/conditionalrender/model.vert`](../shaders/hlsl/conditionalrender/model.vert)

#### 方式 B：Vulkan 属性

```hlsl
[[vk::binding(0)]]
StructuredBuffer<Particle> particleIn;
```

这个仓库里也有，例如：

- [`shaders/hlsl/computecloth/cloth.comp`](../shaders/hlsl/computecloth/cloth.comp)

### 4. 内建变量名字不同

常见映射可以先记这些：

| GLSL | HLSL |
|---|---|
| `gl_Position` | `SV_POSITION` |
| `gl_VertexIndex` | `SV_VertexID` |
| `gl_InstanceIndex` | `SV_InstanceID` |
| `gl_FragCoord` | `SV_POSITION`（在 fragment 输入里用） |
| `gl_FragDepth` | `SV_Depth` |
| `gl_GlobalInvocationID` | `SV_DispatchThreadID` |
| `gl_LocalInvocationID` | `SV_GroupThreadID` |
| `gl_WorkGroupID` | `SV_GroupID` |

你不需要一次记全，先把 vertex / fragment / compute 最常用的几个记住就够了。

### 5. 纹理和采样器通常分开写

GLSL 常常这样：

```glsl
layout(binding = 0) uniform sampler2D colorMap;
```

HLSL 常常这样：

```hlsl
Texture2D colorMap : register(t0);
SamplerState colorSampler : register(s0);
```

采样时写：

```hlsl
float4 color = colorMap.Sample(colorSampler, uv);
```

这和 GLSL 的：

```glsl
vec4 color = texture(colorMap, uv);
```

是最直观的区别之一。

---

## 5. Vulkan 里使用 HLSL 时最重要的几个概念

## 5.1 `[[vk::location(n)]]`

在 Vulkan 下，vertex input、stage 间 varying、fragment output 等经常要明确 location。

所以你会看到：

```hlsl
[[vk::location(0)]] float3 Pos : POSITION0;
[[vk::location(1)]] float3 Normal : NORMAL0;
```

这和 GLSL 的：

```glsl
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
```

是等价思路。

对你来说最实用的规则是：

- **宿主侧 `VkVertexInputAttributeDescription.location` 要和 shader 里的 `[[vk::location(n)]]` 对上**

---

## 5.2 `register(...)` 和 `spaceN`

这部分一开始最容易混乱。

你可以先这样记：

- `b` = constant buffer
- `t` = texture / SRV / read-only resource
- `s` = sampler
- `u` = UAV / RW resource
- `spaceN` = 一般可以对应 Vulkan 的 descriptor set N

比如：

```hlsl
cbuffer ubo : register(b0) { UBO ubo; }
cbuffer NodeBuf : register(b0, space1) { Node node; }
Texture2D textureColorMap : register(t0, space1);
SamplerState samplerColorMap : register(s0, space1);
```

你可以先把它理解成：

- set 0 里有一个 `b0`
- set 1 里有一个 `b0`
- set 1 里还有 `t0`、`s0`

这和 GLSL 的：

```glsl
layout(set = 0, binding = 0) uniform UBO ...
layout(set = 1, binding = 0) uniform Node ...
layout(set = 1, binding = 1) uniform sampler2D ...
```

是在表达同一种资源布局。

---

## 5.3 `[[vk::push_constant]]`

Vulkan 特有的重要路径之一。

这个仓库里你能直接看到例子：

- [`shaders/hlsl/base/uioverlay.vert`](../shaders/hlsl/base/uioverlay.vert)
- [`shaders/hlsl/conditionalrender/model.vert`](../shaders/hlsl/conditionalrender/model.vert)

HLSL 写法通常像这样：

```hlsl
struct PushConstants
{
    float2 scale;
    float2 translate;
};

[[vk::push_constant]]
PushConstants pushConstants;
```

你可以把它理解成：

- Vulkan 里一小块直接塞进 command buffer 的快速参数区

这在做：

- 小量材质参数
- per-draw 参数
- 全屏 pass 开关

时非常好用。

---

## 5.4 `[[vk::constant_id(n)]]`

这是 specialization constant。

这个仓库里有 compute 例子：

- [`shaders/hlsl/computeheadless/headless.comp`](../shaders/hlsl/computeheadless/headless.comp)

例如：

```hlsl
[[vk::constant_id(0)]] const uint BUFFER_ELEMENTS = 32;
```

你可以把它理解成：

- 编 pipeline 时可替换的常量
- 不是每帧变的 uniform
- 也不是 push constant

---

## 6. HLSL 的常用写法

下面按照你以后最常写的几种 shader 类型来整理。

## 6.1 Vertex Shader

典型模板：

```hlsl
struct VSInput
{
    [[vk::location(0)]] float3 Pos : POSITION0;
    [[vk::location(1)]] float3 Normal : NORMAL0;
    [[vk::location(2)]] float2 UV : TEXCOORD0;
};

struct UBO
{
    float4x4 projection;
    float4x4 view;
    float4x4 model;
};

cbuffer ubo : register(b0)
{
    UBO ubo;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float3 Normal : NORMAL0;
    [[vk::location(1)]] float2 UV : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput)0;
    float4 worldPos = mul(ubo.model, float4(input.Pos, 1.0));
    output.Pos = mul(ubo.projection, mul(ubo.view, worldPos));
    output.Normal = input.Normal;
    output.UV = input.UV;
    return output;
}
```

你以后最常干的事情就是：

1. 读顶点属性
2. 读矩阵 UBO
3. 计算裁剪空间位置
4. 把中间量传给 fragment shader

---

## 6.2 Fragment Shader

典型模板：

```hlsl
Texture2D colorMap : register(t0);
SamplerState colorSampler : register(s0);

struct FSInput
{
    [[vk::location(0)]] float3 Normal : NORMAL0;
    [[vk::location(1)]] float2 UV : TEXCOORD0;
};

float4 main(FSInput input) : SV_TARGET
{
    float3 albedo = colorMap.Sample(colorSampler, input.UV).rgb;
    return float4(albedo, 1.0);
}
```

注意几点：

1. 片元输出通常写 `: SV_TARGET`
2. 多渲染目标时可以写 `SV_TARGET0`、`SV_TARGET1`
3. 纹理采样用 `.Sample(...)`

---

## 6.3 Compute Shader

典型模板：

```hlsl
RWStructuredBuffer<float4> outputBuffer : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    outputBuffer[id.x] = float4(1, 0, 0, 1);
}
```

这里几个关键词要记住：

1. `RWStructuredBuffer`
2. `[numthreads(x, y, z)]`
3. `SV_DispatchThreadID`

在这个仓库里可以先看：

- [`shaders/hlsl/computeheadless/headless.comp`](../shaders/hlsl/computeheadless/headless.comp)
- [`shaders/hlsl/computecloth/cloth.comp`](../shaders/hlsl/computecloth/cloth.comp)

---

## 6.4 常量缓冲 `cbuffer`

HLSL 里最常见的 uniform 数据组织方式就是 `cbuffer`。

例如：

```hlsl
struct UBO
{
    float4x4 projection;
    float4x4 model;
    float4 lightPos;
};

cbuffer ubo : register(b0)
{
    UBO ubo;
};
```

这基本对应 GLSL 的：

```glsl
layout(binding = 0) uniform UBO {
    mat4 projection;
    mat4 model;
    vec4 lightPos;
} ubo;
```

你可以继续沿用 GLSL 时那种习惯：

- 把一组逻辑上相关的参数打包成一个 block

---

## 6.5 结构化缓冲

在 compute 或更复杂的图形 shader 里，常见：

```hlsl
StructuredBuffer<Particle> particleIn : register(t0);
RWStructuredBuffer<Particle> particleOut : register(u1);
```

这里可以先这样记：

- `StructuredBuffer<T>`：只读
- `RWStructuredBuffer<T>`：可写

对于粒子、实例数据、可见性结果、indirect draw 数据，这类写法非常常见。

---

## 7. GLSL -> HLSL 对照速查

## 7.1 基本类型

| GLSL | HLSL |
|---|---|
| `float` | `float` |
| `vec2` | `float2` |
| `vec3` | `float3` |
| `vec4` | `float4` |
| `mat3` | `float3x3` |
| `mat4` | `float4x4` |
| `int` | `int` |
| `ivec2` | `int2` |
| `uvec3` | `uint3` |
| `bool` | `bool` |

这个部分最容易适应。

---

## 7.2 常用数学函数

大量函数是几乎同名的：

- `dot`
- `cross`
- `normalize`
- `reflect`
- `refract`
- `length`
- `distance`
- `clamp`
- `lerp` / `mix`
- `sin` / `cos`
- `pow`
- `min` / `max`

注意一个最常见差别：

- GLSL 线性插值函数是 `mix(a, b, t)`
- HLSL 是 `lerp(a, b, t)`

---

## 7.3 采样函数

GLSL：

```glsl
vec4 c = texture(colorMap, uv);
```

HLSL：

```hlsl
float4 c = colorMap.Sample(colorSampler, uv);
```

这个要尽快形成肌肉记忆。

---

## 7.4 输入输出

GLSL：

```glsl
layout(location = 0) in vec3 inPos;
layout(location = 0) out vec3 outColor;
```

HLSL：

```hlsl
struct VSInput {
    [[vk::location(0)]] float3 Pos : POSITION0;
};

struct VSOutput {
    [[vk::location(0)]] float3 Color : COLOR0;
};
```

---

## 8. 初学 HLSL 时最值得注意的几个点

## 8.1 不要只看 semantic，不看 `vk::location`

在 DirectX 语境下，semantic 很重要；在 Vulkan 语境下，**显式 location / binding 更重要**。

对你来说最稳的习惯是：

1. 顶点输入写 `[[vk::location(n)]]`
2. varying 写 `[[vk::location(n)]]`
3. 资源绑定写清楚 `register(...)` 或 `[[vk::binding(... )]]`

这样最不容易和宿主侧管线状态对不齐。

---

## 8.2 矩阵乘法顺序不要想当然

这是从 GLSL 切到 HLSL 时最容易踩坑的地方之一。

你会在这个仓库里看到：

```hlsl
output.Pos = mul(ubo.projectionMatrix,
             mul(ubo.viewMatrix,
             mul(ubo.modelMatrix, float4(input.Pos.xyz, 1.0))));
```

这和 GLSL 的：

```glsl
gl_Position = projection * view * model * vec4(pos, 1.0);
```

在这个仓库里是对应的。

最实用的建议不是去死记“行主序/列主序”的理论，而是：

1. 先在项目内保持一种一致写法。
2. 宿主侧矩阵布局和 shader 乘法顺序始终配套。
3. 一旦画面错位、模型翻转、法线怪异，优先排查矩阵顺序和是否需要转置。

对于你当前这个仓库，最稳的策略就是：

- **先沿用仓库已有的 HLSL 乘法风格**

---

## 8.3 `Texture2D` 和 `SamplerState` 分开是正常的

如果你过去习惯 GLSL 的 `sampler2D`，一开始会觉得 HLSL 啰嗦。

但用一段时间就会习惯，因为它让资源角色更清晰：

- 纹理是纹理
- 采样器是采样器

而且这在做：

- 比较采样器状态
- 阴影采样
- 不同过滤方式

时是挺自然的。

---

## 8.4 并不是所有 GLSL 内建都有直接等价物

这个仓库自己的 HLSL README 里就提到了几个注意点：

- `gl_PointCoord` 没有直接等价物，需要自己算
- `inverse()` 不是 HLSL 那种开箱即用的常见路径，很多时候更建议 CPU 预处理

你可以参考：

- [`shaders/hlsl/README.md`](../shaders/hlsl/README.md)

所以以后从 GLSL 翻译到 HLSL 时，不要假设每个内建函数都能一比一替换。

---

## 8.5 Vulkan 下建议优先用 DXC

在 Vulkan 里写 HLSL，最好把这条习惯固定下来：

- **HLSL -> DXC -> SPIR-V**

这个仓库也是这么组织的。

---

## 9. 我推荐你的 HLSL 入门阅读顺序

既然你已经会 GLSL，那我建议按这个顺序学，而不是先去看纯语言教程。

### 第 1 步：看 Triangle

先对照：

- [`shaders/glsl/triangle/triangle.vert`](../shaders/glsl/triangle/triangle.vert)
- [`shaders/hlsl/triangle/triangle.vert`](../shaders/hlsl/triangle/triangle.vert)
- [`shaders/glsl/triangle/triangle.frag`](../shaders/glsl/triangle/triangle.frag)
- [`shaders/hlsl/triangle/triangle.frag`](../shaders/hlsl/triangle/triangle.frag)

目标：

1. 熟悉 `struct VSInput / VSOutput`
2. 熟悉 `cbuffer`
3. 熟悉 `SV_POSITION` / `SV_TARGET`
4. 熟悉 `[[vk::location]]`

### 第 2 步：看 Push Constant

看：

- [`shaders/hlsl/base/uioverlay.vert`](../shaders/hlsl/base/uioverlay.vert)

目标：

1. 学会 `[[vk::push_constant]]`
2. 接受“Vulkan 特有语义要靠 `vk::...` 属性表达”

### 第 3 步：看 Texture + Sampler

看：

- [`shaders/hlsl/base/uioverlay.frag`](../shaders/hlsl/base/uioverlay.frag)
- [`shaders/hlsl/gltfloading/mesh.frag`](../shaders/hlsl/gltfloading/mesh.frag)

目标：

1. 学会 `Texture2D`
2. 学会 `SamplerState`
3. 学会 `.Sample(...)`
4. 学会 `space1`

### 第 4 步：看 Compute

看：

- [`shaders/hlsl/computeheadless/headless.comp`](../shaders/hlsl/computeheadless/headless.comp)
- [`shaders/hlsl/computecloth/cloth.comp`](../shaders/hlsl/computecloth/cloth.comp)

目标：

1. 学会 `StructuredBuffer`
2. 学会 `RWStructuredBuffer`
3. 学会 `[numthreads(...)]`
4. 学会 `SV_DispatchThreadID`
5. 学会 `[[vk::constant_id]]`

---

## 10. 以后自己写 HLSL 时，我建议的代码风格

为了后面的图形实验更舒服，我建议你尽早固定一套风格。

### 1. 每个 stage 都写输入输出 struct

即使输入很少，也建议写：

```hlsl
struct VSInput { ... };
struct VSOutput { ... };
```

好处是：

- 接口清晰
- 容易扩展
- 和 GLSL 版对照时更稳

### 2. 所有 Vulkan 关键信息都显式写出来

比如：

- `[[vk::location(n)]]`
- `register(b0, space1)`
- `[[vk::push_constant]]`
- `[[vk::constant_id(n)]]`

不要指望“编译器帮你猜”。

### 3. 统一命名

建议坚持：

- `VSInput`
- `VSOutput`
- `FSInput`
- `UBO`
- `PushConstants`

这会让你在多个实验之间切换时非常省脑力。

### 4. 先沿用仓库里现成的乘法风格

也就是：

```hlsl
mul(P, mul(V, mul(M, pos)))
```

这样你和当前 C++ 侧的矩阵组织最不容易打架。

---

## 11. 如果你要从 GLSL 翻译到 HLSL，可以按这个清单走

假设你已经写好一版 GLSL，想翻译成 HLSL：

1. 把 `in/out` 改成 `struct` 输入输出。
2. 把 `vec/mat` 改成 `floatN/floatMxN`。
3. 把 `layout(location=...)` 改成 `[[vk::location(... )]]`。
4. 把 `uniform block` 改成 `cbuffer`。
5. 把 `sampler2D` 改成 `Texture2D + SamplerState`。
6. 把 `texture(...)` 改成 `.Sample(...)`。
7. 把 `gl_Position` 改成 `SV_POSITION`。
8. 把 `gl_FragColor` / fragment output 改成 `SV_TARGET`。
9. 检查矩阵乘法顺序。
10. 用 DXC 编译成 SPIR-V，实际跑一遍验证。

---

## 12. 一份最小 HLSL 模板

### Vertex

```hlsl
struct VSInput
{
    [[vk::location(0)]] float3 Pos : POSITION0;
    [[vk::location(1)]] float3 Color : COLOR0;
};

struct UBO
{
    float4x4 projection;
    float4x4 view;
    float4x4 model;
};

cbuffer ubo : register(b0)
{
    UBO ubo;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float3 Color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput)0;
    output.Color = input.Color;
    output.Pos = mul(ubo.projection, mul(ubo.view, mul(ubo.model, float4(input.Pos, 1.0))));
    return output;
}
```

### Fragment

```hlsl
struct FSInput
{
    [[vk::location(0)]] float3 Color : COLOR0;
};

float4 main(FSInput input) : SV_TARGET
{
    return float4(input.Color, 1.0);
}
```

这个模板就是你之后很多实验的起点。

---

## 13. 最后给你的实际建议

如果你后面的图形实验准备全面转 HLSL，我建议路线是：

1. 先把 `triangle` 的 GLSL/HLSL 对照看熟。
2. 后续新 sample 优先只写 HLSL，不再双写 GLSL。
3. 先把 `cbuffer`、texture/sampler、push constant、compute 这四套模式练熟。
4. 建一个你自己的 HLSL 小模板库。

你不需要先成为“HLSL 专家”再开始做实验。  
对图形实验来说，更有效的方式是：

- **一边做 Vulkan 实验，一边把 HLSL 当成主要表达语言慢慢用熟**

---

## 14. 参考资料

官方主资料：

- Vulkan Guide: HLSL in Vulkan  
  https://docs.vulkan.org/guide/latest/hlsl.html

- Vulkan Guide: GLSL / HLSL 对照  
  https://docs.vulkan.org/guide/latest/high_level_shader_language_comparison.html

仓库内资料：

- [`shaders/hlsl/README.md`](../shaders/hlsl/README.md)
- [`shaders/hlsl/compileshaders.py`](../shaders/hlsl/compileshaders.py)

起步示例：

- [`shaders/hlsl/triangle/triangle.vert`](../shaders/hlsl/triangle/triangle.vert)
- [`shaders/hlsl/triangle/triangle.frag`](../shaders/hlsl/triangle/triangle.frag)
- [`shaders/hlsl/base/uioverlay.vert`](../shaders/hlsl/base/uioverlay.vert)
- [`shaders/hlsl/base/uioverlay.frag`](../shaders/hlsl/base/uioverlay.frag)
- [`shaders/hlsl/computeheadless/headless.comp`](../shaders/hlsl/computeheadless/headless.comp)
