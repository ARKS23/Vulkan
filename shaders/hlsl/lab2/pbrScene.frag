struct FSInput {
    [[vk::location(0)]] float4 worldPos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

struct UBOMatrix {
    float4x4 projection;
    float4x4 model;
    float4x4 view;
    float3 camPos;
};
ConstantBuffer<UBOMatrix> matrices : register(b0, space0);

struct UBOLight {
    float4 lightsPos[4];
    float4 lightsColor[4];
    float4 lightIntensity[4];
};
ConstantBuffer<UBOLight> light : register(b1, space0);

TextureCube irradianceMap : register(t2);
SamplerState irradianceMapSampler : register(s2);

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
    // 环境光照部分，使用漫反射IBL
    float NdotV = max(dot(N, V), 0.0);
    float3 F = F_SchlickRoughness(NdotV, F0, roughness);
    float3 kd = (1.0 - F) * (1.0 - metallic);
    float3 irradiance = irradianceMap.Sample(irradianceMapSampler, N).rgb;
    return kd * albedo * irradiance;
}

FSOutput main(FSInput input) {
    FSOutput output;

    float3 N = normalize(input.normal);
    float3 V = normalize(matrices.camPos - input.worldPos.xyz);
    float3 albedo = float3(pushconstants.r, pushconstants.g, pushconstants.b);
    float roughness = clamp(pushconstants.roughness, 0.04, 1.0);
    float metallic = saturate(pushconstants.metallic);
    float3 F0 = lerp(0.04.xxx, albedo, metallic); // 非金属 F0 约为 0.04，金属 F0 使用 albedo

    // 环境光
    float3 diffuseIBL = computeDiffuseIBL(N, V, albedo, metallic, roughness, F0);
    float3 specularIBL = float3(0.0, 0.0, 0.0); // 后续补充
    float3 ambient = diffuseIBL + specularIBL;

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

    float3 color = ambient + Lo;

    color = color / (color + 1.0);
    color = pow(saturate(color), 1.0 / 2.2);

    output.color = float4(color, 1.0);
    return output;
}
