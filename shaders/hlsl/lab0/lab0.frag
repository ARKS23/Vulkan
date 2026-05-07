struct PushConstants {
  float4x4 modelMatrix;
  float4 ColorMultiplier;
};
[[vk::push_constant]] PushConstants pushConstants;

Texture2D baseColorTexture : register(t1);
SamplerState baseColorSampler : register(s1);

struct PSInput {
  [[vk::location(0)]] float3 Color : COLOR0;
  [[vk::location(1)]] float2 UV : TEXCOORD0;
};

struct FSOutput {
  float4 Color : SV_TARGET;
};

FSOutput main(PSInput input)
{
  float4 texColor = baseColorTexture.Sample(baseColorSampler, input.UV);
  float3 finalColor = texColor.rgb * input.Color * pushConstants.ColorMultiplier.rgb;
  FSOutput output;
  output.Color = float4(finalColor, texColor.a);
  return output;
}