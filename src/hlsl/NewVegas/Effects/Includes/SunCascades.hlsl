// Sun cascade lookup for screen-space effects: the four cascades of TESR_ShadowAtlas, selected by
// distance and cross-faded, exactly as SunShadows.fx has always done it. Shared so the
// ShadowsExteriors composite can tell how much of a pixel the cascades already shaded.
//
// The including effect must declare the TESR_ShadowAtlas sampler, and include
// Includes/Shadows.hlsl, before this file.
//
// Mirrors GetSunShadow in Shaders/Includes/Shadow.hlsl; keep the two in step.

float4x4 TESR_ShadowCameraToLightTransformNear;
float4x4 TESR_ShadowCameraToLightTransformMiddle;
float4x4 TESR_ShadowCameraToLightTransformFar;
float4x4 TESR_ShadowCameraToLightTransformLod;
float4 TESR_SmoothedSunDir;
float4 TESR_ShadowReceiverOffset; // x: the receiver offset toward the sun (GetSunShadow's TESR_ShadowForwardData.y)
float4 TESR_ShadowFormatData; // x: mode, y: format bits per pixels
float4 TESR_ShadowBlur; // x: 1 / atlas resolution, y: whether the lod cascade was updated
float4 TESR_ShadowNearCenter; // x,y,z: center (world space), w: radius
float4 TESR_ShadowMiddleCenter; // x,y,z: center (world space), w: radius
float4 TESR_ShadowFarCenter; // x,y,z: center (world space), w: radius
float4 TESR_ShadowLodCenter; // x,y,z: center (world space), w: radius

static const float Mode = TESR_ShadowFormatData.x;
static const float FormatBits = TESR_ShadowFormatData.y;

// Normal-offset bias in shadow map TEXELS, not world units -- a fixed world-space offset is
// several texels in the Near cascade and a fraction of one in the Lod cascade. See the note in
// Shaders/Includes/Shadow.hlsl. Keep these three in step with the SHADOW_NORMAL_BIAS_TEXELS /
// SHADOW_SLOPE_BIAS / SHADOW_FILTER_TAPS defaults there, or the two paths disagree.
//
// An effect that has to agree with the forward path pixel for pixel defines
// CASCADE_NORMAL_BIAS_TEXELS as the game shaders' 0 before including this; otherwise an edge
// one path calls lit and the other shaded gets shadowed twice.
#ifndef CASCADE_NORMAL_BIAS_TEXELS
    #define CASCADE_NORMAL_BIAS_TEXELS 2.5f
#endif
static const float NormalBiasTexels = CASCADE_NORMAL_BIAS_TEXELS;
static const float SlopeBias = 1.0f;
#define SHADOW_FILTER_TAPS 1
#define SHADOW_FILTER_SPREAD 1.0f

float4 ScreenCoordToTexCoord(float4 coord){
	// apply perspective (perspective division) and convert from -1/1 to range to 0/1 (shadowMap range);
	coord.xyz /= coord.w;
	coord.x = coord.x * 0.5f + 0.5f;
	coord.y = coord.y * -0.5f + 0.5f;

	return coord;
}

// Moments for one cascade, optionally averaged over several taps.
//
// Averaging the MOMENTS and evaluating Chebyshev once is the correct order -- moments are
// linearly filterable, which is the entire reason variance shadow maps exist. Averaging
// four separate Chebyshev results would be both wrong and slower.
// tex2Dlod, not tex2D: with forward compiled in, the deferred lookup sits inside a dynamic
// branch, and a gradient-taking sample there is illegal (X3528). The atlas has no mipmaps, so
// an explicit LOD 0 is exactly equivalent -- the forward path samples it the same way.
float4 SampleShadowMoments(float2 uv, float2 quadrantOffset) {
#if SHADOW_FILTER_TAPS <= 1
    return tex2Dlod(TESR_ShadowAtlas, float4(uv, 0.0f, 0.0f));
#else
	// Taps must stay inside their own quadrant: the atlas packs four unrelated cascades into
	// one texture, so a tap crossing a quadrant border reads another cascade's depths as if
	// they belonged to this one. ShadowMapBlur.pso clamps for the same reason.
    float texel = TESR_ShadowBlur.x;
    float2 lo = quadrantOffset + texel * 0.5f;
    float2 hi = quadrantOffset + 0.5f - texel * 0.5f;

    float2 d = texel * SHADOW_FILTER_SPREAD;
    float4 m;
    m  = tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2( 1.0f,  0.5f), lo, hi), 0.0f, 0.0f));
    m += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2(-0.5f,  1.0f), lo, hi), 0.0f, 0.0f));
    m += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2(-1.0f, -0.5f), lo, hi), 0.0f, 0.0f));
    m += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2( 0.5f, -1.0f), lo, hi), 0.0f, 0.0f));
    return m * 0.25f;
#endif
}

float GetLightAmountValue(float4x4 lightTransform, float4 coord, float offsetX, float offsetY, float bias, float bleedReduction) {
    float4 LightSpaceCoord = ScreenCoordToTexCoord(mul(coord, lightTransform));

	// Offset to the correct position in the atlas.
    LightSpaceCoord.xy *= 0.5;
    LightSpaceCoord.x += offsetX;
    LightSpaceCoord.y += offsetY;

    float4 shadowBufferValue = SampleShadowMoments(LightSpaceCoord.xy, float2(offsetX, offsetY));

    float shadow;
	
	[branch]
    if (Mode == 0.0f)
        shadow = GetLightAmountValueVSM(shadowBufferValue.xy, LightSpaceCoord.z, bias, bleedReduction);
    else if (Mode == 1.0f)
        shadow = GetLightAmountValueEVSM2(shadowBufferValue.xy, LightSpaceCoord.z, bias, bleedReduction, FormatBits);
	else
        shadow = GetLightAmountValueEVSM4(shadowBufferValue, LightSpaceCoord.z, bias, bleedReduction, FormatBits);
	
    return shadow;
}

float GetLightAmount(float4 positionWS, float3 normal)
{
	// Receiver offset toward the sun, as GetSunShadow (near-flat surfaces meeting don't shade each other).
    positionWS.xyz += TESR_SmoothedSunDir.xyz * TESR_ShadowReceiverOffset.x;

	// Normal offset.
    float NdotL = dot(normal, TESR_SmoothedSunDir.xyz);
    float offsetScale = saturate(1 - NdotL);

	// World size of one shadow map texel, per cascade. GetCascadeViewProj builds each cascade
	// as [-radius, +radius], so a texel is 2*radius/cascadeResolution; the atlas is two
	// cascades wide, so cascadeResolution = 0.5 / TESR_ShadowBlur.x and the texel works out
	// to 4 * radius * TESR_ShadowBlur.x.
    float4 radii = {
        TESR_ShadowNearCenter.w,
        TESR_ShadowMiddleCenter.w,
        TESR_ShadowFarCenter.w,
        TESR_ShadowLodCenter.w,
    };
    float4 texelWorld = 4.0f * radii * max(TESR_ShadowBlur.x, 1.0f / 16384.0f);
    float4 offsetDistance = offsetScale * NormalBiasTexels * texelWorld;

	// Slope-scaled variance floor: a grazing texel spans a long run of receiver depth and
	// needs more slack before Chebyshev calls it occluded.
    float bias = (Mode == 0.0f ? 0.00001f : 0.01f) * (1.0f + SlopeBias * offsetScale);

    const float blend = 0.9f;

	// Each cascade is offset in its OWN texel scale -- one shared samplePos cannot suit all
	// four when their texels differ by more than an order of magnitude.
	float4 shadows = {
        GetLightAmountValue(TESR_ShadowCameraToLightTransformNear,   float4(positionWS.xyz + offsetDistance.x * normal, 1.0f), 0.0, 0.0, bias, 0.1f),
		GetLightAmountValue(TESR_ShadowCameraToLightTransformMiddle, float4(positionWS.xyz + offsetDistance.y * normal, 1.0f), 0.5, 0.0, bias, 0.2f),
		GetLightAmountValue(TESR_ShadowCameraToLightTransformFar,    float4(positionWS.xyz + offsetDistance.z * normal, 1.0f), 0.0, 0.5, bias, 0.6f),
		GetLightAmountValue(TESR_ShadowCameraToLightTransformLod,    float4(positionWS.xyz + offsetDistance.w * normal, 1.0f), 0.5, 0.5, bias, 0.8f),
    };

    float4 distances = {
        length(positionWS.xyz - TESR_ShadowNearCenter.xyz),
		length(positionWS.xyz - TESR_ShadowMiddleCenter.xyz),
		length(positionWS.xyz - TESR_ShadowFarCenter.xyz),
		length(positionWS.xyz - TESR_ShadowLodCenter.xyz),
    };
	
    if (distances.x < TESR_ShadowNearCenter.w) {
        if (distances.x < TESR_ShadowNearCenter.w * blend)
            return shadows.x;
		
        return lerp(shadows.x, shadows.y, smoothstep(TESR_ShadowNearCenter.w * blend, TESR_ShadowNearCenter.w, distances.x));
    }
    else if (distances.y < TESR_ShadowMiddleCenter.w) {
        if (distances.y < TESR_ShadowMiddleCenter.w * blend)
            return shadows.y;
		
        return lerp(shadows.y, shadows.z, smoothstep(TESR_ShadowMiddleCenter.w * blend, TESR_ShadowMiddleCenter.w, distances.y));
    }
    else if (distances.z < TESR_ShadowFarCenter.w) {
        if (distances.z < TESR_ShadowFarCenter.w * blend)
            return shadows.z;
		
        return lerp(shadows.z, shadows.w, smoothstep(TESR_ShadowFarCenter.w * blend, TESR_ShadowFarCenter.w, distances.z));
    }
    else if (distances.w < TESR_ShadowLodCenter.w) {
        if (distances.w < TESR_ShadowLodCenter.w * blend)
            return shadows.w;
		
        return lerp(shadows.w, 1.0f, smoothstep(TESR_ShadowLodCenter.w * blend, TESR_ShadowLodCenter.w, distances.w));
    }
    else {
        return 1.0f;
    }
}
