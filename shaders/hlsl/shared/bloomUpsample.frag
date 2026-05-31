#include "common/Bloom.hlsli"

struct FSInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

struct BloomUpsamplePushConstants {
    float2 srcResolution;
    float filterRadius;
    float padding;
};
[[vk::push_constant]] BloomUpsamplePushConstants bloomPushConstants;

Texture2D srcTexture : register(t0);
SamplerState srcTextureSampler : register(s0);

float3 upsample(float2 texelSize, float2 uv) {
    float2 offset = bloomPushConstants.filterRadius * texelSize;

    float3 a = srcTexture.Sample(srcTextureSampler, uv + offset * float2(-1.0f,  1.0f)).rgb;
    float3 b = srcTexture.Sample(srcTextureSampler, uv + offset * float2( 0.0f,  1.0f)).rgb;
    float3 c = srcTexture.Sample(srcTextureSampler, uv + offset * float2( 1.0f,  1.0f)).rgb;

    float3 d = srcTexture.Sample(srcTextureSampler, uv + offset * float2(-1.0f,  0.0f)).rgb;
    float3 e = srcTexture.Sample(srcTextureSampler, uv).rgb;
    float3 f = srcTexture.Sample(srcTextureSampler, uv + offset * float2( 1.0f,  0.0f)).rgb;

    float3 g = srcTexture.Sample(srcTextureSampler, uv + offset * float2(-1.0f, -1.0f)).rgb;
    float3 h = srcTexture.Sample(srcTextureSampler, uv + offset * float2( 0.0f, -1.0f)).rgb;
    float3 i = srcTexture.Sample(srcTextureSampler, uv + offset * float2( 1.0f, -1.0f)).rgb;

    return BloomUpsampleTent9Tap(a, b, c, d, e, f, g, h, i);
}

FSOutput main(FSInput input) {
    FSOutput output;
    float2 texelSize = 1.0f / bloomPushConstants.srcResolution;
    output.color = float4(upsample(texelSize, input.uv), 1.0f);
    return output;
}

