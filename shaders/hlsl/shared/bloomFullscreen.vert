struct VSOutput {
    float4 position : SV_POSITION;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

VSOutput main(uint vertexIndex : SV_VertexID) {
    float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2( 3.0f, -1.0f),
        float2(-1.0f,  3.0f)
    };

    float2 uvs[3] = {
        float2(0.0f, 0.0f),
        float2(2.0f, 0.0f),
        float2(0.0f, 2.0f)
    };

    VSOutput output;
    output.position = float4(positions[vertexIndex], 0.0f, 1.0f);
    output.uv = uvs[vertexIndex];
    return output;
}

