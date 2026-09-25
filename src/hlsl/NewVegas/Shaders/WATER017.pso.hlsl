// wading (displacement) water surface shader

float4 EyePos : register(c1);
float4 ShallowColor : register(c2);
float4 DeepColor : register(c3);
float4 ReflectionColor : register(c4);
float4 FresnelRI : register(c5);
float4 BlendRadius : register(c6);
float4 VarAmounts : register(c8);
float4 FogParam : register(c9);
float4 FogColor : register(c10);
float2 DepthFalloff : register(c11);
float4 SunDir : register(c12);
float4 SunColor : register(c13);
float4 TESR_WaveParams : register(c14); // x: choppiness, y:wave width, z: wave speed, w: reflectivity?
float4 TESR_WaterVolume : register(c15); // x: caustic strength, y:shoreFactor, w: turbidity, z: caustic strength S ?
float4 TESR_WaterSettings : register(c16); // x: caustic strength, y:depthDarkness, w: turbidity, z: caustic strength S ?

float4 TESR_GameTime : register(c17);
float4 TESR_HorizonColor : register(c18);
float4 TESR_SunDirection : register(c19);
float4 TESR_WetWorldData : register(c20);
float4 TESR_WaterShorelineParams : register(c21);
float4 TESR_WaterLODColor : register(c22);
float4 TESR_DebugVar : register(c23);
float4 TESR_SunAmount : register(c24);

sampler2D ReflectionMap : register(s0);
sampler2D RefractionMap : register(s1);
sampler2D NoiseMap : register(s2);
sampler2D DisplacementMap : register(s3);
sampler2D DepthMap : register(s4);
sampler2D TESR_samplerWater : register(s5) < string ResourceName = "Water\water_NRM.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; ADDRESSW = WRAP; MAGFILTER = ANISOTROPIC; MINFILTER = ANISOTROPIC; MIPFILTER = ANISOTROPIC; } ;
sampler2D TESR_RippleSampler : register(s6) < string ResourceName = "Precipitations\ripples.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };

// The water lighting's point-light glints (Water.hlsl, WATER_POINT_LIGHTS) and sun shadow (Shadow.hlsl,
// c100-c133 and s9), in registers clear of this shader's own.
float4 TESR_CameraPosition : register(c140);
float4 TESR_ShadowLightPosition[12] : register(c141);
float4 TESR_LightPosition[12] : register(c153);
float4 TESR_LightColor[24] : register(c165);

#define WATER_SUN_SHADOWS
#define WATER_POINT_LIGHTS
#define WATER_SCENE_DEPTH
#include "Includes/Helpers.hlsl"
#include "Includes/Shadow.hlsl"
#include "Includes/Water.hlsl"

PS_OUTPUT main(PS_INPUT IN) {
    PS_OUTPUT OUT;

    float4 linSunColor = linearize(SunColor);
    float4 linShallowColor = linearize(ShallowColor);
    float4 linDeepColor = linearize(DeepColor);
    float4 linHorizonColor = linearize(TESR_HorizonColor);

    float3 eyeVector = EyePos.xyz - IN.LTEXCOORD_0.xyz; // vector of camera position to point being shaded
    float3 eyeDirection = normalize(eyeVector);         // normalized eye to world vector (for lighting)
    float distance = length(eyeVector.xy);              // surface distance to eye
    float depth = length(eyeVector);                    // depth distance to eye

    // calculate fog coeffs
    float4 screenPos = getScreenpos(IN);                // point coordinates in screen space for water surface

    float2 waterDepth = tex2Dproj(DepthMap, screenPos).xy;  // x= shallowfog, y = deepfog?
    float depthFog = saturate(invlerp(DepthFalloff.x, DepthFalloff.y, waterDepth.y));
    
    float2 fadedDepth = saturate(lerp(waterDepth, 1, invlerp(0, 4096, distance)));

    float3 surfaceNormal = getWaveTexture(IN, distance, TESR_WaveParams).xyz;
    surfaceNormal = getRipples(IN, TESR_RippleSampler, surfaceNormal, distance, TESR_WetWorldData.x);
    surfaceNormal = getDisplacement(IN, BlendRadius.w, surfaceNormal);

    float LODfade = saturate(smoothstep(4096,4096 * 2, distance));
    float isDayTime = smoothstep(0, 0.5, TESR_SunAmount.x);
    float sunLuma = luma(linSunColor) * isDayTime;
    float exteriorRefractionModifier = TESR_WaterSettings.w;		// reduce refraction because of the way interior depth is encoded
    float exteriorDepthModifier = 1;			// reduce depth value for fog because of the way interior depth is encoded

    float refractionCoeff = (waterDepth.y * depthFog) * ((saturate(distance * 0.002) * (-4 + VarAmounts.w)) + 4);
    float4 reflectionPos = getReflectionSamplePosition(IN, surfaceNormal, refractionCoeff * exteriorRefractionModifier);
    float4 reflection = linearize(tex2Dproj(ReflectionMap, reflectionPos));
    float4 refractionPos = reflectionPos;
    refractionPos.y = refractionPos.w - reflectionPos.y;
    float3 refractedDepth = tex2Dproj(DepthMap, refractionPos).rgb;

    // Water lighting (Water.hlsl): the sun shadow on the surface and the glint roughness first -- the
    // roughness takes derivatives, so it stays at the top level -- then the water body, the wave
    // scattering, the reflection and the glints.
    float shadow = getWaterSunShadow(IN.LTEXCOORD_0.xyz);
    float specRoughness = getSpecularRoughness(surfaceNormal, distance);
    float3 transmittance;
    float2 waterPath = getWaterPath(refractionPos, IN.LTEXCOORD_0.xyz);   // through the water to the bed, and straight down

    float4 color = linearize(tex2Dproj(RefractionMap, refractionPos));
    color = getWaterBody(color, refractedDepth, waterPath.x, linShallowColor, linDeepColor, sunLuma, TESR_WaterSettings, sunLuma * lerp(0.4f, 1.0f, shadow), transmittance);
    color = lerp(getTurbidityFog(refractedDepth, linShallowColor, TESR_WaterVolume, sunLuma, color), linearize(TESR_WaterLODColor) * sunLuma, LODfade); // fade to full fog to hide LOD seam
    // color = getTurbidityFog(refractedDepth, linShallowColor, TESR_WaterVolume, sunLuma, color); // fade to full fog to hide LOD seam
    // color = lerp(getDiffuse(surfaceNormal, TESR_SunDirection.xyz, eyeDirection, distance, linHorizonColor, color), linShallowColor,LODfade);
    float3 scattering = getWaveScattering(surfaceNormal, eyeDirection, TESR_SunDirection.xyz, linSunColor.rgb * isDayTime, linShallowColor, shadow) * (1.0f - LODfade);
    color.rgb += scattering;
    float fresnel = getFresnelAmount(surfaceNormal, eyeDirection, reflection, TESR_WaveParams.w, color) * smoothstep(0, 0.2, refractedDepth.x); // reduce fresnel in low depths
    color.rgb = lerp(color.rgb, reflection.rgb, fresnel);
    color = getSunSpecular(surfaceNormal, TESR_SunDirection.xyz, eyeDirection, linSunColor.rgb * shadow, specRoughness, color);
    float3 pointLights = getPointLightsSpecular(surfaceNormal, IN.LTEXCOORD_0.xyz, eyeDirection, specRoughness);
    color.rgb += pointLights;
    color = lerp(getShoreFade(IN, waterDepth.x, TESR_WaterShorelineParams.x, TESR_WaterVolume.y, color), color, LODfade);

    color = delinearize(color); //delinearise
    
    // Standard fog.
    float fogStrength = pow(1 - saturate((FogParam.x - depth) / FogParam.y), FresnelRI.y);
    
    OUT.color_0.rgb = fogStrength * (FogColor.rgb - color.rgb) + color.rgb;
    OUT.color_0.a = lerp(color.a, 1, LODfade); // fade to full opacity to hide LOD seam
    // DebugView ([Shaders.Water.Main]): one term of the water lighting on its own.
    [branch]
    if (TESR_WaterLighting2.w > 0.5f)
        OUT.color_0 = float4(waterDebugView(TESR_WaterLighting2.w, shadow, transmittance, fresnel, scattering, specRoughness, pointLights, waterPath), 1.0f);

    return OUT;
};