[[vk::binding(0, 1)]]
Texture2D textures[];
[[vk::binding(0, 1)]]
SamplerState samplerTexture;

struct UBO
{
	float4x4 projection;
	float4x4 view;
	float time;
	float2 resolution;
};
[[vk::binding(0, 0)]]
ConstantBuffer<UBO> ubo;

struct PushConsts
{
    float4x4 model;
    uint textureIndex;
};
[[vk::push_constant]] PushConsts pushConsts;

struct VSOutput
{
		float4 pos : SV_POSITION;
[[vk::location(0)]] float2 uv : POSITION0;
[[vk::location(1)]] float3 normal : NORMAL0;
[[vk::location(2)]] float4 color : COLOR0;
[[vk::location(3)]] float3 localpos : NORMAL1;
};

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = textures[pushConsts.textureIndex].Sample(samplerTexture, input.uv);
    return color;
}