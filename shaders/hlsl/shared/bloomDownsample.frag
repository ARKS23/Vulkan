#include "common/Bloom.hlsli"

struct FSInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

struct BloomDownsamplePushConstants {
    float2 srcResolution;
    uint useKarisAverage;
    float padding;
};
[[vk::push_constant]] BloomDownsamplePushConstants bloomPushConstants;

Texture2D srcTexture : register(t0);
SamplerState srcTextureSampler : register(s0);

float3 downsample(float2 texelSize, float2 uv) {
    float3 a = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(-2.0f,  2.0f)).rgb;
    float3 b = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2( 0.0f,  2.0f)).rgb;
    float3 c = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2( 2.0f,  2.0f)).rgb;

    float3 d = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(-2.0f,  0.0f)).rgb;
    float3 e = srcTexture.Sample(srcTextureSampler, uv).rgb;
    float3 f = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2( 2.0f,  0.0f)).rgb;

    float3 g = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(-2.0f, -2.0f)).rgb;
    float3 h = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2( 0.0f, -2.0f)).rgb;
    float3 i = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2( 2.0f, -2.0f)).rgb;

    float3 j = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(-1.0f,  1.0f)).rgb;
    float3 k = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2( 1.0f,  1.0f)).rgb;
    float3 l = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(-1.0f, -1.0f)).rgb;
    float3 m = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2( 1.0f, -1.0f)).rgb;

    if (bloomPushConstants.useKarisAverage != 0) {
        return BloomDownsample13TapKaris(a, b, c, d, e, f, g, h, i, j, k, l, m);
    }
    return BloomDownsample13Tap(a, b, c, d, e, f, g, h, i, j, k, l, m);
}

FSOutput main(FSInput input) {
    FSOutput output;
    float2 texelSize = 1.0f / bloomPushConstants.srcResolution;
    float3 color = downsample(texelSize, input.uv);
    output.color = float4(BloomAvoidBlackBox(color), 1.0f);
    return output;
}

