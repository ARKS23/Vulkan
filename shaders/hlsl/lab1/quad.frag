Texture2D offscreenColorTexture : register(t1);
SamplerState offscreenColorSampler : register(s1);

struct FSInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

FSOutput main(FSInput input) {
    FSOutput output;
    float depth = offscreenColorTexture.Sample(offscreenColorSampler, input.uv).r;
    output.color = float4(1- depth, 1- depth, 1- depth, 1.0f);
    return output;
}