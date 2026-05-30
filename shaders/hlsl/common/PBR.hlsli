#ifndef COMMON_PBR_HLSLI
#define COMMON_PBR_HLSLI

#include "Math.hlsli"

// --------------------------------------------- D : Trowbridge-Reitz GGX ---------------------------------------------
float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = NdotH2 * (a2 - 1) + 1;
    denom = max(0.00001f, PI * denom * denom); // 防止除0
    
    return nom / denom;
}

// --------------------------------------------- G : Schlick-GGX ---------------------------------------------
float G_SchlickGGX_Direction(float NdotV, float roughness) {
    float k = (roughness + 1) * (roughness + 1) / 8;    // 直接光照k

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    denom = max(0.00001f, denom);

    return nom / denom;
}

float G_SchlickGGX_Indirection(float NdotV, float roughness) {
    float k = (roughness * roughness) / 2;              // 间接光照k

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    denom = max(0.00001f, denom);

    return nom / denom;
}

// --------------------------------------------- G : Schlick-GGX ---------------------------------------------
float G_SchlickGGX(float NdotV, float roughness) {
    float k = (roughness + 1) * (roughness + 1) / 8;

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

// --------------------------------------------- F : Schlick's approximation ---------------------------------------------
float3 F_Schlick(float cosTheta, float3 F0) {
    // cosTheta一般传入VdotH
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float3 F_SchlickRoughness(float cosTheta, float3 F0, float roughness) {
    // IBL 中使用的 Fresnel-Schlick 近似，加入 roughness 修正
    // 通常 cosTheta 传入 NdotV，用于计算环境光部分的 kS/kD
    cosTheta = saturate(cosTheta);
    roughness = saturate(roughness);

    //float3 maxF = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
    float3 maxF = max((1.0 - roughness).xxx, F0);
    return F0 + (maxF - F0) * pow(1.0 - cosTheta, 5.0);
}

// --------------------------------------------- BRDF: Cook-Torrance BRDF ---------------------------------------------
float3 direcBRDF(float3 N, float3 V, float3 L, float3 H, float roughness, float metallic, float3 albedo, float3 F0) {
    // 直接光照计算函数
    N = normalize(N);
    V = normalize(V);
    L = normalize(L);
    H = normalize(H);

    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    roughness = clamp(roughness, 0.04, 1.0);
    metallic = saturate(metallic);
    
    // DFG
    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(VdotH, F0);

    // 能量分配：镜面反射 + 漫反射
    float3 ks = F;   // 镜面反射比例
    float3 kd = (1.0 - ks) * (1.0 - metallic); // 漫反射比例,而且金属没有漫反射

    // Cook-Torrance specular BRDF
    float3 nom = D * F * G;
    float denom = max(4.0 * NdotV * NdotL, 0.00001);
    float3 specular = nom / denom;

    // Lambert 漫反射
    float3 diffuse = kd * albedo / PI;

    return specular + diffuse;
}

#endif