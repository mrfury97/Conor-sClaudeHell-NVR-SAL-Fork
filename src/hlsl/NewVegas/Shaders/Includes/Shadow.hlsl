// Forward sun-shadow lookup for GAME shaders (objects, parallax, terrain).
//
// All four cascades live in one 2x2 atlas (TESR_ShadowAtlas).
//
// Mirrors Effects/SunShadows.fx.hlsl. Cascade selection, bias and bleed-reduction constants
// must be changed in both.
//
// TESR_ShadowCameraToLightTransform* consume CAMERA-RELATIVE world space (no
// TESR_CameraPosition added). Build it with GetShadowWorldPos() in the VS.
//
//   VS:  OUT.shadowWorldPos = GetShadowWorldPos(OUT.sPosition);
//   PS:  sunLight *= GetSunShadow(IN.shadowWorldPos, worldNormal);

// ---------------------------------------------------------------------------
// Bound by name via ShaderRecord::CreateCT; registers only decide where the values live.
//
// EVERY constant must stay explicitly pinned. Unpinned, the compiler packs them into gaps
// between the host's declared constants -- registers the engine still writes for the vanilla
// shader -- and overwrites them mid-frame.
//
// c100-c133 is clear in both profiles: heaviest host is TerrainTemplate's PS at c93 and the
// skinned VS Bones[54] ending at c97; ps_3_0 allows 224 constants, vs_3_0 256.
//
// The VS matrices are relocatable: GRASS23x00*.vso indexes InstanceData from c20 well past
// c100.
// ---------------------------------------------------------------------------
#ifndef SHADOW_INVPROJ_REG
    #define SHADOW_INVPROJ_REG c100
#endif
#ifndef SHADOW_INVVIEW_REG
    #define SHADOW_INVVIEW_REG c104
#endif

row_major float4x4 TESR_InvProjectionTransform : register(SHADOW_INVPROJ_REG);
row_major float4x4 TESR_InvViewTransform       : register(SHADOW_INVVIEW_REG);

row_major float4x4 TESR_ShadowCameraToLightTransformNear   : register(c108);
row_major float4x4 TESR_ShadowCameraToLightTransformMiddle : register(c112);
row_major float4x4 TESR_ShadowCameraToLightTransformFar    : register(c116);
row_major float4x4 TESR_ShadowCameraToLightTransformLod    : register(c120);

float4 TESR_ShadowNearCenter   : register(c124); // xyz: centre (camera-relative world), w: radius
float4 TESR_ShadowMiddleCenter : register(c125);
float4 TESR_ShadowFarCenter    : register(c126);
float4 TESR_ShadowLodCenter    : register(c127);

float4 TESR_ShadowData        : register(c128); // y: darkness (z is INTERIOR cube texel size only)
float4 TESR_ShadowFormatData  : register(c129); // x: mode (0 VSM, 1 EVSM2, 2 EVSM4), y: format bits
float4 TESR_ShadowFade        : register(c130); // x: sunrise/sunset fade, y: shadow maps active
float4 TESR_SmoothedSunDir    : register(c131);
float4 TESR_ShadowBlur        : register(c132); // x: 1 / atlas resolution, y: lod cascade updated
float4 TESR_ShadowForwardData : register(c133); // x: 1 when the forward path is SUPPRESSED

// Object templates top out at s7. Override BEFORE including for wider sampler arrays --
// TerrainTemplate's NormalMap[7] spans s7-s13.
#ifndef SHADOW_ATLAS_SAMPLER_REG
    #define SHADOW_ATLAS_SAMPLER_REG s9
#endif

// MUST stay on ONE line, closing brace included. ShaderTextureValue::GetSamplerStateString
// finds "register ( sN )" and reads only to the end of that line. Split, it silently falls
// back to TextureRecord's POINT/WRAP defaults. Game shaders only; Effects parse their own.
sampler2D TESR_ShadowAtlas : register(SHADOW_ATLAS_SAMPLER_REG) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };

// Atlas encoding, compile-time: ps_3_0 would flatten a runtime branch across all three
// variants. Must match [_Shaders.ShadowsExteriors.ShadowMaps] Mode.
//   0 = VSM, 1 = EVSM2, 2 = EVSM4 (toml default)
#ifndef SHADOW_FIXED_MODE
    #define SHADOW_FIXED_MODE 2
#endif

static const float SHADOW_FORMAT = TESR_ShadowFormatData.y;

// Normal-offset bias in SHADOW MAP TEXELS, not world units -- cascade texels span two orders
// of magnitude (~0.15 to ~3 world units at Distance 6000 / Lambda 0.95 / Resolution 2048).
// Must also clear the prefilter kernel: ShadowMapBlur.pso reaches +/-3.23 texels on Near
// (Blur9) and +/-1.33 elsewhere (Blur5).
#ifndef SHADOW_NORMAL_BIAS_TEXELS
    #define SHADOW_NORMAL_BIAS_TEXELS 0.0f
#endif

// Widens the VSM/EVSM minimum variance at grazing incidence, where one texel spans a long run
// of receiver depth. 0 gives a constant bias.
#ifndef SHADOW_SLOPE_BIAS
    #define SHADOW_SLOPE_BIAS 1.0f
#endif

// Taps per cascade, on top of the Gaussian prefilter. 1 matches deferred; 4 costs 12 extra
// fetches per fragment. Above 1, average the MOMENTS and run Chebyshev once -- the bound is
// not linear in the moments.
#ifndef SHADOW_FILTER_TAPS
    #define SHADOW_FILTER_TAPS 1
#endif

// Tap spacing in atlas texels.
#ifndef SHADOW_FILTER_SPREAD
    #define SHADOW_FILTER_SPREAD 1.0f
#endif

// Cross-fade between cascades over the outer 10% of each (a second atlas fetch and light-space
// transform there). 0 takes the nearer cascade alone up to its edge: a step where the resolution
// changes, which grass, all noise and overdraw, hides, and saves the second fetch on every blade in
// those rings.
#ifndef SHADOW_CASCADE_BLEND
    #define SHADOW_CASCADE_BLEND 1
#endif

// VS presence sentinel. NVR replaces many more pixel than vertex shaders (objects ~134 vs 51,
// terrain 29 vs 2) and the game pairs them independently, so an NVR PS often runs against a
// vanilla VS whose interpolator holds undefined data. NVR vertex shaders stamp this; the PS
// checks it before shadowing.
#define SHADOW_VS_SENTINEL 1.0f
#define SHADOW_VS_PRESENT(w) (abs((w) - SHADOW_VS_SENTINEL) < 0.001f)

// D3DXMACRO from ShaderRecord::LoadShader, off [Shaders.ShadowsExteriors.Main]
// ForwardShadows. Compile-time gate; TESR_ShadowForwardData.x is the runtime one.
#ifndef FORWARD_SHADOWS
    #define FORWARD_SHADOWS 0
#endif

// ---------------------------------------------------------------------------
// VSM / EVSM evaluation. Mirrors Effects/Includes/Shadows.hlsl, duplicated because the two
// trees compile independently.
// ---------------------------------------------------------------------------
float ShadowLinstep(float a, float b, float v) {
    return saturate((v - a) / (b - a));
}

float ShadowReduceLightBleeding(float pMax, float amount) {
    return ShadowLinstep(amount, 1.0f, pMax);
}

float ShadowChebyshevUpperBound(float2 moments, float mean, float minVariance, float bleedReduction) {
    float variance = moments.y - (moments.x * moments.x);
    variance = max(variance, minVariance);

    float d = mean - moments.x;
    float pMax = variance / (variance + (d * d));

    pMax = ShadowReduceLightBleeding(pMax, bleedReduction);

    return (mean <= moments.x ? 1.0f : pMax);
}

float2 ShadowEVSMExponents() {
    const float maxExponent = (SHADOW_FORMAT == 0.0f) ? 5.54f : 42.0f;
    return min(float2(40.0f, 5.0f), maxExponent);
}

float2 ShadowWarpDepth(float depth, float2 exponents) {
    depth = 2.0f * depth - 1.0f;
    return float2(exp(exponents.x * depth), -exp(-exponents.y * depth));
}

// ---------------------------------------------------------------------------
// Atlas sampling. The four cascades occupy the four quadrants of one texture:
//   Near (0.0, 0.0)   Middle (0.5, 0.0)
//   Far  (0.0, 0.5)   Lod    (0.5, 0.5)
// ---------------------------------------------------------------------------
// tex2Dlod, not tex2D: gradients inside the dynamic cascade branch are illegal in ps_3_0
// (X3528). The atlas has no mipmaps, so LOD 0 is equivalent.
float4 SampleShadowMoments(float2 uv, float2 quadrantOffset) {
#if SHADOW_FILTER_TAPS <= 1
    return tex2Dlod(TESR_ShadowAtlas, float4(uv, 0.0f, 0.0f));
#else
    // Taps must stay inside their quadrant: crossing a border reads another cascade's depths.
    // Half a texel of inset keeps bilinear off the seam.
    float texel = TESR_ShadowBlur.x;
    float2 lo = quadrantOffset + texel * 0.5f;
    float2 hi = quadrantOffset + 0.5f - texel * 0.5f;

    // Rotated grid: diagonals cover the texel footprint more evenly than axes.
    float2 d = texel * SHADOW_FILTER_SPREAD;
    float4 m;
    m  = tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2( 1.0f,  0.5f), lo, hi), 0.0f, 0.0f));
    m += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2(-0.5f,  1.0f), lo, hi), 0.0f, 0.0f));
    m += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2(-1.0f, -0.5f), lo, hi), 0.0f, 0.0f));
    m += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2( 0.5f, -1.0f), lo, hi), 0.0f, 0.0f));
    return m * 0.25f;
#endif
}

float SampleShadowAtlas(float4 lightSpace, float2 quadrantOffset, float bias, float bleedReduction) {
    lightSpace.xyz /= lightSpace.w;
    lightSpace.x = lightSpace.x * 0.5f + 0.5f;
    lightSpace.y = lightSpace.y * -0.5f + 0.5f;

    // Fold into the correct atlas quadrant.
    lightSpace.xy = lightSpace.xy * 0.5f + quadrantOffset;

    float4 moments = SampleShadowMoments(lightSpace.xy, quadrantOffset);

#if SHADOW_FIXED_MODE == 0
    // VSM
    return ShadowChebyshevUpperBound(moments.xy, lightSpace.z, bias, bleedReduction);
#elif SHADOW_FIXED_MODE == 1
    // EVSM2
    float2 exponents = ShadowEVSMExponents();
    float2 warped = ShadowWarpDepth(lightSpace.z, exponents);
    float2 depthScale = bias * exponents * warped;
    return ShadowChebyshevUpperBound(moments.xy, warped.x, depthScale.x * depthScale.x, bleedReduction);
#else
    // EVSM4
    float2 exponents = ShadowEVSMExponents();
    float2 warped = ShadowWarpDepth(lightSpace.z, exponents);
    float2 depthScale = bias * exponents * warped;
    float2 minVariance = depthScale * depthScale;

    float posContrib = ShadowChebyshevUpperBound(moments.xz, warped.x, minVariance.x, bleedReduction);
    float negContrib = ShadowChebyshevUpperBound(moments.yw, warped.y, minVariance.y, bleedReduction);
    return min(posContrib, negContrib);
#endif
}

// ---------------------------------------------------------------------------
// Camera-relative world position from clip space. Call in the VS and interpolate: it is exact
// under perspective-correct interpolation and costs one interpolator.
// ---------------------------------------------------------------------------
float3 GetShadowWorldPos(float4 clipPos) {
    float4 viewPos = mul(clipPos, TESR_InvProjectionTransform);
    viewPos /= viewPos.w;
    return mul(viewPos, TESR_InvViewTransform).xyz;
}

// ---------------------------------------------------------------------------
// Flat face normal from screen-space derivatives of the interpolated world position. Object
// shaders carry their normal in TANGENT space, so no world normal is available to the bias.
//
// MUST be called from the top level of the pixel shader: ddx/ddy are illegal inside dynamic
// flow control.
// ---------------------------------------------------------------------------
float3 GetShadowGeometricNormal(float3 worldPos) {
    float3 n = normalize(cross(ddx(worldPos), ddy(worldPos)));

    // worldPos is camera-relative, so the camera is at the origin and the outward normal is
    // the one facing it. Independent of winding.
    return n * sign(dot(n, -worldPos));
}

// ---------------------------------------------------------------------------
// 1.0 in full light, towards 0 in shadow. Multiply the SUN term by it, before ambient.
// ---------------------------------------------------------------------------
float GetSunShadow(float3 worldPos, float3 worldNormal) {
    // Nonzero SUPPRESSES the forward path; SunShadows.fx reads the same constant and takes
    // over deferred. A missing constant reads zero and leaves forward running.
    if (TESR_ShadowForwardData.x) return 1.0f;

    // Sun shadows never apply indoors. Dedicated flag, not ShadowFade.y below -- that one is
    // shared with ShadowsInteriors.fx's own darkness calc and means something else indoors
    // (interior point-shadows enabled), so it can't double as this gate. Without this check,
    // an interior object shader would fall through to stale exterior cascade data left over
    // from the last time the player was outside.
    if (!TESR_ShadowFormatData.z) return 1.0f;

    // Shadow maps switched off entirely (setting), or sun below horizon.
    if (!TESR_ShadowFade.y) return 1.0f;

    // ShadowMap.pso's channel layout differs per Mode. Fail unshadowed on a mismatch.
    if (TESR_ShadowFormatData.x != (float)SHADOW_FIXED_MODE) return 1.0f;

    // Push the sample along the normal, scaled by how grazing the sun is.
    float NdotL = dot(worldNormal, TESR_SmoothedSunDir.xyz);
    float offsetScale = saturate(1.0f - NdotL);

    float4 radii = float4(TESR_ShadowNearCenter.w, TESR_ShadowMiddleCenter.w,
                          TESR_ShadowFarCenter.w,  TESR_ShadowLodCenter.w);

    // Texel world size per cascade. GetCascadeViewProj's ortho box is 2*radius across, and the
    // atlas is two cascades wide, so with TESR_ShadowBlur.x = 1/atlasResolution:
    //     cascadeResolution = 0.5 / TESR_ShadowBlur.x
    //     texelWorldSize    = 4 * radius * TESR_ShadowBlur.x
    // The floor keeps a zero constant from producing an infinite offset.
    float4 texelWorld = 4.0f * radii * max(TESR_ShadowBlur.x, 1.0f / 16384.0f);
    float4 offsetDistance = offsetScale * SHADOW_NORMAL_BIAS_TEXELS * texelWorld;

#if SHADOW_FIXED_MODE == 0
    const float baseBias = 0.00001f;
#else
    const float baseBias = 0.01f;
#endif

    // Slope-scaled variance floor: bias drives minVariance, the slack Chebyshev gets before
    // calling a texel occluded.
    float bias = baseBias * (1.0f + SHADOW_SLOPE_BIAS * offsetScale);

    // Fraction of the way to a cascade's border at which the cross-fade into the next begins.
    const float blend = 0.9f;

    // Distance to each cascade centre, cross-faded over the outer 10%. Circular boundaries,
    // matching SunShadows.fx: selecting on the square light-space NDC box steps visibly.
    float4 distances = float4(
        length(worldPos - TESR_ShadowNearCenter.xyz),
        length(worldPos - TESR_ShadowMiddleCenter.xyz),
        length(worldPos - TESR_ShadowFarCenter.xyz),
        length(worldPos - TESR_ShadowLodCenter.xyz));

    // Sample inside the branch, not before it. A pixel needs one cascade, or two in the outer
    // 10% where they cross-fade; sampling all four up front cost four atlas fetches and four
    // matrix transforms on every shadowed pixel, and the forward path pays that per object.
    //
    // Legal here only because SampleShadowAtlas uses tex2Dlod -- a gradient instruction under
    // dynamic flow control is X3528 in ps_3_0. The atlas has no mipmaps, so LOD 0 is exact.
#define SHADOW_TAP_NEAR   SampleShadowAtlas(mul(float4(worldPos + offsetDistance.x * worldNormal, 1.0f), TESR_ShadowCameraToLightTransformNear),   float2(0.0f, 0.0f), bias, 0.1f)
#define SHADOW_TAP_MIDDLE SampleShadowAtlas(mul(float4(worldPos + offsetDistance.y * worldNormal, 1.0f), TESR_ShadowCameraToLightTransformMiddle), float2(0.5f, 0.0f), bias, 0.2f)
#define SHADOW_TAP_FAR    SampleShadowAtlas(mul(float4(worldPos + offsetDistance.z * worldNormal, 1.0f), TESR_ShadowCameraToLightTransformFar),    float2(0.0f, 0.5f), bias, 0.6f)
#define SHADOW_TAP_LOD    SampleShadowAtlas(mul(float4(worldPos + offsetDistance.w * worldNormal, 1.0f), TESR_ShadowCameraToLightTransformLod),    float2(0.5f, 0.5f), bias, 0.8f)

    // Initialised: a point beyond the last cascade falls through every branch.
    float shadow = 1.0f;
    [branch] if (distances.x < radii.x) {
        [branch] if (!SHADOW_CASCADE_BLEND || distances.x < radii.x * blend)
            shadow = SHADOW_TAP_NEAR;
        else
            shadow = lerp(SHADOW_TAP_NEAR, SHADOW_TAP_MIDDLE, smoothstep(radii.x * blend, radii.x, distances.x));
    }
    else if (distances.y < radii.y) {
        [branch] if (!SHADOW_CASCADE_BLEND || distances.y < radii.y * blend)
            shadow = SHADOW_TAP_MIDDLE;
        else
            shadow = lerp(SHADOW_TAP_MIDDLE, SHADOW_TAP_FAR, smoothstep(radii.y * blend, radii.y, distances.y));
    }
    else if (distances.z < radii.z) {
        [branch] if (!SHADOW_CASCADE_BLEND || distances.z < radii.z * blend)
            shadow = SHADOW_TAP_FAR;
        else
            shadow = lerp(SHADOW_TAP_FAR, SHADOW_TAP_LOD, smoothstep(radii.z * blend, radii.z, distances.z));
    }
    else if (distances.w < radii.w) {
        shadow = lerp(SHADOW_TAP_LOD, 1.0f, smoothstep(radii.w * blend, radii.w, distances.w));
    }

#undef SHADOW_TAP_NEAR
#undef SHADOW_TAP_MIDDLE
#undef SHADOW_TAP_FAR
#undef SHADOW_TAP_LOD

    shadow = saturate(shadow);

    // Fade out as the sun approaches the horizon, matching SunShadows.fx.
    shadow = lerp(shadow, 1.0f, saturate(TESR_ShadowFade.x));

    return shadow;
}
