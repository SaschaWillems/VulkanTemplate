struct UBO
{
	float4x4 projection;
	float4x4 view;
	float time;
	float2 resolution;
};

cbuffer ubo : register(b0) { UBO ubo; }

struct VSInput
{
[[vk::location(0)]]float3 pos : POSITION0;
[[vk::location(1)]]float3 normal : NORMAL0;
[[vk::location(2)]]float2 uv : TEXCOORD0;
[[vk::location(5)]]float4 color : COLOR0;
};

struct VSOutput
{
	float4 pos : SV_POSITION;
[[vk::location(0)]] float2 uv : TEXCOORD0;
[[vk::location(1)]] float3 normal : NORMAL0;
[[vk::location(2)]] float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;
	float3 pos = input.pos;
	pos *= 1.0 + abs(sin(ubo.time));
	output.pos = mul(ubo.projection, mul(ubo.view, float4(pos.xyz, 1.0)));
    output.uv = input.uv;
	output.normal = input.normal;
	output.color = input.color;
	return output;
}
