float4 main([[vk::location(0)]] float2 inUV : TEXCOORD0) : SV_TARGET
{
  return float4(inUV, 0.0, 0.0);
}