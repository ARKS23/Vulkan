struct FSInput {
    float4 position : SV_POSITION;
    [[vk::location(0)]] float3 normal : NORMAL0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 worldPos : POSITION0;
};

struct FSOutput {
    float4 albedoMetallic : SV_TARGET0;
    float4 normalRoughness : SV_TARGET1;
    float4 emissiveAO : SV_TARGET2;
    // 深度图不用显示输出，直接写入深度缓冲
};

// 变换矩阵和相机信息
struct CameraUBO {
    float4x4 projection;
    float4x4 view;
    float4x4 inverseProjection;
    float4x4 inverseView;
    float4 cameraPos;
    float4 screenSize; 
};
[[vk::binding(0, 0)]] ConstantBuffer<CameraUBO> cameraInfo : register(b0);

// PBR纹理资源
Texture2D baseColorMap : register(t0, space1);
SamplerState baseColorSampler : register(s0, space1);

Texture2D metallicRoughnessMap : register(t1, space1);
SamplerState metallicRoughnessSampler : register(s1, space1);

Texture2D normalMap : register(t2, space1);
SamplerState normalSampler : register(s2, space1);

Texture2D occlusionMap : register(t3, space1);
SamplerState occlusionSampler : register(s3, space1);

Texture2D emissiveMap : register(t4, space1);
SamplerState emissiveSampler : register(s4, space1);

// 没有顶点tangent的情况，直接从屏幕空间导数构造TBN，把法线贴图切线空间变换到世界空间
float3 getWorldNormalFromMap(FSInput input) {
    float3 N = input.normal;
    float3 tangentNormal = normalMap.Sample(normalSampler, input.uv).xyz * 2.0 - 1.0; // 从[0,1]映射到[-1,1]

    float3 dp1 = ddx(input.worldPos.xyz);
    float3 dp2 = ddy(input.worldPos.xyz);
    float2 duv1 = ddx(input.uv);
    float2 duv2 = ddy(input.uv);

    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    // 链式法则
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x; // 屏幕 x 方向的位置变化 = U方向贡献 + V方向贡献
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y; // 屏幕 y 方向的位置变化 = U方向贡献 + V方向贡献

    float invMax = rsqrt(max(dot(T, T), dot(B, B)));
    float3x3 TBN = float3x3(T * invMax, B * invMax, N);

    return normalize(mul(tangentNormal, TBN)); // 等价于tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * N
}

FSOutput main(FSInput input) {
    FSOutput output;

    // 从纹理中采样
    float4 albedo = baseColorMap.Sample(baseColorSampler, input.uv);
    float4 metallicRoughness = metallicRoughnessMap.Sample(metallicRoughnessSampler, input.uv);
    float4 occlusion = occlusionMap.Sample(occlusionSampler, input.uv);
    float4 emissive = emissiveMap.Sample(emissiveSampler, input.uv);
    float3 normal = getWorldNormalFromMap(input);

    // 输出GBuffer数据
    output.albedoMetallic = float4(albedo.rgb, metallicRoughness.b); 
    output.normalRoughness = float4(normal.rgb, metallicRoughness.g); 
    output.emissiveAO = float4(emissive.rgb, occlusion.r);

    return output;
}