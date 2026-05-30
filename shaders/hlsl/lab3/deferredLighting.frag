#include "common/PBR.hlsli"

struct FSInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

// GBuffer 纹理和采样器
Texture2D albedoMetallic : register(t0);
SamplerState albedoMetallicSampler : register(s0);

Texture2D normalRoughness : register(t1);
SamplerState normalRoughnessSampler : register(s1);

Texture2D emissiveAO : register(t2);
SamplerState emissiveAOSampler : register(s2);

Texture2D depthMap : register(t3);
SamplerState depthMapSampler : register(s3);

// 变换矩阵和相机信息
struct CameraUBO {
    float4x4 projection;
    float4x4 view;
    float4x4 inverseProjection;
    float4x4 inverseView;
    float4 cameraPos;
    float4 screenSize; 
};
ConstantBuffer<CameraUBO> cameraInfo : register(b5);

struct Light {
    float4 position;
    float4 color;
    float4 intensity;
};

struct LightsUBO {
    Light lights[4];
    int4 lightCount;
};
ConstantBuffer<LightsUBO> lightsInfo : register(b6);

float3 reconstructWorldPosition(float2 uv, float depth, float4x4 inverseProjection, float4x4 inverseView) {
    float4 clipPos;
    // 从UV的[0, 1] 变换到 NDC的[-1, 1]
    clipPos.x = uv.x * 2.0f - 1.0f;
    clipPos.y = uv.y * 2.0f - 1.0f;
    clipPos.z = depth;
    clipPos.w = 1.0f;

    // 逆变换回世界坐标
    float4 viewPos = mul(inverseProjection, clipPos);
    float4 worldPosition = mul(inverseView, viewPos);

    // 透视除法
    worldPosition.xyz /= worldPosition.z;
    worldPosition.z = 1.0f;

    return worldPosition.xyz;
}

FSOutput main(FSInput input) {
    FSOutput output;
    
    // 从 GBuffer 中采样
    float4 albedoMetallicSample = albedoMetallic.Sample(albedoMetallicSampler, input.uv);
    float4 normalRoughnessSample = normalRoughness.Sample(normalRoughnessSampler, input.uv);
    float4 emissiveAOSample = emissiveAO.Sample(emissiveAOSampler, input.uv);
    float depthSample = depthMap.Sample(depthMapSampler, input.uv).r;

    // 从 GBuffer 数据重建世界坐标和法线
    float3 worldPos = reconstructWorldPosition(input.uv, depthSample, cameraInfo.inverseProjection, cameraInfo.inverseView);
    float3 N = normalize(normalRoughnessSample.xyz);
    float roughness = normalRoughnessSample.w;
    float metallic = albedoMetallicSample.w;
    float ao = emissiveAOSample.w;
    float3 albedo = albedoMetallicSample.rgb;
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // PBR直接光照
    float3 Lo = float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < lightsInfo.lightCount.x; ++i) {
        Light light = lightsInfo.lights[i];
        float3 L = normalize(light.position.xyz - worldPos);
        float3 V = normalize(cameraInfo.cameraPos.xyz - worldPos);
        float3 H = normalize(L + V);

        float distance = length(light.position.xyz - worldPos);
        float attenuation = 1.0f / (distance * distance);

        float3 brdf = direcBRDF(N, V, L, H, roughness, metallic, albedo, F0);
        float3 radiance = light.color.rgb * light.intensity.x * attenuation;
        float NdotL = max(dot(N, L), 0.0001f);
        Lo += radiance * brdf * NdotL;
    }

    // 自发光
    float3 emissive = emissiveAOSample.rgb;
    
    // 环境光
    float3 ambient = albedo * 0.1f; // 简单的环境光

    // 最终颜色
    float3 finalColor = (ambient + Lo + emissive) * ao;

    output.color = float4(finalColor, 1.0f);
    return output;
}

