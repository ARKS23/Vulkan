# First Example 实操指南

这份文档是给你明天动手写第一个 Vulkan sample 用的。

目标不是一上来就做复杂效果，而是先把下面这条主线亲手打通：

```text
创建自己的 example
-> 接入自己的 HLSL shader
-> 编译成 SPIR-V
-> 跑通一帧渲染
-> 把三角形改成圆形
```

如果这条链你能自己完整走一遍，后面再过渡到 `Games202` 实验室就会顺很多。

## 1. 先选哪个 sample 当模板

我建议你直接以 [`examples/trianglevulkan13/trianglevulkan13.cpp`](../examples/trianglevulkan13/trianglevulkan13.cpp) 为模板。

原因：

- 它保留了最核心的 Vulkan 图形主线：`acquire -> update UBO -> record -> draw -> present`
- 它已经是 Vulkan 1.3 + dynamic rendering 路线
- 它比传统 `triangle.cpp` 少了一层 `render pass / framebuffer` 负担
- 它更接近你以后自己做 `games202lab` 时会想保留的风格

如果你中途看不懂某一段，再回头把 [`examples/triangle/triangle.cpp`](../examples/triangle/triangle.cpp) 当“对照组”来看。

## 2. 明天的目标建议

我建议你不要把目标定成“做很多功能”，而是定成下面这 4 步：

1. 从 `trianglevulkan13` 复制出你自己的 example。
2. 用你自己的 HLSL shader 跑通一个“还是三角形”的版本。
3. 把顶点/索引数据改成圆形。
4. 最后再做一点小变化，比如颜色、半径、分段数或轻微动画。

这样你会先把工程链路跑通，再改几何，不容易同时被两类问题卡住。

## 3. 这个项目里怎么新增一个自己的 example

这个仓库的 example 不是“每个目录一个自己的 CMakeLists”。

这里的组织方式是：

- 源码目录放在 `examples/<example_name>/`
- shader 目录放在 `shaders/glsl/<example_name>/`、`shaders/hlsl/<example_name>/` 或 `shaders/slang/<example_name>/`
- 最后只需要把名字加到 [`examples/CMakeLists.txt`](../examples/CMakeLists.txt) 的 `EXAMPLES` 列表里

也就是说，新增 example 的核心就是三件事：

1. 新建 `examples/<你的名字>/`
2. 新建 `shaders/hlsl/<你的名字>/`
3. 把 `<你的名字>` 加进 `examples/CMakeLists.txt`

## 4. 我建议你用的 example 名字

建议先不要起太花哨的名字，先用一个稳定、短小、全小写的名字。

比如：

- `firstexample`
- `circlelab`
- `games202lab`

如果只是明天的第一次练手，我更推荐：

```text
firstexample
```

因为它最直观，也方便你后面再新开一个更长期的 `games202lab`。

下面我都以 `firstexample` 为例。

## 5. 新建 example 的推荐操作顺序

### 5.1 复制代码目录

从：

- [`examples/trianglevulkan13/`](../examples/trianglevulkan13/)

复制一份到：

- `examples/firstexample/`

然后把主文件改名为：

- `examples/firstexample/firstexample.cpp`

这里最省心的方式不是从零写，而是先保留 `trianglevulkan13` 的整体骨架。

## 5.2 把 example 注册到 CMake

打开：

- [`examples/CMakeLists.txt`](../examples/CMakeLists.txt)

在 `set(EXAMPLES ... )` 里面加入：

```cmake
firstexample
```

注意这里很重要：

- 这个仓库的 example 目标就是靠这个列表生成的
- 如果你忘了加，CMake 根本不会生成 `firstexample` 这个 target

## 5.3 新建 HLSL shader 目录

新建目录：

- `shaders/hlsl/firstexample/`

这里建议你先直接复制：

- [`shaders/hlsl/triangle/triangle.vert`](../shaders/hlsl/triangle/triangle.vert)
- [`shaders/hlsl/triangle/triangle.frag`](../shaders/hlsl/triangle/triangle.frag)

到你的目录里。

例如你可以命名成：

- `shaders/hlsl/firstexample/circle.vert`
- `shaders/hlsl/firstexample/circle.frag`

这里有一个非常容易踩坑的点：

- 这个仓库里的 HLSL 文件扩展名仍然是 `.vert` / `.frag`
- 不是 `.hlsl`

编译脚本就是靠这些扩展名来判断 shader stage 的。

## 5.4 在代码里改 shader 路径

在你的 [`firstexample.cpp`](../examples/firstexample/firstexample.cpp) 里，找到 pipeline 创建时加载 shader 的位置。

像 `trianglevulkan13.cpp` 现在是这样：

```cpp
shaderStages[0].module = loadSPIRVShader(getShadersPath() + "triangle/triangle.vert.spv");
shaderStages[1].module = loadSPIRVShader(getShadersPath() + "triangle/triangle.frag.spv");
```

你要改成你自己的路径，例如：

```cpp
shaderStages[0].module = loadSPIRVShader(getShadersPath() + "firstexample/circle.vert.spv");
shaderStages[1].module = loadSPIRVShader(getShadersPath() + "firstexample/circle.frag.spv");
```

这里的 `getShadersPath()` 很关键。

它最终会根据运行参数去选择：

- `shaders/glsl/`
- `shaders/hlsl/`
- `shaders/slang/`

所以这行代码本身不会写死“用 HLSL 还是 GLSL”，它只写：

```text
firstexample/circle.vert.spv
```

真正决定走哪种 shader 目录的是运行参数：

```text
-s hlsl
```

## 5.5 编译 HLSL 到 SPIR-V

这个仓库不会在 CMake 阶段自动把你的 `.vert` / `.frag` 编译成 `.spv`。

`CMake` 的作用主要是：

- 把 shader 文件显示到 IDE 工程里
- 让 example target 知道有哪些源码和资源文件

真正的 shader 编译要靠脚本：

- [`shaders/hlsl/compileshaders.py`](../shaders/hlsl/compileshaders.py)

在仓库根目录下你可以这样做：

```powershell
cd shaders/hlsl
python compileshaders.py --sample firstexample
```

如果 `dxc` 不在 `PATH` 里，就要显式指定：

```powershell
python compileshaders.py --sample firstexample --dxc "C:\path\to\dxc.exe"
```

编译完成后，你应该能看到：

- `shaders/hlsl/firstexample/circle.vert.spv`
- `shaders/hlsl/firstexample/circle.frag.spv`

## 5.6 运行时切到 HLSL

这个项目默认 shader 类型是 `glsl`。

所以如果你只加了 HLSL 文件，而没有加 `glsl` 对应目录，那么运行时一定要带：

```text
-s hlsl
```

例如：

```powershell
.\bin\firstexample.exe -s hlsl
```

如果你忘了这个参数，程序会去 `shaders/glsl/firstexample/` 下面找 `.spv`，然后报找不到文件。

## 6. 你明天最推荐的路线：先跑三角形，再改圆形

不要一开始就“复制 sample + 改 HLSL + 改几何 + 改结构”同时做。

更稳妥的顺序是：

### 阶段 A：先跑你自己的 HLSL 三角形

先保证这些都正确：

- `firstexample` target 能生成
- shader 路径改对了
- `circle.vert/.frag` 已经编译出 `.spv`
- 运行时记得 `-s hlsl`

只要这一版先画出三角形，你就已经打通了：

- 自己的 example 目录
- 自己的 shader 目录
- HLSL 编译链路
- Vulkan 的一帧提交流程

### 阶段 B：再把三角形改成圆形

我建议你的“第一个圆形”不要用片段着色器 `discard` 去画，也不要先上 SDF。

第一版最适合你的方式是：

- **在 CPU 侧生成一个近似圆的三角形网格**

原因：

- 你能继续复用现在这套 `vertex buffer + index buffer + draw indexed`
- 这更符合“学习整体渲染流程”的目标
- 你会真正练到顶点数据组织，而不是把几何问题藏进 fragment shader

## 7. 第一个圆形应该怎么做

最简单的做法是做一个“扇形三角剖分”的圆盘。

思路：

1. 放一个中心点顶点
2. 沿圆周采样 `N` 个点
3. 每一小段圆弧都和中心点组成一个三角形

你可以把顶点组织成这样：

```text
v0 = center
v1 ... vN = circle ring
```

然后索引可以写成：

```text
(0, 1, 2)
(0, 2, 3)
(0, 3, 4)
...
(0, N, 1)
```

这样你仍然可以保留：

```cpp
VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
```

不用去改成别的 topology。

### 7.1 推荐的圆参数

第一版可以这样设：

- 半径：`0.8f`
- 分段数：`64`

`64` 段在学习阶段已经足够看起来像个圆。

### 7.2 圆周顶点生成公式

```cpp
float angle = 2.0f * pi * i / segmentCount;
float x = radius * cos(angle);
float y = radius * sin(angle);
```

然后把它塞进和当前 sample 一样的 `Vertex` 结构里即可。

### 7.3 为什么我推荐这种做法

因为它能帮你练到这几件事：

- CPU 如何生成网格数据
- 顶点缓冲和索引缓冲如何上传
- `draw indexed` 的真实几何输入是什么
- shader 输入 `location` 如何对应到顶点布局

这几件事比“先把一个圆显示出来”本身更重要。

## 8. 你需要重点改哪些代码

如果你是从 `trianglevulkan13.cpp` 复制出来的，我建议你只优先盯这几个位置：

### 8.1 构造函数

主要改：

- `title`

其他像：

- `apiVersion = VK_API_VERSION_1_3`
- `enabledFeatures.dynamicRendering = VK_TRUE`
- 相机初始化

第一天都可以先保持不动。

### 8.2 `createPipeline()`

这里主要改：

- 你的 shader 文件路径

第一天先不要改 pipeline 其他状态。

### 8.3 `createVertexBuffer()`

这是你明天最应该自己动手改的函数。

你要做的是把原来的：

- 3 个三角形顶点
- 3 个索引

换成：

- 1 个中心点
- `N` 个圆周点
- 对应的三角剖分索引

这一步是你明天真正“学到东西”的核心。

### 8.4 `createUniformBuffers()` / `createDescriptors()` / `render()`

这几块第一天可以尽量不改。

因为它们已经很好地展示了：

- UBO 的创建和更新
- descriptor 的绑定方式
- 每帧 command buffer 的录制和提交

你先把这几块当成“稳定底座”更合适。

## 9. 一个很实用的 HLSL 最小模板

如果你不想直接复制 `triangle` 的 HLSL，这里给你一个能和当前顶点布局匹配的最小写法。

### 9.1 `circle.vert`

```hlsl
cbuffer UBO : register(b0)
{
    float4x4 projectionMatrix;
    float4x4 modelMatrix;
    float4x4 viewMatrix;
};

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float3 color    : COLOR0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float3 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(modelMatrix, float4(input.position, 1.0));
    float4 viewPos = mul(viewMatrix, worldPos);
    output.position = mul(projectionMatrix, viewPos);
    output.color = input.color;
    return output;
}
```

### 9.2 `circle.frag`

```hlsl
struct PSInput
{
    [[vk::location(0)]] float3 color : COLOR0;
};

float4 main(PSInput input) : SV_TARGET0
{
    return float4(input.color, 1.0);
}
```

注意：

- entry point 要叫 `main`
- `.vert` 会被脚本按 vertex shader 编译
- `.frag` 会被脚本按 fragment shader 编译

## 10. 明天建议你按这个顺序做

### 第 1 轮：15 到 20 分钟

先完成工程搭建：

1. 复制 `trianglevulkan13`
2. 改目录名和主文件名
3. 在 `examples/CMakeLists.txt` 注册 `firstexample`
4. 新建 `shaders/hlsl/firstexample/`
5. 复制 `triangle` 的 HLSL shader

### 第 2 轮：20 到 30 分钟

先跑通你自己的 HLSL 三角形：

1. 改 shader 路径
2. 编译 `firstexample` 的 HLSL 到 `.spv`
3. 重新生成 / 编译 CMake target
4. 用 `-s hlsl` 运行

这一步成功后，你已经赢了一半。

### 第 3 轮：30 到 45 分钟

改圆形：

1. 在 `createVertexBuffer()` 里生成圆心和圆周点
2. 生成索引
3. 保持 pipeline 和 shader 不变
4. 再次运行验证

### 第 4 轮：10 到 20 分钟

做一点小扩展：

- 改颜色渐变
- 改分段数
- 改半径
- 让圆绕 `z` 轴转一点点

## 11. 常见坑

### 11.1 CMake target 没生成

通常是因为你忘了把 `firstexample` 加进：

- [`examples/CMakeLists.txt`](../examples/CMakeLists.txt)

### 11.2 shader 明明写了，但程序说找不到

通常是下面几种原因：

- 忘了先编译 `.spv`
- 忘了运行时加 `-s hlsl`
- 代码里写的是 `triangle/...`，但你的目录已经叫 `firstexample/...`
- shader 文件名和代码里加载的名字不一致

### 11.3 HLSL 编不过

先检查：

- `dxc` 是否可用
- entry point 是否叫 `main`
- 输入输出 `location` 是否和顶点布局匹配

### 11.4 圆形画出来像裂开或少一块

通常是：

- 最后一段索引没有闭合回第一个圆周点
- 分段数和顶点数的循环边界写错了

## 12. 做完这个 sample 后，下一步怎么接 Games202

当你把这个 `firstexample` 跑通后，我建议你下一步不要立刻大改框架，而是按这个顺序加能力：

1. `texture`
2. `descriptorsets`
3. `pushconstants`
4. `offscreen`
5. `shadowmapping`

等你对这些资源和 pass 组织方式更熟之后，再单独开一个：

```text
games202lab
```

那时候你再开始考虑：

- VMA
- 自己的 `AllocatedBuffer / AllocatedImage`
- 通用 mesh / material / pass 封装

会比现在直接重构稳很多。

## 13. 我对你明天这次练手的总建议

明天最重要的不是“写出最漂亮的圆”，而是你能自己回答这几个问题：

1. 我的 shader 是从哪个目录被加载的？
2. HLSL 是怎么变成 `.spv` 的？
3. 顶点和索引数据是怎么上传到 GPU 的？
4. 这一帧的 command buffer 是在哪里录的？
5. 为什么这个 sample 可以不写传统 `render pass / framebuffer`？

如果你自己能说清楚这 5 个问题，那这次练手就已经非常成功了。

## 14. 配套阅读

建议你边做边参考这几份文档：

- [`Triangle13_Overview.md`](./Triangle13_Overview.md)
- [`Triangle_Overview.md`](./Triangle_Overview.md)
- [`HLSL.md`](./HLSL.md)

如果你愿意，我下一步可以继续帮你把这份文档再往前推一步，直接给你列一个“`firstexample.cpp` 应该从 `trianglevulkan13.cpp` 改哪几处”的精确修改清单。
