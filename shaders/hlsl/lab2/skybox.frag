struct FSInput {
    [[vk::location(0)]] float3 uvw : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

TextureCube skyboxTexture : register(t1, space0);
SamplerState skyboxSampler : register(s1, space0);

FSOutput main(FSInput input) {
    FSOutput output;
    float3 color = skyboxTexture.Sample(skyboxSampler, input.uvw).xyz;
    output.color = float4(color, 1.0f);
    return output;
}
