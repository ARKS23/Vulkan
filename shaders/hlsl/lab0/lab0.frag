struct PushConstants {
  float4 ColorMultiplier;
};

[[vk::push_constant]] PushConstants pushConstants;

float4 main([[vk::location(0)]] float3 Color : COLOR0) : SV_TARGET
{
  return float4(Color * pushConstants.ColorMultiplier.rgb, 1.0);
}