// struct PushConsts {
// 	float4x4 model;
// };
// [[vk::push_constant]] PushConsts primitive;

struct VSOutput
{
[[vk::location(0)]]float2 uv : POSITION0;
[[vk::location(1)]]float3 normal : NORMAL0;
[[vk::location(2)]]float4 color : COLOR0;
};

float4 main(VSOutput input) : SV_TARGET
{
	float4 color = input.color;
	float r = step(frac(input.normal.y * 2.02), 0.5);
	color.rgb = input.normal * r;
    return float4(color.rgb, 1.0);
}