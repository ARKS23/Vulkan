static const int MAX_LIGHT_COUNT = 150;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION0;
};

struct VSOutput {
    float4 position : SV_POSITION;
    [[vk::location(0)]] float3 color : COLOR0;
};

struct CameraUBO {
    float4x4 projection;
    float4x4 view;
    float4x4 inverseProjection;
    float4x4 inverseView;
    float4 cameraPos;
    float4 screenSize;
};
ConstantBuffer<CameraUBO> cameraInfo : register(b0);

struct Light {
    float4 position;
    float4 color;
    // x: PBR lighting intensity, y: visual emissive intensity, z: visual radius, w: lighting radius.
    float4 intensity;
};

struct LightsUBO {
    Light lights[MAX_LIGHT_COUNT];
    int4 lightCount;
};
ConstantBuffer<LightsUBO> lightsInfo : register(b1);

VSOutput main(VSInput input, uint instanceID : SV_InstanceID) {
    VSOutput output;

    Light light = lightsInfo.lights[instanceID];
    float radius = max(light.intensity.z, 0.001f);

    float3 worldPosition = light.position.xyz + input.position * radius;
    output.position = mul(cameraInfo.projection, mul(cameraInfo.view, float4(worldPosition, 1.0f)));
    output.color = light.color.rgb * light.intensity.y;

    return output;
}
