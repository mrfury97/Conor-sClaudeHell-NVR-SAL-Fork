// Skin lighting for SkinTemplate.hlsl. Self-contained: needs nothing from any other skin mod.
//
//   - Spherical-Gaussian skin diffusion (as in The Order: 1886, Neubelt & Pettineo): each colour
//     channel's N.L is blurred by a spherical Gaussian whose width grows with the surface's
//     curvature, evaluated with Stephen Hill's fitted SG irradiance (MJP, "SG Series" part 3).
//     The widths are fitted to Penner's pre-integrated skin (2011) for d'Eon's measured
//     diffusion profile; no lookup texture. Curvature comes from screen-space derivatives of
//     the interpolated normal.
//   - Per-channel normal softening for the small-scale detail the curvature cannot see: red uses the
//     blurriest normal, blue the sharpest.
//   - Pre-integrated shadows: light bleeds into the penumbra, red farthest.
//   - Transmission (Barre-Brisebois & Bouchard 2011): red light through ears, nostrils and
//     fingers against the light, using curvature as the thickness.
//   - Dual-lobe GGX specular (Jimenez / Unreal dual specular) at skin's F0 of 0.028.
//   - Spherical-harmonic sky light, softened per channel like direct light, and a dual-lobe sky
//     reflection energy-balanced against it (see SkyAmbient.hlsl).
//   - Linear lighting shared with the object shaders ([Shaders.PBR.Main] LinearLighting), rain
//     wetness and geometric specular AA.
//
// Direct lighting works in whatever space the caller's vectors share (SkinTemplate uses tangent
// space); the sky terms take WORLD-space vectors.
#ifndef SKINLIGHTING_INCLUDED
#define SKINLIGHTING_INCLUDED

#if defined(__INTELLISENSE__)
    #include "PBR.hlsl"
    #include "PBRScale.hlsl"
#else
    #include "Includes/PBR.hlsl"
    #include "Includes/PBRScale.hlsl"
#endif

// c136 and c146 sit either side of SkyAmbient.hlsl's TESR_SkyIrradiance (c137-c145).
float4 TESR_SkinData : register(c136);       // x SpecularStrength, y Roughness, z PerPixelWidth, w Translucency
float4 TESR_SkinExtraData : register(c146);  // x SkyReflectionScale (0 indoors), y wetness, z ShadowScatter, w VanillaMatchedHighlights

// Set per draw by the SkinShader hook (NewVegas/Hooks/Shaders.cpp), not a TESR_ constant: x is 1
// when the skin scattering effect's target is bound for this draw, i.e. the screen-space blur
// will diffuse this skin; y is 1 when the hook has bound the FaceGen maps to a light-only pass
// (SkinTemplate.hlsl, ONLY_LIGHT). The per-pixel curvature diffusion then steps aside: doing both doubled
// the scattering and lit eyelid and lip creases, where the curvature estimate peaks, orange.
float4 SkinScreenSpaceScatter : register(c147);

// [Shaders.Skin.Debug]: x the skin shader's own DebugView (1-7, 0 off or a scattering view),
// y 1 while the Interiors settings are in use. See SkinDebugView in SkinTemplate.hlsl.
float4 TESR_SkinDebugData : register(c152);

#if defined(__INTELLISENSE__)
    #include "MergedLights.hlsl"
#else
    #include "Includes/MergedLights.hlsl"
#endif

#define SKIN_F0 0.028f
#define SKIN_LOBE0_ROUGHNESS 0.75f      // sharp lobe, x the material roughness
#define SKIN_LOBE1_ROUGHNESS 1.3f       // broad lobe
#define SKIN_LOBE_MIX 0.15f             // weight of the broad lobe
#define SKIN_TRANSMIT_TINT float3(1.0f, 0.28f, 0.12f)   // LINEAR colour of light through flesh
#define SKIN_TRANSMIT_DISTORTION 0.3f
#define SKIN_TRANSMIT_POWER 4.0f
#define SKIN_PENUMBRA_TINT float3(0.3f, 0.06f, 0.04f)   // light bled into a shadow edge

// Curvature in 1/mm (one game unit is 1/70 m), capped at 1 (a 1 mm radius), where the
// diffusion fit below was made. ScatterWidth 1 widens d'Eon's profile 2x, which reads right at
// game viewing distances (the measured width barely shows on a head).
#define SKIN_MM_PER_UNIT 14.2857f
#define SKIN_SCATTER_BASE 2.0f

static float skinRoughness = 0.5f;
static float skinSpecScale = 0.0f;
static float skinVanillaMatch = 1.0f;   // direct highlights only; see setupSkin
static float3 skinCompensation = 1.0f;  // 1 / base texture in the AD passes, else 1; the caller applies it
static float skinCurvature = 0.0f;      // 1/mm after ScatterWidth: 0 flat, 1 a 1 mm radius
static float skinThinness = 0.0f;       // the same curvature without ScatterWidth, for transmission

// Call once per pixel at the TOP LEVEL of the shader: it takes derivatives.
//   normal:       shading normal (tangent space in SkinTemplate; only its derivatives are used)
//   vertexNormal: interpolated geometric normal (world), for the curvature
//   worldPos:     camera-relative world position
//   mask:         the normal map's alpha, the specular mask
//   adAlbedo:     the base texture in the AD passes, whose output the engine multiplies by it
//                 afterwards (the specular is divided by it in advance); 1 elsewhere
void setupSkin(float3 normal, float3 vertexNormal, float3 worldPos, float mask, float3 adAlbedo) {
    linearLighting = TESR_PBRExtraData.w > 0.0f;

    float wet = saturate(TESR_SkinExtraData.y);
    float roughness = lerp(TESR_SkinData.y, TESR_SkinData.y * 0.55f, wet);   // a water film is smoother than skin
    skinRoughness = clamp(SpecularAA(normal, roughness), 0.08f, 1.0f);

    // SpecularStrength 1 reads a 50% mask as real skin at F0 0.028.
    skinSpecScale = mask * 2.0f * TESR_SkinData.x * (1.0f + wet);

    // Vanilla-matched highlights ([Shaders.Skin.Main] VanillaMatchedHighlights), as in
    // Object.hlsl: vanilla lit a face's specular mask with its own Blinn-Phong pass, peaking at
    // mask x the light. The two GGX lobes at skin's F0 peak at F0 / (4 alpha^2) each; this scales
    // the direct highlight so their mixed peak reaches the mask, keeping the PBR shape. Matched at
    // the Roughness setting, before rain smooths it (so wet skin still shines more) and before
    // SpecularAA. 1x at the default 0.45, up to the 12x cap for very rough settings.
    float alpha0 = pow(clamp(TESR_SkinData.y * SKIN_LOBE0_ROUGHNESS, 0.04f, 1.0f), 2.0f);
    float alpha1 = pow(clamp(TESR_SkinData.y * SKIN_LOBE1_ROUGHNESS, 0.04f, 1.0f), 2.0f);
    float peak = lerp(SKIN_F0 / (4.0f * alpha0 * alpha0), SKIN_F0 / (4.0f * alpha1 * alpha1), SKIN_LOBE_MIX);
    float matched = clamp(1.0f / (2.0f * peak), 1.0f, 12.0f);   // the 2: skinSpecScale reads a 50% mask as 1
    skinVanillaMatch = lerp(1.0f, matched, saturate(TESR_SkinExtraData.w));
    skinCompensation = 1.0f / max(decodeColor(adAlbedo), 0.125f);

    // How fast the normal turns per millimetre of surface (Penner).
    float dN = length(fwidth(vertexNormal));
    float dP = length(fwidth(worldPos)) * SKIN_MM_PER_UNIT;
    float curvature = dN / max(dP, 1e-4f);
    skinThinness = saturate(curvature * SKIN_SCATTER_BASE);
    skinCurvature = SkinScreenSpaceScatter.x > 0.0f ? 0.0f : saturate(curvature * SKIN_SCATTER_BASE * TESR_SkinData.z);
}

float3 skinLinearTint(float3 c) { return linearLighting ? c : sqrt(c); }

// Stephen Hill's fit to the irradiance of a normalised spherical Gaussian of sharpness lambda,
// per channel: N.L blurred by the lobe (MJP, "SG Series" part 3). Plain saturate(N.L) as
// lambda grows; light wraps past the terminator and the peak spreads as it shrinks.
float3 SGDiffuse(float3 NdotL, float3 lambda) {
    const float c0 = 0.36f;
    const float c1 = 1.0f / (4.0f * c0);
    float3 eml = exp(-lambda);
    float3 em2l = eml * eml;
    float3 rl = 1.0f / lambda;
    float3 scale = 1.0f + 2.0f * em2l - rl;
    float3 bias = (eml - em2l) * rl - em2l;
    float3 x = sqrt(max(1.0f - scale, 1e-8f));
    float3 x0 = c0 * NdotL;
    float3 x1 = c1 * x;
    float3 n = x0 + x1;
    float3 y = abs(x0) <= x1 ? n * n / x : saturate(NdotL);
    return saturate(scale * y + bias);
}

// Diffusion for one light, along per-channel softened normals. Each channel's lobe sharpness
// comes from the curvature c (1/mm), fitted to Penner's pre-integrated skin for d'Eon's
// profile over N.L -1..1 and radii 1-20 mm: worst error 0.07 red (radii under 2 mm, slightly
// dark), 0.016 green, 0.009 blue.
float3 SkinDiffusion(float3 N, float3 Nsoft, float3 L) {
    float3 normalGreen = normalize(lerp(N, Nsoft, 0.6f));
    float3 normalBlue = normalize(lerp(N, Nsoft, 0.3f));
    float3 NdotL = float3(dot(Nsoft, L), dot(normalGreen, L), dot(normalBlue, L));

    float c = max(skinCurvature, 1e-3f);
    float3 lambda = float3(2.4f * pow(c, -1.5f), 28.0f / (c * c), 71.0f / (c * c));
    return SGDiffuse(NdotL, lambda);
}

// Shadow visibility with light scattered into the penumbra.
float3 SkinShadow(float shadow) {
    float penumbra = shadow * (1.0f - shadow) * 4.0f;
    return saturate(shadow + penumbra * SKIN_PENUMBRA_TINT * TESR_SkinExtraData.z);
}

// Both specular lobes, for a point light (sun = false) or the sun's disc.
float3 SkinSpecular(float3 N, float3 V, float3 L, float3 light, bool sun) {
    float r0 = clamp(skinRoughness * SKIN_LOBE0_ROUGHNESS, 0.04f, 1.0f);
    float r1 = clamp(skinRoughness * SKIN_LOBE1_ROUGHNESS, 0.04f, 1.0f);

    float n0 = 1.0f;
    float n1 = 1.0f;
    float3 L0 = L;
    float3 L1 = L;
    if (sun) {
        L0 = SunSpecularDir(N, V, L, r0, n0);
        L1 = SunSpecularDir(N, V, L, r1, n1);
    }
    float3 lobe0 = SpecularLobe(SKIN_F0.rrr, r0, N, V, L0, light) * n0;
    float3 lobe1 = SpecularLobe(SKIN_F0.rrr, r1, N, V, L1, light) * n1;
    return lerp(lobe0, lobe1, SKIN_LOBE_MIX) * skinSpecScale * skinVanillaMatch;
}

// How far toward the sun the skin template looks a second time into the shadow map, in game units
// (16 = 23 cm): past the far side of a hand or a head, so a point there is lit unless something else
// casts the shadow. See selfShadowEscape below.
#define SKIN_SELF_SHADOW_REACH 16.0f

// One light: returns the diffuse light (with transmission), and the highlight in specular, kept
// apart for the skin scattering effect. lightColor is the engine's GAMMA colour with any gamma-space factor (attenuation,
// the projected actor shadow) multiplied in; shadow is a LINEAR visibility (NVR's sun shadow).
// selfShadowEscape: the sun's visibility SKIN_SELF_SHADOW_REACH toward it (1 for lamps), which says
// whether a shadow is the surface's own (lit out there) or cast by something else (dark there too).
// albedo must already be decoded.
float3 SkinLight(float3 albedo, float3 N, float3 Nsoft, float3 geometricNormal, float3 V, float3 L, float3 lightColor, float shadow, float selfShadowEscape, bool sun, out float3 specular) {
    float3 light = decodeColor(lightColor) * TESR_PBRData.z;
    L = normalize(L);

    float geometricNdotL = dot(geometricNormal, L);

    // Diffusion, split into plain N.L and the light scattering carries beyond it.
    float3 scattered = SkinDiffusion(N, Nsoft, L);
    float3 direct = min(scattered, saturate(dot(N, L)));
    float3 wrapped = scattered - direct;

    // The shadow map also marks the surface's own far side as shadowed, which would cut the
    // scattered wrap off at the geometric terminator. On that far side only (geometric N.L 0.05
    // to -0.1), the WRAPPED part escapes the shadow map; plain N.L always keeps it. Skin facing
    // the light inside another surface's shadow (a neck under the jaw) stays shadowed. So does the
    // terminator itself when the whole surface lies in something else's shadow (hands under a
    // roof): the second look toward the sun (selfShadowEscape) is dark there too. Without it the
    // terminator kept a red band in every cast shadow.
    float selfShadowed = saturate((0.05f - geometricNdotL) / 0.15f);
    selfShadowed = selfShadowed * selfShadowed * (3.0f - 2.0f * selfShadowed) * saturate(TESR_SkinExtraData.z) * saturate(selfShadowEscape);
    float3 shadowColor = SkinShadow(shadow);
    float3 diffuse = albedo * (direct * shadowColor + wrapped * lerp(shadowColor, 1.0f, selfShadowed));

    // Transmission only through thin features (ears, nostrils, fingers: high curvature), and only
    // where the light reaches the far side of the surface (Jimenez: saturate(0.3 - N.L)).
    float thin = smoothstep(0.3f, 0.9f, skinThinness);
    float farSide = saturate(0.3f - geometricNdotL);
    float3 transmitDir = normalize(L + Nsoft * SKIN_TRANSMIT_DISTORTION);
    float back = pow(saturate(dot(V, -transmitDir)), SKIN_TRANSMIT_POWER);
    float3 transmitted = albedo * skinLinearTint(SKIN_TRANSMIT_TINT) * (back * thin * farSide * TESR_SkinData.w * shadow);

    specular = SkinSpecular(N, V, L, light, sun) * shadow;
    return (diffuse + transmitted) * light;
}

// A lamp merged into a light-only pass (Includes/MergedLights.hlsl): SkinLight for a point light
// without the transmission term, which does not fit in ps_3_0's temporaries inside the merged
// loop. For a lamp the rest is the same: with no shadow map, the direct and wrapped parts both
// reach the surface whole.
float3 SkinLampLight(float3 albedo, float3 N, float3 Nsoft, float3 V, float3 L, float3 lightColor, out float3 specular) {
    float3 light = decodeColor(lightColor) * TESR_PBRData.z;
    L = normalize(L);
    specular = SkinSpecular(N, V, L, light, false);
    return albedo * SkinDiffusion(N, Nsoft, L) * light;
}

// Sky reflection off both lobes. Sets skyReflectedFraction, so call it BEFORE SkinAmbient.
float3 SkinSkyReflection(float3 V, float3 geometricNormal, float3 N) {
    float3 r = reflect(-V, N);
    float NdotV = saturate(dot(N, V));

    float horizon = saturate(1.0f + 1.2f * dot(r, geometricNormal));
    horizon *= horizon;

    float r0 = clamp(skinRoughness * SKIN_LOBE0_ROUGHNESS, 0.04f, 1.0f);
    float r1 = clamp(skinRoughness * SKIN_LOBE1_ROUGHNESS, 0.04f, 1.0f);
    float3 radiance = lerp(SkyReflectionRadiance(r, r0), SkyReflectionRadiance(r, r1), SKIN_LOBE_MIX);
    float3 envBRDF = lerp(EnvBRDFApprox(SKIN_F0.rrr, r0, NdotV), EnvBRDFApprox(SKIN_F0.rrr, r1, NdotV), SKIN_LOBE_MIX);

    float3 reflected = envBRDF * horizon * skinSpecScale * TESR_SkinExtraData.x;
    // The share of the sky the ambient must not scatter too (see SkyAmbient.hlsl). Without the
    // AD compensation: the ambient is divided by the same texture afterwards.
    skyReflectedFraction = saturate(reflected);

    // Radiance is linear: encode it for gamma lighting, as getSkyReflection in Object.hlsl does.
    float3 sky = linearLighting ? radiance : sqrt(radiance);
    return sky * reflected;
}

// Weather ambient plus the SH sky, sampled with the sharp and softened normals and blended per
// channel the way direct light is.
float3 SkinAmbient(float3 ambient, float3 albedo, float3 N, float3 Nsoft) {
    float3 flat = decodeColor(ambient) * TESR_PBRData.w;

    float3 sharp = decodeColor(SkyAmbientRadiance(N));
    float3 soft = decodeColor(SkyAmbientRadiance(Nsoft));
    float3 sky = float3(soft.r, lerp(sharp.g, soft.g, 0.6f), lerp(sharp.b, soft.b, 0.3f)) * TESR_PBRExtraData.y;

    return (flat + sky) * albedo * (1.0f - skyReflectedFraction);
}

#endif
