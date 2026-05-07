// Copyright 2020 Google LLC

struct VSInput
{
[[vk::location(0)]] float3 Pos : POSITION0;
[[vk::location(1)]] float3 Color : COLOR0;
[[vk::location(2)]] float2 UV : TEXCOORD0;
};

struct UBO
{
	float4x4 projectionMatrix;
	float4x4 modelMatrix;
	float4x4 viewMatrix;
};

cbuffer ubo : register(b0) { UBO ubo; }

struct VSOutput
{
	float4 Pos : SV_POSITION;
[[vk::location(0)]] float3 Color : COLOR0;
[[vk::location(1)]] float2 UV : TEXCOORD0;
};

struct PushConstants {
  float4x4 modelMatrix;
  float4 ColorMultiplier;
};

[[vk::push_constant]] PushConstants pushConstants;

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;
	output.Color = input.Color;
	output.UV = input.UV;
	output.Pos = mul(ubo.projectionMatrix, 
				mul(ubo.viewMatrix, 
				mul(pushConstants.modelMatrix, float4(input.Pos.xyz, 1.0))));
	return output;
}
