// Image space shadows shader for Oblivion Reloaded

float4x4 TESR_WorldViewProjectionTransform;
float4 TESR_ReciprocalResolution;
float4 TESR_ViewSpaceLightDir;
float4 TESR_ShadowData; // x: quality, y: darkness, z: texel size
float4 TESR_ShadowScreenSpaceData; // x: Enabled, w: intensity
float4 TESR_ShadowContactData; // x: strength
float4 TESR_ShadowContactWeapon; // w: the first-person weapon's depth (nearer is the weapon)
float4 TESR_ShadowContactExtra;  // y: 1 when the weapon casts onto the world
float4 TESR_ShadowContactLight;  // Bend's light coordinate: xy the sun on screen (pixels), z its depth (TESR_DepthBuffer.y), w +1 in front of the camera, -1 behind, 0 off (settling after a change of cell)
float4 TESR_ShadowContactLightDir; // toward the sun in view space: the cascades' smoothed direction the coordinate is made from; w the fade after a transition
float4 TESR_ShadowContactBend;   // x SurfaceThickness, y BilinearThreshold, z ShadowContrast, w samples per ray
// TESR_CameraData (x near, y far) comes with Includes/Depth.hlsl.
float4 TESR_SunAmbient;
float4 TESR_ShadowFade; // x: sunset attenuation, y: shadows maps active, z: point lights shadows active
// Injected as a D3DXMACRO by EffectRecord from [Shaders.ShadowsExteriors.Main] ForwardShadows,
// exactly as it is for the game shaders -- so the two halves cannot disagree.
// 1 = the object/terrain/parallax shaders evaluate the sun cascades themselves, so this
//     effect must not also apply them. 0 = stock deferred behaviour.
#ifndef FORWARD_SHADOWS
    #define FORWARD_SHADOWS 0
#endif
float4 TESR_ShadowForwardData; // x: 1 when the forward path is SUPPRESSED

sampler2D TESR_DepthBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_ShadowAtlas : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_NormalsBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_PointShadowBuffer : register(s3)  = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_NoiseSampler : register(s4) < string ResourceName = "Effects\bluenoise256.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_PointShadowAtlas : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
// The skin shaders' second target (effects/SkinScattering.h): a the camera distance where skin was drawn, 0 elsewhere.
sampler2D TESR_SkinScatterBuffer : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

// Lamps' contact shadows indoors (technique 1). ShaderManager::GetNearbyLights fills the lamps.
float4 TESR_LampContactData;        // x strength (0 off), y lamps that cast (1-2), z fade along the shadow (ContactFade), w ambient grounding (ContactAmbient)
float4 TESR_LampContactBend;        // the lamps' Bend settings, as TESR_ShadowContactBend ([Shaders.ShadowsInteriors.Main])
float4 TESR_LampContactWeapon;      // x the first-person weapon's depth, y 1 when it casts onto the world, z 1 for the debug view, w 1 when TESR_SkinScatterBuffer holds this frame's skin
float4 TESR_ContactLampPosition[24]; // the nearest point lights: xyz world position, w radius (0 past the last); ContactLampsMax
float4 TESR_ContactLampColor[24];    // rgb colour x dimmer, w shadow info: slot * 2 + fade, -1 none (Shaders/Includes/PointShadow.hlsl)
float4 TESR_ContactLampAnchor[24];   // its slot's anchor (world), w the slot's radius
float4 TESR_PointShadowData;         // as Shaders/Includes/PointShadow.hlsl
float4 TESR_PointShadowParams;
float4 TESR_InverseSquare;           // as Shaders/Includes/InverseSquare.hlsl: x 0 off, else the fill light radius; y q, z k, w scale / (1 - k)
float4 TESR_ShadowAmbientLight;      // the ambient and the cell's directional light, as the object shaders get them
float4 TESR_ShadowSunLight;

static const float DARKNESS = 1-TESR_ShadowData.y;


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
#include "Includes/Shadows.hlsl"
#include "Includes/SunCascades.hlsl"
#include "Includes/Normals.hlsl"
#include "Includes/BlurDepth.hlsl"


VSOUT FrameVS(VSIN IN)
{
	VSOUT OUT = (VSOUT)0.0f;
	OUT.vertPos = IN.vertPos;
	OUT.UVCoord = IN.UVCoord;
	return OUT;
}

// returns a semi random float3 between 0 and 1 based on the given seed. (blue noise)
// tailored to return a different value for each uv coord of the screen.
float3 random(float2 seed)
{
	return tex2D(TESR_NoiseSampler, (seed/256 + 0.5) / TESR_ReciprocalResolution.xy).xyz;
}

// --- The sun's contact shadows: Community Shaders' (Bend Studio's screen-space shadows) -----------
//
// CS (features/Screen-Space Shadows) runs Bend Studio's bend_sss_gpu.hlsli as a compute shader, with
// its own settings and defaults, and multiplies the sun's light by the result. D3D9 has no compute
// shaders, so this is the same algorithm per pixel: a ray from the pixel toward the sun's point on
// screen (away from it when the sun is behind the camera), one sample per pixel along its major axis,
// SAMPLE_COUNT of them. Bend's compute version shares those samples across a wavefront in groupshared
// memory; each pixel here reads its own, which costs more reads and gives the same result.
//
// All in non-linear depth, as Bend works: TESR_DepthBuffer.y, near 1 and far 0. Per sample:
//   - two reads, the pixel the ray crosses and its neighbour on the minor axis, and the sample's
//     shadowing depth pushed back toward the neighbour, unless the two differ by more than
//     BilinearThreshold of the depth left to the far plane (an edge: the pixel's own depth);
//   - perspective-corrected by its distance to the light on screen, so every ray is parallel.
// The pixel is shadowed by the sample nearest to lying exactly one SurfaceThickness in front of it,
// the thickness again a fraction of the depth left to the far plane. Four running minimums,
// contrast, and their average, exactly as Bend.
//
// The first-person weapon is not a caster for the world unless ContactWeaponOnWorld: the one thing
// kept from NVR's own contact shadows (otherwise the gun throws a shadow on the ground that moves
// with the camera).

// TESR_DepthBuffer.y is the hardware depth (CombineDepth.fx converts it to view depth and back):
// reversed (TESR_DepthConstants.z, RenderManager::IsReversedDepth), near 1 and far 0, else near 0
// and far 1. Bend's FarDepthValue and z_sign follow it.
static const float BEND_FAR = TESR_DepthConstants.z > 0.5f ? 0.0f : 1.0f;
static const float BEND_Z_SIGN = TESR_DepthConstants.z > 0.5f ? -1.0f : 1.0f;   // near > far: -1

// View depth to TESR_DepthBuffer.y's space: z / w through the projection, whichever way it runs.
float BendDepthOf(float viewZ) {
	float4 clip = mul(float4(0.0f, 0.0f, viewZ, 1.0f), TESR_ProjectionTransform);
	return clip.w != 0.0f ? clip.z / clip.w : 0.0f;
}

// The first-person weapon (nearer than ContactWeaponDepth) does not cast onto the world. Returned as
// the weapon's depth turned so that nearer is larger (BendNearer), or 2 for none; a weapon pixel
// itself takes shadows as anything else.
float BendNearer(float depth) { return BEND_Z_SIGN < 0.0f ? depth : -depth; }
float BendWeaponDepth(float depth0, float weaponDepth, float onWorld) {
	float weaponY = (weaponDepth > 0.0f && onWorld < 0.5f) ? BendNearer(BendDepthOf(weaponDepth)) : 2.0f;
	return BendNearer(depth0) > weaponY ? 2.0f : weaponY;
}

float BendDepth(float2 pixel) {
	// Off screen reads the far depth, as Bend's border sampler does: nothing there casts.
	float2 uv = pixel * TESR_ReciprocalResolution.xy;
	if (any(uv < 0.0f) || any(uv > 1.0f)) return BEND_FAR;
	return tex2Dlod(TESR_DepthBuffer, float4(uv, 0.0f, 0.0f)).y;
}

// One ray sample, as WriteScreenSpaceShadow's read loop makes it: its shadowing depth, perspective
// corrected (1e10 for a sample past the light, or a weapon pixel that does not cast).
float BendSample(float2 position, bool xMajor, float distance, float weaponY, float bilinearThreshold, float lightDepth) {
	float2 readXY = floor(position) + 0.5f;   // USE_HALF_PIXEL_OFFSET
	float minorAxis = xMajor ? position.y : position.x;
	float bias = (frac(minorAxis) - 0.5f) > 0.0f ? 1.0f : -1.0f;
	float2 depths = float2(BendDepth(readXY), BendDepth(readXY + (xMajor ? float2(0.0f, bias) : float2(bias, 0.0f))));
	float thicknessScale = max(abs(BEND_FAR - depths.x), 1e-4f);
	bool pointFilter = abs(depths.x - depths.y) > thicknessScale * bilinearThreshold;
	float shadowing = pointFilter ? depths.x : depths.x + abs(depths.x - depths.y) * BEND_Z_SIGN;
	bool weapon = BendNearer(depths.x) > weaponY;
	return (distance > 0.0f && !weapon) ? (shadowing - lightDepth) / distance : 1e10f;   // the ray's own light (the sun's or a lamp's)
}

float BendVisibility(float2 pixel, float depth0, float4 light, float weaponY, float4 bend);

float4 ScreenSpaceShadow(VSOUT IN) : COLOR0
{
	// Result in the red channel; point light attenuation passes through in green.
	float2 uv = IN.UVCoord;
	float4 color = tex2D(TESR_PointShadowBuffer, uv);
	if (!TESR_ShadowScreenSpaceData.x || TESR_ShadowContactData.x <= 0.0f || TESR_ShadowContactLight.w == 0.0f) return float4(1.0, color.g, 0, 1);

	float2 resolution = 1.0f / TESR_ReciprocalResolution.xy;
	float2 pixel = floor(uv * resolution) + 0.5f;
	float depth0 = tex2Dlod(TESR_DepthBuffer, float4(pixel * TESR_ReciprocalResolution.xy, 0.0f, 0.0f)).y;
	if (depth0 == BEND_FAR || depth0 <= 0.0f || depth0 >= 1.0f) return float4(1.0, color.g, 0, 1);   // sky, or the planes

	// CS shadows the sun only where it lights the surface (dirLightAngle >= 0): nothing to take away
	// elsewhere, and the composite would scale it by a sun share of zero anyway. View-space normals,
	// against the same (smoothed) direction the rays take.
	float3 viewNormal = tex2Dlod(TESR_NormalsBuffer, float4(uv, 0.0f, 0.0f)).xyz * 2.0f - 1.0f;
	if (dot(viewNormal, TESR_ShadowContactLightDir.xyz) < 0.0f) return float4(1.0, color.g, 0, 1);

	float weaponY = BendWeaponDepth(depth0, TESR_ShadowContactWeapon.w, TESR_ShadowContactExtra.y);

	float visibility = BendVisibility(pixel, depth0, TESR_ShadowContactLight, weaponY, TESR_ShadowContactBend);
	color.r = lerp(1.0f, visibility, saturate(TESR_ShadowContactData.x) * TESR_ShadowContactLightDir.w);   // ContactStrength (CS: always 1), faded in after a transition
	return color;
}

// One Bend ray: how much of the light at the light coordinate (xy on screen in pixels, z its depth in
// TESR_DepthBuffer.y, w +1 in front of the camera, -1 behind) reaches the pixel, 1 all of it. For a
// directional light the coordinate is a direction's (the sun's), for a point light its position's
// (bend_sss_cpu.h: either through the view and projection); samples past a point light are skipped,
// as Bend skips them. bend: x SurfaceThickness, y BilinearThreshold, z ShadowContrast, w samples.
float BendVisibility(float2 pixel, float depth0, float4 light, float weaponY, float4 bend)
{
	// The ray, as ComputeWavefrontExtents lays it out: integer pixel offsets from the light's pixel,
	// the major axis the longer, the distance along it to the light's exact position.
	float2 lightXY = floor(light.xy) + 0.5f;
	float2 lightFraction = light.xy - lightXY;
	float2 offset = pixel - lightXY;
	bool xMajor = abs(offset.x) > abs(offset.y);
	float majorAxis = xMajor ? offset.x : offset.y;
	float majorFraction = xMajor ? lightFraction.x : lightFraction.y;
	majorFraction = majorAxis > 0.0f ? -majorFraction : majorFraction;
	float distance0 = abs(majorAxis) + majorFraction;
	float direction = -light.w;   // -1: samples walk toward the light
	float2 stepXY = (light.xy - pixel) / max(distance0, 1e-4f) * -direction;

	// The pixel's own depth, nudged away from the far plane (UsePrecisionOffset, as CS sets it).
	float thicknessScale0 = max(abs(BEND_FAR - depth0), 1e-4f);
	float startDepth = lerp(depth0, BEND_FAR, -1.0f / 65535.0f);
	startDepth = (startDepth - light.z) / distance0;
	float depthScale = min(distance0 + direction, 1.0f / bend.x) * distance0 / thicknessScale0;
	startDepth = startDepth * depthScale - BEND_Z_SIGN;

	// Four running minimums of |start - sample|, sample i into component i & 3. One sample a step, the
	// four rotated after it, so each still takes every fourth sample (the count is a multiple of 8, so
	// they end where they began; their average does not care about the order anyway).
	float4 shadowValue = 1.0f;
	float count = bend.w;
	[loop]
	for (int i = 1; i < 255; i++) {   // ps_3_0 loops end at 255; the count is capped at 248
		if (i > count) break;
		float k = (float)i;
		shadowValue.x = min(shadowValue.x, abs(startDepth - BendSample(pixel + stepXY * k, xMajor, distance0 + k * direction, weaponY, bend.y, light.z) * depthScale));
		shadowValue = shadowValue.yzwx;
	}

	// Contrast, then the average of the four.
	float contrast = bend.z;
	shadowValue = saturate(shadowValue * contrast + (1.0f - contrast));
	return dot(shadowValue, 0.25f);
}

// The lamps' ray: BendVisibility's, with the shadow fading along it ([Shaders.ShadowsInteriors.Main]
// ContactFade). Bend's own fade-out (its FadeOutSamples, which CS leaves off) weakens only the last
// samples; here a sample k of the count can take at most 1 - fade x k / count of the light, so the
// shadow is darkest where it leaves its caster and gives out toward the end of the ray (at its end
// with fade 1, half way with 2). Per sample after the contrast, so the fade does not depend on it.
// fade 0 is BendVisibility exactly.
float LampBendVisibility(float2 pixel, float depth0, float4 light, float weaponY, float4 bend, float fade)
{
	float2 lightXY = floor(light.xy) + 0.5f;
	float2 lightFraction = light.xy - lightXY;
	float2 offset = pixel - lightXY;
	bool xMajor = abs(offset.x) > abs(offset.y);
	float majorAxis = xMajor ? offset.x : offset.y;
	float majorFraction = xMajor ? lightFraction.x : lightFraction.y;
	majorFraction = majorAxis > 0.0f ? -majorFraction : majorFraction;
	float distance0 = abs(majorAxis) + majorFraction;
	float direction = -light.w;
	float2 stepXY = (light.xy - pixel) / max(distance0, 1e-4f) * -direction;

	float thicknessScale0 = max(abs(BEND_FAR - depth0), 1e-4f);
	float startDepth = lerp(depth0, BEND_FAR, -1.0f / 65535.0f);
	startDepth = (startDepth - light.z) / distance0;
	float depthScale = min(distance0 + direction, 1.0f / bend.x) * distance0 / thicknessScale0;
	startDepth = startDepth * depthScale - BEND_Z_SIGN;

	float4 shadowValue = 1.0f;
	float count = bend.w;
	float contrast = bend.z;
	float fadeStep = fade / max(count, 1.0f);
	[loop]
	for (int i = 1; i < 255; i++) {
		if (i > count) break;
		float k = (float)i;
		float delta = abs(startDepth - BendSample(pixel + stepXY * k, xMajor, distance0 + k * direction, weaponY, bend.y, light.z) * depthScale);
		float sampleShadow = saturate(delta * contrast + (1.0f - contrast));
		shadowValue.x = min(shadowValue.x, lerp(sampleShadow, 1.0f, saturate(fadeStep * (k - 1.0f))));
		shadowValue = shadowValue.yzwx;
	}
	return dot(shadowValue, 0.25f);
}

// --- Lamps' contact shadows (indoors, with ForwardPointShadows) ---------------------------------
// Bend's ray (as the sun's, BendVisibility) toward the lamp lighting each pixel most, unblurred, at
// any distance. The object shaders already
// took each lamp's cube shadow out of that lamp's own light (Shaders/Includes/PointShadow.hlsl), so
// this cannot simply darken the pixel: it works out the lamps' shares of the pixel's light -- the
// ambient and the cell's light, and each lamp's light at the pixel as the shaders light it (linear:
// squared) -- and takes away the part of each marched lamp's share that its contact shadow covers
// and its cube shadow did not. The result, the fraction of the pixel's light left, goes in red;
// ShadowsInteriors.fx (technique 1) applies it.


float LampLuma(float3 c) {
    return dot(c, float3(0.3f, 0.59f, 0.11f));
}

// Shaders/Includes/PointShadow.hlsl's lookup with one bilinear percentage-closer filter (its
// PointShadowVisibility), for the same result.
void LampAtlasTexel(float slot, float3 dir, out float2 corner, out float2 inTile) {
    float3 a = abs(dir);
    float xMajor = (a.x >= a.y && a.x >= a.z) ? 1.0f : 0.0f;
    float yMajor = (1.0f - xMajor) * (a.y >= a.z ? 1.0f : 0.0f);
    float zMajor = 1.0f - xMajor - yMajor;
    float major = dot(dir, float3(xMajor, yMajor, zMajor));
    float s = major > 0.0f ? 1.0f : -1.0f;
    float sc = xMajor * (-s * dir.z) + yMajor * dir.x + zMajor * (s * dir.x);
    float tc = yMajor * (s * dir.z) - (1.0f - yMajor) * dir.y;
    float tile = slot * 6.0f + yMajor * 2.0f + zMajor * 4.0f + (s > 0.0f ? 0.0f : 1.0f);
    float columns = TESR_PointShadowParams.y;
    float row = floor((tile + 0.5f) / columns);
    float size = TESR_PointShadowData.w;
    inTile = clamp((float2(sc, tc) / abs(major) * 0.5f + 0.5f) * size, 1.0f, size - 1.0f);
    corner = float2(tile - row * columns, row) * size;
}

float LampShadowTexel(float2 c, float distance, float radius) {
    float stored = tex2Dlod(TESR_PointShadowAtlas, float4(c * TESR_PointShadowData.yz, 0.0f, 0.0f)).r;
    float blocked = stored < distance ? 1.0f : 0.0f;
    [flatten] if (TESR_PointShadowParams.x > 0.0f)
        blocked *= saturate(stored * radius / TESR_PointShadowParams.x);
    return blocked;
}

// Skin's lamp shadow maps count only occluders this far in front (SkinTemplate.hlsl, POINT_SHADOW_MIN_GAP:
// keep the two equal); the contact shadows take the rest.
#define LAMP_SKIN_SHADOW_GAP 4.0f

float LampCubeVisibility(float info, float4 anchor, float3 worldPos, float3 normal, float gap) {
    float visibility = 1.0f;
    [branch] if (info >= 0.0f && TESR_PointShadowData.x > 0.0f && anchor.w > 0.0f) {
        float3 toLight = anchor.xyz - worldPos;
        float reach = length(toLight);
        float facing = dot(normal, toLight) / reach;
        toLight -= normal * (sign(facing) * TESR_PointShadowParams.z * reach);
        float distance = length(toLight) / anchor.w;
        [branch] if (distance < 1.0f) {
            float slot = floor(info * 0.5f);
            float cosine = max(abs(facing), 0.2f);
            float tilt = min(sqrt(1.0f - cosine * cosine) / cosine, 4.0f);
            float compared = distance * (1.0f - 2.0f / TESR_PointShadowData.w * (0.5f + 1.5f * tilt)) - gap / anchor.w;
            float2 corner, inTile;
            LampAtlasTexel(slot, toLight * float3(-1.0f, -1.0f, 1.0f), corner, inTile);
            float2 q = corner + inTile;
            float2 b = floor(q - 0.5f) + 0.5f;
            float2 f = q - b;
            float blocked = lerp(lerp(LampShadowTexel(b, compared, anchor.w), LampShadowTexel(b + float2(1.0f, 0.0f), compared, anchor.w), f.x),
                                 lerp(LampShadowTexel(b + float2(0.0f, 1.0f), compared, anchor.w), LampShadowTexel(b + float2(1.0f, 1.0f), compared, anchor.w), f.x), f.y);
            visibility = lerp(1.0f, 1.0f - blocked, (info - slot * 2.0f) * saturate(facing * 4.0f));
        }
    }
    return visibility;
}

// A lamp's contact shadow: the same Bend ray as the sun's (BendVisibility), toward the lamp's
// position on screen -- its light coordinate, the position through the view and projection, as
// bend_sss_cpu.h makes a point light's -- with the interior settings ([Shaders.ShadowsInteriors.Main]
// ContactSampleCount, ContactSurfaceThickness, ContactBilinearThreshold, ContactShadowContrast,
// ContactWeaponDepth, ContactWeaponOnWorld). 1 the lamp is hidden.
float LampContactMarch(float2 pixel, float depth0, float3 lampView, float weaponY) {
    float4 clip = mul(float4(lampView, 1.0f), TESR_ProjectionTransform);
    const float limit = 0.000002f * 64.0f;   // Bend's FP_limit
    float w = clip.w >= 0.0f ? max(clip.w, limit) : min(clip.w, -limit);
    float2 resolution = 1.0f / TESR_ReciprocalResolution.xy;
    float4 light = float4((clip.x / w * 0.5f + 0.5f) * resolution.x, (clip.y / w * -0.5f + 0.5f) * resolution.y,
                          clip.w != 0.0f ? clip.z / clip.w : 0.0f, clip.w > 0.0f ? 1.0f : -1.0f);
    return 1.0f - LampBendVisibility(pixel, depth0, light, weaponY, TESR_LampContactBend, TESR_LampContactData.z);
}

// One marched lamp: how much of its share light it loses to its contact shadow (beyond what its
// cube shadow took), and in shown, its share as the object shaders left it.
float LampContactLoss(float share, float4 lamp, float4 anchor, float info, float2 pixel, float depth0, float weaponY, float3 worldPos, float3 worldNormal, bool skin, inout float shown, out float contactOut) {
    float loss = 0.0f;
    contactOut = 0.0f;
    [branch] if (share > 0.0f) {   // a share is 0 where the surface faces away from the lamp
        float cube = LampCubeVisibility(info, anchor, worldPos, worldNormal, skin ? LAMP_SKIN_SHADOW_GAP : 0.0f);   // skin's maps: far occluders only
        shown -= share * (1.0f - cube);
        float3 lampView = mul(float4(lamp.xyz - TESR_CameraPosition.xyz, 0.0f), TESR_ViewTransform).xyz;
        float contact = LampContactMarch(pixel, depth0, lampView, weaponY) * TESR_LampContactData.x;
        loss = share * max(cube - (1.0f - contact), 0.0f);
        contactOut = contact;
    }
    return loss;
}

float4 LampContactShadow(VSOUT IN) : COLOR0
{
    float2 uv = IN.UVCoord;
    // TESR_PointShadowBuffer as clearShadowsBuffer left it: it is this pass's target (one pass, no
    // ping-pong), so reading it would be undefined.
    float4 color = float4(1.0f, 0.0f, 0.0f, 1.0f);

    if (TESR_LampContactData.x <= 0.0f) return float4(1.0f, color.g, 0.0f, 1.0f);
    float2 pixel = floor(uv / TESR_ReciprocalResolution.xy) + 0.5f;
    float depth0 = tex2Dlod(TESR_DepthBuffer, float4(pixel * TESR_ReciprocalResolution.xy, 0.0f, 0.0f)).y;
    if (depth0 <= 0.0f || depth0 >= 1.0f) return float4(1.0f, color.g, 0.0f, 1.0f);   // sky, or the near plane
    float weaponY = BendWeaponDepth(depth0, TESR_LampContactWeapon.x, TESR_LampContactWeapon.y);
    float4 originClip = float4(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, depth0, 1.0f);
    float4 originView = mul(originClip, TESR_InvProjectionTransform);
    const float3 origin = originView.xyz / originView.w;
    // Skin (SkinTemplate.hlsl, which skips the lamps' shadow maps): where the skin shaders wrote their
    // distance and nothing was drawn over it since (as SkinScattering.fx tests it: the same distance, or
    // the brow strip just in front).
    float skinDistance = TESR_LampContactWeapon.w > 0.5f ? tex2Dlod(TESR_SkinScatterBuffer, float4(uv, 0.0f, 0.0f)).a : 0.0f;
    float skinGap = skinDistance - length(origin);
    bool skin = skinDistance > 0.0f && (abs(skinGap) < length(origin) * 0.002f + 0.02f || (skinGap > 0.0f && skinGap < 1.5f));

    float3 viewNormal = tex2Dlod(TESR_NormalsBuffer, float4(uv, 0.0f, 0.0f)).xyz * 2.0f - 1.0f;
    float3 worldNormal = normalize(mul(TESR_ViewTransform, float4(viewNormal, 0.0f)).xyz);   // GetWorldNormal
    float3 worldPos = mul(float4(origin, 0.0f), TESR_InvViewTransform).xyz + TESR_CameraPosition.xyz;

    // The pixel's light: the ambient and the cell's light (its direction unknown here: half), then
    // every lamp, keeping the two that light it most (ContactLamps: those cast contact shadows) and the
    // third's weight, for the fade below.
    float ambient = LampLuma(TESR_ShadowAmbientLight.rgb);
    float cellLight = LampLuma(TESR_ShadowSunLight.rgb);
    float base = ambient * ambient + cellLight * cellLight * 0.5f;
    float total = base;
    float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;   // the three strongest lamps' weights (share x eye fade)
    float share1 = 0.0f, share2 = 0.0f;      // the two strongest's shares
    float4 l1 = 0.0f, a1 = 0.0f, l2 = 0.0f, a2 = 0.0f;
    float i1 = -1.0f, i2 = -1.0f;
    [loop]
    for (int i = 0; i < 24; i++) {
        float4 lamp = TESR_ContactLampPosition[i];
        if (lamp.w <= 0.0f) break;   // filled from the start
        float3 toLamp = lamp.xyz - worldPos;
        float3 d = toLamp / lamp.w;
        // The lamp's light here, linear, as the object shaders falloff (Shaders/Includes/InverseSquare.hlsl).
        float x = saturate(dot(d, d));
        float luma = LampLuma(TESR_ContactLampColor[i].rgb);
        float falloff = lamp.w < TESR_InverseSquare.x ? max(TESR_InverseSquare.y / (TESR_InverseSquare.y + x) - TESR_InverseSquare.z, 0.0f) * TESR_InverseSquare.w
                                                    : (1.0f - x) * (1.0f - x);
        float share = luma * luma * falloff * saturate(dot(worldNormal, normalize(toLamp)));
        total += share;
        float4 anchor = TESR_ContactLampAnchor[i];
        float info = TESR_ContactLampColor[i].w;
        // A lamp at the eye (the Pip-Boy light) hides the shadows it casts behind their casters, and on
        // the camera plane its position on screen runs off to infinity: lit by, but no contact ray. Faded
        // out between 96 and 48 units from the eye, so it does not pop.
        float weight = share * saturate(length(lamp.xyz - TESR_CameraPosition.xyz) / 48.0f - 1.0f);
        [flatten] if (weight > s1) {
            s3 = s2;  s2 = s1;  share2 = share1;  l2 = l1;  a2 = a1;  i2 = i1;
            s1 = weight;  share1 = share;  l1 = lamp;  a1 = anchor;  i1 = info;
        }
        else if (weight > s2) {
            s3 = s2;
            s2 = weight;  share2 = share;  l2 = lamp;  a2 = anchor;  i2 = info;
        }
        else s3 = max(s3, weight);
    }
    if (s1 <= 0.0f || total <= 0.0f) return float4(1.0f, color.g, 0.0f, 1.0f);
    bool twoLamps = TESR_LampContactData.y > 1.5f && s2 > 0.0f;

    // ContactDebug: the strongest lamp's contact shadow as marched, before any share of the light, in
    // place of the result (ShadowsInteriors.fx shows it in red over the scene in grey).
    [branch] if (TESR_LampContactWeapon.z > 0.5f && TESR_LampContactWeapon.z < 2.5f) {
        float3 debugLampView = mul(float4(l1.xyz - TESR_CameraPosition.xyz, 0.0f), TESR_ViewTransform).xyz;
        // 2: where the strongest lamp lands on screen, as the march takes it -- 0 within 12 pixels of
        // it (a red disc that should sit on the lamp), and its depth against the pixel's in green.
        [branch] if (TESR_LampContactWeapon.z > 1.5f) {
            float4 clip = mul(float4(debugLampView, 1.0f), TESR_ProjectionTransform);
            float2 lampPixel = (clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f) / TESR_ReciprocalResolution.xy;
            float onLamp = (clip.w > 0.0f && length(lampPixel - pixel) < 12.0f) ? 0.0f : 1.0f;
            return float4(onLamp, saturate(BendDepthOf(debugLampView.z) / max(depth0, 1e-6f) * 0.5f), 0.0f, 1.0f);
        }
        return float4(1.0f - LampContactMarch(pixel, depth0, debugLampView, weaponY), color.g, 0.0f, 1.0f);
    }

    // Where lamps light a pixel about equally, which ones are marched changes from one pixel to the next,
    // and with it the rays: a hard seam. Each marched lamp's shadow fades out as the first lamp left out
    // comes level with it (gone when equal, whole at half its light), and by the eye fade above.
    float rival = twoLamps ? s3 : s2;
    float fade1 = saturate(2.0f - 2.0f * rival / s1) * (s1 / share1);
    float fade2 = twoLamps ? saturate(2.0f - 2.0f * s3 / s2) * (s2 / max(share2, 1e-6f)) : 0.0f;

    // One ray per marched lamp: each takes away what of its own light its contact shadow covers.
    float shown = total;
    float contact1, contact2 = 0.0f;
    float loss = LampContactLoss(share1, l1, a1, i1, pixel, depth0, weaponY, worldPos, worldNormal, skin, shown, contact1) * fade1;
    [branch] if (twoLamps)
        loss += LampContactLoss(share2, l2, a2, i2, pixel, depth0, weaponY, worldPos, worldNormal, skin, shown, contact2) * fade2;
    // Grounding (ContactAmbient): where a contact shadow falls, the ambient and the cell's light are taken
    // in that proportion too, so things sit dark on what they stand on even where the lamps are not most
    // of the light. Not physical (a lamp's caster does not block the ambient); what makes them intense.
    loss += base * max(contact1 * fade1, contact2 * fade2) * TESR_LampContactData.w;
    float left = 1.0f - loss / max(shown, total * 0.001f);

    // ContactDebug 3-5, red over the scene in grey (ShadowsInteriors.fx shows 1 - this in red):
    //   3 the light actually taken away (the result)
    //   4 the marched lamps' share of the pixel's light (what a full contact shadow could take, before grounding)
    //   5 the fades: lamps coming level, and the lamp at the eye
    [branch] if (TESR_LampContactWeapon.z > 2.5f) {
        float view = TESR_LampContactWeapon.z < 3.5f ? left
                   : (TESR_LampContactWeapon.z < 4.5f ? 1.0f - (share1 + (twoLamps ? share2 : 0.0f)) / max(total, 1e-6f)
                                                      : 1.0f - fade1);
        return float4(saturate(view), color.g, 0.0f, 1.0f);
    }
    return float4(saturate(left), color.g, 0.0f, 1.0f);
}

// Technique 2: this frame's contact shadow (red, as technique 0 left it) and depth, kept for the lighting
// of the next (Shaders/Includes/Shadow.hlsl, GetContactShadow).
float4 StoreContact(VSOUT IN) : COLOR0
{
	float contact = tex2Dlod(TESR_PointShadowBuffer, float4(IN.UVCoord, 0.0f, 0.0f)).r;
	float depth = tex2Dlod(TESR_DepthBuffer, float4(IN.UVCoord, 0.0f, 0.0f)).y;
	return float4(contact, depth, 0.0f, 1.0f);
}

// returns a shadow value from darkness setting value (full shadow) to 1 (full light)
float4 Shadow(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;

	// Sample Screen Space shadows. Intensity reshapes the point lights' attenuation (green) only: the
	// contact shadows (red) are Bend's as they come, as CS uses them.
	float4 Shadow = tex2D(TESR_PointShadowBuffer, IN.UVCoord);
    Shadow.g = pow(Shadow.g, TESR_ShadowScreenSpaceData.w);

	if (!TESR_ShadowFade.y) return Shadow; // disable shadow maps if ShadowFade.y == 0 (setting for shadow map disabled)

	// Sample shadows from shadowmaps.
	//
	// Skipped when the forward path is doing the cascade lookup: ObjectTemplate.hlsl and
	// friends then apply the result to the sun term alone, which this screen-space composite
	// cannot do -- it can only scale the finished pixel, dimming ambient, emittance and
	// specular along with the sun.
	//
	// Screen-space contact shadows (already in Shadow.r) and point lights (Shadow.g) stay
	// deferred either way; the forward path only takes over the cascade lookup.
	//
	// FORWARD_SHADOWS decides whether the forward code was COMPILED INTO the game shaders;
	// TESR_ShadowForwardData.x decides whether it is RUNNING. When forward is compiled in we
	// must branch at runtime rather than compile this out, so that turning the setting off
	// mid-session hands the cascades back here in the same frame -- game shaders cannot be
	// recompiled at runtime, so a macro alone would leave neither path drawing shadows.
#if FORWARD_SHADOWS
	if (!TESR_ShadowForwardData.x) return Shadow;
#endif

	// Only reached when the cascades are ours, so the position and normal are only paid for
	// then. tex2Dlod for the normal: it is sampled after the dynamic returns above.
    float viewDepth;
    float4 worldPos = reconstructWorldPosition(uv, viewDepth);
	float3 normal = mul(TESR_ViewTransform, float4(tex2Dlod(TESR_NormalsBuffer, float4(uv, 0.0f, 0.0f)).xyz * 2.0f - 1.0f, 1.0f)).xyz;   // GetWorldNormal
	Shadow.r *= GetLightAmount(worldPos, normal); // contact x cascades, as CS multiplies them

	return Shadow;
}


technique {

	// No blur: CS uses Bend's result as it comes.
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 ScreenSpaceShadow();
	}

    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Shadow();
    }

}

// Lamps' contact shadows (indoors): rendered by ShaderManager into TESR_PointShadowBuffer, through
// SunShadowsEffect's alternating targets; ShadowsInteriors.fx technique 1 applies them.
technique {

	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 LampContactShadow();
	}

}

// Technique 2: the contact shadow history (ShadowsExteriorEffect::StoreContactHistory).
technique {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 StoreContact();
	}
}
