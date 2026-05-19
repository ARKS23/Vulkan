struct VSInput {
    [[vk::location(0)]] float4 pos : POSITION0;
};

struct VSOutput {
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float3 uvw : TEXCOORD0;
};

struct UBOMatrix {
    float4x4 projection;
    float4x4 model;
    float4x4 view;
    float3 camPos;
};
ConstantBuffer<UBOMatrix> matrices : register(b0, space0);

VSOutput main(VSInput input) {
    VSOutput output;
    output.uvw = input.pos.xyz;
    output.pos = mul(matrices.projection, mul(matrices.view, mul(matrices.model, float4(input.pos.xyz, 1.0f))));
    output.pos = output.pos.xyww;
    return output;
}