[[vk::binding(0, 1)]]
TextureCube textures[];
[[vk::binding(0, 1)]]
SamplerState samplerCubeMap;

struct PushConsts
{
    float4x4 model;
    uint textureIndex;
};
[[vk::push_constant]] PushConsts pushConsts;

float4 main([[vk::location(0)]] float3 inUVW : TEXCOORD0) : SV_TARGET
{
    float4 color = textures[pushConsts.textureIndex].Sample(samplerCubeMap, inUVW);
    return color;
}