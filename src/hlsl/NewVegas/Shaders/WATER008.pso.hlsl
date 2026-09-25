// interior autowater normal

float4 NotUsed0 : register(c0);
float4 EyePos : register(c1);
float4 ShallowColor : register(c2);
float4 DeepColor : register(c3);
float4 ReflectionColor : register(c4);
float4 FresnelRI : register(c5);
float4 BlendRadius : register(c6);
float4 NotUsed7 : register(c7);
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
float4 TESR_WaterShorelineParams : register(c20);
float4 TESR_CameraPosition : register(c21);
float4 TESR_DebugVar : register(c22);
float4 TESR_ShadowLightPosition[12] : register(c23);
float4 TESR_LightPosition[12] : register(c35);
float4 TESR_LightColor[24] : register(c47);

sampler2D ReflectionMap : register(s0);
sampler2D RefractionMap : register(s1);
sampler2D NormalMap : register(s2);
sampler2D DisplacementMap : register(s3);
sampler2D DepthMap : register(s4);

sampler2D TESR_samplerWater : register(s5) < string ResourceName = "Water\water_NRM.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; ADDRESSW = WRAP; MAGFILTER = ANISOTROPIC; MINFILTER = LINEAR; MIPFILTER = LINEAR; } ;

#define WATER_POINT_LIGHTS
#define WATER_SCENE_DEPTH
#include "Includes/Helpers.hlsl"
#include "Includes/Water.hlsl"


PS_OUTPUT main(PS_INPUT IN, float2 PixelPos : VPOS) {
    PS_OUTPUT OUT;

    float4 linShallowColor = linearize(ShallowColor); //linearise
    float4 linDeepColor = linearize(DeepColor); //linearise
    float4 linFogColor = linearize(FogColor); 

    float3 eyeVector = EyePos.xyz - IN.LTEXCOORD_0.xyz; // vector of camera position to point being shaded
    float3 eyeDirection = normalize(eyeVector);         // normalized eye to world vector (for lighting)
    float distance = length(eyeVector.xy);              // surface distance to eye
    float depth = length(eyeVector);                    // depth distance to eye
	float3 position = IN.WorldPosition.xyz;             // world position

	//float3 lightDir = normalize(float3(0.2, 0.2, 1));
	float sunLuma = 0.3;
	float interiorRefractionModifier = TESR_WaterSettings.w;		// reduce refraction because of the way interior depth is encoded
	float interiorDepthModifier = 0.5;			// reduce depth value for fog because of the way interior depth is encoded

    // calculate fog coeffs
    float4 screenPos = getScreenpos(IN);                // point coordinates in screen space for water surface
    float2 waterDepth = tex2Dproj(DepthMap, screenPos).xy;  // x= shallowfog, y = deepfog?
    float depthFog = saturate(invlerp(DepthFalloff.x, DepthFalloff.y, waterDepth.y));

    float2 fadedDepth = saturate(lerp(waterDepth, 1, invlerp(0, 4096, distance)));

    float3 surfaceNormal = getWaveTexture(IN, distance, TESR_WaveParams).xyz;

    float refractionCoeff = (waterDepth.y * depthFog) * ((saturate(distance * 0.002) * (-4 + VarAmounts.w)) + 4);
    float4 reflectionPos = getReflectionSamplePosition(IN, surfaceNormal, refractionCoeff * interiorRefractionModifier );
    //float4 reflection = linearize(tex2Dproj(ReflectionMap, reflectionPos));
    float4 refractionPos = reflectionPos;
    refractionPos.y = refractionPos.w - reflectionPos.y;
    refractionPos = getLeakFreeRefraction(refractionPos, screenPos, IN.LTEXCOORD_0.xyz);   // nothing above the water smeared into it
    float3 refractedDepth = tex2Dproj(DepthMap, refractionPos).rgb * interiorDepthModifier;

    // Water lighting (Water.hlsl): the glint roughness first -- it takes derivatives, so it stays at
    // the top level -- then the water body, the reflection and the point-light glints. No sun indoors.
    float shadow = 1.0f;
    float specRoughness = getSpecularRoughness(surfaceNormal, distance);
    float3 transmittance;
    float2 waterPath = getWaterPath(refractionPos, IN.LTEXCOORD_0.xyz);   // through the water to the bed, and straight down
    float2 straightPath = getWaterPath(screenPos, IN.LTEXCOORD_0.xyz);    // the same, right under this pixel: shore and foam
    float foam = getFoamMask(IN.LTEXCOORD_7, straightPath.x, TESR_WaveParams);
    float3 scattering = 0.0f;

    float4 color = linearize(tex2Dproj(RefractionMap, refractionPos));
    color = getWaterBody(color, refractedDepth, waterPath.x, linShallowColor, linDeepColor, 0.5, TESR_WaterSettings, 0.5f, transmittance);
   	color = getTurbidityFog(refractedDepth, linShallowColor, TESR_WaterVolume, sunLuma, color, getTurbidityScale(transmittance));
    float fresnel = getFresnelAmount(surfaceNormal, eyeDirection, linFogColor, TESR_WaveParams.w, color);
    color.rgb = lerp(color.rgb, linFogColor.rgb, fresnel);

    // Point-light glints: out-of-range lights skipped, the loop ended at the last light.
    float3 pointLights = getPointLightsSpecular(surfaceNormal, IN.LTEXCOORD_0.xyz, eyeDirection, specRoughness);
    color.rgb += pointLights;
    // Foam: white, in the room's dim light; it hides the reflection and glints under it.
    color.rgb = lerp(color.rgb, 0.35f, foam);

    color = getShoreFade(IN, waterDepth.x, TESR_WaterShorelineParams.x, TESR_WaterVolume.y, color, straightPath.y, foam);

    color = delinearize(color); //delinearise
	
	// Standard fog.
    float fogStrength = pow(1 - saturate((FogParam.x - depth) / FogParam.y), FresnelRI.y);
    
    OUT.color_0.rgb = fogStrength * (FogColor.rgb - color.rgb) + color.rgb;
    OUT.color_0.a = color.a;

    // DebugView ([Shaders.Water.Main]): one term of the water lighting on its own.
    [branch]
    if (TESR_WaterLighting2.w > 0.5f)
        OUT.color_0 = float4(waterDebugView(TESR_WaterLighting2.w, shadow, transmittance, fresnel, scattering, specRoughness, pointLights, waterPath, foam, color.a), 1.0f);

    return OUT;
};
