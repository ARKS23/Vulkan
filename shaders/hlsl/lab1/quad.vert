struct VSOutput {
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float2 UV : TEXCOORD;
};

VSOutput main(uint vertexIndex : SV_VertexID) {
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0)
    };

    float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0)
    };

    VSOutput output;
    output.Pos = float4(positions[vertexIndex], 0.0, 1.0);
    output.UV = uvs[vertexIndex];
    return output;
}