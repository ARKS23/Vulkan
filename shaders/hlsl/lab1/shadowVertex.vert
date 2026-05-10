struct UBO {
    float4x4 depthMVP;
};

cbuffer ubo : register(b0) { UBO ubo;}

struct VSInput {
    float3 position : POSITION;
};

struct VSOutput {
    float4 position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(ubo.depthMVP, float4(input.position, 1.0f));
    return output;
}
