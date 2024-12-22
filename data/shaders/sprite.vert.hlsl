//struct UBO
//{
//	float4x4 projection;
//	float4x4 view;
//	float time;
//	float2 resolution;
//};

//cbuffer ubo : register(b0) { UBO ubo; }

//struct VSInput
//{
//[[vk::location(0)]]float3 pos : POSITION0;
//[[vk::location(1)]]float3 normal : NORMAL0;
//[[vk::location(2)]]float2 uv : TEXCOORD0;
//[[vk::location(6)]]float4 color : COLOR0;
//};

struct PushConsts {
    uint spriteIndex;
};
[[vk::push_constant]] PushConsts primitive;

struct VSOutput
{
	float4 pos : SV_POSITION;
[[vk::location(0)]] float2 uv : TEXCOORD0;
[[vk::location(1)]] float4 color : COLOR0;
};

//VSOutput main(VSInput input)
VSOutput main(uint VertexIndex : SV_VertexID)
{
    VSOutput output = (VSOutput) 0;
    output.uv = float2((VertexIndex << 1) & 2, VertexIndex & 2);
    output.pos = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
    return output;
}
