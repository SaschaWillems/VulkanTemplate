/* Copyright (c) 2024, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

struct VSInput
{
    [[vk::location(0)]]float3 pos : POSITION0;
};

struct PushConsts
{
    float4x4 mvp;
};
[[vk::push_constant]] PushConsts primitive;

struct VSOutput
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float3 UVW : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    output.UVW = input.pos;
    output.pos = mul(primitive.mvp, float4(input.pos, 1.0));
    return output;
}