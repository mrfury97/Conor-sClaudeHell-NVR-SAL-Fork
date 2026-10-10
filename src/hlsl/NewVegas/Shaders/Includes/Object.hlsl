#if defined(__INTELLISENSE__)
    #include "Pointlights.hlsl"
    #include "PBR.hlsl"
#else
    #include "includes/Pointlights.hlsl"
    #include "includes/PBR.hlsl"
#endif

#if defined(__INTELLISENSE__)
    #include "SkyAmbient.hlsl"
    #include "BounceLighting.hlsl"
#else
    #include "includes/SkyAmbient.hlsl"
    #include "includes/BounceLighting.hlsl"
#endif

// Object lighting, as Community Shaders does it (docs/pbr-rework-design.md):
//   - materials with an authored _rmaos map get true PBR: the OpenPBR Surface base substrate
//     (PBR.hlsl OpenPBR_*), with CS's ambient occlusion handling
//   - every other material keeps vanilla's shading: Lambert diffuse, vanilla's Blinn-Phong
//     highlight on meshes the game flags as specular, vanilla ambient plus NVR's sky ambient,
//     no reflections
// Both in linear space when LinearLighting is on.

// [Shaders.PBR.*], blended by weather and time (PBRShaders::UpdateConstants).
// Pinned high, past every register the game's own shaders use: these were at c32/c33, inside the
// game's range (terrain's LandSpec[2] is c32-c33), and NVR uploads its constants only when the
// shader changes, so a game write in between left the light scale wrong for the following draws.
// Lamps and highlights went dark in interiors depending on draw order, and so on the view.
float4 TESR_PBRData : register(c148);       // z: light scale, w: ambient scale
float4 TESR_PBRExtraData : register(c149);  // y: skylight strength, w: linear lighting
float4 TESR_PBRSpecularData : register(c151);   // y: 1 outdoors (the sky is the environment), 0 indoors, z: LightSourceSize
float4 TESR_PBRDebugData : register(c153);  // x: DebugView (0 off)

#define LIGHT_SCALE         (TESR_PBRData.z)
#define AMBIENT_SCALE       (TESR_PBRData.w)
#define SKY_AMBIENT_STRENGTH (TESR_PBRExtraData.y)
#define OUTDOORS            (TESR_PBRSpecularData.y > 0.5f)
#define LIGHT_SOURCE_SIZE   (TESR_PBRSpecularData.z)          // [Shaders.PBR.Main] LightSourceSize: point lights' radius in game units, for highlights (directLight)

// Per-object data written for every draw by the per-geometry hooks (NewVegas/Hooks/Shaders.cpp,
// WriteObjectMaterial), not through the TESR_ constant table: x is 1 when the mesh carries the
// engine's Specular flag (BSSP_SPECULAR), y is the engine's specular distance fade for flagged
// meshes (1 otherwise), which vanilla applies to the whole highlight.
float4 ObjectMaterial : register(c150);

// Per draw too (MaterialMaps in Hooks/Shaders.cpp): x is 1 when the material's authored _rmaos map
// is bound to RMAOSMap. Channels, as OpenPBR parameters: R specular_roughness, G base_metalness,
// B ambient occlusion, A specular_weight (1 = the dielectric reflectivity of IOR 1.5, F0 0.04;
// it also scales the metal's Fresnel). Not Community Shaders' A, which stores F0 itself: a CS map
// needs its alpha divided by 0.04.
// y is 1 when the dynamic environment cube (effects/DynamicCubemaps.h) is bound to EnvCubeMap:
// what is around the camera, linear, GGX prefiltered with roughness = mip / 8 (256 px, 9 mips).
float4 MaterialMap : register(c152);
sampler2D RMAOSMap : register(s10);
samplerCUBE EnvCubeMap : register(s11);
#define ENVIRONMENT_CUBE (MaterialMap.y > 0.5f)
#define ENVIRONMENT_CUBE_MIPS 8.0f
// Never sharper than half a level: at mip 0 a near-mirror material on a small curved part stretches
// a few cube texels over itself, and their edges show as blocks.
#define ENVIRONMENT_CUBE_MIN_LOD 0.5f

// --- Light-only passes ---------------------------------------------------------------------
// Meshes lit by several lights or casting projected shadows (actors especially) are drawn in
// two passes: an AD pass with the albedo forced to 1 (ONLY_LIGHT, AD), then a separate texture
// pass that MULTIPLIES the frame by the diffuse texture (BSShaderProperty::AddPass::Texture,
// BSSM_TEXTURE*). The engine still binds the diffuse texture to stage 0 for the AD pass
// (ShadowLightShader::UpdateDiffuseNormalStages), so the highlight this shader adds can be
// divided by it here; the texture pass multiplies it back to its true value, instead of tinting
// it with the albedo.
#if defined(ONLY_LIGHT) && !defined(DIFFUSE) && !defined(SPECULAR)
    #define AD_PASS
#endif

static float3 adCompensation = 1.0f;   // 1 / albedo in AD passes, 1 everywhere else

// albedo: the diffuse texture sample, gamma-encoded, before the AD pass forces it to 1. Call
// after setupMaterial (the divisor is the albedo in the lighting space). Capped at 256x, for texels
// so dark the texture pass multiplies the highlight back by next to nothing anyway.
//
// The cap was 8x (albedo 0.125). Linear albedo is the square of the texture, so that already cut in
// below about 35% grey: dark materials (gloves, gunmetal) came out with roughly half their highlight
// when the game drew them in several passes, and full when it drew them in one. A mesh moving in or
// out of a lamp's range switches between the two (GetRenderPasses_2x, 0xBDF790), so first-person
// weapons and gloves flickered between a dim and a bright highlight. The light-only pass renders to
// the HDR target, so the larger values are kept until the texture pass scales them back down.
void setupADCompensation(float3 albedo) {
#ifdef AD_PASS
    adCompensation = 1.0f / max(decodeColor(albedo), 1.0f / 256.0f);
#endif
}

// 1 in a light-only pass that has taken over its mesh's highlight passes (Includes/MergedLights.hlsl,
// TESR_MergedLightCount.y): it draws the flagged material's highlight itself, divided by the
// texture the texture pass multiplies back in, as the specular-only passes would have.
static bool mergedSpecular = false;

// --- Material ---------------------------------------------------------------------------------
// Per pixel state, set by setupMaterial before any lighting call.
static bool   pbrMaterial = false;      // an authored _rmaos map is bound
static float  pbrRoughness = 1.0f;      // specular_roughness
static float  pbrMetalness = 0.0f;      // base_metalness
static float  pbrAO = 1.0f;
static float  pbrSpecularWeight = 1.0f; // specular_weight
static float  pbrEta = 1.5f;            // the IOR ratio after specular_weight's modulation
static float3 pbrAlbedo = 0.5f;         // base_color: the metal's F0 and multi-bounce AO's albedo, real even in light-only passes

static float  vanillaMask = 0.0f;       // the normal map alpha: vanilla's specular mask
static float  vanillaShine = 30.0f;     // the material's glossiness, vanilla's Blinn-Phong exponent

// Vanilla's glossiness (NiMaterialProperty::m_fShine) as a perceptual roughness, for the
// templates that still pass one around and the debug view. Lighting does not use it.
float getMaterialRoughness(float shine) {
    return clamp(shineToRoughness(shine), 0.04f, 1.0f);
}
float getRoughness(float gloss) {
    return saturate(max(0.043f, 1.0f - gloss));
}
#define DEFAULT_ROUGHNESS 1.0f

// Call once per pixel at the TOP LEVEL of the shader: it samples and takes derivatives.
//   specularMask: the normal map alpha
//   shine:        the material's glossiness (Toggles.z, 30 without one)
//   normalTS:     the normal map's normal, for the authored roughness' specular AA
//   uv:           the texture coordinates (after any parallax offset)
//   albedoGamma:  the diffuse texture sample, gamma-encoded, before an AD pass forces it to 1;
//                 1 where no base texture is bound (DIFFUSE, ONLY_SPECULAR)
void setupMaterial(float specularMask, float shine, float3 normalTS, float2 uv, float3 albedoGamma) {
    linearLighting = TESR_PBRExtraData.w > 0.0f;
    vanillaMask = specularMask;
    vanillaShine = max(shine, 1.0f);

    float4 rmaos = tex2D(RMAOSMap, uv);
    pbrMaterial = MaterialMap.x > 0.5f;

    // [Shaders.PBR.Main] PBRLinearLighting (TESR_PBRExtraData.z): how linearly authored materials
    // are lit, whatever LinearLighting says for the rest. Decode exponent 1 + amount (PBR.hlsl).
    if (pbrMaterial) {
        lightingGamma = 1.0f + saturate(TESR_PBRExtraData.z);
        linearLighting = lightingGamma > 1.001f;
    }
    pbrRoughness = SpecularAA(normalTS, clamp(rmaos.r, 0.04f, 1.0f));
    pbrMetalness = saturate(rmaos.g);
    pbrAO = saturate(rmaos.b);
    pbrSpecularWeight = saturate(rmaos.a);
    pbrEta = OpenPBR_ModulatedIOR(pbrSpecularWeight);
    pbrAlbedo = decodeColor(albedoGamma);
}

// --- Normal-mapped ambient -------------------------------------------------------------------
// The direct lights use the normal map in tangent space, but the sky light needs to know which
// way each normal-mapped pixel faces in the WORLD: up toward the sky or down toward the ground.
// The vertex shader expresses world UP in the normal map's own space, through the engine's own
// tangent frame; dot(normal map, that up) is how much each pixel faces the sky. The ambient
// normal keeps the geometric normal's compass heading and takes its up/down from the normal map.
// valid is 0 under a vanilla vertex shader, where the packed channels are undefined.
float3 getAmbientNormal(float3 normalTS, float3 upTS, float3 geometricNormal, float valid) {
    float mapUp = clamp(dot(normalTS, upTS), -1.0f, 1.0f);
    float up = valid > 0.0f ? mapUp : geometricNormal.z;

    float2 heading = geometricNormal.xy;
    float headingLength = length(heading);
    heading = headingLength > 1e-4f ? heading / headingLength : float2(1.0f, 0.0f);
    return float3(heading * sqrt(saturate(1.0f - up * up)), up);
}

// The normal map's normal in world space, through the smooth world frame the vertex shader sent
// (ObjectTemplate.hlsl, WORLD_FRAME_REG / MERGED_LIGHTS): for reflections, where the ambient
// normal's per-triangle heading shows as facets. mirrored is 1 for a left-handed frame.
float3 getReflectionNormal(float3 N, float3 T, float mirrored, float3 normalTS) {
    T = normalize(T - N * dot(T, N));
    float3 B = cross(N, T) * (mirrored > 0.5f ? -1.0f : 1.0f);
    return normalize(normalTS.x * T + normalTS.y * B + normalTS.z * N);
}

// Up in tangent space with z rebuilt from xy, for the variant with no channel spare for it. Its
// sign is the vertex normal's vertical sign, which the geometric normal shares.
float3 rebuildUpTS(float2 xy, float3 geometricNormal) {
    float z = sqrt(saturate(1.0f - dot(xy, xy)));
    return float3(xy, geometricNormal.z >= 0.0f ? z : -z);
}

// --- Vanilla shading, in gamma space -----------------------------------------------------------
// The parallax shader's vanilla fallback (PBR off) and the merged lamps under it.
float3 getVanillaLightingAtt(float3 lightDir, float att, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float gloss, float glossPower) {
    lightDir = normalize(lightDir);
    viewDir = normalize(viewDir);
    float3 halfwayDir = normalize(lightDir + viewDir);

    float NdotL = shades(normal.xyz, lightDir.xyz);

    #if defined(ONLY_SPECULAR)
        float specStrength = gloss * pow(abs(shades(normal.xyz, halfwayDir.xyz)), glossPower);
        float3 lighting = saturate(((0.2 >= NdotL ? (specStrength * saturate(NdotL + 0.5)) : specStrength) * lightColor.rgb) * att);
    #elif defined(SPECULAR)
        float specStrength = gloss * pow(abs(shades(normal.xyz, halfwayDir.xyz)), glossPower);
        float3 lighting = albedo.rgb * NdotL * lightColor.rgb * att;
        lighting += saturate(((0.2 >= NdotL ? (specStrength * saturate(NdotL + 0.5)) : specStrength) * lightColor.rgb) * att);
    #else
        float3 lighting = albedo.rgb * NdotL * lightColor.rgb * att;
    #endif

    return lighting;
}

float3 getVanillaLighting(float3 lightDir, float radius, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float gloss, float glossPower) {
    return getVanillaLightingAtt(lightDir, vanillaAtt(lightDir, radius), lightColor, viewDir, normal, albedo, gloss, glossPower);
}

// --- Direct light -------------------------------------------------------------------------------
// One light, already decoded and scaled (linear when LinearLighting is on). Which terms a
// variant draws:
//   ONLY_SPECULAR (the engine's highlight passes): the highlight alone. Its light colour already
//     carries the engine's specular distance fade (BSShaderLightingProperty::SetLight1x2x).
//     Authored materials never get here: their highlight passes are muted and the highlight is
//     drawn with the rest (MaterialMaps in Hooks/Shaders.cpp), since these passes have no
//     base_color for a metal's Fresnel.
//   SPECULAR (one full pass): diffuse and highlight.
//   the rest (diffuse-only and light-only): diffuse; the highlight too for authored materials,
//     and for vanilla ones whose highlight passes were merged in. Divided by the texture in the
//     light-only pass, which the texture pass multiplies back in.

// Vanilla's Blinn-Phong highlight, mask x N.H^shine, with its lift below N.L 0.2.
float3 vanillaHighlight(float3 N, float3 L, float3 V, float NdotL, float3 light) {
    float specStrength = vanillaMask * pow(saturate(dot(N, normalize(L + V))), vanillaShine);
    return saturate((NdotL <= 0.2f ? specStrength * saturate(NdotL + 0.5f) : specStrength) * light);
}

// lightDistance is the distance to a point light, 0 for the sun and for lights whose distance the
// variant does not have: those stay infinitely small.
float3 directLight(float3 L, float3 light, float3 V, float3 N, float3 albedo, float lightDistance = 0.0f) {
    L = normalize(L);
    V = normalize(V);
    N = normalize(N);

    [branch] if (pbrMaterial) {
        // Sphere lights (Karis 2013, Real Shading in Unreal Engine 4): the game's lamps are points,
        // which on glossy materials give a pinpoint that sparkles from pixel to pixel. Given a size
        // (LightSourceSize), the highlight is lit from the point of the lamp's sphere closest to the
        // reflection ray -- a bulb-sized highlight -- and its peak lowered by how much the sphere
        // widens the lobe, (alpha / alpha')^2 with alpha' = alpha + radius / (2 distance), so the
        // highlight spreads instead of brightening. The diffuse keeps the lamp's centre.
        float NdotL = dot(N, L);
        float specNorm = 1.0f;
        [branch] if (LIGHT_SOURCE_SIZE > 0.0f && lightDistance > 0.0f) {
            float ratio = saturate(LIGHT_SOURCE_SIZE / lightDistance);   // the radius on the unit sphere around the surface
            float3 R = reflect(-V, N);
            float3 toRay = dot(L, R) * R - L;
            L = normalize(L + toRay * saturate(ratio / max(length(toRay), 1e-4f)));   // from here, the highlight's direction
            float alpha = max(pbrRoughness * pbrRoughness, 1e-3f);
            float widened = alpha / saturate(alpha + 0.5f * ratio);
            specNorm = widened * widened;
        }

        float3 diffuse, specular;
        OpenPBR_DirectLight(N, V, NdotL, L, light, pbrRoughness, pbrMetalness, pbrSpecularWeight, pbrEta, albedo, pbrAlbedo, diffuse, specular);
        specular *= specNorm;
        #if defined(ONLY_SPECULAR)
            return specular;
        #else
            return diffuse + specular * adCompensation;
        #endif
    }

    float NdotL = saturate(dot(N, L));
    #if defined(ONLY_SPECULAR)
        return vanillaHighlight(N, L, V, NdotL, light);
    #elif defined(SPECULAR)
        return albedo * NdotL * light + vanillaHighlight(N, L, V, NdotL, light) * saturate(ObjectMaterial.y);
    #else
        float3 diffuse = albedo * NdotL * light;
        [branch] if (mergedSpecular)
            diffuse += vanillaHighlight(N, L, V, NdotL, light) * saturate(ObjectMaterial.y) * adCompensation;
        return diffuse;
    #endif
}

// lightColor arrives gamma-encoded, as the engine supplies it, with any GAMMA-space factor the
// vanilla pipeline applies already in (projected shadow maps, STBB's 0.85): decodeColor turns
// it linear when linear lighting is on. Point light attenuation is applied inside the decode,
// so lights keep the falloff the game was tuned with. visibility (the forward sun shadow) is a
// real visibility and is applied to the decoded light. roughness is unused (the material state
// holds it); the parameter stays for the templates. lightDistance gives the lamp its size in the
// highlights of authored materials (directLight); 0 leaves it a point. visibility is the lamp's
// shadow (Includes/PointShadow.hlsl), a real visibility applied to the decoded light as the sun's.
float3 getPointLightLightingAtt(float3 lightDir, float att, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float roughness, float lightDistance = 0.0f, float visibility = 1.0f) {
    return directLight(lightDir, decodeColor(lightColor * att) * (LIGHT_SCALE * visibility), viewDir, normal, albedo, lightDistance);
}

// lightDir is the full vector to the lamp here (vanillaAtt takes its length as the distance).
float3 getPointLightLighting(float3 lightDir, float radius, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float roughness, float visibility = 1.0f) {
    return getPointLightLightingAtt(lightDir, vanillaAtt(lightDir, radius), lightColor, viewDir, normal, albedo, roughness, length(lightDir), visibility);
}

float3 getSunLighting(float3 lightDir, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float roughness, float visibility = 1.0f) {
    return directLight(lightDir, decodeColor(lightColor) * visibility * LIGHT_SCALE, viewDir, normal, albedo);
}

// [_Main.Develop.Main], via Debug.cpp UpdateSettings. c135: c132 is TESR_ShadowBlur.
float4 TESR_DebugVar : register(c135);

// --- Ambient ----------------------------------------------------------------------------------
// Vanilla materials: the weather ambient plus NVR's sky light (TESR_SkyIrradiance, the
// counterpart of Community Shaders' Skylighting), times the albedo; no reflections.
//
// Authored materials: the OpenPBR substrate's environment lobes (OpenPBR_EnvironmentWeights, split
// sum with energy compensation). The diffuse ambient is the irradiance x base_color x the share
// the dielectric's reflection leaves x CS's multi-bounce AO; the reflection is the environment x
// the specular lobe x CS's specular occlusion. The environment is the dynamic cubemap built from
// the screen (as CS's Dynamic Cubemaps; it falls back to the sky and the room's light where
// nothing was seen). Without it, the sky outdoors and the room's ambient light indoors: a metal
// reflecting nothing would go black.
//
// The templates call getObjectSkyReflection first, then getAmbientLighting.

static float3 pbrSpecularLobe = 0.0f;   // set by getObjectSkyReflection, used by getAmbientLighting
static float  pbrDiffuseShare = 1.0f;
static float  pbrSpecularOcclusion = 1.0f;

// Light-accumulation and highlight-only variants draw no ambient.
#if (defined(ONLY_LIGHT) && !defined(AD_PASS)) || defined(ONLY_SPECULAR)
    #define NO_AMBIENT
#endif

// worldPos is camera-relative, as GetShadowWorldPos builds it; normal is the world normal from
// getAmbientNormal. valid is 0 under a vanilla vertex shader.
float3 getObjectSkyReflection(float3 worldPos, float3 geometricNormal, float3 normal, float materialRoughness, float valid) {
#ifdef NO_AMBIENT
    return 0.0f;
#else
    [branch] if (!pbrMaterial || valid <= 0.0f)
        return 0.0f;

    float3 V = -normalize(worldPos);
    float NdotV = saturate(dot(normal, V));
    // The substrate's environment lobes: the reflection's weight and the share the diffuse keeps.
    float envRough = pbrRoughness;
    OpenPBR_EnvironmentWeights(NdotV, pbrRoughness, pbrMetalness, pbrSpecularWeight, pbrEta, pbrAlbedo, pbrSpecularLobe, pbrDiffuseShare);
    pbrSpecularOcclusion = CS_SpecularOcclusion(NdotV, pbrRoughness * pbrRoughness, pbrAO);

    [branch] if (!OUTDOORS && !ENVIRONMENT_CUBE)
        return 0.0f;   // indoors without the cube the environment is the ambient light: getAmbientLighting

    // A normal map can tilt the reflection below the real surface, where it would see the inside
    // of the object: fade it out as it crosses the geometric horizon.
    float3 r = reflect(-V, normal);
    float horizon = saturate(1.0f + 1.2f * dot(r, geometricNormal));
    float3 radiance;   // linear
    [branch] if (ENVIRONMENT_CUBE)
        radiance = texCUBElod(EnvCubeMap, float4(r, max(envRough * ENVIRONMENT_CUBE_MIPS, ENVIRONMENT_CUBE_MIN_LOD))).rgb;
    else
        radiance = SkyReflectionRadiance(r, envRough);
    // Into the lighting space: the radiance is gamma 2 linear, the lighting space gamma lightingGamma.
    float3 environment = !linearLighting ? sqrt(radiance) : (lightingGamma == 2.0f ? radiance : pow(max(radiance, 0.0f), 0.5f * lightingGamma));
    return environment * pbrSpecularLobe * (pbrSpecularOcclusion * horizon * horizon) * adCompensation;
#endif
}

float3 getAmbientLighting(float3 ambient, float3 albedo, float3 worldNormal, float worldNormalValid) {
    float3 flatAmbient = decodeColor(ambient) * AMBIENT_SCALE;

    // SkylightingScale is the sky's only strength knob: a second, independent light source, not a
    // tint on the weather ambient. Decoded on its own, as SkyAmbientRadiance returns it encoded.
    // worldNormalValid is 0 under a vanilla VS, where the carried world position is undefined.
    float3 irradiance = flatAmbient + decodeColor(SkyAmbientRadiance(worldNormal)) * SKY_AMBIENT_STRENGTH * worldNormalValid;

    // Bounce lighting (Includes/BounceLighting.hlsl): indoors, the light the probes round the pixel
    // hold, in place of the flat ambient (Strength), some of which is kept under it (AmbientFloor).
    // Scaled as the ambient it replaces is (AmbientScale), then by Intensity.
    [branch] if (TESR_ProbeLighting.x > 0.0f && probeWorldPosValid > 0.0f) {
        float4 probe = ProbeLookup(TESR_ProbeAtlasA, TESR_ProbeAtlasB, TESR_ProbeGrid, TESR_ProbeGridScale, TESR_ProbeGridSize, probeWorldPos, worldNormal);
        float3 bounce = probe.rgb * (TESR_ProbeLighting.w * AMBIENT_SCALE);
        irradiance = lerp(irradiance, bounce + flatAmbient * TESR_ProbeLighting.y, TESR_ProbeLighting.x * probe.a);
        // Debug views, shown through the tonemapping, which leaves real bounce light (a few hundredths)
        // all but black: 1 the bounce light x 32, 2 how far the probes round it are trusted (green:
        // inside the room, red: inside walls or beyond the grid), 3 the bounce against the flat ambient
        // it replaces (black none, mid grey as bright, white far brighter).
        float bounceLum = dot(bounce * probe.a, float3(0.2126f, 0.7152f, 0.0722f));
        float flatLum = dot(flatAmbient, float3(0.2126f, 0.7152f, 0.0722f));
        probeDebugLight = TESR_ProbeLighting.z > 2.5f ? (bounceLum / max(bounceLum + flatLum, 1e-5f)).xxx
                        : (TESR_ProbeLighting.z > 1.5f ? float3(1.0f - probe.a, probe.a, 0.0f) : bounce * probe.a * 32.0f);
        probeDebugSet = 1.0f;
    }

#ifndef NO_AMBIENT
    [branch] if (pbrMaterial) {
        float3 diffuse = irradiance * albedo * pbrDiffuseShare * CS_MultiBounceAO(pbrAlbedo, pbrAO);
        float3 indoorReflection = (OUTDOORS || ENVIRONMENT_CUBE) ? 0.0f : flatAmbient * pbrSpecularLobe * pbrSpecularOcclusion * adCompensation;
        return diffuse + indoorReflection;
    }
#endif
    return irradiance * albedo;
}

float3 getAmbientLighting(float3 ambient, float3 albedo) {
    return getAmbientLighting(ambient, albedo, float3(0.0f, 0.0f, 1.0f), 0.0f);
}

// --- Material debug view ([Shaders.PBR.Main] DebugView) -------------------------------------
// 1 roughness (authored; vanilla materials show their glossiness as roughness), 2 metalness,
// 3 flags (red: the engine's Specular flag, green: an authored _rmaos map, blue: its specular
// distance fade), 4 ambient normal (how much each pixel faces the sky: white up, black down),
// 5 the dynamic environment cube along the normal, sharp (authored materials; black without it).
float3 getMaterialDebug(float mode, float roughness, float3 ambientNormal) {
    if (mode < 1.5f) return (pbrMaterial ? pbrRoughness : roughness).xxx;
    if (mode < 2.5f) return (pbrMaterial ? pbrMetalness : 0.0f).xxx;
    if (mode < 3.5f) return float3(ObjectMaterial.x, pbrMaterial ? 1.0f : 0.0f, saturate(ObjectMaterial.y));
    if (mode < 4.5f) return (ambientNormal.z * 0.5f + 0.5f).xxx;
    return (pbrMaterial && ENVIRONMENT_CUBE) ? sqrt(texCUBElod(EnvCubeMap, float4(ambientNormal, 0.0f)).rgb) : 0.0f;
}
