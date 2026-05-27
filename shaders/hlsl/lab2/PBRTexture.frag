struct FSInput {
    [[vk::location(0)]] float4 worldPos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float4 tangent : TANGENT0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

struct PBRMaterialSample {
    float3 albedo;
    float roughness;
    float metallic;
    float ao;
    float3 emissive;
    float alpha;
};

struct UBOMatrix {
    float4x4 projection;
    float4x4 model;
    float4x4 view;
    float3 camPos;
    uint mipNums;
};
ConstantBuffer<UBOMatrix> matrices : register(b0, space0);

struct UBOLight {
    float4 lightsPos[4];
    float4 lightsColor[4];
    float4 lightIntensity[4];
};
ConstantBuffer<UBOLight> light : register(b1, space0);

// IBL纹理资源
TextureCube irradianceMap : register(t2, space0);
SamplerState irradianceMapSampler : register(s2, space0);

TextureCube prefilterMap : register(t3, space0);
SamplerState prefilterMapSampler : register(s3, space0);

Texture2D brdfLUT : register(t4, space0);
SamplerState brdfLUTSampler : register(s4, space0);

// PBR纹理资源
Texture2D baseColorMap : register(t0, space1);
SamplerState baseColorSampler : register(s0, space1);

Texture2D metallicRoughnessMap : register(t1, space1);
SamplerState metallicRoughnessSampler : register(s1, space1);

Texture2D normalMap : register(t2, space1);
SamplerState normalSampler : register(s2, space1);

Texture2D occlusionMap : register(t3, space1);
SamplerState occlusionSampler : register(s3, space1);

Texture2D emissiveMap : register(t4, space1);
SamplerState emissiveSampler : register(s4, space1);

struct Pushconstants {
    [[vk::offset(12)]] float roughness;
    [[vk::offset(16)]] float metallic;
    [[vk::offset(20)]] float r;
    [[vk::offset(24)]] float g;
    [[vk::offset(28)]] float b;
};
[[vk::push_constant]] Pushconstants pushconstants;

static const float PI = 3.14159265359;

// --------------------------------------------- D : Trowbridge-Reitz GGX ---------------------------------------------
float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = NdotH2 * (a2 - 1) + 1;
    denom = max(0.00001f, PI * denom * denom); // 防止除0
    
    return nom / denom;
}

// --------------------------------------------- G : Schlick-GGX ---------------------------------------------
float G_SchlickGGX(float NdotV, float roughness) {
    float k = (roughness + 1) * (roughness + 1) / 8;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    denom = max(0.00001f, denom);

    return nom / denom;
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    // 兼顾视线方向V 和 光照方向L
    float ggxV = G_SchlickGGX(NdotV, roughness);
    float ggxL = G_SchlickGGX(NdotL, roughness);
    return ggxV * ggxL;
}

// --------------------------------------------- F : Schlick's approximation ---------------------------------------------
float3 F_Schlick(float cosTheta, float3 F0) {
    // cosTheta一般传入VdotH
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float3 F_SchlickRoughness(float cosTheta, float3 F0, float roughness) {
    // IBL 中使用的 Fresnel-Schlick 近似，加入 roughness 修正
    // 通常 cosTheta 传入 NdotV，用于计算环境光部分的 kS/kD
    cosTheta = saturate(cosTheta);
    roughness = saturate(roughness);

    //float3 maxF = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
    float3 maxF = max((1.0 - roughness).xxx, F0);
    return F0 + (maxF - F0) * pow(1.0 - cosTheta, 5.0);
}

// --------------------------------------------- BRDF: Cook-Torrance BRDF ---------------------------------------------
float3 direcBRDF(float3 N, float3 V, float3 L, float3 H, float roughness, float metallic, float3 albedo, float3 F0) {
    // 直接光照计算函数
    N = normalize(N);
    V = normalize(V);
    L = normalize(L);
    H = normalize(H);

    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    roughness = clamp(roughness, 0.04, 1.0);
    metallic = saturate(metallic);
    
    // DFG
    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(VdotH, F0);

    // 能量分配：镜面反射 + 漫反射
    float3 ks = F;   // 镜面反射比例
    float3 kd = (1.0 - ks) * (1.0 - metallic); // 漫反射比例,而且金属没有漫反射

    // Cook-Torrance specular BRDF
    float3 nom = D * F * G;
    float denom = max(4.0 * NdotV * NdotL, 0.00001);
    float3 specular = nom / denom;

    // Lambert 漫反射
    float3 diffuse = kd * albedo / PI;

    return specular + diffuse;
}

float3 computeDiffuseIBL(float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0) {
    // 环境光照漫反射部分：Irradiance map
    float NdotV = max(dot(N, V), 0.0);
    float3 F = F_SchlickRoughness(NdotV, F0, roughness);
    float3 kd = (1.0 - F) * (1.0 - metallic);
    float3 irradiance = irradianceMap.Sample(irradianceMapSampler, N).rgb;
    return kd * albedo * irradiance;
}

float3 computeSpecularIBL(float3 N, float3 V, float3 F0, float roughness) {
    // 环境光照高光部分，prefilter + LUT
    float3 color = float3(0.0, 0.0, 0.0);
    float3 R = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0);
    float3 F = F_SchlickRoughness(NdotV, F0, roughness);

    // prefilter env map采样，lod根据roughness计算
    float lod = roughness * float(matrices.mipNums - 1);
    float3 prefilterColor = prefilterMap.SampleLevel(prefilterMapSampler, R, lod).rgb;
    // 查表LUT
    float2 brdf = brdfLUT.Sample(brdfLUTSampler, float2(NdotV, roughness)).rg;
    color = prefilterColor * (F * brdf.x + brdf.y);
    return color;
}

// --------------------------------------------- 纹理采样 ---------------------------------------------
PBRMaterialSample samplePBRMaterial(float2 uv) {
    PBRMaterialSample mat;

    float4 baseColor = baseColorMap.Sample(baseColorSampler, uv);
    float4 metallicRoughness = metallicRoughnessMap.Sample(metallicRoughnessSampler, uv);
    float ao = occlusionMap.Sample(occlusionSampler, uv).r;
    float3 emissive = emissiveMap.Sample(emissiveSampler, uv).rgb;

    // baseColor/emissive 贴图现在以 SRGB 格式创建，采样时 Vulkan 会自动解码到 linear。
    mat.albedo = saturate(baseColor.rgb);
    mat.alpha = baseColor.a;

    mat.roughness = clamp(metallicRoughness.g, 0.04, 1.0);
    mat.metallic = saturate(metallicRoughness.b);
    mat.ao = saturate(ao);

    mat.emissive = saturate(emissive);
    return mat;
}

float3 getWorldNormal(FSInput input) {
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent.xyz);
    T = normalize(T - N * dot(N, T)); // 进行史密斯特正交化

    float3 B = normalize(cross(N, T) * input.tangent.w); // 计算副切线，注意乘以切线的w分量（可能为-1）
    float3 tangentNormal = normalMap.Sample(normalSampler, input.uv).xyz * 2.0 - 1.0; // 从切线空间法线贴图中采样，转换到[-1,1]范围

    // 如果凹凸方向明显反了，进行反转
    // tangentNormal.y *= -1.0;

    return normalize(mul(tangentNormal, float3x3(T, B, N)));
}

// 使用屏幕空间的偏导数函数计算TBN矩阵，适用于非平铺的UV
float3 getWorldNormal2(FSInput input) {
    float3 N = normalize(input.normal);
    float3 tangentNormal = normalMap.Sample(normalSampler, input.uv).xyz * 2.0 - 1.0;

    float3 dp1 = ddx(input.worldPos.xyz);
    float3 dp2 = ddy(input.worldPos.xyz);
    float2 duv1 = ddx(input.uv);
    float2 duv2 = ddy(input.uv);

    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invMax = rsqrt(max(dot(T, T), dot(B, B)));
    float3x3 TBN = float3x3(T * invMax, B * invMax, N);

    return normalize(mul(tangentNormal, TBN));
}

// ----------------------------------------- 后处理 -----------------------------------------
FSOutput main(FSInput input) {
    FSOutput output;

    PBRMaterialSample material = samplePBRMaterial(input.uv);

    float3 N = getWorldNormal2(input);
    float3 V = normalize(matrices.camPos - input.worldPos.xyz);
    float3 albedo = material.albedo;
    float roughness = material.roughness;
    float metallic = material.metallic;
    float ao = material.ao;
    float3 emissive = material.emissive;
    float alpha = material.alpha;
    float3 F0 = lerp(0.04.xxx, albedo, metallic); // 非金属 F0 约为 0.04，金属 F0 使用 albedo

    // 环境光
    float3 diffuseIBL = computeDiffuseIBL(N, V, albedo, metallic, roughness, F0);
    float3 specularIBL = computeSpecularIBL(N, V, F0, roughness);
    float3 ambient = diffuseIBL + specularIBL;
    ambient *= ao; // 使用AO贴图对环境光进行遮蔽

    // 计算直接光照
    float3 Lo = float3(0.0, 0.0, 0.0);
    for (int i = 0; i < 4; ++i) {
        float3 L = normalize(light.lightsPos[i].xyz - input.worldPos.xyz);
        float3 H = normalize(V + L);

        float distance = length(light.lightsPos[i].xyz - input.worldPos.xyz);
        float attenuation = 1.0 / max(distance * distance, 0.01);

        float3 brdf = direcBRDF(N, V, L, H, roughness, metallic, albedo, F0);
        float3 radiance = light.lightsColor[i].xyz * light.lightIntensity[i].xyz * attenuation;
        float NdotL = max(dot(N, L), 0.0);

        Lo += radiance * brdf * NdotL;
    }

    // 自发光
    float emissiveStrength = 100.0;
    emissive *= emissiveStrength;

    float3 color = ambient + Lo + emissive;

    output.color = float4(color, 1.0);
    return output;
}
