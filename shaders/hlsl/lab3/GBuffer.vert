struct VSInput {
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float3 normal   : NORMAL0;
    [[vk::location(2)]] float2 uv       : TEXCOORD0;
    [[vk::location(3)]] float4 tangent  : TANGENT0;
    [[vk::location(4)]] float4 color    : COLOR0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    [[vk::location(0)]] float3 normal : NORMAL0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 worldPos : POSITION0;
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

// 实例化数据
struct InstanceData {
    float4x4 model;
    float4x4 normalMatrix;
    float4 color;
    float4 materialParams; // metallicMul, roughnessMul, emissiveMul, materialIndex/unused
};
[[vk::binding(2, 0)]] StructuredBuffer<InstanceData> instances;

VSOutput main(VSInput input, uint instanceID : SV_InstanceID) {
    VSOutput output;

    InstanceData instance = instances[instanceID];
    float4x4 model = instance.model;
    float3x3 normalMatrix = float3x3(instance.normalMatrix[0].xyz, instance.normalMatrix[1].xyz, instance.normalMatrix[2].xyz);

    // 计算变换
    float4 worldPos = mul(model, float4(input.position, 1.0f));
    float3 normal = normalize(mul(normalMatrix, input.normal));

    // 填入输出结构体
    output.position = mul(cameraInfo.projection, mul(cameraInfo.view, worldPos));
    output.uv = input.uv;
    output.normal = normal;
    output.worldPos = worldPos;

    return output;
}
