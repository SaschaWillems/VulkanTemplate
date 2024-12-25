struct UBO
{
	float4x4 projection;
	float4x4 view;
	float time;
	float2 resolution;
};
[[vk::binding(0, 2)]]
ConstantBuffer<UBO> ubo : register(b0,space2);

struct VSInput
{
    [[vk::location(0)]]float3 pos : POSITION0;
    [[vk::location(1)]]float2 uv : TEXCOORD0;
    // Instanced attributes
    [[vk::location(2)]] float3 instancePos : POSITION1;
    [[vk::location(3)]] float instanceScale: POSITION2;
    [[vk::location(4)]] int instanceTextureIndex : TEXCOORD3;
};

struct PushConsts {
    uint spriteIndex;
};
[[vk::push_constant]] PushConsts primitive;

struct VSOutput
{
	float4 pos : SV_POSITION;
[[vk::location(0)]] float2 uv : TEXCOORD0;
[[vk::location(1)]] float4 color : COLOR0;
[[vk::location(2)]] nointerpolation int textureIndex : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    float3 locPos = input.pos * input.instanceScale;
    output.pos = mul(ubo.view, mul(ubo.projection, float4(locPos + input.instancePos, 1.0)));
    output.uv = input.uv;
    output.textureIndex = input.instanceTextureIndex;
    return output;
}
