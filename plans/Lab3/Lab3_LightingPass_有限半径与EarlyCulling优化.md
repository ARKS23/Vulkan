# Lab3 Lighting Pass 优化：有限半径与 Early Culling

这份文档记录我在 Lab3 延迟渲染里优化 lighting pass 的第一步思路：给点光源设置有限影响半径，并在 shader 中尽早跳过影响不到当前像素的光源。

这个优化不需要立刻引入 tiled deferred、clustered shading 或 compute light culling，改动小，但能帮助我理解 deferred lighting 的性能瓶颈在哪里。

## 1. 当前 lighting pass 的问题

当前 Lab3 的 deferred lighting pass 大致是：

```text
对屏幕上每个像素：
    从 G-Buffer 读 albedo / normal / roughness / metallic / depth
    重建 world position
    遍历所有 light
        计算 PBR 光照
```

伪代码：

```hlsl
for (int i = 0; i < lightCount; ++i) {
    Light light = lights[i];

    float3 L = normalize(light.position.xyz - worldPos);
    float distance = length(light.position.xyz - worldPos);
    float attenuation = 1.0f / (distance * distance);

    float3 brdf = directBRDF(...);
    Lo += light.color.rgb * light.intensity.x * attenuation * brdf * NdotL;
}
```

如果屏幕是 1920x1080，并且有 150 个点光源：

```text
1920 * 1080 * 150 ≈ 3.1 亿次 light evaluation / frame
```

Deferred rendering 已经避免了“每个光源重新画一遍场景几何”，但 lighting pass 本身仍然会很重。

核心问题是：

```text
很多光源离当前像素很远，实际上几乎没有贡献，但 shader 仍然为它计算完整 BRDF。
```

## 2. 优化核心思想

一句话：

```text
不要让一个像素计算根本照不到它的光源。
```

点光源在真实物理里是 inverse-square falloff：

```hlsl
attenuation = 1.0 / distance^2
```

理论上它无限远都有贡献，只是越来越小。  
但实时渲染中，我们通常会人为给它一个有限影响半径：

```text
distance > radius 时，认为光源贡献为 0
```

于是 shader 可以在很早的位置跳过：

```hlsl
if (dist2 > radius * radius) {
    continue;
}
```

这就是 early culling。

## 3. 数据结构设计

Lab3 当前的光源结构可以这样理解：

```cpp
struct Light {
    glm::vec4 position;
    glm::vec4 color;
    glm::vec4 intensity;
};
```

我已经把 `intensity` 设计成可以承载多种语义：

```cpp
// x: PBR lighting intensity
// y: visual light proxy intensity
// z: visual light proxy radius
// w: lighting influence radius
glm::vec4 intensity;
```

这样可以把“照亮场景”和“显示光源球”完全分开：

```text
intensity.x
    -> deferred lighting pass 使用
    -> 决定这个光源照亮场景多强

intensity.y
    -> light proxy pass 使用
    -> 决定光源球自己有多亮

intensity.z
    -> light proxy pass 使用
    -> 决定光源球显示半径

intensity.w
    -> deferred lighting pass 使用
    -> 决定光源影响半径
```

C++ 端可以这样写：

```cpp
const float lightingIntensity = renderSettings.lightIntensity * variation;
const float visualIntensity = renderSettings.lightVisualIntensity * variation;
const float visualRadius = renderSettings.lightVisualRadius;
const float lightingRadius = renderSettings.lightRadius;

light.intensity = glm::vec4(
    lightingIntensity,
    visualIntensity,
    visualRadius,
    lightingRadius
);
```

## 4. Shader 端 Early Culling

原本的 lighting shader 可能是：

```hlsl
float3 lightVec = light.position.xyz - worldPos;
float distance = length(lightVec);
float3 L = lightVec / distance;
float attenuation = 1.0f / max(distance * distance, 0.0001f);
```

优化后建议先算平方距离：

```hlsl
float3 lightVec = light.position.xyz - worldPos;
float dist2 = dot(lightVec, lightVec);

float radius = light.intensity.w;
float radius2 = radius * radius;

if (dist2 > radius2) {
    continue;
}
```

注意这里先用 `dist2`，不要一上来 `length()`，因为：

```text
length(x) = sqrt(dot(x, x))
```

开方比乘加更贵。  
如果这个光源已经被 early culling 跳过，就完全没必要开方、normalize、算 BRDF。

通过 early culling 后，再继续算：

```hlsl
float invDist = rsqrt(max(dist2, 0.0001f));
float distance = 1.0f / invDist;
float3 L = lightVec * invDist;
```

## 5. 推荐衰减公式

只做硬截断会有一个问题：

```text
在 radius 边界处，光照突然从一个小值跳到 0
```

这可能产生可见的光照边界。

所以推荐在半径内加一个平滑 falloff：

```hlsl
float attenuation = 1.0f / max(dist2, 0.0001f);

float falloff = saturate(1.0f - dist2 / radius2);
falloff *= falloff;

float3 radiance =
    light.color.rgb *
    light.intensity.x *
    attenuation *
    falloff;
```

完整片段：

```hlsl
float3 lightVec = light.position.xyz - worldPos;
float dist2 = dot(lightVec, lightVec);

float radius = max(light.intensity.w, 0.001f);
float radius2 = radius * radius;

if (dist2 > radius2) {
    continue;
}

float invDist = rsqrt(max(dist2, 0.0001f));
float3 L = lightVec * invDist;
float3 H = normalize(L + V);

float attenuation = 1.0f / max(dist2, 0.0001f);
float falloff = saturate(1.0f - dist2 / radius2);
falloff *= falloff;

float3 brdf = direcBRDF(N, V, L, H, roughness, metallic, albedo, F0);
float3 radiance = light.color.rgb * light.intensity.x * attenuation * falloff;
float NdotL = max(dot(N, L), 0.0f);

Lo += radiance * brdf * NdotL;
```

## 6. 为什么这能优化性能

原始版本：

```text
每个像素都计算全部 150 个光源
```

加入 early culling 后：

```text
每个像素仍然遍历 150 个光源
但大量光源只做：
    lightVec
    dist2
    radius 判断
然后 continue
```

也就是说，它还没有减少循环次数，但减少了单个光源被完整计算的概率。

完整 BRDF 计算包括：

```text
normalize
dot
GGX D
Smith G
Fresnel F
多次乘除
pow
```

这些都比一次 `dist2 > radius2` 贵得多。

所以这一步的优化收益来自：

```text
把“完整 PBR 光照计算”提前变成“便宜的距离测试”。
```

## 7. 如何选择 lighting radius

半径不能太小，否则光照范围明显不足；也不能太大，否则 early culling 没有效果。

可以从经验值开始：

```cpp
float lightRadius = 8.0f;
```

如果光源强度很大，可以让半径随强度变化：

```cpp
float lightRadius = glm::sqrt(lightingIntensity) * 2.0f;
lightRadius = glm::clamp(lightRadius, 4.0f, 20.0f);
```

也可以暴露 UI：

```cpp
overlay->sliderFloat("Light Radius", &renderSettings.lightRadius, 1.0f, 40.0f);
```

我的建议：

```text
学习阶段先用统一半径
稳定后再根据强度自动推导半径
```

## 8. 和光源可视化半径的区别

这里一定要区分两个半径：

```text
visualRadius
    -> 光源球显示多大
    -> 只影响 light proxy mesh

lightingRadius
    -> 光源照亮场景的范围
    -> 只影响 deferred lighting
```

它们不应该绑定在一起。

例如：

```text
一个灯泡模型可以很小
但它照亮的范围可以很大
```

所以结构里建议：

```cpp
intensity.z = visualRadius;
intensity.w = lightingRadius;
```

shader 里：

```hlsl
// lightProxy.vert
float visualRadius = light.intensity.z;

// deferredLighting.frag
float lightingRadius = light.intensity.w;
```

## 9. 对 Bloom 的影响

Bloom 的输入来自 HDR scene。

如果我把 light proxy sphere 画进 `hdr.sceneColor`，那么：

```text
light visual intensity 越高
    -> 光源球越亮
    -> 越容易触发 Bloom
```

而：

```text
PBR lighting intensity
    -> 控制它照亮场景有多强
```

所以 Bloom 主要受可视化发光体影响，而场景表面高光则受 PBR lighting intensity 影响。

这正是想要的分离：

```text
照明强度：管“照别人”
显示强度：管“自己亮不亮”
Bloom：管“亮处扩散”
```

## 10. 这一步和 Tiled Deferred 的关系

有限半径 + early culling 不是 tiled deferred 的替代品，而是前置基础。

后续如果做 tiled deferred，需要先知道每个光源的影响范围：

```text
light position
light radius
```

Tiled deferred 的 compute pass 会根据半径判断：

```text
这个 light 的 sphere 是否和这个 screen tile 相交
```

所以 `lightingRadius` 也是 tiled / clustered 的必要数据。

学习路线可以是：

```text
1. Naive full-screen deferred lighting
2. Light radius + early culling
3. Light volume
4. Tiled deferred shading
5. Clustered shading
```

当前这一步属于第 2 步。

## 11. 推荐改动清单

C++：

```cpp
struct RenderSettings {
    float lightIntensity;
    float lightVisualIntensity;
    float lightVisualRadius;
    float lightRadius;
};
```

更新光源：

```cpp
light.intensity = glm::vec4(
    lightingIntensity,
    visualIntensity,
    visualRadius,
    lightingRadius
);
```

Deferred shader：

```hlsl
float radius = light.intensity.w;
if (dist2 > radius * radius) {
    continue;
}
```

Light proxy shader：

```hlsl
float visualRadius = light.intensity.z;
float visualIntensity = light.intensity.y;
```

UI：

```cpp
overlay->sliderFloat("Light Radius", &renderSettings.lightRadius, 1.0f, 40.0f);
```

## 12. 调试建议

1. 先把光源数量固定到 150。
2. 打开/关闭 early culling，对比帧时间。
3. 调小 lighting radius，看帧时间是否下降。
4. 调大 lighting radius，看画面是否更接近原始版本，但性能下降。
5. 保持 visual radius 不变，只调 lighting radius，确认“显示大小”和“照明范围”已经分离。
6. 保持 lighting radius 不变，只调 visual intensity，确认 Bloom 变化但场景照明范围不变。

## 13. 总结

这一步优化的价值不是让 lighting pass 变成最终形态，而是让我建立正确的 deferred lighting 性能模型：

```text
Deferred 的瓶颈不只是 draw call；
当光源很多时，瓶颈会转移到每个像素遍历多少光源、每盏光做多少计算。
```

有限半径 + early culling 是最简单、最直观的第一步：

```text
给光源一个合理影响范围；
距离太远就尽早 continue；
把昂贵的 BRDF 计算留给真正受影响的像素。
```

