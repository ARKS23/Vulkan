# Lab2 PBR Scene Fragment Shader 结构设计

这份文档指导后续 `pbrScene.frag` 如何组织，方便你继续接入：

```text
irradiance map
prefiltered environment map
BRDF LUT
tone mapping / gamma
```

核心目标是把 **直接光** 和 **环境光 IBL** 分清楚。不要把 irradiance map、prefiltered map 放进点光源循环里，否则环境光会被 `NdotL` 和点光源 radiance 错误调制。

## 1. 最终推荐结构

推荐把 fragment shader 分成这几层：

```text
输入与材质参数
    |
    v
计算公共向量 N / V / R / F0
    |
    v
Direct Lighting
    点光源 / 方向光
    使用 Cook-Torrance BRDF
    |
    v
Diffuse IBL
    irradianceMap.Sample(N)
    |
    v
Specular IBL
    prefilteredMap.SampleLevel(R, roughness mip)
    BRDF LUT.Sample(NdotV, roughness)
    |
    v
color = direct + ambient
    |
    v
tone mapping + gamma
```

一句话记忆：

```text
direct lighting 在光源循环里算。
IBL ambient 在光源循环外算。
```

## 2. 资源绑定建议

建议继续沿用原工程的 binding 设计：

```hlsl
TextureCube irradianceMap : register(t2, space0);
SamplerState irradianceSampler : register(s2, space0);

Texture2D brdfLUT : register(t3, space0);
SamplerState brdfSampler : register(s3, space0);

TextureCube prefilteredMap : register(t4, space0);
SamplerState prefilteredSampler : register(s4, space0);
```

C++ descriptor set layout 对应：

```cpp
binding 0: matrices UBO
binding 1: light/material params UBO
binding 2: irradiance cubemap
binding 3: BRDF LUT
binding 4: prefiltered environment cubemap
```

你现在已经有 binding 2 的 irradiance map，后续只需要扩展 binding 3 和 binding 4。

## 3. 公共参数

fragment shader 开头建议统一得到这些量：

```hlsl
float3 N = normalize(input.normal);
float3 V = normalize(matrices.camPos - input.worldPos.xyz);
float3 R = reflect(-V, N);

float3 albedo = float3(pushconstants.r, pushconstants.g, pushconstants.b);
float roughness = clamp(pushconstants.roughness, 0.04, 1.0);
float metallic = saturate(pushconstants.metallic);

float NdotV = max(dot(N, V), 0.0);

float3 F0 = float3(0.04, 0.04, 0.04);
F0 = lerp(F0, albedo, metallic);
```

`F0` 是 PBR 里非常核心的量：

```text
非金属：F0 约为 0.04
金属：F0 接近 baseColor / albedo
```

## 4. Direct Lighting 函数

直接光函数只处理点光源、方向光这类显式光源。

```hlsl
float3 DirectBRDF(
    float3 N,
    float3 V,
    float3 L,
    float3 albedo,
    float metallic,
    float roughness,
    float3 F0)
{
    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(VdotH, F0);

    float3 numerator = D * G * F;
    float denominator = max(4.0 * NdotV * NdotL, 0.0001);
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float3 diffuse = kD * albedo / PI;

    return diffuse + specular;
}
```

主循环中使用：

```hlsl
float3 Lo = 0.0;

for (int i = 0; i < 4; ++i) {
    float3 lightPos = light.lightsPos[i].xyz;
    float3 L = normalize(lightPos - input.worldPos.xyz);
    float distance = length(lightPos - input.worldPos.xyz);
    float attenuation = 1.0 / max(distance * distance, 0.01);

    float3 radiance =
        light.lightsColor[i].xyz *
        light.lightIntensity[i].xyz *
        attenuation;

    float NdotL = max(dot(N, L), 0.0);
    float3 brdf = DirectBRDF(N, V, L, albedo, metallic, roughness, F0);

    Lo += radiance * brdf * NdotL;
}
```

注意：这里的 diffuse 是 `albedo / PI`，不是 `albedo * irradiance`。

## 5. Diffuse IBL

Diffuse IBL 来自 irradiance map，应该在光源循环外，只加一次。

```hlsl
float3 ComputeDiffuseIBL(
    float3 N,
    float3 V,
    float3 albedo,
    float metallic,
    float roughness,
    float3 F0)
{
    float NdotV = max(dot(N, V), 0.0);
    float3 F = F_SchlickRoughness(NdotV, F0, roughness);

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float3 irradiance = irradianceMap.Sample(irradianceSampler, N).rgb;
    return kD * albedo * irradiance;
}
```

这里没有 `/ PI`，因为 irradiance map 的卷积生成阶段已经把 Lambert 半球积分折进去了。你当前 `irradianceMap.frag` 里也有类似：

```hlsl
color = PI * color / sampleCount;
```

所以 scene shader 里直接：

```hlsl
diffuseIBL = irradiance * albedo * kD;
```

## 6. Specular IBL

Specular IBL 需要两个资源：

```text
prefiltered environment map
BRDF LUT
```

公式结构：

```hlsl
float3 ComputeSpecularIBL(
    float3 N,
    float3 V,
    float3 R,
    float roughness,
    float3 F0)
{
    float NdotV = max(dot(N, V), 0.0);
    float3 F = F_SchlickRoughness(NdotV, F0, roughness);

    const float MAX_REFLECTION_LOD = 9.0;
    float lod = roughness * MAX_REFLECTION_LOD;

    float3 prefilteredColor =
        prefilteredMap.SampleLevel(prefilteredSampler, R, lod).rgb;

    float2 brdf =
        brdfLUT.Sample(brdfSampler, float2(NdotV, roughness)).rg;

    return prefilteredColor * (F * brdf.x + brdf.y);
}
```

这里的直觉是：

```text
prefiltered map 决定“从反射方向看到什么环境”
BRDF LUT 决定“这个 roughness / view angle 下反射多少”
Fresnel 决定“掠射角反射更强”
```

## 7. Fresnel Roughness 版本

IBL ambient 里建议使用 roughness 修正版本：

```hlsl
float3 F_SchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max((1.0 - roughness).xxx, F0) - F0) *
        pow(1.0 - cosTheta, 5.0);
}
```

它和普通 `F_Schlick` 的区别是：粗糙表面的 grazing angle 反射不会不合理地过强。

## 8. Main 函数推荐蓝图

最终 `main` 建议长这样：

```hlsl
FSOutput main(FSInput input)
{
    FSOutput output;

    float3 N = normalize(input.normal);
    float3 V = normalize(matrices.camPos - input.worldPos.xyz);
    float3 R = reflect(-V, N);

    float3 albedo = float3(pushconstants.r, pushconstants.g, pushconstants.b);
    float roughness = clamp(pushconstants.roughness, 0.04, 1.0);
    float metallic = saturate(pushconstants.metallic);

    float3 F0 = lerp(0.04.xxx, albedo, metallic);

    float3 Lo = 0.0;

    for (int i = 0; i < 4; ++i) {
        float3 lightPos = light.lightsPos[i].xyz;
        float3 L = normalize(lightPos - input.worldPos.xyz);
        float distance = length(lightPos - input.worldPos.xyz);
        float attenuation = 1.0 / max(distance * distance, 0.01);
        float3 radiance = light.lightsColor[i].xyz * light.lightIntensity[i].xyz * attenuation;

        float NdotL = max(dot(N, L), 0.0);
        float3 brdf = DirectBRDF(N, V, L, albedo, metallic, roughness, F0);
        Lo += radiance * brdf * NdotL;
    }

    float3 diffuseIBL = ComputeDiffuseIBL(N, V, albedo, metallic, roughness, F0);
    float3 specularIBL = ComputeSpecularIBL(N, V, R, roughness, F0);

    float3 ambient = diffuseIBL + specularIBL;
    float3 color = Lo + ambient;

    color = ToneMap(color);
    color = GammaCorrect(color);

    output.color = float4(color, 1.0);
    return output;
}
```

现在你的 shader 最需要改的点是：把 `irradianceMap.Sample(...)` 从 `BRDF()` 中移出来，放到 `main()` 的光源循环外。

## 9. Tone Mapping 和 Gamma

短期可以先保留简单 gamma：

```hlsl
color = pow(color, 1.0 / 2.2);
```

但接入 IBL、高亮光源、后续 bloom 后，建议改成：

```hlsl
float3 ReinhardToneMap(float3 color)
{
    return color / (color + 1.0);
}

float3 GammaCorrect(float3 color)
{
    return pow(saturate(color), 1.0 / 2.2);
}
```

或者使用原工程的 Uncharted2 tone mapping：

```hlsl
color = Uncharted2Tonemap(color * exposure);
color = color * (1.0 / Uncharted2Tonemap(11.2.xxx));
color = pow(color, 1.0 / gamma);
```

后续如果你做 HDR offscreen 和 bloom，tone mapping 一定要从 scene pass 移到 final fullscreen pass。

## 10. Debug Mode 建议

接入 IBL 时非常容易“看起来有点对，但其实公式错了”。建议加这些 debug 输出：

```hlsl
0: final color
1: direct lighting only
2: irradiance map sample
3: diffuse IBL only
4: prefiltered map sample
5: BRDF LUT
6: specular IBL only
7: F0 / metallic / roughness visualization
```

每接一个资源，就先单独输出它：

```hlsl
output.color = float4(irradianceMap.Sample(irradianceSampler, N).rgb, 1.0);
```

确认资源正确后，再接公式。

## 11. 分阶段接入路线

建议按这个顺序推进：

```text
阶段 1：修正当前 diffuse IBL 结构
Direct lighting 使用 albedo / PI
Diffuse IBL 放在循环外

阶段 2：生成并 debug BRDF LUT
先全屏显示 BRDF LUT，确认不是黑图

阶段 3：生成 prefiltered environment map
先 debug SampleLevel(prefilteredMap, R, lod)

阶段 4：接入 specular IBL
验证 roughness 越高，反射越模糊

阶段 5：整理 tone mapping
准备后续 HDR scene target 和 bloom
```

## 12. 常见错误

- 把 irradiance diffuse 放进 direct lighting BRDF，再乘 `radiance * NdotL`。
- 直接光 diffuse 忘记除以 `PI`。
- IBL diffuse 又除以 `PI`，导致整体偏暗。
- 金属材质没有关闭 diffuse，也就是忘记 `kD *= 1.0 - metallic`。
- specular IBL 使用 `texture.Sample` 而不是 `SampleLevel`，导致 roughness 不控制 mip。
- `MAX_REFLECTION_LOD` 和 prefiltered map mip 数不一致。
- BRDF LUT 的采样坐标写反，应该是 `float2(NdotV, roughness)`。
- 在已经是 sRGB swapchain 时又手动 gamma，造成二次 gamma。

## 13. 最关键的心智模型

PBR + IBL 可以记成：

```text
Lo = direct diffuse + direct specular
Ambient = diffuse IBL + specular IBL
Color = Lo + Ambient
```

其中：

```text
direct diffuse: kD * albedo / PI
direct specular: Cook-Torrance DGF / denominator
diffuse IBL: kD * albedo * irradianceMap(N)
specular IBL: prefilteredMap(R, roughness) * BRDF_LUT(NdotV, roughness)
```

这套结构跑通后，你的 Lab2 就从 “PBR basic” 进入真正的 “PBR IBL” 了。
