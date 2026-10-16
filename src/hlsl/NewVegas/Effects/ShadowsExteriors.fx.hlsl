// Image space shadows shader for Oblivion Reloaded
# define viewshadows 0

float4 TESR_ReciprocalResolution;
float4 TESR_WaterSettings; //x: water height in the cell, y: water depth darkness, z: is camera underwater
float4 TESR_ShadowData; // x: quality, y: darkness, z: nearmap resolution, w: farmap resolution
float4 TESR_ShadowFade; // x: fading at sunrise/sunset, y:disabled shadows, z: pointlights shadows
float4 TESR_SkyColor;
float4 TESR_SunAmbient;
float4 TESR_SunColor;
float4 TESR_ShadowScreenSpaceData;
float4 TESR_ShadowForwardData; // x: 1 when the forward path is SUPPRESSED
float4 TESR_ShadowSunLight;     // the sun and ambient colours as the object shaders receive them,
float4 TESR_ShadowAmbientLight; // see ShadowsExteriorEffect::UpdateLightColors
float4 TESR_ContactInject;   // x 1 while the sun's contact shadows are applied in the skin's lighting (ShadowsExteriorEffect)
float4 TESR_ContactSkinMask; // x 1 when TESR_SkinScatterBuffer holds this frame's skin (set by ShaderManager)
float4 TESR_ShadowContactDebug; // x: [Shaders.ShadowsExteriors.ScreenSpace] ContactDebug
float4 TESR_PBRData;           // z: LightingScale, w: AmbientScale, as the object shaders apply them
float4 TESR_PBRExtraData;      // y: SkylightingScale
float4 TESR_TerrainData;       // z: LightingScale, w: AmbientScale -- terrain has its own
float4 TESR_TerrainSkyData;    // x: SkylightingScale
float4 TESR_SkyIrradiance[9];  // the sky light the object shaders add, see SkyAmbient.hlsl

// D3DXMACRO from EffectRecord, off [Shaders.ShadowsExteriors.Main] ForwardShadows.
#ifndef FORWARD_SHADOWS
    #define FORWARD_SHADOWS 0
#endif

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = ANISOTROPIC; MIPFILTER = LINEAR; };
sampler2D TESR_PointShadowBuffer : register(s2)  = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_NormalsBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_ShadowAtlas : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
// The skin shaders' second target (effects/SkinScattering.h): a the camera distance where skin was drawn, 0 elsewhere.
sampler2D TESR_SkinScatterBuffer : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

// Skin, as SkinScattering.fx tests it: the skin shaders wrote this pixel's distance and nothing was drawn
// over it since (the same distance, or the brow strip just in front).
bool ContactSkinHere(float2 uv, float distance) {
	float skinDistance = TESR_ContactSkinMask.x > 0.5f ? tex2Dlod(TESR_SkinScatterBuffer, float4(uv, 0.0f, 0.0f)).a : 0.0f;
	float gap = skinDistance - distance;
	return skinDistance > 0.0f && (abs(gap) < distance * 0.002f + 0.02f || (gap > 0.0f && gap < 1.5f));
}


static const float DARKNESS = max(0.0,1-TESR_ShadowData.y);

struct VSOUT
{
	float4 vertPos : POSITION;
	float4 normal : TEXCOORD1;
	float2 UVCoord : TEXCOORD0;
};

struct VSIN
{
	float4 vertPos : POSITION0;
	float2 UVCoord : TEXCOORD0;
};

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Normals.hlsl"
#include "Includes/Shadows.hlsl"
// Same bias as GetSunShadow (SHADOW_NORMAL_BIAS_TEXELS), so the cascade value this composite
// reads back is the one the object shaders applied.
#define CASCADE_NORMAL_BIAS_TEXELS 0.0f
#include "Includes/SunCascades.hlsl"


// Mirrors SkyAmbientRadiance (mode 0) in Shaders/Includes/SkyAmbient.hlsl, encoded the same way.
float3 SkyIrradiance(float3 n) {
    float3 irradiance = TESR_SkyIrradiance[0].rgb
         + TESR_SkyIrradiance[1].rgb * n.y
         + TESR_SkyIrradiance[2].rgb * n.z
         + TESR_SkyIrradiance[3].rgb * n.x
         + TESR_SkyIrradiance[4].rgb * (n.x * n.y)
         + TESR_SkyIrradiance[5].rgb * (n.y * n.z)
         + TESR_SkyIrradiance[6].rgb * (3.0f * n.z * n.z - 1.0f)
         + TESR_SkyIrradiance[7].rgb * (n.x * n.z)
         + TESR_SkyIrradiance[8].rgb * (n.x * n.x - n.y * n.y);
    return sqrt(max(irradiance, 0.0f));
}

VSOUT FrameVS(VSIN IN)
{
	VSOUT OUT = (VSOUT)0.0f;
	OUT.vertPos = IN.vertPos;
	OUT.UVCoord = IN.UVCoord;
	return OUT;
}

/*
 * Load Shadows Buffer and filter water surfaces 
 * returns a shadow value from darkness setting value (full shadow) to 1 (full light)
*/
float4 Shadow(VSOUT IN) : COLOR0
{
	float4 color = tex2D(TESR_RenderedBuffer, IN.UVCoord);
	float2 uv = IN.UVCoord;

	float depth = readDepth(uv);
	float3 camera_vector = toWorld(uv) * depth;
	float uniformDepth = length(camera_vector);
	float4 world_pos = float4(TESR_CameraPosition.xyz + camera_vector, 1.0f);
	float3 world_normal = GetWorldNormal(IN.UVCoord);

	// early out for underwater surface (if camera is underwater and surface to shade is close to water level with normal pointing downward)
	if (TESR_WaterSettings.z == 1 && world_pos.z < (TESR_WaterSettings.x + 2) && world_pos.z > (TESR_WaterSettings.x - 2) && dot(world_normal, float3(0, 0, -1)) > 0.999) return color;

	float2 Shadow = tex2D(TESR_PointShadowBuffer, IN.UVCoord).rg;

#if FORWARD_SHADOWS
	// Skin has the contact shadows in its lighting (TESR_ContactInject, Shaders/Includes/Shadow.hlsl, where the
	// skin scattering softens them): nothing left to take there. Everything else takes them here.
	bool contactInLighting = TESR_ContactInject.x > 0.5f && ContactSkinHere(IN.UVCoord, uniformDepth);
	// With forward shadows running, the object shaders shadow the sun term alone: a surface in
	// shadow keeps its ambient and sky light, and so keeps their colour. The red channel here
	// then holds only the contact shadows, and they get the same treatment, so the two read as
	// one shadow: remove the sun's share of this pixel's light instead of scaling the whole of
	// it by Darkness and tinting it.
	//
	// The share is rebuilt from the terms the object shaders sum -- sun * N.L * LightingScale,
	// weather ambient * AmbientScale, sky light * SkylightingScale -- in the same encoded space
	// they light in, so scaling the pixel by (ambient + sky) / (sun + ambient + sky) is what the
	// forward path would have written. Point light shadows still take the path below.
	//
	// The object shaders have already applied the cascades, so the contact shadow takes its share of
	// what they left, as CS multiplies the two (dirDetailedShadow *= the screen-space shadow): sun
	// visibility goes from the cascade's c to c * k, k being the contact shadow's, and the pixel
	// scales by (ambient + sun * c * k) / (ambient + sun * c). c comes from the same cascade lookup,
	// done here only where a contact shadow exists. Not faded at sunrise and sunset: CS applies it
	// whenever the sun lights the surface.
	[branch] if (!TESR_ShadowForwardData.x && !TESR_ShadowFade.z) {
		float contactVisibility = saturate(Shadow.r);
		// On skin in the lighting (above): nothing to take here, but the debug view still shows the mask.

		// Debug view. The scene in grey, the contact shadow mask in red, and in blue whatever the
		// cascades had already shaded, which the contact shadows must leave alone.
		[branch] if (TESR_ShadowContactDebug.x) {
			float debugCascade = 1.0f;
			[branch] if (TESR_ShadowFade.y) {
				float debugDepth;
				debugCascade = GetLightAmount(reconstructWorldPosition(IN.UVCoord, debugDepth), world_normal);
			}
			float3 debugColor = luma(color.rgb).xxx * 0.6f;
			debugColor = lerp(debugColor, float3(0.1f, 0.2f, 0.9f), (1.0f - debugCascade) * 0.6f);
			debugColor = lerp(debugColor, float3(1.0f, 0.05f, 0.05f), 1.0f - contactVisibility);
			return float4(debugColor, 1.0f);
		}

		if (contactInLighting || contactVisibility >= 1.0f) return float4(color.rgb, 1.0f);   // most of the screen

		float cascadeVisibility = 1.0f;
		[branch] if (TESR_ShadowFade.y) {
			float cascadeDepth;
			cascadeVisibility = GetLightAmount(reconstructWorldPosition(IN.UVCoord, cascadeDepth), world_normal);
			cascadeVisibility = saturate(lerp(cascadeVisibility, 1.0f, TESR_ShadowFade.x));   // faded as GetSunShadow fades it
		}
		float sunTaken = cascadeVisibility * (1.0f - contactVisibility);
		if (sunTaken <= 0.0f) return float4(color.rgb, 1.0f);   // already in the cascades' shade

		// Terrain lights with its own [Shaders.Terrain.*] scales, and nothing in the frame says
		// which pixels are terrain. Contact shadows are mostly seen where things meet the ground,
		// so surfaces facing up take the terrain scales and walls and objects the PBR ones.
		// The terrain constants are only filled while the terrain shaders are enabled.
		float terrainWeight = (TESR_TerrainData.z + TESR_TerrainData.w > 0.0f) ? smoothstep(0.5f, 0.8f, world_normal.z) : 0.0f;
		float3 scales = lerp(float3(TESR_PBRData.z, TESR_PBRData.w, TESR_PBRExtraData.y),
		                     float3(TESR_TerrainData.z, TESR_TerrainData.w, TESR_TerrainSkyData.x), terrainWeight);

		float NdotL = saturate(dot(world_normal, TESR_SmoothedSunDir.xyz));
		float3 sunLight = TESR_ShadowSunLight.rgb * (NdotL * scales.x);
		float3 ambientLight = TESR_ShadowAmbientLight.rgb * scales.y + SkyIrradiance(world_normal) * scales.z;
		float3 litNow = ambientLight + sunLight * cascadeVisibility;

		return float4(color.rgb * (1.0f - sunLight * sunTaken / max(litNow, 0.0001f)), 1.0f);
	}
	// Point light shadows on: this path, but on skin the contact shadows (all the red holds with the
	// forward shadows running) are already in the lighting.
	if (!TESR_ShadowForwardData.x && contactInLighting) Shadow.r = 1.0f;
#endif
	Shadow.r = lerp(TESR_ShadowFade.x, 1.0f, Shadow.r); // fade shadows to light when sun is low

	// scale shadows strength to ambient before adding attenuation for pointlights (ShadowFade.z means point Lights are on)
	float ambient = lerp(1, luma(TESR_SunAmbient), DARKNESS * TESR_ShadowFade.z); // linearise
	Shadow.r = lerp(0, ambient, Shadow.r); //scale brightest areas to the ambient so it can be lit further with attenuation
	Shadow.r += Shadow.g; // Apply poing light attenuation (includes point light shadows)

	Shadow.r = lerp(DARKNESS, 1.0, Shadow.r); 	// brighten shadow value from 0 to darkness from config value

	Shadow.r = saturate(Shadow.r);

#if viewshadows == 1
	return Shadow;
#endif
    color.rgb = pows(color.rgb, 2.2); // linearise
    float4 skyColor = float4(pows(TESR_SkyColor.rgb, 2.2),TESR_SkyColor.w); // linearise
	// tint shadowed areas with Sky color before blending
	float4 colorShadow = luma(color.rgb) * Shadow.r * skyColor;
	colorShadow.rgb = lerp(colorShadow, color * Shadow.r, saturate(Shadow.r + 0.5)).rgb;// bias the transition between the 2 colors to make it less noticeable
    colorShadow.rgb = pows(max(0.0,colorShadow.rgb), 1.0/2.2); // delinearise
	return float4(colorShadow.rgb, 1.0); 
}


technique {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 Shadow();
	}
}
 