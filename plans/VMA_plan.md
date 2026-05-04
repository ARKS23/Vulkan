# VMA Integration Plan

## 背景

这个仓库当前的资源分配方式，整体还是 Sascha Willems 风格的原生 Vulkan 路线：

- 自己找 memory type
- 自己 `vkAllocateMemory`
- 自己 `vkBindBufferMemory` / `vkBindImageMemory`
- `vks::Buffer` 和 `vks::Texture` 直接持有 `VkDeviceMemory`

这对学习 Vulkan 很有价值，但如果目标是逐步长成一个更像 `vk-guide` 的实验框架，就会遇到几个问题：

1. buffer / image 分配逻辑会在不同 example 中重复出现。
2. 一旦开始做 mesh、texture、offscreen、G-buffer、shadow map，内存管理会越来越碎。
3. 以后想做资源池、上传通道、deletion queue、frame graph 时，手写内存分配会成为噪音。

因此，建议引入 VMA，但采用“渐进式接入”，而不是一次性重构整个仓库。

## 目标

这次设计的目标不是“把仓库所有代码都改成 VMA”，而是：

1. 给 `base/` 层接入统一的 `VmaAllocator`。
2. 给后续新代码提供 VMA-native 的 buffer / image 封装。
3. 保持现有 example 尽量不受影响，尤其是 `triangle` 这类教学样例。
4. 为未来的 `games202lab` 或自定义 example 提供更工程化的资源管理入口。

## 非目标

下面这些事情不建议在第一阶段做：

1. 不做全仓库一次性迁移。
2. 不立刻把 `vks::Buffer` 和 `vks::Texture` 全部改成 VMA 后端。
3. 不为了 VMA 破坏 `triangle` 当前“教学展开”的价值。
4. 不在第一版里引入完整 render graph 或资源系统。

## 总体策略

推荐策略：`双轨并行，新的用 VMA，旧的先保留`

具体来说：

1. 在 `base/` 层新增 VMA 支持。
2. 将 `VmaAllocator` 绑定到 `vks::VulkanDevice` 的生命周期上。
3. 新增一套 VMA-native 的资源结构体和辅助函数。
4. 新 example 和你自己的实验代码优先走 VMA。
5. 现有 example 继续沿用原始 `VkDeviceMemory` 路线，后续按需迁移。

这样做的好处是：

- 风险低
- 不影响现有样例的教学用途
- 你可以很快开始写自己的框架
- 后面想迁移时有清晰落点

## 不推荐的方案

### 方案 A：直接把所有 buffer / image 代码全改成 VMA

不推荐，原因是：

1. 改动面太大。
2. 很容易把教学代码和工程代码混在一起。
3. 回归成本高，很难判断是 VMA 接入问题还是原仓库逻辑问题。

### 方案 B：每个新 example 自己单独 include VMA，自行管理 allocator

也不推荐，原因是：

1. allocator 生命周期会分散。
2. 会把“框架职责”重新打回 sample 层。
3. 后面统一管理资源和上传逻辑会很别扭。

## 推荐架构

### 1. 把 VMA 放到 `vks::VulkanDevice` 里

这是最合适的落点。

原因：

1. `VulkanDevice` 已经封装了 physical device、logical device、queue family、command pool。
2. allocator 生命周期天然依附于 `VkInstance + VkPhysicalDevice + VkDevice`。
3. `vks::Texture`、`vks::Buffer`、未来的资源辅助函数，本来就经常拿到 `vks::VulkanDevice*`。

建议新增成员：

```cpp
VmaAllocator allocator = VK_NULL_HANDLE;
bool allocatorInitialized = false;
```

并新增辅助接口，例如：

```cpp
void initAllocator(VkInstance instance, uint32_t apiVersion);
void destroyAllocator();
```

其中：

- `initAllocator()` 在逻辑设备创建成功后调用。
- `destroyAllocator()` 必须发生在 `vkDestroyDevice()` 之前。

### 2. 不要立即改造 `vks::Buffer`

当前 [`base/VulkanBuffer.h`](../base/VulkanBuffer.h) 的 `vks::Buffer` 明确是原始 Vulkan 所有权模型：

- `VkBuffer`
- `VkDeviceMemory`
- `mapped`
- `flush/invalidate/destroy`

如果第一阶段强行让它同时支持 VMA，会出现“双重所有权语义”：

1. 到底是 `vkFreeMemory` 还是 `vmaDestroyBuffer`？
2. `mapped` 是自己 map 的还是 VMA persistent mapping 给的？
3. `destroy()` 里怎么区分 raw memory 和 VMA allocation？

所以第一阶段建议：

- 保留 `vks::Buffer` 不动
- 新增 `AllocatedBuffer`

### 3. 新增 VMA-native 资源结构

建议新增一个新头文件，例如：

- `base/VulkanAllocation.h`

内部定义：

```cpp
struct AllocatedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo info{};
    VkDescriptorBufferInfo descriptor{};
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};

struct AllocatedImage {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo info{};
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
};
```

第一阶段重点只做这两类对象即可。

## 推荐新增文件

### `base/VulkanVMA.h`

职责：

1. 统一 include `vk_mem_alloc.h`
2. 集中放置 VMA 相关宏配置
3. 避免项目里到处直接 include 第三方头

建议内容形态：

```cpp
#pragma once

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
```

如果后面需要开 VMA 的编译宏，例如禁用静态函数加载、配合 Volk、开启 asserts，也统一在这里管。

### `base/VulkanVMA.cpp`

职责：

1. 作为唯一一个 `#define VMA_IMPLEMENTATION` 的翻译单元

例如：

```cpp
#define VMA_IMPLEMENTATION
#include "VulkanVMA.h"
```

这是必须遵守的约束。不要把 `VMA_IMPLEMENTATION` 放到头文件里。

### `base/VulkanAllocation.h`

职责：

1. 定义 `AllocatedBuffer`
2. 定义 `AllocatedImage`
3. 预留少量与资源所有权有关的辅助函数声明

### 可选：`base/VulkanAllocation.cpp`

如果后续你不想把所有 VMA helper 都塞进 `VulkanDevice.cpp`，可以把 VMA 的 buffer / image 创建与销毁逻辑放到这里。

## 第三方库放置建议

这个仓库已经把第三方库都放在 `external/` 下，所以 VMA 也建议放进去。

推荐两种方式：

### 方式 1：只 vendoring 单头文件

目录示例：

```text
external/vma/vk_mem_alloc.h
```

优点：

- 最简单
- 和当前仓库风格兼容
- 接入成本最低

这是第一推荐。

### 方式 2：保留完整仓库结构

目录示例：

```text
external/VulkanMemoryAllocator/include/vk_mem_alloc.h
```

优点：

- 后续看文档和样例更方便

缺点：

- CMake include path 稍微啰嗦一点

如果只是为了这个仓库接入，没必要一开始就上完整仓库布局。

## CMake 改动建议

### 根 `CMakeLists.txt`

当前已经有：

- `include_directories(external)`
- `include_directories(base)`

如果采用 `external/vma/vk_mem_alloc.h` 布局，建议再加：

```cmake
include_directories(external/vma)
```

### `base/CMakeLists.txt`

需要把新文件纳入 `base` 静态库：

- `VulkanVMA.h`
- `VulkanVMA.cpp`
- `VulkanAllocation.h`
- 可选 `VulkanAllocation.cpp`

重点是保证 `VulkanVMA.cpp` 一定被编译到 `base` 库中。

## `VulkanDevice` 接入建议

### 新增成员

建议在 [`base/VulkanDevice.h`](../base/VulkanDevice.h) 中新增：

```cpp
VmaAllocator allocator{ VK_NULL_HANDLE };
bool allocatorInitialized{ false };
```

并增加接口：

```cpp
void initAllocator(VkInstance instance, uint32_t apiVersion);
void destroyAllocator();
```

### 初始化时机

建议在 `createLogicalDevice()` 成功之后，由上层显式调用：

```cpp
vulkanDevice->initAllocator(instance, apiVersion);
```

更具体地说，推荐在 [`base/vulkanexamplebase.cpp`](../base/vulkanexamplebase.cpp) 的 `initVulkan()` 中：

1. `vkCreateDevice` 完成
2. `device = vulkanDevice->logicalDevice`
3. `vkGetDeviceQueue(...)`
4. `vulkanDevice->initAllocator(instance, apiVersion)`

这样 allocator 生命周期和 `VulkanExampleBase` 的设备初始化路径一致。

### 销毁时机

建议在 `VulkanDevice::~VulkanDevice()` 中：

1. 若 allocator 已初始化，则先 `vmaDestroyAllocator`
2. 再销毁 command pool
3. 最后 `vkDestroyDevice`

注意：

- 从资源生命周期上讲，真正的 buffer / image 应该先于 allocator 被销毁。
- allocator 析构只是最后兜底，不能依赖它替你回收所有 sample 资源。

## VMA 创建参数建议

第一阶段保持保守，先只填核心字段：

```cpp
VmaAllocatorCreateInfo allocatorInfo{};
allocatorInfo.instance = instance;
allocatorInfo.physicalDevice = physicalDevice;
allocatorInfo.device = logicalDevice;
allocatorInfo.vulkanApiVersion = apiVersion;
```

### 可选 flag

后续可以按能力逐步加：

1. `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`
2. `VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT`

但不建议第一天全开，原因是：

- 你需要和实际启用的 device features / extensions 对齐
- 这个仓库 example 覆盖面很广，不同平台组合比较多

更稳的做法是：

1. 第一阶段只用最小配置跑通
2. 第二阶段再按 `games202lab` 的真实需求启用进阶 flag

## VMA helper API 设计建议

不要把所有 VMA 调用散落到 example 里。建议集中做几类 helper。

### Buffer 创建

建议接口形态：

```cpp
AllocatedBuffer createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaMemoryUsage memoryUsage,
    VmaAllocationCreateFlags flags = 0);
```

或者更现代一点，直接用 VMA 3.x 推荐的 `AUTO` 路线：

```cpp
AllocatedBuffer createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaAllocationCreateFlags flags,
    const char* debugName = nullptr);
```

内部根据用途决定：

- GPU only
- CPU to GPU
- GPU to CPU

### Buffer 销毁

```cpp
void destroyBuffer(AllocatedBuffer& buffer);
```

### Image 创建

```cpp
AllocatedImage createImage(
    const VkImageCreateInfo& imageInfo,
    VmaMemoryUsage memoryUsage,
    VmaAllocationCreateFlags flags = 0);
```

### Image 销毁

```cpp
void destroyImage(AllocatedImage& image);
```

### 上传辅助

后续建议增加：

```cpp
AllocatedBuffer createStagingBuffer(...);
void uploadBuffer(...);
void uploadImage(...);
```

但这些可以放到第二阶段，不需要和 allocator 初始化同时完成。

## 内存使用策略建议

如果你参考 `vk-guide` 的旧章节，会常见到：

- `VMA_MEMORY_USAGE_GPU_ONLY`
- `VMA_MEMORY_USAGE_CPU_ONLY`
- `VMA_MEMORY_USAGE_CPU_TO_GPU`

这些历史写法还兼容，但在 VMA 3.x 里已经不是最推荐路线。

更建议逐步转到：

- `VMA_MEMORY_USAGE_AUTO`
- `VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE`
- `VMA_MEMORY_USAGE_AUTO_PREFER_HOST`

再配合：

- `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT`
- `VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT`
- `VMA_ALLOCATION_CREATE_MAPPED_BIT`

建议策略：

1. 第一阶段 helper 内部先封装“常用预设”，外部不要直接散写一堆 VMA flag。
2. 等你自己的框架稳定后，再决定是否把这些参数开放给上层。

## 和现有 `Texture` / `Buffer` 的关系

### `vks::Buffer`

第一阶段：

- 不改
- 继续服务旧 example

第二阶段可选方向：

1. 让 `vks::Buffer` 变成纯 view，不再拥有内存
2. 或新增 `vks::BufferVma`
3. 或逐渐让新代码只用 `AllocatedBuffer`

我更建议第三种：新代码直接用 `AllocatedBuffer`，不要把旧结构搅复杂。

### `vks::Texture`

当前 [`base/VulkanTexture.h`](../base/VulkanTexture.h) 内部直接持有：

- `VkImage`
- `VkDeviceMemory`
- `VkImageView`
- `VkSampler`

这意味着它和 VMA 的关系也不适合第一阶段硬改。

建议：

1. 第一阶段不动 `vks::Texture`
2. 新的 render target / offscreen image 使用 `AllocatedImage`
3. 如果未来你要做自己的材质和纹理系统，再设计一套新的 `TextureResource`

## 推荐迁移顺序

### Phase 0：接入依赖，不改运行逻辑

改动：

1. 加入 `vk_mem_alloc.h`
2. CMake include path 接入
3. 添加 `VulkanVMA.h/.cpp`

验收：

1. 工程能继续编译
2. 原有 example 行为不变

### Phase 1：把 allocator 接到 `VulkanDevice`

改动：

1. `VulkanDevice` 增加 `VmaAllocator`
2. `VulkanExampleBase::initVulkan()` 初始化 allocator
3. `VulkanDevice` 析构中销毁 allocator

验收：

1. allocator 创建和销毁路径正确
2. 旧 sample 仍正常运行

### Phase 2：新增 VMA-native 资源层

改动：

1. 新增 `AllocatedBuffer`
2. 新增 `AllocatedImage`
3. 新增基础创建/销毁 helper

验收：

1. 可以创建 host visible staging buffer
2. 可以创建 device local vertex/index buffer
3. 可以创建 basic sampled image 或 offscreen image

### Phase 3：只迁移你自己的新 example

推荐目标：

- `examples/games202lab/`

做法：

1. 新 example 的 mesh buffer、uniform buffer、staging buffer 全走 VMA
2. 新 example 的 offscreen attachment 也走 VMA
3. 旧 sample 不动

验收：

1. 新 example 从零到一全走 VMA
2. 现有仓库 example 不受影响

### Phase 4：按需回收技术债

未来如果你觉得有必要，再考虑：

1. 把 `VulkanTexture` 的内部实现迁到 VMA
2. 给 `vks::Buffer` 增加迁移路径
3. 收敛 raw allocation 和 VMA allocation 的双轨代码

这一步不要提前做。

## 我最推荐的第一版落地范围

如果只做一版最小但正确的 VMA 接入，我建议范围控制在下面这些点：

1. 加入 `external/vma/vk_mem_alloc.h`
2. 新增 `base/VulkanVMA.h`
3. 新增 `base/VulkanVMA.cpp`
4. 在 `VulkanDevice` 中持有并初始化 `VmaAllocator`
5. 新增 `AllocatedBuffer`
6. 新增 `AllocatedImage`
7. 新增最小 helper：
   - `createBuffer`
   - `destroyBuffer`
   - `createImage`
   - `destroyImage`
8. 只在未来的 `games202lab` 中使用，不回头改 `triangle`

这是风险最低、收益最高的一版。

## 风险点

### 1. 双系统并存

短期内仓库会同时存在：

- raw `VkDeviceMemory`
- VMA allocation

这是刻意接受的过渡成本，不是坏事。关键是不要让一个结构同时支持两种所有权。

### 2. 销毁顺序

必须确保：

1. resource 先销毁
2. allocator 后销毁
3. device 最后销毁

### 3. 误把教学代码也“工程化”

`triangle` 的价值是把底层步骤展开给你看，不是当最终框架模板。不要为了统一风格，把它的教学属性抹掉。

## 这份方案之后的下一步

当这份方案真正开始实施时，我建议顺序是：

1. 先做 `Phase 0 + Phase 1`
2. 编译确认工程没坏
3. 再做 `AllocatedBuffer / AllocatedImage`
4. 最后新建 `games202lab`

## 附加建议

如果你的目标是尽量贴近 `vk-guide` 的开发体验，那么 VMA 只是第一步。之后还建议继续补这几个基础设施：

1. `DeletionQueue`
2. `ImmediateSubmit` / `UploadContext`
3. `FrameData`
4. 统一的 `DescriptorAllocator`
5. 统一的 `SceneUBO`

也就是说：

- VMA 负责“资源怎么分配”
- deletion / upload / frame system 负责“资源怎么活得舒服”

这两部分搭起来之后，这个仓库才会真正开始像一个你自己的实验框架，而不只是一个 example 集合。
