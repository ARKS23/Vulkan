struct VSInput {
    [[vk::location(0)]] float3 pos : POSITION0;
};

struct VSOutput {
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float3 UVW : TEXCOORD0;
};

struct Pushconstants {
    float4x4 mvp;
    float deltaPhi;
    float deltaTheta;
};
[[vk::push_constant]] Pushconstants pushConstants;

VSOutput main(VSInput input) {
    VSOutput output;
    output.pos = mul(pushConstants.mvp, float4(input.pos, 1.0));
    output.UVW = input.pos;
    return output;
}