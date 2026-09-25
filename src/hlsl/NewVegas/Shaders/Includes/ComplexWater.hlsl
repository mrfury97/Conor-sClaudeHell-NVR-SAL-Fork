// Complex Water: the functions ComplexWater.pso.hlsl is built from. See that file for the overview and
// for the constants and samplers these expect to be declared before this is included.

struct PS_INPUT {
    float4 LTEXCOORD_0 : TEXCOORD0_centroid;     // the surface point, camera-relative world position
    float4 LTEXCOORD_1 : TEXCOORD1_centroid;     // the surface point on the water plane, local
    float4 LTEXCOORD_2 : TEXCOORD2_centroid;     // projection to screen texture space, 1st row
    float4 LTEXCOORD_3 : TEXCOORD3_centroid;     // 2nd row
    float4 LTEXCOORD_4 : TEXCOORD4_centroid;     // 3rd row
    float4 LTEXCOORD_5 : TEXCOORD5_centroid;     // 4th row
    float4 LTEXCOORD_6 : TEXCOORD6;              // wading displacement map position
    float2 LTEXCOORD_7 : TEXCOORD7;              // wave texture position
};

struct PS_OUTPUT {
    float4 color_0 : COLOR0;
};

#include "Includes/PBR.hlsl"

// ---------------------------------------------------------------------------------------------
// Settings ([Shaders.Water.ComplexWater], WaterShaders::UpdateSettings). Registers clear of the
// engine's water constants (c0-c13), of the template's own (c14-c72), of Shadow.hlsl (c100-c133)
// and of the scene-depth constants below (c192-c201).
//   TESR_WaterLighting     x: SunShadows      y: AbsorptionDepth  z: WaterColorBrightness  w: WaveScattering
//   TESR_WaterLighting2    x: SpecularAA      y: PointLights      z: SunGlitter            w: DebugView
//   TESR_WaterLighting3    x: Foam            y: FoamWidth        z: ShoreFadeWidth        w: ReflectionBlur
//   TESR_WaterLighting4    x: Caustics        y: CausticsScale    z: 1 outdoors, 0 indoors (per frame)
//   TESR_WaterScatterColor rgb: ScatterColor, w: 1 when set (else the water form's own colours)
//   TESR_WaterAbsorption   rgb: AbsorptionColor, the absorption rate of each colour
// The DLL keeps the ones that must never be 0 (AbsorptionDepth, WaterColorBrightness, FoamWidth,
// CausticsScale) off 0.
// ---------------------------------------------------------------------------------------------
float4 TESR_WaterLighting     : register(c190);
float4 TESR_WaterLighting2    : register(c191);
float4 TESR_WaterLighting3    : register(c202);
float4 TESR_WaterLighting4    : register(c203);
float4 TESR_WaterScatterColor : register(c204);
float4 TESR_WaterAbsorption   : register(c205);

// Water's reflectance looking straight down: 2% (index of refraction 1.33).
#define WATER_F0 0.02f
// The sun glint's roughness on calm water.
#define WATER_ROUGHNESS 0.02f
// Game units in a metre, near enough.
#define WATER_UNITS_PER_METRE 70.0f

// A texture position turned by angle (radians): each wave layer, the foam and the caustics at their own.
float2 rotateWaterUV(float2 uv, float angle){
    float s = sin(angle);
    float c = cos(angle);
    return float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
}

// ---------------------------------------------------------------------------------------------
// Screen positions. The vertex shader hands over the projection as four rows; a point on the water
// plane, offset in the plane's own units, projects to the reflection map's texture space. The
// refraction map (the scene behind the water) is the same space flipped vertically.
// ---------------------------------------------------------------------------------------------
float4 getStraightScreenPos(PS_INPUT IN){
    float4 screenPos;
    screenPos.x = dot(IN.LTEXCOORD_2, IN.LTEXCOORD_1);
    screenPos.w = dot(IN.LTEXCOORD_5, IN.LTEXCOORD_1);
    screenPos.y = screenPos.w - dot(IN.LTEXCOORD_3, IN.LTEXCOORD_1);
    screenPos.z = dot(IN.LTEXCOORD_4, IN.LTEXCOORD_1);
    return screenPos;
}

float4 getReflectionScreenPos(PS_INPUT IN, float2 planeOffset){
    float4 planePos = float4(IN.LTEXCOORD_1.xy + planeOffset, IN.LTEXCOORD_1.z, 1.0f);
    return mul(float4x4(IN.LTEXCOORD_2, IN.LTEXCOORD_3, IN.LTEXCOORD_4, IN.LTEXCOORD_5), planePos);
}

float4 flipToRefraction(float4 reflectionPos){
    float4 refractionPos = reflectionPos;
    refractionPos.y = reflectionPos.w - reflectionPos.y;
    return refractionPos;
}

// ---------------------------------------------------------------------------------------------
// The scene behind the water. The world depth buffer, which the DLL resolves just before the water
// is drawn (ShaderRecord::SetCT, for any game shader reading TESR_DepthBufferWorld), still holds
// what lies under and behind the water, so the real depth of water the view passes through is
// known everywhere: the game's own water depth map only grades the first few metres off the shore.
// ---------------------------------------------------------------------------------------------
// MUST stay on ONE line (see Shadow.hlsl's TESR_ShadowAtlas).
sampler2D TESR_DepthBufferWorld : register(s8) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
row_major float4x4 TESR_ProjectionTransform : register(c192);
row_major float4x4 TESR_ViewTransform : register(c196);
float4 TESR_CameraData : register(c200);     // x: near, y: far
float4 TESR_DepthConstants : register(c201); // z: 1 when the depth buffer is reversed

// World-space direction from the camera through screen position uv, scaled so that its component
// along the view axis is 1: times a view-space depth, it is the camera-relative position there.
float3 getWaterViewRay(float2 uv){
    float2 ndc = uv * 2.0f - 1.0f;
    float3 ray = float3(TESR_ViewTransform[0][2], TESR_ViewTransform[1][2], TESR_ViewTransform[2][2]);
    ray += (ndc.x / TESR_ProjectionTransform[0][0]) * float3(TESR_ViewTransform[0][0], TESR_ViewTransform[1][0], TESR_ViewTransform[2][0]);
    ray += (-ndc.y / TESR_ProjectionTransform[1][1]) * float3(TESR_ViewTransform[0][1], TESR_ViewTransform[1][1], TESR_ViewTransform[2][1]);
    return ray;
}

// View-space depth of the scene behind the water at uv. Nothing there reads as the far plane.
float getSceneViewZ(float2 uv){
    float rawDepth = tex2Dlod(TESR_DepthBufferWorld, float4(uv, 0.0f, 0.0f)).x;
    float nearZ = TESR_CameraData.x;
    float farZ = TESR_CameraData.y;
    return TESR_DepthConstants.z > 0.5f ? nearZ * farZ / (nearZ + rawDepth * (farZ - nearZ))
                                        : nearZ * farZ / (farZ - rawDepth * (farZ - nearZ));
}

// Camera-relative position of what lies behind the water at projective screen position screenPos.
float3 getBedFromCamera(float4 screenPos){
    float2 uv = screenPos.xy / screenPos.w;
    return getWaterViewRay(uv) * getSceneViewZ(uv);
}

// x: how far the view travels through the water to what is behind it, y: how far that lies below
// the surface point, both in game units.
float2 getWaterPath(float4 screenPos, float3 surfaceFromCamera){
    float3 bed = getBedFromCamera(screenPos);
    return float2(max(length(bed) - length(surfaceFromCamera), 0.0f),
                  max(surfaceFromCamera.z - bed.z, 0.0f));
}

// The refraction is read from the screen offset by the wave normal; where the offset lands on
// something in FRONT of the water -- a pier post, legs, the shore -- that would smear into the water
// around it, so there the straight position is used instead.
float4 getLeakFreeRefraction(float4 refractionPos, float4 straightPos, float3 surfaceFromCamera){
    float3 forward = float3(TESR_ViewTransform[0][2], TESR_ViewTransform[1][2], TESR_ViewTransform[2][2]);
    return getSceneViewZ(refractionPos.xy / refractionPos.w) < dot(surfaceFromCamera, forward) ? straightPos : refractionPos;
}

// ---------------------------------------------------------------------------------------------
// Waves. One normal texture (water_NRM, watercalm_NRM for placed water) in four layers, each turned
// to its own angle so the texture repeat never lines up across the water: a broad swell, large and
// medium waves, and fine ripples that fade out with distance, where they would only shimmer. A very
// large, slow pattern varies the wave strength into rougher and calmer patches. choppiness,
// waveWidth and waveSpeed from the water's own section. tex2D: top level only.
// ---------------------------------------------------------------------------------------------
float3 getWaveNormal(float2 texPos, float distance, float4 waveParams){
    float choppiness = waveParams.x;
    float speed = TESR_GameTime.x * 0.002f * waveParams.z;
    float2 p = texPos * waveParams.y;

    float near = 1.0f - saturate(distance / 3000.0f);
    float3 swell  = expand(tex2D(TESR_samplerWater, rotateWaterUV(p * 0.15f, 0.61f) + normalize(float2(1, 3)) * speed * 0.5f).xyz);
    float3 large  = expand(tex2D(TESR_samplerWater, rotateWaterUV(p * 0.5f, 1.23f) + normalize(float2(-3, -2)) * speed).xyz);
    float3 medium = expand(tex2D(TESR_samplerWater, rotateWaterUV(p * 2.0f, 2.17f) + normalize(float2(2, -1)) * speed).xyz);
    float3 micro  = expand(tex2D(TESR_samplerWater, rotateWaterUV(p * 4.0f, 2.89f) + normalize(float2(2, 2)) * speed).xyz);
    float patches = 0.6f + 0.8f * saturate(tex2D(TESR_samplerWater, p * 0.03f + speed * 0.1f).x);

    float2 tilt = (swell.xy * 0.8f + large.xy + medium.xy * 0.5f + micro.xy * 0.3f * near) * patches;
    float up = (swell.z * 0.8f + large.z + medium.z * 0.5f + micro.z * 0.3f * near) / max(choppiness, 1e-6f);
    return normalize(float3(tilt, up));
}

// Rain rings on the surface (WetWorld's rain amount). Four layers of the ripple texture, each
// dropping at its own time; faded out with distance.
float3 getRainRing(float2 uv, float time, float weight){
    float4 ripple = tex2D(TESR_RippleSampler, uv);
    ripple.yz = expand(ripple.yz);
    float period = frac(ripple.w + time);
    float timeFrac = period - 1.0f + ripple.x;
    float drop = saturate(0.2f + weight * 0.8f - period);
    float strength = drop * ripple.x * sin(clamp(timeFrac * 9.0f, 0.0f, 3.0f) * PI);
    return float3(ripple.yz * strength * 0.35f, 1.0f);
}

float3 getRainRipples(float2 texPos, float3 N, float distance, float rain){
    float fade = 1.0f - saturate(distance / 3500.0f);
    float4 weights = saturate(float4(1.0f, 0.75f, 0.5f, 0.25f) * rain * 4.0f) * 2.0f * fade;
    float4 times = float4(0.96f, 0.97f, 0.98f, 0.99f) * 0.07f * TESR_GameTime.x;
    float2 uv = texPos * 5.0f;
    float3 r1 = getRainRing(uv + float2(0.25f, 0.0f), times.x, weights.x);
    float3 r2 = getRainRing(uv * 1.1f + float2(-0.55f, 0.3f), times.y, weights.y);
    float3 r3 = getRainRing(uv * 1.3f + float2(0.6f, 0.85f), times.z, weights.z);
    float3 r4 = getRainRing(uv * 1.5f + float2(0.5f, -0.75f), times.w, weights.w);
    float2 rings = weights.x * r1.xy + weights.y * r2.xy + weights.z * r3.xy + weights.w * r4.xy;
    return normalize(float3(N.xy + rings, N.z));
}

// The engine's wading ripples around the player and actors (the displacement map), bent into the
// wave normal.
float3 getWadingNormal(float2 displacementPos, float blendRadius, float3 N){
    float4 displacement = tex2D(DisplacementMap, displacementPos);
    float2 wadeSlope = (displacement.zw - 0.5f) * blendRadius / 2.0f;   // reconstructZ is a macro: pass a name
    float3 wade = reconstructZ(wadeSlope);
    return normalize(float3(N.xy + wade.xy * 2.0f, N.z * wade.z));
}

// ---------------------------------------------------------------------------------------------
// Light on the surface
// ---------------------------------------------------------------------------------------------

// GGX glint off a water normal. NdotV and NdotL are kept off exactly 0 inside the BRDF, whose
// 4 * NdotV * NdotL denominator would make 0/0 at the horizon; multiplied by the real NdotL after.
float3 getGlint(float3 N, float3 L, float3 eyeDirection, float roughness){
    float3 H = normalize(eyeDirection + L);
    float NdotL = shades(N, L);
    float NdotV = max(shades(N, eyeDirection), 1e-4f);
    float NdotH = shades(N, H);
    float3 Ks = FresnelShlick(0.08, H, eyeDirection);
    return BRDF(roughness, Ks, NdotV, max(NdotL, 1e-4f), NdotH) * NdotL;
}

// Glint roughness with specular anti-aliasing (SpecularAA): calm water's glint is so narrow that
// where the waves are finer than a pixel it lands on some pixels and misses their neighbours and
// crawls; widened by how fast the normal changes across the pixel, and a little with distance.
// ddx/ddy inside: top level only.
float getSpecularRoughness(float3 N, float distance){
    float antiAliased = SpecularAA(N, WATER_ROUGHNESS);
    antiAliased = sqrt(antiAliased * antiAliased + saturate(distance / 16000.0f) * 0.04f);
    return lerp(WATER_ROUGHNESS, antiAliased, saturate(TESR_WaterLighting2.x));
}

// The sun on the water: the glint, and with SunGlitter the broad sun path full of sharp sparkles
// that sunlit water seen toward the sun has -- a soft lobe draws the path, and the sharp glint of
// each wave facet, kept inside it and capped, draws the sparkles. In sunColor units.
float3 getSunGlint(float3 N, float3 sunDirection, float3 eyeDirection, float roughness){
    float3 glint = getGlint(N, sunDirection, eyeDirection, roughness);
    float3 path = getGlint(N, sunDirection, eyeDirection, 0.3f);
    float3 sparkle = min(getGlint(N, sunDirection, eyeDirection, WATER_ROUGHNESS), 40.0f);
    glint += (sparkle * (path / (path + 1.0f)) * 0.15f + path * 0.1f) * TESR_WaterLighting2.z;
    return glint * 10.0f;
}

// How much of the reflection shows: Schlick with water's own 2%, so the water is clear looking down
// and a mirror at low angles; reflectivity (the water's own section) scales it.
float getFresnel(float3 N, float3 eyeDirection, float reflectivity){
    float fresnel = WATER_F0 + (1.0f - WATER_F0) * pow(1.0f - saturate(dot(eyeDirection, N)), 5.0f);
    return saturate(fresnel * reflectivity);
}

// Wave scattering (WaveScattering): looking toward a low sun, sunlight shines through the thin tops
// and flanks of the waves and lights them up in the water's colour. Measured across the water: the
// wave normals lean only slightly off vertical, and the view down onto the water is well off the sun
// even with the sun straight ahead. Gone once the sun is under the horizon. In light units.
float3 getWaveScattering(float3 N, float3 eyeDirection, float3 sunDirection, float3 sunLight, float3 waterHue){
    float crest = saturate(length(N.xy) * 5.0f);
    float2 viewFlat = -eyeDirection.xy * rsqrt(max(dot(eyeDirection.xy, eyeDirection.xy), 1e-6f));
    float2 sunFlat = sunDirection.xy * rsqrt(max(dot(sunDirection.xy, sunDirection.xy), 1e-6f));
    float towardSun = pow(saturate(dot(viewFlat, sunFlat)), 3.0f);
    float lowSun = (1.0f - saturate(sunDirection.z)) * saturate(sunDirection.z * 20.0f);
    return waterHue * sunLight * crest * towardSun * lowSun * TESR_WaterLighting.w;
}

// Reflection, blurred on choppy water (ReflectionBlur): four taps around the lookup, spread by how
// steep the waves are there, so calm water stays mirror-sharp. Linear. tex2Dproj: top level only.
float3 getBlurredReflection(float4 reflectionPos, float3 N){
    float radius = TESR_WaterLighting3.w * 0.006f * saturate(length(N.xy) * 4.0f) * reflectionPos.w;
    float4 sum = tex2Dproj(ReflectionMap, reflectionPos);
    sum += tex2Dproj(ReflectionMap, reflectionPos + float4( radius, 0.0f, 0.0f, 0.0f));
    sum += tex2Dproj(ReflectionMap, reflectionPos + float4(-radius, 0.0f, 0.0f, 0.0f));
    sum += tex2Dproj(ReflectionMap, reflectionPos + float4(0.0f,  radius, 0.0f, 0.0f));
    sum += tex2Dproj(ReflectionMap, reflectionPos + float4(0.0f, -radius, 0.0f, 0.0f));
    return linearize(sum * 0.2f).rgb;
}

// ---------------------------------------------------------------------------------------------
// The water body
// ---------------------------------------------------------------------------------------------

// The colour the water body glows with: ScatterColor when set -- the game's water colours are near
// black once linear -- else the water form's shallow-to-deep colour by depth.
float3 getWaterColor(float3 shallowColor, float3 deepColor, float depthBelow){
    return TESR_WaterScatterColor.w > 0.5f ? TESR_WaterScatterColor.rgb
                                           : lerp(shallowColor, deepColor, saturate(depthBelow / (20.0f * WATER_UNITS_PER_METRE)));
}

// Beer-Lambert absorption over the path through the water: each colour at its own rate
// (AbsorptionColor), how fast set by AbsorptionDepth. At the defaults, 5 m of water lets through
// about a third of the red and three quarters of the green and blue.
float3 getTransmittance(float pathLength){
    return exp(-TESR_WaterAbsorption.rgb * pathLength * (TESR_WaterLighting.y / 300.0f));
}

// Caustics (Caustics, CausticsScale): sunlight focused by the waves into bright rippling lines on
// the bed in shallow water. Two layers of the wave texture drift across the bed's world position;
// where their slopes cancel, the light converges. None at the waterline, fading out in deep water.
// A factor on the sunlit bed. Needs TESR_CameraPosition. tex2Dlod, a level from the distance.
float getCaustics(float3 bedFromCamera, float depthBelow, float4 waveParams){
    float speed = TESR_GameTime.x * 0.002f * waveParams.z;
    float2 world = (bedFromCamera.xy + TESR_CameraPosition.xy) / TESR_WaterLighting4.y;
    float lod = log2(max(length(bedFromCamera) / (TESR_WaterLighting4.y * 2.0f), 1.0f));
    float2 a = expand(tex2Dlod(TESR_samplerWater, float4(world + float2(0.8f, 0.6f) * speed * 3.0f, 0.0f, lod))).xy;
    float2 b = expand(tex2Dlod(TESR_samplerWater, float4(rotateWaterUV(world * 1.37f, 1.1f) - float2(0.3f, 0.95f) * speed * 3.0f, 0.0f, lod))).xy;
    float focus = saturate(1.0f - length(a + b) * 1.8f);
    focus = focus * focus * focus * 3.0f;
    float depthFade = saturate(depthBelow / 20.0f) * exp(-depthBelow / 500.0f);
    return focus * depthFade * TESR_WaterLighting4.x;
}

// ---------------------------------------------------------------------------------------------
// The edge
// ---------------------------------------------------------------------------------------------

// Foam (Foam, FoamWidth): where the view passes through the least water -- along the shore and
// around anything standing in the water -- solid at the edge and breaking into drifting patches made
// from the wave texture at two other sizes. By the path through the water, not the depth under the
// point: in front of a post that depth is small all the way down the post. tex2D: top level only.
float getFoamMask(float2 texPos, float pathLength, float4 waveParams){
    float band = 1.0f - saturate(pathLength / TESR_WaterLighting3.y);
    band *= band;
    float speed = TESR_GameTime.x * 0.002f * waveParams.z;
    float2 p = texPos * waveParams.y;
    float n = tex2D(TESR_samplerWater, rotateWaterUV(p * 3.0f, 0.4f) + float2(0.7f, 0.3f) * speed).x
            + tex2D(TESR_samplerWater, rotateWaterUV(p * 7.0f, 1.9f) - float2(0.4f, 0.9f) * speed).y - 0.5f;
    return saturate((band * 1.6f - (1.0f - saturate(n))) * 2.5f) * saturate(TESR_WaterLighting3.x);
}

// Shoreline fade (ShoreFadeWidth): the water fades out over that much depth at the edge, lapping in
// and out with shoreMovement; the foam stays visible through it and draws the waterline. 0: none.
float getShoreAlpha(float depthBelow, float shoreMovement, float foam){
    float width = TESR_WaterLighting3.z;
    if (width <= 0.0f) return 1.0f;
    float lap = sin(TESR_GameTime.x * shoreMovement * 0.1f) * 0.25f;
    return max(smoothstep(0.0f, width, depthBelow + lap * width), foam * 0.9f);
}

// ---------------------------------------------------------------------------------------------
// Point lights (PointLights): lamps and campfires glinting on the water. Needs TESR_CameraPosition,
// TESR_ShadowLightPosition[12], TESR_LightPosition[12] and TESR_LightColor[24]. Light positions
// are world space; toLight is built as (light - camera) - pixel with the pixel camera-relative, so
// the large coordinates cancel first. A light that does not reach the pixel is skipped, and the
// loop ends at the first slot where both lists are empty (both are packed from slot 0).
// ---------------------------------------------------------------------------------------------
#ifdef WATER_POINT_LIGHTS
float3 getPointLightGlint(float3 N, float4 light, float4 colour, float3 pixelFromCamera, float3 eyeDirection, float roughness){
    float3 toLight = (light.xyz - TESR_CameraPosition.xyz) - pixelFromCamera;
    float distSq = dot(toLight, toLight);
    float radiusSq = light.w * light.w;
    float3 glint = 0.0f;
    [branch]
    if (light.w > 0.0f && distSq < radiusSq) {
        float s = distSq / radiusSq;
        float atten = saturate(((1.0f - s) * (1.0f - s)) / (1.0f + 5.0f * s));
        float3 L = toLight * rsqrt(max(distSq, 1e-4f));
        glint = getGlint(N, L, eyeDirection, roughness) * colour.rgb * colour.w * atten;
    }
    return glint;
}

float3 getPointLights(float3 N, float3 pixelFromCamera, float3 eyeDirection, float roughness){
    float3 specular = 0.0f;
    float strength = TESR_WaterLighting2.y;
    [branch]
    if (strength > 0.0f) {
        [loop]
        for (int i = 0; i < 12; i++) {
            if (TESR_ShadowLightPosition[i].w <= 0.0f && TESR_LightPosition[i].w <= 0.0f) break;
            specular += getPointLightGlint(N, TESR_ShadowLightPosition[i], TESR_LightColor[i], pixelFromCamera, eyeDirection, roughness);
            specular += getPointLightGlint(N, TESR_LightPosition[i], TESR_LightColor[12 + i], pixelFromCamera, eyeDirection, roughness);
        }
        specular *= strength;
    }
    return specular;
}
#endif

// ---------------------------------------------------------------------------------------------
// The sun shadow on the surface (SunShadows). Needs Shadow.hlsl; forward shadows only
// (FORWARD_SHADOWS compiled in, and not suppressed at runtime -- GetSunShadow gives 1 then). The
// surface is flat, so the bias normal is up. [branch] around it; tex2Dlod inside.
// ---------------------------------------------------------------------------------------------
#ifdef WATER_SUN_SHADOWS
float getWaterSunShadow(float3 surfaceFromCamera){
    float shadow = 1.0f;
#if FORWARD_SHADOWS
    [branch]
    if (TESR_WaterLighting.x > 0.0f)
        shadow = lerp(1.0f, GetSunShadow(surfaceFromCamera, float3(0.0f, 0.0f, 1.0f)), saturate(TESR_WaterLighting.x));
#endif
    return shadow;
}
#endif

// ---------------------------------------------------------------------------------------------
// DebugView: one term on its own, in place of the water.
//   1 sun shadow on the surface (black in shadow)   2 what still shows through the water, per colour
//   3 reflection amount                              4 wave scattering
//   5 glint roughness (black calm, white widened)    6 point-light glints
//   7 path through the water, black 0 to white 20 m  8 depth below the surface, black 0 to white 20 m
//   9 foam                                           10 shoreline fade (black see-through, white solid)
//  11 caustics
// ---------------------------------------------------------------------------------------------
struct WaterDebug {
    float shadow;
    float3 transmittance;
    float fresnel;
    float3 scattering;
    float roughness;
    float3 pointLights;
    float2 path;
    float foam;
    float alpha;
    float caustics;
};

float3 getWaterDebugView(float view, WaterDebug d){
    float3 result = d.shadow;
    result = view > 1.5f ? d.transmittance : result;
    result = view > 2.5f ? d.fresnel : result;
    result = view > 3.5f ? saturate(d.scattering) : result;
    result = view > 4.5f ? saturate((d.roughness - WATER_ROUGHNESS) / 0.4f) : result;
    result = view > 5.5f ? saturate(d.pointLights) : result;
    result = view > 6.5f ? saturate(d.path.x / (20.0f * WATER_UNITS_PER_METRE)) : result;
    result = view > 7.5f ? saturate(d.path.y / (20.0f * WATER_UNITS_PER_METRE)) : result;
    result = view > 8.5f ? d.foam : result;
    result = view > 9.5f ? saturate(d.alpha) : result;
    result = view > 10.5f ? saturate(d.caustics) : result;
    return result;
}
