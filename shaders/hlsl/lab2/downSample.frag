struct FSInput {
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

struct BloomPushConstants {
    float2 srcResolution;
};
[[vk::push_constant]] BloomPushConstants bloomPushConstants;

Texture2D srcTexture : register(t0);
SamplerState srcTextureSampler : register(s0);

float3 downSample(float2 texelSize, float2 uv) {
    float3 sampleColor = float3(0.0, 0.0, 0.0);
    // === 13像素采样卷积核(笔记中有图) ===
    // a - b - c
    // - j - k -
    // d - e - f
    // - l - m -
    // g - h - i
    // === e是当前像素 ===
    float3 a = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(-2.0, 2.0)).rgb;
    float3 b = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(0.0, 2.0)).rgb;
    float3 c = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(2.0, 2.0)).rgb;

    float3 d = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(-2.0, 0.0)).rgb;
    float3 e = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(0.0, 0.0)).rgb;
    float3 f = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(2.0, 0.0)).rgb;

    float3 g = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(-2.0, -2.0)).rgb;
    float3 h = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(0.0, -2.0)).rgb;
    float3 i = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(2.0, -2.0)).rgb;

    float3 j = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(-1.0, 1.0)).rgb;
    float3 k = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(1.0, 1.0)).rgb;
    float3 l = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(-1.0, -1.0)).rgb;
    float3 m = srcTexture.Sample(srcTextureSampler, uv + texelSize * float2(1.0, -1.0)).rgb;

    // 13像素卷积核权重，权重和为1:
    // 0.5 + 0.25 + 0.125 + 0.125 = 1
    // 0.125*5 + 0.03125*4 + 0.0625*4 = 1

    // e:         0.125
    // a/c/g/i:   0.125
    // b/d/f/h:   0.25
    // j/k/l/m:   0.5
    // ----------------
    // total:     1.0

    sampleColor = e * 0.125;
    sampleColor += (a + c + g + i) * 0.03125;
    sampleColor += (b + d + f + h) * 0.0625;
    sampleColor += (j + k + l + m) * 0.125;

    return sampleColor;
}

FSOutput main(FSInput input) {
    FSOutput output;
    float2 texelSize = 1.0 / bloomPushConstants.srcResolution;
    float3 color = downSample(texelSize, input.UV);
    color = max(color, 0.0001f.xxx); // 防黑盒效应
    output.color = float4(color, 1.0);
    return output;
}