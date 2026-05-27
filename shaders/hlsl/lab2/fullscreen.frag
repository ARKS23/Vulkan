Texture2D offscreenTexture : register(t0);
SamplerState offscreenSampler : register(s0);

struct PSInput {
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

float3 ACESFilm(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

FSOutput main(PSInput input) {
    FSOutput output;
    float3 color = offscreenTexture.Sample(offscreenSampler, input.UV).rgb;
    color = ACESFilm(color);
    color = pow(saturate(color), 1.0f / 2.2f);
    output.color = float4(color, 1.0);
    return output;
}
