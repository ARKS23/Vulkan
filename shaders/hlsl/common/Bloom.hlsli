#ifndef COMMON_BLOOM_HLSLI
#define COMMON_BLOOM_HLSLI

float BloomLuminance(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float BloomKarisWeight(float3 color) {
    return 1.0f / (1.0f + max(BloomLuminance(color), 0.0f));
}

float3 BloomKarisAverage4(float3 a, float3 b, float3 c, float3 d) {
    float wa = BloomKarisWeight(a);
    float wb = BloomKarisWeight(b);
    float wc = BloomKarisWeight(c);
    float wd = BloomKarisWeight(d);
    float weightSum = max(wa + wb + wc + wd, 1e-5f);
    return (a * wa + b * wb + c * wc + d * wd) / weightSum;
}

float3 BloomDownsample13Tap(
    float3 a, float3 b, float3 c,
    float3 d, float3 e, float3 f,
    float3 g, float3 h, float3 i,
    float3 j, float3 k, float3 l, float3 m) {
    float3 color = e * 0.125f;
    color += (a + c + g + i) * 0.03125f;
    color += (b + d + f + h) * 0.0625f;
    color += (j + k + l + m) * 0.125f;
    return color;
}

float3 BloomDownsample13TapKaris(
    float3 a, float3 b, float3 c,
    float3 d, float3 e, float3 f,
    float3 g, float3 h, float3 i,
    float3 j, float3 k, float3 l, float3 m) {
    float3 group0 = BloomKarisAverage4(a, b, d, e);
    float3 group1 = BloomKarisAverage4(b, c, e, f);
    float3 group2 = BloomKarisAverage4(d, e, g, h);
    float3 group3 = BloomKarisAverage4(e, f, h, i);
    float3 group4 = BloomKarisAverage4(j, k, l, m);

    return (group0 + group1 + group2 + group3) * 0.125f + group4 * 0.5f;
}

float3 BloomUpsampleTent9Tap(
    float3 a, float3 b, float3 c,
    float3 d, float3 e, float3 f,
    float3 g, float3 h, float3 i) {
    float3 color = e * 4.0f;
    color += a + c + g + i;
    color += (b + d + f + h) * 2.0f;
    return color / 16.0f;
}

float3 BloomAvoidBlackBox(float3 color) {
    return max(color, 0.0001f.xxx);
}

#endif

