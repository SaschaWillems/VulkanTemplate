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
[[vk::location(6)]]float4 color : COLOR0;
};

struct PushConsts {
	float4x4 model;
};
[[vk::push_constant]] PushConsts primitive;

struct VSOutput
{
	float4 pos : SV_POSITION;
[[vk::location(0)]] float2 uv : TEXCOORD0;
[[vk::location(1)]] float3 normal : NORMAL0;
[[vk::location(2)]] float4 color : COLOR0;
[[vk::location(3)]] float3 worldpos : NORMAL1;
};

VSOutput main(VSInput input)
{
	VSOutput output = (VSOutput)0;
    output.worldpos = mul(primitive.model, float4(input.pos, 1.0)).xyz;
	float4x4 modelView = mul(ubo.view, primitive.model);
	output.pos = mul(ubo.projection, mul(modelView, float4(input.pos, 1.0)));
    output.uv = input.uv;
	// Note: Only works with uniform scaling
    output.normal = mul(primitive.model, input.normal);
	output.color = input.color;
	return output;
}
