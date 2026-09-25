

// Requires the following registers:
//
//   Name            Reg   Size
//   --------------- ----- ----
//   EyePos          const_1       1
//   shallowColor    const_2       1
//   deepColor       const_3       1
//   ReflectionColor const_4       1
//   FresnelRI       const_5       1  //x: reflectamount, y:fresnel, w: opacity, z:speed
//   BlendRadius     const_6       1
//   VarAmounts      const_8       1  // x: water glossiness y: reflectivity z: refrac, w: lod
//   FogParam        const_9       1
//   FogColor        const_10      1
//   DepthFalloff    const_11      1  // start / end depth fog
//   SunDir          const_12      1
//   SunColor        const_13      1
//   ReflectionMap   texture_0       1
//   RefractionMap   texture_1       1
//   NoiseMap        texture_2       1
//   DisplacementMap texture_3       1
//   DepthMap        texture_4       1
//   float4 TESR_WaveParams : register(c14); // x: choppiness, y:wave width, z: wave speed, w: reflectivity?
//   float4 TESR_WaterVolume : register(c15); // x: caustic strength, y:shoreFactor, w: turbidity, z: caustic strength S ?
//   float4 TESR_WaterSettings : register(c16); // x: caustic strength, y:depthDarkness, w: turbidity, z: caustic strength S ?
//   float4 TESR_GameTime : register(c17);
//   float4 TESR_SkyColor : register(c18);
//   float4 TESR_SunDirection : register(c19);
//   sampler2D TESR_samplerWater : register(s5);


struct PS_INPUT {
    float4 LTEXCOORD_0 : TEXCOORD0_centroid;     // world position of underwater points
    float4 LTEXCOORD_1 : TEXCOORD1_centroid;     // local position on plane object surface
    float4 LTEXCOORD_2 : TEXCOORD2_centroid;     // modelviewproj matrix 1st row 
    float4 LTEXCOORD_3 : TEXCOORD3_centroid;     // modelviewproj matrix 2nd row 
    float4 LTEXCOORD_4 : TEXCOORD4_centroid;     // modelviewproj matrix 3rd row 
    float4 LTEXCOORD_5 : TEXCOORD5_centroid;     // modelviewproj matrix 4th row 
    float4 LTEXCOORD_6 : TEXCOORD6;              // displacement sampling position
    float2 LTEXCOORD_7 : TEXCOORD7;              // waves sampling position
    float4 WorldPosition : TEXCOORD8;
};

struct PS_OUTPUT {
    float4 color_0 : COLOR0;
};

#include "Includes/PBR.hlsl"

// ---------------------------------------------------------------------------------------------
// Water lighting ([Shaders.Water.Main], WaterShaders::UpdateSettings). Every term is off at 0, which
// is also what a missing setting reads as, so with all of them 0 the water renders as it did before.
//   TESR_WaterLighting   x: SunShadows      how much the sun shadow takes off the sun glint, the wave
//                                           scattering and the sunlit water body (forward shadows only)
//                        y: Absorption      0 the old water colour, 1 depth absorption (Beer-Lambert)
//                        z: AbsorptionDepth how fast light is absorbed with depth (never 0)
//                        w: WaveScattering  sunlight glowing through wave crests
//   TESR_WaterLighting2  x: SpecularAA      widens the glint where the waves are finer than a pixel
//                        y: PointLights     point-light glints (campfires, lamps); 0 skips the loop
//                        z: PhysicalFresnel 0 the old reflection strength, 1 water's real reflectance
//                        w: DebugView       0 off, see waterDebugView
// c190/c191: clear of every water shader's own constants (up to c70) and of Shadow.hlsl (c100-c133).
// ---------------------------------------------------------------------------------------------
float4 TESR_WaterLighting  : register(c190);
float4 TESR_WaterLighting2 : register(c191);

#ifdef WATER_SCENE_DEPTH
// ---------------------------------------------------------------------------------------------
// How much water the view passes through to reach what is behind it. The game's water depth map
// (DepthMap, both channels) only grades the first few metres off the shore and then stays flat, so
// it cannot tell a shallow bank from the middle of a lake. Instead the scene depth behind the water
// -- the world depth buffer, which the DLL resolves just before each draw of a shader that reads
// TESR_DepthBufferWorld (ShaderRecord::SetCT), before the water itself is drawn into it -- gives the
// bed's position along the view ray; the water surface's own is the camera-relative LTEXCOORD_0.
// All constants pinned, clear of the water shaders' own and of Shadow.hlsl (c100-c133).
// MUST stay on ONE line (see Shadow.hlsl's TESR_ShadowAtlas).
sampler2D TESR_DepthBufferWorld : register(s8) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
row_major float4x4 TESR_ProjectionTransform : register(c192);
row_major float4x4 TESR_ViewTransform : register(c196);
float4 TESR_CameraData : register(c200);     // x: near, y: far
float4 TESR_DepthConstants : register(c201); // z: 1 when the depth buffer is reversed

// World-space direction from the camera through screen position uv, scaled so that its component
// along the view axis is 1: times a view-space depth it is the camera-relative position there.
float3 getWaterViewRay(float2 uv){
    float2 ndc = uv * 2.0f - 1.0f;
    float3 ray = float3(TESR_ViewTransform[0][2], TESR_ViewTransform[1][2], TESR_ViewTransform[2][2]);
    ray += (ndc.x / TESR_ProjectionTransform[0][0]) * float3(TESR_ViewTransform[0][0], TESR_ViewTransform[1][0], TESR_ViewTransform[2][0]);
    ray += (-ndc.y / TESR_ProjectionTransform[1][1]) * float3(TESR_ViewTransform[0][1], TESR_ViewTransform[1][1], TESR_ViewTransform[2][1]);
    return ray;
}

// x: the distance the view travels through the water to the bed, y: the water's depth straight down
// there, both in game units (about 70 to a metre). screenPos: the projective position the
// refraction is read from, so the depth goes with the bed the pixel shows. Nothing behind the water
// (depth buffer empty) reads as the far plane: as deep as it gets. tex2Dlod: legal anywhere.
float2 getWaterPath(float4 screenPos, float3 surfaceFromCamera){
    float2 uv = screenPos.xy / screenPos.w;
    float rawDepth = tex2Dlod(TESR_DepthBufferWorld, float4(uv, 0.0f, 0.0f)).x;
    float nearZ = TESR_CameraData.x;
    float farZ = TESR_CameraData.y;
    float viewZ = TESR_DepthConstants.z > 0.5f ? nearZ * farZ / (nearZ + rawDepth * (farZ - nearZ))
                                               : nearZ * farZ / (farZ - rawDepth * (farZ - nearZ));
    float3 bedFromCamera = getWaterViewRay(uv) * viewZ;
    return float2(max(length(bedFromCamera) - length(surfaceFromCamera), 0.0f),
                  max(surfaceFromCamera.z - bedFromCamera.z, 0.0f));
}
#endif

// Water's reflectance looking straight down: 2% (index of refraction 1.33).
#define WATER_F0 0.02f
// The glint's roughness on calm water, as before.
#define WATER_ROUGHNESS 0.02f

float4 getScreenpos(PS_INPUT IN){
    float4 screenPos;  // point coordinates in screen space for water surface
    screenPos.x = dot(IN.LTEXCOORD_2, IN.LTEXCOORD_1);
    screenPos.w = dot(IN.LTEXCOORD_5, IN.LTEXCOORD_1);
    screenPos.y = screenPos.w - dot(IN.LTEXCOORD_3, IN.LTEXCOORD_1);
    screenPos.z = dot(IN.LTEXCOORD_4, IN.LTEXCOORD_1);
    
    return screenPos;
}

float3 getWaveTexture(PS_INPUT IN, float distance, float4 waveParams) {

    float2 texPos = IN.LTEXCOORD_7;

	float waveWidth = waveParams.y;
    float choppiness = waveParams.x;
    float speed = TESR_GameTime.x * 0.002 * waveParams.z;
    float smallScale = 0.5;
    float bigScale = 2;
    float3 waveTexture = expand(tex2D(TESR_samplerWater, texPos * smallScale * waveWidth + normalize(float2(1, 4)) * speed)).xyz * 0.5;
    float3 waveTextureLarge = expand(tex2D(TESR_samplerWater, texPos * bigScale * waveWidth + normalize(float2(-3, -2)) * speed)).xyz * 1;
    float3 waveTextureMicro = expand(tex2D(TESR_samplerWater, texPos * bigScale * 2 * waveWidth + normalize(float2(2, 2)) * speed)).xyz * 0.3;

    // combine waves
    waveTexture = float3(waveTextureLarge.xy + waveTexture.xy + waveTextureMicro.xy,  waveTextureLarge.z + waveTexture.z + waveTextureMicro.z);
    waveTexture.z *= 1/max(choppiness, 0.000001);
    // waveTexture.z *= lerp(1, 0.5, (distance / 4096)) / max(choppiness, 0.000001);

    waveTexture = normalize(waveTexture);

    return waveTexture;
}

float4 getReflectionSamplePosition(PS_INPUT IN, float3 surfaceNormal, float refractionCoeff) {
    int4 const_7 = {0, 2, -1, 1}; // used to cancel/double/invert vector components

    float4 samplePosition;
    samplePosition.xy = ((refractionCoeff * surfaceNormal.xy)) + IN.LTEXCOORD_1.xy;
    // waveTexture.xy = ((refractionCoeff * surfaceNormal.xy) / IN.LTEXCOORD_0.w) + IN.LTEXCOORD_1.xy;
    samplePosition.zw = (IN.LTEXCOORD_1.z * const_7.wx) + const_7.xw;

    float4 reflectionPos = mul(float4x4(IN.LTEXCOORD_2, IN.LTEXCOORD_3, IN.LTEXCOORD_4, IN.LTEXCOORD_5), samplePosition); // convert local normal to view space

    return reflectionPos;
}

float3 getDisplacement(PS_INPUT IN, float blendRadius, float3 surfaceNormal){
    // sample displacement and mix with the wave texture
    float4 displacement = tex2D(DisplacementMap, IN.LTEXCOORD_6.xy);

    displacement.xy = (displacement.zw - 0.5) * blendRadius / 2;

    // sample displacement and mix with the wave texture
    float3 DisplacementNormal = normalize(reconstructZ(displacement.xy));

    surfaceNormal = float3(surfaceNormal.xy + DisplacementNormal.xy * 2,  surfaceNormal.z * DisplacementNormal.z);
    surfaceNormal = normalize(surfaceNormal);
    return surfaceNormal;
}

float4 getLightTravel(float3 refractedDepth, float4 shallowColor, float4 deepColor, float sunLuma, float4 waterSettings, float4 color){
    float4 waterColor = lerp(shallowColor, deepColor, refractedDepth.y); 
    //float4 waterColor = shallowColor; 
    float depthDarknessPower = saturate(pows((1 - waterSettings.y), 3)); // high darkness means low values
    float3 result = color.rgb * lerp(0.7, lerp(waterColor.rgb * depthDarknessPower, 1, depthDarknessPower) , refractedDepth.x) ; //never reach 1 so that water is always absorbing some light
    return float4(result, 1);
}

float4 getTurbidityFog(float3 refractedDepth, float4 shallowColor, float4 waterVolume, float sunLuma, float4 color){
    float turbidity = waterVolume.z;

    float depth = pows(refractedDepth.x, turbidity);

    float fogCoeff = 1 - saturate((FogParam.z - (refractedDepth.x * FogParam.z)) / FogParam.w);
    float3 fog = shallowColor.rgb * sunLuma;

    float3 result = lerp(color.rgb, fog.rgb, saturate(fogCoeff * FogColor.a * turbidity));

    // return float4(1 - refractedDepth.yyy, 1);
    return float4(result, 1);
}

float4 getDiffuse(float3 surfaceNormal, float3 lightDir, float3 eyeDirection, float distance, float4 diffuseColor, float4 color){
    float verticalityFade =  (1 - shades(eyeDirection, float3(0, 0, 1)));
    float distanceFade = smoothstep(0, 1, distance * 0.001);
    float diffuse = shades(lightDir, surfaceNormal) * verticalityFade * distanceFade; // increase intensity with distance
    float3 result = lerp(color.rgb, diffuseColor.rgb, saturate(diffuse));

    return float4(result, 1);
}

// How much of the reflection shows. The old blend -- 80% Schlick plus 20% of however much brighter
// the reflection is than the water -- showed bright sky even looking straight down; PhysicalFresnel
// moves it to Schlick with water's own 2%, so the water is clear looking down and a mirror at low
// angles. Reflectivity scales either.
float getFresnelAmount(float3 surfaceNormal, float3 eyeDirection, float4 reflection, float reflectivity, float4 color){
    float fresnelCoeff = pow(1.0f - saturate(dot(eyeDirection, surfaceNormal)), 5.0f);
    float lumaDiff = saturate(luma(reflection) - luma(color));
    float legacy = saturate((fresnelCoeff * 0.8f + 0.2f * lumaDiff) * reflectivity);
    float physical = saturate((WATER_F0 + (1.0f - WATER_F0) * fresnelCoeff) * reflectivity);
    return lerp(legacy, physical, saturate(TESR_WaterLighting2.z));
}

float4 getFresnel(float3 surfaceNormal, float3 eyeDirection, float4 reflection, float reflectivity, float4 color){
	float3 result = lerp(color.rgb, reflection.rgb, getFresnelAmount(surfaceNormal, eyeDirection, reflection, reflectivity, color));
    return float4(result, 1);
}

// GGX glint off a water normal. NdotV and NdotL are kept off exactly 0 inside the BRDF, whose
// 4 * NdotV * NdotL denominator would otherwise make 0/0 (NaN) with the light at the horizon; the
// result is still multiplied by the real NdotL, so it is 0 there.
float3 getGlint(float3 N, float3 L, float3 eyeDirection, float roughness){
    float3 H = normalize(eyeDirection + L);
    float NdotL = shades(N, L);
    float NdotV = max(shades(N, eyeDirection), 1e-4f);
    float NdotH = shades(N, H);
    float3 Ks = FresnelShlick(0.08, H, eyeDirection);
    return BRDF(roughness, Ks, NdotV, max(NdotL, 1e-4f), NdotH) * NdotL;
}

// Glint roughness with specular anti-aliasing (SpecularAA). Calm water's glint is a GGX lobe so
// narrow that where the waves are finer than a pixel -- all distant water -- it lands on some
// pixels and misses their neighbours, and sparkles and crawls from frame to frame. Widened by how
// fast the wave normal changes across the pixel (PBR.hlsl's SpecularAA), plus a little with
// distance, which mipmapping hides from the derivatives. A wider GGX lobe keeps its energy, so the
// glint gets broader and softer, not dimmer overall.
// ddx/ddy inside: call at the top level of the shader, never under a branch or in a loop.
float getSpecularRoughness(float3 surfaceNormal, float distance){
    float antiAliased = SpecularAA(surfaceNormal, WATER_ROUGHNESS);
    antiAliased = sqrt(antiAliased * antiAliased + saturate(distance / 16000.0f) * 0.04f);
    return lerp(WATER_ROUGHNESS, antiAliased, saturate(TESR_WaterLighting2.x));
}

float4 getSunSpecular(float3 surfaceNormal, float3 lightDir, float3 eyeDirection, float3 specColor, float roughness, float4 color){
    float specularBoost = 10;
    return float4(color.rgb + getGlint(normalize(surfaceNormal), lightDir, eyeDirection, roughness) * specColor * specularBoost, color.a);
}

// Depth absorption (Absorption, AbsorptionDepth). Light through water loses each colour at its own
// rate (Beer-Lambert) as real water absorbs it: red first, then green, blue last, so the bed goes
// from clear in the shallows through blue-green to gone. The water's own colour -- the water form's
// shallow-to-deep colour -- comes in as the light scattered back out of the water body, lit by
// inscatterLight, in place of what absorption takes away, so every water still keeps its look.
// (Rates taken from the deep colour instead turned near-black deep colours -- most of the game's,
// once linear -- into water that swallowed every channel within a few metres, and tinted the
// shallows with whatever channel the deep colour happened to favour.)
// pathLength: how far the view travels through the water (getWaterPath), in game units. At
// AbsorptionDepth 1, 5 m of water (350 units) lets through about 30% red, 65% green, 75% blue, and
// 20 m next to no red and a third of the blue. refractedDepth: the depth map's 0-1 depths, still
// what the old colour ramp and getLightTravel use. Absorption 0 is the old getLightTravel, exactly.
#define WATER_ABSORPTION float3(1.0f, 0.4f, 0.25f)

float4 getWaterBody(float4 color, float3 refractedDepth, float pathLength, float4 shallowColor, float4 deepColor, float sunLuma, float4 waterSettings, float inscatterLight, out float3 transmittance){
    float4 legacy = getLightTravel(refractedDepth, shallowColor, deepColor, sunLuma, waterSettings, color);

    transmittance = exp(-WATER_ABSORPTION * pathLength * (TESR_WaterLighting.z / 300.0f));

    float3 waterColor = lerp(shallowColor.rgb, deepColor.rgb, saturate(refractedDepth.y));
    float3 physical = color.rgb * transmittance + waterColor * inscatterLight * (1.0f - transmittance);
    return float4(lerp(legacy.rgb, physical, saturate(TESR_WaterLighting.y)), 1.0f);
}

// Wave scattering (WaveScattering): looking toward a low sun, sunlight shines through the thin tops
// and flanks of the waves and lights them up in the water's colour -- the turquoise glow on backlit
// waves. Where the surface is tilted (wave flanks and crests), with the sun ahead of the camera,
// stronger the lower the sun. The shallow colour's hue, at the sun's colour and brightness.
// Both measured flat, across the water: the wave normals lean only a little off vertical (a tilt of
// 0.05-0.3), so 1 - N.z barely left zero, and the view ray down onto the water is well off the sun
// even with the sun straight ahead, so the full 3D angle between them killed it too.
// eyeDirection points from the surface to the camera, sunDirection to the sun.
float3 getWaveScattering(float3 surfaceNormal, float3 eyeDirection, float3 sunDirection, float3 sunColor, float4 shallowColor, float shadow){
    float crest = saturate(length(surfaceNormal.xy) / max(length(surfaceNormal), 1e-4f) * 5.0f);
    float2 viewFlat = -eyeDirection.xy * rsqrt(max(dot(eyeDirection.xy, eyeDirection.xy), 1e-6f));
    float2 sunFlat = sunDirection.xy * rsqrt(max(dot(sunDirection.xy, sunDirection.xy), 1e-6f));
    float towardSun = pow(saturate(dot(viewFlat, sunFlat)), 3.0f);
    float lowSun = 1.0f - saturate(sunDirection.z);
    float3 hue = shallowColor.rgb / max(max(shallowColor.r, max(shallowColor.g, shallowColor.b)), 1e-6f);
    return hue * sunColor * crest * towardSun * lowSun * shadow * TESR_WaterLighting.w;
}

#ifdef WATER_SUN_SHADOWS
// Sun shadow on the water surface (SunShadows): 1 in sunlight. In shadow the sun glint, the wave
// scattering and the sunlit part of the water body go; the reflection stays, since it shows the sky
// and the shore, which the shadow does not touch. Needs Shadow.hlsl included first; forward shadows
// only (FORWARD_SHADOWS compiled in, and not suppressed at runtime -- GetSunShadow gives 1 then).
// cameraRelativePos: IN.LTEXCOORD_0.xyz, which the water vertex shaders write as the world
// transform's output, camera-relative (WATER000.vso adds TESR_CameraPosition to it for the world
// position), the same space the shadow cascades are in. The surface is flat, so the bias normal is up.
float getWaterSunShadow(float3 cameraRelativePos){
    float shadow = 1.0f;
#if FORWARD_SHADOWS
    [branch]
    if (TESR_WaterLighting.x > 0.0f)
        shadow = lerp(1.0f, GetSunShadow(cameraRelativePos, float3(0.0f, 0.0f, 1.0f)), saturate(TESR_WaterLighting.x));
#endif
    return shadow;
}
#endif

#ifdef WATER_POINT_LIGHTS
// Point-light glints (PointLights): lamps and campfires reflected in the water. Needs
// TESR_CameraPosition, TESR_ShadowLightPosition[12], TESR_LightPosition[12] and TESR_LightColor[24]
// declared first. Light positions are world space; toLight is built as (light - camera) - pixel with
// the pixel camera-relative, so the large world coordinates cancel before any per-pixel maths.
// Attenuation as the interior water always had it. A light that does not reach the pixel is skipped
// (its attenuation there is exactly 0), and the loop stops at the first slot where both lists are
// empty, since both are packed from slot 0.
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

float3 getPointLightsSpecular(float3 surfaceNormal, float3 pixelFromCamera, float3 eyeDirection, float roughness){
    float3 specular = 0.0f;
    float strength = TESR_WaterLighting2.y;
    [branch]
    if (strength > 0.0f) {
        float3 N = normalize(surfaceNormal);
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

// DebugView ([Shaders.Water.Main]): one term on its own, in place of the water.
//   1 sun shadow on the surface (black in shadow)       2 absorption: what still shows through, per colour
//   3 reflection amount (Fresnel), black none to white  4 wave scattering
//   5 glint roughness: black calm, white fully widened  6 point-light glints
//   7 how far the view travels through the water to the bed, black 0 to white 20 m (1400 units) --
//     what Absorption and AbsorptionDepth work from
//   8 the water's depth straight down, black 0 to white 20 m
float3 waterDebugView(float view, float shadow, float3 transmittance, float fresnel, float3 scattering, float roughness, float3 pointLights, float2 waterPath){
    float3 result = shadow;
    result = view > 1.5f ? transmittance : result;
    result = view > 2.5f ? fresnel : result;
    result = view > 3.5f ? saturate(scattering) : result;
    result = view > 4.5f ? saturate((roughness - WATER_ROUGHNESS) / 0.4f) : result;
    result = view > 5.5f ? saturate(pointLights) : result;
    result = view > 6.5f ? saturate(waterPath.x / 1400.0f) : result;
    result = view > 7.5f ? saturate(waterPath.y / 1400.0f) : result;
    return result;
}

float4 getShoreFade(PS_INPUT IN, float depth, float shoreSpeed, float shoreFactor, float4 color){
    float scale = 0.07;
    shoreSpeed *= 0.1;
    shoreFactor *= 0.1;

    float shoreAnimation = sin(IN.LTEXCOORD_7.x/scale + TESR_GameTime.x * shoreSpeed);
    shoreAnimation *= cos(IN.LTEXCOORD_7.y/scale + TESR_GameTime.x * shoreSpeed);
    shoreAnimation = compress(shoreAnimation); // create a grid of gradient values from 0 to 1

    float depthGradient = smoothstep(saturate(shoreFactor) * compress(sin(TESR_GameTime.x * shoreSpeed) * shoreAnimation), 0, depth);

    color.a = 1 - depthGradient;
    return color;
}


float3 ComputeRipple(sampler2D puddlesSampler, float2 UV, float CurrentTime, float Weight)
{
    float4 Ripple = tex2D(puddlesSampler, UV);
    Ripple.yz = expand(Ripple.yz); // convert from 0/1 to -1/1 

    float period = frac(Ripple.w + CurrentTime);
    float TimeFrac = period - 1.0f + Ripple.x;
    float DropFactor = saturate(0.2f + Weight * 0.8f - period);
    float FinalFactor = DropFactor * Ripple.x * sin( clamp(TimeFrac * 9.0f, 0.0f, 3.0f) * PI);

    return float3(Ripple.yz * FinalFactor * 0.35f, 1.0f);
}


float3 getRipples(PS_INPUT IN, sampler2D puddlesSampler, float3 surfaceNormal, float distance, float rainCoeff){
    float distanceFade = 1 - saturate(invlerp(0, 3500, distance));

    if (!rainCoeff || !distanceFade) return surfaceNormal;

    // sample and combine rain ripples
    float4 time = float4(0.96f, 0.97f,  0.98f, 0.99f) * 0.07; // Ripple timing

	float2 rippleUV = IN.LTEXCOORD_7 * 5; // scale coordinates
	float4 Weights = float4(1, 0.75, 0.5, 0.25) * rainCoeff;
	Weights = saturate(Weights * 4) * 2 * distanceFade;
	float3 Ripple1 = ComputeRipple(puddlesSampler, rippleUV + float2( 0.25f,0.0f), time.x * TESR_GameTime.x, Weights.x);
	float3 Ripple2 = ComputeRipple(puddlesSampler, rippleUV * 1.1 + float2(-0.55f,0.3f), time.y * TESR_GameTime.x, Weights.y);
	float3 Ripple3 = ComputeRipple(puddlesSampler, rippleUV * 1.3 + float2(0.6f, 0.85f), time.z * TESR_GameTime.x, Weights.z);
	float3 Ripple4 = ComputeRipple(puddlesSampler, rippleUV * 1.5 + float2(0.5f,-0.75f), time.w * TESR_GameTime.x, Weights.w);

	float4 Z = lerp(1, float4(Ripple1.z, Ripple2.z, Ripple3.z, Ripple4.z), Weights);
	float3 ripple = float3( Weights.x * Ripple1.xy + Weights.y * Ripple2.xy + Weights.z * Ripple3.xy + Weights.w * Ripple4.xy, Z.x * Z.y * Z.z * Z.w);
	float3 ripnormal = normalize(ripple);
    
    float3 combnom = normalize(float3(ripnormal.xy + surfaceNormal.xy, surfaceNormal.z));

    return combnom;
}