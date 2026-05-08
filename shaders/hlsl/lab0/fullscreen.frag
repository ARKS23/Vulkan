Texture2D offscreenTexture : register(t0);
SamplerState offscreenSampler : register(s0);

struct PSInput {
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

FSOutput main(PSInput input) {
    FSOutput output;
    float3 color = offscreenTexture.Sample(offscreenSampler, input.UV).rgb;
    float testColor = 1.15f;
    output.color = float4(color * testColor, 1.0);
    return output;
}