struct FSInput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

static const float PI = 3.1415926536;

// 低差异序列
float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

// 获取大小为N的样本集中的低差异i
float2 Hammersley(uint i, uint N) {
    return float2(float(i)/float(N), RadicalInverse_VdC(i));
}

// 重要性采样: Xi相当于映射phi和theta的值，期中phi是均匀的
float3 importanceSampleGGX(float2 Xi, float roughness, float3 N) {
    float a = roughness * roughness;
    
    float phi = 2.0 * 3.14159265359 * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y)); // DGGX分布函数反函数推导得出
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    
    // 从切线空间转换到世界空间
    float3 H;
    H.x = sinTheta * cos(phi);
    H.y = sinTheta * sin(phi);
    H.z = cosTheta;
    
    // 构建切线空间
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangentX = normalize(cross(up, N));
    float3 tangentY = cross(N, tangentX);
    
    // 转换到世界空间
    return normalize(tangentX * H.x + tangentY * H.y + N * H.z);
}

// --------------------------------------------- G : Schlick-GGX ---------------------------------------------
float G_SchlickGGX(float NdotV, float roughness) {
    float k = (roughness * roughness) / 2;  // 这里和直接光照的不同

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    denom = max(0.00001f, denom);

    return nom / denom;
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    // 兼顾视线方向V 和 光照方向L
    float ggxV = G_SchlickGGX(NdotV, roughness);
    float ggxL = G_SchlickGGX(NdotL, roughness);
    return ggxV * ggxL;
}

// 核心函数: 实现近似和的另一部分的积分运算, 推导过程在笔记中
float2 integrateBRDF(float NdotV, float roughness) {
    // 局部法向量方向设置朝向Z轴方向
    float3 N = float3(0.0, 0.0, 1.0);

    // 根据NdotV构造V, 各项同性旋转具有对称性，因此y分量可以设置为0简化运算
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    const uint sampleCount = 4096;
    for (uint i = 0; i < sampleCount; ++i) {
        float2 xi = Hammersley(i, sampleCount);
        float3 H = importanceSampleGGX(xi, roughness, N);   // D重要性采样构造半程向量H
        float3 L = reflect(-V, H);

        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0);
        float VdotH = max(dot(V, H), 0.0);
        float NdotH = max(dot(N, H), 0.0);

        if (NdotL > 0.0) {
            float G = G_Smith(NdotV, NdotL, roughness);
            // pdf = pdf = D * NdotH / (4 * VdotH); 推导：用H代替L的转换
            float GVis = G * VdotH / max(NdotV * NdotH, 0.0001);    // 推导公式除以pdf后化简的结果
            float Fc = pow(1.0 - VdotH, 5);

            A += (1.0 - Fc) * GVis;
            B += Fc * GVis;
        }
    }
    
    // 依旧蒙特卡洛积分
    A /= float(sampleCount);
    B /= float(sampleCount);
    return float2(A, B);
}

FSOutput main(FSInput input) {
    FSOutput output;
    float NdotV = input.uv.x;
    float roughness = input.uv.y;
    float2 result = integrateBRDF(NdotV, roughness);
    output.color = float4(result.x, result.y, 0.0, 1.0);
    return output;
}