# Image 与 Texture Helper 使用指南

这份文档记录当前项目里 `vk_images` 相关封装的使用方式。目标不是把 Vulkan image 的细节藏到完全看不见，而是把重复的创建、销毁、descriptor 填写和常见 layout transition 收拢起来，让后续 Lab2 PBR、IBL、Bloom、Shadow Map 等实验可以更专注在渲染算法本身。

## 1. 先区分 Image 和 Texture

在 Vulkan 里，`VkImage` 只是 GPU 图片资源本体。它能不能被 shader 采样，还取决于 `VkImageView`、`VkSampler`、当前 `VkImageLayout` 和 descriptor 写入。

当前项目里建议这样理解：

| 类型 | 适合用途 | 是否带 sampler | 是否带 descriptor |
| --- | --- | --- | --- |
| `AllocatedImage` | depth image、offscreen color target、shadow map、临时 render target | 否 | 否 |
| `AllocatedTexture` | 从 CPU 像素上传的 2D 贴图 | 是 | 是 |
| `AllocatedCubeTexture` | 运行时生成的 cubemap，例如 irradiance map、prefiltered env map | 是 | 是 |
| `vks::TextureCubeMap` | 从 KTX 文件直接读取的 cubemap，例如环境贴图输入 | 是 | 是 |

一个很实用的判断方式：

```text
只是作为 render attachment 写入：AllocatedImage
要给 shader 采样的 2D 贴图：AllocatedTexture
要给 shader 采样的运行时 cubemap：AllocatedCubeTexture
从 .ktx 读取现成 cubemap：vks::TextureCubeMap
```

## 2. AllocatedImage

`AllocatedImage` 是最基础的 VMA image 包装，内部持有：

```cpp
VkImage image;
VkImageView imageView;
VmaAllocation allocation;
VkExtent3D extent;
VkFormat format;
VkImageLayout layout;
```

创建：

```cpp
AllocatedImage hdrColor = vkutil::createAllocatedImage(
    device,
    allocator,
    { width, height, 1 },
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    VK_IMAGE_ASPECT_COLOR_BIT
);
```

销毁：

```cpp
vkutil::destroyAllocatedImage(device, allocator, hdrColor);
```

注意：`createAllocatedImage` 会创建 2D image view，但不会创建 sampler，也不会自动 transition layout。你需要在真正写入或采样前自己调用 `cmdTransitionImageLayout`。

## 3. AllocatedTexture

`AllocatedTexture` 适合“从 CPU 像素创建一张普通 2D 采样贴图”。它包含一个 `AllocatedImage`，并额外保存 `sampler` 和 `descriptor`。

创建：

```cpp
AllocatedTexture texture = vkutil::createTexture2DFromPixels(
    device,
    allocator,
    immediateSubmitContext,
    pixels,
    width,
    height,
    VK_FORMAT_R8G8B8A8_SRGB
);
```

这个函数内部会完成：

```text
创建 staging buffer
复制 CPU 像素到 staging buffer
创建 device-local image
UNDEFINED -> TRANSFER_DST_OPTIMAL
copy buffer -> image
TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
创建 sampler
填好 VkDescriptorImageInfo
```

descriptor 写入时可以直接使用：

```cpp
VkWriteDescriptorSet write =
    vkutil::writeCombinedImageSampler(descriptorSet, binding, &texture.descriptor);
```

销毁：

```cpp
vkutil::destroyTexture(device, allocator, texture);
```

注意：当前版本假设输入是 `RGBA8` 像素，每像素 4 字节。之后如果要支持 HDR、BC 压缩、mipmap 或 KTX，不建议在这个函数里硬扩，应该单独做 loader。

## 4. AllocatedCubeTexture

`AllocatedCubeTexture` 是给运行时生成 cubemap 准备的，例如：

```text
environment cubemap -> irradiance cubemap
environment cubemap -> prefiltered cubemap
```

它使用 VMA 管理内存，内部创建：

```text
VkImage: 2D image, arrayLayers = 6, CUBE_COMPATIBLE
VkImageView: VK_IMAGE_VIEW_TYPE_CUBE
VkSampler: clamp to edge, linear filter
VkDescriptorImageInfo
```

创建 irradiance cubemap：

```cpp
const uint32_t dim = 64;
const uint32_t numMips = static_cast<uint32_t>(std::floor(std::log2(dim))) + 1;

textures.irradianceCubeMap = vkutil::createAllocatedCubeTexture(
    device,
    allocator,
    dim,
    numMips,
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_IMAGE_USAGE_TRANSFER_DST_BIT
);
```

创建 prefiltered cubemap：

```cpp
const uint32_t dim = 512;
const uint32_t numMips = static_cast<uint32_t>(std::floor(std::log2(dim))) + 1;

textures.prefilteredCubeMap = vkutil::createAllocatedCubeTexture(
    device,
    allocator,
    dim,
    numMips,
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_IMAGE_USAGE_TRANSFER_DST_BIT
);
```

销毁：

```cpp
vkutil::destroyAllocatedCubeTexture(device, allocator, textures.irradianceCubeMap);
vkutil::destroyAllocatedCubeTexture(device, allocator, textures.prefilteredCubeMap);
```

注意：不要把 VMA 创建出来的 image 塞进 `vks::TextureCubeMap` 再调用它的 `destroy()`。原项目的 `vks::TextureCubeMap` 使用 `vkFreeMemory`，而 VMA 资源必须使用 `vmaDestroyImage`。

## 5. Cubemap Layout Transition

普通 2D image 可以用旧接口：

```cpp
vkutil::cmdTransitionImageLayout(
    cmd,
    image,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    VK_IMAGE_ASPECT_COLOR_BIT
);
```

但 cubemap 必须覆盖 6 个 layer，并且通常还要覆盖多个 mip，所以要用 range overload：

```cpp
VkImageSubresourceRange range = vkutil::cubeSubresourceRange(numMips);

vkutil::cmdTransitionImageLayout(
    cmd,
    textures.irradianceCubeMap.image,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    range
);
```

生成完成后：

```cpp
vkutil::cmdTransitionImageLayout(
    cmd,
    textures.irradianceCubeMap.image,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    range
);

textures.irradianceCubeMap.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
textures.irradianceCubeMap.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
```

核心原则：

```text
不是每帧采样前 transition。
而是在资源生产完后 transition 到 SHADER_READ_ONLY_OPTIMAL，然后保持这个状态给 shader 采样。
```

## 6. IBL 推荐数据流

Lab2 做 IBL 时，建议采用混合方案：

```text
KTX 环境贴图输入:
vks::TextureCubeMap environmentCubeMap

运行时生成资源:
AllocatedCubeTexture irradianceCubeMap
AllocatedCubeTexture prefilteredCubeMap
AllocatedTexture 或 AllocatedImage brdfLUT
```

推荐顺序：

```text
1. load environmentCubeMap from KTX
2. create irradianceCubeMap
3. render 6 faces/mips into offscreen 2D image
4. copy offscreen image into irradianceCubeMap face/mip
5. transition irradianceCubeMap to SHADER_READ_ONLY_OPTIMAL
6. write irradianceCubeMap.descriptor into PBR descriptor set
7. shader 使用 normal 方向采样 irradiance cubemap
```

fragment shader 侧大概是：

```hlsl
TextureCube textureIrradiance : register(t2, space0);
SamplerState samplerIrradiance : register(s2, space0);

float3 irradiance = textureIrradiance.Sample(samplerIrradiance, N).rgb;
float3 diffuseIBL = irradiance * albedo;
```

## 7. 常见错误清单

- `descriptor.imageLayout` 写的是 `SHADER_READ_ONLY_OPTIMAL`，但 image 实际还在 `TRANSFER_DST_OPTIMAL`。
- cubemap transition 只 transition 了 face0/mip0，没有覆盖 `layerCount = 6`。
- VMA image 使用了原项目 `vks::Texture::destroy()`，导致释放方式不匹配。
- 生成 cubemap 时忘记给 image usage 加 `VK_IMAGE_USAGE_TRANSFER_DST_BIT`。
- 作为 render target 的 offscreen image 忘记加 `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`，导致不能 copy 到 cubemap。
- IBL 贴图格式用了 UNORM，导致 HDR 环境光被截断。推荐 `VK_FORMAT_R16G16B16A16_SFLOAT` 起步。
- 采样 HDR cubemap 后直接输出到 UNORM swapchain，没有 tone mapping 和 gamma，看起来会过曝或颜色不对。

## 8. 当前建议

短期继续保持这条路线：

```text
环境输入：继续使用 vks::TextureCubeMap::loadFromFile
运行时生成 IBL 资源：使用 AllocatedCubeTexture
普通 2D 调试纹理：使用 AllocatedTexture
offscreen/HDR/Depth/Shadow：使用 AllocatedImage
```

这能最大化复用原工程的 KTX 加载能力，同时让你自己的渲染实验逐步建立一套清晰、可控、可迁移的资源管理层。
