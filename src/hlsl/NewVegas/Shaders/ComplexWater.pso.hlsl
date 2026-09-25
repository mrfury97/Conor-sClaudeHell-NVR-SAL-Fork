// Complex Water -- every water surface pixel shader, one source (WaterShaders::Templates).
//
//   WATER000.pso  outdoor water (autowater)            (defaults)
//   WATER017.pso  outdoor water, waded into            WATER_WADING 1
//   WATER001.pso  placed water                         WATER_PLACED 1
//   WATER018.pso  placed water, waded into             WATER_PLACED 1, WATER_WADING 1
//   WATER008.pso  interior water                       WATER_INTERIOR 1
//   WATER025.pso  interior water, waded into           WATER_INTERIOR 1, WATER_WADING 1
//   WATER033.pso  distant (LOD) water                  WATER_LOD 1
//   WATER016.pso  the surface seen from underwater     WATER_BELOW 1
//
// What a surface pixel does, in order:
//   waves      four turned layers of the wave texture, rain rings, the wading ripples
//   depth      the real depth of water the view passes through, from the scene depth behind it
//   refraction the scene behind the water, offset by the waves (more the deeper the water), never
//              pulling in anything in front of the water
//   water body the bed (with caustics) absorbed colour by colour over the path through the water,
//              and the water's own glow taking its place
//   surface    wave scattering, the reflection by Fresnel (blurred on rough water), the sun glint
//              and sun glitter, point-light glints, all under the sun shadow
//   edge       foam, and the shoreline fade
//   fog        the game's own distance fog
// Underwater looking up (WATER_BELOW): Snell's window -- the world above through a circle overhead,
// the underside of the surface a mirror of the water body outside it.
// Settings: [Shaders.Water.ComplexWater] (Includes/ComplexWater.hlsl), and each water type's own
// section (Default, Interiors, Placed) for its waves, reflectivity, refraction and shore movement.

#ifndef WATER_PLACED
    #define WATER_PLACED 0
#endif
#ifndef WATER_INTERIOR
    #define WATER_INTERIOR 0
#endif
#ifndef WATER_WADING
    #define WATER_WADING 0
#endif
#ifndef WATER_LOD
    #define WATER_LOD 0
#endif
#ifndef WATER_BELOW
    #define WATER_BELOW 0
#endif
#define WATER_SUNLIT (!WATER_INTERIOR)

// The engine's water constants, the same registers in every vanilla water shader.
float4 EyePos      : register(c1);
float4 ShallowColor : register(c2);
float4 DeepColor   : register(c3);
float4 FresnelRI   : register(c5);   // y: distance fog power
float4 BlendRadius : register(c6);   // w: wading ripple strength
float4 VarAmounts  : register(c8);   // w: how far the reflection lookup is offset with distance
float4 FogParam    : register(c9);   // x: fog end, y: fog range
float4 FogColor    : register(c10);
float4 SunColor    : register(c13);

// NVR's, bound by name: each water type's own section, and the world.
#if WATER_PLACED
float4 TESR_PlacedWaveParams            : register(c14);   // x: choppiness, y: wave width, z: wave speed, w: reflectivity
float4 TESR_PlacedWaterSettings         : register(c15);   // w: refraction strength
float4 TESR_PlacedWaterShorelineParams  : register(c16);   // x: shore movement
#define WAVE_PARAMS      TESR_PlacedWaveParams
#define WATER_SETTINGS   TESR_PlacedWaterSettings
#define SHORELINE_PARAMS TESR_PlacedWaterShorelineParams
#else
float4 TESR_WaveParams                  : register(c14);
float4 TESR_WaterSettings               : register(c15);   // x: water height, w: refraction strength
float4 TESR_WaterShorelineParams        : register(c16);
#define WAVE_PARAMS      TESR_WaveParams
#define WATER_SETTINGS   TESR_WaterSettings
#define SHORELINE_PARAMS TESR_WaterShorelineParams
#endif
float4 TESR_GameTime                    : register(c17);
float4 TESR_HorizonColor                : register(c18);
float4 TESR_SunDirection                : register(c19);
float4 TESR_SunAmount                   : register(c20);
float4 TESR_WetWorldData                : register(c21);   // x: rain
float4 TESR_CameraPosition              : register(c22);
float4 TESR_SkyLowColor                 : register(c23);
float4 TESR_SunColor                    : register(c24);
float4 TESR_ShadowLightPosition[12]     : register(c25);
float4 TESR_LightPosition[12]           : register(c37);
float4 TESR_LightColor[24]              : register(c49);

sampler2D ReflectionMap   : register(s0);
sampler2D RefractionMap   : register(s1);
sampler2D DisplacementMap : register(s3);
// Each MUST stay on ONE line (ShaderTextureValue::GetSamplerStateString reads to the end of it).
#if WATER_PLACED
sampler2D TESR_samplerWater : register(s5) < string ResourceName = "Water\watercalm_NRM.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; ADDRESSW = WRAP; MAGFILTER = ANISOTROPIC; MINFILTER = ANISOTROPIC; MIPFILTER = LINEAR; };
#else
sampler2D TESR_samplerWater : register(s5) < string ResourceName = "Water\water_NRM.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; ADDRESSW = WRAP; MAGFILTER = ANISOTROPIC; MINFILTER = ANISOTROPIC; MIPFILTER = LINEAR; };
#endif
sampler2D TESR_RippleSampler : register(s6) < string ResourceName = "Precipitations\ripples.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };

#define WATER_POINT_LIGHTS
#if WATER_SUNLIT
    #define WATER_SUN_SHADOWS
#endif
#include "Includes/Helpers.hlsl"
#include "Includes/Shadow.hlsl"
#include "Includes/ComplexWater.hlsl"

// The game's distance fog, over the finished colour (gamma space, as the game applies it).
float3 applyDistanceFog(float3 linearColor, float eyeDistance){
    float3 color = delinearize(float4(linearColor, 1.0f)).rgb;
    float fog = pow(1.0f - saturate((FogParam.x - eyeDistance) / FogParam.y), FresnelRI.y);
    return lerp(color, FogColor.rgb, fog);
}

PS_OUTPUT main(PS_INPUT IN) {
    PS_OUTPUT OUT;

    float3 surface = IN.LTEXCOORD_0.xyz;                  // camera-relative
    float3 eyeVector = EyePos.xyz - IN.LTEXCOORD_0.xyz;
    float3 eyeDirection = normalize(eyeVector);           // surface to camera
    float distance = length(eyeVector.xy);
    float eyeDistance = length(eyeVector);
    float4 waveParams = WAVE_PARAMS;

    // Light. Outdoors: the sun (gone at night and below the horizon) and the sky. Indoors: a dim
    // room light, no sun.
#if WATER_SUNLIT
    float3 sunDirection = TESR_SunDirection.xyz;
    float3 sunLight = linearize(WATER_BELOW ? TESR_SunColor : SunColor).rgb * smoothstep(0.0f, 0.5f, TESR_SunAmount.x) * saturate(sunDirection.z * 5.0f);
    float3 skyLight = linearize(TESR_HorizonColor).rgb;
#else
    float3 sunDirection = float3(0.0f, 0.0f, 1.0f);
    float3 sunLight = 0.0f;
    float3 skyLight = 0.4f;
#endif
    float brightness = TESR_WaterLighting.z;

    // Waves.
    float3 N = getWaveNormal(IN.LTEXCOORD_7, distance, waveParams);
#if !WATER_INTERIOR && !WATER_LOD
    N = getRainRipples(IN.LTEXCOORD_7, N, distance, TESR_WetWorldData.x);
#endif
#if WATER_WADING
    N = getWadingNormal(IN.LTEXCOORD_6.xy, BlendRadius.w, N);
#endif
    float roughness = getSpecularRoughness(N, distance);   // derivatives: top level

    // How far off the reflection and refraction lookups are pushed by the waves (the game's own
    // falloff with distance), times the water type's refraction strength for the refraction.
    float lookupOffset = (saturate(distance * 0.002f) * (-4.0f + VarAmounts.w)) + 4.0f;

    WaterDebug debug = (WaterDebug)0;
    debug.alpha = 1.0f;
    debug.shadow = 1.0f;
    debug.transmittance = 1.0f;
    debug.roughness = roughness;

#if WATER_BELOW
    // ---- Underwater, looking up at the surface --------------------------------------------------
    // Snell's window: light from above only reaches the eye within about 49 degrees of straight up;
    // outside that, the underside of the surface reflects everything (total internal reflection),
    // and shows the water body. Fresnel (Schlick, on the angle the light leaves the water at) grades
    // the edge of the window.
    float3 below = -N;                                     // the surface faces down, toward the eye
    float cosI = saturate(dot(eyeDirection, below));
    float sinT2 = 1.7689f * (1.0f - cosI * cosI);          // (1.33 sin I)^2
    float cosT = sqrt(saturate(1.0f - sinT2));
    float reflectance = sinT2 >= 1.0f ? 1.0f : WATER_F0 + (1.0f - WATER_F0) * pow(1.0f - cosT, 5.0f);
    reflectance = lerp(reflectance, 1.0f, smoothstep(0.85f, 1.0f, sinT2));

    // Above: the sky, its horizon to its low colour by how steeply up, the sun disc, and the world
    // above the water fading in as the camera nears the surface.
    float4 abovePos = flipToRefraction(getReflectionScreenPos(IN, below.xy * WATER_SETTINGS.w * 4.0f));
    float cameraDepth = WATER_SETTINGS.x - TESR_CameraPosition.z;
    float3 sky = lerp(skyLight, linearize(TESR_SkyLowColor).rgb, sqrt(saturate(-eyeDirection.z)));
    sky += sunLight * 5.0f * pow(shades(-eyeDirection, sunDirection), 20.0f);
    float3 above = sky + linearize(tex2Dproj(RefractionMap, abovePos)).rgb * smoothstep(200.0f, 0.0f, cameraDepth);

    // Outside the window: the water body, lit from above.
    float3 inside = getWaterColor(linearize(ShallowColor).rgb, linearize(DeepColor).rgb, cameraDepth)
                  * (luma(sunLight) + luma(skyLight) * 0.5f) * brightness;
    float3 color = lerp(above, inside, reflectance);
    debug.fresnel = reflectance;
    float alpha = 1.0f;

#elif WATER_LOD
    // ---- Distant water -----------------------------------------------------------------------
    // Too far for the bed to show: the water body's glow, its reflection by Fresnel, the sun glint.
    // The same colour the near water fades to with distance, so the two meet without a seam.
    float3 reflection = linearize(tex2Dproj(ReflectionMap, getReflectionScreenPos(IN, N.xy * lookupOffset))).rgb;
    float3 waterColor = getWaterColor(linearize(ShallowColor).rgb, linearize(DeepColor).rgb, 1e6f);
    float3 color = waterColor * (luma(sunLight) + luma(skyLight) * 0.5f) * brightness;
    float3 scattering = getWaveScattering(N, eyeDirection, sunDirection, sunLight, waterColor / max(max(waterColor.r, max(waterColor.g, waterColor.b)), 1e-6f));
    color += scattering;
    float fresnel = getFresnel(N, eyeDirection, waveParams.w);
    color = lerp(color, reflection, fresnel);
    color += getSunGlint(N, sunDirection, eyeDirection, roughness) * sunLight;
    debug.fresnel = fresnel;
    debug.scattering = scattering;
    float alpha = 1.0f;

#else
    // ---- The water surface ------------------------------------------------------------------
    float4 straightPos = getStraightScreenPos(IN);
    float2 straightPath = getWaterPath(straightPos, surface);   // x: path through the water, y: depth below, under this pixel

    // Refraction: the scene behind the water, pushed by the waves -- none at the waterline, where the
    // bed meets the surface, more as the water deepens -- and never onto anything in front of it.
    float4 reflectionPos = getReflectionScreenPos(IN, N.xy * lookupOffset * WATER_SETTINGS.w * saturate(straightPath.y / 150.0f));
    float4 refractionPos = getLeakFreeRefraction(flipToRefraction(reflectionPos), straightPos, surface);
    float2 path = getWaterPath(refractionPos, surface);         // the same, for the bed the pixel shows

    // What the surface reflects: the reflection map outdoors (blurred on rough water), the sky's
    // colour on placed water (which has no reflection map), the room's fog indoors.
#if WATER_INTERIOR
    float3 reflection = linearize(FogColor).rgb;
#elif WATER_PLACED
    float3 reflection = skyLight;
#else
    float3 reflection = getBlurredReflection(reflectionPos, N);
#endif

    // Sun shadow on the surface, and foam (top level: texture reads).
#if WATER_SUNLIT
    float shadow = getWaterSunShadow(surface);
#else
    float shadow = 1.0f;
#endif
    float foam = getFoamMask(IN.LTEXCOORD_7, straightPath.x, waveParams);

    // The water body. The bed, with caustics on it where the sun reaches it, loses its colours one
    // by one over the path through the water; the water's own glow, lit by the sun and the sky,
    // takes their place.
    float3 bed = linearize(tex2Dproj(RefractionMap, refractionPos)).rgb;
#if WATER_SUNLIT
    float caustics = getCaustics(getBedFromCamera(refractionPos), path.y, waveParams) * shadow;
    bed *= 1.0f + caustics * luma(sunLight);
#else
    float caustics = 0.0f;
#endif
    float3 transmittance = getTransmittance(path.x);
    float3 waterColor = getWaterColor(linearize(ShallowColor).rgb, linearize(DeepColor).rgb, path.y);
    float bodyLight = luma(sunLight) * lerp(0.4f, 1.0f, shadow) + luma(skyLight) * 0.5f;
    float3 color = bed * transmittance + waterColor * bodyLight * brightness * (1.0f - transmittance);

    // The surface: light through the wave crests, the reflection (none right at the waterline,
    // where the surface is too thin to hold a mirror), the sun, the point lights.
    float3 waterHue = waterColor / max(max(waterColor.r, max(waterColor.g, waterColor.b)), 1e-6f);
    float3 scattering = getWaveScattering(N, eyeDirection, sunDirection, sunLight * shadow, waterHue);
    color += scattering;
    float fresnel = getFresnel(N, eyeDirection, waveParams.w) * saturate(straightPath.y / 30.0f);
    color = lerp(color, reflection, fresnel);
    color += getSunGlint(N, sunDirection, eyeDirection, roughness) * sunLight * shadow;
    float3 pointLights = getPointLights(N, surface, eyeDirection, roughness);
    color += pointLights;

    // The edge: foam, white under the sun and the sky, over everything; then the shoreline fade.
    // Outdoor water beyond the LOD distance turns opaque, to meet the distant water without a seam.
    color = lerp(color, (sunLight * saturate(sunDirection.z) * shadow + skyLight * 0.6f) * 0.9f, foam);
    float alpha = getShoreAlpha(straightPath.y, SHORELINE_PARAMS.x, foam);
#if !WATER_INTERIOR && !WATER_PLACED
    alpha = lerp(alpha, 1.0f, smoothstep(4096.0f, 8192.0f, distance));
#endif

    debug.shadow = shadow;
    debug.transmittance = transmittance;
    debug.fresnel = fresnel;
    debug.scattering = scattering;
    debug.pointLights = pointLights;
    debug.path = path;
    debug.foam = foam;
    debug.alpha = alpha;
    debug.caustics = caustics;
#endif

    OUT.color_0 = float4(applyDistanceFog(color, eyeDistance), alpha);

    [branch]
    if (TESR_WaterLighting2.w > 0.5f)
        OUT.color_0 = float4(getWaterDebugView(TESR_WaterLighting2.w, debug), 1.0f);

    return OUT;
}
