struct FSInput {
    [[vk::location(0)]] float4 worldPos : POSITION0;
    [[vk::location(1)]] float3 normal : NORMAL0;
    [[vk::location(2)]] float3 baseColor : COLOR0;
    [[vk::location(3)]] float4 cameraPos : POSITION1;
    [[vk::location(4)]] float4 lightPos : POSITION2;
    [[vk::location(5)]] float4 lightSpacePos : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

// 推送常量
struct PushconstantData {
    float4 lightColor;
    float minShadowBias;
    float slopeShadowBias;
    int enablePCF;
    int PCFRadius;
};
[[vk::push_constant]] PushconstantData pushConstan;

Texture2D depthTexture : register(t1);
SamplerState depthSampler : register(s1); 

float computeBias(float3 N, float3 L) {
    float ndotL = saturate(dot(N, L));
    float bias = max(pushConstan.minShadowBias, pushConstan.slopeShadowBias * (1 - ndotL));
    return bias;
}

float shadowCompare(float2 uv, float currentDepth, float bias) {
    float closetDepth = depthTexture.Sample(depthSampler, uv).r;
    return currentDepth - bias > closetDepth ? 1.0 : 0.0;
}

float calculatePCF(float2 shadowUV, float currentDepth, float bias, int radius = 3) {
    uint shadowWidth;
    uint shadowHeight;
    depthTexture.GetDimensions(shadowWidth, shadowHeight);  // 获取尺寸

    float2 texelSize = 1.0 / float2(shadowWidth, shadowHeight); // 单位格子
    float shadow = 0.0f;
    int sampleCount = (radius * 2 + 1) * (radius * 2 + 1);
    //[unroll] // 循环展开指令
    for (int x = -radius; x <= radius; ++x) {  // 根据半径调整采样范围
        //[unroll]
        for (int y = -radius; y <= radius; ++y) {
            float2 offset = float2(x, y) * texelSize;
            shadow += shadowCompare(shadowUV + offset, currentDepth, bias);
        }
    }
    return shadow / float(sampleCount);
}

float calculateShadow(FSInput input, float3 N, float3 L) {
    float3 shadowCoord = input.lightSpacePos.xyz / input.lightSpacePos.w;
    float2 shadowUV = shadowCoord.xy * 0.5 + 0.5; // 将[-1, 1]范围的坐标转换为[0, 1]
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||  // 有效性检查
        shadowUV.y < 0.0 || shadowUV.y > 1.0 || 
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0) {
        return 0.0;
    }

    float currentDepth = shadowCoord.z;
    float shadowBias = computeBias(N, L);
    int enablePCF = pushConstan.enablePCF;
    int PCFRadius = pushConstan.PCFRadius;
    
    float shadow = 0.0f;
    if (enablePCF) {
        shadow = calculatePCF(shadowUV, currentDepth, shadowBias, PCFRadius);
    }
    else {
        shadow = shadowCompare(shadowUV, currentDepth, shadowBias);
    }
    return shadow;
}


FSOutput main(FSInput input) {
    FSOutput output;
    float3 lightColor = pushConstan.lightColor.xyz;

    float3 N = normalize(input.normal);
    float3 L = normalize(input.lightPos.xyz - input.worldPos.xyz);
    float3 V = normalize(input.cameraPos.xyz - input.worldPos.xyz);
    float3 H = normalize(L + V);

    // 环境光
    float ambientStrength = 0.2f;
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
    float shadow = calculateShadow(input, N, L);
    // bling-phong result
    float3 phongColor = ambient + (1 - shadow) * (diffuse + specular);
    
    output.color = float4(phongColor.xyz, 1);
    return output;
}