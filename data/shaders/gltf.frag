// struct PushConsts {
// 	float4x4 model;
// };
// [[vk::push_constant]] PushConsts primitive;

struct UBO
{
	float4x4 projection;
	float4x4 view;
	float time;
	float2 resolution;
};

cbuffer ubo : register(b0) { UBO ubo; }

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
	float r = step(frac(input.pos.y * 2.02), 0.5); // 0.25
	// float3 color = (normalize(input.localpos) * 2.0 + 1.0) * r;
	// float3 color = input.color.rgb * r;
	float3 color = input.color.rgb;
    return float4(color.rgb, 1.0);
}