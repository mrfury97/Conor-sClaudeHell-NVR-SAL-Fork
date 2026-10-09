// Shader To compute a shadow pass for point light shadows (Only for 6 lights)

float4 TESR_ShadowLightPosition[12];
float4 TESR_ShadowLightFade[3];   // slot i's shadow strength: [i / 4], component i % 4
float4 TESR_LightPosition[48];   // the point lights without a shadow slot, nearest first (TrackedLightsMax); w 0 past the last
float4 TESR_LightColor[60];      // 0-11 the shadow slots', 12-59 these
float4 TESR_ShadowFade;
float4 TESR_SpotLightPosition;
float4 TESR_SpotLightDirection;
float4 TESR_SpotLightColor;


//sampler_state removed to avoid a artifact. TODO investigate
sampler2D TESR_DepthBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_NormalsBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = NONE; MINFILTER = NONE; MIPFILTER = NONE; };
samplerCUBE TESR_ShadowCubeMapBuffer0 : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer1 : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer2 : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer3 : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer4 : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer5 : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Normals.hlsl"
#include "Includes/Shadows.hlsl"

struct VSOUT
{
	float4 vertPos : POSITION;
	float2 UVCoord : TEXCOORD0;
};

struct VSIN
{
	float4 vertPos : POSITION0;
	float2 UVCoord : TEXCOORD0;
};

VSOUT FrameVS(VSIN IN)
{
	VSOUT OUT = (VSOUT)0.0f;
	OUT.vertPos = IN.vertPos;
	OUT.UVCoord = IN.UVCoord;
	return OUT;
}


float GetSpotLightAmount(float4 worldPos, float4 spotLightPosition, float4 spotLightDirection, float4 normal){
    float3 lightToWorld = spotLightPosition.rgb - worldPos.xyz;
    float3 lightVector = normalize(lightToWorld);

	// radius based attenuation based on https://lisyarus.github.io/blog/graphics/2022/07/30/point-light-attenuation.html
    float radius = spotLightPosition.w;
    float Distance = length(lightToWorld)/radius;
	float s = saturate(Distance * Distance); 
	float atten = saturate(((1 - s) * (1 - s)) / (1 + 5.0 * s));

	float angleCosMax = cos(radians(spotLightDirection.w));
	float angleCosMin = cos(radians(spotLightDirection.w * 0.5));
	float cone = pow(invlerps(angleCosMax, angleCosMin, shades(spotLightDirection.xyz, lightVector * -1)), 2.0);

    float diffuse = shade(lightVector, normal.xyz);
	return diffuse * cone * atten;
}


float4 Shadow( VSOUT IN ) : COLOR0 {

	float2 uv = IN.UVCoord;
	float depth = readDepth(uv);
    float3 camera_vector = toWorld(uv) * depth;
    float4 world_pos = float4(TESR_CameraPosition.xyz + camera_vector, 1.0f);	
	float4 normal = float4(GetWorldNormal(uv), 1);
	// float Shadow = 0.0;

	float Shadow = GetPointLightAmountFaded(TESR_ShadowCubeMapBuffer0, world_pos, TESR_ShadowLightPosition[0], normal, TESR_ShadowLightFade[0].x) * luma(TESR_LightColor[0].rgb) * TESR_LightColor[0].w;
	Shadow += GetPointLightAmountFaded(TESR_ShadowCubeMapBuffer1, world_pos, TESR_ShadowLightPosition[1], normal, TESR_ShadowLightFade[0].y) * luma(TESR_LightColor[1].rgb) * TESR_LightColor[1].w;
	Shadow += GetPointLightAmountFaded(TESR_ShadowCubeMapBuffer2, world_pos, TESR_ShadowLightPosition[2], normal, TESR_ShadowLightFade[0].z) * luma(TESR_LightColor[2].rgb) * TESR_LightColor[2].w;
	Shadow += GetPointLightAmountFaded(TESR_ShadowCubeMapBuffer3, world_pos, TESR_ShadowLightPosition[3], normal, TESR_ShadowLightFade[0].w) * luma(TESR_LightColor[3].rgb) * TESR_LightColor[3].w;
	Shadow += GetPointLightAmountFaded(TESR_ShadowCubeMapBuffer4, world_pos, TESR_ShadowLightPosition[4], normal, TESR_ShadowLightFade[1].x) * luma(TESR_LightColor[4].rgb) * TESR_LightColor[4].w;
	Shadow += GetPointLightAmountFaded(TESR_ShadowCubeMapBuffer5, world_pos, TESR_ShadowLightPosition[5], normal, TESR_ShadowLightFade[1].y) * luma(TESR_LightColor[5].rgb) * TESR_LightColor[5].w;

	Shadow += GetSpotLightAmount(world_pos, TESR_SpotLightPosition, TESR_SpotLightDirection, normal) * luma(TESR_SpotLightColor.rgb) * TESR_SpotLightColor.w;
	
	[loop] for (int i = 0; i < 48; i++){
		if (TESR_LightPosition[i].w <= 0.0f) break;   // the list is filled from the start
		Shadow += GetPointLightContribution(world_pos, TESR_LightPosition[i], normal) * luma(TESR_LightColor[i + 12].rgb) * TESR_LightColor[i + 12].w;
	}

	Shadow = saturate(Shadow);
	
	return float4(Shadow, Shadow, Shadow, 1.0f);
}

technique {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 Shadow();
	}
}
