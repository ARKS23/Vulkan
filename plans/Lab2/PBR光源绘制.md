# PBR 光源绘制：把照明强度和显示亮度分开

## 1. 问题从哪里来

在 PBR 场景里，一个“光源”通常同时有两层含义：

1. 它是参与光照计算的数学光源，例如点光源、方向光、面积光。
2. 它也可能是屏幕上能看见的一个物体，例如一个小球、灯泡、发光面板。

这两个东西看起来像是同一个光源，但它们不应该完全共用同一个强度。

如果直接用 PBR 光照强度去画光源球：

```hlsl
float3 color = lightColor * lightIntensity;
```

就会遇到两个极端：

- `lightIntensity` 很大时，光源球直接变成一片白，看不到颜色。
- `lightIntensity` 被调小后，PBR 物体照明舒服了，但光源球本身又不够亮，Bloom 也不明显。

所以更好的做法是把它拆成两条线：

| 数据 | 用途 | 影响对象 |
|---|---|---|
| `lightIntensity` | PBR 光照强度 | 物体被照亮的程度 |
| `lightVisualIntensity` | 光源球显示亮度 | 光源球自身亮度和 Bloom |

一句话：**PBR 光照强度控制“它照别人有多亮”，显示亮度控制“它自己看起来有多亮”。**

## 2. 当前 Lab2 的数据流

现在 Lab2 的渲染路径大概是：

```text
UBOLights.lightIntensity
    -> pbrScene.frag / PBRTexture.frag
    -> 计算物体表面的 PBR 光照
    -> 写入 HDR Scene Color

lightVisualIntensity
    -> light.frag
    -> 直接输出光源球的 emissive HDR 颜色
    -> 写入 HDR Scene Color

HDR Scene Color
    -> Bloom Downsample
    -> Bloom Upsample
    -> Composite + Tone Mapping + Gamma
    -> Swapchain
```

注意，两条线最后都会写入同一个 HDR 场景颜色图里。区别是：

- PBR 物体亮不亮，由 PBR shader 里的光照公式决定。
- 光源球亮不亮，由 light sphere shader 直接输出的 emissive 颜色决定。

## 3. PBR 光照强度：照亮物体

在 C++ 里，PBR 光照强度保存在 `UBOLights.lightIntensity` 中：

```cpp
UBOLights.lightIntensity[0] = glm::vec4(24.0f, 24.0f, 24.0f, 1.0f);
UBOLights.lightIntensity[1] = glm::vec4(15.0f, 15.0f, 15.0f, 1.0f);
UBOLights.lightIntensity[2] = glm::vec4(33.0f, 33.0f, 33.0f, 1.0f);
UBOLights.lightIntensity[3] = glm::vec4(20.0f, 20.0f, 20.0f, 1.0f);

UBOLights.lightsColor[0] = glm::vec4(0.85f, 0.47f, 0.33f, 1.0f);
UBOLights.lightsColor[1] = glm::vec4(0.23f, 0.66f, 0.36f, 1.0f);
UBOLights.lightsColor[2] = glm::vec4(0.98f, 0.29f, 0.27f, 1.0f);
UBOLights.lightsColor[3] = glm::vec4(0.52f, 0.76f, 0.85f, 1.0f);
```

这些数据会进入 PBR shader。比如 `pbrScene.frag` 和 `PBRTexture.frag` 中都有类似逻辑：

```hlsl
float3 radiance = light.lightsColor[i].xyz *
                  light.lightIntensity[i].xyz *
                  attenuation;
```

这里的 `radiance` 是参与 PBR BRDF 计算的入射光强度。它会影响：

- 球体受光面有多亮。
- 金属反射有多强。
- 粗糙表面的高光有多明显。
- 物体整体是否被光照充分照亮。

这就是“物理光照强度”。

## 4. 光源球显示亮度：让灯自己发光

光源球不是靠 PBR shader 照亮的普通物体，而是一个“可视化代理”。它的作用是告诉观察者：

```text
这里有一盏灯。
这盏灯是什么颜色。
它看起来有多亮。
```

所以我们给它单独准备了 `lightVisualIntensity`：

```cpp
glm::vec4 lightVisualIntensity[4] = {
    glm::vec4(14.0f, 14.0f, 14.0f, 1.0f),
    glm::vec4(12.0f, 12.0f, 12.0f, 1.0f),
    glm::vec4(16.0f, 16.0f, 16.0f, 1.0f),
    glm::vec4(10.0f, 10.0f, 10.0f, 1.0f)
};
```

这些数值只控制光源球自己输出的 HDR 颜色，不影响 PBR 物体的光照。

## 5. C++ 侧：Push Constant 怎么传

光源球使用 push constant 传每盏灯的数据：

```cpp
struct PushconstantsLight {
    glm::vec4 Pos;
    glm::vec4 Color;
    glm::vec4 Intensity;
    glm::vec4 VisualIntensity;
};
```

这里保留了两个强度：

```cpp
glm::vec4 Intensity;        // PBR 光照强度
glm::vec4 VisualIntensity;  // 光源球显示亮度
```

在 `cmdDrawLight()` 里，每画一个光源球，就把对应的数据 push 给 shader：

```cpp
for (int i = 0; i < 4; ++i) {
    pushconstantsLight.Pos = UBOLights.lightsPos[i];
    pushconstantsLight.Color = UBOLights.lightsColor[i];

    // 给 PBR 系统用的物理光强，保留在 push constant 里，便于调试和扩展。
    pushconstantsLight.Intensity = UBOLights.lightIntensity[i];

    // 只给 light sphere 显示用，不影响 PBR 光照。
    pushconstantsLight.VisualIntensity = lightVisualIntensity[i];

    vkCmdPushConstants(
        cmd,
        pipelinesLayout.lightPipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(pushconstantsLight),
        &pushconstantsLight
    );

    lightObject.draw(cmd);
}
```

这一步完成了“同一个灯传两种强度”的数据准备。

## 6. Vertex Shader：只需要位置

`light.vert` 主要负责把光源球移动到点光源位置：

```hlsl
struct Pushconstants {
    float4 Pos;
    float4 Color;
    float4 Intensity;
    float4 VisualIntensity;
};
[[vk::push_constant]] Pushconstants pushconstants;

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(matrices.model, float4(input.pos, 1.0f));
    worldPos = worldPos + pushconstants.Pos;
    output.pos = mul(matrices.projection, mul(matrices.view, worldPos));
    return output;
}
```

虽然 vertex shader 只使用 `Pos`，但 push constant 结构仍然要和 C++ 侧保持一致。

原因是 pipeline layout 里声明的是一整段 push constant range：

```cpp
vks::initializers::pushConstantRange(
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    sizeof(PushconstantsLight),
    0
)
```

为了避免布局混乱，C++、vertex shader、fragment shader 里的 push constant 结构最好保持同样的字段顺序和大小。

## 7. Fragment Shader：只用显示亮度输出 emissive

现在 `light.frag` 的核心非常简单：

```hlsl
struct Pushconstants {
    float4 Pos;
    float4 Color;
    float4 Intensity;
    float4 VisualIntensity;
};
[[vk::push_constant]] Pushconstants pushconstants;

FSOutput main() {
    FSOutput output;
    float3 color = pushconstants.Color.rgb * pushconstants.VisualIntensity.rgb;
    output.color = float4(color, 1.0f);
    return output;
}
```

这里故意没有使用 `pushconstants.Intensity`。

这是关键点：

```text
light.frag 只负责画“灯泡看起来有多亮”
PBR shader 才负责算“灯照别人有多亮”
```

因为你的场景使用 HDR render target，`color` 可以大于 1：

```hlsl
float3 color = float3(0.85, 0.47, 0.33) * 14.0;
```

这会输出一个很亮的 HDR 橙色。后面的 Bloom downsample/upsample 会捕捉到这个高亮区域，于是光源球周围就会出现柔和光晕。

## 8. 为什么不用 PBR 光强直接画光源球

假设直接这样写：

```hlsl
float3 color = pushconstants.Color.rgb * pushconstants.Intensity.rgb;
```

当 PBR 光强比较高时，例如：

```cpp
UBOLights.lightIntensity[0] = glm::vec4(200.0f);
```

光源球输出就会非常大：

```text
color = lightColor * 200
```

经过 tone mapping 后，这个球体很容易变成接近白色。颜色信息被压没了，看起来就是一个白点。

如果你为了保留颜色，把它压缩成：

```hlsl
float visibleIntensity = 1.0 + 5.0 * saturate(rawIntensity / 200.0);
```

又会出现另一个问题：当 PBR 光强只有十几到几十时，显示亮度只有 1 到 2 左右，Bloom 不够明显。

所以最稳定的做法就是拆开：

```cpp
lightIntensity       // 给 PBR
lightVisualIntensity // 给 light sphere
```

## 9. 和 Bloom 的关系

Bloom 的输入不是“光源对象”本身，而是 HDR 场景颜色。

也就是说，Bloom 只关心最终写进 `hdrSceneColor` 的像素值有多亮：

```text
light.frag 输出 HDR 高亮颜色
    -> 写入 hdrSceneColor
    -> Downsample 抓取亮区域
    -> Upsample 扩散光晕
    -> Composite 加回 HDR scene
```

因此，如果你希望光源球有明显 Bloom，需要让 `light.frag` 输出大于 1 的 HDR 值：

```hlsl
float3 color = pushconstants.Color.rgb * 10.0;
```

如果输出接近 1：

```hlsl
float3 color = pushconstants.Color.rgb * 1.5;
```

它可能仍然能被看到，但 Bloom 会很弱。

## 10. 推荐调参范围

可以从下面的范围开始：

| 参数 | 推荐范围 | 作用 |
|---|---:|---|
| `UBOLights.lightIntensity` | `10 ~ 50` | 物体受光强度 |
| `lightVisualIntensity` | `8 ~ 20` | 光源球自身发光强度 |
| `bloomStrength` | `0.1 ~ 0.5` | Bloom 加回最终画面的强度 |
| `bloomFilterRadius` | `0.5 ~ 2.0` | 光晕扩散半径 |
| `exposure` | `0.2 ~ 1.0` | 最终 tone mapping 前曝光 |

调参时建议按这个顺序：

1. 先调 `UBOLights.lightIntensity`，让物体照明舒服。
2. 再调 `lightVisualIntensity`，让光源球本身像灯。
3. 再调 `bloomStrength` 和 `bloomFilterRadius`，让光晕自然。
4. 最后调 `exposure`，控制整体画面明暗。

## 11. 常见现象和原因

### 光源球还是太暗

提高：

```cpp
lightVisualIntensity[i] = glm::vec4(16.0f);
```

不要急着提高 `UBOLights.lightIntensity`，因为那会改变 PBR 物体照明。

### 物体太暗，但光源球够亮

提高：

```cpp
UBOLights.lightIntensity[i] = glm::vec4(40.0f);
```

或者提高环境光、曝光。

### 光源球变成纯白

降低：

```cpp
lightVisualIntensity[i]
```

或者降低最终 `exposure`。纯白通常说明 tone mapping 前的值太高，颜色比例被压扁了。

### Bloom 太大、糊成一片

降低：

```cpp
bloomStrength
bloomFilterRadius
```

光源球亮度和 Bloom 强度是两层东西。光源球可以很亮，但 Bloom 不一定要很强。

## 12. 这套设计的优点

这种拆法有几个好处：

1. PBR 光照更稳定  
   调灯泡外观时，不会破坏物体受光。

2. Bloom 更可控  
   光源球可以稳定输出 HDR emissive 值，让 Bloom 有明确输入。

3. 颜色不容易丢失  
   不直接使用过大的物理光强画球，避免 tone mapping 后变成纯白。

4. 更接近真实渲染系统  
   真实引擎里也常把 light data、light mesh、emissive material 分开管理。

## 13. 可以继续扩展的方向

后面如果想做得更完整，可以继续加：

```cpp
struct LightVisualData {
    glm::vec4 color;
    float emissiveStrength;
    float radius;
};
```

或者把光源球当作真正的 emissive material：

```hlsl
float3 emissive = baseColor * emissiveStrength;
output.color = float4(emissive, 1.0);
```

再进一步，可以让 `lightVisualIntensity` 进入 UI：

```cpp
overlay->sliderFloat("Light Visual", &lightVisualStrength, 1.0f, 30.0f);
```

这样你就能实时调整：

```text
灯照别人有多亮
灯自己看起来有多亮
Bloom 有多强
画面整体曝光是多少
```

这四个量分开以后，PBR + HDR + Bloom 的调试会清楚很多。

## 14. 现代引擎通常怎么处理

现代游戏引擎和离线渲染器通常不会把“照明强度”和“可见发光外观”绑死在一起。它们更常见的设计是把一个灯拆成几个概念：

| 系统 | 负责什么 | 对应 Lab2 |
|---|---|---|
| Light Component / Light Node | 真正照亮场景，参与 BRDF、阴影、GI | `UBOLights.lightIntensity` |
| Emissive Mesh / Emissive Material | 让灯泡、灯管、屏幕自己看起来发亮 | `lightVisualIntensity` + `light.frag` |
| Bloom / Glow / Lens Flare | 后处理里的视觉光晕、眩光 | Bloom downsample / upsample / composite |
| Editor Gizmo / Helper | 只在编辑器里帮助定位灯的位置和范围 | Lab2 里暂时没有 |

也就是说，在真实工程里，一个“灯”经常是一个组合：

```text
Lamp Actor
    PointLightComponent      -> 照亮世界
    StaticMeshComponent      -> 灯泡模型
    EmissiveMaterial         -> 灯泡自己发亮
    Optional LensFlare/Bloom -> 视觉效果
    Editor Icon/Gizmo        -> 编辑器辅助显示
```

这和 Lab2 现在做的事情是同一个思想：

```text
UBOLights.lightIntensity  -> PBR 照明
lightVisualIntensity      -> 光源球可视化
Bloom                     -> 光晕
```

### Unreal Engine 的做法

Unreal 的光源有真实的 Light 类型，例如 Point Light、Spot Light、Rect Light、Directional Light。它们的强度可以使用物理单位，例如 Candela、Lumen、Lux 等。Unreal 文档里明确说明，Point、Spot、Rect Light 可以选择 Candela、Lumen 或 Unitless，Directional Light 使用 Lux，Emissive Surface 则以 `cd/m2` 这类表面亮度来描述。

这说明 Unreal 已经把两件事区分得很清楚：

```text
Light Actor 的 Intensity
    -> 用物理单位控制它照亮场景的能力

Emissive Material 的 Emissive Color
    -> 控制表面自己在 HDR 里有多亮
```

Unreal 的 Emissive Material 文档也提到，给 Emissive Color 输入大于 `1.0` 的值会把材质推入 HDR 范围，从而产生 Bloom。它还特别说明，如果只是用发光材质模拟光源表面，比如灯泡或 light card，可以使用更便宜的 Unlit Shading Model。

所以 Unreal 常见的灯具做法是：

```text
PointLight / RectLight
    控制实际照明、阴影、衰减、GI

Lamp Mesh + Emissive Material
    控制灯泡、灯管、霓虹灯牌自己看起来有多亮

Post Process Bloom
    控制高亮表面周围的光晕
```

如果用 Lab2 的代码类比 Unreal：

```cpp
// 类似 Unreal Light Component 的 Intensity
UBOLights.lightIntensity[i] = glm::vec4(24.0f);

// 类似 Unreal Emissive Material 的 emissive multiplier
lightVisualIntensity[i] = glm::vec4(14.0f);
```

一个重点是：**Unreal 里的灯可以照亮世界，但灯本身不一定是一个可见物体；可见的灯泡通常还是靠 mesh + emissive material。**

### Unity 的做法

Unity 也把 Light 组件和 Emissive Material 分开。

Unity 的 Light 组件有自己的 `Color`、`Intensity`、`Range`、`Shadow Type` 等属性。`Intensity` 控制这盏灯在场景里有多亮，也就是它对其他物体的照明贡献。

而 Unity 的 Emissive Material 是材质层面的东西。官方文档说明，Emission 属性控制材质表面发出的颜色和强度，用来让物体看起来像从内部发光，例如屏幕、刹车盘、发光按钮等。Unity 文档还特别指出，有些 emissive material 即使不贡献场景照明，也仍然可以在屏幕上明亮地发光，用来制作霓虹灯或其他可见光源。

所以 Unity 里常见的灯具 prefab 会是：

```text
GameObject: Lamp
    Light Component
        intensity = 5
        range = 10
        color = warm white

    Mesh Renderer
        material = emissive bulb material
        emission color = warm white * 10

    Optional Bloom
        在 URP/HDRP/Post Processing 里处理
```

映射到 Lab2：

```cpp
// Unity Light.intensity 类比
UBOLights.lightIntensity[i] = glm::vec4(24.0f);

// Unity Material emission intensity 类比
lightVisualIntensity[i] = glm::vec4(14.0f);
```

这样做的好处是非常实际的：美术想让灯泡“更刺眼”，可以调 emission；技术美术想让场景“照得更亮”，可以调 Light intensity。两个人不会互相破坏对方的结果。

### Blender / Cycles / Eevee 的做法

Blender 也把 Light Object 和 Emission Shader 分开。

Light Object 是专门的光源对象，例如 Point、Sun、Spot、Area。它们负责给场景提供照明。Blender 的 Emission Shader 则是材质/着色器层面的发光，用在物体表面或 light surface 上。官方文档里提到，Emission Shader 有 `Color` 和 `Strength`，可以让材质或灯面输出光。

在 Blender 里做一个可见灯泡，经常会组合：

```text
Point Light / Area Light
    负责照亮房间

Small Sphere Mesh + Emission Shader
    负责让灯泡自己可见、发亮

Compositor Bloom / Eevee Bloom / Glare
    负责后处理光晕
```

这和 Lab2 现在的光源球非常像：

```text
PBR 点光源
    -> 照亮球阵列

light sphere emissive shader
    -> 让小球自己看起来是发光体

Bloom
    -> 把 HDR 高亮扩散成光晕
```

### Godot 的做法

Godot 文档也把光源和材质 emission 区分开。它列出的光来源包括：

```text
1. 材质自身的 emission color
2. Light nodes: DirectionalLight3D / OmniLight3D / SpotLight3D
3. Ambient light / Reflection probes
4. Global illumination
```

文档还说明，材质的 emission color 本身不一定会影响附近物体，除非启用了 baked 或 screen-space indirect lighting。Godot 的 Light3D 也有自己的 `Color`、`Energy`、`Indirect Energy`、`Specular` 等参数。

这正是同一套思想：

```text
Light3D.energy
    -> 负责照明

Material emission
    -> 负责物体自己发光

Glow/Bloom
    -> 负责屏幕空间视觉光晕
```

### 现代引擎的共同结论

综合这些引擎，可以总结出一个很稳定的工程原则：

```text
不要让“灯照别人有多亮”和“灯自己看起来有多亮”只共用一个数。
```

更专业的拆法是：

```cpp
struct LightData {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 radiometricIntensity; // 给 PBR/BRDF/阴影/GI
};

struct LightVisualData {
    glm::vec3 emissiveColor;
    float emissiveStrength;         // 给可见 mesh / bloom
    float visualRadius;
};
```

对于 Lab2，目前可以保持轻量：

```cpp
UBOLights.lightIntensity[i] = glm::vec4(24.0f);      // PBR
lightVisualIntensity[i] = glm::vec4(14.0f);          // 可视化
```

等项目规模变大，再把它们抽象成两个结构：

```cpp
struct PhysicalLight {
    glm::vec4 position;
    glm::vec4 color;
    glm::vec4 intensity;
};

struct VisibleLightProxy {
    glm::vec4 color;
    glm::vec4 emissiveStrength;
    float radius;
};
```

这种设计更接近现代引擎：

- Light 数据进入 lighting pass。
- Visible proxy 进入 forward/emissive pass。
- HDR buffer 进入 Bloom。
- Tone mapping 最后统一处理。

### 对 Lab2 的实践建议

现在 Lab2 的方案已经是现代引擎思路的简化版：

```text
PBR 强度：UBOLights.lightIntensity
显示强度：lightVisualIntensity
Bloom 强度：bloomStrength
曝光强度：exposure
```

后面可以继续优化：

1. 给 UI 增加 `Light Visual Strength` slider。  
   这样可以实时调整光源球发光程度。

2. 给光源球增加半径参数。  
   小而亮的光源会像刺眼点光；大而柔和的光源更像灯泡或发光球。

3. 给 light sphere 单独做 unlit/emissive pipeline。  
   当前已经接近这个方向，后面可以更明确地把它当作 emissive pass。

4. 给 Bloom 加 threshold 或 soft knee 可选项。  
   Physically Based Bloom 通常不硬切，但教学阶段加一个 soft threshold 有助于理解。

5. 给 light visual data 单独建结构体。  
   当光源数量增加时，比散落的数组更清晰。

## 15. 参考资料

- Unreal Engine: Physical Lighting Units  
  https://dev.epicgames.com/documentation/unreal-engine/using-physical-lighting-units-in-unreal-engine

- Unreal Engine: Using the Emissive Material Input  
  https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-emissive-material-input-in-unreal-engine

- Unity Manual: Lights  
  https://docs.unity.cn/Manual/class-Light.html

- Unity Manual: Add light emission to a material  
  https://docs.unity3d.com/6000.1/Documentation/Manual/StandardShaderMaterialParameterEmission.html

- Unity Manual: Emissive materials  
  https://docs.unity.cn/2021.1/Documentation/Manual/lighting-emissive-materials.html

- Blender Manual: Emission Shader  
  https://docs.blender.org/manual/en/latest/render/shader_nodes/shader/emission.html

- Godot Docs: 3D lights and shadows  
  https://docs.godotengine.org/en/4.5/tutorials/3d/lights_and_shadows.html

- Godot Docs: Light3D  
  https://docs.godotengine.org/en/latest/classes/class_light3d.html
