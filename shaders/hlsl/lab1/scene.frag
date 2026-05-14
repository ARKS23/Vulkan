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
    int shadowMode;
    float lightSize;
    int PCFRadius;
    int usePoissonDisk;
    int PoissonSampleCount;
    int debugMode;
};
[[vk::push_constant]] PushconstantData pushConstan;

Texture2D depthTexture : register(t1);
SamplerState depthSampler : register(s1); 

// 测试数据
struct DebugData {
    float shadow;
    float2 shadowUV;
    float currentDepth;
    float closetDepth;
    float shadowBias;
};

// 泊松圆盘分布
static const float2 poissonDisk[16] = {
    float2(-0.94201624, -0.39906216),
    float2( 0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870),
    float2( 0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432),
    float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845),
    float2( 0.97484398, 0.75648379),
    float2( 0.44323325, -0.97511554),
    float2( 0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023),
    float2( 0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507),
    float2(-0.81409955, 0.91437590),
    float2( 0.19984126, 0.78641367),
    float2( 0.14383161, -0.14100790)
};

float random01(float2 p) {
    // 白噪声函数，基于sin和dot的哈希函数，生成一个0到1之间的随机数
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float2 rotate2D(float2 v, float angle) {
    // 逆时针二维旋转矩阵，三角函数公式推导出来
    float cosAngle = cos(angle);
    float sinAngle = sin(angle);
    return float2(v.x * cosAngle - v.y * sinAngle, v.x * sinAngle + v.y * cosAngle);
}

float computeBias(float3 N, float3 L) {
    float ndotL = saturate(dot(N, L));
    float bias = max(pushConstan.minShadowBias, pushConstan.slopeShadowBias * (1 - ndotL));
    return bias;
}

float shadowCompare(float2 uv, float currentDepth, float bias) {
    float closetDepth = depthTexture.Sample(depthSampler, uv).r;
    return currentDepth - bias > closetDepth ? 1.0 : 0.0;
}

float calculatePCF(float2 shadowUV, float currentDepth, float bias, float radius = 3) {
    uint shadowWidth;
    uint shadowHeight;
    depthTexture.GetDimensions(shadowWidth, shadowHeight);  // 获取尺寸

    float2 texelSize = 1.0 / float2(shadowWidth, shadowHeight); // 单位格子
    float shadow = 0.0f;
    int sampleCount = 0;

    if (pushConstan.usePoissonDisk == 1) {  // 泊松圆盘采样
        sampleCount = pushConstan.PoissonSampleCount;
        [unroll]
        for (int i = 0; i < sampleCount; ++i) {
            float2 offset = poissonDisk[i] * radius * texelSize;
            float angle = random01(shadowUV * 4096) * 6.2831853;
            offset = rotate2D(offset, angle); // 随机旋转，减少重复采样带来的伪影
            shadow += shadowCompare(shadowUV + offset, currentDepth, bias);
        }
    }
    else {
        sampleCount = (radius * 2 + 1) * (radius * 2 + 1);
        for (int x = -radius; x <= radius; ++x) {  // 根据半径调整采样范围
            for (int y = -radius; y <= radius; ++y) {
                float2 offset = float2(x, y) * texelSize;
                shadow += shadowCompare(shadowUV + offset, currentDepth, bias);
            }
        }
    }
    return shadow / float(sampleCount);
}

// PCSS平均遮挡深度计算
float findAvgBlockerDepth (float2 shadowUV, float currentDepth, float bias, float2 texelSize, float searchRadius) {
    float blockerDepthSum = 0.0f;
    int blockerCount = 0;

    [unroll]
    for (int i = 0; i < pushConstan.PoissonSampleCount; ++i) {
        float2 sampleUV = shadowUV + poissonDisk[i] * texelSize * searchRadius;
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) continue;  // 越界无效值检查

        float sampleDepth = depthTexture.Sample(depthSampler, sampleUV).r;
        if (sampleDepth < currentDepth - bias) {
            blockerDepthSum += sampleDepth;
            blockerCount++;
        }
    }

    if (blockerCount == 0) return -1.0; // 无遮挡物，完全受光

    return blockerDepthSum / blockerCount;
}

float calculatePCSS(float2 shadowUV, float currentDepth, float bias) {
    uint shadowWidth;
    uint shadowHeight;
    depthTexture.GetDimensions(shadowWidth, shadowHeight);  // 获取尺寸
    float2 texelSize = 1.0 / float2(shadowWidth, shadowHeight);
    float searchRadius = 16.0f; // 硬编码搜索半径

    // 遮挡物搜索，获取平均遮挡物深度
    float avgBlockDepth = findAvgBlockerDepth(shadowUV, currentDepth, bias, texelSize, searchRadius);
    if (avgBlockDepth < 0.0f) return 0.0f; // 无遮挡物，完全受光

    // 根据平均遮挡物深度计算PCF采样半径
    float penumbraRatio = (currentDepth - avgBlockDepth) / avgBlockDepth;
    float fliterRadius = penumbraRatio * pushConstan.lightSize * searchRadius; // 相似三角形推导出的公式变形
    fliterRadius = clamp(fliterRadius, 0.0f, 32); // 硬编码半径范围

    // PCF
    return calculatePCF(shadowUV, currentDepth, bias, fliterRadius);
}

float calculateShadow(FSInput input, float3 N, float3 L, out DebugData debugData) {
    float3 shadowCoord = input.lightSpacePos.xyz / input.lightSpacePos.w;
    float2 shadowUV = shadowCoord.xy * 0.5 + 0.5; // 将[-1, 1]范围的坐标转换为[0, 1]
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||  // 有效性检查
        shadowUV.y < 0.0 || shadowUV.y > 1.0 || 
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0) {
        return 0.0;
    }
    debugData.shadowUV = shadowUV; // 调试数据
    debugData.currentDepth = shadowCoord.z; // 调试数据
    debugData.shadowBias = computeBias(N, L); // 调试数据
    debugData.closetDepth = depthTexture.Sample(depthSampler, shadowUV).r; // 调试数据

    float currentDepth = shadowCoord.z;
    float shadowBias = computeBias(N, L);
    int PCFRadius = pushConstan.PCFRadius;
    int shadowMode = pushConstan.shadowMode;
    
    float shadow = 0.0f;
    if (shadowMode == 0) {      // Hard shadow
        shadow = shadowCompare(shadowUV, currentDepth, shadowBias);
    }
    else if (shadowMode == 1) {  // PCF
        shadow = calculatePCF(shadowUV, currentDepth, shadowBias, PCFRadius);
    }
    else if (shadowMode == 2) {  // PCSS
        shadow = calculatePCSS(shadowUV, currentDepth, shadowBias);
    }
    debugData.shadow = shadow; // 调试数据

    return shadow;
}

float4 debugOutput(float4 normalColor, FSInput input, DebugData debugData) {
    if (pushConstan.debugMode == 0) {   // 正常渲染
        return normalColor;
    }
    else if (pushConstan.debugMode == 1) {  // 阴影部分显示为黑色，非阴影部分显示为白色
        return float4((1.0 - debugData.shadow), (1.0 - debugData.shadow), (1.0 - debugData.shadow),1.0);
    }
    else if (pushConstan.debugMode == 2) {  // 显示阴影贴图坐标
        return float4(debugData.shadowUV, 0, 1);
    }
    else if (pushConstan.debugMode == 3) {  // 显示当前片元深度
        return float4(debugData.currentDepth, debugData.currentDepth, debugData.currentDepth, 1);
    }
    else if (pushConstan.debugMode == 4) {  // 显示最近的深度
        return float4(debugData.closetDepth, debugData.closetDepth, debugData.closetDepth, 1);
    }
    else if (pushConstan.debugMode == 5) {  // 显示阴影偏移
        return float4((debugData.shadowBias * 100.0), (debugData.shadowBias * 100.0), (debugData.shadowBias * 100.0), 1.0);
    }
    else if (pushConstan.debugMode == 6) {  // 法线可视化
        return float4(normalize(input.normal) * 0.5 + 0.5, 1);
    }
    return normalColor;
}


FSOutput main(FSInput input) {
    FSOutput output;
    float3 lightColor = pushConstan.lightColor.xyz;

    float3 N = normalize(input.normal);
    float3 L = normalize(input.lightPos.xyz - input.worldPos.xyz);
    float3 V = normalize(input.cameraPos.xyz - input.worldPos.xyz);
    float3 H = normalize(L + V);

    DebugData debugData; // 调试数据

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
    float shadow = calculateShadow(input, N, L, debugData);
    // bling-phong result
    float3 phongColor = ambient + (1 - shadow) * (diffuse + specular);
    
    output.color = debugOutput(float4(phongColor, 1.0), input, debugData);
    return output;
}