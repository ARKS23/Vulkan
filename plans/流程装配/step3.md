# Step 3：学习 Push Constant，区分 per-frame 和 per-draw 数据

你已经完成 Step 2：

```text
vertex buffer  -> VMA AllocatedBuffer
index buffer   -> VMA AllocatedBuffer
uniform buffer -> VMA AllocatedBuffer
```

现在非常适合进入 Step 3：学习 Push Constant。

这一轮目标很小：

```text
给 Lab0 增加一个 push constant
用它控制圆盘的颜色倍率
不改 descriptor
不改 vertex layout
不改 mesh 数据
不引入 texture
```

完成后你会理解一个非常重要的分工：

```text
UBO
-> 适合 per-frame / per-camera / 较大一点的数据
-> 比如 projection、view、model、light 参数数组

Push Constant
-> 适合很小的 per-draw 数据
-> 比如当前 draw 的颜色、object id、material id、小矩阵或开关参数
```

这一步是从“单物体 sample”走向“多物体渲染”的关键前置知识。

## 1. 先读哪个 sample

建议读：

```text
examples/pushconstants/pushconstants.cpp
shaders/hlsl/pushconstants/pushconstants.vert
shaders/hlsl/pushconstants/pushconstants.frag
```

阅读时不要被 glTF 模型和多个 sphere 分散注意力。

只看三件事：

```text
1. C++ 里定义了 push constant 数据结构
2. pipeline layout 里声明了 VkPushConstantRange
3. command buffer 里调用 vkCmdPushConstants
```

核心代码在 `examples/pushconstants/pushconstants.cpp`：

```cpp
VkPushConstantRange pushConstantRange{};
pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
pushConstantRange.offset = 0;
pushConstantRange.size = sizeof(SpherePushConstantData);

pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
```

以及 draw 前：

```cpp
vkCmdPushConstants(
    cmdBuffer,
    pipelineLayout,
    VK_SHADER_STAGE_VERTEX_BIT,
    0,
    sizeof(SpherePushConstantData),
    &spheres[j]);
```

你要抓住这条数据流：

```text
C++ struct
-> pipeline layout 声明这个 struct 的大小和 shader stage
-> command buffer 录入 vkCmdPushConstants
-> shader 通过 [[vk::push_constant]] 读取
```

## 2. 本轮推荐做法

本轮我们给 fragment shader 加一个颜色倍率：

```cpp
struct PushConstantData {
    glm::vec4 colorMultiplier;
};
```

shader 里：

```hlsl
struct PushConstants {
    float4 colorMultiplier;
};

[[vk::push_constant]]
PushConstants pushConstants;
```

fragment 输出：

```hlsl
return float4(Color * pushConstants.colorMultiplier.rgb, 1.0);
```

为什么先放 fragment shader？

```text
因为你当前圆盘已经有 vertex color
fragment shader 只需要乘一个颜色倍率
不需要改 vertex input
不需要改 UBO
不需要改 descriptor
```

这就是最小闭环。

## 3. 先让 Lab0 拥有自己的 HLSL shader

你当前 Lab0 还在加载：

```cpp
getShadersPath() + "triangle/triangle.vert.spv"
getShadersPath() + "triangle/triangle.frag.spv"
```

这个可以跑，但不适合继续改。

因为你接下来要改 shader 接口，如果直接改 `shaders/hlsl/triangle`，会影响原始 triangle sample。

建议新建：

```text
shaders/hlsl/lab0/lab0.vert
shaders/hlsl/lab0/lab0.frag
```

先把下面两个文件内容复制过去：

```text
shaders/hlsl/triangle/triangle.vert
shaders/hlsl/triangle/triangle.frag
```

然后把 `lab0.cpp` 的 pipeline shader 加载路径改成：

```cpp
shaderStages[0].module = loadSPIRVShader(getShadersPath() + "lab0/lab0.vert.spv");
shaderStages[1].module = loadSPIRVShader(getShadersPath() + "lab0/lab0.frag.spv");
```

这样以后 Lab0 的 shader 就完全归你自己控制。

如果你暂时想最小改动，也可以继续改 `triangle` shader。
但我的建议是：从 Step 3 开始给 Lab0 独立 shader。

## 4. 修改 HLSL fragment shader

`lab0.vert` 可以先保持和 triangle vertex shader 一样。

重点改：

```text
shaders/hlsl/lab0/lab0.frag
```

从：

```hlsl
float4 main([[vk::location(0)]] float3 Color : COLOR0) : SV_TARGET
{
  return float4(Color, 1.0);
}
```

改成：

```hlsl
struct PushConstants {
    float4 colorMultiplier;
};

[[vk::push_constant]]
PushConstants pushConstants;

float4 main([[vk::location(0)]] float3 Color : COLOR0) : SV_TARGET
{
    return float4(Color * pushConstants.colorMultiplier.rgb, 1.0);
}
```

这里要注意：

```text
HLSL 里的 float4
对应 C++ 里的 glm::vec4
大小都是 16 bytes
```

这能避免 push constant 数据布局踩坑。

## 5. 在 lab0.h 里定义 PushConstantData

在 `VulkanExample` 类里加：

```cpp
struct PushConstantData {
    glm::vec4 colorMultiplier{ 1.0f };
};
```

建议放在 `ShaderData` 附近。

你也可以暂时不把它做成成员变量，只在 `render()` 里创建局部变量。

本轮最小推荐：

```cpp
struct PushConstantData {
    glm::vec4 colorMultiplier{ 1.0f };
};
```

然后在 `lab0.cpp` 或 `lab0.h` 里加一个静态检查：

```cpp
static_assert(sizeof(VulkanExample::PushConstantData) == sizeof(glm::vec4));
```

如果你不想在类外写这个，也可以先不加。
但长期建议保留这种数据布局检查习惯。

## 6. 在 createPipeline 里声明 push constant range

Push constant 必须写进 pipeline layout。

你当前 `createPipeline()` 里大概有：

```cpp
VkPipelineLayoutCreateInfo pipelineLayoutCI = vkinit::pipelineLayoutCreateInfo();
pipelineLayoutCI.setLayoutCount = 1;
pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));
```

改成：

```cpp
VkPushConstantRange pushConstantRange{};
pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
pushConstantRange.offset = 0;
pushConstantRange.size = sizeof(PushConstantData);

VkPipelineLayoutCreateInfo pipelineLayoutCI = vkinit::pipelineLayoutCreateInfo();
pipelineLayoutCI.setLayoutCount = 1;
pipelineLayoutCI.pSetLayouts = &descriptorSetLayout;
pipelineLayoutCI.pushConstantRangeCount = 1;
pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));
```

这里的 `stageFlags` 必须和 shader 使用位置一致。

本轮 fragment shader 读取 push constant，所以写：

```cpp
VK_SHADER_STAGE_FRAGMENT_BIT
```

如果以后 vertex shader 也要读取，比如用 push constant 控制 model matrix，就要改成：

```cpp
VK_SHADER_STAGE_VERTEX_BIT
```

或者两个都能访问：

```cpp
VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
```

## 7. 在 render() 里调用 vkCmdPushConstants

在 command buffer 录制 draw 前加入：

```cpp
PushConstantData pushData{};
pushData.colorMultiplier = glm::vec4(1.0f, 0.85f, 0.35f, 1.0f);

vkCmdPushConstants(
    commandBuffer,
    pipelineLayout,
    VK_SHADER_STAGE_FRAGMENT_BIT,
    0,
    sizeof(PushConstantData),
    &pushData);
```

建议放在这些命令之后：

```cpp
vkCmdBindDescriptorSets(...)
vkCmdBindPipeline(...)
```

并且放在：

```cpp
vkCmdDrawIndexed(...)
```

之前。

完整顺序可以是：

```cpp
vkCmdBindDescriptorSets(...);
vkCmdBindPipeline(...);

PushConstantData pushData{};
pushData.colorMultiplier = glm::vec4(1.0f, 0.85f, 0.35f, 1.0f);
vkCmdPushConstants(
    commandBuffer,
    pipelineLayout,
    VK_SHADER_STAGE_FRAGMENT_BIT,
    0,
    sizeof(PushConstantData),
    &pushData);

vkCmdBindVertexBuffers(...);
vkCmdBindIndexBuffer(...);
vkCmdDrawIndexed(...);
```

`vkCmdPushConstants` 本质上是录进 command buffer 的状态。

你可以把它理解成：

```text
从这里开始，后面的 draw 使用这一小块 push constant 数据
直到你再次 push 新数据
```

## 8. 可选：让颜色动起来

先跑通固定颜色倍率。

跑通后可以再改成轻微变化：

```cpp
static float colorPhase = 0.0f;
colorPhase += 0.02f;
const float pulse = 0.5f + 0.5f * std::sin(colorPhase);

PushConstantData pushData{};
pushData.colorMultiplier = glm::vec4(1.0f, 0.5f + 0.5f * pulse, 0.35f, 1.0f);
```

这样你会看到颜色随时间变化。

这能帮助你理解：

```text
push constant 可以每帧更新
也可以每个 draw 更新
它不需要 descriptor set
也不需要 buffer allocation
```

## 9. 编译前检查清单

检查 C++：

```powershell
rg -n "PushConstantData|VkPushConstantRange|vkCmdPushConstants|pushConstantRangeCount" examples/Lab0
```

应该能看到：

```text
lab0.h
-> PushConstantData

lab0.cpp
-> VkPushConstantRange
-> pipelineLayoutCI.pushConstantRangeCount
-> vkCmdPushConstants
```

检查 shader：

```powershell
rg -n "push_constant|colorMultiplier" shaders/hlsl/lab0
```

应该能看到：

```text
lab0.frag
-> [[vk::push_constant]]
-> colorMultiplier
```

检查 Lab0 是否还在加载 triangle shader：

```powershell
rg -n "triangle/triangle|lab0/lab0" examples/Lab0/lab0.cpp
```

理想结果：

```text
只看到 lab0/lab0.vert.spv
只看到 lab0/lab0.frag.spv
```

## 10. 编译和运行

编译：

```powershell
cmake --build build --config Debug --target lab0 -j 32
```

如果新建了 shader 文件但编译系统没有识别，先重新 configure CMake。

如果出现 shader 文件找不到：

```text
确认 shaders/hlsl/lab0/lab0.vert 存在
确认 shaders/hlsl/lab0/lab0.frag 存在
确认 C++ 加载路径是 lab0/lab0.vert.spv 和 lab0/lab0.frag.spv
确认 shader 已经被编译成 .spv
```

运行：

```powershell
build\bin\Debug\lab0.exe -v -vl
```

成功标准：

```text
画面仍然是圆盘
颜色发生了 push constant 控制的变化
validation layer 没有新增错误
```

## 11. 常见错误

如果 validation 报 push constant range 不匹配：

```text
C++ 的 VkPushConstantRange.stageFlags
必须覆盖 shader 实际读取 push constant 的 stage
```

本轮 shader 在 fragment 读取，所以：

```cpp
VK_SHADER_STAGE_FRAGMENT_BIT
```

如果 shader 编译失败：

```text
确认 HLSL 写法使用 [[vk::push_constant]]
确认 push constant 结构体不要写成 cbuffer register(b0)
确认已有 UBO 仍然是 cbuffer ubo : register(b0)
```

如果画面颜色没有变化：

```text
确认 Lab0 加载的是 lab0/lab0.frag.spv，不是 triangle/triangle.frag.spv
确认 vkCmdPushConstants 在 vkCmdDrawIndexed 前
确认 stageFlags 是 VK_SHADER_STAGE_FRAGMENT_BIT
确认 fragment shader 最终返回乘了 colorMultiplier
```

如果程序启动后关闭：

```text
优先看 shader 路径是否错误
loadSPIRVShader 找不到文件时通常会返回 VK_NULL_HANDLE
后续 pipeline 创建就可能失败
```

## 12. Push Constant 和 UBO 的边界

这一步完成后，你应该形成这个习惯：

```text
Camera / projection / view
-> UBO

每帧全局光照参数
-> UBO 或 SSBO

每个 draw 的小参数
-> Push Constant

大量 per-object 数据
-> Dynamic UBO / SSBO

材质纹理
-> Descriptor image sampler
```

不要把所有东西都塞 push constant。

Vulkan 规范只保证至少：

```text
128 bytes push constant
```

很多 GPU 支持更多，但学习阶段按 128 bytes 思考最稳。

所以 push constant 适合：

```text
glm::vec4 color
uint materialIndex
uint objectId
小的 flag
单个 mat4
```

不适合：

```text
大量灯光数组
大量材质参数
骨骼矩阵
整份 scene object data
```

## 13. Step 3 完成后的下一步

完成 push constant 后，我建议你做一个很小的延伸练习：

```text
把圆盘画两次
每次 vkCmdPushConstants 给不同 colorMultiplier
每次 vkCmdDrawIndexed 一次
```

这会让你真正理解：

```text
push constant 是 per-draw 状态
同一个 pipeline
同一个 vertex/index buffer
同一个 descriptor set
可以通过不同 push constant 画出不同结果
```

不过如果要让两个圆盘位置不同，你还需要把 model matrix 或 position offset 放进 vertex shader 的 push constant。

那可以作为 Step 3.5：

```text
PushConstantData 增加 glm::vec4 positionOffset
vertex shader 读取 push constant
C++ stageFlags 改成 VERTEX | FRAGMENT
draw 两次，每次不同 positionOffset 和 colorMultiplier
```

这一步做完，你就从“画一个对象”迈向“循环 draw 多个对象”了。

