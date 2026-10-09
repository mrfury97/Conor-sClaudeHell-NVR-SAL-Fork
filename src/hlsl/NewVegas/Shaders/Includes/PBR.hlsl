// PBR calculations.
#if defined(__INTELLISENSE__)
    #include "Helpers.hlsl"
#endif

// Geometric specular AA
// http://www.jp.square-enix.com/tech/library/pdf/ImprovedGeometricSpecularAA.pdf
// https://www.jcgt.org/published/0010/02/02/paper.pdf
//
// Widens roughness where the NORMAL varies fast across a pixel's screen-space footprint --
// exactly what a high-frequency normal map (hair, being the sharpest example NVR ships) does.
// Without this, a GGX peak scales roughly as 1/roughness^4: two adjacent texels whose gloss
// differs by 10 points (0.85 vs 0.95) put out about an 80x difference in peak brightness, pure
// per-texel noise the material texture never intended anyone to resolve. In ObjectTemplate.hlsl
// that noise is the HAIR (ONLY_SPECULAR) pass's own alpha-blend weight, so it showed up as a
// splotchy, moth-eaten alpha pattern rather than as a shimmer in the highlight itself.
float SpecularAA(float3 normal, float roughness) {
    const float SIGMA2 = 0.15915494;
    const float KAPPA = 0.18;
    float3 dndu = ddx(normal);
    float3 dndv = ddy(normal);
    float variance = SIGMA2 * (dot(dndu, dndu) + dot(dndv, dndv));
    float kernel_roughness = min(KAPPA, variance);
    return sqrt(saturate(roughness * roughness + kernel_roughness));
}

// Fresnel
// Schlick approximation
float3 Fresnel(float3 f0, float3 f90, float cosine) {
    return f0 + (f90 - f0) * pow(1 - cosine, 5.f);
}

// Diffuse
// Lambert
float3 LambertianDiffuse(float3 albedo, float3 fresnel) {
    return (1 - fresnel) * albedo / PI;
}

float3 DisneyDiffuse(float3 albedo, float roughness, float NdotV, float NdotL, float LdotH) {
    const float linearRoughness = roughness * roughness;
    
    const float energyBias = lerp (0, 0.5 , linearRoughness);
    const float energyFactor = lerp (1.0, 1.0 / 1.51, linearRoughness);
    const float fd90 = energyBias + 2.0 * LdotH * LdotH * linearRoughness;
    const float3 f0 = float(1.0).xxx;
    const float lightScatter = Fresnel(f0, fd90, NdotL).r;
    const float viewScatter = Fresnel(f0, fd90, NdotV).r;

    return (albedo / PI) * lightScatter * viewScatter * energyFactor;
}

// Specular
// D (normal distribution function)
float GGX(float NdotH, float roughness) {
    float alpha = roughness * roughness;
    float a2 = pow(roughness, 4);
    float d = max((NdotH * a2 - NdotH) * NdotH + 1, 1e-5);
    return a2 / (PI * d * d);
}

// G1
float ShlickBeckmann(float NdotX, float roughness) {
    float k = pow(roughness + 1, 2) / 8.0;
    return NdotX/max(NdotX * (1 - k) + k, 0.00000001);
}

// Smith
float GeometryShadowing(float roughness, float NdotV, float NdotL) {
    return ShlickBeckmann(NdotV, roughness) * ShlickBeckmann(NdotL, roughness);
}

// F
float3 FresnelShlick(float3 reflectance, float3 halfway, float3 eyeDir) {
    return reflectance + (1 - reflectance) * pow(1 - shades(halfway, eyeDir), 5.0);
}

// BRDF
float3 BRDF(float roughness, float3 fresnel, float NdotV, float NdotL, float NdotH){
    float3 num = GGX(NdotH, roughness) * GeometryShadowing(roughness, NdotV, NdotL) * fresnel;
    float denom = 4.0 * NdotV * NdotL;
    return num/denom;
}

// --- Lighting space ---------------------------------------------------------------------------
// The game lights in GAMMA space: textures and light colours are gamma-encoded and multiplied
// as they are. Products survive that (sqrt(a) * sqrt(b) = sqrt(ab)), but the cosine, the BRDF
// and every sum of light do not: falloff toward the terminator comes out too soft and sums of
// lights too bright, which flattens everything a physically based model is meant to shape.
//
// With linear lighting on ([Shaders.PBR.Main] LinearLighting / [Shaders.Terrain.Main], set by
// the template through linearLighting), colours are decoded on the way in (x^2, the same
// encoding SkyAmbient.hlsl uses), all lighting is computed and summed linearly, and the result
// is encoded once at the end. Off, decode and encode do nothing and this is the old behaviour.
static bool linearLighting = false;
// The decode exponent while linearLighting is on: 2, except for authored (_rmaos) materials, which
// take 1 + [Shaders.PBR.Main] PBRLinearLighting (Object.hlsl, setupMaterial): 1 is the game's gamma
// space, 2 fully linear, and anything between lights partly linearly. Every pass of a mesh uses the
// same exponent, so the light-only + texture pass route stays exact at any value.
static float lightingGamma = 2.0f;

// One pow each: a separate exponent-2 fast path doubled the temporaries per light and pushed the
// six-light shaders (SLS2029/2030) past ps_3_0's 32 temp registers.
float3 decodeColor(float3 c) { return linearLighting ? pow(max(c, 0.0f), lightingGamma) : c; }
float3 encodeColor(float3 c) { return linearLighting ? pow(max(c, 0.0f), 1.0f / lightingGamma) : c; }

// Blinn-Phong exponent (vanilla glossiness, NiMaterialProperty::m_fShine or a land layer's
// specular exponent) to GGX roughness: alpha = sqrt(2 / (n + 2)), roughness = sqrt(alpha).
float shineToRoughness(float shine) {
    return pow(2.0f / (max(shine, 0.0f) + 2.0f), 0.25f);
}

// --- Direct lighting --------------------------------------------------------------------------
// All of these return light in the caller's units with PI folded in: a Lambertian surface lit
// head-on returns albedo * lightColor. specScale scales the specular lobe only (a material's
// specular mask times the specular strength setting).
//
// Diffuse is plain Lambert in every variant. It used to be (1 - F(L.H)) * Lambert where a
// specular lobe existed and plain Lambert where it did not, so the same mesh changed
// brightness at grazing light depending on which shader variant the game drew it with (and
// it switches with distance and light count).

float3 PBRDiffuse(float metallicness, float roughness, float3 albedo, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor) {
    const float NdotL = shades(normalize(normal), normalize(lightDir));
    return (1 - metallicness) * albedo * NdotL * lightColor;
}

// GGX specular for one light direction, times N.L, PI and the light.
float3 SpecularLobe(float3 f0, float roughness, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor) {
    const float3 halfway = normalize(eyeDir + lightDir);
    const float NdotL = max(shades(normal, lightDir), 0.00001);
    const float NdotV = max(shades(normal, eyeDir), 0.00001);
    const float NdotH = shades(normal, halfway);
    const float LdotH = shades(lightDir, halfway);
    const float3 fresnel = Fresnel(f0, (1.0).xxx, LdotH);
    return BRDF(roughness, fresnel, NdotV, NdotL, NdotH) * NdotL * lightColor * PI;
}

float3 PBRSpecular(float metallicness, float roughness, float3 albedo, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor, float specScale = 1.0f) {
    const float3 f0 = lerp(float(0.04).rrr, albedo, metallicness);
    return SpecularLobe(f0, roughness, normalize(normal), normalize(eyeDir), normalize(lightDir), lightColor) * specScale;
}

float3 PBR(float metallicness, float roughness, float3 albedo, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor, float specScale = 1.0f) {
    return PBRDiffuse(metallicness, roughness, albedo, normal, eyeDir, lightDir, lightColor)
         + PBRSpecular(metallicness, roughness, albedo, normal, eyeDir, lightDir, lightColor, specScale);
}

// The sun is a disc, not a point: its angular radius in radians (the full disc is about 0.53
// degrees; this is generous, as the old value was).
#define SUN_RADIUS 0.00918043

// Karis' representative point (Real Shading in Unreal Engine 4, 2013): the point of the sun's
// disc nearest the VIEW reflection, plus the normalisation that keeps the lobe widened to cover
// the disc from gaining energy. The old version reflected the LIGHT direction instead of the
// view, which never lands on the disc, so the sun was effectively a point light and the
// normalisation was missing.
float3 SunSpecularDir(float3 normal, float3 eyeDir, float3 lightDir, float roughness, out float normalization) {
    const float3 r = reflect(-eyeDir, normal);
    const float3 centerToRay = dot(lightDir, r) * r - lightDir;
    const float3 closest = lightDir + centerToRay * saturate(SUN_RADIUS / max(length(centerToRay), 1e-5f));

    const float alpha = roughness * roughness;
    const float alphaPrime = saturate(alpha + SUN_RADIUS * 0.5f);
    normalization = (alpha / alphaPrime) * (alpha / alphaPrime);

    return normalize(closest);
}

float3 PBRSunSpecular(float metallicness, float roughness, float3 albedo, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor, float specScale = 1.0f) {
    const float3 f0 = lerp(float(0.04).rrr, albedo, metallicness);
    normal = normalize(normal);
    eyeDir = normalize(eyeDir);
    lightDir = normalize(lightDir);

    float normalization;
    const float3 sunDir = SunSpecularDir(normal, eyeDir, lightDir, roughness, normalization);
    return SpecularLobe(f0, roughness, normal, eyeDir, sunDir, lightColor) * normalization * specScale;
}

float3 PBRSun(float metallicness, float roughness, float3 albedo, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor, float specScale = 1.0f) {
    return PBRDiffuse(metallicness, roughness, albedo, normal, eyeDir, lightDir, lightColor)
         + PBRSunSpecular(metallicness, roughness, albedo, normal, eyeDir, lightDir, lightColor, specScale);
}

// --- Community Shaders TruePBR ----------------------------------------------------------------
// The BRDF of Community Shaders' TruePBR (package/Shaders/Common/BRDF.hlsli, PBRMath.hlsli,
// PBR.hlsli and Shading.hlsli; GPL-3.0-or-later), for materials with authored _rmaos maps. The
// object shaders use it through Object.hlsl; the skin and terrain shaders still use the functions
// above.

// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float CS_D_GGX(float roughness, float NdotH) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * d * d);
}

// Approximation of the joint Smith term for GGX, with the 1 / (4 N.L N.V) folded in.
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float CS_Vis_SmithJointApprox(float roughness, float NdotV, float NdotL) {
    float a = roughness * roughness;
    float visV = NdotL * (NdotV * (1.0f + a) + a);
    float visL = NdotV * (NdotL * (1.0f + a) + a);
    return 0.5f / max(visV + visL, 1e-5f);
}

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
float3 CS_F_Schlick(float3 f0, float VdotH) {
    float fc = pow(1.0f - VdotH, 5.0f);
    return fc + (1.0f - fc) * f0;
}

// Split-sum environment BRDF (x scales F0, y is added).
// [Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II"]
float2 CS_EnvBRDF(float roughness, float NdotV) {
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

// Ambient occlusion with interreflections. [Jimenez et al. 2016, "Practical Realtime Strategies
// for Accurate Indirect Occlusion"]
float3 CS_MultiBounceAO(float3 baseColor, float ao) {
    float3 a = 2.0404f * baseColor - 0.3324f;
    float3 b = -4.7951f * baseColor + 0.6417f;
    float3 c = 2.7552f * baseColor + 0.6903f;
    return max(ao, ((ao * a + b) * ao + c) * ao);
}

// [Lagarde et al. 2014, "Moving Frostbite to Physically Based Rendering 3.0"]
float CS_SpecularOcclusion(float NdotV, float alpha, float occlusion) {
    return saturate(pow(abs(NdotV + occlusion), alpha) - 1.0f + occlusion);
}

// One light, CS's GetDirectLightInput: GGX x Smith x Schlick specular and Lambert diffuse scaled
// by (1 - F). In NVR's units, with PI folded in: a white Lambertian surface lit head-on returns
// the light. diffuseAlbedo is the base colour x (1 - metalness).
void CS_DirectLight(float3 N, float3 V, float3 L, float3 light, float roughness, float3 f0, float3 diffuseAlbedo, out float3 diffuse, out float3 specular) {
    float3 H = normalize(V + L);
    float NdotL = clamp(dot(N, L), 1e-4f, 1.0f);
    float NdotV = saturate(abs(dot(N, V)) + 1e-4f);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 F = CS_F_Schlick(f0, VdotH);
    float3 Fr = CS_D_GGX(roughness, NdotH) * CS_Vis_SmithJointApprox(roughness, NdotV, NdotL) * F;
    diffuse = diffuseAlbedo * (1.0f - F) * NdotL * light;
    specular = Fr * (PI * NdotL) * light;
}

// --- OpenPBR Surface --------------------------------------------------------------------------
// The base substrate of the OpenPBR Surface specification (Academy Software Foundation,
// https://github.com/AcademySoftwareFoundation/OpenPBR), for materials with authored _rmaos maps:
//   R  specular_roughness   (GGX, alpha = r^2)
//   G  base_metalness       (statistical mix of the dielectric base and the metal)
//   B  ambient occlusion    (not an OpenPBR parameter: a renderer term)
//   A  specular_weight      (default 1: the dielectric reflectivity of specular_ior 1.5, F0 0.04)
// Everything else at the specification's defaults: base_weight 1, specular_color white,
// specular_ior 1.5, base_diffuse_roughness 0 (Lambertian), no anisotropy, coat, fuzz, thin film,
// transmission or subsurface. GGX, Smith masking and the split-sum environment term are the CS_
// functions above.

#define OPENPBR_SPECULAR_IOR 1.5f

// The IOR ratio after specular_weight modulates the reflectivity at normal incidence
// ("Base Substrate", modulated_ior): F_s = ((1 - eta) / (1 + eta))^2, eps = sqrt(weight F_s),
// eta' = (1 + eps) / (1 - eps). A weight of 0 gives eta' = 1: no reflection.
float OpenPBR_ModulatedIOR(float specularWeight) {
    float Fs = (OPENPBR_SPECULAR_IOR - 1.0f) / (OPENPBR_SPECULAR_IOR + 1.0f);
    Fs *= Fs;
    float eps = sqrt(saturate(specularWeight * Fs));
    return (1.0f + eps) / max(1.0f - eps, 1e-4f);
}

// Exact unpolarised dielectric Fresnel reflectance for an IOR ratio eta >= 1 at incidence cosine c.
float OpenPBR_FresnelDielectric(float c, float eta) {
    float g2 = eta * eta - 1.0f + c * c;
    float g = sqrt(max(g2, 0.0f));
    float a = (g - c) / max(g + c, 1e-5f);
    float b = (c * (g + c) - 1.0f) / (c * (g - c) + 1.0f);
    return 0.5f * a * a * (1.0f + b * b);
}

// The dielectric's reflectivity at normal incidence, for the split-sum terms.
float OpenPBR_DielectricF0(float eta) {
    float f = (eta - 1.0f) / (eta + 1.0f);
    return f * f;
}

// Multiple scattering between microfacets, which the single-scattering lobe loses (the
// specification asks implementations to account for it): 1 + F0 (1 / E_ss - 1), with E_ss the
// lobe's directional albedo for a white F0, A + B of the split-sum fit.
float3 OpenPBR_EnergyCompensation(float3 f0, float2 envBRDF) {
    return 1.0f + f0 * (1.0f / max(envBRDF.x + envBRDF.y, 1e-3f) - 1.0f);
}

// One light on the base substrate. In NVR's units, with PI folded in: a white Lambertian surface
// lit head-on returns the light. albedo is base_color in the lighting space (decoded); in a
// light-only pass it is 1 for the diffuse (the texture pass multiplies it in) while metalF0 keeps
// the real colour for the metal's Fresnel.
//   specular: (1 - M) f_dielectric + M f_metal, f_metal = specular_weight x Schlick(base_color)
//             (the F82-tint model at its default white edge tint reduces to Schlick)
//   diffuse:  (1 - M) base_color (1 - E_dielectric(view)) / PI, the albedo-scaling form of the
//             glossy-diffuse slab with a Lambertian base
// NdotLd is N.L for the diffuse (the lamp's centre); L the direction the highlight is lit from:
// the same for a point light, a sphere light's representative point otherwise (Object.hlsl
// directLight, which also applies the sphere light's normalization).
void OpenPBR_DirectLight(float3 N, float3 V, float NdotLd, float3 L, float3 light, float roughness, float metalness, float specularWeight, float eta,
                         float3 albedo, float3 metalF0, out float3 diffuse, out float3 specular) {
    float3 H = normalize(V + L);
    float NdotL = clamp(NdotLd, 1e-4f, 1.0f);
    float NdotLs = clamp(dot(N, L), 1e-4f, 1.0f);
    float NdotV = saturate(abs(dot(N, V)) + 1e-4f);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float DV = CS_D_GGX(roughness, NdotH) * CS_Vis_SmithJointApprox(roughness, NdotV, NdotLs);
    float2 envBRDF = CS_EnvBRDF(roughness, NdotV);
    float dielectricF0 = OpenPBR_DielectricF0(eta);

    float3 Fmetal = specularWeight * CS_F_Schlick(metalF0, VdotH);
    float  Fdielectric = OpenPBR_FresnelDielectric(VdotH, eta);
    float3 F = lerp(Fdielectric.xxx, Fmetal, metalness);
    float3 f0 = lerp(dielectricF0.xxx, specularWeight * metalF0, metalness);

    specular = DV * F * OpenPBR_EnergyCompensation(f0, envBRDF) * (PI * NdotLs) * light;

    float Edielectric = dielectricF0 * envBRDF.x + envBRDF.y;
    diffuse = albedo * ((1.0f - metalness) * (1.0f - Edielectric) * NdotL) * light;
}

// The environment lobes of the same substrate (split sum), for the ambient: x the specular lobe
// weight (multiplies the environment radiance), and the share the diffuse ambient keeps.
void OpenPBR_EnvironmentWeights(float NdotV, float roughness, float metalness, float specularWeight, float eta, float3 metalF0,
                                out float3 specularWeightOut, out float diffuseShare) {
    float2 envBRDF = CS_EnvBRDF(roughness, NdotV);
    float dielectricF0 = OpenPBR_DielectricF0(eta);
    float  Edielectric = dielectricF0 * envBRDF.x + envBRDF.y;
    float3 Emetal = specularWeight * (metalF0 * envBRDF.x + envBRDF.y);
    float3 f0 = lerp(dielectricF0.xxx, specularWeight * metalF0, metalness);
    specularWeightOut = lerp(Edielectric.xxx, Emetal, metalness) * OpenPBR_EnergyCompensation(f0, envBRDF);
    diffuseShare = (1.0f - metalness) * (1.0f - Edielectric);
}
