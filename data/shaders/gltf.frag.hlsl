[[vk::binding(0, 1)]]
Texture2D textures[];
[[vk::binding(0, 1)]]
TextureCube cubemaps[];
[[vk::binding(0, 1)]]
SamplerState samplerTexture;

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
    float4x4 model;
    uint textureIndex;
    uint radianceIndex;
    uint irradianceIndex;
};
[[vk::push_constant]] PushConsts pushConsts;

struct VSOutput
{
		float4 pos : SV_POSITION;
[[vk::location(0)]] float2 uv : POSITION0;
[[vk::location(1)]] float3 normal : NORMAL0;
[[vk::location(2)]] float4 color : COLOR0;
[[vk::location(3)]] float3 worldpos : NORMAL1;
};

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max((1.0 - roughness).rrr, F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float4 main(VSOutput input) : SV_TARGET
{
    float4 albedo = textures[pushConsts.textureIndex].Sample(samplerTexture, input.uv);
    //float4 ambient = cubemaps[pushConsts.irradianceIndex].Sample(samplerTexture, input.normal);
    
    float3 V = normalize(-input.worldpos);
    float3 N = normalize(input.normal);
    
    float3 F0 = (0.04).rrr;
    float roughness = 0.5;
    
    float3 kS = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    float3 kD = 1.0 - kS;
    float3 irradiance = cubemaps[pushConsts.irradianceIndex].Sample(samplerTexture, input.normal).rgb;
    //float3 ambient = (0.5f).rrr;
    //float3 diffuse = irradiance * albedo.rgb;
    
    float3 lightPos = float3(0.0, 0.0, 0.0);
    float3 lightDir = normalize(lightPos - input.worldpos);
    float3 diffuse = max(dot(N, lightDir), 0.0);
        
    float ao = 2.5;
    float3 ambient = max(kD * diffuse, 0.1) * ao;       

    return float4((ambient + diffuse) * albedo.rgb, 1.0);
    
}