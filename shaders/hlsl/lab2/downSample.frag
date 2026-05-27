struct FSInput {
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

struct BloomPushConstants {
    float2 srcResolution;
    uint useKairsWeight;
    float padding;
};
[[vk::push_constant]] BloomPushConstants bloomPushConstants;

Texture2D srcTexture : register(t0);
SamplerState srcTextureSampler : register(s0);

// ------------------------------------------- 13下采样 -------------------------------------------
float3 downSampleNormal(
    // 13像素卷积核权重，权重和为1:
    // 0.5 + 0.25 + 0.125 + 0.125 = 1
    // 0.125*5 + 0.03125*4 + 0.0625*4 = 1

    // e:         0.125
    // a/c/g/i:   0.125
    // b/d/f/h:   0.25
    // j/k/l/m:   0.5
    // ----------------
    // total:     1.0

    float3 a, float3 b, float3 c,
    float3 d, float3 e, float3 f,
    float3 g, float3 h, float3 i,
    float3 j, float3 k, float3 l, float3 m) {
    float3 sampleColor = float3(0.0, 0.0, 0.0);

    sampleColor = e * 0.125;
    sampleColor += (a + c + g + i) * 0.03125;
    sampleColor += (b + d + f + h) * 0.0625;
    sampleColor += (j + k + l + m) * 0.125;

    return sampleColor;
}

// -------------------------------------------- Karis权重下采样 -------------------------------------------
float luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float karisWeight(float3 color) {
    // 防止 HDR 极亮点支配平均值
    return 1.0 / (1.0 + max(luminance(color), 0.0));
}

float3 karisAverage4(float3 a, float3 b, float3 c, float3 d) {
    float wa = karisWeight(a);
    float wb = karisWeight(b);
    float wc = karisWeight(c);
    float wd = karisWeight(d);

    float wsum = wa + wb + wc + wd;

    return (a * wa + b * wb + c * wc + d * wd) / max(wsum, 1e-5);
}

float3 downSampleKaris(
    float3 a, float3 b, float3 c,
    float3 d, float3 e, float3 f,
    float3 g, float3 h, float3 i,
    float3 j, float3 k, float3 l, float3 m) {
    // 分成五组，中间组的权重最高，外侧四组权重较低，且相等
    // 0.5 + 0.125 + 0.125 + 0.125 + 0.125 = 1
    // Group 1: a b d e
    // Group 2: b c e f
    // Group 3: d e g h
    // Group 4: e f h i
    // Group 5: j k l m
    float3 group0 = karisAverage4(a, b, d, e);
    float3 group1 = karisAverage4(b, c, e, f);
    float3 group2 = karisAverage4(d, e, g, h);
    float3 group3 = karisAverage4(e, f, h, i);
    float3 group4 = karisAverage4(j, k, l, m);

    float3 sampleColor = float3(0.0, 0.0, 0.0);

    // 四个外侧 2x2 group 各 0.125
    sampleColor += group0 * 0.125;
    sampleColor += group1 * 0.125;
    sampleColor += group2 * 0.125;
    sampleColor += group3 * 0.125;

    // 中心附近 group 占 0.5
    sampleColor += group4 * 0.5;

    return sampleColor;
}

float3 downSample(float2 texelSize, float2 uv) {
    float3 sampleColor = float3(0.0, 0.0, 0.0);
    // === 像素采样卷积核(笔记中有图) ===
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

    if (bloomPushConstants.useKairsWeight == 1)
        sampleColor = downSampleKaris(a, b, c, d, e, f, g, h, i, j, k, l, m);
    else 
        sampleColor = downSampleNormal(a, b, c, d, e, f, g, h, i, j, k, l, m);

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