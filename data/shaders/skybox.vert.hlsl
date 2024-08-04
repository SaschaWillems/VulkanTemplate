struct UBO
{
	float4x4 projection;
	float4x4 model;
};
[[vk::binding(0, 0)]]
ConstantBuffer<UBO> ubo : register(b0);

struct VSOutput
{
	float4 Pos : SV_POSITION;
[[vk::location(0)]] float3 UVW : TEXCOORD0;
};

VSOutput main([[vk::location(0)]] float3 Pos : POSITION0)
{
	VSOutput output = (VSOutput)0;
	output.UVW = Pos;
	// Convert cubemap coordinates into Vulkan coordinate space
	output.UVW.xy *= 1.0;
	// Remove translation from view matrix
	float4x4 viewMat = ubo.model;
	viewMat[0][3] = 0.0;
	viewMat[1][3] = 0.0;
	viewMat[2][3] = 0.0;
	output.Pos = mul(ubo.projection, mul(viewMat, float4(Pos.xyz, 1.0)));
	return output;
}
