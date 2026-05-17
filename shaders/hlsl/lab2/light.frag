struct FSOutput {
    float4 color : SV_TARGET;
};

struct Pushconstants {
    float4 Pos;
    float4 Color;
    float4 Intensity;
};
[[vk::push_constant]] Pushconstants pushconstants;

FSOutput main() {
    FSOutput output;
    float3 color = pushconstants.Color.rgb;
    output.color = float4(color, 1.0f);
    return output;
}