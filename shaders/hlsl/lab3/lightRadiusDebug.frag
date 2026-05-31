struct FSInput {
    [[vk::location(0)]] float3 color : COLOR0;
    [[vk::location(1)]] float alpha : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

FSOutput main(FSInput input) {
    FSOutput output;
    output.color = float4(input.color, input.alpha);
    return output;
}
