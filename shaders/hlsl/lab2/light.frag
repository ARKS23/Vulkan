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
    float3 rawIntensity = pushconstants.Intensity.rgb;
    float visibleIntensity = 1.0f + 5.0f * saturate(rawIntensity / 200.0f); // 光强压缩
    color *= visibleIntensity;
    output.color = float4(color, 1.0f);
    return output;
}