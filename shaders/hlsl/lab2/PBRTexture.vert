struct VSInput {
    [[vk::location(0)]] float3 pos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float4 tangent : TANGENT0;
};

struct VSOutput {
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float4 worldPos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] float4 tangent : TANGENT0;
};

struct UBOMatrix {
    float4x4 projection;
    float4x4 model;
    float4x4 view;
};
ConstantBuffer<UBOMatrix> matrices : register(b0, space0);

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(matrices.model, float4(input.pos, 1.0));

    output.pos = mul(matrices.projection, mul(matrices.view, worldPos));
    output.worldPos = worldPos;
    output.normal = mul((float3x3)matrices.model, input.normal);
    output.uv = input.uv;
    output.tangent = float4(normalize(mul((float3x3)matrices.model, input.tangent.xyz)), input.tangent.w);
    return output;
}
