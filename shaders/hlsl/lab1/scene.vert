struct VSInput {
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 color : COLOR0;
    [[vk::location(3)]] float3 normal : NORMAL0;
};

struct VSOutput {
    float4 outPosition : SV_POSITION;
    [[vk::location(0)]] float4 worldPos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float3 baseColor : COLOR0;
    [[vk::location(3)]] float4 cameraPos : POSITION1;
    [[vk::location(4)]] float4 lightPos : POSITION2;
    [[vk::location(5)]] float4 lightSpacePos : TEXCOORD0;
};

struct UBO {
    float4x4 projection;
    float4x4 view;
    float4x4 model;
    float4x4 depthBiasMVP;
    float4 lightPos;
    float4 cameraPos;
    float zNear;
    float zFar;
};
cbuffer ubo : register(b0) { UBO ubo; }

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(ubo.model, float4(input.position, 1.0));

    output.outPosition = mul(ubo.projection, mul(ubo.view, worldPos));
    output.worldPos = worldPos;
    output.normal = normalize(mul((float3x3)ubo.model, input.normal));
    output.baseColor = input.color;
    output.cameraPos = ubo.cameraPos;
    output.lightPos = ubo.lightPos;
    output.lightSpacePos = mul(ubo.depthBiasMVP, output.worldPos);
    return output;
}