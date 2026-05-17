struct VSInput {
    [[vk::location(0)]] float3 pos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
};

struct VSOutput {
    float4 pos : SV_POSITION;
};

struct UBOMatrix {
    float4x4 projection;
    float4x4 model;
    float4x4 view;
    float3 camPos;
};
ConstantBuffer<UBOMatrix> matrices : register(b0, space0);

struct Pushconstants {
    float4 Pos;
    float4 Color;
    float4 Intensity;
};
[[vk::push_constant]] Pushconstants pushconstants;

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(matrices.model, float4(input.pos, 1.0f));
    worldPos = worldPos + pushconstants.Pos;
    output.pos = mul(matrices.projection, mul(matrices.view, worldPos));
    return output;
}