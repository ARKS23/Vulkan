struct FSOutput {
    float4 color : SV_TARGET;
};


struct PushConstantData {
    float4 lightColor;
    float4x4 mvp;
};
[[vk::push_constant]] PushConstantData pushConstants;

FSOutput main() {
    FSOutput output;
    output.color = pushConstants.lightColor;
    return output;
}