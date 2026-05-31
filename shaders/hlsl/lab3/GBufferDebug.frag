#include "common/Color.hlsli"

struct FSInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

struct GBufferDebugPushConstants {
    int debugView;
    int enableBloom;
    float nearPlane;
    float farPlane;
    float exposure;
    float bloomStrength;
    float2 padding;
};
[[vk::push_constant]] GBufferDebugPushConstants pushConstants;

Texture2D albedoMetallic : register(t0);
SamplerState albedoMetallicSampler : register(s0);

Texture2D normalRoughness : register(t1);
SamplerState normalRoughnessSampler : register(s1);

Texture2D emissiveAO : register(t2);
SamplerState emissiveAOSampler : register(s2);

Texture2D depthMap : register(t3);
SamplerState depthMapSampler : register(s3);

Texture2D sceneColor : register(t4);
SamplerState sceneColorSampler : register(s4);

Texture2D bloomTexture : register(t5);
SamplerState bloomSampler : register(s5);

float3 debugColor(FSInput input) {
    float3 color = float3(0.xxx);
    // final composite
    if (pushConstants.debugView == 0) {
        float3 hdrColor = sceneColor.Sample(sceneColorSampler, input.uv).rgb;
        float3 bloomColor = bloomTexture.Sample(bloomSampler, input.uv).rgb;
        if (pushConstants.enableBloom == 0) {
            bloomColor = float3(0.0, 0.0, 0.0);
        }

        // Bloom 仍然在线性 HDR 空间合成；只有最终输出到 swapchain 前才做显示变换。
        color = hdrColor + bloomColor * pushConstants.bloomStrength;
        color *= pushConstants.exposure;
        color = ACESFilm(color);
        color = LinearToSRGB(color);
    }
    // Albedo
    else if (pushConstants.debugView == 1) {
        color = albedoMetallic.Sample(albedoMetallicSampler, input.uv).rgb;
    }
    // Normal
    else if (pushConstants.debugView == 2) {
        color = normalRoughness.Sample(normalRoughnessSampler, input.uv).rgb;
    }
    // Roughness
    else if (pushConstants.debugView == 3) {
        float roughness = normalRoughness.Sample(normalRoughnessSampler, input.uv).a;
        color = float3(roughness, roughness, roughness);
    }
    // Metallic
    else if (pushConstants.debugView == 4) {
        float metallic = albedoMetallic.Sample(albedoMetallicSampler, input.uv).a;
        color = float3(metallic, metallic, metallic);
    }
    // Emissive
    else if (pushConstants.debugView == 5) {
        float3 emissive = emissiveAO.Sample(emissiveAOSampler, input.uv).rgb;
        color = emissive;
    }
    else if (pushConstants.debugView == 6) {
        float ao = emissiveAO.Sample(emissiveAOSampler, input.uv).a;
        color = float3(ao, ao, ao);
    }
    // Depth
    else if (pushConstants.debugView == 7) {
        float depth = depthMap.Sample(depthMapSampler, input.uv).r;
        // 线性化深度
        float linearDepth = pushConstants.nearPlane * pushConstants.farPlane / (pushConstants.farPlane - depth * (pushConstants.farPlane - pushConstants.nearPlane));
        // 归一化到 [0, 1] 范围
        float normalizedDepth = (linearDepth - pushConstants.nearPlane) / (pushConstants.farPlane - pushConstants.nearPlane);
        color = float3(normalizedDepth, normalizedDepth, normalizedDepth);
    }
    else {
        color = float3(0.15, 0.34, 0.55);
    }

    return color;
}

FSOutput main(FSInput input) {
    FSOutput output;
    float3 color = debugColor(input);
    output.color = float4(color, 1.0);
    return output;
}
