struct VSOutput {
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

VSOutput main(uint vertexIndex : SV_VertexID) {
    // uv.x = (pos.x + 1) / 2, 单三角形覆盖屏幕，多余部分会被裁剪掉
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
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
    // vulkan的y轴向下，所以要把uv的y坐标翻转一下
    // output.UV = float2(uvs[vertexIndex].x, 1.0 - uvs[vertexIndex].y);

    return output;
}