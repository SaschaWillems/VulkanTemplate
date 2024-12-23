[[vk::binding(0, 0)]]
Texture2D textures[];
[[vk::binding(0, 1)]]
SamplerState samplers[];

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

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = textures[input.textureIndex].Sample(samplers[0], input.uv);
    //float4 color = textures[primitive.spriteIndex].Sample(samplers[0], input.uv);
    if (color.a < 1.0)
    {
        discard;
    }
    return color;    
}