# glTF 2.0 学习与项目使用指南

这份文档用于帮助你理解 glTF 2.0 的基本规则，并说明在当前 Vulkan Example 项目里应该如何使用 glTF 模型。重点不是把规范背下来，而是建立一套能落地的 mental model：一个 glTF 文件到底由哪些数据组成，loader 如何把它变成 Vulkan buffer、texture、descriptor，下载网上模型时应该避开哪些坑。

## 1. glTF 2.0 是什么

glTF 全称是 GL Transmission Format，通常被叫作“3D 领域的 JPEG”。它的目标是让 3D 模型可以高效、标准化地在引擎、编辑器、Web、实时渲染器之间传输。

glTF 2.0 主要描述这些内容：

- 场景层级：scene、node、transform。
- 几何数据：mesh、primitive、index、vertex attribute。
- 材质系统：默认使用 metallic-roughness PBR。
- 贴图系统：image、sampler、texture。
- 动画系统：translation、rotation、scale animation。
- 骨骼蒙皮：skin、joint、inverse bind matrix。
- 扩展机制：例如 Draco 压缩、clearcoat、transmission、specular 等。

官方资料：

- glTF 2.0 规范：https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
- Khronos glTF Sample Assets：https://github.com/KhronosGroup/glTF-Sample-Assets
- glTF 官方仓库：https://github.com/KhronosGroup/glTF

## 2. 常见文件格式

glTF 2.0 通常有两种形态。

### `.gltf` 分离格式

`.gltf` 是一个 JSON 文件，通常会引用外部 `.bin` 和贴图文件：

```text
model/
  scene.gltf
  scene.bin
  baseColor.png
  normal.png
  metallicRoughness.png
```

优点是结构清晰，适合学习和调试。你可以直接打开 `.gltf` 看 JSON 内容。

缺点是文件分散，移动模型时必须保持相对路径不变。

### `.glb` 二进制格式

`.glb` 是单文件二进制打包格式，JSON、buffer、图片可以打包在一个文件里：

```text
model.glb
```

优点是便于分发，网上很多模型默认提供 `.glb`。

缺点是调试不如 `.gltf` 直观。当前项目的 `VulkanglTFModel::loadFromFile()` 默认只调用 `LoadASCIIFromFile`，所以它主要支持 `.gltf`。tinygltf 本身支持 `.glb`，但本项目要额外根据扩展名调用 `LoadBinaryFromFile` 才能稳定加载 `.glb`。

## 3. glTF 的核心数据结构

可以把 glTF 理解成三层：

```text
Scene Graph
  -> Mesh / Primitive
    -> Buffer / BufferView / Accessor
  -> Material / Texture / Image
```

### Scene

`scene` 是入口，里面引用一组根节点：

```json
{
  "scene": 0,
  "scenes": [
    { "nodes": [0, 1, 2] }
  ]
}
```

一个 glTF 可以有多个 scene，但通常只用默认 scene。

### Node

`node` 负责层级和变换。一个 node 可以有：

- `translation`
- `rotation`
- `scale`
- `matrix`
- `children`
- `mesh`
- `skin`

注意：node 不一定有 mesh。有些 node 只是空节点，用于组织层级或作为骨骼 joint。

### Mesh 与 Primitive

`mesh` 是一个模型对象，里面包含一个或多个 `primitive`。每个 primitive 通常对应一次 draw call。

primitive 里会指定：

- `attributes`：例如 `POSITION`、`NORMAL`、`TEXCOORD_0`、`TANGENT`。
- `indices`：索引 buffer。
- `material`：材质索引。
- `mode`：绘制拓扑，常见是 `TRIANGLES`。

简单理解：

```text
mesh = 多个 primitive
primitive = 一组顶点属性 + 一组索引 + 一个材质
```

## 4. Buffer / BufferView / Accessor

这是 glTF 最容易绕的部分，但理解后很清晰。

### Buffer

`buffer` 是原始二进制数据块，可能来自 `.bin` 文件，也可能嵌入在 `.glb` 里。

```json
"buffers": [
  { "uri": "scene.bin", "byteLength": 102400 }
]
```

### BufferView

`bufferView` 是对 buffer 的一段切片，类似：

```text
buffer + offset + length
```

它通常表示“一段连续的顶点数据”或“一段连续的索引数据”。

### Accessor

`accessor` 解释 bufferView 中的数据类型。例如这段数据是 `VEC3 float`，那就是 position；如果是 `SCALAR unsigned short`，那可能是 index。

常见 accessor 信息：

- `componentType`：float、unsigned short、unsigned int 等。
- `type`：SCALAR、VEC2、VEC3、VEC4、MAT4。
- `count`：元素数量。
- `min/max`：常用于包围盒。

在 Vulkan 中，loader 最终会把 accessor 解析成自己的顶点数组和索引数组，再上传到 `VkBuffer`。

## 5. 坐标系与单位

glTF 2.0 使用右手坐标系：

- `+Y` 是上方向。
- 标准资产通常面向 `+Z`。
- camera 和 light 的局部前方通常是 `-Z`。
- 角度单位使用弧度。
- 距离单位通常按米理解，但规范本身不会强制真实比例。

Vulkan 的 clip space 和 OpenGL/glTF 习惯有差异，所以项目里经常使用这些加载 flag：

```cpp
vkglTF::FileLoadingFlags::PreTransformVertices |
vkglTF::FileLoadingFlags::FlipY
```

其中：

- `PreTransformVertices`：把 node 层级变换预先烘焙进顶点，简化 draw 阶段。
- `FlipY`：翻转 Y 轴，适配项目里的坐标/投影约定。
- `FlipUV`：翻转纹理 V 坐标，某些纹理上下颠倒时使用。

这几个 flag 是项目实现层面的选择，不是 glTF 规范本身要求。

## 6. glTF PBR 材质

glTF 2.0 默认材质模型是 metallic-roughness PBR。核心参数在 `pbrMetallicRoughness` 中：

```json
"materials": [
  {
    "pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 0.0, 0.0, 1.0],
      "metallicFactor": 0.5,
      "roughnessFactor": 0.8,
      "baseColorTexture": { "index": 0 },
      "metallicRoughnessTexture": { "index": 1 }
    },
    "normalTexture": { "index": 2 },
    "occlusionTexture": { "index": 3 },
    "emissiveTexture": { "index": 4 }
  }
]
```

### Base Color

`baseColorFactor` 和 `baseColorTexture` 共同决定基础颜色。

通常：

- 非金属：base color 近似漫反射颜色。
- 金属：base color 更接近镜面反射颜色，也就是 F0。

base color 贴图应按 sRGB 颜色理解，进入 shader 计算前通常需要转到 linear。

### Metallic-Roughness Texture

glTF 的 metallic-roughness 通常打包在一张贴图里：

- `G` 通道：roughness。
- `B` 通道：metallic。
- `R` 通道：通常未使用，有些流程会放 AO，但标准 AO 是单独 `occlusionTexture`。

这是一个非常常见的坑：不要把 roughness 和 metallic 通道读反。

### Normal Texture

normal map 一般需要顶点提供 tangent。glTF 支持 `TANGENT` attribute；如果模型没有 tangent，渲染器需要自己生成 tangent，或者暂时不做 normal mapping。

当前项目的顶点结构里有 tangent：

```cpp
enum class VertexComponent {
    Position, Normal, UV, Color, Tangent, Joint0, Weight0
};
```

但你的 Lab2 当前 shader 主要使用 `Position + Normal`，还没有正式接入 TBN normal mapping。

## 7. 当前项目的 glTF loader 支持情况

项目 loader 在这里：

```text
base/VulkanglTFModel.h
base/VulkanglTFModel.cpp
external/tinygltf/tiny_gltf.h
```

文件头里已经写明：这不是完整 glTF loader，不支持 glTF 2.0 的所有功能。

### 当前支持较好的内容

- glTF 2.0 `.gltf` 文本格式。
- 顶点 position、normal、uv、color、tangent、joint、weight。
- index buffer。
- node 层级与 transform。
- baseColorTexture。
- metallicRoughnessTexture。
- normalTexture。
- occlusionTexture。
- emissiveTexture。
- baseColorFactor、metallicFactor、roughnessFactor。
- alphaMode：opaque、mask、blend。
- animation 和 skin 的基础数据。
- PNG/JPG 贴图。
- 外部 KTX 贴图。

### 当前需要注意的限制

- `loadFromFile()` 默认只调用 `LoadASCIIFromFile`，所以 `.glb` 不一定能直接加载。
- Draco 压缩通常不支持，除非你额外启用 tinygltf 的 Draco 支持并接入解码库。
- KTX2/BasisU 不等于 KTX，当前代码专门处理的是 `.ktx`。
- 复杂材质扩展不一定支持，例如 clearcoat、transmission、sheen、ior、specular 等。
- loader 读取了 metallic/roughness 材质，但你的 Lab2 当前并没有自动把 glTF 材质接入 PBR shader。
- 如果用 `PreTransformVertices`，node transform 会被烘焙进顶点；这对静态模型方便，但对动画/骨骼模型不一定合适。

## 8. 在项目中加载一个模型

最简单方式：

```cpp
vkglTF::Model model;
model.loadFromFile(
    getAssetPath() + "models/my_model/scene.gltf",
    vulkanDevice,
    queue,
    vkglTF::FileLoadingFlags::PreTransformVertices |
    vkglTF::FileLoadingFlags::FlipY
);
```

如果你只需要顶点，不需要贴图：

```cpp
model.loadFromFile(
    getAssetPath() + "models/my_model/scene.gltf",
    vulkanDevice,
    queue,
    vkglTF::FileLoadingFlags::PreTransformVertices |
    vkglTF::FileLoadingFlags::FlipY |
    vkglTF::FileLoadingFlags::DontLoadImages
);
```

绘制时：

```cpp
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sceneDescriptor, 0, nullptr);
model.draw(cmd);
```

如果要使用 glTF 自带 baseColorTexture，需要使用：

```cpp
model.draw(cmd, vkglTF::RenderFlags::BindImages, pipelineLayout);
```

并且 pipeline layout 里需要包含 `vkglTF::descriptorSetLayoutImage` 对应的 descriptor set layout。很多原工程 sample 是这样做的。

## 9. Vertex Input 怎么匹配 shader

当前 loader 的顶点结构是固定的：

```cpp
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color;
    glm::vec4 joint0;
    glm::vec4 weight0;
    glm::vec4 tangent;
};
```

pipeline 中通过下面函数声明你要用哪些属性：

```cpp
pipelineCI.pVertexInputState =
    vkglTF::Vertex::getPipelineVertexInputState({
        vkglTF::VertexComponent::Position,
        vkglTF::VertexComponent::Normal,
        vkglTF::VertexComponent::UV
    });
```

shader location 必须和这里的顺序对应。例如上面顺序对应：

```hlsl
[[vk::location(0)]] float3 pos    : POSITION0;
[[vk::location(1)]] float3 normal : NORMAL0;
[[vk::location(2)]] float2 uv     : TEXCOORD0;
```

如果 C++ vertex input 和 HLSL location 对不上，常见现象是：

- 模型变形。
- 法线颜色很怪。
- 贴图坐标错乱。
- PBR 高光方向异常。

## 10. 网上下载模型的建议

优先选择：

- glTF 2.0。
- Separate `.gltf + .bin + png/jpg`。
- 不带 Draco 压缩。
- 不依赖复杂扩展。
- 模型面数适中。
- 有明确许可证。

推荐资源：

- Khronos glTF Sample Assets：https://github.com/KhronosGroup/glTF-Sample-Assets
- Sketchfab glTF 下载：https://sketchfab.com/features/gltf
- Poly Haven：https://polyhaven.com
- Kenney Assets：https://kenney.nl/assets

下载后建议这样放：

```text
assets/models/my_model/
  scene.gltf
  scene.bin
  textures/
    baseColor.png
    normal.png
    metallicRoughness.png
```

路径不要乱改，因为 `.gltf` 里的 `uri` 通常是相对路径。

## 11. Blender 导出建议

如果你从 Blender 导出，建议：

- Format 选择 `glTF Separate (.gltf + .bin + textures)`。
- 勾选 `+Y Up`。
- Geometry 中导出 `Normals`。
- 如果做 normal mapping，导出 `Tangents`。
- Materials 使用 Principled BSDF，对应 glTF metallic-roughness。
- 不要启用 Draco Compression，除非项目已支持。
- 贴图尽量使用 PNG/JPG，先别用 KTX2。

如果模型进入项目后方向不对，可以先尝试：

```cpp
vkglTF::FileLoadingFlags::FlipY
```

如果贴图上下颠倒，再尝试：

```cpp
vkglTF::FileLoadingFlags::FlipUV
```

## 12. Lab2 后续如何接入 glTF PBR 材质

你现在的 Lab2 是“材质球实验”，材质主要来自 push constant：

```hlsl
float3 albedo = float3(pushconstants.r, pushconstants.g, pushconstants.b);
float roughness = clamp(pushconstants.roughness, 0.04, 1.0);
float metallic = saturate(pushconstants.metallic);
```

如果后续想变成真正的 glTF PBR viewer，需要做这些改造：

1. vertex input 加入 `UV`，shader 接收 `uv`。
2. descriptor layout 中加入 glTF 材质贴图。
3. 绘制时使用 `model.draw(cmd, vkglTF::RenderFlags::BindImages, pipelineLayout)`。
4. 在 shader 中采样 baseColorTexture 和 metallicRoughnessTexture。
5. 正确处理 baseColor 的 sRGB 到 linear。
6. 从 metallicRoughnessTexture 的 `G/B` 通道读取 roughness/metallic。
7. 加入 normal map 时，需要接入 tangent 并构建 TBN。

伪代码：

```hlsl
float4 baseColor = baseColorTexture.Sample(baseColorSampler, uv);
float2 mr = metallicRoughnessTexture.Sample(mrSampler, uv).gb;

float3 albedo = SRGBToLinear(baseColor.rgb) * baseColorFactor.rgb;
float roughness = mr.g * roughnessFactor;
float metallic = mr.b * metallicFactor;
```

注意这里的 `mr.g / mr.b` 只是示意。如果你把采样结果写成 `float4 mrTex`，更清楚的写法是：

```hlsl
roughness = mrTex.g * roughnessFactor;
metallic = mrTex.b * metallicFactor;
```

## 13. 常见问题

### 模型加载失败

优先检查：

- 是不是 `.glb`？当前项目默认主要支持 `.gltf`。
- `.gltf` 引用的 `.bin` 或贴图路径是否还存在？
- 路径中是否有中文或特殊字符？建议先避免。
- 模型是否用了 Draco 压缩？

### 模型能显示，但全黑

可能原因：

- shader 没有正确使用法线。
- 光源位置或相机位置不对。
- 材质贴图没有绑定。
- baseColor 是 sRGB，但你当 linear 直接算，亮度可能不对。
- normal 方向被 FlipY 或模型变换影响了。

### 贴图颜色不对

检查：

- baseColor 应按 sRGB 处理。
- normal、metallic、roughness、AO 应按 linear 数据贴图处理。
- metallicRoughness 通道是否读对：roughness 在 G，metallic 在 B。

### 贴图上下反了

尝试 `FlipUV`，但不要盲目一直开。不同来源模型贴图约定可能不同。

### 动画模型不对

如果模型依赖骨骼动画，不建议使用 `PreTransformVertices`。这个 flag 对静态模型很舒服，但会把节点变换烘焙进顶点，可能影响动画流程。

## 14. 我的建议路线

如果你准备把 Lab2 往“能展示网上 PBR 模型”的方向推进，建议按这个顺序：

1. 先加载 Khronos Sample Assets 里的简单静态模型。
2. 只显示 position/normal，不接材质贴图。
3. 加入 UV 和 baseColorTexture。
4. 加入 metallicRoughnessTexture。
5. 加入 normalTexture 和 tangent/TBN。
6. 加入 emissive 和 alpha mask。
7. 最后再考虑 `.glb`、动画、Draco、复杂扩展。

这样每一步都可验证，不容易把 loader、descriptor、shader、资源格式几个问题搅在一起。

