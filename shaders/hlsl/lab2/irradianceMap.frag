struct FSInput {
    [[vk::location(0)]] float3 UVW : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

struct Pushconstants {
    float4x4 mvp;
    float deltaPhi;
    float deltaTheta;
};
[[vk::push_constant]] Pushconstants pushConstants;

TextureCube envCube : register(t0);
SamplerState envSampler : register(s0);

#define PI 3.1415926535897932384626433832795

float4 conv(float3 UVW, float deltaPhi, float deltaTheta) {
    // 构建本地坐标系,后续采样要使用
    float3 N = normalize(UVW);
    float3 helper = float3(0.0, 1.0, 0.0);
    float3 right = normalize(cross(helper, N));
    helper = normalize(cross(N, right));

    // 采样上半球
    float twoPI = 2 * PI;
    float halfPI = 0.5 * PI;

    uint sampleCnt = 0;
    float3 color = float3(0.0, 0.0, 0.0);
    for (float phi = 0.0; phi < twoPI; phi += deltaPhi) {
        for (float theta = 0.0; theta < halfPI; theta += deltaTheta) {
            float3 tagentVec = float3(
                sin(theta) * cos(phi),
                sin(theta) * sin(phi),
                cos(theta)
            );
            float3 samplevec = normalize(tagentVec.x * right + tagentVec.y * helper + tagentVec.z * N); // 转成世界坐标
            color += envCube.Sample(envSampler, samplevec).rgb * cos(theta) * sin(theta);
            sampleCnt += 1;
        }
    }
    color = PI * color / float(sampleCnt);
    return float4(color, 1.0);
}

FSOutput main(FSInput input) {
    FSOutput output;
    output.color = conv(input.UVW, pushConstants.deltaPhi, pushConstants.deltaTheta);
    return output;
}