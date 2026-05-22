struct FSInput {
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float3 UVW : TEXCOORD0;
};

struct FSOutput {
    float4 color : SV_TARGET;
};

struct Pushconstants {
    float4x4 mvp;
    float roughness;
    uint numSamples;
};
[[vk::push_constant]] Pushconstants pushConstants;

TextureCube skyboxTextureCube : register(t0);
SamplerState skyboxSampler : register(s0);

static const float PI = 3.14159265359;

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

FSOutput main(FSInput input) {
    FSOutput output;

    float3 N = normalize(input.UVW);
    float3 R = N;
    float3 V = R;

    const uint sampleCnt = pushConstants.numSamples;
    float totalWeight = 0.0f;
    float3 prefilterColor = float3(0.0, 0.0, 0.0);
    float roughness = pushConstants.roughness;
    for (uint i = 0; i < sampleCnt; ++i) {
        float2 xi = Hammersley(i, sampleCnt);
        float3 H = importanceSampleGGX(xi, roughness, N);
        float3 L = reflect(-V, H);

        float NdotL = max(dot(N, L), 0.0);  // radiance
        if (NdotL > 0.0) {
            prefilterColor += skyboxTextureCube.Sample(skyboxSampler, L).rgb * NdotL;
            totalWeight += NdotL;   // 权重处理当作概率函数，用于蒙特卡洛积分
        }
    }
    prefilterColor = prefilterColor / totalWeight;  // 蒙特卡洛积分

    output.color = float4(prefilterColor, 1.0);
    return output;
}