struct FSInput {
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

struct Pushconstants {
    float exposure;
    float bloomStrength;
    uint enableBloom;
    float padding;
};
[[vk::push_constant]] Pushconstants pushConstants;

Texture2D hdrScene : register(t0, space0);
SamplerState hdrSampler : register(s0, space0);

Texture2D bloomTexture : register(t1, space0);
SamplerState bloomSampler : register(s1, space0);

float3 ACESFilm(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

FSOutput main(FSInput input) {
    FSOutput output;
    float3 hdrColor = hdrScene.Sample(hdrSampler, input.UV).rgb;
    float3 bloomColor = bloomTexture.Sample(bloomSampler, input.UV).rgb;
    if (pushConstants.enableBloom == 0) bloomColor = float3(0.0, 0.0, 0.0);
    float3 color = lerp(hdrColor, hdrColor + bloomColor, pushConstants.bloomStrength);

    color *= pushConstants.exposure;
    color = ACESFilm(color);
    color = pow(saturate(color), 1.0f / 2.2f);

    output.color = float4(color, 1.0f);
    return output;
}
