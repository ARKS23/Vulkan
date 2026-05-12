struct VSInput {
    [[vk::location(0)]] float3 position : POSITION0;
};

struct VSOutput {
    [[vk::location(0)]] float4 position : SV_POSITION;
};

struct PushConstantData {
    float4 lightColor;
    float4x4 mvp;
};
[[vk::push_constant]] PushConstantData pushConstants;

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(pushConstants.mvp, float4(input.position, 1.0));
    return output;
}