struct FSInput {
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

struct BloomPushConstants {
    float2 srcResolution;
    float filterRadius;
    float padding; // 对齐到 16 字节
};
[[vk::push_constant]] BloomPushConstants bloomPushConstants;

Texture2D srcTexture : register(t0);
SamplerState srcTextureSampler : register(s0);

float3 upSample(float2 texelSize, float2 filterRadius, float2 uv) {
    float2 offset = filterRadius * texelSize;
    // 3x3卷积核
    // a - b - c
    // d - e - f
    // g - h - i
    // e是当前像素
    float3 a = srcTexture.Sample(srcTextureSampler, uv + offset * float2(-1.0, 1.0)).rgb;
    float3 b = srcTexture.Sample(srcTextureSampler, uv + offset * float2(0.0, 1.0)).rgb;
    float3 c = srcTexture.Sample(srcTextureSampler, uv + offset * float2(1.0, 1.0)).rgb;

    float3 d = srcTexture.Sample(srcTextureSampler, uv + offset * float2(-1.0, 0.0)).rgb;
    float3 e = srcTexture.Sample(srcTextureSampler, uv + offset * float2(0.0, 0.0)).rgb;
    float3 f = srcTexture.Sample(srcTextureSampler, uv + offset * float2(1.0, 0.0)).rgb;

    float3 g = srcTexture.Sample(srcTextureSampler, uv + offset * float2(-1.0, -1.0)).rgb;
    float3 h = srcTexture.Sample(srcTextureSampler, uv + offset * float2(0.0, -1.0)).rgb;
    float3 i = srcTexture.Sample(srcTextureSampler, uv + offset * float2(1.0, -1.0)).rgb;

    // 3x3卷积核权重，权重和为1:
    //  1   | 1 2 1 |
    // -- * | 2 4 2 |
    // 16   | 1 2 1 |
    float3 sampleColor = e * 4.0;
    sampleColor += (a + c + g + i);
    sampleColor += (b + d + f + h) * 2.0;
    sampleColor /= 16.0;

    return sampleColor;
}

FSOutput main(FSInput input) {
    FSOutput output;
    float2 texelSize = 1.0 / bloomPushConstants.srcResolution;
    float3 color = upSample(texelSize, bloomPushConstants.filterRadius, input.UV);
    output.color = float4(color, 1.0);
    return output;
}