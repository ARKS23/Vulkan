struct FSInput {
    [[vk::location(0)]] float4 worldPos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float3 baseColor : COLOR0;
    [[vk::location(3)]] float4 cameraPos : POSITION1;
    [[vk::location(4)]] float4 lightPos : POSITION2;
    [[vk::location(5)]] float4 ShadowCoord : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

Texture2D depthTexture : register(t1);
SamplerState depthSampler : register(s1); 

float calculateShadow(FSInput input) {
    // TODO: 阴影计算
    return 0.0;
}


FSOutput main(FSInput input) {
    FSOutput output;
    float3 lightColor = float3(0.13, 0.11, 0.45);

    float3 N = input.normal;
    float3 L = normalize(input.lightPos.xyz - input.worldPos.xyz);
    float3 V = normalize(input.cameraPos.xyz - input.worldPos.xyz);
    float3 H = normalize(L + V);

    // 环境光
    float ambientStrength = 0.1f;
    float3 ambient = input.baseColor * lightColor * ambientStrength;
    // 漫反射
    float diff = max(dot(L, N), 0.0);
    float3 diffuse = lightColor * input.baseColor * diff;
    // 高光: 只有光源颜色
    float shiness = 32;
    float specularStrength = 0.8;
    float spec = pow(max(dot(H, N), 0.0), shiness) * specularStrength;
    float3 specular = lightColor * spec;
    // 阴影计算
    float shadow = calculateShadow(input);
    // bling-phong result
    float3 phongColor = ambient + (1 - shadow) * (diffuse + specular);
    
    output.color = float4(phongColor.xyz, 1);
    return output;
}