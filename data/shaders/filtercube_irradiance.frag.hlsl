/* Copyright (c) 2018-2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

// Generates an irradiance cube from an environment map using convolution

struct VSOutput
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float3 UVW : TEXCOORD0;
};

[[vk::binding(0, 0)]] TextureCube textureCubeMap;
[[vk::binding(0, 0)]] SamplerState samplerCubeMap;

struct PushConsts
{
    [[vk::offset(64)]] float deltaPhi;
    [[vk::offset(68)]] float deltaTheta;
};
[[vk::push_constant]] PushConsts consts;

#define PI 3.1415926535897932384626433832795

float4 main(VSOutput input) : SV_TARGET
{
	float3 N = normalize(input.pos.xyz);
	float3 up = float3(0.0, 1.0, 0.0);
	float3 right = normalize(cross(up, N));
	up = cross(N, right);

	const float TWO_PI = PI * 2.0;
	const float HALF_PI = PI * 0.5;

	float3 color = float3(0.0, 0.0, 0.0);
	uint sampleCount = 0u;
	for (float phi = 0.0; phi < TWO_PI; phi += consts.deltaPhi) {
		for (float theta = 0.0; theta < HALF_PI; theta += consts.deltaTheta) {
			float3 tempVec = cos(phi) * right + sin(phi) * up;
			float3 sampleVector = cos(theta) * N + sin(theta) * tempVec;
            color += textureCubeMap.Sample(samplerCubeMap, input.UVW).rgb * cos(theta) * sin(theta);		
			sampleCount++;
		}
	}
	return float4(PI * color / float(sampleCount), 1.0);
}
