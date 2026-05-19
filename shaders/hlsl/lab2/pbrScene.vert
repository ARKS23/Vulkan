struct VSInput {
    [[vk::location(0)]] float3 pos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
};

struct VSOutput {
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float4 worldPos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
};

struct UBOMatrix {
    float4x4 projection;
    float4x4 model;
    float4x4 view;
};
ConstantBuffer<UBOMatrix> matrices : register(b0, space0);

struct Pushconstants {
    float3 posOffset;
};
[[vk::push_constant]] Pushconstants pushconstants;

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(matrices.model, float4(input.pos, 1.0));
    worldPos = worldPos + float4(pushconstants.posOffset, 0);

    output.pos = mul(matrices.projection, mul(matrices.view, worldPos));
    output.worldPos = worldPos;
    output.normal = mul((float3x3)matrices.model, input.normal);
    return output;
}
